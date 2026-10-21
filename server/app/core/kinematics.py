"""Single source of truth for step/mm scaling.

``machine_config.steps_per_unit_*`` is the calibrated resolution at
``REFERENCE_MICROSTEP_MODE`` (1/16). Toolpaths are planned in reference
steps; at execution time they are rescaled to the microstep mode that is
actually active on the controller. This keeps a job dimensionally correct
even if the operator changes microstepping between preparing and running it.
"""

from typing import Any

REFERENCE_MICROSTEP_MODE = 16
BASE_STEPS_PER_MM = 5.0
DEFAULT_REFERENCE_STEPS_PER_UNIT = BASE_STEPS_PER_MM * REFERENCE_MICROSTEP_MODE


def effective_steps_per_mm(reference_steps_per_unit: float, microstep_mode: int) -> float:
    mode = int(microstep_mode or REFERENCE_MICROSTEP_MODE)
    return float(reference_steps_per_unit) * mode / REFERENCE_MICROSTEP_MODE


def scale_segments_for_microstep(
    segments: list[Any],
    target_mode: int,
    source_mode: int = REFERENCE_MICROSTEP_MODE
) -> list[Any]:
    scale = int(target_mode or REFERENCE_MICROSTEP_MODE) / int(source_mode or REFERENCE_MICROSTEP_MODE)
    if scale == 1.0:
        return segments

    scaled = []
    for seg in segments:
        scaled.append(seg.model_copy(update={
            "dx": int(round(seg.dx * scale)),
            "dy": int(round(seg.dy * scale)),
            # Keep mm/s constant: speed in steps/s must scale with steps/mm.
            "speed_sps": max(1, int(round(seg.speed_sps * scale))),
        }))
    return scaled


def scale_plan_for_microstep(plan: Any, target_mode: int) -> Any:
    """Return ``plan`` rescaled from its reference mode to ``target_mode``."""
    mode = int(target_mode or REFERENCE_MICROSTEP_MODE)
    source_mode = int(getattr(plan, "reference_microstep_mode", REFERENCE_MICROSTEP_MODE))
    if mode == source_mode:
        return plan

    scale = mode / source_mode
    return plan.model_copy(update={
        "segments": scale_segments_for_microstep(plan.segments, mode, source_mode),
        "reference_microstep_mode": mode,
        "total_travel_distance_steps": int(round(plan.total_travel_distance_steps * scale)),
        "total_cut_distance_steps": int(round(plan.total_cut_distance_steps * scale)),
    })
