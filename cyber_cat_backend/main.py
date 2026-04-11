"""
Cyber Cat Backend - FastAPI 应用入口
"""
import os
import time
import asyncio
from pathlib import Path
from dotenv import load_dotenv

# 加载环境变量
load_dotenv()

from fastapi import FastAPI, Request, Response
from fastapi.staticfiles import StaticFiles
from fastapi.middleware.cors import CORSMiddleware

from services.stt import transcribe
from services.llm import chat_completion
from services.tts import generate_speech, init_tts_cache
from services.emotion import analyze_emotion
from memory.repository import init_memory, get_memory_summary, get_session_history, add_session_entry, maybe_update_memory

# ============================================================
# 配置
# ============================================================
TTS_CACHE_DIR = Path("/opt/cybercat/tts_cache")
DEFAULT_SESSION_ID = "default"  # 默认会话 ID（ESP32 可传递 MAC 地址）


# ============================================================
# 应用初始化
# ============================================================
app = FastAPI(
    title="Cyber Cat Backend",
    description="AI 猫咪后端服务 - STT → LLM → TTS",
    version="0.1.0"
)

# CORS 允许所有来源（ESP32 需要）
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# 初始化 TTS 缓存目录
init_tts_cache()
TTS_CACHE_DIR.mkdir(parents=True, exist_ok=True)

# 初始化记忆模块
init_memory()

# 挂载静态文件服务（TTS 音频下载）
app.mount("/tts", StaticFiles(directory=str(TTS_CACHE_DIR)), name="tts")


# ============================================================
# API 端点
# ============================================================
@app.get("/health")
async def health_check():
    """健康检查"""
    return {"status": "ok"}


@app.post("/chat")
async def chat(request: Request):
    """
    主接口：接收 WAV 音频，返回 AI 回复和 TTS 音频 URL

    请求：
        Content-Type: audio/wav
        X-Session-ID: 会话标识（可选，默认为 default）
        Body: 原始 WAV 字节（44 字节头 + 16-bit PCM, 16kHz mono）

    响应：
        {
            "text_reply": "喵～今天天气真好呀",
            "emotion": "happy",
            "audio_url": "http://122.51.129.89:8000/tts/xxxxx.wav"
        }
    """
    # Step 1: 读取 session_id 和音频数据
    session_id = request.headers.get("x-session-id", DEFAULT_SESSION_ID)
    wav_bytes = await request.body()
    if not wav_bytes:
        return Response(
            content='{"detail": "Empty audio data"}',
            status_code=400,
            media_type="application/json"
        )

    try:
        # Step 2: STT - 语音转文字
        transcript = await transcribe(wav_bytes)
        if not transcript:
            return Response(
                content='{"detail": "STT failed - no transcript"}',
                status_code=400,
                media_type="application/json"
            )

        # Step 3: 获取长期记忆和对话历史
        memory_summary = get_memory_summary(session_id)
        session_history = get_session_history(session_id)

        # Step 4: LLM - 生成回复（注入记忆 + 历史）
        reply = await chat_completion(transcript, memory_summary, session_history)

        # Step 5: 保存对话到短期记忆
        add_session_entry(session_id, "user", transcript)
        add_session_entry(session_id, "assistant", reply)

        # Step 6: 检查是否需要更新长期记忆
        await maybe_update_memory(session_id)

        # Step 7: TTS - 文字转语音
        audio_url = await generate_speech(reply)

        # Step 8: 情绪分析
        emotion = analyze_emotion(reply)

        return {
            "text_reply": reply,
            "emotion": emotion,
            "audio_url": audio_url
        }

    except Exception as e:
        return Response(
            content=f'{{"detail": "{str(e)}"}}',
            status_code=500,
            media_type="application/json"
        )


# ============================================================
# TTS 缓存清理任务
# ============================================================
async def cleanup_tts_cache():
    """删除超过 1 小时的 TTS 文件"""
    while True:
        try:
            cutoff = time.time() - 3600  # 1 小时前
            for f in TTS_CACHE_DIR.glob("*.wav"):
                if f.stat().st_mtime < cutoff:
                    f.unlink()
                    print(f"[Cleanup] Deleted: {f.name}")
        except Exception as e:
            print(f"[Cleanup] Error: {e}")
        await asyncio.sleep(300)  # 每 5 分钟检查一次


@app.on_event("startup")
async def startup_event():
    """启动时运行缓存清理任务"""
    asyncio.create_task(cleanup_tts_cache())


# ============================================================
# 主程序入口
# ============================================================
if __name__ == "__main__":
    import uvicorn
    port = int(os.getenv("PORT", "8000"))
    uvicorn.run(app, host="0.0.0.0", port=port)
