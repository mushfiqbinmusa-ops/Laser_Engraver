import pytest
from app.svg.validator import validate_svg_content, SvgValidationError
from app.svg.parser import SvgParser
from app.svg.transformer import SvgTransformer
from app.svg.tsp_optimizer import TspOptimizer
from app.svg.preview import render_preview_png
from app.svg.parametric import ParametricShapeGenerator
from app.svg.job_planner import JobPlanner
from app.models.preset_models import PresetModel, MachineConfigModel

def test_xml_security_validation():
    # XXE injection attempt
    malicious_svg = """<?xml version="1.0"?>
    <!DOCTYPE foo [<!ENTITY xxe SYSTEM "file:///etc/passwd">]>
    <svg><text>&xxe;</text></svg>"""
    with pytest.raises(SvgValidationError, match="DOCTYPE and ENTITY"):
        validate_svg_content(malicious_svg)

    # Text element rejection (must be converted to paths)
    text_svg = """<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100">
        <text x="10" y="20">Hello Laser</text>
    </svg>"""
    with pytest.raises(SvgValidationError, match="convert all text to paths"):
        validate_svg_content(text_svg)

    # Valid simple SVG
    valid_svg = """<svg xmlns="http://www.w3.org/2000/svg" width="50mm" height="50mm" viewBox="0 0 50 50">
        <rect x="5" y="5" width="20" height="20" />
        <circle cx="35" cy="35" r="8" />
    </svg>"""
    ok, msg = validate_svg_content(valid_svg)
    assert ok is True

def test_svg_parser_and_transformer():
    svg = """<svg width="40mm" height="40mm">
        <rect x="0" y="0" width="20mm" height="10mm" />
        <line x1="5mm" y1="5mm" x2="15mm" y2="5mm" />
    </svg>"""
    contours = SvgParser.extract_contours(svg)
    assert len(contours) == 2

    bounds = SvgTransformer.get_bounds(contours)
    assert bounds[0] == pytest.approx(0.0)
    assert bounds[1] == pytest.approx(0.0)
    assert bounds[2] == pytest.approx(20.0)
    assert bounds[3] == pytest.approx(10.0)

    # Fit to 50x50 mm work area with 2mm margin
    fitted = SvgTransformer.fit_to_work_area(contours, work_w_mm=50.0, work_h_mm=50.0, margin_mm=2.0)
    fbounds = SvgTransformer.get_bounds(fitted)
    assert fbounds[0] >= 2.0
    assert fbounds[1] >= 2.0
    assert fbounds[2] <= 48.0
    assert fbounds[3] <= 48.0

def test_tsp_optimizer():
    # 3 distant squares
    c1 = [(10.0, 10.0), (15.0, 10.0)]
    c2 = [(40.0, 40.0), (45.0, 40.0)]
    c3 = [(12.0, 11.0), (14.0, 11.0)]

    ordered = TspOptimizer.optimize_order([c2, c1, c3], start_pos=(0.0, 0.0))
    # Starting from (0, 0), c1 or c3 should come first, not c2!
    assert ordered[0] in (c1, c3)
    assert ordered[-1] == c2

def test_preview_renderer():
    contours = [
        [(5.0, 5.0), (25.0, 5.0), (25.0, 25.0), (5.0, 25.0), (5.0, 5.0)]
    ]
    png_bytes = render_preview_png(contours, work_w_mm=50.0, work_h_mm=50.0, img_size=300)
    assert len(png_bytes) > 100
    # PNG signature: 89 50 4E 47 0D 0A 1A 0A
    assert png_bytes[:8] == b"\x89PNG\r\n\x1a\n"

def test_job_planner():
    contours = ParametricShapeGenerator.generate_square(20.0)
    preset = PresetModel(
        id="test-preset",
        name="Test",
        travel_speed_sps=200,
        engrave_speed_sps=100,
        laser_power_pct=15,
        laser_duty=614,
        passes=1
    )
    config = MachineConfigModel(
        steps_per_unit_x=80.0,
        steps_per_unit_y=80.0,
        work_area_width_mm=50.0,
        work_area_height_mm=50.0
    )

    plan = JobPlanner.build_plan("job-test-1", "Square Test", contours, preset, config)
    assert plan.total_segments > 0
    assert len(plan.plan_hash) == 64
    assert plan.estimated_duration_s > 0

    # Ensure rapid moves have duty = 0
    for seg in plan.segments:
        if not seg.is_cut:
            assert seg.duty == 0
        else:
            assert seg.duty == 614
