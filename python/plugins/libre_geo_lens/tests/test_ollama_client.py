"""Protocol tests without QGIS, model downloads, credentials, or external requests."""

import base64
import importlib.util
import io
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import threading
import time
import unittest
from unittest.mock import patch
from urllib.error import HTTPError


_spec = importlib.util.spec_from_file_location("libre_geo_lens_client", Path(__file__).parents[1] / "ollama_client.py")
client_module = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(client_module)
OllamaClient = client_module.OllamaClient
OllamaError = client_module.OllamaError
OllamaCancelled = client_module.OllamaCancelled


def encoded_json(value):
    return json.dumps(value, ensure_ascii=False).encode("utf-8")


class StreamProtocolTests(unittest.TestCase):
    def stream(self, raw, mode="native", **kwargs):
        deltas, stages = [], []
        client = OllamaClient(mode=mode)
        with patch.object(client_module, "urlopen", return_value=io.BytesIO(raw)) as opened:
            reply = client.chat_stream("vision-model", "请分析图片", on_delta=deltas.append,
                                       on_status=stages.append, **kwargs)
        return reply, deltas, stages, opened.call_args.args[0]

    def test_native_jsonl_streams_chinese_and_hides_private_reasoning(self):
        chunks = [
            {"message": {"thinking": "private", "content": "<thi"}, "done": False},
            {"message": {"content": "nk>hidden<analysis>nested</analysis></thi"}, "done": False},
            {"message": {"content": "nk >植被"}, "done": False},
            {"message": {"content": "恢复良好。"}, "done": True},
        ]
        raw = b"\n".join(encoded_json(chunk) for chunk in chunks)
        reply, deltas, stages, request = self.stream(raw)
        self.assertEqual(reply, "植被恢复良好。")
        self.assertEqual(deltas, ["植被", "恢复良好。"])
        self.assertEqual(len(stages), 4)
        self.assertIn("已完成", stages[-1])
        self.assertTrue(json.loads(request.data)["stream"])

    def test_openai_sse_comments_multiline_and_done(self):
        raw = (": heartbeat\r\n\r\nevent: message\r\n"
               'data: {"choices":[{"delta":\r\n'
               'data: {"reasoning_content":"private","content":"施工"}}]}\r\n\r\n'
               'data: {"choices":[{"delta":{"content":[{"type":"text","text":"扰动"},'
               '{"type":"reasoning","text":"private"},{"type":"output_text","text":"可见。"}]}}]}\r\n\r\n'
               "data: [DONE]\r\n\r\n").encode("utf-8")
        reply, deltas, _, request = self.stream(raw, mode="openai")
        self.assertEqual(reply, "施工扰动可见。")
        self.assertEqual(deltas, ["施工", "扰动可见。"])
        self.assertEqual(request.full_url, "http://127.0.0.1:11434/v1/chat/completions")

    def test_sse_final_event_without_blank_line(self):
        raw = b"data: " + encoded_json({"choices": [{"delta": {"content": "中文回答"}, "finish_reason": "stop"}]})
        self.assertEqual(self.stream(raw, mode="openai")[0], "中文回答")

    def test_normal_json_fallback_and_image_history(self):
        response = {"choices": [{"message": {"content": "图片已收到"}, "finish_reason": "stop"}]}
        history = [{"role": "user", "content": "前一问题"}, {"role": "assistant", "content": "前一回答"},
                   {"role": "tool", "content": "不得发送"}, {"role": "assistant", "content": ["invalid"]}]
        reply, _, _, request = self.stream(json.dumps(response, ensure_ascii=False, indent=2).encode("utf-8"),
                                          mode="openai", image_bytes=b"PNG-data", history=history)
        messages = json.loads(request.data)["messages"]
        self.assertEqual(reply, "图片已收到")
        self.assertEqual(len(messages), 3)
        self.assertEqual(messages[1], history[1])
        self.assertEqual(messages[-1]["content"][1]["image_url"]["url"],
                         "data:image/png;base64," + base64.b64encode(b"PNG-data").decode("ascii"))

    def test_analysis_channels_never_reach_public_answer(self):
        chunks = [{"choices": [{"delta": {"channel": "analysis", "content": "private"}}]},
                  {"choices": [{"channel": "analysis", "delta": {"content": "private"}}]},
                  {"choices": [{"delta": {"content": "公开回答"}, "finish_reason": "stop"}]}]
        raw = b"\n\n".join(b"data: " + encoded_json(chunk) for chunk in chunks)
        reply, deltas, _, _ = self.stream(raw, mode="openai")
        self.assertEqual(reply, "公开回答")
        self.assertEqual(deltas, ["公开回答"])

    def test_legacy_chat_retains_nonstreaming_native_image_format(self):
        client = OllamaClient()
        with patch.object(client_module, "urlopen", return_value=io.BytesIO(encoded_json({"message": {"content": "answer"}}))) as opened:
            response = client.chat("model", "问题", image_bytes=b"image")
        payload = json.loads(opened.call_args.args[0].data)
        self.assertFalse(payload["stream"])
        self.assertEqual(payload["messages"][0]["images"], [base64.b64encode(b"image").decode("ascii")])
        self.assertEqual(response["message"]["content"], "answer")

    def test_provider_error_and_malformed_or_invalid_encoding(self):
        for raw in (encoded_json({"error": {"message": "模型不可用"}}),
                    b'data: {"choices":broken}\n\n', b'{"incomplete":', b'\xff\n'):
            with self.subTest(raw=raw), self.assertRaises(OllamaError):
                self.stream(raw)

    def test_http_error_is_chinese_and_does_not_include_api_key(self):
        error = HTTPError("https://example.invalid/v1/chat/completions", 401, "Unauthorized", {}, io.BytesIO(b'{"error":"unauthorized"}'))
        client = OllamaClient("https://example.invalid/v1", mode="openai", api_key="secret-token")
        with patch.object(client_module, "urlopen", side_effect=error), self.assertRaises(OllamaError) as caught:
            client.chat_stream("model", "问题")
        self.assertIn("服务请求失败", str(caught.exception))
        self.assertIn("401", str(caught.exception))
        self.assertNotIn("secret-token", str(caught.exception))

    def test_cancel_before_connect_and_after_first_chunk(self):
        event = threading.Event()
        event.set()
        client = OllamaClient()
        with patch.object(client_module, "urlopen") as opened, self.assertRaises(OllamaCancelled):
            client.chat_stream("model", "问题", cancel_event=event)
        opened.assert_not_called()
        raw = encoded_json({"message": {"content": "第一段"}, "done": False}) + b"\n" + encoded_json({"message": {"content": "第二段"}, "done": True})
        client, deltas, stages = OllamaClient(), [], []

        def receive(text):
            deltas.append(text)
            client.cancel()

        with patch.object(client_module, "urlopen", return_value=io.BytesIO(raw)), self.assertRaises(OllamaCancelled):
            client.chat_stream("model", "问题", on_delta=receive, on_status=stages.append)
        self.assertEqual(deltas, ["第一段"])
        self.assertNotIn("模型回答已完成。", stages)

    def test_private_only_response_is_not_a_success(self):
        with self.assertRaises(OllamaError):
            self.stream(encoded_json({"message": {"thinking": "private", "content": "<think>private</think>"}, "done": True}))

    def test_nested_same_name_reasoning_tags_do_not_leak(self):
        reply, deltas, _, _ = self.stream(encoded_json({"message": {"content": "<think>a<think>b</think>c</think>公开正文"}, "done": True}))
        self.assertEqual(reply, "公开正文")
        self.assertEqual(deltas, ["公开正文"])

    def test_truncated_stream_does_not_report_success(self):
        for mode, raw in (("native", encoded_json({"message": {"content": "未完成"}, "done": False})),
                          ("openai", b"data: " + encoded_json({"choices": [{"delta": {"content": "未完成"}}]}) + b"\n\n")):
            with self.subTest(mode=mode), self.assertRaises(OllamaError) as caught:
                self.stream(raw, mode=mode)
            self.assertIn("未完整结束", str(caught.exception))

    def test_cancelling_blocked_windows_socket_does_not_block_gui_caller(self):
        chunk_seen, release_server, finished = threading.Event(), threading.Event(), threading.Event()
        outcomes = []

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_POST(self):
                self.rfile.read(int(self.headers["Content-Length"]))
                self.send_response(200)
                self.send_header("Content-Type", "application/x-ndjson")
                self.end_headers()
                self.wfile.write(encoded_json({"message": {"content": "第一段"}, "done": False}) + b"\n")
                self.wfile.flush()
                release_server.wait(5)

        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        server.daemon_threads = True
        threading.Thread(target=server.serve_forever, daemon=True).start()
        client = OllamaClient("http://127.0.0.1:%s" % server.server_address[1])

        def request():
            try:
                client.chat_stream("model", "问题", on_delta=lambda text: chunk_seen.set())
                outcomes.append("completed")
            except OllamaCancelled:
                outcomes.append("cancelled")
            except Exception as exc:
                outcomes.append(repr(exc))
            finally:
                finished.set()

        worker = threading.Thread(target=request, daemon=True)
        worker.start()
        try:
            self.assertTrue(chunk_seen.wait(4), "Initial streamed text never arrived")
            time.sleep(0.05)  # Let the worker enter the next blocked readline.
            start = time.monotonic()
            client.cancel()
            self.assertLess(time.monotonic() - start, 0.4, "The GUI caller waited for the buffered read lock")
            self.assertTrue(finished.wait(1), "The blocked socket read was not interrupted")
            self.assertEqual(outcomes, ["cancelled"])
        finally:
            release_server.set()
            client.cancel()
            worker.join(1)
            server.shutdown()
            server.server_close()


if __name__ == "__main__":
    unittest.main()
