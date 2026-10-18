import math
from typing import List
from app.svg.parser import Contour

class ParametricShapeGenerator:
    @staticmethod
    def generate_square(size_mm: float = 20.0) -> List[Contour]:
        return ParametricShapeGenerator.generate_rectangle(size_mm, size_mm)

    @staticmethod
    def generate_rectangle(width_mm: float = 20.0, height_mm: float = 15.0) -> List[Contour]:
        return [[
            (0.0, 0.0),
            (width_mm, 0.0),
            (width_mm, height_mm),
            (0.0, height_mm),
            (0.0, 0.0)
        ]]

    @staticmethod
    def generate_circle(radius_mm: float = 10.0, steps: int = 48) -> List[Contour]:
        pts = []
        for i in range(steps + 1):
            theta = 2.0 * math.pi * i / steps
            x = radius_mm + radius_mm * math.cos(theta)
            y = radius_mm + radius_mm * math.sin(theta)
            pts.append((x, y))
        return [pts]

    @staticmethod
    def generate_grid(width_mm: float = 20.0, height_mm: float = 20.0, rows: int = 5, cols: int = 5) -> List[Contour]:
        contours: List[Contour] = []
        
        # Horizontal lines
        dx = width_mm / cols
        dy = height_mm / rows
        for r in range(rows + 1):
            y = r * dy
            contours.append([(0.0, y), (width_mm, y)])

        # Vertical lines
        for c in range(cols + 1):
            x = c * dx
            contours.append([(x, 0.0), (x, height_mm)])

        return contours
