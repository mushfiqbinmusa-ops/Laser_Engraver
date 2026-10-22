# Laser XY Scanner: Server & Firmware Bug Audit & Resolution Report

**Date:** October 2026  
**Target Hardware:** ESP32-S3 SuperMini (`esp32s3_a4988_v1`)  
**Components:** Laser Diode (LEDC PWM GPIO 12), 2x 28BYJ-48 Steppers with A4988 Drivers (X/Y), 2x FC-51/KY-032 Optical IR Sensors (GPIO 13 / 8), XY Deflection Mirrors.  
**Software Stack:** FastAPI Python Server, React + TypeScript Vite Client, Arduino C++ Firmware.

---

## 1. Executive Summary

A comprehensive architectural and logic audit was conducted across the firmware (`firmware/src/`), Python server (`server/app/`), and web frontend (`client/src/`). Multiple logical contradictions, unsafe default states, coordinate boundary discrepancies, and a blocking I/O bottleneck in the HTTP telemetry loop were identified and resolved. 

All unit tests in both the firmware subsystem/parser suite (100% pass) and the Python test suite (20/20 pass) have been verified. The updated firmware was compiled and successfully flashed to the ESP32-S3 SuperMini via `arduino-cli`.

---

## 2. Identified Bugs & Logic Contradictions

### Bug 1: Fallback Snapshot Inaccurately Reported Drivers Enabled
- **Location:** [`server/app/bridge/dispatcher.py`](../server/app/bridge/dispatcher.py)
- **Problem:** When disconnected or prior to receiving the first hardware telemetry frame, `get_effective_snapshot()` returned `"drivers_enabled": True`. However, the physical hardware boots with drivers disabled (`pinMode(PIN_ENABLE_*, OUTPUT); digitalWrite(PIN_ENABLE_*, HIGH);`), and `AUTO_DISABLE_MS` shuts down coils after 30 seconds of inactivity. Reporting drivers enabled gave the user a false impression that stepper coils were energized.
- **Resolution:** Updated fallback snapshot in `dispatcher.py` to `"drivers_enabled": False`.

### Bug 2: MoveAbs Speed Default Contradicted Firmware & Hardware Limits
- **Location:** [`server/app/models/protocol_models.py`](../server/app/models/protocol_models.py)
- **Problem:** `MotionMoveAbsRequest` defaulted to `speed_sps = 150`, whereas the firmware `machine_limits.h` defines `DEFAULT_SPEED_SPS = 100`, the database schema defaults to `100`, and `MotionJogRequest` defaults to `100`. 28BYJ-48 unipolar/bipolar geared stepper motors experience torque drop-off and skip steps above 100–120 sps under load.
- **Resolution:** Normalized `speed_sps: Optional[int] = 100` across all models.

### Bug 3: Absolute Move Coordinate Space Discrepancy (Negative Coordinates)
- **Location:** [`server/app/api/routes_machine.py`](../server/app/api/routes_machine.py)
- **Problem:** In `/api/machine/move-abs`, the server validated coordinates using `if abs(req.x) > max_x or abs(req.y) > max_y:`, permitting negative values like `x = -50`. However, the firmware parser strictly enforces `req.targetX < 0 || req.targetY < 0 -> ErrorCode::OutOfBounds`.
- **Resolution:** Updated server validation to enforce `if req.x < 0 or req.x > max_x or req.y < 0 or req.y > max_y:`. Also added an explicit check for homing validity (`NOT_HOMED`) and active faults.

### Bug 4: HTTP Status Polling Deadlock / Serial Bus Contention
- **Location:** [`server/app/api/routes_machine.py`](../server/app/api/routes_machine.py)
- **Problem:** `GET /api/machine/status` contained synchronous fallback logic: `if not dispatcher.is_fresh() and dispatcher.bridge.is_connected(): await dispatcher.send_command({"type": "system.snapshot"}, timeout_s=0.5)`. Because the React UI and background supervisor continually poll `/status`, every request attempted to acquire `self._lock` and send a command over the serial bus. If the serial response had any latency, the lock became saturated, causing incoming HTTP requests to time out.
- **Resolution:** Removed synchronous serial commands from the `GET /status` and `GET /status/report` endpoints. Telemetry collection is cleanly delegated to the asynchronous background supervisor, making HTTP GET calls instantaneous (<1ms).

