# Laser Engraver Hybrid Web App and Production Firmware Roadmap

**Document type:** Product and engineering construction blueprint  
**Status:** Proposed; hardware and operating-policy decisions remain open  
**Scope:** ESP32-S3 firmware, Python/FastAPI server, SQLite persistence, SVG-to-motion pipeline, and static browser application  
**Primary operator goal:** Safely prepare, calibrate, preview, run, observe, and review an XY laser-engraving job from a browser  
**Implementation rule:** This roadmap does not authorize copying a bench pin map, energizing a laser, or assuming machine dimensions. Resolve the decision gates before the corresponding hardware work.

---

## 1. Executive summary

The intended product is not merely a page with a laser-power slider and a button that sends G-code. It is a small machine-control system with three cooperating layers: a browser-based operator station, a Python control and job-management service, and a real-time ESP32 motion controller connected to physical motors, home sensors, and a laser driver. The product must make its state visible, reject operations that are unsafe or mechanically invalid, and give the operator a truthful account of what the machine knows versus what it only assumes.

The application should have four top-level work areas:

1. **Engrave:** choose an uploaded SVG or a built-in parametric shape, configure the job, inspect the transformed preview and estimated run, confirm readiness, start, pause, resume where safe, stop, and watch progress.
2. **Calibration / Homing:** inspect raw and filtered sensors, move one axis by a bounded number of steps at a conservative rate, run real sensor-driven homing, see the exact phase and outcome, and establish the machine coordinate reference.
3. **Settings and presets:** edit validated axis, travel, motion, homing, and laser configuration; save named configurations; choose one for a job; and compare a temporary job override with the saved preset without silently changing the saved value.
4. **Machine test:** run clearly bounded, low-risk, operator-observed checks for communications, sensors, drivers, motion, and laser PWM response. A motor pulse is not proof that the shaft moved, and an electrical PWM command is not proof that the laser output is safe or optically calibrated.

The current repository already provides a useful skeleton: the firmware is split across configuration, parser, motion, laser, homing, status, and Wi-Fi files; the server is a FastAPI application with SQLite job persistence, serial/TCP bridge implementations, SVG processing modules, and a small static frontend. However, the production firmware is only a prototype: its homing routines return success without moving, its motion driver is not a production-quality stepper controller, the main sketch blocks on Wi-Fi startup, and its parser does not implement the server’s generated command dialect end to end. The current web UI exposes only health, a continuous laser slider, homing, SVG upload, and a preview. The database persists jobs but not presets, user configuration, run events, or machine calibration. The owner has selected a guarded, bounded browser laser pulse as the desired test experience, subject to the physical safeguards and commissioning gate described below.

The owner has confirmed the production motion architecture is A4988-style STEP/DIR with bipolar motors, but the actual production GPIO map is not yet confirmed. The current production `config.h` describes four-wire coil outputs associated with ULN2003-style drive, while the homing test sketch documents dedicated STEP/DIR/ENABLE pins for A4988 drivers and labels itself a separate fixture. The laser test sketch uses GPIO 10, which is also assigned to a motor signal in the source configurations. These are code-level conflicts; they do not prove which GPIOs are physically wired. The owner has selected a discrete UI list of OFF, 5%, 15%, …, 95%, plus a 100% endpoint. That endpoint is a UI selection preference, not approval to exceed the firmware’s separately chosen safe cap; it must remain unavailable above that cap. A production build cannot safely merge these sketches by copying constants. Step 1 must produce a signed-off hardware interface contract, including the exact driver model and pin map, sensor polarity, laser PWM wiring, safe laser-disable behavior, travel reference, and power/interlock policy.

The recommended implementation sequence is deliberately safety-first. First define the physical machine and command contracts. Then build an always-safe, non-blocking firmware base; add truthful sensor and homing behavior; add bounded motor controls and laser control behind independent safety gates; and only then integrate the server, persistent configuration, job lifecycle, richer SVG pipeline, and operator UI. Each milestone is sized to be reviewed and tested independently. Software simulations and mocked hardware prove protocol and state behavior; they do not replace staged bench tests.

The default architectural direction is a single-machine, local-network application. The browser communicates only with FastAPI over HTTP and WebSocket. FastAPI owns persistence, validation, SVG preparation, job admission, and the device bridge. Firmware owns motion timing, sensor sampling, the immediate emergency stop, motion bounds, and laser-off response. There is one authoritative versioned command/status contract between host and ESP32. The controller should not attempt to render SVG, store presets, or decide browser workflow; the server should not attempt to generate step timing; the browser should never speak directly to a raw TCP socket.

The system should have a conservative state model: disconnected, idle-unhomed, idle-homed, calibrating, homing, preparing, running, pausing, paused, stopping, faulted, and recovery-required. All UI buttons, API routes, and firmware commands must agree about which operations are legal in each state. A job must not start because an HTTP request happened to succeed; the server must validate the job, active preset, machine connection, homing state, bounds, and safety conditions, then receive explicit controller acknowledgement before presenting it as accepted or running.

The roadmap includes twelve milestones and a release gate. Steps 1–5 primarily establish firmware behavior and a stable device contract; steps 6–9 deliver server persistence, bridge lifecycle, job preparation, and UI shell; steps 10–11 build calibration/settings and engraving workflows; step 12 hardens and validates the complete product. Some UI and server work can begin in parallel after the contract has been approved, but no parallel stream may invent a competing command schema or pin mapping.

---

## 2. Repository findings and current baseline

This section records what was observed in the repository during planning. It is a planning baseline, not a certification that any hardware has been tested.

### 2.1 Firmware baseline

The production firmware entry point is `laser-engraver-firmware/laser-engraver-firmware.ino`. It constructs `LaserController`, `MotionController`, `HomingController`, and `WiFiServerController`, initializes them, and reads one command from Wi-Fi in its main loop. The serial handling path only parses commands and replies; it does not dispatch the parsed motion, laser, or homing operation. The Wi-Fi setup call currently waits until the network connects, so an absent access point can prevent normal startup. The loop also uses a fixed delay.

The production `gcode_parser.cpp` accepts a small subset of text, detects G28, emergency stop, X, Y, and S fields, and recognizes G0, G1, and M3 prefixes. It does not parse token boundaries robustly, validate every numeric range, or support the server’s generated M5 and M220 commands. Its field extraction can mistake parts of an arbitrary line for values. The parser’s `laserDuty` is an integer, but the API/server semantics need an explicit and consistent unit: percent, raw LEDC duty, or a normalized job power value.

The current `motion.cpp` toggles limited coil pins using a simple interpolation loop; it does not implement a verified coil sequence, independent axis scheduling, acceleration, coordinated timing, limit enforcement, or non-blocking cancellation. The `MotionController` interface accepts laser duty but ignores it. The coordinate update is open loop and does not indicate whether a real sensor reference exists. The current `homing.cpp` configures inputs but `homeX()` and `homeY()` return true without moving or reading an endstop. Therefore, a successful response to current homing must not be interpreted as physical homing.

`config.h` assigns GPIO 1, 2, 38, 39 to X coil outputs and GPIO 40, 41, 42, 9 to Y coil outputs; GPIO 10 is laser PWM; GPIO 11 and 12 are endstop inputs; GPIO 13 is called status LED. These settings describe the present production prototype, not a confirmed safe final wiring. They conflict with the separate PWM test’s GPIO 10 use as laser input and with the A4988 test’s GPIO 10 use as Y direction. The firmware’s Wi-Fi credentials are compile-time macros. This is unacceptable for a shared repository or production distribution; credentials need a local-only provisioning/configuration method.

The current status serializer can format position, laser value, machine state, progress, and two endstops as JSON, but it is not yet integrated into a complete live status transport. The current Wi-Fi server accepts a client and reads a line with a blocking call. It does not demonstrate safe multi-client control, reliable command/response correlation, or robust reconnect handling.

### 2.2 Bench sketch baseline

`stepper_ir_homing_test/stepper_ir_homing_test.ino` is a much richer test fixture. It explicitly documents the target as ESP32-S3 SuperMini, two 28BYJ-48 motors converted to bipolar, two A4988 drivers, two FC-51/KY-032 optical sensors, and no laser. Its X axis uses STEP 5, DIR 6, ENABLE 7; Y uses STEP 9, DIR 10, ENABLE 11; shared microstep pins are 1, 2, and 4; sensors are X GPIO 13 and Y GPIO 8. The sketch documents GPIO 13 as an input only and forbids using it as an LED. Its driver, sensor, and homing logic are valuable requirements and test references, but the sketch is not proof of production wiring compatibility.

That sketch contains a broad operator-oriented command set: enable/disable, discrete steps, continuous motion, stop, speed, absolute moves, position/zero/direction controls, microstep settings, square/circle patterns, sensor reads and diagnostics, calibration, homing, self-test, benchmark, and status. It includes a multi-phase sensor homing state machine and bounded operation concepts. Port only features that have a clear production purpose, preserve their semantics explicitly, and remove or relabel test routines whose output could be misread as closed-loop measurement. `ZERO` is not physical home; a software pulse benchmark is not a shaft-speed measurement; and a stall check without feedback is not electronic stall detection.

The bench test guide is valuable for staged wiring and operator checks. It says that the fixture is laser-free, requires a common ground, warns against hot-plugging motor wires, calls for verifying sensor output voltage against ESP32-S3 input limits, and explains the difference between commanded and measured motor position. Production commissioning material should retain those warnings while replacing any bench-only pin map and component assumptions with the final build’s reviewed wiring diagram.

### 2.3 Laser test baseline

`laser_pwm_test/laser_pwm_test.ino` characterizes a laser driver on GPIO 10 using LEDC, 12-bit duty resolution, and a default 5 kHz PWM frequency. It boots with the laser off and has a safety ceiling concept, defaulting to 90 percent. It includes many characterization features such as power sweeps, pulses, bursts, ramps, frequency sweeps, thermal soak, and status reporting. This is a standalone characterization sketch, not an application API or a production firmware subsystem. Production should retain the tested PWM setup pattern only after verifying ESP32 Arduino core compatibility and the final physical pin assignment. The broad characterization commands should not be exposed as casual web controls; they belong, if retained at all, behind a maintenance mode with explicit physical safeguards.

The requested UI power choices are described as 5%, 15%, 25%, and so forth through 100%. A strict step of ten after 5 yields 5, 15, …, 95; it does not yield 100. The product contract should resolve this as a discrete set of `0, 5, 15, 25, …, 95, 100`, where 100 is a separately offered endpoint, or choose another list. The default plan uses the former only as a proposed UI selection set, with a configurable hard cap below 100 enforced by firmware and hardware policy. The server must never treat a UI value as permission to bypass that cap.

### 2.4 Server and database baseline

The server is under `laser-engraver-server/`. `main.py` provides health, home, laser-power, upload, job-preview, and WebSocket routes. It creates one serial or TCP client based on a small settings dataclass. The API uses wildcard CORS, and the WebSocket endpoint sends status periodically only while receiving text, rather than implementing a clear publish/subscribe stream. Device exceptions are converted to 503 responses for some control routes, but there is no shared command dispatcher, command timeout model, idempotency key, structured machine state, or operation lock.

The `esp32_bridge` directory has TCP and serial adapters. TCP `send_line` treats a response timeout as `"ok"`; serial similarly converts broad read exceptions and empty replies into `"ok"`. This can turn an unconfirmed movement or laser action into a success-shaped result. The production protocol must make timeouts explicit and keep acknowledgement distinct from completion. Device bridge ownership and lifecycle also need to be centralized, because a single client/socket can be touched by HTTP endpoints and background job processing.

The database is a single SQLite file containing a `jobs` table with filename, status, JSON job data, preview bytes, and creation timestamp. It does not yet persist presets, machine configuration, calibration, job events, uploads as managed assets, or schema migration history. Some database functions open and close raw connections without context managers; production upgrades should use explicit transactions, foreign keys where appropriate, busy timeouts, and versioned migrations.

The SVG pipeline has `parser.py`, `transform.py`, `sorter.py`, `gcode_gen.py`, and `preview.py`. The parser tries `svgpathtools` and includes a permissive regex fallback that can infer coordinate pairs from arbitrary numeric content. Transform currently fits point coordinates to configured step ranges, not physical units or calibrated steps/mm. G-code generation inserts G28, M5, M220, M3, G0, G1, and trailing G28, but the current firmware parser does not implement that dialect. The current pipeline does not expose a first-class, immutable prepared-job object or sufficiently rich validation report for the UI.

### 2.5 Static UI baseline

The current static UI under `static/` is intentionally minimal: a sidebar with Home Axes, a continuous power slider, and SVG upload; a status row; an image preview; and an empty G-code preview. JavaScript makes health requests, sends power updates on every slider input event, calls home, uploads an SVG, and updates a few display elements. It has no routing between workflow sections, no robust loading/error state, no control-state gating, no job execution lifecycle, no calibration or settings views, no preset selector, no shape library, no accessible operation confirmation, and no responsive production layout. The current UI is a starting point rather than a design constraint.

### 2.6 Existing tests and practical verification boundary

The server has pytest tests for basic health, SVG upload, parsing, transform, and G-code output. A host-side homing logic test exists in `tests/test_homing_logic.cpp`, but its role and build command should be documented and kept separate from physical ESP32 testing. The repository does not show a complete integration test harness that runs server, firmware simulator, and browser as one system. The new plan adds contract tests and deterministic fake-device behavior before relying on a laser or stepper bench.

All existing uncommitted and untracked files are outside the scope of the plan’s changes. The implementation agent must inspect the worktree before every coding milestone, preserve those user changes, and avoid deleting generated or experimental files unless the owner explicitly requests cleanup.

---

## 3. Product vision and design principles

### 3.1 Product promise

The operator should be able to answer, at any time:

