# Cyber Cat Backend — 开发快速参考

## 项目定位

这是运行在腾讯云服务器上的 AI 后端服务，配套 **Waveshare ESP32-S3** 上的猫咪固件。  
ESP32 负责收音/显示/播放，所有 AI 推理和记忆全部在这里完成。

**服务器：** `122.51.129.89`，Ubuntu 24.04，Python 3.12  
**部署路径：** `/opt/cybercat/`  
**Python 环境：** `/opt/cybercat/venv/`  
**服务名：** `cybercat-backend.service`（systemd 守护进程）

---

## 项目文件结构

```
/opt/cybercat/
├── main.py                    # FastAPI 应用入口
├── .env                       # 环境变量（API Key 等）
├── .env.example               # 环境变量模板
├── requirements.txt           # Python 依赖
├── cybercat-backend.service   # systemd 服务配置
├── services/
│   ├── __init__.py
│   ├── stt.py                 # 语音转文字服务（SiliconFlow）
│   ├── llm.py                 # 大语言模型服务（DashScope/MiniMax）
│   ├── tts.py                 # 文字转语音服务（SiliconFlow）
│   └── emotion.py             # 情绪分析服务
├── memory/
│   ├── __init__.py
│   ├── database.py            # SQLite 数据库初始化
│   └── repository.py          # 记忆读写操作
├── tts_cache/                 # TTS 音频临时文件（定期清理）
└── memory.db                  # SQLite 数据库文件
```

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
| LLM        | MiniMax-M2.5（阿里云 DashScope）          |
| TTS        | SiliconFlow `FunAudioLLM/CosyVoice2-0.5B` |
| 记忆存储   | SQLite（`/opt/cybercat/memory.db`）       |
| 音频临时文件 | `/opt/cybercat/tts_cache/`（定期清理）  |

**API Key 说明：** 使用 SiliconFlow 统一 Key，LLM 用阿里云 DashScope Key。  
Key 放在 `/opt/cybercat/.env` 里，通过 `python-dotenv` 加载，**不要硬编码到代码里**。

```bash
# /opt/cybercat/.env
SILICONFLOW_KEY=sk-...
DASHSCOPE_KEY=sk-...
DASHSCOPE_API_BASE=https://coding.dashscope.aliyuncs.com/v1/chat/completions
LLM_MODEL=MiniMax-M2.5
SERVER_IP=122.51.129.89
PORT=8000
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

## 当前配置（Phase 5 已实现）

**API 配置**：
- **STT**: SiliconFlow SenseVoiceSmall (`FunAudioLLM/SenseVoiceSmall`)
- **LLM**: MiniMax-M2.5 via DashScope (`https://coding.dashscope.aliyuncs.com/v1/chat/completions`)
- **TTS**: SiliconFlow CosyVoice2-0.5B (`FunAudioLLM/CosyVoice2-0.5B:anna`)

**音色选择**：
- `anna`: 温柔女声（默认）
- `alex`: 阳光男声
- 更多音色参考：https://docs.siliconflow.cn/cn/api-reference/audio/create-speech

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

## 服务层实现参考

### STT 服务 (`services/stt.py`)

```python
import httpx
from services.stt import transcribe

# 使用 SiliconFlow SenseVoiceSmall 模型
async def transcribe(wav_bytes: bytes) -> str:
    async with httpx.AsyncClient() as client:
        resp = await client.post(
            "https://api.siliconflow.cn/v1/audio/transcriptions",
            headers={"Authorization": f"Bearer {SILICONFLOW_KEY}"},
            files={"file": ("audio.wav", wav_bytes, "audio/wav")},
            data={"model": "FunAudioLLM/SenseVoiceSmall"},
            timeout=30,
        )
        return resp.json()["text"]
```

### LLM 服务 (`services/llm.py`)

```python
import httpx
from services.llm import chat_completion

# 支持 MiniMax、通义千问等 OpenAI 兼容接口
async def chat_completion(user_input: str, memory_summary: str = None, session_history: str = None) -> str:
    messages = [{"role": "system", "content": SYSTEM_PROMPT}]
    if memory_summary:
        messages[0]["content"] += f"\n[关于用户的记忆：{memory_summary}]"
    
    # 注入对话历史（最近 10 轮）
    if session_history:
        messages.append({"role": "user", "content": f"[对话历史]\n{session_history}"})
    
    messages.append({"role": "user", "content": user_input})
    
    async with httpx.AsyncClient() as client:
        resp = await client.post(
            DASHSCOPE_API,
            headers={"Authorization": f"Bearer {DASHSCOPE_KEY}"},
            json={"model": LLM_MODEL, "messages": messages},
            timeout=30,
        )
        return resp.json()["choices"][0]["message"]["content"]
```

### TTS 服务 (`services/tts.py`)

