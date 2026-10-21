"""Laser duty-cycle safety constants and conversions.

The 12-bit LEDC channel accepts 0..4095, but the hardware safety ceiling is
90% (3685). Presets store both a percentage and the derived duty; keep them
in sync through this module instead of duplicating the formula.
"""

import math

MAX_DUTY = 4095
SAFETY_MAX_PCT = 90
SAFETY_MAX_DUTY = 3685


def clamp_duty(duty: int) -> int:
    return max(0, min(SAFETY_MAX_DUTY, int(duty)))


def pct_to_duty(power_pct: int) -> int:
    """Half-up rounding, matching the web UI's Math.round()."""
    pct = max(0, min(SAFETY_MAX_PCT, int(power_pct)))
    return int(math.floor((pct / float(SAFETY_MAX_PCT)) * SAFETY_MAX_DUTY + 0.5))
