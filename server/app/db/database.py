import sqlite3
from contextlib import contextmanager
from pathlib import Path
from typing import Generator, Optional
from app.core.config import settings

def get_connection(db_path: Optional[Path] = None) -> sqlite3.Connection:
    target_path = db_path or settings.db_path
    conn = sqlite3.connect(str(target_path), timeout=5.0)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA journal_mode = WAL;")
    conn.execute("PRAGMA foreign_keys = ON;")
    conn.execute("PRAGMA busy_timeout = 5000;")
    return conn

@contextmanager
def get_db(db_path: Optional[Path] = None) -> Generator[sqlite3.Connection, None, None]:
    conn = get_connection(db_path)
    try:
        yield conn
        conn.commit()
    except Exception:
        conn.rollback()
        raise
    finally:
        conn.close()