```python
import uuid
import aiofiles
from services.tts import generate_speech

# 使用 SiliconFlow CosyVoice2-0.5B，支持 anna/alex 等音色
async def generate_speech(text: str) -> str:
    async with httpx.AsyncClient() as client:
        resp = await client.post(
            "https://api.siliconflow.cn/v1/audio/speech",
            headers={"Authorization": f"Bearer {SILICONFLOW_KEY}"},
            json={
                "model": "FunAudioLLM/CosyVoice2-0.5B",
                "voice": "FunAudioLLM/CosyVoice2-0.5B:anna",
                "input": text,
                "response_format": "wav",
                "sample_rate": 16000
            },
            timeout=60,
        )
    
    filename = f"{uuid.uuid4().hex}.wav"
    path = TTS_CACHE_DIR / filename
    async with aiofiles.open(path, "wb") as f:
        await f.write(resp.content)
    
    return f"http://{SERVER_IP}:8000/tts/{filename}"
```

### 情绪分析服务 (`services/emotion.py`)

```python
from services.emotion import analyze_emotion

def analyze_emotion(text: str) -> str:
    """
    分析文本情绪
    返回：neutral, happy, thinking, talking
    """
    text_lower = text.lower()
    
    # Happy 检测
    if any(p in text_lower for p in ["!", "～", "~", "哈哈", "嘻嘻"]):
        return "happy"
    
    # Thinking 检测
    if any(p in text_lower for p in ["嗯", "呃", "也许", "可能"]):
        return "thinking"
    
    return "neutral"
```

---

## 注意事项

- ESP32 上传的 WAV 是 **16kHz 16-bit mono**，SiliconFlow STT 可以直接处理
- TTS 返回的 WAV 也需要 **16kHz**（固件 I2S 配置为 16kHz，不匹配会失速播放）
- 服务器 IP 是公网 IP，如果用 HTTPS 需要配置 nginx + 证书；初期 HTTP 即可（固件 WiFiClientSecure 已设 `setInsecure()`）
- 记忆 SQLite 文件建议每天备份到对象存储，避免服务器重装丢失用户数据

---

## Phase 5 实现计划

### 阶段一：最小可用版 (MVP)

目标：实现 `/chat` 接口，跑通 STT→LLM→TTS 链路

| 任务 | 文件 | 说明 |
|------|------|------|
| 项目骨架 | `requirements.txt`, `.env`, `main.py` | FastAPI 入口 + 依赖 |
| STT 服务 | `services/stt.py` | SiliconFlow SenseVoice |
| LLM 服务 | `services/llm.py` | 阿里云 DashScope |
| TTS 服务 | `services/tts.py` | SiliconFlow CosyVoice |
| `/chat` 接口 | `main.py` | 顺序调用三服务 |
| 静态文件 | `main.py` | `/tts/` 挂载 |

**验收标准**：ESP32 修改 `secrets.h` 指向后端，能完成一轮对话

**接口格式**：
```
POST /chat
Content-Type: audio/wav
X-Session-ID: <ESP32 MAC 地址>（可选）
Body: 原始 WAV 字节（44 字节头 + 16-bit PCM, 16kHz mono）
```

### 阶段二：情绪分析 ✅

情绪分析模块已实现（`services/emotion.py`），使用规则匹配：

| 情绪 | 触发条件 |
|------|----------|
| `happy` | 包含 `!`、`～`、`~`、`哈哈`、`嘻嘻`、`喵喵` 等 |
| `thinking` | 包含 `嗯`、`呃`、`也许`、`可能`、`让我想想` 等 |
| `neutral` | 默认值 |
| `talking` | TTS 播放中（默认返回） |

**未来优化方向**：
- 使用 LLM 进行情绪分类（更准确）
- 增加更多情绪类型（如 `excited`、`curious`、`sleepy`）

### 阶段三：记忆模块 ✅

记忆模块已实现，使用 SQLite 存储：

**文件结构**：
- `memory/database.py`: 数据库初始化，创建表结构
- `memory/repository.py`: 记忆读写操作（CRUD）

**数据库表**：
| 表名 | 用途 | 字段 |
|------|------|------|
| `sessions` | 短期对话历史 | id, session_id, role, content, created_at |
| `long_term_memory` | 长期记忆摘要 | id, session_id, summary, updated_at |

**记忆管理策略**：
- 短期记忆：保留最近 10 轮对话，自动保存到 `sessions` 表
- 长期记忆：每 5 轮对话调用 LLM 摘要一次，更新 `long_term_memory` 表

**LLM 注入**：
```python
# main.py /chat 接口中自动注入
session_id = request.headers.get("x-session-id", "default")
memory_summary = get_memory_summary(session_id)
session_history = get_session_history(session_id)
reply = await chat_completion(transcript, memory_summary, session_history)
```

**示例记忆摘要**：
```
用户名字：小明
喜好：喜欢吃鱼
宠物：养了一只真猫
```

### 阶段四：部署配置 ✅

