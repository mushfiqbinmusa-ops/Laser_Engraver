import asyncio
import pytest
from httpx import AsyncClient, ASGITransport
from simulation.fake_esp32 import FakeEsp32Server
from app.main import app
from app.db.migrations import run_migrations
from app.bridge.tcp_bridge import TcpControllerBridge
from app.bridge.dispatcher import ControllerDispatcher
import app.main as main_module

@pytest.fixture
async def sim_environment():
    # 0. Run migrations
    run_migrations()

    # 1. Start simulated ESP32 TCP server
    sim_port = 8899
    sim = FakeEsp32Server(host="127.0.0.1", port=sim_port)
    await sim.start()

    # 2. Configure app bridge and dispatcher to point to simulated ESP32
    bridge = TcpControllerBridge(host="127.0.0.1", port=sim_port)
    connected = await bridge.connect()
    assert connected is True, "Failed to connect to Fake ESP32"

    dispatcher = ControllerDispatcher(bridge)
    # Set singletons in main module
    main_module._bridge = bridge
    main_module._dispatcher = dispatcher

    # Wait for initial snapshot emission
    await asyncio.sleep(0.1)

    try:
        transport = ASGITransport(app=app)
        async with AsyncClient(transport=transport, base_url="http://test") as client:
            yield client, sim, dispatcher
    finally:
        if bridge.is_connected():
            await bridge.disconnect()
        await sim.stop()
        main_module._bridge = None
        main_module._dispatcher = None

@pytest.mark.asyncio
async def test_e2e_static_web_ui_serving(sim_environment):
    client, _, _ = sim_environment
    resp = await client.get("/")
    assert resp.status_code == 200
    assert "Laser XY Scanner — Operator Station" in resp.text
    assert "assets/index-" in resp.text

@pytest.mark.asyncio
async def test_e2e_machine_lifecycle_and_safety_gates(sim_environment):
    client, sim, dispatcher = sim_environment

    # 1. Initial Status Check
    res = await client.get("/api/machine/status")
    assert res.status_code == 200
    data = res.json()
    assert data["connected"] is True
    assert data["fresh"] is True
    snap = data["snapshot"]
    assert snap["state"] == "idle_unhomed"
    assert snap["pos_valid"] is False

    # 2. Ping Check
    res = await client.post("/api/machine/ping")
    assert res.status_code == 200
    assert res.json()["accepted"] is True

    # 3. Safety Gate: Unhomed MoveAbs Rejection
    res = await client.post("/api/machine/move-abs", json={"x": 100, "y": 100, "speed_sps": 200})
    assert res.status_code == 400
    assert "NOT_HOMED" in res.json()["detail"]

    # 4. Driver Enable Toggle
    res = await client.post("/api/machine/driver", json={"enabled": True})
    assert res.status_code == 200
    assert res.json()["accepted"] is True

    # 5. Execute 2-Stage Homing Operation
    res = await client.post("/api/machine/home", json={"axis": "all"})
    assert res.status_code == 200
    home_data = res.json()
    assert home_data["ack"]["accepted"] is True
    assert home_data["event"]["type"] == "operation.completed"
    assert home_data["event"]["result"] == "success"

    # 6. Verify Post-Homing Calibrated State
    res = await client.get("/api/machine/status")
    snap = res.json()["snapshot"]
    assert snap["homed"]["x"] is True
    assert snap["homed"]["y"] is True
    assert snap["pos_valid"] is True
    assert snap["pos"]["x"] == 0
    assert snap["pos"]["y"] == 0

    # 7. Calibrated MoveAbs
    res = await client.post("/api/machine/move-abs", json={"x": 20, "y": 20, "speed_sps": 200})
    assert res.status_code == 200
    assert res.json()["accepted"] is True

    # 7b. Server enforces the configured travel bounds (4096 reference steps)
    res = await client.post("/api/machine/jog", json={"axis": "x", "steps": 5000, "speed_sps": 200})
    assert res.status_code == 400
    assert "out of bounds" in res.json()["detail"].lower()

    res = await client.post("/api/machine/move-abs", json={"x": 5000, "y": 0, "speed_sps": 200})
    assert res.status_code == 400
    assert "out of bounds" in res.json()["detail"].lower()

    # 8. Guarded Laser Pulse Safety Clamp
    # 8a: Within safety limit (3685)
    res = await client.post("/api/machine/laser/pulse", json={"duty": 3685, "duration_ms": 50})
    assert res.status_code == 200
    assert res.json()["accepted"] is True

    # 8b: Beyond 90% hard clamp ceiling (>3685) - rejected at schema boundary
    res = await client.post("/api/machine/laser/pulse", json={"duty": 4000, "duration_ms": 50})
    assert res.status_code == 422

    # 9. Parametric Shape Job Execution
    res = await client.post("/api/jobs/shape", json={
        "shape_type": "square",
        "preset_id": "preset-wood",
        "width_mm": 10.0,
        "name": "E2E Test Square"
    })
    assert res.status_code == 200
    job_info = res.json()
    job_id = job_info["id"]
    assert job_info["total_segments"] > 0

    # Start Job
    res = await client.post(f"/api/jobs/{job_id}/start")
    assert res.status_code == 200
    assert res.json()["status"] == "started"

    # Await job streaming completion
    for _ in range(100):
        await asyncio.sleep(0.1)
        res = await client.get("/api/jobs")
        jobs = res.json()
        target_job = next((j for j in jobs if j["id"] == job_id), None)
        if target_job and target_job["status"] == "completed":
            break
    assert target_job is not None
    assert target_job["status"] == "completed"
    assert target_job["completed_segments"] == target_job["total_segments"]

    # Auto-home after completion restores a calibrated state.
    res = await client.get("/api/machine/status")
    snap = res.json()["snapshot"]
    assert snap["homed"]["x"] is True
    assert snap["homed"]["y"] is True
    assert snap["state"] == "idle_homed"

    # 9b. False Engraving (Dry Run: motors only, laser forced to 0 duty)
    res = await client.post("/api/jobs/shape", json={
        "shape_type": "square",
        "preset_id": "preset-wood",
        "width_mm": 5.0,
        "name": "Dry Run Test Square"
    })
    assert res.status_code == 200
    dry_job_id = res.json()["id"]

    res = await client.post(f"/api/jobs/{dry_job_id}/start?dry_run=true")
    assert res.status_code == 200
    assert res.json()["status"] == "started"

    for _ in range(50):
        await asyncio.sleep(0.05)
        res = await client.get("/api/jobs")
        dry_job = next((j for j in res.json() if j["id"] == dry_job_id), None)
        if dry_job and dry_job["status"] == "completed":
            break
    assert dry_job is not None
    assert dry_job["status"] == "completed"
    assert sim.laser_duty == 0

    # 10. Emergency Stop (E-Stop)
    res = await client.post("/api/machine/estop")
    assert res.status_code == 200
    assert res.json()["accepted"] is True

    # Verify immediate fault state and position invalidation
    res = await client.get("/api/machine/status")
    snap = res.json()["snapshot"]
    assert snap["state"] == "faulted"
    assert snap["fault"] == "ESTOP_ACTIVATED"
    assert snap["pos_valid"] is False
    assert snap["homed"]["x"] is False
    assert snap["homed"]["y"] is False

    # 11. Fault Reset
    res = await client.post("/api/machine/reset-fault")
    assert res.status_code == 200
    assert res.json()["accepted"] is True

    res = await client.get("/api/machine/status")
    snap = res.json()["snapshot"]
    assert snap["fault"] is None
    assert snap["state"] == "idle_unhomed"

