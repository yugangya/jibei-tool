"""Independent Jianghe conversation UI for map and image questions."""

import os
import threading
import time

from qgis.core import QgsApplication, QgsSettings
from qgis.PyQt.QtCore import (
    QByteArray, QBuffer, QEvent, QIODevice, QObject, QSize, QThread,
    QTimer, Qt, pyqtSignal, pyqtSlot,
)
from qgis.PyQt.QtGui import QAction, QColor, QIcon, QImage, QPainter, QPen, QPixmap, QTextCursor
from qgis.PyQt.QtWidgets import (
    QDockWidget, QFileDialog, QFrame, QHBoxLayout, QLabel, QPlainTextEdit,
    QProgressBar, QPushButton, QScrollArea, QSizePolicy, QTextBrowser,
    QToolButton, QVBoxLayout, QWidget,
)

from .ollama_client import OllamaCancelled, OllamaClient, OllamaError
from .settings_dialog import ModelSettingsDialog
from .ui_theme import apply_assistant_theme


class _OllamaWorker(QObject):
    models_ready = pyqtSignal(object)
    reply_ready = pyqtSignal(str)
    delta_ready = pyqtSignal(str)
    status_ready = pyqtSignal(str)
    failed = pyqtSignal(str)
    cancelled = pyqtSignal()
    finished = pyqtSignal()

    def __init__(self, config, operation, prompt="", image_bytes=None,
                 image_mime="image/png", history=None):
        super().__init__()
        self.config = dict(config)
        self.operation, self.prompt = operation, prompt
        self.image_bytes, self.image_mime = image_bytes, image_mime
        self.history = list(history or [])
        self.cancel_event = threading.Event()
        self.client = None

    def cancel(self):
        self.cancel_event.set()
        if self.client is not None:
            self.client.cancel()

    @pyqtSlot()
    def run(self):
        try:
            self.client = OllamaClient(
                self.config["endpoint"], mode=self.config["mode"],
                api_key=self.config["api_key"] or "ollama",
            )
            if self.cancel_event.is_set():
                raise OllamaCancelled()
            if self.operation == "models":
                self.models_ready.emit(self.client.list_models())
            else:
                prompt = self.prompt if self.operation == "chat" else (
                    "请用简体中文简短回复：连接正常。若附有图片，请描述图片中的一个可见特征。"
                )
                reply = self.client.chat_stream(
                    self.config["model"], prompt, self.image_bytes, self.image_mime,
                    on_delta=self.delta_ready.emit, on_status=self.status_ready.emit,
                    cancel_event=self.cancel_event, history=self.history,
                )
                if self.cancel_event.is_set():
                    raise OllamaCancelled()
                self.reply_ready.emit(reply)
        except OllamaCancelled:
            self.cancelled.emit()
        except OllamaError as exc:
            self.failed.emit(str(exc))
        except Exception as exc:
            self.failed.emit("模型请求失败：%s" % exc)
        finally:
            self.finished.emit()


