import re
import math
import xml.etree.ElementTree as ET
from typing import List, Tuple

# Type definition for a 2D polyline contour in millimeters
Point = Tuple[float, float]
Contour = List[Point]

# Standard unit conversion factors to millimeters
UNIT_TO_MM = {
    "mm": 1.0,
    "cm": 10.0,
    "in": 25.4,
    "pt": 25.4 / 72.0,
    "pc": 25.4 / 6.0,
    "px": 25.4 / 96.0,
    "": 25.4 / 96.0 # Default unit is pixels at 96 DPI
}

def parse_unit(value_str: str) -> float:
    if not value_str:
        return 0.0
    value_str = value_str.strip()
    match = re.match(r"^([+-]?[0-9.]+)\s*([a-zA-Z]*)$", value_str)
    if not match:
        try:
            return float(value_str) * UNIT_TO_MM[""]
        except ValueError:
            return 0.0
    num = float(match.group(1))
    unit = match.group(2).lower()
    factor = UNIT_TO_MM.get(unit, UNIT_TO_MM[""])
    return num * factor

class SvgParser:
    @staticmethod
    def extract_contours(svg_xml: str) -> List[Contour]:
        root = ET.fromstring(svg_xml)
        contours: List[Contour] = []

        for elem in root.iter():
            tag = elem.tag.split("}")[-1] if "}" in elem.tag else elem.tag

            if tag == "line":
                x1 = parse_unit(elem.attrib.get("x1", "0"))
                y1 = parse_unit(elem.attrib.get("y1", "0"))
                x2 = parse_unit(elem.attrib.get("x2", "0"))
                y2 = parse_unit(elem.attrib.get("y2", "0"))
                contours.append([(x1, y1), (x2, y2)])

            elif tag == "rect":
                x = parse_unit(elem.attrib.get("x", "0"))
                y = parse_unit(elem.attrib.get("y", "0"))
                w = parse_unit(elem.attrib.get("width", "0"))
                h = parse_unit(elem.attrib.get("height", "0"))
                if w > 0 and h > 0:
                    contours.append([
                        (x, y), (x + w, y), (x + w, y + h), (x, y + h), (x, y)
                    ])

            elif tag == "circle":
                cx = parse_unit(elem.attrib.get("cx", "0"))
                cy = parse_unit(elem.attrib.get("cy", "0"))
                r = parse_unit(elem.attrib.get("r", "0"))
                if r > 0:
                    # Approximate circle with 36 segments
                    pts = []
                    steps = 36
                    for i in range(steps + 1):
                        theta = 2.0 * math.pi * i / steps
                        pts.append((cx + r * math.cos(theta), cy + r * math.sin(theta)))
                    contours.append(pts)

            elif tag in ("polyline", "polygon"):
                points_str = elem.attrib.get("points", "")
                raw_nums = re.findall(r"[+-]?[0-9.]+", points_str)
                if len(raw_nums) >= 4:
                    pts = []
                    for i in range(0, len(raw_nums) - 1, 2):
                        pts.append((float(raw_nums[i]) * UNIT_TO_MM[""], float(raw_nums[i+1]) * UNIT_TO_MM[""]))
                    if tag == "polygon" and pts:
                        pts.append(pts[0])
                    contours.append(pts)

            elif tag == "path":
                d_str = elem.attrib.get("d", "")
                path_contours = SvgParser._parse_path_d(d_str)
                contours.extend(path_contours)

        return contours

    @staticmethod
    def _parse_path_d(d_str: str) -> List[Contour]:
        tokens = re.findall(r"([a-zA-Z]|[+-]?[0-9.]+)", d_str)
        contours: List[Contour] = []
        current_contour: Contour = []
        cx, cy = 0.0, 0.0
        start_x, start_y = 0.0, 0.0
        cmd = "M"

        i = 0
        while i < len(tokens):
            token = tokens[i]
            if token.isalpha():
                cmd = token
                i += 1
                if cmd in ("Z", "z"):
                    if current_contour:
                        current_contour.append((start_x, start_y))
                    cx, cy = start_x, start_y
                continue

            if cmd in ("M", "m"):
                if i + 1 >= len(tokens):
                    break
                if current_contour:
                    contours.append(current_contour)
                    current_contour = []
                x = float(token) * UNIT_TO_MM[""]
                y = float(tokens[i+1]) * UNIT_TO_MM[""]
                i += 2
                if cmd == "m":
                    cx += x
                    cy += y
                else:
                    cx = x
                    cy = y
                start_x, start_y = cx, cy
                current_contour.append((cx, cy))
                cmd = "L" if cmd == "M" else "l"

            elif cmd in ("L", "l"):
                if i + 1 >= len(tokens):
                    break
                x = float(token) * UNIT_TO_MM[""]
                y = float(tokens[i+1]) * UNIT_TO_MM[""]
                i += 2
                if cmd == "l":
                    cx += x
                    cy += y
                else:
                    cx = x
                    cy = y
                current_contour.append((cx, cy))

            elif cmd in ("H", "h"):
                x = float(token) * UNIT_TO_MM[""]
                i += 1
                cx = (cx + x) if cmd == "h" else x
                current_contour.append((cx, cy))

            elif cmd in ("V", "v"):
                y = float(token) * UNIT_TO_MM[""]
                i += 1
                cy = (cy + y) if cmd == "v" else y
                current_contour.append((cx, cy))

            elif cmd in ("Z", "z"):
                if current_contour:
                    current_contour.append((start_x, start_y))
                cx, cy = start_x, start_y
                i += 1

            else:
                # Unsupported path command; advance
                i += 1

        if current_contour:
            contours.append(current_contour)

        return contours
