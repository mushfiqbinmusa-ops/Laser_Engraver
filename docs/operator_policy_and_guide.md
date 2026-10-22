# Laser XY Scanner: Operator Policy & Standard Operating Procedure (SOP)

**Applies to:** Laser XY Scanner System  
**Controller:** ESP32-S3 SuperMini (`esp32s3_a4988_v1`)  
**Host Interfaces:** Web Application (React SPA) & REST/WebSocket Control Service (FastAPI)  
**Hardware Elements:** Diode Laser Module (LEDC GPIO 12), 2x 28BYJ-48 Steppers with A4988 Drivers (X/Y), 2x FC-51/KY-032 Optical Endstops, Dual Deflection Mirrors.

---

## 1. System Architecture & Component Roles

```
  +-------------------------------------------------------------+
  |              Operator Web Application (Browser)             |
  |   [Engrave Tab]    [Calibration Tab]    [Settings]  [Tests] |
  +------------------------------+------------------------------+
                                 | HTTP REST / WebSocket (port 8000)
  +------------------------------v------------------------------+
  |                 FastAPI Host Server Bridge                  |
  |  - Kinematics & Scaler  - Vector Parser & Toolpath Planner  |
  |  - Readiness Gate       - Coordinated Segment Streamer      |
  +------------------------------+------------------------------+
                                 | Serial (/dev/ttyACM0, 115200) or TCP (8888)
  +------------------------------v------------------------------+
  |           ESP32-S3 SuperMini Embedded Controller            |
  |  - Bresenham Motion Engine    - Optical Sensor Filter       |
  |  - 5-Phase Homing Machine     - 500ms Watchdog & Clamp      |
  +--------+---------------------+-------------------+----------+
           |                     |                   |
    [A4988 Drivers]       [Optical Endstops]   [Laser Diode]
    2x 28BYJ-48 Steppers  GPIO 13 (X), 8 (Y)   GPIO 12 PWM
    XY Deflection Mirrors (Active-LOW IR slot) (12-bit, ≤90%)
```

### Component Directory:
1. **Laser Diode Module (GPIO 12):** 
   - Controlled via 12-bit LEDC PWM at 5 kHz (values 0–4095).
   - Firmware enforces a non-negotiable **90% hard clamp** (maximum duty 3685/4095).
   - Protected by a **500 ms hardware watchdog**: if motion segments or pulse refreshes stop arriving for >500 ms, the laser is immediately cut off.
2. **2x 28BYJ-48 Stepper Motors & A4988 Drivers:**
   - 64:1 internal gear reduction (~2048 full steps per output revolution; 32,768 steps/rev in 1/16 microstepping).
   - Motor A drives Axis X mirror tilt; Motor B drives Axis Y mirror tilt.
   - Stepper drivers feature an automatic **30-second idle timeout** (`AUTO_DISABLE_MS = 30000`). If idle for 30s, coils de-energize to prevent heat buildup and coil burnout. De-energizing clears the calibrated homed state.
3. **2x FC-51 / KY-032 Optical Endstop Sensors:**
   - Active-LOW infrared optical beam sensors (Sensor X on GPIO 13, Sensor Y on GPIO 8).
   - Electrical 3.3V (HIGH) = Beam Unbroken (Clear); 0V (LOW) = Physical Flag in Slot (Tripped/Detected).
   - Firmware filters all sensor pins through a 3-sample, 3-millisecond debounce window to eliminate electrical noise.
4. **Dual XY Deflection Mirrors:**
   - Stepper shafts orient first-surface reflective mirrors to steer the laser beam across the 50x50 mm work area.

---

## 2. Mandatory Operator Safety Rules

> [!CAUTION]
> **CLASS 3B / CLASS 4 LASER RADIATION WARNING**  
> Direct exposure or specular reflections from mirrors can cause irreversible eye damage, permanent blindness, and skin burns within milliseconds.

1. **Certified Eye Protection is Mandatory:**
   - Everyone in the room must wear optical density rated laser safety goggles matched to your laser diode's specific wavelength (typically 405nm, 445nm, or 450nm; OD 4+ minimum) before the system is powered.
2. **Never Leave the Machine Unattended:**
   - Never initiate an engraving job or laser pulse without continuous visual observation. Keep a fire blanket or carbon dioxide (CO₂) fire extinguisher immediately accessible.
3. **Approved Materials Only:**
   - **Permitted:** Basswood, balsa, plywood, untreated craft softwood, veg-tanned leather, heavy cardstock, cardboard, dark non-transparent acrylic.
   - **STRICTLY PROHIBITED:**
     - **PVC, Vinyl, Pleather, Neoprene:** Produces corrosive hydrogen chloride gas and deadly chlorine gas which destroys mechanical optics and ruins lungs.
     - **ABS / Polycarbonate:** Emits cyanide and toxic styrene gas and melts with heavy flare-ups.
     - **Bare Mirrors / Chrome / Bare Metals:** Causes dangerous 100% specular reflections back toward the operator or mirrors.