- Is the control server reachable?
- Is the controller reachable over the selected transport?
- Is the machine in a state where it is safe to move?
- Has each axis physically homed since the last event that could invalidate position?
- Which calibration and preset are active?
- What exact design and settings will be executed?
- Is the laser definitely off during travel and when the machine is idle?
- What phase of a job is running, and what did the controller actually acknowledge?
- If an operation fails, what failed, what stopped, and what must the operator do next?

The product must favor explicit, recoverable failures over optimistic state. An uncertain device connection must never be represented as a confirmed stop, a completed home, or a successful job. A timeout must be reported as unknown outcome where the controller may have received the command; it must not be silently converted to success.

### 3.2 Operator journeys

The first journey is **safe setup**. The user starts the server and sees server health separately from controller health. The browser shows the selected serial/TCP transport, controller identity and firmware version when available, and the last successful communication time. The laser state defaults to off. A user can inspect sensor inputs before moving. The UI tells the user that stepper coordinates are open loop and marks coordinates unknown until homed.

The second journey is **machine calibration**. The user selects one axis, checks its sensor, chooses a small bounded move distance and a conservative speed, then commands a jog. The interface indicates movement is active and provides an always-visible stop. The user can establish or update steps-per-unit only through an explicit calibration workflow, not by entering arbitrary conversion factors in an unrelated settings form. Homing runs a documented state machine and reports clear-initial, fast-seek, pull-off, slow-latch, success, or a precise bounded failure. A successful home marks only the completed axis homed.

The third journey is **preset configuration**. The user can inspect built-in safe defaults, clone a preset, modify fields within device and machine limits, validate, save, duplicate, rename, and delete custom presets. The active preset is versioned or snapshotted into each job at job preparation. Editing a preset later must not mutate a job that was already prepared or run. For an on-the-spot custom configuration, the user can select “temporary job settings”; those values are validated and saved in the job snapshot but do not overwrite the chosen saved preset.

The fourth journey is **job preparation and engraving**. The user chooses an SVG from the managed file library or uploads a new one. A built-in shape selector supports common shapes such as square, rectangle, circle, and test grid, with dimensions expressed in confirmed physical units. The server parses and validates the source, resolves SVG units and transforms, fits it into the calibrated work area with configurable margin and origin, generates a preview and an execution plan, and returns warnings and estimates. The operator chooses a preset or temporary settings, reviews the final preview, and explicitly starts. If the device is disconnected, not homed, faulted, or in an unsupported state, the start button is disabled and the reason is visible.

The fifth journey is **testing and recovery**. The user can run sensor read tests, bounded axis jog tests, short dry-run trajectories with laser off, and a narrowly defined laser output test if physical safeguards and operating policy allow it. A test result distinguishes “command accepted,” “firmware output toggled,” “sensor changed,” and “operator confirmed physical movement.” Emergency stop is always prominent. After a connection loss, the job is not automatically resumed. The UI asks the operator to inspect the machine and perform a recovery flow; open-loop position is considered unknown until re-homed.

### 3.3 Product principles

1. **Safe off is the default.** Boot, reset, watchdog, command error, disconnect, and ordinary idle must all tend toward laser disabled. A communications failure is not a reason to leave a laser enabled.
2. **Firmware is the safety boundary for motion.** The browser and server provide usability and policy, but firmware validates ranges, state, homing, travel limits, timing, and immediate stop behavior.
3. **One contract, multiple transports.** Serial and TCP must share the same parser, semantics, state model, errors, and version. Web transport is separate and must not expose raw controller access to browsers.
4. **No ambiguous success.** Accepted, started, progress, completed, aborted, failed, and unknown are distinct outcomes.
5. **Immutable job intent.** Prepared jobs capture the exact design transform, machine calibration revision, preset revision, overrides, and firmware-compatible program.
6. **Truthful telemetry.** Commanded position is not measured position. The UI labels it; it does not present encoder-level precision where no encoder exists.
7. **Bounded operation.** Every move, homing phase, test, upload, SVG complexity, job queue, and retry has explicit limits.
8. **Operator comprehension.** Safety warnings explain the action and consequence in plain language. Avoid confirmations so frequent that operators learn to ignore them.
9. **Local-first deployment.** Default assumptions are one machine and a trusted local network; no cloud account or remote access is required for basic operation.
10. **Test before power.** Contract, simulation, compilation, and dry-run tests precede staged motor and laser commissioning.

---

## 4. Scope, assumptions, and non-goals

### 4.1 Included in the first production release

- One ESP32-S3 controller and one XY machine.
- A reviewed, locked hardware profile with final motor-driver class, pin assignments, sensor wiring/polarity, PWM wiring, motor orientation, and physical work area.
- Serial and Wi-Fi TCP controller transports, with one configured primary transport and explicit fallback procedure.
- Real, bounded, sensor-driven homing for X and Y, including per-axis state and failure reporting.
- Safe bounded jog/move, speed configuration, driver enable/disable where hardware supports it, and the minimum set of motion controls required for calibration.
- A consistent laser PWM interface with zero/off semantics, firmware hard ceiling, and laser interlock policy.
- Versioned device protocol and structured state/event reporting.
- FastAPI routes for health, controller state, homing, jog, stop, presets, file/shape preparation, job lifecycle, and live events.
- SQLite migrations for jobs, presets, machine configuration, and run events, sized for single-user/local-machine deployment.
- SVG validation and transformation into a previewed execution plan with explicit coordinate transform, physical units/calibration, bounds, and warnings.
- Browser areas for Engrave, Calibration/Homing, Settings/Presets, and Tests.
- Basic built-in parametric shapes, including square, rectangle, circle, and test grid, all constrained to the same work-area pipeline as imported SVG.
- Safe dry-run and clear recovery behavior.
- Automated tests at firmware logic, Python unit/API, protocol integration, and frontend interaction/state levels, plus staged physical acceptance checklists.

### 4.2 Deferred or not included unless approved

- Multiple machines, accounts, remote multi-tenant access, cloud job storage, or internet-accessible command APIs.
- Camera alignment, autofocus, rotary axis, Z axis, air assist, enclosure sensors, door interlocks beyond the hardware safety design, and material database.
- Image engraving/raster dithering, DXF or PDF import, bitmap tracing, advanced nesting, and multi-pass material recipes.
- Firmware-over-the-air updates, remote bootloader control, or automatic rollback.
- Automatic continuation after power loss or network failure.
- Closed-loop position or stall detection without adding suitable sensors/encoders.
- Claims of laser safety certification, electrical compliance, or safety-rated emergency stopping from a web button.
- A broad, unrestricted “send any G-code” console or test mode that bypasses validation.

### 4.3 Working assumptions pending user confirmation

The first plan assumes a single machine, a local FastAPI host, a browser on the same trusted network, SQLite, and a single operator at a time. It assumes the final controller board is ESP32-S3 and that the existing Python server remains the host-side control plane. It assumes the two axes are open-loop steppers with optical home sensors and no encoder feedback. It assumes a separate physical laser power supply and shared signal ground are required, but the final laser module input and polarity must be verified. It assumes no change to mechanical hardware until the pin map and safety envelope are reviewed.

The first release should treat a physical, hardware-connected emergency stop and laser-enable/interlock chain as a design requirement. The browser stop button is a software request, not a substitute for a physical emergency stop. The final circuit should make the laser disabled when the controller is unpowered, reset, disconnected, or in a fault state. The exact electrical design requires confirmation against the selected driver and module datasheets.

---

## 5. Target architecture

### 5.1 Component responsibilities

**Browser application**

- Presents machine and server state, workflow navigation, job previews, configuration forms, and operator prompts.
- Sends typed HTTP requests and consumes a single live-event stream.
- Disables operations that the current state does not permit, while still relying on the server and firmware to enforce the same rules.
- Never opens a TCP socket to the ESP32 and never sends raw serial data.
- Maintains an idempotent UI for retries; it does not create duplicate jobs when a request is retried.
- Distinguishes persisted configuration from temporary overrides and from the frozen job snapshot.

**FastAPI application**

- Owns HTTP and WebSocket API schemas, authorization/local-network policy, validation, file handling, SVG preparation, presets, job database, job state transitions, and audit events.
- Owns a single serialized controller command dispatcher. It prevents overlapping incompatible commands and correlates request identifiers with acknowledgements and completion events.
- Starts and closes transport resources in application lifespan management rather than creating unmanaged sockets per request.
- Publishes machine and job events to all subscribed browser clients with bounded buffering and reconnection snapshots.
- Does not claim physical movement; it records the firmware’s reported commanded position and home state.

**Device bridge**

- Implements TCP and serial transports behind a common interface.
- Provides bounded connect, read, write, and response deadlines; explicit disconnected/timeout/protocol-error outcomes; reconnect logic with limits; and orderly shutdown.
- Uses one reader loop for asynchronous controller messages and one command queue to prevent interleaved responses.
- Keeps transport diagnostics out of the command response channel unless explicitly typed.

**ESP32 firmware**

- Boots to a safe output state before starting Wi-Fi.
- Owns pin initialization, sensor sampling/debounce, step timing, homing state machine, coordinate validity, travel limits, laser PWM, safety watchdogs, command parser, and status/events.
- Uses a cooperative/non-blocking event loop for motion, homing, transport input, and stop handling.
- Independently validates every command and refuses moves that exceed configured bounds, conflict with current state, require home when not homed, or violate hard laser limits.
- Reports firmware version, protocol version, build identity, state, fault, position validity, sensor levels, active operation, and laser output state.

**SQLite**

- Persists configuration, presets, prepared jobs, job execution state, and a bounded event/audit history.
- Uses explicit schema migrations and transaction boundaries.
- Stores uploaded source assets in a controlled managed location or database only after choosing a deliberate size/retention approach; do not accidentally store unbounded files or duplicate large previews in every job row.

### 5.2 Interaction topology

```text
Browser
  ├── HTTP: commands, presets, files, prepare/start/stop, snapshots
  └── WebSocket: machine state + job state + events
        │
        ▼
FastAPI service
  ├── Validation and machine/job state machine
  ├── SVG parser → unit/transform resolution → bounds → preview
  ├── SQLite repository + migrations
  └── Serialized controller dispatcher
        │
        ├── TCP newline/framed protocol (preferred only if approved)
        └── Serial protocol (local USB mode)
              │
              ▼
           ESP32-S3 firmware
           ├── Sensor sampling and homing
           ├── Motion planner and motor driver
           ├── Laser PWM and safe gate
           └── Independent stop and fault reporting
```

### 5.3 State ownership and authority

The firmware is the authority for immediate device facts: whether it is currently moving, whether an axis is marked homed, the filtered endstop state, commanded coordinates, whether laser PWM is enabled, and the current firmware fault. The server is the authority for jobs, saved presets, transformed execution plans, browser-facing operation identifiers, and event history. The browser is a presentation and command client, not an authority.

The server may cache the latest controller snapshot, but it must mark it stale after a bounded interval and should expose the timestamp and connection state. On reconnect it requests a full snapshot before accepting ordinary motion commands. If firmware and database state disagree, the server reports the inconsistency and requires a safe reconciliation flow rather than choosing the most optimistic value.

### 5.4 Protocol direction

Use a versioned, newline-delimited JSON protocol over TCP and serial if the ESP32 memory and selected Arduino core permit it reliably. This is preferred over expanding the current G-code subset because the application needs operation IDs, structured errors, detailed homing phases, async progress, firmware identity, and an explicit accepted-versus-completed distinction. If constrained memory or compatibility makes JSON undesirable, define an equally explicit compact framed protocol with a schema and golden tests. Do not mix two competing command dialects.

A representative command shape:

```json
{"v":1,"id":"req-unique-id","type":"home.start","axis":"all"}
```

A representative immediate response:

```json
{"v":1,"id":"req-unique-id","type":"ack","accepted":true,"operation_id":"op-42"}
```

A separate event describes completion:

```json
{"v":1,"type":"operation.completed","operation_id":"op-42","result":"success","homed":{"x":true,"y":true}}
```

The exact protocol is a contract deliverable, not an implementation example to copy without checking ESP32 resource use, line limits, and transport framing. It must define:

- Protocol and firmware version negotiation.
- Maximum frame length, encoding, line endings, and malformed-frame behavior.
- Request IDs, operation IDs, duplicate/retry semantics, and response correlation.
- Command acceptance, completion, failure, abort, timeout, and unknown-outcome semantics.
- Canonical units for position, speed, acceleration, laser percent, and raw duty.
- Safe ranges and invalid-value error codes.
- Status snapshot schema and event names.
- How the host requests a snapshot after connect.
- Whether one TCP client or one host command authority is allowed.
- How emergency stop/abort is handled when the operation connection is unavailable.
- Backward compatibility policy for protocol revisions.

Firmware should report progress asynchronously and must not block waiting for the server to read a reply. If the existing newline G-code interface must temporarily remain, implement it as a versioned compatibility layer that maps only explicitly supported commands into the same internal command types. Do not let it become a second, less-safe execution path.

### 5.5 Machine configuration model

Define a machine profile with immutable hardware facts and separately versioned tuning values. Hardware facts include board revision, motor driver type, pin map, logic levels, sensor polarity, whether axes share a microstep bus, laser PWM pin/frequency/resolution/polarity, and physical safety-gate signals. Tuning includes steps per unit per axis, travel limits, axis direction inversion, homing direction, fast and slow homing speeds, pull-off distance, debounce settings, acceleration limits, default jog speed, and safe laser maximum. Never let a browser form alter pin mappings on a live machine.

A preset should capture supported operator-adjustable engraving settings such as travel speed, engraving speed, acceleration within firmware limits, selected laser level, passes, line interval if raster is later supported, margin, and possibly a material label. A preset must not override hard machine safety limits. Distinguish machine calibration from process presets: a preset is not a mechanism for changing work-area dimensions or bypassing homing.

---

## 6. Functional requirements and acceptance behavior

### 6.1 Calibration and homing area

