from __future__ import annotations

import io
from typing import List, Tuple

from PIL import Image, ImageDraw

Polyline = List[Tuple[float, float]]


def render_preview(
    polylines: List[Polyline],
    width: int = 400,
    height: int = 400,
    bg_color=(20, 20, 20),
    line_color=(0, 200, 255),
    travel_color=(80, 80, 80),
) -> bytes:
    image = Image.new("RGB", (width, height), bg_color)
    draw = ImageDraw.Draw(image)

    all_x = [point[0] for polyline in polylines for point in polyline]
    all_y = [point[1] for polyline in polylines for point in polyline]
    if not all_x or not all_y:
        return _to_png(image)

    min_x, max_x = min(all_x), max(all_x)
    min_y, max_y = min(all_y), max(all_y)
    span_x = max_x - min_x or 1
    span_y = max_y - min_y or 1

    def to_px(x: float, y: float):
        px = int((x - min_x) / span_x * (width - 20) + 10)
        py = int((y - min_y) / span_y * (height - 20) + 10)
        return px, py

    prev_end = None
    for polyline in polylines:
        if len(polyline) < 2:
            continue
        start = to_px(*polyline[0])
        if prev_end is not None:
            draw.line([prev_end, start], fill=travel_color, width=1)
        pts = [to_px(x, y) for x, y in polyline]
        draw.line(pts, fill=line_color, width=2)
        prev_end = pts[-1]

    return _to_png(image)


def _to_png(image: Image.Image) -> bytes:
    buffer = io.BytesIO()
    image.save(buffer, format="PNG")
    return buffer.getvalue()