### Bug 5: Inactive/Dead Code in Firmware `clearFault()`
- **Location:** [`firmware/src/state/machine_state.cpp`](../firmware/src/state/machine_state.cpp)
- **Problem:** `clearFault()` contained the line `m_state = (m_homedX && m_homedY) ? MachineStateEnum::IdleHomed : MachineStateEnum::IdleUnhomed;`. However, `setFault()` unconditionally clears `m_homedX = m_homedY = false`. Thus, `(m_homedX && m_homedY)` was always `false`, making the ternary expression dead code.
- **Resolution:** Simplified to `m_state = MachineStateEnum::IdleUnhomed;`.

### Bug 6: Homing Allowed from Faulted State Without Clearing Alarm
- **Location:** [`firmware/src/state/machine_state.cpp`](../firmware/src/state/machine_state.cpp)
- **Problem:** `canHome()` only checked `m_state != Moving && m_state != RunningJob`. If the machine entered a `Faulted` state (e.g., E-Stop, optical sensor stuck, or watchdog trip), homing could be initiated while the fault string remained uncleared, contaminating subsequent telemetry.
- **Resolution:** Modified `canHome()` to `return (m_state == MachineStateEnum::IdleUnhomed || m_state == MachineStateEnum::IdleHomed);`. The operator must explicitly resolve and reset the fault before homing can begin.

### Bug 7: Sensor Status Report Filtered Bit Default Inversion
- **Location:** [`server/app/api/routes_machine.py`](../server/app/api/routes_machine.py)
- **Problem:** In `get_status_report()`, `sx_x_filt` and `sx_y_filt` defaulted to `1` (which signifies DETECTED / obstructed in the inverted active-low filter) when absent.
- **Resolution:** Changed default to `0` (CLEAR).

### Bug 8: Schema Discrepancy on Laser Power Ceilings
- **Location:** [`server/app/models/preset_models.py`](../server/app/models/preset_models.py)
- **Problem:** `laser_ceiling_pct` allowed up to `le=100` and `laser_ceiling_duty` allowed `le=MAX_DUTY` (4095). This contradicted the hardware hard clamp (`SAFETY_MAX_PCT = 90`, `SAFETY_MAX_DUTY = 3685`), which would result in firmware rejecting commands with `LASER_DUTY_CLAMPED`.
- **Resolution:** Clamped schema models to `le=SAFETY_MAX_PCT` (90) and `le=SAFETY_MAX_DUTY` (3685).

### Bug 9: Outdated Firmware Subsystem Homing Unit Test Assertion
- **Location:** [`firmware/test/test_subsystems.cpp`](../firmware/test/test_subsystems.cpp)
- **Problem:** When `HOMING_FINAL_PULLOFF_STEPS` (200 steps) and the 20-step pull-off safety clearance were added to `homing_sm.cpp`, the native unit test only ticked 1 step and asserted an obsolete phase, causing `test_subsystems` to abort.
- **Resolution:** Updated unit test loop to tick through the full 5-phase homing sequence (`ClearInitial` → `FastSeek` → `PullOff` → `SlowLatch` → `FinalPullOff` → `Complete`).

---

## 3. Verification & Test Matrix

| Layer | Component | Test Command | Result |
| :--- | :--- | :--- | :--- |
| **Firmware Protocol** | Golden Corpus Parser | `g++ -I firmware/src firmware/test/test_protocol_parser.cpp ...` | **100% Passed** (19 valid, 11 invalid) |
| **Firmware Subsystems** | Kinematics, Watchdog, Homing | `g++ -I firmware/src firmware/test/test_subsystems.cpp ...` | **100% Passed** (All assertions green) |
| **ESP32-S3 Build** | Arduino Core v3.x | `arduino-cli compile --fqbn esp32:esp32:esp32s3 firmware/` | **Success** (903 kB flash, 47 kB RAM) |
| **ESP32-S3 Flash** | USB Serial / JTAG | `arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:esp32s3 firmware/` | **Uploaded & Verified** at 921600 baud |
| **Server Backend** | Pytest Suite | `PYTHONPATH=. .venv/bin/pytest tests` | **20 Passed** in 1.2s |
| **Web Client** | React TypeScript Build | `cd client && npm run build` | **Built successfully** into `server/static` |
| **Live Telemetry** | HTTP Status Endpoint | `curl -s http://localhost:8000/api/machine/status` | **200 OK** in <2ms |