- Show X/Y raw electrical sensor values and interpreted filtered `detected/clear` states separately.
- Show which polarity is configured, sensor debounce stability, sensor last transition, and whether the value is stale.
- Permit home X, home Y, and home all. `G28`, if retained as a compatibility command, must mean the documented real homing operation.
- Display homing phase, elapsed time, steps/attempts where reasonable, and exact bounded failure reason.
- Provide one-axis jog controls with signed distance, speed, and step/unit display after calibration. Require an explicit valid range and do not permit jog beyond software travel bounds.
- Provide a “set logical zero” only if it is clearly labeled as a software zero and does not claim physical home. Prefer that ordinary users use sensor homing rather than arbitrary zero.
- A homing timeout, seek cap, sensor fault, abort, reboot, or loss of position validity leaves the affected axis unhomed.
- A successful home of X does not implicitly mark Y homed. A home-all failure reports which axis succeeded and which failed.
- When a sensor is already active at start, the firmware uses a bounded clear-initial phase before seeking. If it cannot clear, it fails explicitly.
- No axis starts home movement until the controller confirms it is idle, sensor state is available, the direction and limits are configured, and the requested homing operation is within hard bounds.

### 6.2 Settings and stepper test area

- Expose only settings supported by the confirmed hardware and implemented firmware. A UI control is not “production” merely because a bench command exists.
- Provide safe per-axis jog distance/speed controls, direction display, microstep choice only if driver supports it and changing it cannot invalidate step coordinates, acceleration only if implemented, and driver enable state where available.
- Make configuration edits validate on the server and again in firmware. Display units and allowed ranges near each control.
- Separate “apply to machine now,” “save to preset,” and “use only for this job.” A failed save or device apply must not make the UI show the value as active.
- If both A4988 drivers share microstep pins, document that mode applies to both axes. If final drivers are ULN2003 or another architecture, do not display A4988-specific STEP/DIR controls or microstep settings.
- Expose tests as finite operations with maximum duration/steps and a visible cancellation path. Tests may report physical verification as operator-confirmed, not sensor-inferred, unless feedback hardware actually proves it.

### 6.3 Laser configuration and testing

- Present a clearly labeled discrete set of power levels. The initial proposal is `OFF`, `5%`, `15%`, `25%`, …, `95%`, and a terminal `100%` option, with 100% disabled unless the hard safety cap and operator policy explicitly allow it.
- Show both requested percent and normalized/raw duty only in an advanced diagnostic view. Do not imply that PWM duty equals optical output power without calibration data.
- Enforce maximum duty in firmware, with a lower default commissioning cap and an explicit local configuration decision. Server and UI limits are convenience validation, not the safety authority.
- Boot, reset, idle, command parse failure, invalid state, watchdog, emergency stop, and communication-loss behavior must disable laser output according to the reviewed hardware design.
- Manual laser output testing is a hazardous operation. A production UI should not create an indefinite “on” state from a slider. Prefer a controlled, momentary pulse or short test with laser shield/door/interlock conditions verified, a physical key/enable policy if appropriate, and clear operator confirmation. The test must always end off, including timeout and disconnect.
- During engraving, the controller must ensure laser off during travel and at all stops, between paths where required, and after job completion or fault. The server’s generated program and firmware motion integration must be tested to prove the intended on/off sequence.
- Laser frequency changes, pulses, ramps, bursts, and thermal soaks belong to maintenance characterization, not the ordinary engraving screen, unless there is a separate approved, guarded maintenance mode.

### 6.4 Engraving and job preparation

- Support SVG upload, a managed list of previous files, and built-in vector shapes. An upload is not executed immediately.
- Validate SVG size, XML structure, namespace, external references, path complexity, transforms, units, dimensions, and unsupported features. Reject or warn on text requiring fonts, image references, filters, clipping, masks, stroke semantics, or other unsupported constructs instead of silently changing geometry.
- The preview must show the machine work-area boundary, origin, margins, transformed design, and any clipping/out-of-bounds warning. The operator can choose fit, scale, offset, and optionally rotate/mirror only within validated limits.
- Provide a path/shape count and conservative duration estimate, including travel and laser-on time where possible. Label estimates as estimates.
- Use the same immutable prepared plan for preview and execution. Do not regenerate with potentially changed defaults after the operator presses Start.
- Built-in square, rectangle, circle, and calibration grid are generated as vector geometry in the server domain and run through the identical transform, bounds, preview, and validation steps as uploaded SVG.
- A prepared job stores source asset identity/hash, source filename, parsed geometry or execution plan, transformed preview, selected machine calibration revision, preset revision, per-job overrides, firmware protocol requirements, warnings, and a deterministic plan hash.
- Start requires connected controller, idle machine, required axes homed, no active fault, safe laser state, valid preset/config, and explicit confirmation of the final plan.
- Pause and resume are only included if firmware can pause at a safe motion boundary, turn laser off, preserve trustworthy position, and resume without executing a stale or ambiguous segment. Otherwise expose stop and require re-home/restart.
- On cancel, controller disconnect, reset, watchdog, or job error: stop issuing new segments, turn the laser off, stop motion using the defined safe behavior, record the terminal state and reason, and require operator inspection before another job.

### 6.5 Status, diagnostics, and logs

- The dashboard presents server status, controller status, transport, firmware/protocol version, last-seen time, machine state, homed flags, sensor states, laser output state, active operation/job, and latest fault.
- State changes are broadcast via a WebSocket event stream; initial subscription always receives a full snapshot.
- Server records structured events for operator actions, controller acknowledgements, status transitions, homing outcomes, job transitions, validation warnings, and faults. Avoid recording secrets or unnecessary personal data.
- Diagnostics have a bounded retention policy and export format suitable for support. They should include timestamps and operation IDs to correlate browser, server, and firmware traces.
- The UI labels loss of connection as stale/unknown, not merely offline if the last known machine state might still be active. It communicates the safe action to take, including physical stop if necessary.

---

## 7. Milestone plan

The sequence below is a recommended twelve-step build. Each step should be a separate, reviewable change set unless repository constraints justify a small grouping. The agent doing an individual step should read its named source files and relevant protocol/decision records, verify current worktree status, and not rely on conversation history. Do not begin hardware-dependent implementation until Step 1’s hardware gate is resolved.

### Step 1 — Confirm hardware profile, safety boundaries, and protocol contract

**Objective and context.** Establish the factual baseline on which firmware, server, and UI depend. The repository currently documents a production wiring profile and separate A4988 and laser test sketches that overlap or conflict on pin assignments. No later change can safely fix homing or PWM until the actual driver hardware and final pin map are confirmed. This milestone should produce a concise, reviewable configuration contract and not rewrite the prototype code prematurely.

**Dependencies.** None. This is the prerequisite for all hardware-specific implementation.

**Work items.**

1. Inspect the physical assembly, board revision, motor wiring, driver modules, sensor modules, laser driver, power supplies, and actual GPIO connections. Photograph or diagram the confirmed wiring if the project documentation workflow permits.
2. Resolve whether production motors are driven by four coil wires through ULN2003-class boards, bipolar STEP/DIR drivers such as A4988, or another driver. Confirm the actual motors’ winding connection and do not infer from test sketches.
3. Create a pin-allocation table with one row per GPIO, signal, direction, voltage domain, boot level, and conflict check. Identify strapping/USB/boot-sensitive pins on the exact ESP32-S3 module.
4. Confirm sensor electrical output voltage, active polarity, pull-up requirements, sensor mounting position, travel direction toward home, and what physical flag triggers each sensor.
5. Confirm laser module PWM input range, active polarity, minimum/maximum useful frequency, default-off electrical state, and how a physical interlock or key switch gates laser enable. Do not power the diode during logic-only checks.
6. Measure or mechanically establish usable work-area travel, axis steps-per-unit calibration procedure, safe approach directions, and the coordinate origin. Do not turn undocumented step counts into advertised millimeters.
7. Confirm deployment assumptions: host operating system, Wi-Fi mode, whether TCP or USB serial is intended for production, network trust boundary, and whether browser and server run on the same local network.
8. Approve protocol version, units, acknowledgement semantics, state definitions, hard travel and speed limits, maximum laser duty, line/frame size, retry/idempotency behavior, and emergency stop semantics.
9. Create a shared golden contract corpus for valid and invalid requests, responses, status snapshots, events, bounds, and protocol-version negotiation. The contract must require firmware and server to reject unsupported command types explicitly; an unknown command must never be accepted as a no-op or translated into success.
10. Freeze this contract before any server or UI work beyond isolated mock/simulator scaffolding. A change after freeze requires a versioned contract update and coordinated firmware/server tests, not an undocumented dialect extension.
11. Record unresolved items as explicit blockers, owners, and acceptance evidence. Do not silently fill a pin map or maximum duty with a guess.

**Deliverables.** Hardware profile and pin table; safe-start and stop policy; machine limits and unit definitions; frozen controller protocol schema plus shared golden valid/invalid examples; a decision log for unresolved items; update of directly related wiring/commissioning documentation.

**Verification.** Review the source documentation against physical wiring; check that each GPIO has exactly one production use; compile-time profile checks reject duplicate pins where practical; protocol golden examples validate against the chosen schema and are consumed by both firmware and server tests; unknown/unsupported commands are rejected; a review confirms all three current sketch pin maps are labeled as production, characterization, or bench-only.

**Exit criteria.** Hardware owner signs off driver type, pin map, sensor wiring/polarity, PWM output, travel calibration basis, and laser-disable policy. Protocol consumers can independently generate the same valid command and status examples, reject the same invalid/unsupported commands, and pass the shared golden corpus. No unsafe conflict remains between laser PWM and motor pins. If a fact cannot be confirmed, downstream hardware work is blocked rather than guessed.

**Rollback.** This is a documentation/contract change. If the physical machine differs from the assumed map, revise the profile and schema before merging firmware changes; do not preserve an incorrect pin assignment for compatibility.

### Step 2 — Create fail-safe firmware foundation and command lifecycle

**Objective and context.** Replace the current blocking startup and single-line Wi-Fi read foundation with deterministic startup, bounded transports, an explicit state model, and safe command lifecycle. The work should establish the framework before moving motors or enabling PWM. It should use the approved hardware profile from Step 1 and keep test-specific firmware isolated.

**Dependencies.** Step 1.

**Work items.**

1. Refactor initialization so laser and motor outputs are driven to approved safe levels immediately, before Wi-Fi setup, serial logging, or long startup work. Validate the reset/boot electrical behavior as well as software writes.
2. Remove repository credentials from compiled shared configuration. Support local ignored configuration, provisioning, or another approved secret-injection approach; fail visibly if Wi-Fi is not configured while preserving USB serial operation.
3. Replace blocking `readStringUntil` and unbounded connection loops with bounded input buffers and cooperative service. Reject overlong lines/frames entirely rather than parsing truncated prefixes.
4. Implement one parser for all transports. Parse into typed command structs, validate every field and range before side effects, distinguish missing values from zero, and return stable error codes.
5. Define one firmware state machine: booting, idle-unhomed, idle-homed, homing, calibrating, moving, running-job, stopping, faulted, and recovery-required as appropriate. Document exact transitions.
6. Create a command dispatcher that validates legal state and transport authorization before calling internal behavior. Status and stop remain available while busy; other commands receive `BUSY` or a state-specific error.
7. Implement explicit request IDs and operation IDs, with acknowledgement for acceptance and separate completion/failure events for long-running commands. Replayed IDs should not accidentally run the same move twice.
8. Provide full startup identity and snapshot messages after host connection: firmware version, protocol version, hardware profile identifier, safe output state, current state, homed flags, sensor state, laser state, and fault.
9. Implement deterministic reset/fault handling, including loss of Wi-Fi and serial disconnect. Define which operations abort, how outputs go safe, and which position flags are invalidated.
10. Keep diagnostics separate from machine protocol frames; do not print free-form serial strings into a TCP response stream.
11. Keep all server and browser work that proceeds in parallel strictly on a fake controller or an explicitly laser-disabled profile. Remove or hard-disable any existing production route that can set laser output until Step 5 has passed its hardware safety gate; a UI slider or API endpoint must not reach the real PWM output early.

**Deliverables.** Firmware transport abstraction, parser/dispatcher, state definitions, protocol examples or schema shared with server, safe boot logic, configuration injection pattern, and firmware unit/host tests for parsing and transitions.

**Verification.** Compile using the approved ESP32 board and core; run host tests for valid, malformed, oversized, boundary, duplicate-ID, busy, and abort commands; verify serial remains available when Wi-Fi cannot connect; use a logic analyzer or equivalent to verify safe output levels at boot/reset; test two TCP clients and confirm the policy is explicit and deterministic.

**Exit criteria.** Firmware starts safely with no network, reports its identity, rejects invalid and conflicting requests without output changes, preserves responsive status/stop while an operation is active, and does not report completion on mere acceptance. No actual motor or laser movement is required in this step. The production server cannot energize the real laser through any route before the Step 5 hardware safety gate passes.

**Rollback.** Keep the existing production sketch buildable in a separate commit/tag until the new firmware skeleton compiles on the confirmed target. If a protocol change breaks a client, stop feature integration and restore contract compatibility before proceeding; do not hide mismatches behind success fallbacks.

### Step 3 — Implement axis driver abstraction and bounded motion primitives

**Objective and context.** Implement the motion backend for the hardware profile confirmed in Step 1. The production `MotionController` currently toggles only limited pins and blocks while interpolating. The A4988 bench logic is useful only if the production hardware is confirmed to use that driver path. This step establishes safe one-axis and coordinated motion without yet relying on homing success.

**Dependencies.** Steps 1 and 2.

**Work items.**

