"""
记忆模块 - 读写操作

提供短期记忆（sessions）和长期记忆（long_term_memory）的 CRUD 操作
"""
import sqlite3
from typing import List, Optional, Dict
from datetime import datetime
from memory.database import get_connection, DB_PATH, init_database

# ============================================================
# 短期记忆（Sessions）
# ============================================================

def add_session_entry(session_id: str, role: str, content: str) -> int:
    """
    添加一条对话记录

    Args:
        session_id: 会话标识（ESP32 MAC 地址）
        role: "user" 或 "assistant"
        content: 对话内容

    Returns:
        新记录的 ID
    """
    conn = get_connection()
    cursor = conn.cursor()
    cursor.execute(
        "INSERT INTO sessions (session_id, role, content) VALUES (?, ?, ?)",
        (session_id, role, content)
    )
    record_id = cursor.lastrowid
    conn.commit()
    conn.close()
    return record_id


def get_recent_sessions(session_id: str, limit: int = 10) -> List[Dict]:
    """
    获取最近的对话记录

    Args:
        session_id: 会话标识
        limit: 返回记录数量

    Returns:
        对话记录列表
    """
    conn = get_connection()
    cursor = conn.cursor()
    cursor.execute(
        """
        SELECT id, session_id, role, content, created_at
        FROM sessions
        WHERE session_id = ?
        ORDER BY created_at DESC
        LIMIT ?
        """,
        (session_id, limit)
    )
    rows = cursor.fetchall()
    conn.close()
    return [dict(row) for row in reversed(rows)]


def get_session_history(session_id: str, max_turns: int = 10) -> str:
    """
    获取格式化的对话历史（用于 LLM 注入）

    Args:
        session_id: 会话标识
        max_turns: 最大对话轮数

    Returns:
        格式化的对话历史字符串
    """
    sessions = get_recent_sessions(session_id, limit=max_turns * 2)
    history_lines = []
    for s in sessions:
        prefix = "用户" if s["role"] == "user" else "猫"
        history_lines.append(f"{prefix}: {s['content']}")
    return "\n".join(history_lines)


def clear_old_sessions(session_id: str, keep: int = 20) -> int:
    """
    清理旧的对话记录，只保留最近的 N 条

    Args:
        session_id: 会话标识
        keep: 保留数量

    Returns:
        删除的记录数
    """
    conn = get_connection()
    cursor = conn.cursor()
    # 获取要删除的记录 ID
    cursor.execute(
        """
        SELECT id FROM sessions
        WHERE session_id = ?
        ORDER BY created_at DESC
        LIMIT -1 OFFSET ?
        """,
        (session_id, keep)
    )
    old_ids = [row["id"] for row in cursor.fetchall()]
    if old_ids:
        placeholders = ",".join("?" * len(old_ids))
        cursor.execute(f"DELETE FROM sessions WHERE id IN ({placeholders})", old_ids)
        deleted = cursor.rowcount
    else:
        deleted = 0
    conn.commit()
    conn.close()
    return deleted


# ============================================================
# 长期记忆（Long-term Memory）
# ============================================================

def get_memory_summary(session_id: str) -> Optional[str]:
    """
    获取长期记忆摘要

    Args:
        session_id: 会话标识

    Returns:
        记忆摘要，如果没有则返回 None
    """
    conn = get_connection()
    cursor = conn.cursor()
    cursor.execute(
        """
        SELECT summary FROM long_term_memory
        WHERE session_id = ?
        ORDER BY updated_at DESC
        LIMIT 1
        """,
        (session_id,)
    )
    row = cursor.fetchone()
    conn.close()
    return row["summary"] if row else None


def update_memory_summary(session_id: str, summary: str) -> int:
    """
    更新长期记忆摘要

    Args:
        session_id: 会话标识
        summary: 记忆摘要

    Returns:
        受影响的行数
    """
    conn = get_connection()
    cursor = conn.cursor()
    # 检查是否存在
    cursor.execute(
        "SELECT id FROM long_term_memory WHERE session_id = ?",
        (session_id,)
    )
    existing = cursor.fetchone()
    if existing:
        cursor.execute(
            "UPDATE long_term_memory SET summary = ?, updated_at = CURRENT_TIMESTAMP WHERE session_id = ?",
            (summary, session_id)
        )
    else:
        cursor.execute(
            "INSERT INTO long_term_memory (session_id, summary) VALUES (?, ?)",
            (session_id, summary)
        )
    conn.commit()
    conn.close()
    return 1


async def extract_memory_from_dialog(dialog_history: str) -> str:
    """
    从对话历史中提取关键信息（调用 LLM 生成摘要）

    Args:
        dialog_history: 格式化的对话历史

    Returns:
        记忆摘要
    """
    from services.llm import _extract_memory_llm

    try:
        return await _extract_memory_llm(dialog_history)
    except Exception as e:
        print(f"[Memory] LLM extraction failed: {e}")
        return ""


async def maybe_update_memory(session_id: str, dialog_turn_count: int = 5) -> bool:
    """
    根据对话轮数决定是否更新长期记忆

    Args:
        session_id: 会话标识
        dialog_turn_count: 每多少轮对话更新一次记忆

    Returns:
        是否更新了记忆
    """
    # 获取当前对话数量
    conn = get_connection()
    cursor = conn.cursor()
    cursor.execute(
        "SELECT COUNT(*) as cnt FROM sessions WHERE session_id = ?",
        (session_id,)
    )
    count = cursor.fetchone()["cnt"]
    conn.close()

    # 每 N 轮对话更新一次记忆
    if count > 0 and count % dialog_turn_count == 0:
        history = get_session_history(session_id, max_turns=count)
        summary = await extract_memory_from_dialog(history)
        if summary:
            update_memory_summary(session_id, summary)
            return True
    return False


# ============================================================
# 初始化
# ============================================================

def init_memory():
    """初始化记忆模块"""
    init_database()
    print("[Memory] Module initialized")
