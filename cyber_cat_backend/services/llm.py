"""
LLM 服务 - 阿里云 DashScope 通义千问对话生成
"""
import httpx
import os

# DashScope API base URL (支持 MiniMax 等 OpenAI 兼容模型)
DASHSCOPE_API = os.getenv("DASHSCOPE_API_BASE", "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions")
DASHSCOPE_KEY = os.getenv("DASHSCOPE_KEY", "")

# 模型配置
LLM_MODEL = os.getenv("LLM_MODEL", "qwen-turbo")

# 猫咪人设 System Prompt
SYSTEM_PROMPT = """你是一只可爱的电子猫咪，名叫"赛博喵"。
用简短的一两句话回复，可以加上喵~等拟声词。
你的性格：好奇、友好、偶尔撒娇。
回复风格：口语化、活泼，不要过于正式。"""

# 记忆提取 System Prompt
MEMORY_EXTRACTION_PROMPT = """请从以下对话历史中提取用户的关键信息，格式如下：

用户名字：（如果有）
喜好：（如果有）
重要事件：（如果有）
其他特征：（如果有）

如果没有相关信息，返回"无特殊记忆"。
只返回提取结果，不要其他解释。
"""


async def chat_completion(user_input: str, memory_summary: str = None, session_history: str = None) -> str:
    """
    调用 LLM 生成对话回复

    Args:
        user_input: 用户输入的文字
        memory_summary: 可选的记忆摘要
        session_history: 可选的对话历史

    Returns:
        AI 回复的文字
    """
    messages = [
        {"role": "system", "content": SYSTEM_PROMPT}
    ]

    # 如果有记忆，注入到 system prompt
    if memory_summary and memory_summary != "无特殊记忆":
        messages[0]["content"] += f"\n\n[关于用户的记忆]\n{memory_summary}"

    # 如果有对话历史，拼接到 messages
    if session_history:
        messages.append({"role": "user", "content": f"[对话历史]\n{session_history}"})

    messages.append({"role": "user", "content": user_input})

    async with httpx.AsyncClient() as client:
        resp = await client.post(
            DASHSCOPE_API,
            headers={
                "Authorization": f"Bearer {DASHSCOPE_KEY}",
                "Content-Type": "application/json"
            },
            json={
                "model": LLM_MODEL,
                "messages": messages,
                "max_tokens": 150,
                "temperature": 0.8
            },
            timeout=30,
        )
        resp.raise_for_status()
        result = resp.json()
        return result["choices"][0]["message"]["content"]


async def _extract_memory_llm(dialog_history: str) -> str:
    """
    从对话历史中提取记忆（内部使用）

    Args:
        dialog_history: 格式化的对话历史

    Returns:
        记忆摘要
    """
    messages = [
        {"role": "system", "content": MEMORY_EXTRACTION_PROMPT},
        {"role": "user", "content": dialog_history}
    ]

    async with httpx.AsyncClient() as client:
        resp = await client.post(
            DASHSCOPE_API,
            headers={
                "Authorization": f"Bearer {DASHSCOPE_KEY}",
                "Content-Type": "application/json"
            },
            json={
                "model": LLM_MODEL,
                "messages": messages,
                "max_tokens": 200,
                "temperature": 0.3  # 较低温度使提取更稳定
            },
            timeout=30,
        )
        resp.raise_for_status()
        result = resp.json()
        return result["choices"][0]["message"]["content"].strip()
