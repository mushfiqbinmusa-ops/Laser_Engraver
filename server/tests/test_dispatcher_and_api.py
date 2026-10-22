import asyncio
from pathlib import Path
import pytest
import pytest_asyncio
from httpx import AsyncClient, ASGITransport

from tests.simulation.fake_esp32 import FakeEsp32Server
from app.bridge.tcp_bridge import TcpControllerBridge
from app.bridge.dispatcher import ControllerDispatcher
from app.db.migrations import run_migrations
from app.core.config import settings
from app.main import app
import app.main as main_module

@pytest_asyncio.fixture
async def setup_environment(tmp_path: Path):
    test_db = tmp_path / "test_api.sqlite3"
    settings.db_path = test_db
    run_migrations(test_db)

    # Spin up Fake ESP32 on port 8999
    sim = FakeEsp32Server(host="127.0.0.1", port=8999)
    await sim.start()

    # Connect bridge and dispatcher
    bridge = TcpControllerBridge(host="127.0.0.1", port=8999)
    await bridge.connect()

    dispatcher = ControllerDispatcher(bridge)
    main_module._dispatcher = dispatcher
    main_module._bridge = bridge

    # Initial snapshot
    await dispatcher.send_command({"type": "system.snapshot"}, timeout_s=2.0)

    yield dispatcher, sim

    await bridge.disconnect()
    await sim.stop()

@pytest.mark.asyncio
async def test_dispatcher_ping_and_snapshot(setup_environment):
    dispatcher, sim = setup_environment

    ack = await dispatcher.send_command({"type": "system.ping"})
    assert ack.accepted is True
    assert ack.code == "OK"

    assert dispatcher.latest_snapshot is not None
    assert dispatcher.is_fresh() is True

@pytest.mark.asyncio
async def test_dispatcher_homing_and_jog(setup_environment):
    dispatcher, sim = setup_environment

    # Homing operation
    ack, ev = await dispatcher.send_operation({"type": "home.start", "axis": "all"}, op_timeout_s=5.0)
    assert ack.accepted is True
    assert ev.result == "success"
    assert ev.homed.get("x") is True
    assert ev.homed.get("y") is True

    # Jog
    ack_jog = await dispatcher.send_command({
        "type": "motion.jog",
        "axis": "x",
        "steps": 100,
        "speed_sps": 100
    })
    assert ack_jog.accepted is True

@pytest.mark.asyncio
async def test_api_endpoints(setup_environment):
    dispatcher, sim = setup_environment
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        # 1. Machine Status
        resp = await client.get("/api/machine/status")
        assert resp.status_code == 200
        data = resp.json()
        assert data["connected"] is True
        assert data["fresh"] is True

        # 2. Presets list
        resp_presets = await client.get("/api/presets")
        assert resp_presets.status_code == 200
        presets = resp_presets.json()
        assert len(presets) >= 4
        assert all("microstep_mode" in p for p in presets)

        # 2b. Microstep config persistence
        resp_cfg = await client.get("/api/presets/config")
        assert resp_cfg.status_code == 200
        cfg = resp_cfg.json()
        assert cfg["default_microstep_mode"] == 16
        cfg["default_microstep_mode"] = 8
        resp_cfg_put = await client.put("/api/presets/config", json=cfg)
        assert resp_cfg_put.status_code == 200
        assert resp_cfg_put.json()["default_microstep_mode"] == 8

        resp_new_preset = await client.post("/api/presets", json={
            "name": "Microstep Test Material",
            "laser_power_pct": 10,
            "engrave_speed_sps": 120,
            "microstep_mode": 4
        })
        assert resp_new_preset.status_code == 200
        new_preset = resp_new_preset.json()
        assert new_preset["microstep_mode"] == 4

        resp_patch = await client.put(f"/api/presets/{new_preset['id']}", json={"microstep_mode": 2})
        assert resp_patch.status_code == 200
        assert resp_patch.json()["microstep_mode"] == 2

        # Built-ins allow execution microstep changes but stay otherwise immutable.
        resp_builtin = await client.put("/api/presets/preset-wood", json={"microstep_mode": 1})
        assert resp_builtin.status_code == 200
        assert resp_builtin.json()["microstep_mode"] == 1
        resp_builtin_bad = await client.put("/api/presets/preset-wood", json={"name": "Renamed"})
        assert resp_builtin_bad.status_code == 400

        # 3. Parametric shape generation
        resp_shape = await client.post("/api/jobs/shape", json={
            "shape_type": "square",
            "width_mm": 25.0,
            "preset_id": "preset-wood"
        })
        assert resp_shape.status_code == 200
        job_data = resp_shape.json()
        job_id = job_data["id"]
        assert job_data["total_segments"] > 0

        # 4. Fetch Preview PNG
        resp_prev = await client.get(f"/api/jobs/{job_id}/preview")
        assert resp_prev.status_code == 200
        assert resp_prev.headers["content-type"] == "image/png"
        assert resp_prev.content[:4] == b"\x89PNG"

        # 5. Connection inspection
        resp_conn = await client.get("/api/machine/connection")
        assert resp_conn.status_code == 200
        conn_data = resp_conn.json()
        assert "transport" in conn_data
        assert "host" in conn_data
        assert "port" in conn_data
