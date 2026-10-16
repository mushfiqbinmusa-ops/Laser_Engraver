from __future__ import annotations

import os
import re
import tempfile
from typing import List, Tuple

try:
    from svgpathtools import svg2paths2
except Exception:  # pragma: no cover - dependency may not be available in minimal environments
    svg2paths2 = None

Polyline = List[Tuple[float, float]]


def parse_svg(svg_content: bytes) -> List[Polyline]:
    """Parse SVG content and return a list of polylines."""
    if isinstance(svg_content, str):
        svg_content = svg_content.encode("utf-8")

    if svg2paths2 is not None:
        with tempfile.NamedTemporaryFile(suffix=".svg", delete=False) as tmp:
            tmp.write(svg_content)
            temp_path = tmp.name
        try:
            paths, _, _ = svg2paths2(temp_path)
        finally:
            try:
                os.unlink(temp_path)
            except OSError:
                pass

        polylines: List[Polyline] = []
        for path in paths:
            if not path:
                continue
            points: Polyline = []
            for segment in path:
                samples = 20
                for index in range(samples):
                    t = index / samples
                    pt = segment.point(t)
                    points.append((float(pt.real), float(pt.imag)))
            if points:
                last = path[-1].point(1.0)
                points.append((float(last.real), float(last.imag)))
            if len(points) >= 2:
                polylines.append(points)
        if polylines:
            return polylines

    return _fallback_parse_svg(svg_content.decode("utf-8", errors="ignore"))


def _fallback_parse_svg(svg_text: str) -> List[Polyline]:
    polylines: List[Polyline] = []
    matches = re.findall(r"<path[^>]*d=\"([^\"]+)\"", svg_text, flags=re.IGNORECASE)
    for d in matches:
        coords = re.findall(r"[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?", d)
        pairs = []
        for i in range(0, len(coords) - 1, 2):
            try:
                x = float(coords[i])
                y = float(coords[i + 1])
            except IndexError:
                continue
            pairs.append((x, y))
        if len(pairs) >= 2:
            polylines.append(pairs)
    if polylines:
        return polylines

    numeric = re.findall(r"[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?", svg_text)
    if len(numeric) >= 4:
        pairs = []
        for i in range(0, len(numeric) - 1, 2):
            pairs.append((float(numeric[i]), float(numeric[i + 1])))
        return [pairs]
    return []