1. Introduce an axis/driver abstraction whose implementation matches the actual driver. For ULN2003-style coil drive, implement an explicit validated coil sequence and timing; for STEP/DIR drivers, implement pulse width, direction setup/hold time, enable polarity, and microstep support against the selected driver’s documentation.
2. Initialize all motor outputs to a safe state and keep drivers disabled until a validated operation requires them. Define whether disable loses holding torque and whether that invalidates coordinate trust for the actual mechanics.
3. Implement non-blocking motion scheduling. A loop pass must service communications, sensors, stop requests, and watchdogs. No large movement may run inside a blocking `for` loop or delay-driven routine.
4. Implement bounded single-axis jog and absolute/relative moves with clear unit semantics, maximum steps/distance, maximum speed, acceleration policy, and travel limits.
5. Implement coordinated X/Y interpolation appropriate for the actual mirror mechanism. Verify axes start and finish consistently, with a known error tolerance and no accumulated off-by-one steps.
6. Implement per-axis direction inversion and calibration fields only where required. Configuration changes that alter step scale or microstep mode invalidate or rescale coordinates deliberately; never silently leave an apparently valid home coordinate in a new unit system.
7. Make all motion abortable by physical stop input, software stop, disconnect policy, watchdog, sensor/limit event, or fault. Define whether stop is controlled deceleration or immediate pulse cancellation and verify laser-off ordering.
8. Keep `position_steps` separate from calibrated physical units and `position_valid`/`homed`. Document that no encoder confirms shaft movement.
9. Add finite test operations such as a small jog, axis polarity check, and coordinated square/circle only if they are useful for alignment and can be bounded. Label software-only benchmarks honestly.
10. Add compile-time guards or runtime checks for impossible configurations, including invalid pin overlap, unsupported microstep divisor, zero speed, or travel range overflow.

**Deliverables.** Production axis driver interface, motion scheduler, coordinate validity behavior, typed motion commands, bounded test command set, firmware tests, and revised wiring and commissioning guide for the approved driver.

**Verification.** Run host tests for step counts, signs, bounds, coordinates, interpolation, abort paths, and invalidation. Compile against the approved ESP32 core. With laser power disconnected and motor supply initially off, verify GPIO direction and safe levels; then enable one driver/axis at a time at conservative limits and confirm the physical direction, repeatability, heating, current, and stop response. Verify the second axis and coordinated moves only after isolated tests pass.

**Exit criteria.** No movement command can exceed configured bounds; stop can be serviced while a move is active; expected step pulses and physical direction agree with the reviewed machine profile; position is never described as measured feedback; and an interrupted or untrusted movement marks position invalid.

**Rollback.** Keep the driver abstraction boundary so a hardware-profile change replaces the backend instead of rewriting protocol, server, or UI. If physical direction or current is uncertain, disable the profile and return to logic-only testing rather than tuning by trial with a powered laser.

### Step 4 — Add sensor diagnostics and real sensor-driven homing

**Objective and context.** Replace the production homing stubs with a real, observable, bounded state machine. The combined test sketch contains the strongest reference for sensor filtering and homing concepts, but its pin assignments and driver behavior are not imported unless the approved production profile matches. Homing is a core prerequisite for trustworthy engraving coordinates.

**Dependencies.** Steps 1–3.

**Work items.**

1. Read each sensor’s raw electrical level and separately track polarity-interpreted candidate and stable state. Use non-blocking debounce based on repeated samples and elapsed time.
2. Expose sensor health and diagnostics: raw level, stable state, transition counters, last change, unstable/jitter count, and stale-sample status. Do not infer a healthy sensor solely because its line reads high or low.
3. Define axis-specific home direction, fast seek speed, slow latch speed, pull-off distance, maximum seek travel, per-phase timeout, sensor polarity, and debounce values in the approved profile. Set conservative commissioning defaults and never infer an actual machine range.
4. Implement phases such as validate, clear-initial, fast-seek, pull-off, slow-latch, complete, aborted, and failed. Advance at most bounded motion each loop iteration.
5. Bound every phase by both a time limit and a step/travel limit. Use wrap-safe elapsed-time comparisons and reject a configuration where speed and timeout could defeat the cap.
6. Require the sensor to be stable for the defined window before phase changes. Stop motion promptly when detected. A stuck-active input must fail during clear-initial; a never-triggering sensor must fail at seek bound; repeated chatter must not create success.
7. Preserve per-axis result and partial home-all outcome. If X succeeds and Y fails, report that X remains homed and Y is not; never send an overall success-shaped response.
8. Invalidate `homed` on abort, timeout, limit failure, driver fault, reset, configuration change affecting coordinates, or any event where motion may have been lost.
9. Provide physical homing commands and an unambiguous response lifecycle. If G28 is supported, map it only to actual sensor homing; document the exact equivalent command.
10. Provide a calibration wizard that asks the operator to confirm low-speed direction and sensor activation before a full homing run. A software wizard cannot infer direction or sensor alignment without feedback.

**Deliverables.** Sensor-filter module, homing state machine, status/event schema updates, fault codes, unit tests, hardware commissioning and fault-injection test procedure, and clear distinction between physical home and logical zero.

**Verification.** Host-test already-active sensor, initially-clear sensor, normal fast-seek/pull-off/slow-latch, no trigger, stuck active, polarity inversion, noise/chatter, timeout, step limit, abort in every phase, TCP loss, serial stop, single-axis home, and partial home-all. On hardware, initially test sensors with motor power off; then home one axis at slow speed, verify clear/latch sequence and repeatability across multiple cycles, then test the second axis and home-all. Never add the laser to these homing tests.

**Exit criteria.** Every apparent success includes actual stable sensor transitions from the correct axis; every failure yields a bounded result and leaves the affected axis unhomed; abort remains responsive; and the reported coordinate origin matches the physical reference validated by the operator.

**Rollback.** If the physical sensor cannot be trusted, retain diagnostic reads and disable production homing rather than returning true. Firmware may expose a maintenance-only test state, but the engraving start gate must refuse jobs unless both required axes have verified home state.

### Step 5 — Integrate laser PWM behind a fail-safe output policy

**Objective and context.** Port only the necessary LEDC behavior from the laser characterization sketch into production firmware. The test uses GPIO 10, which conflicts with existing motor allocations in both code paths; the final pin must come from Step 1. The goal is controllable engraving output with safe-off behavior, not reproducing every characterization command as a web feature.

**Dependencies.** Steps 1–4.

**Work items.**

1. Implement a hardware-profile-specific PWM initialization path that supports the selected Arduino core API version and verifies successful attach/configuration. Record frequency, resolution, active polarity, and duty conversion semantics.
2. Force output off before and after PWM setup. Verify reset and watchdog electrical behavior so an unconfigured GPIO cannot float into laser-on state.
3. Define one canonical external value, preferably integer percent or a bounded normalized level. Convert to raw duty in one tested helper. Reject invalid values; do not silently clamp except where the agreed interface explicitly specifies saturation.
4. Enforce a firmware hard ceiling independent of server and browser limits. Make the commissioning ceiling the default. Any mechanism to raise it must require a local, deliberate maintenance configuration, with a documented restart/application effect and safety review.
5. Define laser state separately from requested power: `requested_level`, `output_enabled`, `interlock_ok`, and `fault`. Setting a desired power must not by itself turn the laser on outside an approved short test or active engraving segment.
6. Add explicit laser-off behavior for boot, idle, invalid command, emergency stop, end of every path/job, motion travel, timeout, reset/watchdog, disconnect according to policy, and fault. Ensure stop precedence is unambiguous.
7. Design a guarded manual laser test. Prefer a bounded pulse with a maximum duration, explicit operator action, physical interlock/key policy, displayed safety warning, and forced end/off confirmation. Do not implement a slider that keeps the diode continuously on as a routine settings interaction.
8. Implement firmware-level interlocks required by the approved hardware design. If no enclosure/door feedback exists, do not invent one in software; document the physical limitation and restrict the feature set appropriately.
9. Add laser PWM diagnostics that can report commanded percent/duty and output configuration. Do not claim optical power or material performance unless calibrated with appropriate external measurement.
10. Ensure job motion can request laser output only through the same safety gate and only for a valid, homed, active job segment.

**Deliverables.** Laser controller module, safe duty conversion, hard cap policy, guarded test interface, state/fault status, unit tests, and an updated laser test guide that separates electrical commissioning from optical characterization.

**Verification.** Test conversion at 0, supported levels, just below/at/above the cap, invalid negative and >100 values, output off on every state transition, pulse timeout, abort, reset, network loss, and PWM attach failure. First test PWM with laser supply physically disconnected and measure the signal with a suitable instrument. Only after a separate safety checklist and approved enclosure/interlock setup should optical output be characterized at low power with appropriate protective controls.

**Exit criteria.** A request to change the selected power cannot fire an idle laser; all error/stop paths force output off; the hard ceiling is enforced in firmware; supported power values and any 100% endpoint match the approved policy; and tests prove server/browser cannot bypass the gate. The production capability remains disabled until the approved hardware profile passes the staged electrical-off, hard-cap, interlock, and output-off acceptance checks on the assembled machine. Record that evidence and profile revision before Step 7 or Step 11 can enable any real laser actuator path.

**Rollback.** Preserve the characterization sketch as an isolated laboratory tool. If production PWM cannot be verified, disable laser output in the production profile and continue motor, SVG, and UI work in dry-run mode.

### Step 6 — Add server schemas, SQLite migrations, presets, and machine configuration

**Objective and context.** Introduce durable, typed application models before adding UI settings controls. The current SQLite database stores jobs only, while `Settings` is an in-memory dataclass with hardcoded defaults and the existing `laser_config.py` contains constants. This step defines how machine configuration, presets, prepared jobs, and events persist without putting hardware facts into an engraving preset.

**Dependencies.** Step 1’s domain/protocol decisions; firmware work can continue independently after contract approval. Any bridge, settings, or API work before Step 5 must use a fake controller or laser-disabled profile and must not send a real laser-enable/PWM command.

**Work items.**

1. Choose and document a migration mechanism already compatible with the project. Add a schema-version table and deterministic upgrade path from the existing `jobs` table. Preserve prior job records.
2. Define typed request/response models for machine profile, editable calibration, preset, temporary override, prepared job, machine snapshot, operation, job event, and validation issue. Keep units explicit in names or schema metadata.
3. Split immutable hardware profile facts, calibrated machine parameters, and process presets. Enforce that presets cannot change GPIO assignments, hard travel limits, or firmware hard laser caps.
4. Create tables and indexes for presets, machine configuration revisions, prepared jobs/run snapshots, source asset metadata, and job/event history as justified by the first release. Avoid prematurely generalizing to multi-tenant or distributed storage.
5. Make preset save/update/delete transactional, validate uniqueness and referential behavior, and prevent deletion of a preset referenced by historical jobs unless snapshots fully preserve its meaning.
6. Store an immutable settings snapshot and plan hash with each prepared job. Later preset changes must not modify an already prepared or completed job.
7. Decide asset storage policy and limits. Validate names, size, media type, content, and retention; avoid unsafe path construction from a user filename.
8. Add a seeded default safe preset only after the hardware profile’s safe ranges and cap are approved. Clearly mark built-in versus user-defined presets.
9. Replace scattered hardcoded values with one settings load path, validated at startup. Report missing or invalid required configuration explicitly; do not substitute success-shaped defaults.
10. Make database operations use transaction/context management, parameterized SQL, explicit busy timeout, and predictable errors. Keep SQLite as a single-host deployment target.
11. Keep any persisted power field as a requested, validated configuration value only until the Step 5 safety gate passes. Persisting a setting must not create an actuator path.

**Deliverables.** Versioned DB migrations, Pydantic/domain models, repository layer, default configuration strategy, preset CRUD, job snapshot persistence, migration tests, and documentation for local configuration/backup.

**Verification.** Upgrade a copy of the current database and verify existing jobs remain readable; test fresh DB and each migration; test invalid ranges, duplicate names, deletion constraints, transaction rollback, immutable job snapshots after preset edits, corrupted config, and concurrent read/write behavior within the expected single-process deployment.

**Exit criteria.** A fresh install and existing installation both initialize deterministically; invalid hardware configuration fails visibly; presets cannot alter hardware safety constants; jobs preserve their exact settings independently of later changes; and migration does not drop existing jobs or previews.

**Rollback.** Each migration has a documented recovery/backup path. For a failed release, restore from a verified backup or ship a forward-fix migration; do not instruct users to delete the database as the normal remedy.

### Step 7 — Build the reliable controller bridge, API, and live status channel

**Objective and context.** Replace ad hoc API-to-client calls and silent acknowledgement fallbacks with one managed bridge. The current API treats connect/send per route, catches hardware errors in individual endpoints, and the transport clients may return `"ok"` on timeout or empty response. The production server must expose consistent error and operation semantics to every browser feature.

**Dependencies.** Steps 1, 2, and 6; firmware integration against the command contract from Step 1 and implemented in Steps 2–5. Until Step 5 passes its actual-hardware safety acceptance, this bridge and all connected UI/API paths are simulator-only or laser-disabled. The legacy `/api/control/laser/power` path must be removed or return an explicit not-commissioned error, and cannot reach real PWM.

**Work items.**

1. Define a common asynchronous controller interface for connect, close, request, event stream, snapshot, and health. Adapt serial carefully if its implementation is blocking; isolate it with a thread/async-safe queue rather than blocking FastAPI’s event loop.
2. Instantiate and own the controller bridge through FastAPI lifespan. Guarantee one reader loop, serialized command writes, bounded queues, and clean shutdown.
3. Model connection states: configured, connecting, connected, degraded, disconnected, reconnecting, protocol-incompatible, and faulted. Report last successful communication and controller identity.
4. Implement explicit connect/read/write deadlines, retry/backoff bounds, disconnect handling, response correlation, cancellation, and duplicate request protection. A timeout yields an explicit unknown outcome if delivery may have occurred.
5. Replace per-route device calls with a shared operation service and legal-state checks. The service should serialize state-changing commands and make emergency stop highest priority.
6. Implement health/snapshot endpoints and routes for home, jog, settings apply, sensor diagnostics, test operations, stop, and later job lifecycle. Return typed error bodies with stable code, message, operation ID, retryability, and machine state.
7. Add a WebSocket broadcaster that sends a full initial snapshot, then machine/job events; handle subscriptions, heartbeat, disconnect, reconnection, and bounded client queues without requiring arbitrary browser text to trigger status.
8. Remove wildcard CORS for deployment defaults. Allow same-origin operation by default; configurable origins must be explicit. Bind host/port and network exposure through configuration.
9. Add a local control authorization decision. If any network beyond a trusted private local host is possible, require authentication and CSRF-aware browser design before allowing motion. Do not assume CORS alone is authorization.
10. Provide a simulated controller adapter for deterministic tests, with scenario controls for disconnect, timeout, homing failure, sensor transitions, and job completion.
11. Add a server-side production capability gate that refuses all laser-output commands and laser-bearing job execution until the approved hardware profile is marked commissioned and the Step 5 safety acceptance is recorded. A fresh install, missing profile, or profile mismatch defaults to disabled. A browser flag or ordinary API setting cannot clear this gate.

