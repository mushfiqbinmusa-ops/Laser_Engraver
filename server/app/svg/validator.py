import xml.etree.ElementTree as ET
from typing import Tuple

MAX_XML_DEPTH = 20
MAX_SVG_BYTES = 5 * 1024 * 1024 # 5 MB

class SvgValidationError(ValueError):
    pass

def validate_svg_content(content: str) -> Tuple[bool, str]:
    if len(content.encode("utf-8")) > MAX_SVG_BYTES:
        raise SvgValidationError("SVG file exceeds maximum permitted size of 5 MB.")

    # Defuse XML entity injection (Billion Laughs / XXE)
    if "<!ENTITY" in content or "<!DOCTYPE" in content:
        raise SvgValidationError("DOCTYPE and ENTITY declarations are not permitted in SVG artwork.")

    try:
        root = ET.fromstring(content)
    except Exception as e:
        raise SvgValidationError(f"Invalid XML syntax: {e}")

    # Check depth
    def check_depth(elem, current_depth=1):
        if current_depth > MAX_XML_DEPTH:
            raise SvgValidationError(f"SVG element tree exceeds maximum nesting depth of {MAX_XML_DEPTH}.")
        for child in elem:
            check_depth(child, current_depth + 1)

    check_depth(root)

    # Check for unsupported tags that would silently drop geometry
    unsupported_tags = {"text", "tspan", "image", "filter", "clipPath", "mask"}
    found_unsupported = set()
    for elem in root.iter():
        tag_name = elem.tag.split("}")[-1] if "}" in elem.tag else elem.tag
        if tag_name in unsupported_tags:
            found_unsupported.add(tag_name)

    if "text" in found_unsupported or "tspan" in found_unsupported:
        raise SvgValidationError("SVG contains <text> elements. Please convert all text to paths/outlines in your vector editor before uploading.")

    if found_unsupported:
        raise SvgValidationError(f"SVG contains unsupported elements: {', '.join(sorted(found_unsupported))}")

    return True, "SVG validation passed"