@pytest.mark.asyncio
async def test_e2e_preset_microstep_applied_and_completion_auto_homes(sim_environment):
    client, sim, _ = sim_environment

    res = await client.post("/api/machine/home", json={"axis": "all"})
    assert res.status_code == 200

    res = await client.post("/api/presets", json={
        "name": "1/8 Microstep Test",
        "laser_power_pct": 10,
        "engrave_speed_sps": 120,
        "microstep_mode": 8
    })
    assert res.status_code == 200
    preset_id = res.json()["id"]

    res = await client.post("/api/jobs/shape", json={
        "shape_type": "square",
        "preset_id": preset_id,
        "width_mm": 5.0,
        "name": "Microstep E2E"
    })
    assert res.status_code == 200
    job_id = res.json()["id"]

    res = await client.post(f"/api/jobs/{job_id}/start")
    assert res.status_code == 200

    target_job = None
    for _ in range(100):
        await asyncio.sleep(0.1)
        res = await client.get("/api/jobs")
        target_job = next((j for j in res.json() if j["id"] == job_id), None)
        if target_job and target_job["status"] == "completed":
            break

    assert target_job is not None
    assert target_job["status"] == "completed"
    assert sim.microstep_mode == 8

    res = await client.get("/api/machine/status")
    snap = res.json()["snapshot"]
    assert snap["homed"]["x"] is True
    assert snap["homed"]["y"] is True
    assert snap["state"] == "idle_homed"


@pytest.mark.asyncio
async def test_e2e_stop_invalidates_homing_only_while_active(sim_environment):
    client, sim, _ = sim_environment

    res = await client.post("/api/machine/home", json={"axis": "all"})
    assert res.status_code == 200

    # Interrupting nothing preserves the calibrated reference.
    res = await client.post("/api/machine/stop")
    assert res.status_code == 200
    snap = (await client.get("/api/machine/status")).json()["snapshot"]
    assert snap["homed"]["x"] is True
    assert snap["homed"]["y"] is True
    assert snap["state"] == "idle_homed"

    # Interrupting an active job invalidates homing.
    sim.state = "running_job"
    res = await client.post("/api/machine/stop")
    assert res.status_code == 200
    snap = (await client.get("/api/machine/status")).json()["snapshot"]
    assert snap["homed"]["x"] is False
    assert snap["homed"]["y"] is False
    assert snap["state"] == "idle_unhomed"
    assert snap["pos_valid"] is False
