# Cyber Cat Backend — 开发快速参考

## 项目定位

这是运行在腾讯云服务器上的 AI 后端服务，配套 **Waveshare ESP32-S3** 上的猫咪固件。  
ESP32 负责收音/显示/播放，所有 AI 推理和记忆全部在这里完成。

**服务器：** `122.51.129.89`，Ubuntu 24.04，Python 3.12  
**部署路径：** `/opt/cybercat/`  
**Python 环境：** `/opt/cybercat/venv/`  
**服务名：** `cybercat-backend.service`（systemd 守护进程）

---

## ESP32 ↔ 后端：HTTP 合约（固件侧已实现，后端需配合）

### 当前固件行为（Phase 4，已完成）

ESP32 目前**直接**调用 SiliconFlow 和阿里云 API：
- STT：`POST {STT_API_BASE}/audio/transcriptions`（multipart WAV）
- LLM：`POST {LLM_API_BASE}/chat/completions`（OpenAI 格式）
- TTS：`POST {TTS_API_BASE}/audio/speech` → 下载 WAV → 本地播放

### Phase 5 目标：统一接口

ESP32 改为只调一个后端接口，后端负责串联所有 AI 服务：

```
POST /chat
Content-Type: audio/wav
Body: 原始 WAV 字节（44字节头 + 16-bit PCM, 16kHz mono）

Response 200:
{
  "text_reply": "喵～今天天气真好呀",
  "emotion":    "happy",
  "audio_url":  "https://122.51.129.89/tts/xxxxx.wav"
}
```

**emotion 可选值**（固件侧已定义，后端必须严格用这四个之一）：

| 值         | 含义                   | 触发场景       |
|------------|------------------------|----------------|
| `neutral`  | 默认平静               | 普通回复       |
| `happy`    | 开心眯眼               | 被夸 / 轻松聊天 |
| `thinking` | 眯眼外移，沉思         | 不确定 / 复杂回复 |
| `talking`  | 嘴型律动（TTS 播放中） | 所有有 TTS 的回复（默认值）|

> 当前固件 Phase 5 未接入时：emotion 字段暂时不使用，表情由固件内部状态机驱动。  
> 后端只需保证 emotion 字段存在且合法，固件升级后自动生效。

---

## 技术栈

| 层         | 选型                                      |
|------------|-------------------------------------------|
| Web 框架   | FastAPI + uvicorn                         |
| STT        | SiliconFlow `FunAudioLLM/SenseVoiceSmall` |
| LLM        | 通义千问 / DeepSeek（OpenAI 兼容接口）    |
| TTS        | SiliconFlow `FunAudioLLM/CosyVoice2-0.5B` |
| 记忆存储   | SQLite（`/opt/cybercat/memory.db`）       |
| 音频临时文件 | `/opt/cybercat/tts_cache/`（定期清理）  |

**API Key 说明：** 使用 SiliconFlow 统一 Key，LLM 用阿里云 DashScope Key。  
Key 放在 `/opt/cybercat/.env` 里，通过 `python-dotenv` 加载，**不要硬编码到代码里**。

```
# /opt/cybercat/.env
SILICONFLOW_KEY=sk-...
DASHSCOPE_KEY=sk-...
```

---

## 实现顺序（MVP 优先）

### Step 1：最小可用版（先跑通链路）

实现一个顺序调用三个 API 的 `/chat` 端点，不需要 Agent 框架：

```python
@app.post("/chat")
async def chat(request: Request):
    wav_bytes = await request.body()
    transcript = await stt(wav_bytes)          # → SiliconFlow STT
    reply = await llm(transcript)              # → LLM（猫猫人设）
    audio_url = await tts_and_save(reply)      # → SiliconFlow TTS → 保存到 /tts_cache/
    return {"text_reply": reply, "emotion": "talking", "audio_url": audio_url}
```

**调通的标志：** ESP32 固件修改 `secrets.h` 指向后端，能完成一轮对话。

### Step 2：情绪分析

在 LLM 回复后，增加一次轻量分类请求（或在同一次 LLM 调用里用结构化输出），  
将 emotion 从 `"talking"` 改为实际情绪标签。

### Step 3：记忆模块

SQLite 两张表：
- `sessions`：短期对话历史（每次对话追加，超过 N 轮截断）
- `long_term_memory`：提炼的用户偏好/信息（用 LLM 定期摘要写入）

