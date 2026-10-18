# Comprehensive Technical Report: `laser-engraver-server` and `laser-engraver-firmware`

**Document Version:** 1.0.0  
**Project:** Laser XY Scanner / Engraver  
**Target Architecture:** ESP32-S3 SuperMini + Dual 28BYJ-48 Steppers + Optical IR Limit Sensors + TTL Blue Laser Diode  
**Date:** October 18, 2026  

---

## 1. Executive Summary

This report delivers a thorough audit, architectural review, setup guide, and discrepancy analysis of:
1. **`laser-engraver-server`**: A FastAPI-based Python control server handling SVG parsing, path optimization, toolpath generation (G-code), and communications (TCP / Serial) to the hardware.
2. **`laser-engraver-firmware`**: The baseline legacy ESP32 Arduino firmware designed to receive G-code commands over Wi-Fi TCP / Serial and drive motion, homing, and laser PWM.

> [!IMPORTANT]
> **Legacy Status & Reality Check:**  
> Both `laser-engraver-server` and `laser-engraver-firmware` represent early baseline implementations. Since they were drafted, dedicated test fixtures were developed for hardware commissioning:
> - [`stepper_motor_test/`](file:///home/mushfiq/Desktop/Laser-XY-Scanner/stepper_motor_test) & [`stepper_ir_homing_test/`](file:///home/mushfiq/Desktop/Laser-XY-Scanner/stepper_ir_homing_test) (Dual A4988 bipolar stepper drivers, microstepping on MS1/MS2/MS3, IR homing on GPIO 13/8).
> - [`laser_pwm_test/`](file:///home/mushfiq/Desktop/Laser-XY-Scanner/laser_pwm_test) (ESP32-S3 LEDC PWM control on GPIO 10).
> - [`ir_sensor_test/`](file:///home/mushfiq/Desktop/Laser-XY-Scanner/ir_sensor_test) (Active-LOW digital detection with internal pullups).
>
> The legacy firmware and server are **substantially decoupled from current hardware realities**. They require targeted modernization before they can control the physical bench setup safely.

---

## 2. Server Deep-Dive (`laser-engraver-server`)

### 2.1 Architecture Overview

The server is built with **FastAPI** running on Python 3.13 / Uvicorn. It acts as an orchestrator converting vector graphics into physical motions:

```
[Web Browser / UI]  <--->  [FastAPI App (main.py)]  <--->  [SQLite DB (jobs.sqlite3)]
                                   │
                    ┌──────────────┴──────────────┐
                    ▼                             ▼
           [SVG Processing Engine]         [ESP32 Bridge]
           - parser.py                     - tcp_client.py (asyncio)
           - transform.py                  - serial_client.py (pyserial)
           - sorter.py                            │
           - gcode_gen.py                         ▼
           - preview.py                    [Physical ESP32-S3]
```

### 2.2 Server Controls & API Endpoints

The server exposes both a graphical web dashboard and RESTful / WebSocket endpoints:

| Endpoint | Method | Description | Request / Parameters | Response / Behavior |
|---|---|---|---|---|
| `/` | `GET` | Serves the web dashboard | None | Returns `static/index.html` |
| `/api/health` | `GET` | Health check & ESP32 connection state | None | `{"ok": true, "esp_connected": bool, "transport": "tcp"\|"serial"}` |
| `/api/control/home` | `POST` | Triggers axis homing | None | Sends `G28\n` via TCP/Serial, returns ESP32 response |
| `/api/control/laser/power` | `POST` | Sets laser test power (0–100%) | Query param `level: int` | Converts percentage to 12-bit duty (`int(level/100 * 4095)`), sends `M3 S<duty>` |
| `/api/upload` | `POST` | Uploads and processes SVG file | Multipart Form: `svg_file: .svg` | 1. Extracts paths (`parser.py`)<br>2. Fits into work area (`transform.py`)<br>3. Solves TSP nearest-neighbor (`sorter.py`)<br>4. Generates G-code (`gcode_gen.py`)<br>5. Generates PNG preview (`preview.py`)<br>6. Saves to SQLite; returns job metadata & preview URL |
| `/api/jobs/{id}/preview` | `GET` | Retrieves rendered PNG preview | URL Path: `job_id: int` | PNG image stream (`image/png`) |
| `/ws` | `WebSocket` | Real-time status push | WebSocket connection | Pushes `{"ok": true, "esp_connected": bool, "transport": str}` |

### 2.3 Dashboard UI (`static/`)

The web UI provides a clean dashboard:
- **Home Axes Button (`#homeBtn`)**: Sends POST request to `/api/control/home`.
- **Laser Power Slider (`#powerSlider`)**: Range 0–100%, sends live updates to `/api/control/laser/power?level=...`.
- **SVG Upload Form (`#uploadForm`)**: Uploads vector artwork, initiates toolpath processing, displays the rendered path preview and G-code preview.
- **Connection Badges**: Displays `ONLINE` / `OFFLINE` status and active transport protocol (polled every 3 seconds).

### 2.4 Server Setup & Run Guide

#### Prerequisites
- Linux / macOS / Windows with **Python 3.10+** (Python 3.13 tested)
- A virtual environment is already present at `laser-engraver-server/.venv/`

#### Step-by-Step Instructions

1. **Navigate to the server directory**:
   ```bash
   cd /home/mushfiq/Desktop/Laser-XY-Scanner/laser-engraver-server
   ```

2. **Activate the Virtual Environment**:
   ```bash
   source .venv/bin/activate
   ```

3. **Install / Verify Dependencies**:
   ```bash
   pip install -r requirements.txt
   ```
   *(Requirements: `fastapi`, `uvicorn[standard]`, `websockets`, `svgpathtools`, `Pillow`, `pyserial`, `numpy`, `python-multipart`, `pytest`, `httpx`)*

4. **Configure Network or Serial Transport**:
   Open `config.py` and adjust to your environment:
   ```python
   @dataclass
   class Settings:
       esp32_host: str = "192.168.1.50"   # ESP32 IP address on your Wi-Fi network
       esp32_port: int = 8888             # TCP port (default: 8888)
       serial_port: str = "/dev/ttyUSB0"  # or /dev/ttyACM0 for direct USB CDC
       baudrate: int = 115200
       transport: str = "tcp"             # Select "tcp" or "serial"
       work_area_steps_x: int = 1000      # Mechanical travel limit in steps
       work_area_steps_y: int = 800
       laser_duty: int = 1024             # Default burning duty (12-bit, max 4095)
   ```

5. **Run the Server**:
   ```bash
   uvicorn main:app --host 0.0.0.0 --port 8000 --reload
   ```
   Alternatively, execute directly with Python:
   ```bash
   python main.py
   ```

6. **Access Dashboard**:
   Open your browser at `http://localhost:8000`.

7. **Run Existing Test Suite**:
   ```bash
   pytest tests/
   ```
   *(All 5 tests currently pass: SVG parsing, transform, path sorting, and FastAPI endpoints).*

---

## 3. Firmware Deep-Dive (`laser-engraver-firmware`)

### 3.1 Firmware Architecture

The legacy firmware uses an object-oriented modular structure in standard Arduino C++:
- **`laser-engraver-firmware.ino`**: Setup routine, main loop polling Wi-Fi TCP and Serial for G-code lines.
- **`config.h`**: Pin definitions, timing, limits, and Wi-Fi credentials.
- **`gcode_parser.cpp / .h`**: Parses simple G-code subsets (`G0`, `G1`, `G28`, `M3 S<duty>`, `!`).
- **`motion.cpp / .h`**: Step calculation and coordinated move execution.
- **`homing.cpp / .h`**: Endstop reading and homing routines.
- **`laser.cpp / .h`**: ESP32 LEDC PWM controller for laser diode power.
- **`wifi_server.cpp / .h`**: Blocking Wi-Fi connector and TCP server on port 8888.
- **`status.cpp / .h`**: JSON status telemetry serializer.

### 3.2 Supported G-Code Command Set

| Command | Syntax | Parsed Action |
|---|---|---|
| `G28` | `G28` | Invokes `homing.homeAll()` |
| `G0` / `G1` | `G1 X<val> Y<val> [S<duty>]` | Interpolates move from current position to `(xTarget, yTarget)`. If `S` is present, updates laser duty |
| `M3` | `M3 S<duty>` | Sets laser PWM duty cycle (0–4095) |
| `!` | `!` | Emergency stop: turns laser immediately off |

---

## 4. Gap Analysis: Legacy Code vs. Verified Commissioning Firmwares

A detailed comparison between the legacy firmware/server and the actual tested bench hardware reveals several critical gaps:

| Feature / Subsystem | Legacy Firmware / Server (`laser-engraver-*`) | Current Testing Bench Firmwares (`*_test/`) | Conflict / Gap Severity |
|---|---|---|---|
| **Motor Drive Topology** | **Unipolar 4-phase sequence** (`STEP_X_IN1..4` on GPIO 1, 2, 38, 39; `STEP_Y_IN1..4` on GPIO 40, 41, 42, 9). Intended for direct ULN2003 driving. | **Dual A4988 Bipolar Drivers** requiring single STEP + DIR + ENABLE pins per motor. | 🔴 **CRITICAL**: Pins 38, 39, 40, 41, 42 don't even exist on the ESP32-S3 SuperMini board. Pinout is completely incompatible. |
| **Microstep Control** | None (assumes full/half-step on ULN2003 coils). | Dynamic microstepping bus: `MS1` (GPIO 1), `MS2` (GPIO 2), `MS3` (GPIO 4) allowing up to 1/16 microstepping. | 🟡 **HIGH**: Smooth mirror motion requires 1/16 microstepping to prevent optical jitter. |
| **Current Stepper Pinout** | Legacy X: 1, 2, 38, 39<br>Legacy Y: 40, 41, 42, 9 | **Motor A (X)**: Step=5, Dir=6, En=7<br>**Motor B (Y)**: Step=9, Dir=10, En=11 | 🔴 **CRITICAL**: Compiling legacy firmware would fail to drive the motors or drive incorrect floating pins. |
| **Motion Generator** | `motion.cpp` has a rudimentary loop with `digitalWrite(STEP_X_IN1, (x_ % 2 == 0) ? HIGH : LOW); delayMicroseconds(500);`. It does **not** generate 4-step unipolar sequences correctly nor A4988 step pulses. | Advanced Bresenham DDA multi-axis step generator with acceleration ramps, directional setup delays, and step pulse widths in `stepper_motor_test.ino`. | 🔴 **CRITICAL**: Legacy `motion.cpp` cannot physically step the motor. |
| **Homing Implementation** | `homing.cpp` has dummy stubs: `return true;`. Pins configured to GPIO 11 & 12. | Two-stage precision optical homing in `stepper_ir_homing_test.ino`: Fast seek (120 sps) -> pull-off retract -> Slow latch (40 sps) with debouncing. Sensors on **GPIO 13** (X) and **GPIO 8** (Y). | 🔴 **CRITICAL**: Running homing on legacy firmware does nothing and risks crashing mechanical carriages. |
| **Sensor & LED Conflict** | Configures `STATUS_LED_PIN 13` as an OUTPUT and drives it HIGH. | Sensor X is wired to **GPIO 13** with `INPUT_PULLUP`. Driving GPIO 13 HIGH as an output creates an electrical pin fight with the LM393 comparator on the IR module! | 🔴 **CRITICAL SAFETY ISSUE**: Driving an active-LOW optical sensor output pin as a GPIO output can damage the board or sensor. |
| **Laser PWM API** | Uses legacy ESP32 Arduino Core 2.x API: `ledcSetup()` and `ledcAttachPin()`. | Newer ESP32 Arduino Core 3.x uses `ledcAttachChannel()` / `ledcAttach(pin, freq, res)`. | 🟡 **MEDIUM**: Core 3.x throws deprecation warnings or compilation errors with Core 2.x LEDC calls. |
| **Laser Safety Bounds** | Config allows up to `LASER_MAX_DUTY` (100% / 4095). Server sends up to 100%. | `laser_pwm_test.ino` enforces a **90% safety limit** (`SAFETY_MAX_PCT = 90`) and failsafe auto-off timeouts to prevent diode thermal death and eye hazards. | 🟡 **HIGH SAFETY**: Unbounded power risks burning the 2W diode or overheating the driver board. |
| **Execution Architecture** | Server sends individual G-code lines over TCP/Serial and expects immediate `"ok"`, but `main.py` has **no job streaming executor** to iterate through lines sequentially! | Bench tests use dedicated stateful execution queues. | 🟡 **HIGH**: Uploading an SVG currently generates G-code and stores it in SQLite, but the server has no background worker to actually stream the toolpath to the ESP32. |

---

## 5. Architectural Recommendations & Roadmap

To bridge the gap between the prototype repository code and the proven bench hardware:

```
[Phase 1: Pinout & Safety Harmonisation]
  ├── Update config.h to use SuperMini pins (STEP X=5, Y=9; DIR X=6, Y=10; EN X=7, Y=11)
  ├── Remap IR Sensors to GPIO 13 (X) and GPIO 8 (Y) as INPUT_PULLUP
  └── Remove STATUS_LED on GPIO 13 to prevent sensor pin contention
            │
            ▼
[Phase 2: Firmware Engine Upgrade]
  ├── Port Bresenham DDA step generator from stepper_motor_test.ino into motion.cpp
  ├── Port two-stage fast/slow homing routine from stepper_ir_homing_test.ino into homing.cpp
  ├── Add A4988 microstep pins (MS1=1, MS2=2, MS3=4) set to 1/16 microstepping
  └── Add Core 3.x compatible LEDC wrapper with 90% hard clamp
            │
            ▼
[Phase 3: Server Toolpath Streamer]
  ├── Implement a Job Execution Worker in laser-engraver-server (async streaming of G-code lines)
  ├── Add ping-pong flow control (send line -> wait for ESP32 'ok' -> send next line)
  └── Implement WebSocket streaming of live coordinates & engraving progress
```

---

## 6. Summary of Configuration Matrix

Use this cheat-sheet when updating configuration files:

| Subsystem | Signal Name | ESP32-S3 SuperMini Pin | Logic Level / Specification |
|---|---|---|---|
| **Motor A (X-Axis)** | `PIN_STEP_X` | **GPIO 5** | Rising edge pulse (min 3 µs) |
| | `PIN_DIR_X` | **GPIO 6** | HIGH = CW, LOW = CCW |
| | `PIN_ENABLE_X` | **GPIO 7** | Active LOW (LOW = energized, HIGH = coils off) |
| **Motor B (Y-Axis)** | `PIN_STEP_Y` | **GPIO 9** | Rising edge pulse (min 3 µs) |
| | `PIN_DIR_Y` | **GPIO 10** | HIGH = CW, LOW = CCW |
| | `PIN_ENABLE_Y` | **GPIO 11** | Active LOW (LOW = energized, HIGH = coils off) |
| **Microstepping Bus** | `PIN_MS1`, `MS2`, `MS3` | **GPIO 1, 2, 4** | HIGH, HIGH, HIGH for 1/16 Microstepping |
| **Optical Homing** | `PIN_SENSOR_X` | **GPIO 13** | `INPUT_PULLUP`, Active LOW (Beam broken / Obstacle) |
| | `PIN_SENSOR_Y` | **GPIO 8** | `INPUT_PULLUP`, Active LOW (Beam broken / Obstacle) |
| **Laser Diode** | `LASER_PWM_PIN` | **GPIO 10** | 5000 Hz, 12-bit LEDC, Hard Clamped ≤ 90% duty |
| **Communications** | Serial Baud | 115200 | USB CDC (enable in Arduino IDE Tools) |
| | TCP Socket | Port 8888 | Stream line terminated by `\n` |
