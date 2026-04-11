"""
TTS 服务 - SiliconFlow CosyVoice 文字转语音
"""
import uuid
import aiofiles
from pathlib import Path
import httpx
import os

SILICONFLOW_API = os.getenv("SILICONFLOW_TTS_API_BASE", "https://api.siliconflow.cn/v1/audio/speech")
SILICONFLOW_KEY = os.getenv("SILICONFLOW_KEY", "")
SERVER_IP = os.getenv("SERVER_IP", "122.51.129.89")

# TTS 模型和音色配置
TTS_MODEL = "FunAudioLLM/CosyVoice2-0.5B"
TTS_VOICE = "FunAudioLLM/CosyVoice2-0.5B:anna"  # 温柔女声

# TTS 缓存目录
TTS_CACHE_DIR = Path("/opt/cybercat/tts_cache")


def init_tts_cache():
    """初始化 TTS 缓存目录"""
    TTS_CACHE_DIR.mkdir(parents=True, exist_ok=True)


async def generate_speech(text: str) -> str:
    """
    将文字转换为语音并保存

    Args:
        text: 要转换的文字

    Returns:
        TTS 音频文件的 URL
    """
    async with httpx.AsyncClient() as client:
        resp = await client.post(
            SILICONFLOW_API,
            headers={
                "Authorization": f"Bearer {SILICONFLOW_KEY}",
                "Content-Type": "application/json"
            },
            json={
                "model": TTS_MODEL,
                "voice": TTS_VOICE,
                "input": text,
                "response_format": "wav",
                "sample_rate": 16000
            },
            timeout=60,
        )
        resp.raise_for_status()

    # 生成唯一文件名
    filename = f"{uuid.uuid4().hex}.wav"
    path = TTS_CACHE_DIR / filename

    # 保存到缓存目录
    async with aiofiles.open(path, "wb") as f:
        await f.write(resp.content)

    # 返回访问 URL
    return f"http://{SERVER_IP}:8000/tts/{filename}"
