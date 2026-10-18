import logging
from pathlib import Path
import sqlite3
from app.db.database import get_db

logger = logging.getLogger(__name__)

MIGRATIONS_DIR = Path(__file__).parent / "sql"

def run_migrations(db_path: Path = None) -> int:
    applied_count = 0
    with get_db(db_path) as conn:
        conn.execute("""
            CREATE TABLE IF NOT EXISTS schema_migrations (
                version INTEGER PRIMARY KEY,
                applied_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
            );
        """)

        cursor = conn.execute("SELECT version FROM schema_migrations ORDER BY version ASC;")
        applied_versions = {row["version"] for row in cursor.fetchall()}

        sql_files = sorted(MIGRATIONS_DIR.glob("*.sql"))
        for sql_file in sql_files:
            try:
                version = int(sql_file.name.split("_")[0])
            except ValueError:
                continue

            if version not in applied_versions:
                logger.info(f"Applying database migration: {sql_file.name}")
                script = sql_file.read_text(encoding="utf-8")
                conn.executescript(script)
                conn.execute("INSERT INTO schema_migrations (version) VALUES (?);", (version,))
                applied_count += 1

    return applied_count
