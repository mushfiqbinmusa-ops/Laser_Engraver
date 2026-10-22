-- 003_microstep_and_homing.sql: Persist microstep selection per preset and
-- expose a machine-level default. Existing rows keep the 1/16 reference mode.
ALTER TABLE presets ADD COLUMN microstep_mode INTEGER NOT NULL DEFAULT 16;
ALTER TABLE machine_config ADD COLUMN default_microstep_mode INTEGER NOT NULL DEFAULT 16;