**Deliverables.** Bridge manager, protocol codec, typed API routes, machine-operation service, event broadcaster, fake controller, and operator-facing API contract documentation.

**Verification.** API tests for all response/error states; fake-device tests for acknowledgement vs completion, timeout unknown outcome, duplicate request, stale snapshot, reconnection, busy state, stop priority, multi-WebSocket subscriptions, and server shutdown; serial tests with a pseudo-terminal where feasible; TCP loopback tests with a fake firmware server.

**Exit criteria.** No transport returns success when it has no acknowledgement; only one command dispatcher touches the controller; stale/disconnected state is explicit; WebSocket clients can recover from missed events via a snapshot; and the same command state machine governs serial and TCP. Before Step 5 hardware sign-off, attempts to use any real laser-output or laser-bearing job route are rejected server-side with an explicit not-commissioned error.

**Rollback.** Keep the bridge behind an interface and retain a simulation mode. If the new protocol cannot interoperate with firmware, block live control and use the simulator; never revert to silent `"ok"` fallbacks to make the UI appear connected.

### Step 8 — Rebuild SVG/shape preparation as validated immutable job plans

**Objective and context.** Make imported and generated geometry safe to preview and execute. The current parser has a fallback that extracts numeric pairs from arbitrary SVG text, the transform uses steps as the work-area unit, and G-code generation emits commands unsupported by the current firmware. This step establishes a plan format that is independent of a particular rendering widget or old G-code assumptions.

**Dependencies.** Steps 1 and 6; Step 7 protocol contract for execution representation.

**Work items.**

1. Define supported SVG feature scope and failure behavior. Parse XML with a hardened parser, reject external entities/references, bound document size, path count, segment count, and computed point count, and avoid silently treating malformed SVG text as geometry.
2. Resolve `viewBox`, width/height units, nested transforms, path commands, and units into a normalized design coordinate system. Handle unsupported text, clipping, masking, filters, images, and stroke/fill interpretation with explicit warnings or rejection.
3. Convert geometry into a deterministic vector plan with explicit physical units, machine origin, steps-per-unit calibration revision, path ordering, travel segments, laser-on segments, configured speeds, and bounds.
4. Define fit-to-work-area behavior: preserve aspect ratio by default, margin, origin anchoring, optional rotate/mirror, center/offset controls, and out-of-range response. A requested transform that clips or exceeds travel must be rejected or require explicit user correction, not silently truncated.
5. Improve path ordering only where it preserves semantics. Reversing a path may change direction-sensitive process behavior; make path optimization opt-in or prove it is safe for supported vector engraving.
6. Implement built-in parametric shapes as source geometry: square, rectangle, circle, and a calibration grid. Validate positive dimensions, maximum size, and spacing. Use the same transform and validation as SVG.
7. Generate a raster/vector preview from the exact immutable plan. Draw machine boundary, coordinate origin, margin, travel path distinction, and out-of-bounds warning. Persist preview metadata and a deterministic plan hash.
8. Estimate run time from move lengths, speeds, acceleration assumptions, and path transitions. State assumptions and confidence; do not use the estimate for safety limits.
9. Produce a firmware-compatible execution representation. If a G-code subset remains, specify supported commands and parse/generate round-trip tests; otherwise use typed protocol commands with bounded trajectory blocks. Do not leave M5/M220 or coordinate-unit ambiguity unresolved.
10. Return structured validation warnings/errors for unsupported geometry, unusual scale, tiny dimensions, excessive complexity, questionable laser-on travel, and estimated overrun.

**Deliverables.** Safe parser, normalized geometry representation, deterministic plan generator, shape library, preview updates, plan/preview persistence, execution serializer, and comprehensive parser/geometry tests.

**Verification.** Unit/property tests for SVG transforms, relative/absolute paths, curves, arcs if supported, units, nested transforms, empty/degenerate geometry, negative coordinates, large coordinates, malformed XML, external resources, oversized inputs, pathological segment count, shape dimensions, aspect preservation, boundaries, deterministic output, and laser-off travel. Golden tests prove the preview and emitted execution plan use identical transformed coordinates. Run a dry-run plan through the fake firmware and verify all points stay inside bounds.

**Exit criteria.** No untrusted SVG can trigger arbitrary resource access or unbounded computation; preview and execution plan are identical in geometry; the user sees every nontrivial warning; all paths fit validated machine limits; and firmware supports every emitted command.

**Rollback.** Unsupported SVG features are rejected with actionable messages rather than passed through a numeric regex fallback. Keep the existing upload API only as an adapter if old clients rely on it; do not let the fallback bypass safe parsing.

### Step 9 — Build the accessible web application shell and machine dashboard

**Objective and context.** Replace the current single sidebar page and unstructured fetch handlers with a coherent, state-aware workflow shell. Keep the static deployment approach initially to avoid an unnecessary frontend-stack migration. Use progressive components and plain JavaScript/TypeScript choices consistent with repository constraints; no framework is required just to build navigation. Until Step 5’s actual-hardware gate passes, any power control is mock-only or read-only requested configuration; no live slider or button may operate the production PWM output.

**Dependencies.** API schemas from Steps 6–8. The visual shell and simulated status can progress in parallel with server implementation once the contract stabilizes.

**Work items.**

1. Define navigation for Engrave, Calibration/Homing, Settings/Presets, Tests, and optionally History/Diagnostics. Keep the active machine state and emergency stop visible across all sections.
2. Design a responsive layout for desktop/tablet and a safe simplified mobile read-only/control experience. Avoid placing hazardous Start/laser actions near accidental touch targets.
3. Create a typed API client with common error parsing, timeout/abort behavior, request IDs, and no unhandled promise rejections. Avoid firing hardware commands continuously from slider `input` events.
4. Create a single connection store sourced from server snapshots and WebSocket events. Track freshness and stale state. Reconcile on reconnect by fetching a full snapshot.
5. Build reusable status indicators for server, ESP32, transport, firmware version, homed X/Y, sensor states, laser state, current operation, and active fault.
6. Add explicit loading, empty, disabled, success, warning, and failure states to every machine action. Explain disabled reasons directly next to controls.
7. Implement operation progress and notifications with operation IDs and stable outcome semantics. Do not announce job success until server sees controller completion.
8. Add keyboard navigation, visible focus, semantic headings, labels, live-region announcements for state changes, sufficient contrast, reduced-motion support, and accessible confirmation dialogs.
9. Ensure WebSocket events cannot cause unsafe duplicate actions or stale form submission. UI state is a mirror, not the enforcement point.
10. Keep the UI’s language consistent: “commanded position,” “home verified,” “laser output off,” “server connected,” and “controller state unknown” are distinct and accurate phrases.

**Deliverables.** Application navigation, responsive shell, API/event client, machine dashboard, accessibility checks, and component/state tests using the simulated API.

**Verification.** Browser tests at desktop/tablet/mobile widths; keyboard-only navigation; screen-reader-oriented semantic/ARIA checks; stale connection and reconnection scenarios; server and controller disagreeing states; operation errors; validation messages; zero-power default; and no automatic laser/motion command on page load or slider drag.

**Exit criteria.** Every machine state is readable at a glance; all unavailable actions explain why; reconnect restores a canonical snapshot; the shell functions without console errors; and hazardous controls are separated, labeled, and operable by keyboard without accidental activation.

**Rollback.** Keep the current static application deployable until the replacement shell covers health and upload. If a frontend framework is proposed later, justify it separately; do not combine a framework migration with firmware safety work.

### Step 10 — Deliver calibration, homing, settings, presets, and manual tests

**Objective and context.** Implement the user’s requested non-engraving workspaces using the stable machine API and persistent model. Calibration and homing are operational workflows, not just forms. The step should expose useful stepper settings while respecting the confirmed driver type and firmware’s actual capabilities.

**Dependencies.** Steps 3–7 and 9; machine API may be implemented before the final polished screen.

**Work items.**

1. Build the Calibration/Homing screen with raw/stable sensor indicators, per-axis home actions, home-all, phase progress, bounded jog, stop, last result, and explicit “position not trusted” notices.
2. Design a guided first-time setup: sensor polarity check, movement direction check at very low speed, sensor-to-home sequence, travel calibration, repeatability check, and saved calibration revision. Each step requires operator confirmation where code cannot verify the physical outcome.
3. Build a Settings page organized by machine calibration, motion defaults, laser profile, and UI preferences. Display units, sources, ranges, active revision, and which values require restart/rehome.
4. Build preset list/create/clone/edit/rename/delete flows with validation, built-in preset protection, and visible comparison to current values. Include a default commissioning preset only when safe values are approved.
5. Build job-level override UI and a reset-to-preset affordance. Before Start, show a summary of effective values and indicate which differ from the saved preset.
6. Implement a finite motor test: select axis, direction, step count/distance, speed, and optional repeat count within hard limits; show countdown/progress; keep stop visible; return a result that distinguishes controller pulses from operator-confirmed motion.
7. Implement sensor test: read once, monitor transitions, show raw/stable values and debounce; never start a motor as part of a sensor test unless it is a separate confirmed operation.
8. Implement laser PWM test with a power selection and bounded pulse only if the approved physical safeguards exist. Otherwise expose signal-configuration diagnostics with laser supply disabled and label optical testing as unavailable.
9. Prevent settings apply during active homing/job; make microstep/direction/steps-per-unit changes require rehome if they affect coordinates.
10. Persist operator actions and test results with timestamps and device firmware/profile identity for troubleshooting.

**Deliverables.** Calibration/homing wizard, motor/sensor/laser test panels, settings editor, preset management, job overrides, and workflow tests.

**Verification.** UI/API integration tests for invalid values, busy machine, failed homing, partial home, sensor noise, save/clone/delete, preset immutability, failed apply, rehome-required changes, manual stop, and test timeout. Hardware tests must use the staged procedure: sensor-only, one motor/axis at a time, no laser supply, bounded motion; laser test remains physically guarded and isolated.

**Exit criteria.** The requested calibration/homing and settings/test sections are usable end to end; each saved or applied value is accurately reflected by both server and controller; tests are bounded and cancelable; and no settings control implies unsupported hardware capability.

**Rollback.** If a particular motor setting has no real production implementation, leave it out or mark it read-only with a clear reason. Do not expose a fake UI field that only changes the database.

### Step 11 — Deliver engrave workflow, job lifecycle, and run history

**Objective and context.** Complete the central user journey: select an SVG or a built-in shape, choose a preset or temporary configuration, preview the final machine plan, and run it with truthful progress and safe stop behavior. The current server can upload and preview but cannot execute a reliable job, and its generated command set is not aligned with production firmware. This step may deliver preparation and simulator/dry-run behavior before live laser commissioning, but no real job may energize PWM until Step 5’s hardware safety gate has passed.

**Dependencies.** Steps 6–10 and the firmware job execution protocol.

**Work items.**

1. Build a file selector/upload component with allowed extensions, size limits, filename sanitization, upload progress, duplicate handling, and delete/retention behavior.
2. Build a shape picker for the approved built-ins. Provide dimensions in physical units, optional line/grid spacing, and preview before preparation.
3. Build settings selection: saved preset dropdown, “customize for this job” workflow, effective configuration summary, and confirmation that overrides do not modify the saved preset.
4. Add transform controls for fit, scale, rotation, origin, offset, and margins only where supported by the prepared-plan contract. Show coordinate units and work-area boundary.
5. Implement a two-stage flow: prepare/validate first, then start. Preparation returns a frozen plan hash and warnings; Start submits that exact hash, not a recomputed request body.
6. Gate Start on connected/compatible controller, homed axes, idle/fault-free state, valid machine calibration, valid plan and preset, and required physical safety policy acknowledgements. Server performs every check even if UI disables the button.
7. Implement accepted, queued, running, pausing/paused if supported, stopping, completed, failed, canceled, and outcome-unknown states. Each transition has an operation ID and persisted timestamp/event.
8. Add progress based on firmware-confirmed segment or operation events, not a browser timer alone. Present current phase, estimated remaining time, commanded coordinates, and warning state honestly.
9. Implement emergency stop and normal cancel. Emergency stop requests immediate safe state; normal cancel may stop at a safe segment boundary. Describe which semantics apply. If the controller is unreachable, show outcome unknown and instruct physical inspection.
10. Do not auto-resume after server restart, ESP32 reboot, transport loss, or browser refresh. Reconcile status and require operator confirmation/rehome if coordinate validity is lost.
11. Store completed/failed/canceled job history with source filename/hash, thumbnail, plan snapshot, preset/calibration revision, effective settings, duration, and terminal reason. Allow duplicate/re-run only after revalidation against current machine configuration.
12. Add a job details page that can inspect preview, path statistics, warnings, settings, and logs without exposing an unsafe free-form command console.

**Deliverables.** Engrave workflow, job API/state machine, command streaming, progress WebSocket events, history/detail screen, failure/recovery UX, and integration tests.