---

## 3. Step-by-Step Standard Operating Procedure (SOP)

### Phase 1: Pre-Power Hardware Inspection
1. Verify optical sensor flags on Motor X and Motor Y move freely into their respective sensor slots without mechanical binding or collision.
2. Check that the deflection mirrors are clean and free of fingerprint oils or smoke residue. Use only optical-grade lens wipes if cleaning is required.
3. Confirm USB serial cable is securely connected to the host PC (`/dev/ttyACM0`).

### Phase 2: Connecting the Web Application
1. Launch the web application in your browser (default `http://localhost:8000`).
2. Observe the top header bar:
   - Transport indicator must show **CONNECTED** (`SERIAL: /dev/ttyACM0 @ 115200`).
   - Machine State will show `IDLE_UNHOMED` (or `IDLE_HOMED` if previously homed).
   - Telemetry heartbeat should pulse green (<2s fresh).

### Phase 3: Sensor Verification in Calibration Tab
1. Navigate to the **Calibration** tab.
2. Inspect the **Optical Endstops Status** cards:
   - When mirrors are positioned in the open area: Both Sensor X and Sensor Y must show **BEAM OPEN (3.3V)** with Debounced State **CLEAR**.
   - If an axis flag is manually moved into the slot: The status must immediately update to **TRIPPED (0V)** with Debounced State **BLOCKED / DETECTED**.
   - If a sensor reads `TRIPPED` while physically clear, adjust the sensitivity potentiometer on the FC-51/KY-032 sensor module until the indicator LED just turns off.

### Phase 4: Machine Homing Procedure
1. In the Calibration tab (or quick button on Engrave tab), click **Home All Axes (X + Y)**.
2. The firmware executes the automated 5-phase homing state machine:
   - **Phase 1 (ClearInitial):** If the flag starts inside the sensor, the motor reverses until the optical beam is clear.
   - **Phase 2 (FastSeek):** Motors seek forward at 200 sps until the flag trips the optical beam.
   - **Phase 3 (PullOff):** Motors back off past the sensor trigger point (at least 20 steps).
   - **Phase 4 (SlowLatch):** Precision approach at 40 sps to capture the exact microstep edge transition.
   - **Phase 5 (FinalPullOff):** Motor retracts 200 steps so the beam remains completely open during normal workspace operation.
3. Both axes zero their coordinates to `(0, 0)`, and the state transitions to `IDLE_HOMED`.

### Phase 5: Vector Art Preparation & Upload
1. Prepare artwork in vector format (SVG) using Inkscape, Illustrator, or CAD:
   - Keep design dimensions within the **50 mm x 50 mm** work area.
   - Convert all text and strokes to closed paths (`Object to Path`).
   - Ungroup nested objects and remove hidden or clipping layers.
2. In the **Engrave** tab:
   - Under **Vector Input Source**, upload your `.svg` file (or generate a quick test shape using the Parametric Generator).
   - Select a material preset (e.g., `Softwood / Basswood` at 25% power, 100 sps; or `Cardstock` at 5% power, 150 sps).
   - Review the rendered **Toolpath Preview** in the center pane to confirm scaling and orientation.

### Phase 6: The Readiness Gate Verification
Before the **Start Engraving** button unlocks, the system automatically checks all 5 safety requirements:
- [x] **Controller Online:** Serial or TCP bridge active.
- [x] **Telemetry Fresh (<2s):** Live snapshot received without latency.
- [x] **Both Axes Physically Homed:** Machine coordinate frame calibrated.
- [x] **No Active Hardware Faults:** E-Stop, watchdog, or sensor interlocks clear.
- [x] **Valid Job Prepared in Bounds:** All segment coordinates fit inside the configured travel envelope.

### Phase 7: False Engraving (Dry Run / Motors Only) — MANDATORY FIRST PASS
1. Always enable the toggle: **False Engraving (Motors Only)**.
2. Click **Start Job (Dry Run)**.
3. The stepper motors will drive the mirrors along the exact toolpath while the laser diode remains completely **OFF (0% duty)**.
4. Verify:
   - The beam target area traces cleanly across your intended workpiece surface.
   - The mirrors do not hit mechanical limits or stretch wiring.
   - The motion is smooth without stepper stalling or jitter.

