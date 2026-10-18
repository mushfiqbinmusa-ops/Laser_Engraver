import json
from pathlib import Path
import pytest
from app.models.protocol_models import (
    BaseRequest, AckResponse, OperationEvent, SystemSnapshot,
    PingRequest, SnapshotRequest, StopRequest, EStopRequest,
    DriverSetEnabledRequest, HomeStartRequest, MotionJogRequest,
    MotionMoveAbsRequest, LaserPulseRequest, LaserSetDutyRequest,
    JobBeginRequest, JobSegmentRequest, JobEndRequest
)

GOLDEN_PATH = Path(__file__).resolve().parent.parent.parent / "contracts" / "protocol_v1_golden.json"

def test_golden_file_exists():
    assert GOLDEN_PATH.exists(), f"Golden contract file missing at {GOLDEN_PATH}"

def test_valid_requests_schema():
    with open(GOLDEN_PATH, "r", encoding="utf-8") as f:
        data = json.load(f)

    valid_requests = data.get("valid_requests", [])
    assert len(valid_requests) > 0

    model_map = {
        "system.ping": PingRequest,
        "system.snapshot": SnapshotRequest,
        "system.stop": StopRequest,
        "system.estop": EStopRequest,
        "system.reset_fault": BaseRequest,
        "driver.set_enabled": DriverSetEnabledRequest,
        "home.start": HomeStartRequest,
        "motion.jog": MotionJogRequest,
        "motion.move_abs": MotionMoveAbsRequest,
        "laser.pulse": LaserPulseRequest,
        "laser.set_duty": LaserSetDutyRequest,
        "job.begin": JobBeginRequest,
        "job.segment": JobSegmentRequest,
        "job.end": JobEndRequest,
    }

    for item in valid_requests:
        obj = item["obj"]
        cmd_type = obj.get("type")
        assert cmd_type in model_map, f"Unknown command type in golden corpus: {cmd_type}"
        model_cls = model_map[cmd_type]
        validated = model_cls.model_validate(obj)
        assert validated.v == 1
        assert validated.id == obj["id"]

def test_valid_responses_schema():
    with open(GOLDEN_PATH, "r", encoding="utf-8") as f:
        data = json.load(f)

    valid_responses = data.get("valid_responses", [])
    assert len(valid_responses) > 0

    for item in valid_responses:
        obj = item["obj"]
        t = obj.get("type")
        if t == "ack":
            validated = AckResponse.model_validate(obj)
            assert validated.v == 1
        elif t in ("operation.started", "operation.progress", "operation.completed"):
            validated = OperationEvent.model_validate(obj)
            assert validated.v == 1
        elif t == "system.snapshot":
            validated = SystemSnapshot.model_validate(obj)
            assert validated.v == 1
            assert validated.protocol_version == 1
            assert validated.profile_id == "esp32s3_a4988_v1"
