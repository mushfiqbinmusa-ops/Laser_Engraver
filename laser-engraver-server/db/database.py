from __future__ import annotations

import sqlite3
from pathlib import Path
from typing import Any, Dict, Optional

ROOT = Path(__file__).resolve().parent.parent
DB_PATH = ROOT / "jobs.sqlite3"


def init_db() -> None:
    conn = sqlite3.connect(DB_PATH)
    conn.execute(
        """
        CREATE TABLE IF NOT EXISTS jobs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            filename TEXT NOT NULL,
            status TEXT NOT NULL,
            job_data TEXT NOT NULL,
            preview BLOB,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP
        )
        """
    )
    conn.commit()
    conn.close()


def save_job(filename: str, job_data: Dict[str, Any], preview: Optional[bytes] = None) -> int:
    import json

    conn = sqlite3.connect(DB_PATH)
    cursor = conn.execute(
        "INSERT INTO jobs (filename, status, job_data, preview) VALUES (?, ?, ?, ?)",
        (filename, job_data.get("status", "queued"), json.dumps(job_data), preview),
    )
    conn.commit()
    job_id = int(cursor.lastrowid)
    conn.close()
    return job_id


def get_job(job_id: int) -> Optional[Dict[str, Any]]:
    import json

    conn = sqlite3.connect(DB_PATH)
    row = conn.execute(
        "SELECT id, filename, status, job_data, preview FROM jobs WHERE id = ?",
        (job_id,),
    ).fetchone()
    conn.close()
    if row is None:
        return None
    return {
        "id": row[0],
        "filename": row[1],
        "status": row[2],
        "job_data": json.loads(row[3]),
        "preview": row[4],
    }


init_db()
