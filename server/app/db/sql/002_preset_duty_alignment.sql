-- Keep built-in preset duty values in sync with the single pct -> duty
-- mapping (round(pct / 90 * 3685)). The original acrylic seed was off by one.
UPDATE presets
SET laser_duty = CAST(ROUND((laser_power_pct / 90.0) * 3685) AS INTEGER)
WHERE is_builtin = 1
  AND laser_duty != CAST(ROUND((laser_power_pct / 90.0) * 3685) AS INTEGER);