| 任务 | 文件 | 说明 |
|------|------|------|
| systemd 服务 | `cybercat-backend.service` | 守护进程配置 |
| TTS 清理 | `main.py` (后台任务) | 定期删除 1h+ 缓存 |
| 日志监控 | `journalctl -u cybercat-backend` | 实时日志 |

---

### 阶段五：Phase 5 接入 ✅

ESP32 固件改为调用后端 `/chat` 接口，后端负责串联所有 AI 服务。

**HTTP 合约**：
```
POST /chat
Content-Type: audio/wav
X-Session-ID: <ESP32 MAC 地址>（可选）
Body: 原始 WAV 字节（44 字节头 + 16-bit PCM, 16kHz mono）

Response 200:
{
  "text_reply": "喵～听到了听到了！有什么想聊的吗？🐱",
  "emotion": "happy",
  "audio_url": "http://122.51.129.89:8000/tts/xxxxx.wav"
}
```

**固件侧修改**：
1. `_sttWorkerTask` 改为直发裸 WAV（不是 multipart/form-data）
2. 解析三字段 JSON 响应
3. 第二次 HTTP GET 下载 `audio_url`
4. `emotion` 映射到 `CatExpression`

---

**全阶段完成** ✅

- 阶段一：/chat 接口，STT→LLM→TTS 链路
- 阶段二：情绪分析（规则匹配）
- 阶段三：记忆模块（SQLite 短期 + 长期记忆）
- 阶段四：部署配置（systemd 守护进程）
- 阶段五：Phase 5 接入（ESP32 固件对接后端）

**ESP32 实测日志**：
```
[Cloud] Backend POST http://122.51.129.89:8000/chat (143084 bytes WAV)
[Cloud] Backend response: {"text_reply":"喵~听到了听到了！有什么想聊的吗？🐱","emotion":"happy","audio_url":"http://122.51.129.89:8000/tts/b9544fd93a674e1fa351f2e113b7481b.wav"}
[Cloud] Backend: text=喵~听到了听到了！有什么想聊的吗？🐱 emotion=happy
[Cloud] Downloading TTS WAV: http://122.51.129.89:8000/tts/b9544fd93a674e1fa351f2e113b7481b.wav
[Cloud] TTS WAV downloaded 125532 bytes
[ES8311] play: 16000Hz 1ch 16bit, 125488 bytes PCM
[ES8311] play loop done in 3823ms
```

**端到端流程**：
1. ESP32 长按录音 → VAD 检测语音 → 停止后上传 WAV
2. 后端 `/chat` 接口：STT → 记忆检索 → LLM 生成回复 → 保存对话 → TTS
3. ESP32 下载 TTS 音频 → ES8311 播放 → 表情切换到 `happy`

---

## 部署说明

### 1. 服务器初始化

```bash
# 创建部署目录
mkdir -p /opt/cybercat/tts_cache
cd /opt/cybercat

# 上传代码
# (将本项目文件上传到 /opt/cybercat/)

# 创建 Python 虚拟环境
python3 -m venv venv
source venv/bin/activate

# 安装依赖
pip install -r requirements.txt
```

### 2. 配置环境变量

```bash
# 编辑 .env 文件
vim /opt/cybercat/.env

# 填入真实的 API Key
# SILICONFLOW_KEY=sk-xxx
# DASHSCOPE_KEY=sk-xxx
```

### 3. 启动服务

```bash
# 方式一：开发模式（调试用）
cd /opt/cybercat
source venv/bin/activate
uvicorn main:app --host 0.0.0.0 --port 8000 --reload

# 方式二：systemd 守护进程（生产环境）
cp cybercat-backend.service /etc/systemd/system/
systemctl daemon-reload
systemctl enable cybercat-backend
systemctl start cybercat-backend
```

### 4. 测试接口

```bash
# 健康检查
curl http://localhost:8000/health

# 测试 /chat (需要 WAV 文件)
curl -X POST http://localhost:8000/chat \
  -H "Content-Type: audio/wav" \
  --data-binary @test.wav
```

### 5. API Key 验证

如果返回 `401 Unauthorized`，检查：
- API Key 是否正确（无多余空格）
- Key 是否已激活/有余额
- 阿里云 DashScope 是否完成实名认证

SiliconFlow Key 获取：https://cloud.siliconflow.cn/
阿里云 DashScope Key 获取：https://dashscope.console.aliyun.com/

### 6. ESP32 固件配置

修改 `secrets.h` 指向后端：

```cpp
#define BACKEND_URL "http://122.51.129.89:8000"

// _sttWorkerTask 改为：
// 1. POST WAV 到 {BACKEND_URL}/chat
// 2. 解析 {text_reply, emotion, audio_url}
// 3. 下载 audio_url 并播放
```

### 时间表

| 阶段 | 预计 | 依赖 |
|------|------|------|
| 阶段一 | 2h | API Key 配置 |
| 阶段二 | 1h | 阶段一完成 |
| 阶段三 | 3h | 阶段二完成 |
| 阶段四 | 1h | 阶段一完成 |
