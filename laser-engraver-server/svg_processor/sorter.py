from __future__ import annotations

import math
from typing import List, Tuple

Polyline = List[Tuple[float, float]]


def _distance(a: Tuple[float, float], b: Tuple[float, float]) -> float:
    return math.hypot(b[0] - a[0], b[1] - a[1])


def sort_paths_nearest_neighbour(polylines: List[Polyline]) -> List[Polyline]:
    if not polylines:
        return []

    remaining = list(polylines)
    ordered = [remaining.pop(0)]
    current_end = ordered[0][-1]

    while remaining:
        best_index = 0
        best_distance = float("inf")
        reverse = False

        for idx, candidate in enumerate(remaining):
            start_dist = _distance(current_end, candidate[0])
            end_dist = _distance(current_end, candidate[-1])
            if start_dist < best_distance:
                best_distance = start_dist
                best_index = idx
                reverse = False
            if end_dist < best_distance:
                best_distance = end_dist
                best_index = idx
                reverse = True

        chosen = remaining.pop(best_index)
        if reverse:
            chosen = list(reversed(chosen))
        ordered.append(chosen)
        current_end = chosen[-1]

    return ordered
