from typing import List, Tuple
from app.svg.parser import Contour, Point

class SvgTransformer:
    @staticmethod
    def get_bounds(contours: List[Contour]) -> Tuple[float, float, float, float]:
        valid_contours = [c for c in contours if c]
        if not valid_contours:
            return 0.0, 0.0, 0.0, 0.0
        min_x = min(p[0] for c in valid_contours for p in c)
        max_x = max(p[0] for c in valid_contours for p in c)
        min_y = min(p[1] for c in valid_contours for p in c)
        max_y = max(p[1] for c in valid_contours for p in c)
        return min_x, min_y, max_x, max_y

    @staticmethod
    def fit_to_work_area(
        contours: List[Contour],
        work_w_mm: float = 50.0,
        work_h_mm: float = 50.0,
        margin_mm: float = 2.0,
        center: bool = True
    ) -> List[Contour]:
        min_x, min_y, max_x, max_y = SvgTransformer.get_bounds(contours)
        orig_w = max_x - min_x
        orig_h = max_y - min_y

        if orig_w <= 0.0 or orig_h <= 0.0:
            return contours

        usable_w = max(0.1, work_w_mm - 2 * margin_mm)
        usable_h = max(0.1, work_h_mm - 2 * margin_mm)

        # Scale down if artwork exceeds usable bounds, keeping aspect ratio
        scale = min(usable_w / orig_w, usable_h / orig_h)
        if scale > 1.0:
            scale = 1.0 # Do not upscale small artwork automatically

        scaled_w = orig_w * scale
        scaled_h = orig_h * scale

        if center:
            offset_x = margin_mm + (usable_w - scaled_w) / 2.0
            offset_y = margin_mm + (usable_h - scaled_h) / 2.0
        else:
            offset_x = margin_mm
            offset_y = margin_mm

        transformed: List[Contour] = []
        for contour in contours:
            new_c = []
            for (x, y) in contour:
                tx = (x - min_x) * scale + offset_x
                ty = (y - min_y) * scale + offset_y
                # Clamp within work area bounds
                tx = max(0.0, min(work_w_mm, tx))
                ty = max(0.0, min(work_h_mm, ty))
                new_c.append((tx, ty))
            transformed.append(new_c)

        return transformed

    @staticmethod
    def to_steps(contours: List[Contour], steps_per_mm_x: float, steps_per_mm_y: float) -> List[List[Tuple[int, int]]]:
        step_contours = []
        for c in contours:
            sc = [(int(round(p[0] * steps_per_mm_x)), int(round(p[1] * steps_per_mm_y))) for p in c]
            step_contours.append(sc)
        return step_contours
