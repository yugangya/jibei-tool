# Jianghe AI Remote Sensing Assistant

This is a QGIS 4 / Qt 6 compatible, dependency-free integration for a local
Ollama vision model. It is intentionally separate from the Jianghe ecological
restoration tools and does not create or modify business layers.

The default mode uses Ollama's native HTTP API (`/api/tags` and `/api/chat`),
so no `pip` install is performed. The dock also has an OpenAI-compatible mode:
use `https://api.openai.com/v1` with an OpenAI API key, or use
`http://127.0.0.1:11434/v1` with the placeholder key `ollama` to use Ollama's
compatibility endpoint.

Ollama does not contain OpenAI's hosted model catalog. Install a local
multimodal model into Ollama, for example:

```powershell
ollama pull qwen2.5vl:7b
```

启动 Ollama 后，从业务工具栏打开“AI遥感助手”。右上角齿轮打开独立的模型设置窗口，
可配置接口模式、地址、密钥、模型，刷新模型或测试连接。保存后主面板只显示当前模型。
服务地址和模型使用 QGIS 用户设置保存；API 密钥仅保留在当前应用会话中。

点击“截取地图”附加当前地图画布，或“打开图片”附加本地照片。附图卡片显示缩略图、
文件名和尺寸。图片统一转换为 PNG；移除按钮可取消附图，不影响工程图层。

底部输入问题后点击“发送”（Ctrl+Enter）。问答连续保留，支持多轮追问；模型公开回答
按流式事件逐步显示，Markdown 标题、列表和表格会正常排版。处理栏显示真实连接、等待、
接收和完成状态，以及耗时和已接收字数。模型不提供任务总量时使用活动条，不伪造百分比。
点击“停止”保留已收到的正文；未完成的回答不加入后续请求的对话上下文。

Ollama 使用 JSONL，OpenAI 兼容接口使用 SSE。若服务对 stream=true 返回普通 JSON，
仍能显示完整回答。处理状态来自实际请求事件，不显示模型的隐藏推理字段。

右上角“新建对话”清空当前问答和附图。关闭停靠面板不会销毁仍在运行的请求线程；
卸载插件时先取消请求，再在后台线程结束后释放面板。插件不拥有或修改业务图层。

协议回归测试使用 Python 标准库运行 tests/test_ollama_client.py（无需在线 API 密钥）。
