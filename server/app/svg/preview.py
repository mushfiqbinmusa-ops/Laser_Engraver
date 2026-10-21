import io
from typing import List, Tuple
from PIL import Image, ImageDraw
from app.svg.parser import Contour, Point

def render_preview_png(
    contours: List[Contour],
    work_w_mm: float = 50.0,
    work_h_mm: float = 50.0,
    img_size: int = 600
) -> bytes:
    img = Image.new("RGBA", (img_size, img_size), (20, 23, 28, 255)) # #14171C
    draw = ImageDraw.Draw(img)

    margin_px = 40
    plot_size = img_size - 2 * margin_px

    def to_pixel(pt: Point) -> Tuple[int, int]:
        px = margin_px + int((pt[0] / work_w_mm) * plot_size)
        py = margin_px + int((pt[1] / work_h_mm) * plot_size)
        return px, py

    # 1. Draw machine boundary box
    b_tl = to_pixel((0.0, 0.0))
    b_br = to_pixel((work_w_mm, work_h_mm))
    draw.rectangle([b_tl, b_br], outline=(75, 83, 102, 255), width=2) # #4B5366

    # 2. Draw 10mm grid lines
    grid_step_mm = 10.0
    gx = grid_step_mm
    while gx < work_w_mm:
        p1 = to_pixel((gx, 0.0))
        p2 = to_pixel((gx, work_h_mm))
        draw.line([p1, p2], fill=(45, 50, 62, 120), width=1)
        gx += grid_step_mm

    gy = grid_step_mm
    while gy < work_h_mm:
        p1 = to_pixel((0.0, gy))
        p2 = to_pixel((work_w_mm, gy))
        draw.line([p1, p2], fill=(45, 50, 62, 120), width=1)
        gy += grid_step_mm

    # 3. Draw rapid transits (laser off) and cutting paths (laser on)
    curr_pos = (0.0, 0.0)
    for contour in contours:
        if not contour:
            continue
        # Rapid move to start of contour (dotted gray)
        p_from = to_pixel(curr_pos)
        p_to = to_pixel(contour[0])
        draw.line([p_from, p_to], fill=(107, 114, 128, 180), width=1) # #6B7280

        # Cut path (crimson red)
        for i in range(len(contour) - 1):
            p1 = to_pixel(contour[i])
            p2 = to_pixel(contour[i+1])
            draw.line([p1, p2], fill=(224, 49, 49, 255), width=2) # #E03131

        curr_pos = contour[-1]

    # 4. Draw rapid return to origin, matching the planner toolpath
    if curr_pos != (0.0, 0.0):
        draw.line(
            [to_pixel(curr_pos), to_pixel((0.0, 0.0))],
            fill=(107, 114, 128, 180),
            width=1
        )

    # 5. Draw origin marker (0, 0)
    p_orig = to_pixel((0.0, 0.0))
    r = 4
    draw.ellipse([p_orig[0] - r, p_orig[1] - r, p_orig[0] + r, p_orig[1] + r], fill=(47, 158, 68, 255)) # #2F9E44

    out_bytes = io.BytesIO()
    img.save(out_bytes, format="PNG")
    return out_bytes.getvalue()
