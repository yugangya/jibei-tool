"""Non-modal model settings, separate from the assistant conversation."""

from qgis.PyQt.QtCore import pyqtSignal
from qgis.PyQt.QtWidgets import (
    QComboBox, QDialog, QDialogButtonBox, QFormLayout, QHBoxLayout,
    QLabel, QLineEdit, QPlainTextEdit, QPushButton, QVBoxLayout,
)

from .ui_theme import apply_settings_theme


class ModelSettingsDialog(QDialog):
    refresh_requested = pyqtSignal(object)
    test_requested = pyqtSignal(object)
    settings_saved = pyqtSignal(object)

    def __init__(self, config, models, parent=None):
        super().__init__(parent)
        self.setWindowTitle("AI遥感助手 · 模型设置")
        self.resize(520, 410)
        self.setMinimumWidth(380)
        self.setModal(False)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(18, 16, 18, 16)
        layout.setSpacing(12)
        title = QLabel("模型连接")
        title.setObjectName("sectionTitle")
        layout.addWidget(title)
        form = QFormLayout()
        form.setRowWrapPolicy(QFormLayout.RowWrapPolicy.WrapLongRows)
        self.provider = QComboBox()
        self.provider.addItem("Ollama 本地接口", "native")
        self.provider.addItem("OpenAI 兼容接口", "openai")
        self.provider.setCurrentIndex(max(0, self.provider.findData(config["mode"])))
        self.endpoint = QLineEdit(config["endpoint"])
        self.api_key = QLineEdit(config["api_key"])
        self.api_key.setEchoMode(QLineEdit.EchoMode.Password)
        self.api_key.setPlaceholderText("请输入 API 密钥")
        self.model = QComboBox()
        self.model.setEditable(True)
        self.model.addItems(models)
        self.model.setCurrentText(config["model"])
        self.model.setToolTip("选择已获取的视觉模型，或填写服务提供的模型名称")
        form.addRow("接口模式", self.provider)
        form.addRow("服务地址", self.endpoint)
        form.addRow("API 密钥", self.api_key)
        form.addRow("视觉模型", self.model)
        layout.addLayout(form)
        actions = QHBoxLayout()
        self.refresh_models = QPushButton("刷新模型")
        self.test_model = QPushButton("测试连接")
        actions.addWidget(self.refresh_models)
        actions.addWidget(self.test_model)
        actions.addStretch()
        layout.addLayout(actions)
        self.status = QLabel("本地模式请先启动 Ollama；在线模式请填写地址、密钥和模型。")
        self.status.setWordWrap(True)
        self.status.setObjectName("executionStatus")
        layout.addWidget(self.status)
        self.test_result = QPlainTextEdit()
        self.test_result.setReadOnly(True)
        self.test_result.setPlaceholderText("连接测试结果会显示在这里")
        self.test_result.setMaximumHeight(90)
        layout.addWidget(self.test_result)
        note = QLabel("服务地址和模型会保留；API 密钥仅用于当前应用会话。")
        note.setWordWrap(True)
        note.setObjectName("messageMeta")
        layout.addWidget(note)
        self.buttons = QDialogButtonBox()
        self.save_button = self.buttons.addButton("保存设置", QDialogButtonBox.ButtonRole.AcceptRole)
        self.save_button.setObjectName("primaryButton")
        self.buttons.addButton("取消", QDialogButtonBox.ButtonRole.RejectRole)
        layout.addWidget(self.buttons)
        self.refresh_models.clicked.connect(lambda: self.refresh_requested.emit(self.config()))
        self.test_model.clicked.connect(lambda: self.test_requested.emit(self.config()))
        self.buttons.accepted.connect(self._save)
        self.buttons.rejected.connect(self.reject)
        self.provider.currentIndexChanged.connect(self._provider_changed)
        apply_settings_theme(self)

    def config(self):
        return {
            "mode": self.provider.currentData() or "native",
            "endpoint": self.endpoint.text().strip().rstrip("/"),
            "api_key": self.api_key.text().strip(),
            "model": self.model.currentText().strip(),
        }

    def _save(self):
        config = self.config()
        if not config["endpoint"].startswith(("http://", "https://")):
            self.status.setText("请输入以 http:// 或 https:// 开头的服务地址。")
            return
        if not config["model"]:
            self.status.setText("请先刷新模型列表或填写视觉模型名称。")
            return
        if config["mode"] == "openai" and not config["api_key"]:
            self.status.setText("请输入在线接口的 API 密钥。")
            return
        self.settings_saved.emit(config)
        self.accept()

    def _provider_changed(self):
        if self.provider.currentData() == "openai":
            if self.endpoint.text().strip() in ("", "http://127.0.0.1:11434"):
                self.endpoint.setText("https://api.openai.com/v1")
            if self.api_key.text().strip() == "ollama":
                self.api_key.clear()
        else:
            if self.endpoint.text().strip() in ("", "https://api.openai.com/v1"):
                self.endpoint.setText("http://127.0.0.1:11434")
            if not self.api_key.text().strip():
                self.api_key.setText("ollama")
        self.model.clear()
        self.status.setText("接口已切换，请刷新模型列表。")
        self.test_result.clear()

    def set_busy(self, busy):
        for widget in (self.provider, self.endpoint, self.api_key, self.model,
                       self.refresh_models, self.test_model, self.save_button):
            widget.setEnabled(not busy)

    def set_models(self, models):
        previous = self.model.currentText()
        self.model.clear()
        self.model.addItems(models)
        if previous:
            self.model.setCurrentText(previous)
        self.status.setText("已获取 %d 个模型，请选择支持图片输入的视觉模型。" % len(models)
                            if models else "未找到可用视觉模型，请检查服务或手动填写模型名称。")
