from app.core.laser import SAFETY_MAX_DUTY, SAFETY_MAX_PCT, clamp_duty, pct_to_duty


def test_pct_to_duty_uses_safety_ceiling():
    assert pct_to_duty(0) == 0
    assert pct_to_duty(SAFETY_MAX_PCT) == SAFETY_MAX_DUTY
    assert pct_to_duty(45) == 1843


def test_pct_to_duty_clamps_out_of_range():
    assert pct_to_duty(-10) == 0
    assert pct_to_duty(150) == SAFETY_MAX_DUTY


def test_clamp_duty_enforces_hard_ceiling():
    assert clamp_duty(4095) == SAFETY_MAX_DUTY
    assert clamp_duty(-1) == 0
