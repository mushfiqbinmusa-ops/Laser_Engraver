import math
from typing import List, Tuple
from app.svg.parser import Contour, Point

def euclidean_dist(p1: Point, p2: Point) -> float:
    return math.hypot(p1[0] - p2[0], p1[1] - p2[1])

class TspOptimizer:
    @staticmethod
    def optimize_order(contours: List[Contour], start_pos: Point = (0.0, 0.0)) -> List[Contour]:
        valid_contours = [c for c in contours if len(c) > 0]
        if len(valid_contours) <= 1:
            return valid_contours

        contours = valid_contours
        unvisited = list(range(len(contours)))
        ordered: List[Contour] = []
        current_pt = start_pos

        while unvisited:
            best_idx = -1
            best_dist = float("inf")
            should_reverse = False

            for idx in unvisited:
                c = contours[idx]
                d_start = euclidean_dist(current_pt, c[0])
                d_end = euclidean_dist(current_pt, c[-1])

                if d_start < best_dist:
                    best_dist = d_start
                    best_idx = idx
                    should_reverse = False

                # If contour is not closed, we can potentially reverse it if end is closer
                is_closed = euclidean_dist(c[0], c[-1]) < 0.01
                if not is_closed and d_end < best_dist:
                    best_dist = d_end
                    best_idx = idx
                    should_reverse = True

            unvisited.remove(best_idx)
            chosen = contours[best_idx]
            if should_reverse:
                chosen = list(reversed(chosen))

            ordered.append(chosen)
            current_pt = chosen[-1]

        return ordered