**Verification.** Simulated integration tests for upload, prepare, reject-out-of-bounds, choose preset/override, stale plan hash, no-home start rejection, controller busy, accepted/running/completed, cancellation, emergency stop, partial delivery timeout, device disconnect, reboot, server restart, duplicate start, and history snapshot immutability. On hardware, first run laser-disabled dry-run patterns and verify travel visually; then use approved low-power guarded test and a small nonhazardous material only after full interlock and physical commissioning checks.

**Exit criteria.** A user can complete prepare-to-run with exact plan/settings snapshot; job state reflects controller acknowledgements; any ambiguous outcome is surfaced; stop and fault paths force laser off; and no reconnect/retry can cause duplicate engraving.

**Rollback.** If firmware cannot safely stream/execute a job yet, keep the Engrave flow in preview/dry-run mode and explicitly disable Start. Do not label generated G-code as executable until the firmware parser and output timing pass compatibility tests.

### Step 12 — Production hardening, system acceptance, deployment, and release

**Objective and context.** Integrate all layers, test failure modes, document the actual hardware, and make the product operable by someone who did not write it. A repository test pass is not a physical laser-machine release; software, electrical, mechanical, and operator gates must all be satisfied separately.

**Dependencies.** Steps 1–11.

**Work items.**

1. Run focused lint, type checks, firmware compile, unit tests, API tests, protocol contract tests, frontend tests, and system tests. Record exact tool/core/compiler versions.
2. Perform cross-layer compatibility checks: firmware protocol version, server codec, generated motion plan, laser duty conversion, coordinate units, status state names, fault codes, homing phases, and CORS/network settings.
3. Conduct fault injection: controller boot during job, host crash, Wi-Fi loss, serial disconnect, malformed frame, oversized frame, duplicate command, sensor stuck high/low, sensor chatter, travel cap, motor driver failure where detectable, laser PWM attach failure, and repeated stop.
4. Verify database migration/backup/restore, disk-full behavior, corrupt file, malformed SVG, cleanup/retention, SQLite lock behavior, and graceful server shutdown.
5. Perform a security and deployment review appropriate to local machine control: bind address, origins, authentication if network-exposed, request size limits, upload path isolation, secret handling, WebSocket authorization, debug/reload disabled in production, and dependency maintenance.
6. Conduct accessibility and usability review with an operator who did not implement the UI; verify warnings, state names, keyboard controls, and error recovery are understandable.
7. Review physical risk controls: actual laser enable path, interlock, enclosure, emergency stop, power supply, cable strain relief, common ground, sensor voltage, stepper current, thermal limits, ventilation, and protective eyewear applicable to the actual wavelength/class. Do not represent this checklist as a substitute for local regulatory advice.
8. Define deployment as a reproducible service: environment configuration, systemd/container decision if needed, logs, restart policy that starts laser-safe, database backup, static asset version, health endpoint, and documented rollback.
9. Produce an operator manual, wiring diagram, calibration record template, supported SVG feature list, preset guide, maintenance checklist, troubleshooting flow, and release notes. Describe the exact machine hardware and firmware version tested.
10. Establish measurable acceptance thresholds: homing repeatability tolerance, max move range, command latency, stop latency, sensor debounce, job position tolerance, laser output cap, reconnect behavior, upload complexity limit, and preview-to-plan determinism. Owners set numeric thresholds before release, not after observing results.
11. Label each test as automated, simulated, bench electrical, mechanical, laser-disabled dry run, guarded optical test, or operator acceptance. Never report an unperformed test as passing.
12. Keep a known-good firmware/server artifact and a tested rollback procedure. Firmware downgrade must not silently reinterpret persisted positions or job snapshots.

**Deliverables.** Release candidate, end-to-end test report, physical commissioning sign-off, deployment instructions, user documentation, rollback procedure, and a prioritized post-release backlog.

**Verification.** A fresh install and upgrade path both work; all checks pass in the documented environment; failure injection yields safe and explicit states; operator acceptance is signed; a known-good release can be restored; and release notes distinguish software verification from hardware verification.

**Exit criteria.** No open critical safety, protocol, data-loss, or job-integrity issue remains; physical acceptance thresholds are met; laser-off and stop behavior are verified on the actual assembled machine; operator documentation matches actual code and wiring; and all release artifacts identify exact firmware and server versions.

**Rollback.** Stop rollout if laser-off behavior, home repeatability, stop latency, coordinate bounds, or protocol state are uncertain. Disable job execution and retain preview/simulation mode while fixing the blocker. Preserve database and diagnostics for analysis.

---

## 8. Dependency graph and parallel work opportunities

```text
Step 1: Hardware/profile/protocol contract
  ├── Step 2: Firmware fail-safe foundation
  │     ├── Step 3: Motion backend
  │     │     └── Step 4: Sensors and homing
  │     │            └── Step 5: Laser integration
  │     └── Firmware protocol simulator/contract tests
  ├── Step 6: Server models and DB migrations
  │     └── Step 7: Bridge/API/live status
  │             └── Step 8: Prepared job pipeline
  └── UI contract/design tokens and shell can start after schemas settle
        └── Step 9: Web shell/dashboard
               ├── Step 10: Calibration/settings/presets/tests
               └── Step 11: Engrave/job lifecycle
                      └── Step 12: System acceptance/release
```

Steps 2 and 6 can proceed in parallel after Step 1, provided they share the same frozen protocol and domain contracts and do not create conflicting configuration schemas. The UI shell can be developed against typed mock snapshots while the API is being implemented. SVG preparation can begin after units and work-area definitions are decided, but execution serialization must wait for the firmware protocol. Before Step 5 passes actual-hardware laser safety acceptance, all server/API/UI actuator paths remain simulator-only or explicitly laser-disabled; no live power slider or laser-bearing job route may drive real PWM. Motion and sensor work is sequential in firmware because homing depends on a safe motion primitive. Laser integration follows established motion/stop semantics. The release gate depends on every branch of the graph.

Suggested review gates:

- **Gate A:** Hardware owner confirms GPIO map and power/safety topology before Step 2 hardware changes.
- **Gate B:** Protocol review signs the shared golden command/status corpus and unsupported-command rejection rules before server and firmware adapters diverge.
- **Gate C:** Motion-only bench sign-off before combining sensor homing.
- **Gate D:** Sensor/homing repeatability sign-off before work-area calibration.
- **Gate E:** Laser electrical-off, hard-cap, physical interlock, and output-off behavior sign-off on the actual hardware profile. Until this gate passes, every real laser-output API and laser-bearing execution path remains server-disabled; mock/simulator and laser-disabled dry-run paths only.
- **Gate F:** Immutable-plan and simulated-device tests before live job execution.
- **Gate G:** Full operator acceptance and rollback rehearsal before production release.

---

## 9. Cross-layer contracts and design details

### 9.1 Coordinates, units, and calibration

The current application uses values described as steps in settings, while SVG files usually express lengths in CSS pixels, millimeters, or inches. The production contract must not conflate these. Define a source geometry space, a physical machine space, and an integer motor-step space. The server converts source geometry to physical coordinates using resolved SVG units and an explicit transform. Firmware either consumes physical units and calibrated scale, or consumes bounded steps generated against a versioned calibration; one layer owns conversion, and the protocol clearly states which.

For an open-loop mechanism, current position is a count of commanded steps relative to a verified homing origin. It is not a sensor-measured position. Each axis has `homed`, `position_valid`, `commanded_steps`, and calibration revision. A move beyond a soft travel boundary is rejected before any pulse. A home operation establishes the reference only if the sensor sequence completes. Changing microstep mode, gear ratio, steps-per-unit, direction inversion, or relevant mechanics invalidates coordinate interpretation and requires rehome.

The work-area profile must distinguish effective engraving area from physical travel. Margins should account for sensor clearance, optics, and mechanical tolerances, not just numeric percentages. A shape preview shows the effective area and origin. For mirrored or scanner-style mechanics, X/Y direction and geometry orientation must be physically confirmed before enabling a laser job.

### 9.2 Job and operation state transitions

A job should move through a controlled lifecycle, for example:

```text
draft → prepared → queued → starting → running → completed
                                ├→ cancel_requested → canceled
                                ├→ failed
                                └→ outcome_unknown → needs_operator_review
```

Not every deployment needs a persistent queue. If the first release supports only one active job, enforce one active operation in the database and controller dispatcher. “Prepared” means the geometry and settings have been validated; it does not mean the controller is ready. “Accepted” means the controller accepted a particular operation ID; it does not mean movement began. “Running” requires a corresponding controller event or snapshot. “Completed” requires firmware terminal completion, not the server exhausting a list of lines.

An operation state is separate from a job state. Homing, jog, manual laser test, and dry run each have operation identifiers and completion outcomes. The server should store enough data to recover a display after a browser refresh while not claiming that a server restart can safely resume an active physical operation.

### 9.3 Emergency stop, stop, pause, and cancel

The UI must distinguish:

- **Emergency stop:** immediate request to disable laser and stop or inhibit motion under the hardware-approved policy. It should be available everywhere and should correspond to an independent physical emergency stop circuit; web-only software cannot provide safety-rated stopping.
- **Stop:** software request to halt current motion and turn laser off. Exact deceleration versus immediate stop must be documented and tested.
- **Cancel job:** orderly job termination at a defined safe boundary when the controller and state are trustworthy.
- **Pause:** temporary hold with laser off and preserved position only if firmware implements a reliable pause/resume boundary. If it cannot preserve state safely, omit pause rather than presenting a misleading button.

A disconnected browser cannot stop the machine. A disconnected server may not reach the ESP32. Therefore the system’s primary safe response to controller/host loss must be local to the machine: hardware gate/interlock, firmware watchdog policy, and safe laser defaults. On reconnection, the server reconciles state and instructs inspection/rehome as necessary.

### 9.4 Presets and configuration revisions

At minimum keep four concepts separate:

1. **Hardware profile:** wiring and driver facts; controlled by deployment/build.
2. **Machine calibration:** steps-per-unit, travel envelope, home direction, sensor polarity and offsets; controlled by commissioning.
3. **Process preset:** speeds, power selection, passes or other engraving settings; controlled by operator.
4. **Job override:** temporary settings for one prepared job.

The active job snapshot includes a hash or revision for each relevant layer. The server may show a preset name on a historical job, but the frozen snapshot is the source of truth. Preset import/export should be considered only after validation and a stable schema; imported values must never override firmware hard limits.

### 9.5 Laser power value semantics

The product UI expresses a requested integer percentage. Firmware converts percentage to duty at an approved resolution and enforces its hard maximum. The database records percentage and the firmware configuration/curve revision; raw duty may be stored for audit but should not be an operator-facing primary unit. A calibration table may eventually map a percentage to material-specific output, but “percent” without optical calibration means fraction of configured PWM duty, not a measured wattage or a universal engraving darkness.

The proposed list has an arithmetic ambiguity: 5 plus repeated increments of 10 yields 5 through 95, not 100. The roadmap treats 100 as a special final choice for discussion, not as an approved command. The safer production default should omit or disable any choice above the firmware commissioning cap. The final list and cap are decisions for the machine owner.

### 9.6 SVG support boundary

SVG is a full graphics language. A production promise must name the supported subset. A reasonable first release supports path and basic geometric primitives, standard transforms, viewBox, and physical/CSS dimensions. Text may be rejected unless converted to paths. External images and references should be rejected. Unsupported filters/masks/clips should not be silently ignored because the preview could diverge from intended output. A validation panel should report supported/unsupported constructs with counts and location hints.

The parser must treat uploads as untrusted input even in a local application. Bound file size, XML depth, number of paths, number of segments, sample count, memory use, and processing time. Avoid arbitrary external entity resolution. A generated shape should enter the same normalized representation as an imported SVG so there is exactly one path to the firmware.

### 9.7 Networking and deployment model

The first production instance should be reachable only from the intended local network. Binding FastAPI to `0.0.0.0` is a deployment choice with security implications. Wildcard CORS is not authorization, and a private LAN is not automatically trusted. Configure allowed origins, host binding, authentication if users outside the host are in scope, rate limits on expensive uploads/operations, and request-size limits. Disable development reload in production. Store Wi-Fi credentials outside tracked source. Provide a deployment profile that clearly states whether TCP or serial owns the controller at a time.

If TCP is used, define IP reservation/discovery, reconnection, one-controller ownership, and what happens if multiple browser tabs or clients issue commands. If serial is used, define device path discovery, permissions, reconnect, and the fact that USB disconnect can leave machine state uncertain. Do not support simultaneous TCP and serial command authority unless firmware includes a safe arbitration model.

---

## 10. Test strategy and measurable quality gates

### 10.1 Firmware logic tests

Firmware tests should be split between pure host-testable logic and target-specific compile/hardware checks. Pure logic includes parser/schema validation, range checks, motion planning, state transitions, sensor debounce, homing phase rules, duty conversion, plan segment acceptance, and fault behavior. Where practical, abstract GPIO/time so deterministic tests can advance virtual time and simulate pin states. Every failure result should be asserted, not only happy-path acceptance.

Required edge cases include integer overflow, negative values, zero speed, extreme speed, malformed numeric values, excess command tokens, truncated frames, duplicate request IDs, concurrent operations, time wraparound, stuck sensors, sensor chatter, axis already homed, axis unhomed, microstep change, stop during every homing phase, stop during travel and engraving, watchdog, and state reset. Host tests do not prove actual pulse timing or pin electrical behavior.

### 10.2 Python/server tests

Use focused pytest tests for repository migrations, typed schema validation, preset lifecycle, plan immutability, SVG parser bounds, transform invariants, execution serializer, bridge timeouts, response correlation, command serialization, API state gates, WebSocket initial snapshot/reconnect, and simulated controller integration. Avoid requiring a real SQLite file shared with the working tree; use temporary database paths/fixtures and clean them reliably. Existing tests should be updated to the chosen command contract rather than preserving contradictory assumptions.

Tests should prove that:

