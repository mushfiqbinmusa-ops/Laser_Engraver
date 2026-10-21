-- 001_initial.sql: Initial schema for Laser XY Scanner production database

CREATE TABLE IF NOT EXISTS schema_migrations (
    version INTEGER PRIMARY KEY,
    applied_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS machine_config (
    id TEXT PRIMARY KEY,
    steps_per_unit_x REAL NOT NULL DEFAULT 80.0,
    steps_per_unit_y REAL NOT NULL DEFAULT 80.0,
    work_area_width_mm REAL NOT NULL DEFAULT 50.0,
    work_area_height_mm REAL NOT NULL DEFAULT 50.0,
    max_travel_steps_x INTEGER NOT NULL DEFAULT 4096,
    max_travel_steps_y INTEGER NOT NULL DEFAULT 4096,
    default_speed_sps INTEGER NOT NULL DEFAULT 100,
    max_speed_sps INTEGER NOT NULL DEFAULT 25000,
    laser_ceiling_pct INTEGER NOT NULL DEFAULT 90,
    laser_ceiling_duty INTEGER NOT NULL DEFAULT 3685,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS presets (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    description TEXT,
    travel_speed_sps INTEGER NOT NULL DEFAULT 200,
    engrave_speed_sps INTEGER NOT NULL DEFAULT 100,
    laser_power_pct INTEGER NOT NULL DEFAULT 15,
    laser_duty INTEGER NOT NULL DEFAULT 614,
    passes INTEGER NOT NULL DEFAULT 1,
    is_builtin BOOLEAN NOT NULL DEFAULT 0,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS jobs (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    source_filename TEXT,
    source_hash TEXT NOT NULL,
    status TEXT NOT NULL DEFAULT 'draft',
    preset_snapshot_json TEXT NOT NULL,
    machine_config_snapshot_json TEXT NOT NULL,
    plan_json TEXT NOT NULL,
    preview_png BLOB,
    total_segments INTEGER NOT NULL DEFAULT 0,
    completed_segments INTEGER NOT NULL DEFAULT 0,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS job_events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    job_id TEXT NOT NULL,
    event_type TEXT NOT NULL,
    details_json TEXT,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY(job_id) REFERENCES jobs(id) ON DELETE CASCADE
);

-- Seed initial machine configuration
INSERT OR IGNORE INTO machine_config (id) VALUES ('default');

-- Seed standard immutable built-in presets
INSERT OR IGNORE INTO presets (id, name, description, travel_speed_sps, engrave_speed_sps, laser_power_pct, laser_duty, passes, is_builtin)
VALUES 
('preset-wood', 'Softwood / Basswood', 'Safe baseline for soft wood etching', 200, 100, 25, 1024, 1, 1),
('preset-leather', 'Vegetable Tanned Leather', 'Low-power surface marking for leather', 250, 120, 15, 614, 1, 1),
('preset-paper', 'Cardstock / Paper', 'High speed low power cutting/scoring', 300, 150, 5, 205, 1, 1),
('preset-acrylic', 'Dark Acrylic', 'Conservative etching profile for dark opaque acrylic', 150, 80, 45, 1843, 1, 1);
