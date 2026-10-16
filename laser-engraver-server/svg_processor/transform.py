from __future__ import annotations

from typing import List, Tuple

Polyline = List[Tuple[float, float]]


def fit_to_work_area(
    polylines: List[Polyline],
    work_area_steps_x: int,
    work_area_steps_y: int,
    margin_pct: float = 0.05,
    keep_aspect: bool = True,
) -> List[Polyline]:
    if not polylines:
        return []

    all_x = [point[0] for polyline in polylines for point in polyline]
    all_y = [point[1] for polyline in polylines for point in polyline]
    if not all_x or not all_y:
        return polylines

    min_x, max_x = min(all_x), max(all_x)
    min_y, max_y = min(all_y), max(all_y)
    svg_w = max(max_x - min_x, 1.0)
    svg_h = max(max_y - min_y, 1.0)

    avail_x = work_area_steps_x * (1 - 2 * margin_pct)
    avail_y = work_area_steps_y * (1 - 2 * margin_pct)
    offset_x = work_area_steps_x * margin_pct
    offset_y = work_area_steps_y * margin_pct

    scale_x = avail_x / svg_w
    scale_y = avail_y / svg_h
    if keep_aspect:
        scale = min(scale_x, scale_y)
        scale_x = scale_y = scale

    transformed: List[Polyline] = []
    for polyline in polylines:
        new_polyline: Polyline = []
        for x, y in polyline:
            nx = (x - min_x) * scale_x + offset_x
            ny = (y - min_y) * scale_y + offset_y
            new_polyline.append((nx, ny))
        transformed.append(new_polyline)
    return transformed
