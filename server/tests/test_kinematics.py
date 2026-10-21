from app.core.kinematics import (
    REFERENCE_MICROSTEP_MODE,
    effective_steps_per_mm,
    scale_plan_for_microstep,
)
from app.models.job_models import PreparedJobPlan, ToolpathSegment


def _plan() -> PreparedJobPlan:
    return PreparedJobPlan(
        job_id="job-test",
        name="scale test",
        plan_hash="x",
        reference_microstep_mode=REFERENCE_MICROSTEP_MODE,
        total_segments=1,
        estimated_duration_s=1.0,
        total_travel_distance_steps=80,
        total_cut_distance_steps=80,
        segments=[
            ToolpathSegment(seg_idx=0, dx=80, dy=40, speed_sps=1000, duty=1843, is_cut=True)
        ],
    )


def test_effective_steps_per_mm_derives_from_reference_mode():
    assert effective_steps_per_mm(80.0, 16) == 80.0
    assert effective_steps_per_mm(80.0, 8) == 40.0
    assert effective_steps_per_mm(80.0, 2) == 10.0


def test_plan_is_scaled_to_live_microstep_mode():
    scaled = scale_plan_for_microstep(_plan(), 8)

    seg = scaled.segments[0]
    assert (seg.dx, seg.dy) == (40, 20)
    assert seg.speed_sps == 500  # mm/s preserved
    assert scaled.reference_microstep_mode == 8
    assert scaled.total_cut_distance_steps == 40


def test_scaling_is_a_noop_at_reference_mode():
    plan = _plan()
    assert scale_plan_for_microstep(plan, REFERENCE_MICROSTEP_MODE) is plan