- A malformed or oversized SVG is rejected without writing a job row or unbounded temp file.
- A prepared plan does not change when a preset is edited afterward.
- A job cannot start when either required axis is not homed or the controller is stale.
- An API timeout never returns success-shaped `"ok"`.
- Repeated start requests with the same idempotency token do not execute twice.
- A controller event with the wrong operation ID cannot advance the wrong job.
- Laser output is reported off when the controller disconnects, and if the outcome cannot be observed the UI says unknown rather than claiming off.
- WebSocket reconnect obtains a full snapshot and does not require a browser message to keep updates flowing.
- An upload filename cannot escape managed storage or overwrite unrelated files.

### 10.3 Frontend tests

Use unit/component tests for connection state, stale state, validation, effective preset display, transform warnings, operation lifecycle, and disabled-state reasons. Use browser-level interaction tests for the full path: upload or select a shape; prepare; select preset; see bounds and warnings; reject start before home; home through simulation; start a job; receive progress; stop; observe terminal reason; refresh and inspect history. Run keyboard navigation and automated accessibility checks, but complement automation with manual review of focus order and screen-reader announcements.

Critical browser assertions include no device command on slider `input`, no machine action on page load, no double submission, no active controls while disconnected, explicit confirmation for hazardous operations, and visible emergency stop in all relevant machine-control states.

### 10.4 Contract and simulated integration tests

Use a shared protocol schema or golden example corpus consumed by firmware and server tests. Include valid and invalid commands, events, snapshot versions, error codes, frame lengths, and numeric bounds. The fake controller should simulate delayed acknowledgements, missing completion, duplicate events, reconnects, reset, partial home, busy state, active sensor, and laser fault.

An end-to-end test harness should run FastAPI against a temporary SQLite database, connect a simulated firmware process or in-process transport, and drive the browser/API through operation states. This catches mismatches that isolated tests miss. Test logs should include request ID, operation ID, job ID, and protocol version for correlation.

### 10.5 Physical acceptance sequence

Physical tests are staged and stop at the first failure:

1. **Documentation and inspection:** compare actual wiring to approved profile; verify no loose or shorted wiring; verify supply ratings and grounding.
2. **Logic-only safe boot:** disconnect motor and laser power supplies; verify GPIO defaults, Wi-Fi/serial startup, status, watchdog/reset behavior, and stop input.
3. **Sensor-only:** power sensors within ESP32 input limits; verify raw/stable polarity and transitions at the actual flags; test disconnected, stuck, and noisy conditions where safe.
4. **Single motor:** laser power disconnected; one axis at conservative speed and short distance; validate direction, current, heating, pulse timing, mechanical binding, and stop.
5. **Both axes:** repeat independent axis checks, then coordinated dry-run lines and shapes. Verify travel range and position counts.
6. **Homing:** one axis at a time, low speed, conservative caps, repeat cycles and measure repeatability. Test already-triggered sensor, no trigger, abort, and power/network loss.
7. **Laser electrical signal:** laser supply physically isolated; verify PWM frequency and duty with an instrument, zero output on boot, stop, reset, idle, and fault.
8. **Safety chain:** verify physical key/interlock/enclosure and emergency stop independently of browser/network. Confirm restart does not re-enable output.
9. **Guarded low-power optical check:** only after the owner confirms appropriate enclosure, controls, eyewear, target, and operating procedure. Start at the lowest allowed level and a bounded test pulse.
10. **Dry-run job:** execute representative square/circle and small SVG with laser inhibited; compare preview, coordinates, and actual trajectory.
11. **Controlled engraving:** use a known, safe test material and a low-power process only after all earlier gates pass. Record settings and results.
12. **Failure rehearsal:** disconnect host/network, reset controller, press physical stop, and verify output state and rehome requirements.

These stages do not prescribe an unsafe laser setup. They require the owner to evaluate the specific wavelength, output class, enclosure, physical environment, and local operating rules. A software test cannot establish optical safety.

### 10.6 Suggested numeric targets to agree before implementation

Do not invent values in code until the owner selects and validates them. The acceptance plan must assign a value and evidence method for:

- Maximum physical and logical travel per axis.
- Maximum jog command duration and distance.
- Maximum normal speed and acceleration for each driver/motor.
- Homing seek/pull-off/latch step and time limits.
- Homing repeatability tolerance over a stated number of cycles.
- Stop request to laser-off latency and motion stop latency.
- Sensor debounce and maximum accepted response latency.
- Maximum laser duty for commissioning and normal engraving.
- Maximum manual laser-test duration.
- Maximum SVG size, path count, segment count, parse time, and generated point count.
- Maximum job duration or operation watchdog policy.
- Controller heartbeat interval and stale-state threshold.
- Server reconnect/backoff and command timeout thresholds.
- Job database/asset retention limits and backup frequency.

Where a numeric target depends on the final hardware, record it as “pending measurement” rather than a fake acceptance pass.

---

## 11. Safety, reliability, and error-handling model

### 11.1 Risk categories

The highest-consequence risks are unintended laser emission, uncontrolled motion into mechanical stops, loss of coordinate reference followed by a job start, and an operator believing the machine stopped when communication is lost. Medium risks include data loss, corrupted or mis-scaled geometry, overheating motors or laser, duplicate jobs after retry, and firmware/server schema mismatch. Lower but still important risks include confusing diagnostics, stale UI state, inaccessible controls, and an unsafe deployment default.

### 11.2 Failure response matrix

| Failure | Firmware response | Server response | UI/operator response |
|---|---|---|---|
| Invalid command | Reject before side effects; report stable code | Return validation/protocol error | Preserve form values; explain valid range |
| Controller disconnect during idle | Keep outputs in safe state; report reconnect on next link | Mark state stale/disconnected | Show last-seen time and unknowns |
| Disconnect during motion/job | Apply local timeout/interlock policy; laser off; stop per approved behavior; invalidate position if needed | Mark outcome unknown; stop sending segments | Tell operator to inspect and rehome; never auto-resume |
| Sensor never triggers | Stop at phase step/time cap; leave axis unhomed | Persist failed home operation | Show axis, phase, and check sensor/direction |
| Sensor stuck active | Bounded clear-initial failure; no unbounded retreat | Preserve fault/event | Show raw/stable state and recovery instructions |
| Homing abort | Stop pulses, laser off, unhome affected axis | Mark aborted, not successful | Require re-home before job |
| Laser PWM setup failure | Keep pin/output disabled; latch fault | Refuse laser-dependent job | Show laser unavailable |
| Host timeout after sending command | Cannot infer delivery; firmware deduplicates request ID | Mark unknown and reconcile snapshot | Do not offer blind retry for non-idempotent operation |
| Server restart with active job | Firmware watchdog/local policy decides safe state | Restore DB state as interrupted/unknown | Require operator inspection; no resume |
| Database error | Do not claim persisted job/preset | Fail request; preserve machine-safe state | Show save/run failure and action to retry safely |
| SVG parse/resource limit | No job creation or execution | Return structured issues | Show file correction guidance |
| Emergency stop input | Immediate hardware/firmware policy; laser gate off | Record state when possible | Persistent fault/acknowledgement and clear recovery |

### 11.3 Error reporting

Every error should have a stable machine-readable code and a safe human message. Examples include `CONTROLLER_DISCONNECTED`, `PROTOCOL_VERSION_MISMATCH`, `BUSY`, `NOT_HOMED`, `SENSOR_STUCK_ACTIVE`, `HOME_SEEK_LIMIT`, `HOME_TIMEOUT`, `TRAVEL_LIMIT`, `LASER_INTERLOCK_OPEN`, `LASER_DUTY_LIMIT`, `PLAN_OUT_OF_BOUNDS`, `SVG_UNSUPPORTED_FEATURE`, `OPERATION_OUTCOME_UNKNOWN`, and `DATABASE_UNAVAILABLE`. Include operation/job IDs where applicable and state whether retry is safe. Avoid leaking stack traces, credentials, filesystem paths outside the application, or raw user SVG data in browser errors.

Logs should distinguish expected rejection from internal failure. A failed device operation must not be swallowed by broad catches or replaced with a default. Catch only at boundaries where an error is translated to a typed API result, and retain diagnostic context for local logs without exposing secrets.

---

## 12. Operational workflow and intended screen content

### 12.1 Global header / machine status strip

Every control section should show the machine name/profile, server status, ESP32 status, transport, firmware/protocol version, fresh/stale indicator, and current operating state. Include a machine-specific fault banner. The emergency stop should be visually distinct and remain available when an operation is active. It must not share color/placement with ordinary navigation. A physical emergency stop remains primary.

When disconnected, the header should state which entity is disconnected. “Server offline” and “controller offline” are different. If the last controller snapshot is old, show its timestamp and avoid displaying a green homed or laser-off indicator as current truth.

### 12.2 Engrave page layout

The workflow should proceed in visible stages:

1. **Choose design:** SVG library, upload, or built-in shape.
2. **Set dimensions and transform:** physical size, fit/scale, orientation, origin, and offset.
3. **Select process:** preset or temporary overrides, including requested power selection.
4. **Prepare:** server validates geometry and returns preview/warnings/estimate.
5. **Review:** work area, transformed paths, travel, effective settings, home/connection readiness.
6. **Run:** explicit start confirmation, progress, pause only if supported, cancel, emergency stop.
7. **Result:** completed/failed/canceled/unknown outcome and immutable job details.

The preview should occupy adequate space and be useful for comparing the work with the physical bed. Include a separate view or legend for cutting/engraving path and travel moves. The UI should not merely show raw G-code. A diagnostics view may show the generated execution representation, but a non-engineer should be able to understand the preview without reading G-code.

The final start panel should summarize source, dimensions, preset, overrides, maximum requested laser percentage, estimated time, warnings, and current machine readiness. If something is not ready, explain it. Starting a job with stale or unknown homing status is forbidden.

### 12.3 Calibration/Homing page layout

Show a top-down coordinate diagram with current commanded position and sensor flags, but label the position as commanded/open-loop. Provide axis-specific controls rather than a coupled joystick until the user intentionally selects coordinated movement. Each jog requires direction, bounded distance, and speed; use step increments that match the operator’s calibration stage. A press-and-hold continuous jog should be deferred unless there is a reliable dead-man behavior, keyboard/touch release handling, and firmware timeout.

Sensor diagnostics display both electrical state and logical interpretation. A sensor “detected” LED in the UI should be textual and color-independent. A history of sensor transitions can help alignment, but it must be bounded. Homing progress must show which axis and phase are active, elapsed time, and the safe abort action. On completion, show homed state and the measured repeatability record if the operator has performed a calibration series.

### 12.4 Settings and preset page layout

Group settings by purpose:

- **Machine calibration:** steps per mm/degree if confirmed, effective work area, axis polarity, home direction, sensor polarity, homing speeds and bounds.
- **Motion behavior:** travel speed, engrave speed, acceleration if supported, jog speed, driver enable policy, microstep selection if supported.
- **Laser profile:** PWM frequency/resolution, configured cap, process-level requested percentage, test availability/interlock status.
- **Presets:** named profiles and descriptions, duplicate/edit/delete, default selection.
- **Advanced diagnostics:** firmware and protocol versions, raw duty, sensor counters, event log.

Fields that affect coordinate validity should carry a “rehome required” indicator. Applying values to hardware, saving them to a preset, and setting them temporarily for a job are separate actions. The user must be able to see which action succeeded. Show an explicit diff from active configuration, not just a generic “saved” toast.

### 12.5 Test page layout

The test page should be a controlled test bench inside the application, not a shell. It can offer:

- Controller ping and protocol compatibility.
- Sensor raw/stable read and monitor.
- Driver enable state where available.
- Bounded one-axis jog.
- Coordinated dry-run pattern with laser physically disabled.
- Laser PWM signal configuration test and guarded optical pulse only if policy allows.
- Clear pass/fail/unknown result and a record of what the test actually observed.

Tests should never bypass home requirements, travel bounds, laser cap, or the controller state machine. Add a clearly labeled maintenance mode only if there is a real use case and a safe authorization model. A maintenance mode is not permission to send arbitrary commands.

---

## 13. Documentation and release artifacts

Directly related documentation must change with the implementation. The release should include:

- **Hardware profile and wiring diagram:** exact board/module variants, GPIO mapping, voltage levels, connectors, supply domains, grounding, laser gate, sensor polarity, and a date/revision.
- **Operator setup guide:** server install/configuration, browser access, transport selection, first boot, credential provisioning, and safe shutdown.
- **Calibration guide:** sensor checks, axis direction check, homing, travel measurements, steps-per-unit calibration, repeatability record, and when re-homing is required.
- **Preset guide:** built-in/custom presets, saved vs temporary override semantics, effective laser percentage meaning, and safe power cap.
- **SVG support guide:** supported features, units, text handling, transforms, limits, warnings, and troubleshooting.
- **Safety and maintenance guide:** approved laser controls for the actual module, physical emergency stop, enclosure/interlock, protective equipment and operational practices, motor-current/thermal checks, never-hot-plug warnings where applicable, and inspection cadence.
- **Developer guide:** firmware build/core version, server/test commands, protocol schema, simulator, database migrations, and architecture.
- **Troubleshooting guide:** connection states, common sensor faults, homing failures, out-of-bounds geometry, unknown operation outcomes, and recovery/rehome steps.
- **Release notes:** firmware/server versions, migrations, compatibility, known limitations, and separately labeled software and physical verification.

Do not simply copy the bench fixture’s wiring instructions into production docs. Preserve its scope label and keep its laser-free/A4988-specific directions clear. The PWM characterization sketch should remain a lab artifact unless the production workflow explicitly needs a maintenance test; if changed, its operating instructions must match the selected laser and profile.

---

## 14. Risks and mitigation register

