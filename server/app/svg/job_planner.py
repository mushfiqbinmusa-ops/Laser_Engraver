import hashlib
import json
import math
from typing import List, Tuple
from app.svg.parser import Contour
from app.svg.transformer import SvgTransformer
from app.svg.tsp_optimizer import TspOptimizer
from app.models.job_models import ToolpathSegment, PreparedJobPlan
from app.models.preset_models import PresetModel, MachineConfigModel

class JobPlanner:
    MARGIN_MM = 2.0

    @staticmethod
    def prepare_contours(
        contours_mm: List[Contour],
        config: MachineConfigModel
    ) -> List[Contour]:
        """Return the exact contour geometry/order used for the toolpath.

        Shared by the planner and the preview so the rendered image cannot
        drift from what the machine will actually execute.
        """
        fitted_contours = SvgTransformer.fit_to_work_area(
            contours_mm,
            work_w_mm=config.work_area_width_mm,
            work_h_mm=config.work_area_height_mm,
            margin_mm=JobPlanner.MARGIN_MM,
            center=True
        )
        return TspOptimizer.optimize_order(fitted_contours, start_pos=(0.0, 0.0))

    @staticmethod
    def build_plan(
        job_id: str,
        name: str,
        contours_mm: List[Contour],
        preset: PresetModel,
        config: MachineConfigModel
    ) -> PreparedJobPlan:
        ordered_contours = JobPlanner.prepare_contours(contours_mm, config)
        return JobPlanner.build_plan_from_ordered(job_id, name, ordered_contours, preset, config)

    @staticmethod
    def build_plan_from_ordered(
        job_id: str,
        name: str,
        ordered_contours: List[Contour],
        preset: PresetModel,
        config: MachineConfigModel
    ) -> PreparedJobPlan:
        # 1. Convert to step coordinates
        step_contours = SvgTransformer.to_steps(
            ordered_contours,
            steps_per_mm_x=config.steps_per_unit_x,
            steps_per_mm_y=config.steps_per_unit_y
        )

        segments: List[ToolpathSegment] = []
        seg_idx = 0
        curr_x, curr_y = 0, 0
        total_travel_steps = 0
        total_cut_steps = 0
        total_time_s = 0.0

        for pass_idx in range(preset.passes):
            for contour in step_contours:
                if not contour:
                    continue

                # Rapid travel to start of contour (laser off)
                start_x, start_y = contour[0]
                rapid_dx = start_x - curr_x
                rapid_dy = start_y - curr_y

                if rapid_dx != 0 or rapid_dy != 0:
                    dist = math.hypot(rapid_dx, rapid_dy)
                    total_travel_steps += int(dist)
                    time_s = dist / float(preset.travel_speed_sps)
                    total_time_s += time_s

                    segments.append(ToolpathSegment(
                        seg_idx=seg_idx,
                        dx=rapid_dx,
                        dy=rapid_dy,
                        speed_sps=preset.travel_speed_sps,
                        duty=0,
                        is_cut=False
                    ))
                    seg_idx += 1
                    curr_x, curr_y = start_x, start_y

                # Cutting moves along contour
                for i in range(len(contour) - 1):
                    pt_next = contour[i + 1]
                    cut_dx = pt_next[0] - curr_x
                    cut_dy = pt_next[1] - curr_y

                    if cut_dx != 0 or cut_dy != 0:
                        dist = math.hypot(cut_dx, cut_dy)
                        total_cut_steps += int(dist)
                        time_s = dist / float(preset.engrave_speed_sps)
                        total_time_s += time_s

                        segments.append(ToolpathSegment(
                            seg_idx=seg_idx,
                            dx=cut_dx,
                            dy=cut_dy,
                            speed_sps=preset.engrave_speed_sps,
                            duty=preset.laser_duty,
                            is_cut=True
                        ))
                        seg_idx += 1
                        curr_x, curr_y = pt_next[0], pt_next[1]

        # Rapid return to origin (0, 0)
        return_dx = 0 - curr_x
        return_dy = 0 - curr_y
        if return_dx != 0 or return_dy != 0:
            dist = math.hypot(return_dx, return_dy)
            total_travel_steps += int(dist)
            time_s = dist / float(preset.travel_speed_sps)
            total_time_s += time_s

            segments.append(ToolpathSegment(
                seg_idx=seg_idx,
                dx=return_dx,
                dy=return_dy,
                speed_sps=preset.travel_speed_sps,
                duty=0,
                is_cut=False
            ))
            seg_idx += 1

        # Compute deterministic SHA-256 plan hash
        seg_dicts = [s.model_dump() for s in segments]
        plan_hash = hashlib.sha256(json.dumps(seg_dicts, sort_keys=True).encode("utf-8")).hexdigest()

        return PreparedJobPlan(
            job_id=job_id,
            name=name,
            plan_hash=plan_hash,
            total_segments=len(segments),
            estimated_duration_s=round(total_time_s, 2),
            total_travel_distance_steps=total_travel_steps,
            total_cut_distance_steps=total_cut_steps,
            segments=segments
        )