LLM system prompt 中注入记忆摘要：
```
你是一只可爱的电子猫咪。用简短的一两句话回复，可以加上喵~等拟声词。
[关于用户的记忆：{memory_summary}]
```

### Step 4：并行优化（可选）

情绪分析 + LLM 回复可以合并成一次结构化输出，TTS 请求可在 LLM 返回后立刻开始，减少总延迟。

---

## `/tts/{filename}.wav` 静态文件服务

TTS 生成后保存为 UUID 文件名，返回直链给 ESP32 直接播放：

```python
from fastapi.staticfiles import StaticFiles
app.mount("/tts", StaticFiles(directory="/opt/cybercat/tts_cache"), name="tts")
```

文件定期清理（超过 1 小时删除），保持磁盘占用可控。

---

## ESP32 固件侧需要的修改（Phase 5 接入时）

> 这部分由固件 Claude 完成，记录在此供参考。

`src/cloud_client.cpp` 的 `_sttWorkerTask` 需要改为：
1. 直接 POST WAV 到 `{BACKEND_URL}/chat`
2. 解析 `{text_reply, emotion, audio_url}` 三字段
3. 用 `audio_url` 下载 WAV 并调 `es8311Play()`
4. 把 `emotion` 字段通过 `_exprPending` 机制传给 Core 0 切换表情

---

## 部署命令速查

```bash
# 初始化
mkdir -p /opt/cybercat/tts_cache
cd /opt/cybercat && python3 -m venv venv
source venv/bin/activate
pip install fastapi uvicorn httpx python-dotenv aiofiles

# 启动调试
cd /opt/cybercat
uvicorn main:app --host 0.0.0.0 --port 8000 --reload

# systemd 守护进程（/etc/systemd/system/cybercat-backend.service）
[Unit]
Description=Cyber Cat Backend
After=network.target

[Service]
WorkingDirectory=/opt/cybercat
ExecStart=/opt/cybercat/venv/bin/uvicorn main:app --host 0.0.0.0 --port 8000
Restart=always
User=root
EnvironmentFile=/opt/cybercat/.env

[Install]
WantedBy=multi-user.target

# 注册并启动
systemctl daemon-reload
systemctl enable cybercat-backend
systemctl start cybercat-backend
journalctl -u cybercat-backend -f  # 实时日志
```

---

## SiliconFlow API 调用格式

### STT（Whisper 兼容）

```python
import httpx

async def stt(wav_bytes: bytes) -> str:
    async with httpx.AsyncClient() as client:
        resp = await client.post(
            "https://api.siliconflow.cn/v1/audio/transcriptions",
            headers={"Authorization": f"Bearer {SILICONFLOW_KEY}"},
            files={"file": ("audio.wav", wav_bytes, "audio/wav")},
            data={"model": "FunAudioLLM/SenseVoiceSmall"},
            timeout=15,
        )
        return resp.json()["text"]
```

### TTS（OpenAI 兼容，返回 WAV 字节）

```python
async def tts_and_save(text: str) -> str:
    async with httpx.AsyncClient() as client:
        resp = await client.post(
            "https://api.siliconflow.cn/v1/audio/speech",
            headers={"Authorization": f"Bearer {SILICONFLOW_KEY}",
                     "Content-Type": "application/json"},
            json={"model": "FunAudioLLM/CosyVoice2-0.5B",
                  "voice": "FunAudioLLM/CosyVoice2-0.5B:alex",
                  "input": text,
                  "response_format": "wav",
                  "sample_rate": 16000},
            timeout=30,
        )
    filename = f"{uuid.uuid4().hex}.wav"
    path = f"/opt/cybercat/tts_cache/{filename}"
    async with aiofiles.open(path, "wb") as f:
        await f.write(resp.content)
    return f"https://122.51.129.89/tts/{filename}"
```

---

## 注意事项

- ESP32 上传的 WAV 是 **16kHz 16-bit mono**，SiliconFlow STT 可以直接处理
- TTS 返回的 WAV 也需要 **16kHz**（固件 I2S 配置为 16kHz，不匹配会失速播放）
- 服务器 IP 是公网 IP，如果用 HTTPS 需要配置 nginx + 证书；初期 HTTP 即可（固件 WiFiClientSecure 已设 `setInsecure()`）
- 记忆 SQLite 文件建议每天备份到对象存储，避免服务器重装丢失用户数据
