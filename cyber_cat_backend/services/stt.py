"""
STT 服务 - SiliconFlow SenseVoice 语音转文字
"""
import httpx
import os

SILICONFLOW_API = os.getenv("SILICONFLOW_API_BASE", "https://api.siliconflow.cn/v1/audio/transcriptions")
SILICONFLOW_KEY = os.getenv("SILICONFLOW_KEY", "")
STT_MODEL = "FunAudioLLM/SenseVoiceSmall"


async def transcribe(wav_bytes: bytes) -> str:
    """
    将 WAV 音频数据转换为文字

    Args:
        wav_bytes: WAV 音频字节（44 字节头 + 16-bit PCM, 16kHz mono）

    Returns:
        转写后的文字
    """
    async with httpx.AsyncClient() as client:
        resp = await client.post(
            SILICONFLOW_API,
            headers={"Authorization": f"Bearer {SILICONFLOW_KEY}"},
            files={"file": ("audio.wav", wav_bytes, "audio/wav")},
            data={"model": STT_MODEL},
            timeout=30,
        )
        resp.raise_for_status()
        result = resp.json()
        return result.get("text", "")
