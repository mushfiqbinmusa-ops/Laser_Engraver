from pathlib import Path
import sqlite3
import pytest
from app.db.migrations import run_migrations
from app.db.database import get_db

def test_migration_and_seed(tmp_path: Path):
    db_file = tmp_path / "test_migration.sqlite3"
    applied = run_migrations(db_file)
    assert applied >= 1

    # Verify tables
    with get_db(db_file) as conn:
        cursor = conn.execute("SELECT name FROM sqlite_master WHERE type='table';")
        tables = {row["name"] for row in cursor.fetchall()}
        assert "schema_migrations" in tables
        assert "machine_config" in tables
        assert "presets" in tables
        assert "jobs" in tables
        assert "job_events" in tables

        # Verify seed presets
        cursor = conn.execute("SELECT count(*) as cnt FROM presets WHERE is_builtin = 1;")
        assert cursor.fetchone()["cnt"] >= 4

        # Verify default machine config
        cursor = conn.execute("SELECT * FROM machine_config WHERE id = 'default';")
        cfg = cursor.fetchone()
        assert cfg is not None
        assert cfg["laser_ceiling_pct"] == 90
        assert cfg["laser_ceiling_duty"] == 3685
        assert cfg["default_microstep_mode"] == 16

        cursor = conn.execute("SELECT microstep_mode FROM presets WHERE is_builtin = 1 LIMIT 1;")
        assert cursor.fetchone()["microstep_mode"] == 16

    # Re-running migrations should be idempotent
    applied_again = run_migrations(db_file)
    assert applied_again == 0