class _MarkdownView(QTextBrowser):
    """A selectable Markdown message whose height follows the outer scroll area."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFrameShape(QFrame.Shape.NoFrame)
        self.setOpenExternalLinks(False)
        self.setOpenLinks(False)
        self.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.setSizePolicy(QSizePolicy.Policy.Ignored, QSizePolicy.Policy.Fixed)
        self.setMinimumHeight(32)
        self.document().setDefaultStyleSheet(
            "h1,h2,h3 { color:#f1f5f9; } p { margin:6px 0; } "
            "pre,code { background-color:#252526; } "
            "a { color:#75beff; } table { border-collapse:collapse; } "
            "th,td { border:1px solid #454545; padding:5px; }"
        )
        self.document().documentLayout().documentSizeChanged.connect(self._fit_height)

    def _fit_height(self, *_):
        height = max(32, int(self.document().size().height()) + 10)
        if abs(self.height() - height) > 1:
            self.setFixedHeight(height)

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self.document().setTextWidth(max(20, self.viewport().width() - 4))
        self._fit_height()


class _MessageCard(QFrame):
    def __init__(self, role, text="", model="", image=None, image_name="", parent=None):
        super().__init__(parent)
        self.setObjectName("userMessage" if role == "user" else "assistantMessage")
        self.setSizePolicy(QSizePolicy.Policy.Ignored, QSizePolicy.Policy.Preferred)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(12, 10, 12, 10)
        layout.setSpacing(6)
        row = QHBoxLayout()
        role_label = QLabel("你" if role == "user" else "AI遥感助手")
        role_label.setObjectName("messageRole")
        row.addWidget(role_label)
        if model:
            meta = QLabel(model)
            meta.setObjectName("messageMeta")
            meta.setSizePolicy(QSizePolicy.Policy.Ignored, QSizePolicy.Policy.Preferred)
            meta.setToolTip(model)
            row.addWidget(meta, 1)
        else:
            row.addStretch()
        layout.addLayout(row)
        if image:
            thumbnail = QImage.fromData(image)
            label = QLabel()
            label.setFixedSize(112, 70)
            label.setAlignment(Qt.AlignmentFlag.AlignCenter)
            label.setPixmap(QPixmap.fromImage(thumbnail).scaled(
                label.size(), Qt.AspectRatioMode.KeepAspectRatio,
                Qt.TransformationMode.SmoothTransformation,
            ))
            label.setToolTip(image_name)
            layout.addWidget(label)
        self.execution_label = None
        self.events = []
        self._event_origin = time.monotonic()
        if role == "assistant":
            execution_row = QHBoxLayout()
            self.execution_label = QLabel("正在准备请求…")
            self.execution_label.setObjectName("executionStatus")
            self.execution_label.setWordWrap(True)
            execution_row.addWidget(self.execution_label, 1)
            self.execution_toggle = QToolButton()
            self.execution_toggle.setText("处理过程")
            self.execution_toggle.setArrowType(Qt.ArrowType.RightArrow)
            self.execution_toggle.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextBesideIcon)
            self.execution_toggle.setToolTip("展开实际请求阶段和耗时记录")
            self.execution_toggle.setCheckable(True)
            execution_row.addWidget(self.execution_toggle)
            layout.addLayout(execution_row)
            self.execution_log = QLabel()
            self.execution_log.setObjectName("messageMeta")
            self.execution_log.setWordWrap(True)
            self.execution_log.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
            layout.addWidget(self.execution_log)
            self.execution_log.hide()
            self.execution_toggle.toggled.connect(self._toggle_execution_log)
        self.response = _MarkdownView(self)
        if role == "user":
            self.response.setPlainText(text)
        else:
            self.response.setMarkdown(text)
        layout.addWidget(self.response)
        self.meta_label = QLabel()
        self.meta_label.setObjectName("messageMeta")
        self.meta_label.setWordWrap(True)
        layout.addWidget(self.meta_label)
        self.meta_label.hide()

    def add_event(self, message, state="active"):
        if self.execution_label is None:
            return
        if not self.events or self.events[-1] != message:
            self.events.append(message)
            timestamp = time.monotonic() - self._event_origin
            existing = self.execution_log.text()
            line = "%.1f 秒  ·  %s" % (timestamp, message)
            self.execution_log.setText((existing + "\n" if existing else "") + line)
        self.execution_label.setText(message)
        self.execution_label.setProperty("status", state)
        self.execution_label.style().unpolish(self.execution_label)
        self.execution_label.style().polish(self.execution_label)

    def _toggle_execution_log(self, expanded):
        self.execution_log.setVisible(expanded)
        self.execution_toggle.setArrowType(
            Qt.ArrowType.DownArrow if expanded else Qt.ArrowType.RightArrow
        )


class LibreGeoLensDock(QDockWidget):
    SETTINGS_PREFIX = "jianghe/aiAssistant/"

    def __init__(self, iface):
        super().__init__("AI遥感助手", iface.mainWindow())
        self.iface = iface
        self.setObjectName("JiangheLibreGeoLensDock")
        self.setAllowedAreas(Qt.DockWidgetArea.LeftDockWidgetArea | Qt.DockWidgetArea.RightDockWidgetArea)
        settings = QgsSettings()
        mode = settings.value(self.SETTINGS_PREFIX + "mode", "native")
        if mode not in ("native", "openai"):
            mode = "native"
        self._config = {
            "mode": mode,
            "endpoint": settings.value(self.SETTINGS_PREFIX + "endpoint", "http://127.0.0.1:11434"),
            "api_key": "ollama" if mode == "native" else "",
            "model": settings.value(self.SETTINGS_PREFIX + "model", ""),
        }
        self._models = []
        self._thread = self._worker = None
        self._operation = ""
        self._settings_dialog = None
        self._active_card = None
        self._history = []
        self._reply_text = ""
        self._pending_prompt = ""
        self._request_error = False
        self._request_cancelled = False
        self._request_complete = False
        self._shutting_down = False
        self._started_at = 0.0
        self._image_bytes, self._image_name = None, ""
        self._image_mime = "image/png"
        self._preview_pixmap = QPixmap()
        self._follow_output = True
        self._render_timer = QTimer(self)
        self._render_timer.setSingleShot(True)
        self._render_timer.setInterval(80)
        self._render_timer.timeout.connect(self._render_reply)
        self._elapsed_timer = QTimer(self)
        self._elapsed_timer.setInterval(250)
        self._elapsed_timer.timeout.connect(self._update_elapsed)
        self._build_ui()
        if self._config["mode"] == "native":
            QTimer.singleShot(0, self._refresh_models)
        else:
            self._set_status("点击右上角设置，配置密钥后即可提问。")

    def _tool_button(self, icon_name, tooltip, callback):
        button = QToolButton()
        if icon_name in ("settings-gear", "caption-new", "caption-float", "caption-close"):
            pixmap = QPixmap(24, 24)
            pixmap.fill(Qt.GlobalColor.transparent)
            painter = QPainter(pixmap)
            painter.setRenderHint(QPainter.RenderHint.Antialiasing)
            painter.setPen(QPen(QColor("#d4d4d4"), 2))
            if icon_name == "settings-gear":
                painter.drawEllipse(6, 6, 12, 12)
                painter.drawEllipse(9, 9, 6, 6)
                painter.translate(12, 12)
                for _ in range(8):
                    painter.drawLine(0, -10, 0, -6)
                    painter.rotate(45)
            elif icon_name == "caption-new":
                painter.drawLine(5, 12, 19, 12)
                painter.drawLine(12, 5, 12, 19)
            elif icon_name == "caption-float":
                painter.drawRect(8, 4, 12, 12)
                painter.drawLine(4, 8, 4, 20)
                painter.drawLine(4, 20, 16, 20)
            else:
                painter.drawLine(6, 6, 18, 18)
                painter.drawLine(6, 18, 18, 6)
            painter.end()
            button.setIcon(QIcon(pixmap))
        else:
            button.setIcon(QgsApplication.getThemeIcon(icon_name))
        button.setIconSize(QSize(16, 16))
        button.setToolTip(tooltip)
        button.setAccessibleName(tooltip)
        button.setFixedSize(30, 30)
        button.clicked.connect(callback)
        return button

    def _build_ui(self):
        # A single custom caption avoids duplicated native dock controls.
        caption = QWidget(self)
        caption.setObjectName("aiDockCaption")
        caption.setStyleSheet(
            "QWidget#aiDockCaption { background:#181818; } "
            "QLabel { color:#f1f5f9; font-weight:600; background:transparent; } "
            "QToolButton { background:transparent; border:0; padding:5px; } "
            "QToolButton:hover { background:#3a3d41; } "
            "QToolButton#aiCloseButton:hover { background:#c42b1c; }"
        )
        caption.setFixedHeight(31)
        row = QHBoxLayout(caption)
        row.setContentsMargins(10, 0, 0, 0)
        row.setSpacing(0)
        row.addWidget(QLabel("AI遥感助手"), 1)
        self.new_chat_button = self._tool_button("caption-new", "新建对话", self._new_chat)
        self.settings_button = self._tool_button("settings-gear", "模型设置", self._open_settings)
        self.settings_button.setObjectName("settingsButton")
        self.float_button = self._tool_button("caption-float", "浮动 / 停靠面板", self._toggle_float)
        close_button = self._tool_button("caption-close", "关闭面板", self.close)
        close_button.setObjectName("aiCloseButton")
        for button in (self.new_chat_button, self.settings_button, self.float_button, close_button):
            row.addWidget(button)
        self.setTitleBarWidget(caption)
        root = QWidget(self)
        root.setObjectName("jiangheAiLensRoot")
        layout = QVBoxLayout(root)
        layout.setContentsMargins(10, 8, 10, 10)
        layout.setSpacing(8)
        self.model_summary = QLabel()
        self.model_summary.setObjectName("modelSummary")
        self.model_summary.setWordWrap(True)
        layout.addWidget(self.model_summary)
        self.conversation = QScrollArea()
        self.conversation.setObjectName("aiConversation")
        self.conversation.setWidgetResizable(True)
        self.conversation.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.conversation.setMinimumHeight(160)
        self._conversation_widget = QWidget()
        self._conversation_layout = QVBoxLayout(self._conversation_widget)
        self._conversation_layout.setContentsMargins(0, 4, 0, 6)
        self._conversation_layout.setSpacing(14)
        self.welcome = QLabel(
            "查看地图，理解变化\n\n"
            "截取当前地图或附加现场图片，提出问题后，\n"
            "助手会逐步展示处理状态和分析结果。"
        )
        self.welcome.setWordWrap(True)
        self.welcome.setObjectName("messageMeta")
        self.welcome.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.welcome.setContentsMargins(10, 22, 10, 22)
        self._conversation_layout.addWidget(self.welcome)
        self._conversation_layout.addStretch()
        self.conversation.setWidget(self._conversation_widget)
        self.conversation.verticalScrollBar().valueChanged.connect(self._scroll_changed)
        self.conversation.verticalScrollBar().rangeChanged.connect(self._scroll_range_changed)
        layout.addWidget(self.conversation, 1)
        self.attachment = QFrame()
        self.attachment.setObjectName("executionCard")
        attachment_layout = QHBoxLayout(self.attachment)
        attachment_layout.setContentsMargins(8, 6, 4, 6)
        self.image_preview = QLabel()
        self.image_preview.setObjectName("imagePreview")
        self.image_preview.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.image_preview.setFixedSize(76, 54)
        attachment_layout.addWidget(self.image_preview)
        self.image_status = QLabel("未加载图片")
        self.image_status.setWordWrap(True)
        self.image_status.setObjectName("imageStatus")
        self.image_status.setSizePolicy(QSizePolicy.Policy.Ignored, QSizePolicy.Policy.Preferred)
        attachment_layout.addWidget(self.image_status, 1)
        self.remove_image_button = self._tool_button("caption-close", "移除附图", self._clear_image)
        attachment_layout.addWidget(self.remove_image_button)
        layout.addWidget(self.attachment)
        self.attachment.hide()
        self.progress = QProgressBar()
        self.progress.setObjectName("aiProgress")
        self.progress.setRange(0, 0)
        self.progress.setTextVisible(False)
        self.progress.setFixedHeight(3)
        layout.addWidget(self.progress)
        self.progress.hide()
        self.status = QLabel("准备就绪")
        self.status.setObjectName("executionStatus")
        self.status.setWordWrap(True)
        layout.addWidget(self.status)
        composer = QFrame()
        composer.setObjectName("aiComposer")
        composer_layout = QVBoxLayout(composer)
        composer_layout.setContentsMargins(4, 4, 4, 4)
        composer_layout.setSpacing(4)
        self.prompt = QPlainTextEdit()
        self.prompt.setObjectName("aiPrompt")
        self.prompt.setPlaceholderText("询问影像中的施工扰动、植被恢复或环水保问题…")
        self.prompt.setFixedHeight(85)
        self.prompt.installEventFilter(self)
        composer_layout.addWidget(self.prompt)
        actions = QHBoxLayout()
        actions.setSpacing(5)
        self.capture_button = QPushButton("截取地图")
        self.capture_button.setToolTip("截取当前地图画布并附加到下一次提问")
        self.capture_button.clicked.connect(self._capture_map)
        self.image_button = QPushButton("打开图片")
        self.image_button.clicked.connect(self._open_image)
        self.ask_button = QPushButton("发送")
        self.ask_button.setObjectName("primaryButton")
        self.ask_button.setToolTip("发送问题（Ctrl+Enter）；生成时可停止")
        self.ask_button.clicked.connect(self._send_or_stop)
        actions.addWidget(self.capture_button)
        actions.addWidget(self.image_button)
        actions.addStretch(1)
        actions.addWidget(self.ask_button)
        composer_layout.addLayout(actions)
        layout.addWidget(composer)
        hint = QLabel("Ctrl+Enter 发送  ·  处理过程实时更新")
        hint.setObjectName("messageMeta")
        layout.addWidget(hint)
        self.setWidget(root)
        apply_assistant_theme(self, root)
        self._update_model_summary()

    def _toggle_float(self):
        self.setFloating(not self.isFloating())
        self.show()
        self.raise_()

    def eventFilter(self, watched, event):
        if watched is self.prompt and event.type() == QEvent.Type.KeyPress:
            if event.key() in (Qt.Key.Key_Return, Qt.Key.Key_Enter) and event.modifiers() & Qt.KeyboardModifier.ControlModifier:
                if self._thread is None:
                    self._ask()
                return True
        return super().eventFilter(watched, event)

    def _set_status(self, text):
        self.status.setText(text)

    def _update_model_summary(self):
        provider = "本地 Ollama" if self._config["mode"] == "native" else "在线兼容接口"
        model = self._config["model"] or "尚未选择模型"
        self.model_summary.setText("%s  ·  %s" % (provider, model))
        self.model_summary.setToolTip("点击标题栏齿轮配置模型")

    def _open_settings(self):
        if self._thread is not None:
            return
        if self._settings_dialog is None:
            self._settings_dialog = ModelSettingsDialog(self._config, self._models, self)
            self._settings_dialog.refresh_requested.connect(self._settings_refresh)
            self._settings_dialog.test_requested.connect(self._settings_test)
            self._settings_dialog.settings_saved.connect(self._save_settings)
            self._settings_dialog.finished.connect(self._settings_closed)
        self._settings_dialog.show()
        self._settings_dialog.raise_()
        self._settings_dialog.activateWindow()

    def _settings_closed(self, result):
        if result != 0 or self._settings_dialog is None:
            return
        if self._thread is not None and self._operation in ("test", "settings-models"):
            self._stop_request()
        self._settings_dialog.deleteLater()
        self._settings_dialog = None

    def _save_settings(self, config):
        changed = (config["mode"], config["endpoint"], config["model"]) != (
            self._config["mode"], self._config["endpoint"], self._config["model"]
        )
        self._config = dict(config)
        self._models = [self._settings_dialog.model.itemText(index)
                        for index in range(self._settings_dialog.model.count())]
        settings = QgsSettings()
        for key in ("mode", "endpoint", "model"):
            settings.setValue(self.SETTINGS_PREFIX + key, self._config[key])
        self._update_model_summary()
        if changed:
            self._history.clear()
            self._set_status("模型设置已保存；后续提问使用新配置。")
        else:
            self._set_status("模型设置已保存。")
        self._settings_dialog.deleteLater()
        self._settings_dialog = None

    def _settings_refresh(self, config):
        self._start("settings-models", config=config)

    def _settings_test(self, config):
        if not config["model"]:
            self._settings_dialog.status.setText("请先选择或填写模型名称。")
            return
        self._settings_dialog.test_result.clear()
        self._start("test", config=config, image_bytes=self._image_bytes)

    def _start(self, operation, prompt="", image_bytes=None, config=None):
        if self._thread is not None or self._shutting_down:
            return
        self._operation = operation
        self._request_error = self._request_cancelled = self._request_complete = False
        self._started_at = time.monotonic()
        request_config = config or self._config
        worker_operation = "models" if operation in ("models", "settings-models") else operation
        self._thread = QThread(self)
        self._worker = _OllamaWorker(
            request_config, worker_operation, prompt, image_bytes,
            self._image_mime, self._history if operation == "chat" else None,
        )
        self._worker.moveToThread(self._thread)
        self._thread.started.connect(self._worker.run)
        self._worker.models_ready.connect(self._set_models)
        self._worker.reply_ready.connect(self._set_reply)
        self._worker.delta_ready.connect(self._on_delta)
        self._worker.status_ready.connect(self._on_stage)
        self._worker.failed.connect(self._set_error)
        self._worker.cancelled.connect(self._on_cancelled)
        # quit() must not depend on a waiting GUI thread during shutdown.
        self._worker.finished.connect(self._thread.quit, Qt.ConnectionType.DirectConnection)
        self._worker.finished.connect(self._worker.deleteLater)
        self._thread.finished.connect(self._finish_request)
        self._thread.finished.connect(self._thread.deleteLater)
        self._set_busy(True)
        self._set_status("正在获取模型列表…" if worker_operation == "models" else "正在连接模型…")
        if self._settings_dialog is not None:
            self._settings_dialog.set_busy(True)
            self._settings_dialog.status.setText(self.status.text())
        self._elapsed_timer.start()
        self._thread.start()

    def _set_busy(self, busy):
        for widget in (self.capture_button, self.image_button, self.remove_image_button,
                       self.settings_button, self.new_chat_button):
            widget.setEnabled(not busy)
        self.ask_button.setText("停止" if busy and self._operation in ("chat", "test") else "发送")
        self.ask_button.setEnabled(not busy or self._operation in ("chat", "test"))
        self.progress.setVisible(busy)

    def _finish_request(self):
        self._render_reply()
        self._elapsed_timer.stop()
        self._thread = self._worker = None
        if self._settings_dialog is not None:
            self._settings_dialog.set_busy(False)
        self._set_busy(False)
        if self._operation == "chat" and self._active_card is not None:
            elapsed = time.monotonic() - self._started_at
            state = "已停止" if self._request_cancelled else ("请求失败" if self._request_error else "已完成")
            self._active_card.meta_label.setText("%s  ·  %.1f 秒  ·  %d 字" % (state, elapsed, len(self._reply_text)))
            self._active_card.meta_label.show()
            if not self._request_complete and not self._reply_text:
                self._active_card.response.setPlainText("本次生成已停止。" if self._request_cancelled else "未收到模型回答。")
        self._operation = ""
        if self._shutting_down:
            self.deleteLater()

    def _refresh_models(self):
        self._start("models")

    def _set_models(self, models):
        if self._operation == "settings-models":
            if self._settings_dialog is not None:
                self._settings_dialog.set_models(models)
            return
        self._models = list(models)
        if self._config["model"] not in models:
            self._config["model"] = models[0] if models else ""
        self._update_model_summary()
        self._set_status("准备就绪，可附图并提问。" if models else "未找到可用视觉模型，点击齿轮设置模型。")

    def _on_stage(self, message):
        if message == "模型回答已完成。":
            return  # reply_ready marks completion once, after the final text.
        self._set_status(message)
        if self._operation == "chat" and self._active_card is not None:
            self._active_card.add_event(message)
        elif self._settings_dialog is not None:
            self._settings_dialog.status.setText(message)

    def _on_delta(self, delta):
        if self._request_cancelled:
            return
        if self._operation == "test":
            if self._settings_dialog is not None:
                edit = self._settings_dialog.test_result
                edit.moveCursor(QTextCursor.MoveOperation.End)
                edit.insertPlainText(delta)
            return
        if self._operation != "chat":
            return
        self._reply_text += delta
        if not self._render_timer.isActive():
            self._render_timer.start()

    def _render_reply(self):
        if self._operation == "chat" and self._active_card is not None and self._reply_text:
            self._active_card.response.setMarkdown(self._reply_text)
            self._active_card.response._fit_height()
            self._scroll_to_bottom()

    def _update_elapsed(self):
        if self._operation == "chat" and self._active_card is not None:
            self._active_card.meta_label.setText("生成中  ·  %.1f 秒  ·  已接收 %d 字" % (
                time.monotonic() - self._started_at, len(self._reply_text),
            ))
            self._active_card.meta_label.show()

    def _set_reply(self, reply):
        self._request_complete = True
        if self._operation == "test":
            if self._settings_dialog is not None:
                self._settings_dialog.test_result.setPlainText(reply or "服务连接正常，但没有返回正文。")
                self._settings_dialog.status.setText("连接测试完成。")
            self._set_status("连接测试完成。")
            return
        self._reply_text = reply or ""
        if self._active_card is not None:
            self._active_card.add_event("回答已完成", "complete")
            if not reply:
                self._active_card.response.setPlainText("服务没有返回回答正文，请检查模型是否支持对话和图片。")
        self._render_reply()
        if reply:
            self._history.extend([
                {"role": "user", "content": self._pending_prompt},
                {"role": "assistant", "content": reply},
            ])
            self._history = self._history[-12:]
        self._set_status("回答已完成，可继续追问。")

    def _set_error(self, message):
        self._request_error = True
        self._set_status(message)
        if self._operation == "chat" and self._active_card is not None:
            self._active_card.add_event(message, "error")
        elif self._settings_dialog is not None:
            self._settings_dialog.status.setText(message)

    def _on_cancelled(self):
        self._request_cancelled = True
        self._set_status("已停止生成，已接收的回答会保留。")
        if self._operation == "chat" and self._active_card is not None:
            self._active_card.add_event("已停止生成", "error")
        elif self._settings_dialog is not None:
            self._settings_dialog.status.setText("已停止连接测试。")

    def _send_or_stop(self):
        if self._thread is not None:
            self._stop_request()
        else:
            self._ask()

    def _stop_request(self):
        if self._worker is not None:
            self._request_cancelled = True
            self.ask_button.setEnabled(False)
            self._worker.cancel()
            self._set_status("正在停止请求…")

    def _ask(self):
        if self._thread is not None:
            return
        prompt = self.prompt.toPlainText().strip()
        if not self._config["model"]:
            self._set_status("请点击齿轮配置视觉模型。")
            self._open_settings()
            return
        if self._config["mode"] == "openai" and not self._config["api_key"]:
            self._set_status("请在模型设置中填写 API 密钥。")
            self._open_settings()
            return
        if not prompt:
            self._set_status("请先输入问题。")
            self.prompt.setFocus()
            return
        self.welcome.hide()
        self._follow_output = True
        self._add_card(_MessageCard(
            "user", prompt, image=self._image_bytes, image_name=self._image_name,
        ))
        self._active_card = _MessageCard("assistant", model=self._config["model"])
        self._add_card(self._active_card)
        self._active_card.add_event("已准备图片与问题" if self._image_bytes else "已准备问题")
        self._reply_text = ""
        self._pending_prompt = prompt
        self.prompt.clear()
        language_prompt = (
            "请始终使用简体中文回答。你是一名遥感与生态环境监管助手。"
            "请基于实际收到的图片及问题，先简述分析重点，再给出可见证据、结论与核查建议。"
            "没有依据时请明确说明；不要宣称执行了未实际进行的地图操作或现场检查。\n\n" + prompt
        )
        self._start("chat", prompt=language_prompt, image_bytes=self._image_bytes)

    def _add_card(self, card):
        self._conversation_layout.insertWidget(self._conversation_layout.count() - 1, card)
        self._scroll_to_bottom()

    def _scroll_changed(self, value):
        bar = self.conversation.verticalScrollBar()
        self._follow_output = value >= bar.maximum() - 24

    def _scroll_range_changed(self, *_):
        if self._follow_output:
            self.conversation.verticalScrollBar().setValue(self.conversation.verticalScrollBar().maximum())

    def _scroll_to_bottom(self):
        if self._follow_output:
            QTimer.singleShot(0, self._scroll_range_changed)

    def _new_chat(self):
        if self._thread is not None:
            return
        self._history.clear()
        self._active_card = None
        for index in reversed(range(self._conversation_layout.count())):
            item = self._conversation_layout.itemAt(index)
            widget = item.widget()
            if isinstance(widget, _MessageCard):
                self._conversation_layout.removeWidget(widget)
                widget.deleteLater()
        self.welcome.show()
        self._clear_image()
        self._set_status("已新建对话。")

    @staticmethod
    def _png_bytes(image):
        if image.isNull():
            return b""
        data = QByteArray()
        buffer = QBuffer(data)
        if not buffer.open(QIODevice.OpenModeFlag.WriteOnly):
            return b""
        saved = image.save(buffer, "PNG")
        buffer.close()
        return bytes(data) if saved else b""

    def _attach_image(self, image, name):
        image_bytes = self._png_bytes(image)
        if not image_bytes:
            raise ValueError("图片转换失败")
        self._image_bytes = image_bytes
        self._image_name, self._image_mime = name, "image/png"
        self._preview_pixmap = QPixmap.fromImage(image)
        self._refresh_preview()
        self.image_status.setText("已附图片 · %s\n%d × %d 像素" % (name, image.width(), image.height()))
        self.image_status.setToolTip(name)
        self.attachment.show()

    def _capture_map(self):
        canvas = self.iface.mapCanvas()
        if canvas is None or canvas.width() <= 0 or canvas.height() <= 0:
            self._set_status("当前地图画布不可用，无法截取。")
            return
        try:
            self._attach_image(canvas.grab().toImage(), "地图截图")
            self._set_status("地图截图已加载，输入问题后点击发送。")
        except ValueError as exc:
            self._set_status("地图截图失败：%s" % exc)

    def _open_image(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "选择图片", "", "图片 (*.png *.jpg *.jpeg *.webp *.tif *.tiff);;所有文件 (*.*)",
        )
        if not path:
            return
        try:
            image = QImage(path)
            if image.isNull():
                raise ValueError("图片格式无法读取")
            self._attach_image(image, os.path.basename(path))
            self._set_status("图片已加载，输入问题后点击发送。")
        except (OSError, ValueError) as exc:
            # Keep the previous valid attachment and its preview together.
            self._set_status("图片加载失败：%s" % exc)

    def _clear_image(self):
        self._image_bytes, self._image_name = None, ""
        self._preview_pixmap = QPixmap()
        self.image_preview.clear()
        self.image_status.setText("未加载图片")
        self.attachment.hide()

    def _refresh_preview(self):
        if not self._preview_pixmap.isNull():
            self.image_preview.setPixmap(self._preview_pixmap.scaled(
                self.image_preview.size(), Qt.AspectRatioMode.KeepAspectRatio,
                Qt.TransformationMode.SmoothTransformation,
            ))

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self._refresh_preview()

    def closeEvent(self, event):
        # Closing a dock hides it; retain worker ownership while a request ends.
        super().closeEvent(event)

    def shutdown(self):
        self._shutting_down = True
        self.hide()
        if self._settings_dialog is not None:
            self._settings_dialog.hide()
        if self._worker is not None:
            self._worker.cancel()
        else:
            self.deleteLater()


class LibreGeoLens:
    """Plugin entry point; no business-layer ownership or map editing."""

    def __init__(self, iface):
        self.iface, self.action, self.dock = iface, None, None

    def initGui(self):
        self.action = QAction("AI遥感助手", self.iface.mainWindow())
        self.action.setToolTip("使用视觉模型分析当前地图或图片")
        self.action.triggered.connect(self.run)
        self.iface.addPluginToMenu("AI遥感助手", self.action)
        self.iface.addToolBarIcon(self.action)

    def run(self):
        if self.dock is None:
            self.dock = LibreGeoLensDock(self.iface)
            self.iface.addDockWidget(Qt.DockWidgetArea.RightDockWidgetArea, self.dock)
        self.dock.show()
        self.dock.raise_()

    def unload(self):
        if self.dock is not None:
            self.iface.removeDockWidget(self.dock)
            self.dock.shutdown()
            self.dock = None
        if self.action is not None:
            self.iface.removePluginMenu("AI遥感助手", self.action)
            self.iface.removeToolBarIcon(self.action)
            self.action.deleteLater()
            self.action = None
