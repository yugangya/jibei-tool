"""Dependency-free Ollama and OpenAI-compatible vision chat client."""

import base64
import json
import re
import socket
import threading
from http.client import HTTPException
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


class OllamaError(RuntimeError):
    """Raised when a model service request cannot be completed."""


class OllamaCancelled(OllamaError):
    """Raised when a request is stopped by the user."""


class _PublicTextFilter:
    """Remove tagged private reasoning, including tags split across chunks."""

    _tags = ("think", "thinking", "analysis", "reasoning")
    _tag_pattern = re.compile(r"<\s*(/?)\s*(think|thinking|analysis|reasoning)\s*>", re.IGNORECASE)
    _tag_strings = tuple("<%s%s>" % (slash, tag) for tag in _tags for slash in ("", "/"))

    def __init__(self):
        self._pending = ""
        self._hidden = []

    def feed(self, text):
        remaining, self._pending = self._pending + text, ""
        visible = []
        while remaining:
            pos = remaining.find("<")
            if pos < 0:
                if not self._hidden:
                    visible.append(remaining)
                break
            if pos and not self._hidden:
                visible.append(remaining[:pos])
            remaining = remaining[pos:]
            match = self._tag_pattern.match(remaining)
            if match:
                closing, tag = match.group(1), match.group(2).lower()
                if not closing:
                    self._hidden.append(tag)
                elif tag in self._hidden:
                    index = len(self._hidden) - 1 - self._hidden[::-1].index(tag)
                    self._hidden = self._hidden[:index]
                remaining = remaining[match.end():]
                continue
            lower = re.sub(r"\s+", "", remaining.lower())
            if any(tag.startswith(lower) for tag in self._tag_strings):
                self._pending = remaining
                break
            if not self._hidden:
                visible.append("<")
            remaining = remaining[1:]
        return "".join(visible)

    def finish(self):
        # A partial reasoning tag or unfinished reasoning block is never public.
        self._pending = ""