| Risk | Severity | Likelihood before controls | Mitigation / decision gate |
|---|---:|---:|---|
| GPIO conflict between motor and laser assignments | Critical | High | Step 1 physical pin audit; compile-time pin uniqueness; do not merge sketches by copy/paste |
| Homing reports success without physical movement | Critical | Certain in current prototype | Replace stub; tests require real stable sensor transitions; gate job start on verified home |
| Laser left enabled on reset, disconnect, or UI error | Critical | Unknown | Hardware default-off gate, firmware watchdog/stop, guarded test, physical interlock; staged verification |
| Wrong stepper driver architecture in production | High | High | Confirm ULN2003 vs STEP/DIR hardware before motion backend |
| Server says `ok` after missing response | High | Present in bridge code | Remove success fallbacks; model timeout/unknown; request correlation and snapshot reconciliation |
| G-code generator and firmware parser disagree | High | Present | Freeze protocol; shared contract tests; reject unsupported commands |
| Coordinate system assumes steps equal physical dimensions | High | Present | Calibration model; explicit units and effective work area; immutable plan |
| SVG regex fallback misinterprets arbitrary numeric data | High | Present | Harden XML/SVG parser; reject malformed/unsupported content; resource limits |
| Open-loop missed steps cause geometry shift | High | Inherent | Honest position validity; conservative acceleration; rehome after uncertain movement; optional future feedback |
| Browser reconnect creates duplicate job | High | Possible | Idempotency tokens, job operation IDs, immutable prepared hash |
| Wi-Fi credentials committed in firmware | High | Present as a pattern risk | Local ignored config/provisioning; secret scanning; no credential logging |
| Wildcard CORS and network exposure | Medium–High | Present | Same-origin default; documented bind policy; authentication if remotely accessible |
| Preset edit mutates historical job meaning | Medium | Likely without snapshot | Versioned configuration and immutable job snapshots |
| Slow/pathological SVG exhausts host memory | Medium–High | Possible | Upload, complexity, time, and memory limits; parser cancellation/worker strategy if needed |
| No physical emergency stop | Critical | Unknown | Release blocker for real operation; owner confirms hardware stop independent of web |
| Test page presents software output as physical proof | Medium | Likely | Observability labels: commanded, sensed, operator-confirmed |
| User selects 100% although increments imply 95% maximum | Medium–High | Certain ambiguity | Decide exact discrete values; firmware hard cap; disable unsupported values |
| SQLite file corruption or upgrade loss | Medium | Low–Medium | Migrations, backups, transaction tests, restore rehearsal |
| Protocol change breaks firmware/server pairing | High | Medium | Version negotiation; explicit incompatibility; golden tests; rollback artifacts |
| Starting before both axes home | High | Possible | Server and firmware gates; visible readiness checklist; state freshness timeout |

---

## 15. Product decisions to confirm before implementation

The roadmap can guide planning now, but the following choices are genuine blockers for the corresponding engineering work. The project owner should answer them before hardware steps begin. If unanswered, engineers should preserve the safe default and leave live motion/laser execution disabled.

### 15.1 Hardware profile

1. What are the actual production stepper driver boards and wiring: two ULN2003 boards driving unipolar 28BYJ-48 motors, two A4988/DRV8825-style STEP/DIR drivers with converted bipolar motors, or a different arrangement?
2. Are the GPIO assignments in production `config.h` physically wired and tested, or are they tentative? Which exact ESP32-S3 SuperMini revision is installed?
3. Is GPIO 10 connected to the laser PWM input in the assembled machine, or to a motor driver? If the current maps are not authoritative, who can verify the final pin table?
4. Are the FC-51/KY-032 sensors the final home sensors? What are the measured output voltage and active polarity at 3.3V-safe logic levels?
5. What is the actual laser module/driver model, PWM input polarity/range, wavelength, and power supply? Does the machine have an enclosure, key switch, door interlock, and physical emergency stop?

### 15.2 Machine geometry and motion

6. What is the real usable work area in millimeters (or another chosen physical unit), and what mechanical mechanism maps the X/Y motors to the laser spot?
7. What is the expected first-release speed/precision priority: slow/reliable commissioning, or faster engraving after measurable calibration?
8. Should microstep selection be exposed if the final driver supports it, or should it remain fixed to avoid changing calibration?
9. How should position validity be handled when drivers are disabled or a motor may lose steps? The conservative proposal is to require re-home after any uncertain position event.

### 15.3 Laser and power choices

10. Should the discrete settings be `OFF, 5, 15, …, 95, 100` with 100 a special endpoint, or should 100 be omitted in favor of a strict +10 progression after 5?
11. What commissioning maximum should be allowed in software before the first controlled optical characterization? The current laser test sketch’s 90% default is not automatically approved for production.
12. Do you want manual laser testing in the browser? The recommended default is a short guarded pulse only, and no indefinite slider-controlled on state.

### 15.4 Product and deployment

13. Will the server run on the same local network as the browser and ESP32, and is USB serial or Wi-Fi TCP the intended production connection?
14. Is this a single-operator local application, or must it support multiple users/roles, authentication, or remote network access?
15. Which SVG elements do you expect to engrave first: simple paths/shapes, Inkscape exports with transforms, text converted to paths, or imported images/raster work?
16. For the first release, should the application support only vector engraving or should it also support raster engraving? The recommended scope is vector-only until motion and laser modulation are proven.
17. Should jobs be resumed after a power/network interruption? The safe default is no automatic resume; require operator review and re-home where position is uncertain.

These decisions need not all be answered to continue documentation or simulator work. The hardware, laser, travel, and protocol questions must be answered before live machine control is enabled.

---

## 16. Extra features worth considering after the core is safe

These are deliberately not hidden in the first-release scope. They are optional product ideas to rank after the baseline engraver workflow is reliable.

### 16.1 Material and process library

A material library could store material type, thickness, nominal requested power, speed, passes, focus notes, and test outcomes. It should present recipes as user-owned starting points, not universal truth: laser output, focus, material composition, ventilation, and machine mechanics vary. A small power/speed test-grid generator can help calibrate settings while enforcing bounded runs and a guarded test procedure. The library should record the exact machine profile and firmware version used to generate each result.

### 16.2 Camera-assisted alignment

A camera preview could help position a design relative to material, but it adds calibration error, lighting sensitivity, perspective correction, and privacy/storage questions. It should be designed only after the machine’s coordinate transform and physical work area are measured. The UI must distinguish camera pixels from calibrated machine coordinates and show alignment confidence. A camera should not replace homing or travel limits.

### 16.3 Material-presence and enclosure inputs

Additional inputs can validate enclosure closed, key enable, material present, exhaust/air-assist status, or cooling condition. These need carefully selected sensors and an electrical safety design. Software indicators alone are not safety interlocks. The controller should report the raw and interpreted state of any such input and fail closed where the hardware design requires.

### 16.4 Job queue and operator profiles

A queue may be useful for multiple prepared jobs, but one active operation should remain the default. If added, queue execution requires explicit confirmation, material changes, and readiness checks per job. Operator profiles/roles may distinguish setup, maintenance, and ordinary engraving, but they must be implemented with real authorization and not merely hidden UI controls.

### 16.5 Image/raster engraving

Raster engraving requires a different planner: image decoding, dithering/grayscale mapping, scanline direction, acceleration compensation, laser modulation timing, overscan, and preview fidelity. It should not be treated as “just upload a PNG.” This work should follow a measured vector path pipeline and dedicated laser timing tests.

### 16.6 Rotary axis or Z/focus control

Rotary or focus axes change machine kinematics, coordinate calibration, travel limits, homing, and job representation. They need separate machine profiles and explicit interlocks to prevent using an XY plan on the wrong setup. Do not add generic extra-axis fields to the initial schema without a real hardware requirement.

### 16.7 Maintenance and diagnostics dashboard

A maintenance view could correlate homing repeatability, sensor jitter, driver temperature (if instrumented), job duration, abort rate, and firmware resets. It is valuable only when measurements have known meaning and bounded retention. Avoid converting software counters into claims about wear or motor condition without validation.

### 16.8 Backup/export and reproducibility

Users may benefit from exportable job packages containing original SVG, transformed preview, plan hash, preset snapshot, machine calibration revision, firmware/protocol version, and run log. Treat such packages as untrusted when imported; revalidate against current machine limits. Provide a simple database backup/restore path before a full cloud synchronization feature.

### 16.9 Guided first-job onboarding

A first-run checklist can guide connectivity, hardware profile, sensor check, axis direction, homing, work-area calibration, interlock check, and dry-run. Make steps skippable only where safe and record the reason. The onboarding must not run motors or lasers automatically on first load.

### 16.10 Test-pattern and calibration tools

Built-in geometric patterns can include a bounding box, small square, concentric circles, axes cross, and repeated registration marks. The first release can include simple vector shapes; future calibration patterns can measure scaling, skew, backlash, or optical alignment. Any correction routine should report what it measures and what it cannot infer from open-loop motors.

---

## 17. Implementation checklist for every milestone

Before coding:

- Read current branch and worktree; preserve pre-existing user changes.
- Read the targeted source and tests; do not assume the brief or this plan matches later code.
- Check the relevant hardware/profile decision is closed.
- Identify user-visible behavior and safety state transitions.
- Agree the protocol/data contract before editing both ends.
- Write a small test plan covering success, failure, bounds, abort, and reconnect.

During implementation:

- Keep hardware profile facts in one authoritative location.
- Keep shared protocol schema/examples versioned.
- Validate on both host and firmware.
- Use explicit units and bounded values.
- Avoid blocking loops and success-shaped fallbacks.
- Keep laser off unless the exact operation and safety gate permit it.
- Mark position invalid when its physical trust is lost.
- Use migrations for persistent schema changes.
- Keep upload parsing bounded and deterministic.
- Provide an accessible explanation for every disabled or failed control.
- Do not combine unrelated cleanup or rewrite the stack without evidence.

Before merging:

- Run the smallest relevant test set, then cross-layer tests for affected contracts.
- Compile firmware with the pinned/recommended ESP32 core.
- Check tests are hermetic and clean their temporary resources.
- Review generated docs and UI against actual supported behavior.
- Verify no credential, machine secret, or private network detail is committed.
- Inspect the full diff and worktree; do not stage unrelated pre-existing files.
- Record hardware tests that were performed and explicitly state tests not performed.
- Confirm that failures leave outputs safe and are visible to the operator.
- Ensure every new API/command has an error path and a safe state gate.

---

## 18. Definition of “production-ready” for this project

“Production” should mean that the system has a stable, tested, documented operating envelope, not that it has a long feature list or a polished dashboard. The project is ready for controlled use only when all of the following are true:

1. The assembled machine has one reviewed hardware profile with no overlapping pin assignments and no ambiguous driver assumptions.
2. Firmware safe-state behavior is verified electrically across power-on, reset, watchdog, disconnect, malformed command, emergency stop, and fault.
3. Homing physically moves each axis, observes the correct sensor through a defined sequence, is repeatable within an agreed tolerance, and fails boundedly when sensor conditions are wrong.
4. Motion bounds, direction, speed, stop latency, and coordinate validity are tested on the actual mechanical assembly.
5. Laser output is gated by reviewed hardware and firmware controls; a browser cannot keep it on indefinitely; normal job travel is laser-off; every terminal state is laser-off.
6. The controller protocol has explicit version, IDs, acknowledgement/completion/error semantics, and contract tests shared by host and firmware.
7. The server’s serial/TCP behavior has no silent success fallback, no unbounded wait, and no path for concurrent clients to interleave motion commands.
8. SVG preparation is deterministic, bounded, physically calibrated, preview-equivalent, and rejects unsupported or out-of-bounds input.
9. Presets and jobs are versioned and immutable where historical meaning matters. Migrations and backup/restore are verified.
10. The web application is usable with keyboard and assistive technology, clearly states freshness and machine readiness, and gives actionable errors.
11. Failure injection and staged hardware tests pass, and the deployment/rollback procedure is known.
12. Documentation identifies exact hardware and software versions and does not overstate what was tested.

The system should remain in preview/simulator or laser-disabled dry-run mode if any critical requirement above is not met. This is a legitimate intermediate release, not a failure: it allows UI and SVG work to advance while preventing unsafe claims about the physical machine.

---

## 19. Recommended immediate next actions

1. Answer the hardware-profile questions in Section 15.1, especially the production driver type and GPIO 10 conflict.
2. Confirm whether the current production `config.h` pin assignments match the assembled machine or are placeholders.
3. Provide measured work-area dimensions and the mechanical relation between motor steps and laser-spot coordinates.
4. Confirm the final laser module and its physical safety controls, then choose whether the UI offers a guarded manual pulse or only PWM diagnostics.
5. Approve the 5/15/…/95/100 power-selection proposal and a conservative commissioning cap.
6. Agree on local network/serial deployment and whether one operator is sufficient for v1.
7. Close Step 1’s hardware/protocol contract; then implement the first firmware foundation and backend schemas in parallel.

Until actions 1–4 are answered, proceed only with documentation, simulator, non-hardware-specific API/UI shell, and SVG contract work. Do not enable production laser output or present homing as complete.

---

## 20. Final recommendation

Build this as a safe, explainable machine-control product in measured increments. Start with the actual physical configuration and a command/state contract. Keep the current test sketches as evidence and isolated tools, not as copy/paste production firmware. Implement a non-blocking ESP32 controller that can truthfully home, bound motion, stop, and hold the laser off. Replace the server’s success-shaped bridge behavior with a single operation dispatcher and explicit event stream. Build presets and immutable job snapshots before presenting configuration controls. Make every shape and SVG use one validated geometry pipeline. Then provide the polished browser workflows the product deserves: Engrave, Calibration/Homing, Settings/Presets, and Tests.

This sequencing delivers the requested features while keeping the highest-risk uncertainties visible. It also lets the project show progress early in simulator and dry-run mode without confusing a working web page with a verified laser engraver. The defining standard for the finished hybrid web application should be that every operation has a clear precondition, a bounded execution path, a truthful result, an immediate safe response to failure, and enough history for an operator to understand what the machine did.
