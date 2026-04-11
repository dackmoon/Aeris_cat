"""
情绪分析服务 - 简单规则匹配
"""


def analyze_emotion(text: str) -> str:
    """
    分析文本情绪

    Args:
        text: AI 回复的文字

    Returns:
        情绪标签：neutral, happy, thinking, talking

    情绪规则：
    - happy: 包含感叹号、~、语气词（哈哈、嘻嘻、喵等）
    - thinking: 包含不确定词汇（嗯、呃、也许、可能等）
    - talking: 默认值（TTS 播放中）
    - neutral: 普通回复
    """
    text_lower = text.lower()

    # Happy 检测：感叹号、~、语气词
    happy_patterns = ["!", "!", "～", "~", "哈哈", "嘻嘻", "嘿嘿", "喵喵", "喵~", "喵！"]
    if any(p in text_lower for p in happy_patterns):
        return "happy"

    # Thinking 检测：不确定词汇
    thinking_patterns = ["嗯", "呃", "这个", "让我想想", "也许", "可能", "大概", "说不定"]
    if any(p in text_lower for p in thinking_patterns):
        return "thinking"

    # 默认 neutral
    return "neutral"