### Phase 8: Live Engraving Execution
1. Ensure everyone has laser safety goggles on and ventilation is active.
2. Disable the **False Engraving** toggle.
3. Click **Start Live Engrave**.
4. The server sequentially streams segments to the ESP32-S3 with flow-control handshaking:
   - The progress bar displays completed segments in real time.
   - Watch the cut area continuously.
   - If any smoke flare-up, material shifting, or unexpected movement occurs, immediately click the red **E-STOP** button in the header.

---

## 4. Prohibited Actions ("What the Operator Must NOT Do")

| Prohibited Action | Why It Is Dangerous / Harmful | Correct Procedure |
| :--- | :--- | :--- |
| **Never change microstepping mode during motion or while running a job.** | Modifying MS1/MS2/MS3 pins while stepping alters steps/mm dynamically, ruins position calibration, causes skipped steps, and invalidates homing. | Only change microstep mode from the Calibration tab while the machine is stationary in `IDLE`. Always re-home immediately after changing. |
| **Do not exceed 100–120 sps for 28BYJ-48 stepper moves.** | Geared 28BYJ-48 motors have high rotor reluctance. Running speeds above 120 sps causes torque loss, stalling, and lost steps. | Use preset defaults (80–100 sps for engraving; 150–200 sps for non-burn travel moves). |
| **Do not force laser duty above 90% (3685/4095).** | Diode lasers driven beyond 90% duty experience severe thermal degradation, optical wavelength shift, and rapid premature diode failure. | Stay within presets (5% to 45% is optimal for wood/paper/acrylic). The firmware will reject commands >90% with `LASER_DUTY_CLAMPED`. |
| **Do not bypass the Readiness Gate or force unhomed jobs.** | Running without homing means absolute position is unknown. The laser can deflect past the workpiece, burning machine casing or desktop. | Always run **Home All** after powering on or waking from driver idle. |
| **Do not manually push or twist mirror mounts while drivers are enabled.** | Forcing the motor rotor against energized coils strips the internal 64:1 nylon gear train and damages driver FETs. | Use the **Jog Step Buttons** in the UI, or disable drivers via UI before manual physical adjustments. |
| **Do not obstruct optical sensors during homing.** | Prematurely blocking the beam tricks the state machine into false zeroing, establishing an incorrect origin. | Keep hands and tools clear of the sensor slot flags during homing. |

---

## 5. Built-In Firmware Safety Interlocks

1. **Automatic Coil De-Energize (30s Idle Timeout):**
   - If no motion command is issued for 30 seconds, `AUTO_DISABLE_MS` disables the A4988 driver outputs.
   - This keeps the motors completely cool when not in use.
   - *Operator Note:* When drivers de-energize, the machine marks homing as invalidated (`IDLE_UNHOMED`). You must press **Home Now** before starting your next cut.
2. **500 ms Hardware Laser Watchdog:**
   - The firmware checks `m_laser.checkWatchdog(nowMs)` on every cooperative loop cycle.
   - If serial communications stall, the computer freezes, or network lag exceeds 500 ms, the laser is immediately turned off.
3. **Guarded Manual Pulse Station:**
   - The manual laser test pulse in the Calibration and Engrave tabs is restricted to a maximum duration of **500 ms** and requires a mandatory safety confirmation checkbox before firing.

---

## 6. Fault Codes & Troubleshooting Guide

| Fault Displayed | Root Cause | Operator Recovery Steps |
| :--- | :--- | :--- |
| `SENSOR_STUCK_ACTIVE` | The optical beam remained blocked during the `ClearInitial` or `PullOff` homing phase. | 1. Check if the mirror flag is mechanically caught in the sensor.<br>2. Check for ambient IR sunlight or reflective glare hitting the phototransistor.<br>3. Adjust the sensor module trimmer potentiometer. |
| `HOME_SEEK_LIMIT` | Motor stepped 45,000 steps without the sensor triggering. | 1. Verify the motor is physically rotating and not stalling.<br>2. Verify sensor wiring (GPIO 13 for X, GPIO 8 for Y).<br>3. Ensure direction inversion setting is correct. |
| `NOT_HOMED` | Attempted `move-abs` or job start before homing was completed. | 1. Click **Home All** in the UI.<br>2. Wait for state to show `IDLE_HOMED`. |
| `LASER_DUTY_CLAMPED` | Commanded power exceeded 90% (3685 duty). | 1. Reduce laser power percentage in preset or override to ≤ 90%. |
| `OUT_OF_BOUNDS` | Commanded coordinate is negative or exceeds configured envelope. | 1. Check SVG scaling.<br>2. Keep design within 0 to 50 mm in width/height. |
| `EMERGENCY_STOP` | Operator clicked E-STOP or serial safety trip fired. | 1. Clear any physical hazard or fire.<br>2. Click **Reset Hardware Fault** in the header.<br>3. Re-home both axes before resuming. |