class OllamaClient:
    def __init__(self, base_url="http://127.0.0.1:11434", timeout=30, mode="native", api_key="ollama"):
        self.mode = mode or "native"
        self.base_url = (base_url or "").rstrip("/")
        self.timeout = timeout
        self.api_key = api_key or "ollama"
        self._response_lock = threading.Lock()
        self._active_response = None
        self._cancel_requested = threading.Event()
        if self.mode == "native" and self.base_url.endswith("/v1"):
            self.base_url = self.base_url[:-3].rstrip("/")
        elif self.mode == "openai" and not self.base_url.endswith("/v1"):
            self.base_url += "/v1"

    def _make_request(self, path, payload=None, method=None, streaming=False):
        body = None
        headers = {"Accept": "text/event-stream, application/x-ndjson, application/json" if streaming else "application/json"}
        if self.mode == "openai":
            headers["Authorization"] = "Bearer " + self.api_key
        if payload is not None:
            body = json.dumps(payload).encode("utf-8")
            headers["Content-Type"] = "application/json"
        return Request(self.base_url + path, data=body, headers=headers, method=method or ("POST" if body else "GET"))

    @staticmethod
    def _http_error(exc):
        detail = exc.read().decode("utf-8", errors="replace")[:4000]
        return OllamaError("服务请求失败（HTTP %s）：%s" % (exc.code, detail))

    def _request(self, path, payload=None, timeout=None, method=None):
        request = self._make_request(path, payload, method)
        try:
            with urlopen(request, timeout=timeout or self.timeout) as response:
                raw = response.read()
        except HTTPError as exc:
            raise self._http_error(exc) from exc
        except (URLError, OSError, TimeoutError) as exc:
            raise OllamaError("无法连接到模型服务 %s：%s" % (self.base_url, exc)) from exc
        try:
            return json.loads(raw.decode("utf-8"))
        except (ValueError, UnicodeDecodeError) as exc:
            raise OllamaError("模型服务返回的数据格式无效") from exc

    def list_models(self):
        result = self._request("/api/tags" if self.mode == "native" else "/models")
        models = result.get("models", []) if self.mode == "native" else result.get("data", [])
        key = "name" if self.mode == "native" else "id"
        names = []
        for item in models:
            name = str(item.get(key, "")).strip()
            is_remote = bool(item.get("remote_model") or item.get("remote_host"))
            capabilities = item.get("capabilities") or []
            is_vision = "vision" in capabilities if capabilities else True
            if name and (self.mode != "native" or not is_remote) and is_vision:
                names.append(name)
        return names

    def test_chat(self, model, image_bytes=None, image_mime="image/png"):
        result = self.chat(model, "请用简体中文回复：如果收到图片，请说明你能看到图片；如果没有收到图片，请明确说明。", image_bytes, image_mime)
        if self.mode == "native":
            return str(result.get("message", {}).get("content", "")).strip()
        choices = result.get("choices", [])
        message = choices[0].get("message", {}) if choices else {}
        return str(message.get("content", "")).strip()

    def _chat_payload(self, model, prompt, image_bytes, image_mime, stream, history=None):
        model = (model or "").strip()
        if not model:
            raise OllamaError("请先选择一个模型")
        prompt = (prompt or "").strip()
        if not prompt:
            raise OllamaError("请输入问题")
        message = {"role": "user", "content": prompt}
        if image_bytes:
            if isinstance(image_bytes, bytearray):
                image_bytes = bytes(image_bytes)
            if not isinstance(image_bytes, bytes) or not image_bytes:
                raise OllamaError("图片数据为空，无法发送")
            image_mime = (image_mime or "image/png").lower().split(";", 1)[0].strip()
            if image_mime not in ("image/png", "image/jpeg", "image/webp", "image/gif"):
                raise OllamaError("当前图片格式不受视觉模型支持，请转换为 PNG 或 JPEG")
            encoded = base64.b64encode(image_bytes).decode("ascii")
            if self.mode == "native":
                message["images"] = [encoded]
            else:
                message["content"] = [
                    {"type": "text", "text": prompt},
                    {"type": "image_url", "image_url": {"url": "data:%s;base64,%s" % (image_mime, encoded)}},
                ]
        messages = []
        for previous in history or []:
            if not isinstance(previous, dict):
                continue
            role, content = previous.get("role"), previous.get("content")
            if role in ("system", "user", "assistant") and isinstance(content, str) and content.strip():
                messages.append({"role": role, "content": content})
        messages.append(message)
        return {"model": model, "messages": messages, "stream": stream}

    def chat(self, model, prompt, image_bytes=None, image_mime="image/png"):
        payload = self._chat_payload(model, prompt, image_bytes, image_mime, False)
        return self._request("/api/chat" if self.mode == "native" else "/chat/completions", payload, timeout=max(self.timeout, 120))

    def cancel(self):
        """Interrupt an active stream; safe to call from the GUI thread."""
        self._cancel_requested.set()
        with self._response_lock:
            response = self._active_response
        if response is None:
            return
        # HTTPResponse.close() can wait for its buffered reader's lock. On
        # Windows even shutdown() alone may leave a concurrent recv blocked.
        # Close the socket handle directly; the worker owns response cleanup.
        connection = getattr(getattr(getattr(response, "fp", None), "raw", None), "_sock", None)
        if connection is None:
            return
        try:
            connection.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        try:
            handle = connection.detach()
            if handle != -1:
                socket.close(handle)
        except (OSError, ValueError):
            pass

    def _check_cancelled(self, cancel_event):
        if self._cancel_requested.is_set() or (cancel_event is not None and cancel_event.is_set()):
            raise OllamaCancelled("已停止生成。")

    def _stream_objects(self, response, cancel_event):
        """Read JSONL, SSE (including multi-line data), or ordinary JSON."""
        sse_data = []
        json_parts = []
        is_sse = False
        while True:
            self._check_cancelled(cancel_event)
            try:
                raw_line = response.readline()
            except (OSError, ValueError, AttributeError, HTTPException) as exc:
                self._check_cancelled(cancel_event)
                raise OllamaError("读取模型响应失败，连接可能已中断：%s" % exc) from exc
            self._check_cancelled(cancel_event)
            if not raw_line:
                break
            try:
                line = raw_line.decode("utf-8-sig").rstrip("\r\n")
            except UnicodeDecodeError as exc:
                raise OllamaError("模型服务返回的数据编码无效。") from exc
            if line.startswith(":"):
                is_sse = True
                continue
            if line.startswith(("event:", "id:", "retry:")):
                is_sse = True
                continue
            if line.startswith("data:"):
                is_sse = True
                data = line[5:]
                sse_data.append(data[1:] if data.startswith(" ") else data)
                continue
            if is_sse:
                if not line and sse_data:
                    data, sse_data = "\n".join(sse_data), []
                    if data.strip() == "[DONE]":
                        yield None
                        return
                    yield self._decode_stream_object(data)
                elif line:
                    raise OllamaError("模型服务返回的流式事件格式无效。")
                continue
            if not line.strip():
                continue
            json_parts.append(line)
            try:
                obj = json.loads("\n".join(json_parts))
            except ValueError:
                continue
            json_parts = []
            yield self._validate_stream_object(obj)
        if sse_data:
            data = "\n".join(sse_data)
            if data.strip() == "[DONE]":
                yield None
            else:
                yield self._decode_stream_object(data)
        if json_parts:
            raise OllamaError("模型服务返回的数据格式无效或响应不完整。")

    @staticmethod
    def _validate_stream_object(obj):
        if not isinstance(obj, dict):
            raise OllamaError("模型服务返回的数据格式无效。")
        error = obj.get("error")
        if error:
            detail = error.get("message", "请求失败") if isinstance(error, dict) else str(error)
            raise OllamaError("模型服务返回错误：%s" % detail)
        return obj

    def _decode_stream_object(self, data):
        try:
            return self._validate_stream_object(json.loads(data))
        except ValueError as exc:
            raise OllamaError("模型服务返回的流式事件数据格式无效。") from exc

    @staticmethod
    def _public_content(message):
        if not isinstance(message, dict) or message.get("channel") in ("analysis", "reasoning", "thinking"):
            return ""
        content = message.get("content", "")
        if isinstance(content, str):
            return content
        if isinstance(content, list):
            return "".join(item.get("text", "") for item in content
                           if isinstance(item, dict) and item.get("type") in ("text", "output_text") and isinstance(item.get("text"), str))
        return ""

    def chat_stream(self, model, prompt, image_bytes=None, image_mime="image/png",
                    on_delta=None, on_status=None, cancel_event=None, history=None):
        """Stream public answer text and truthful request stages on this thread.

        Callbacks are called on the requesting worker thread. A Qt caller should
        emit queued signals from them instead of updating widgets directly.
        Providers' thinking/reasoning fields are deliberately never forwarded.
        """
        payload = self._chat_payload(model, prompt, image_bytes, image_mime, True, history)
        request = self._make_request("/api/chat" if self.mode == "native" else "/chat/completions", payload, streaming=True)
        self._check_cancelled(cancel_event)
        status = on_status or (lambda text: None)
        status("正在连接模型服务并发送问题…")
        self._check_cancelled(cancel_event)
        chunks, text_filter = [], _PublicTextFilter()
        receiving = complete = False
        try:
            with urlopen(request, timeout=max(self.timeout, 120)) as response:
                with self._response_lock:
                    self._active_response = response
                self._check_cancelled(cancel_event)
                status("已连接，正在等待模型返回…")
                for obj in self._stream_objects(response, cancel_event):
                    if obj is None:
                        complete = True
                        break
                    if self.mode == "native":
                        text = self._public_content(obj.get("message", {}))
                        done = obj.get("done") is True
                    else:
                        choices = obj.get("choices") or []
                        choice = choices[0] if choices else {}
                        if not isinstance(choice, dict):
                            raise OllamaError("模型服务返回的回答格式无效。")
                        text = "" if choice.get("channel") in ("analysis", "reasoning", "thinking") else self._public_content(choice.get("delta") or choice.get("message") or {})
                        done = choice.get("finish_reason") is not None or bool(choice.get("message"))
                    public_text = text_filter.feed(text)
                    if public_text:
                        if not receiving:
                            receiving = True
                            status("正在接收模型回答…")
                        chunks.append(public_text)
                        if on_delta is not None:
                            on_delta(public_text)
                    if done:
                        complete = True
                        break
                text_filter.finish()
                self._check_cancelled(cancel_event)
                if not complete:
                    raise OllamaError("模型响应未完整结束，当前回答可能不完整，请重试。")
        except HTTPError as exc:
            self._check_cancelled(cancel_event)
            raise self._http_error(exc) from exc
        except (URLError, OSError, TimeoutError, ValueError, HTTPException) as exc:
            self._check_cancelled(cancel_event)
            raise OllamaError("模型请求中断或无法连接到服务 %s：%s" % (self.base_url, exc)) from exc
        finally:
            with self._response_lock:
                self._active_response = None
        reply = "".join(chunks).strip()
        if not reply:
            raise OllamaError("模型没有返回可显示的回答，请确认所选模型支持当前请求。")
        status("模型回答已完成。")
        return reply
