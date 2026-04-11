"""
记忆模块数据库初始化

创建两张表：
- sessions: 短期对话历史
- long_term_memory: 长期记忆摘要
"""
import sqlite3
from pathlib import Path

# 数据库文件路径
DB_PATH = Path("/opt/cybercat/memory.db")


def init_database():
    """初始化数据库，创建表结构"""
    # 确保数据库目录存在
    DB_PATH.parent.mkdir(parents=True, exist_ok=True)

    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()

    # 创建 sessions 表（短期对话历史）
    cursor.execute("""
        CREATE TABLE IF NOT EXISTS sessions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id TEXT NOT NULL,
            role TEXT NOT NULL,
            content TEXT NOT NULL,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
    """)

    # 创建 long_term_memory 表（长期记忆）
    cursor.execute("""
        CREATE TABLE IF NOT EXISTS long_term_memory (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id TEXT NOT NULL,
            summary TEXT NOT NULL,
            updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
    """)

    # 创建索引以加速查询
    cursor.execute("""
        CREATE INDEX IF NOT EXISTS idx_sessions_session_id
        ON sessions(session_id)
    """)
    cursor.execute("""
        CREATE INDEX IF NOT EXISTS idx_memory_session_id
        ON long_term_memory(session_id)
    """)

    conn.commit()
    conn.close()
    print(f"[Memory] Database initialized at {DB_PATH}")


def get_connection():
    """获取数据库连接"""
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    return conn
