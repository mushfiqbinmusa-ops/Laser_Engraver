# Dual Stepper Motor & A4988 Driver Test Guide

**Sketch location:** `stepper_motor_test/stepper_motor_test.ino`  
**Target Board:** ESP32-S3 SuperMini  
**Motors:** Dual 28BYJ-48 Stepper Motors (Converted to 4-Wire Bipolar)  
**Drivers:** Dual Allegro A4988 Microstepping Drivers  
**Power Rails:** 5V VMOT (Motor Power) | 3.3V VDD (Logic from ESP32-S3)  
**No external libraries required** — uses standard ESP32 Arduino Core.

---

## Table of Contents

1. [System Architecture & Block Diagram](#1-system-architecture--block-diagram)
2. [28BYJ-48 Bipolar Conversion Guide](#2-28byj-48-bipolar-conversion-guide)
3. [A4988 Driver Specifics & Vref Current Calibration](#3-a4988-driver-specifics--vref-current-calibration)
4. [Circuit Wiring Diagrams](#4-circuit-wiring-diagrams)
   - [Single Motor Test Wiring (Motor A Only)](#41-single-motor-test-wiring-motor-a-only)
   - [Dual Motor Test Wiring (Motor A + Motor B)](#42-dual-motor-test-wiring-motor-a--motor-b)
5. [Microstepping Resolution & Kinematics Table](#5-microstepping-resolution--kinematics-table)
6. [Arduino IDE & Board Configuration](#6-arduino-ide--board-configuration)
7. [Phase-by-Phase Testing Roadmap](#7-phase-by-phase-testing-roadmap)
   - [Phase 1: Cold Logic Verification (VMOT OFF)](#phase-1-cold-logic-verification-vmot-off)
   - [Phase 2: Motor A Rotation & Direction (VMOT ON)](#phase-2-motor-a-rotation--direction-vmot-on)
   - [Phase 3: Speed Characterisation & Stall Limit](#phase-3-speed-characterisation--stall-limit)
   - [Phase 4: Microstepping Transitions (1/1 to 1/16)](#phase-4-microstepping-transitions-11-to-116)
   - [Phase 5: Gear Backlash Characterisation](#phase-5-gear-backlash-characterisation)
   - [Phase 6: Dual-Axis Coordinated Vectoring (Square, Circle)](#phase-6-dual-axis-coordinated-vectoring-square-circle)
   - [Phase 7: Full Automated Self-Test & Benchmark](#phase-7-full-automated-self-test--benchmark)
8. [Troubleshooting Guide](#8-troubleshooting-guide)
9. [Serial Command Reference](#9-serial-command-reference)

---

## 1. System Architecture & Block Diagram

```mermaid
flowchart TD
    subgraph Power["Power Architecture"]
        USB["USB-C 5V Supply"] --> ESP32["ESP32-S3 SuperMini"]
        ESP32 -->|"3.3V Logic Rail"| VDD["VDD (A4988 Logic)"]
        EXT_5V["External 5V / 2A Supply"] -->|"5V VMOT Rail"| VMOT["VMOT (A4988 Motor Power)"]
        GND_SYS["Common Ground Rail (GND)"] --- ESP32
        GND_SYS --- EXT_5V
    end

    subgraph Controller["ESP32-S3 SuperMini"]
        IO5["GPIO 5 (STEP_A)"]
        IO6["GPIO 6 (DIR_A)"]
        IO7["GPIO 7 (EN_A)"]
        IO9["GPIO 9 (STEP_B)"]
        IO10["GPIO 10 (DIR_B)"]
        IO11["GPIO 11 (EN_B)"]
        MS_BUS["Shared Microstep Bus\nGPIO 1 (MS1)\nGPIO 2 (MS2)\nGPIO 38 (MS3)"]
        IO13["GPIO 13 (Status LED)"]
    end

    subgraph Drivers["A4988 Stepper Drivers"]
        subgraph DriverA["Driver A (X-Axis)"]
            DRV_A["A4988 Driver Module"]
            CAP_A["100uF Electrolytic Decoupling Cap"]
        end
        subgraph DriverB["Driver B (Y-Axis)"]
            DRV_B["A4988 Driver Module"]
            CAP_B["100uF Electrolytic Decoupling Cap"]
        end
    end

    subgraph Motors["28BYJ-48 Steppers (Bipolar Mod)"]
        MOT_A["Motor A (X Steering Mirror)\nCoil 1: Blue/Pink\nCoil 2: Orange/Yellow"]
        MOT_B["Motor B (Y Steering Mirror)\nCoil 1: Blue/Pink\nCoil 2: Orange/Yellow"]
    end

    IO5 --> DRV_A
    IO6 --> DRV_A
    IO7 --> DRV_A
    IO9 --> DRV_B
    IO10 --> DRV_B
    IO11 --> DRV_B
    MS_BUS --> DRV_A
    MS_BUS --> DRV_B

    VMOT --> CAP_A --> DRV_A
    VMOT --> CAP_B --> DRV_B
    VDD --> DRV_A
    VDD --> DRV_B

    DRV_A --> MOT_A
    DRV_B --> MOT_B
```

---

## 2. 28BYJ-48 Bipolar Conversion Guide

The stock 28BYJ-48 is a 5-wire unipolar stepper driven by Darlington arrays (e.g. ULN2003A). In unipolar mode, only half of each coil carries current at any given instant. 

By converting the motor to a **4-wire bipolar configuration**, both phases are driven symmetrically by an H-bridge driver (A4988), doubling torque production, improving efficiency, and allowing microstepping.

### Step-by-Step Conversion Procedure

1. **Remove Blue Plastic Cover:**
   - Gently pry the small tabs of the blue plastic cap on the rear of the 28BYJ-48 using a hobby knife or flat miniature screwdriver.
   - Slide the cap off to expose the internal small printed circuit board (PCB) where the 5 colored leads are soldered.

2. **Locate the Center Tap Trace:**
   - You will see 5 solder pads: `Blue`, `Pink`, `Yellow`, `Orange`, and `Red`.
   - The `Red` wire is the center tap. A trace runs across the PCB connecting the red wire's solder pad to the midpoint of the internal coils.

3. **Cut the PCB Trace:**
   - With an X-Acto knife or sharp razor, carefully scrape/slice through the copper trace running between the red pad and the center connection.
   - Cut through until the underlying substrate is visible. Ensure no stray copper burrs bridge the cut.

4. **Isolate or Desolder the Red Wire:**
   - Snip the red wire flush at the PCB, or heat shrink its free end. It will **not** be connected to the A4988.

5. **Verify with a Multimeter (DMM):**
   - Measure resistance between **Blue** and **Pink**: Should measure approximately **95 Ω to 105 Ω** (Phase A).
   - Measure resistance between **Orange** and **Yellow**: Should measure approximately **95 Ω to 105 Ω** (Phase B).
   - Measure resistance between **Red** and **any other wire**: Must read **infinite (open circuit / OL)**.
   - Measure resistance between **Blue** and **Yellow**: Must read **infinite (isolated phases)**.

### Wiring Assignment to A4988

| Motor Wire Color | Motor Phase | A4988 Pin |
|---|---|---|
| **Blue** | Phase A (+) | **1A** |
| **Pink** | Phase A (-) | **1B** |
| **Yellow** | Phase B (+) | **2A** |
| **Orange** | Phase B (-) | **2B** |
| **Red** | Cut Center Tap | **NC (Disconnected)** |

> [!TIP]
> If a motor rotates in reverse of your desired direction, you can either issue the firmware command `DIR CCW` or simply swap the two wires of one coil (e.g. swap Blue and Pink).

---

## 3. A4988 Driver Specifics & Vref Current Calibration

### Essential Hardware Pin Strapping

1. **RST and SLP Tied Together:**
   - The A4988 `~RESET` and `~SLEEP` pins are active LOW logic inputs.
   - Jumper `RESET` directly to `SLEEP`. The driver's internal pull-up on SLEEP holds RESET high, keeping the driver active.
2. **ENABLE Pin Control:**
   - The `~ENABLE` pin is **active LOW**.
   - Connected to ESP32 GPIO: `LOW` = Driver energized; `HIGH` = Outputs high-impedance (de-energized, cool idle).
3. **VMOT Decoupling Capacitor:**
   - Place a **100 µF (16V–35V) electrolytic capacitor** directly across `VMOT` and `GND` as close to each A4988 module as possible.
   - **Crucial:** Never disconnect a motor while VMOT is energized; inductive flyback will instantly destroy the A4988 MOSFETs.

### Current Limit Setting ($V_{ref}$ Formula)

The 28BYJ-48 converted to bipolar has a DC coil resistance of $\approx 100\,\Omega$. When running on a 5V supply, the natural continuous DC current is:
$$I = \frac{5\,\text{V}}{100\,\Omega} = 50\,\text{mA}$$

To achieve fast step transitions and overcome gear inertia at higher step rates, the A4988 chopper is calibrated between **150 mA and 200 mA** per phase ($I_{max}$).

The Allegro A4988 current trip formula is:
$$I_{max} = \frac{V_{ref}}{8 \times R_s}$$

Where $R_s$ is the onboard current sense resistor.

#### Identifying Your Current Sense Resistors ($R_s$)
Inspect the two small surface-mount resistors on the A4988 module near the chip:
- Resistor labeled `R100` $\implies R_s = 0.10\,\Omega$ *(Most common on Pololu and standard modules)*
- Resistor labeled `R050` $\implies R_s = 0.05\,\Omega$
- Resistor labeled `R200` $\implies R_s = 0.20\,\Omega$

#### Target $V_{ref}$ Calculation Table

For the standard $R_s = 0.10\,\Omega$ module:
$$V_{ref} = I_{max} \times 8 \times R_s = I_{max} \times 0.8$$

| Target Phase Current ($I_{max}$) | $R_s = 0.10\,\Omega$ ($V_{ref}$) | $R_s = 0.05\,\Omega$ ($V_{ref}$) | Thermal Profile |
|---|---|---|---|
| **120 mA** | **0.096 V (96 mV)** | 0.048 V (48 mV) | Cool, low torque |
| **150 mA (Recommended)** | **0.120 V (120 mV)** | 0.060 V (60 mV) | **Optimal balance of torque & heat** |
| **180 mA** | **0.144 V (144 mV)** | 0.072 V (72 mV) | High torque, warm motor |
| **200 mA (Max)** | **0.160 V (160 mV)** | 0.080 V (80 mV) | Maximum torque, hot motor |

### How to Adjust $V_{ref}$:
1. Connect the ESP32-S3 logic ground (`GND`) to your multimeter's negative black lead (e.g. using an alligator clip).
2. Set multimeter to DC Volts (millivolt range, e.g. 200 mV or 2 V DC).
3. Connect 3.3V logic supply to A4988 `VDD` and `GND` (motor 5V VMOT can be ON or OFF during measurement).
4. Touch the multimeter's red probe carefully to the metal top of the small onboard potentiometer (trimpot).
5. Using a miniature ceramic or insulated screwdriver, gently rotate the trimpot until the meter reads **120 mV (0.12 V)**.
   - Clockwise usually increases voltage; counter-clockwise decreases it.

---

## 4. Circuit Wiring Diagrams

### 4.1 Single Motor Test Wiring (Motor A Only)

For initial bring-up and characterising Motor A:

```
  ESP32-S3 SuperMini               A4988 Driver Module A
┌─────────────────────┐          ┌───────────────────────┐
│                 GND ├──────────┤ GND (Logic Ground)    │
│                 3.3V├──────────┤ VDD (Logic Power)     │
│ GPIO 5 (STEP_A)     ├──────────┤ STEP                  │
│ GPIO 6 (DIR_A)      ├──────────┤ DIR                   │
│ GPIO 7 (EN_A)       ├──────────┤ ENABLE                │
│ GPIO 1 (MS1)        ├──────────┤ MS1                   │
│ GPIO 2 (MS2)        ├──────────┤ MS2                   │
│ GPIO 38 (MS3)       ├──────────┤ MS3                   │
│                     │          │ RESET ──┐             │
│                     │          │ SLEEP ──┘ (Jumpered)  │
└─────────────────────┘          │                       │
                                 │ 1A ────── Blue Wire   │
  5V External Supply             │ 1B ────── Pink Wire   │──> 28BYJ-48
┌─────────────────────┐          │ 2A ────── Yellow Wire │    Motor A
│ 5V (+)              ├──────────┤ VMOT                  │  (Red wire CUT)
│ GND (-)             ├──┬───────┤ GND (Motor Ground)    │
└─────────────────────┘  │       │ 2B ────── Orange Wire │
                      ┌──┴──┐    └───────────────────────┘
                      │100uF│
                      │ Cap │ (Electrolytic across VMOT & GND)
                      └──┬──┘
                         │
     [Common Ground] ────┴─────────────────────── (To ESP32 GND)
```

---

### 4.2 Dual Motor Test Wiring (Motor A + Motor B)

Complete dual-axis wiring layout for the Laser-XY mirror steering assembly:

```
                      ESP32-S3 SuperMini
                    ┌─────────────────────┐
                    │ 3.3V            GND ├─── Common GND Bus
                    │ 3.3V            GND │
                    │ RST              5V │
                    │ GPIO 4           5V │
   STEP_A ──────────┤ GPIO 5         GPIO 43 (TX0)
   DIR_A  ──────────┤ GPIO 6         GPIO 44 (RX0)
   EN_A   ──────────┤ GPIO 7         GPIO 1  ├────── MS1 Bus
                    │ GPIO 8         GPIO 2  ├────── MS2 Bus
   STEP_B ──────────┤ GPIO 9         GPIO 42 │
   DIR_B  ──────────┤ GPIO 10        GPIO 41 │
   EN_B   ──────────┤ GPIO 11        GPIO 40 │
                    │ GPIO 12        GPIO 39 │
   Status LED ──────┤ GPIO 13        GPIO 38 ├────── MS3 Bus
                    └─────────────────────┘

               A4988 - MOTOR A (X)                  A4988 - MOTOR B (Y)
             ┌─────────────────────┐              ┌─────────────────────┐
  EN_A ──────┤ ENABLE       ENABLE ├──────────────┤ ENABLE       ENABLE ├── EN_B
   MS1 ──────┤ MS1             MS1 ├──────┬───────┤ MS1             MS1 ├── MS1
   MS2 ──────┤ MS2             MS2 ├──┬───┼───────┤ MS2             MS2 ├── MS2
   MS3 ──────┤ MS3             MS3 ├──┼───┼───┬───┤ MS3             MS3 ├── MS3
             │ RESET ──┐           │  │   │   │   │ RESET ──┐           │
             │ SLEEP ──┘ (Tied)    │  │   │   │   │ SLEEP ──┘ (Tied)    │
STEP_A ──────┤ STEP           STEP │  │   │   │   │ STEP           STEP ├── STEP_B
 DIR_A ──────┤ DIR             DIR │  │   │   │   │ DIR             DIR ├── DIR_B
   GND ──────┤ GND             GND │  │   │   │   │ GND             GND ├── GND
  3.3V ──────┤ VDD             VDD │  │   │   │   │ VDD             VDD ├── 3.3V
             │ 1B ── Pink          │  │   │   │   │ 1B ── Pink          │
             │ 1A ── Blue  Motor A │  │   │   │   │ 1A ── Blue  Motor B │
             │ 2A ── Yellow (X)    │  │   │   │   │ 2A ── Yellow (Y)    │
             │ 2B ── Orange        │  │   │   │   │ 2B ── Orange        │
   GND ──────┤ GND             GND │  │   │   │   │ GND             GND ├── GND
5V VMOT ─────┤ VMOT           VMOT │  │   │   │   │ VMOT           VMOT ├── 5V VMOT
             └──────────┬──────────┘  │   │   │   └──────────┬──────────┘
                      ┌─┴─┐           │   │   │            ┌─┴─┐
                      │Cap│ 100uF     │   │   │            │Cap│ 100uF
                      └───┘           └───┼───┼────────────┘   │
                                          └───┼────────────────┘
                                              └────────────────┘
```

> [!IMPORTANT]
> - Ensure all grounds (**ESP32-S3 GND**, **5V Power Supply GND**, **A4988 Logic GND**, **A4988 Motor GND**) are tied to a single common low-impedance ground point.
> - Never route motor high-current return loops through the ESP32 breadboard tracks.

---

## 5. Microstepping Resolution & Kinematics Table

The 28BYJ-48 features a 4-stage internal spur reduction gearbox. 
- Rotor: 32 full steps per revolution (11.25° step angle).
- Gear ratio: $63.68395 : 1$ (commonly approximated as $64:1$).
- Full-step output resolution: $32 \times 64 = 2048$ steps per revolution.

| Mode | Command | MS1 | MS2 | MS3 | Steps/Rev | Deg / Step | Arcmin / Step | Arcsec / Step | Mirror Res (@ 100mm) |
|---|---|---|---|---|---|---|---|---|---|
| **1/1 Full** | `MICRO 1` | LOW | LOW | LOW | **2,048** | 0.1758° | 10.55' | 632.8" | 306 µm |
| **1/2 Half** | `MICRO 2` | HIGH | LOW | LOW | **4,096** | 0.0879° | 5.27' | 316.4" | 153 µm |
| **1/4 Quarter**| `MICRO 4` | LOW | HIGH | LOW | **8,192** | 0.0439° | 2.64' | 158.2" | 76 µm |
| **1/8 Eighth** | `MICRO 8` | HIGH | HIGH | LOW | **16,384** | 0.0220° | 1.32' | 79.1" | 38 µm |
| **1/16 16th**  | `MICRO 16`| HIGH | HIGH | HIGH| **32,768** | 0.0110° | 0.66' | 39.5" | **19 µm** |

> [!NOTE]
> **Optical Reflection Multiplier:**  
> When steering a laser beam with a mirror, rotation of the mirror by an angle $\theta$ deflects the reflected beam by **$2\theta$** (Law of Reflection). 
> At 1/16 microstepping, each individual microstep delivers an optical beam resolution of **0.022° (1.32 arcmin)**!

---

## 6. Arduino IDE & Board Configuration

### Board Settings (Tools Menu)

| Setting | Selection | Purpose |
|---|---|---|
| **Board** | `ESP32S3 Dev Module` | Standard core for ESP32-S3 |
| **USB Mode** | `Hardware CDC and JTAG` / `USB-OTG` | Native USB communication |
| **USB CDC On Boot** | **`Enabled`** | **Mandatory** for Serial Monitor over USB-C |
| **Upload Mode** | `UART0 / Hardware CDC` | Automatic flashing |
| **Flash Size** | `4MB` or `8MB` | Match your SuperMini flash |
| **Partition Scheme**| `Default 4MB with spiffs` | Standard layout |
| **PSRAM** | `Disabled` | SuperMini does not feature external PSRAM |
| **CPU Frequency** | `240MHz` | Full performance for precision microsecond pulses |

---

## 7. Phase-by-Phase Testing Roadmap

Follow these 7 structured validation phases sequentially:

---

### Phase 1: Cold Logic Verification (VMOT OFF)

**Objective:** Verify ESP32-S3 GPIO outputs, serial commands, and microstepping pin states before turning on 5V motor power.

1. Disconnect 5V VMOT supply (or leave supply turned OFF). Keep USB-C plugged in.
2. Open Serial Monitor at **115200 baud** (Newline).
3. Confirm boot banner appears:
   ```text
   +==================================================+
   |   STEPPER MOTOR & A4988 DRIVER TEST FIRMWARE     |
   |   ESP32-S3 Super Mini  |  Dual 28BYJ-48 Bipolar  |
   +==================================================+
   ```
4. Type `STATUS` and press Enter. Verify:
   - Drivers Disabled: `YES`
   - Positions: `A=0, B=0`
5. Test microstepping pin switching:
   - Type `MICRO 16`. Verify response: `Microstepping set to 1/16`.
   - Measure voltage with DMM on pins MS1, MS2, MS3: All three must read **3.3V**.
   - Type `MICRO 1`. Measure MS1, MS2, MS3: All three must read **0.0V**.
6. **Pass Criteria:** Firmware boots cleanly, commands respond, GPIO logic transitions properly.

---

### Phase 2: Motor A Rotation & Direction (VMOT ON)

**Objective:** Confirm coil wiring, holding torque, and forward/reverse rotation on Motor A.

1. Turn ON the external 5V VMOT power supply.
2. Type `ENABLE`.
   - You should feel holding torque on the Motor A output shaft (it will resist turning by hand).
3. Type `STEP 200`.
   - Motor A should smoothly take 200 steps clockwise.
4. Type `STEP -200`.
   - Motor A should step 200 steps counter-clockwise, returning to the starting point.
5. Type `REV 1`.
   - Motor A should rotate exactly one full revolution of the output shaft (2048 steps).
6. **Pass Criteria:** Motor turns smoothly without clicking, buzzing, or running backwards.

---

### Phase 3: Speed Characterisation & Stall Limit

**Objective:** Determine the maximum reliable pulse rate and torque drop-off threshold.

1. Test conservative stepping speed:
   - `SPEED 300` $\implies$ Motor spins continuously at ~8.8 RPM.
   - Type `STOP` to halt.
2. Step speed progression:
   - `SPEED 500` $\implies$ Verify smooth rotation.
   - `SPEED 800` $\implies$ Normal upper speed limit in full-step mode.
   - `SPEED 1200` $\implies$ Listen for skipped steps or high-pitched rotor stall.
3. Automated stall diagnostic:
   - Type `STALL 800 2048`.
   - The firmware measures execution duration with microsecond timers and reports timing variance.
4. **Pass Criteria:** Motor completes continuous rotation up to 600–800 steps/sec in full-step mode without losing synchronisation.

---

### Phase 4: Microstepping Transitions (1/1 to 1/16)

**Objective:** Verify smooth acoustic resonance suppression and fine step resolution.

1. Set Half-Step:
   - `MICRO 2`
   - `REV 1` (Motor performs 4,096 pulses per rev; noticeably quieter).
2. Set 1/16 Microstepping:
   - `MICRO 16`
   - `REV 1` (Motor performs 32,768 pulses per rev; near-silent gear glide).
3. Observe output shaft:
   - Motion is ultra-smooth with zero step-induced vibration, ideal for laser mirrors.
4. **Pass Criteria:** At 1/16 microstepping, motor operates silently without resonance vibration.

---

### Phase 5: Gear Backlash Characterisation

**Objective:** Measure internal spur gearbox hysteresis (gear play) when reversing direction.

1. Type `BACKLASH`.
2. Firmware runs a 3-second countdown.
3. Observe the output shaft / mirror indicator:
   - Motor pre-loads forward by +200 steps.
   - Firmware steps reverse in increments of: 5, 10, 15, 20, 25, 30, 40 steps.
4. Note the step threshold at which physical movement is visually detected upon reversing.
5. **Pass Criteria:** Backlash is quantified (typically 15 to 30 steps / 0.5° to 1.0°). Use this value to configure software backlash compensation.

---

### Phase 6: Dual-Axis Coordinated Vectoring (Square, Circle)

**Objective:** Verify synchronous dual-axis motion (Bresenham / DDA interpolation) for X-Y mirror rastering.

1. Test diagonal simultaneous step:
   - `DUAL STEP 500 500`
   - Both motors step synchronously at 45°.
2. Test coordinated square:
   - `DUAL SQUARE 600`
   - Motor A and B draw a closed square polygon and return to start.
3. Test coordinated circle:
   - `DUAL CIRCLE 400 32`
   - Coordinated sine/cosine trajectory tracing.
4. Test automated return:
   - `DUAL HOME`
   - Both axes smoothly return to coordinate `(0, 0)`.
5. **Pass Criteria:** Both axes move simultaneously without binding or timing jitter.

---

### Phase 7: Full Automated Self-Test & Benchmark

**Objective:** Execute automated hardware self-test suite and measure ESP32-S3 interrupt/timer headroom.

1. Type `TEST`.
   - Firmware runs all 6 hardware tests: LED, Microstepping logic, Motor A motion, Motor B motion, Dual diagonal vector, and Microstepping resolution switch.
   - Verify `[ALL SELF-TEST CHECKS PASSED [OK]]`.
2. Type `BENCHMARK`.
   - ESP32-S3 evaluates raw GPIO step generation and DDA math overhead.
   - Observe step pulse capability exceeds 100 kHz (far above 28BYJ-48 requirement).
3. **Pass Criteria:** All automated diagnostics report `PASS`.

---

## 8. Troubleshooting Guide

| Symptom | Probable Cause | Corrective Action |
|---|---|---|
| **Motor hums / whines loudly but does not turn** | 1. Coil wiring swapped (e.g. Phase A and B crossed)<br>2. Step rate exceeds motor starting torque<br>3. $V_{ref}$ set too low ($< 80\,\text{mV}$)<br>4. 5V VMOT supply absent | 1. Check wire pairs with DMM: Blue-Pink must be ~100Ω, Orange-Yellow ~100Ω.<br>2. Reduce speed: Try `SPEED 200`.<br>3. Adjust $V_{ref}$ to 120 mV.<br>4. Verify 5V VMOT rail with DMM. |
| **Motor shaft spins backwards** | Direction inverted | Type `DIR CCW` to invert logic, or swap the Blue and Pink wires on the A4988 header. |
| **Motor gets excessively hot (> 60°C)** | 1. $V_{ref}$ calibrated too high ($> 180\,\text{mV}$)<br>2. Motor left energized while stationary | 1. Re-adjust trimpot to 120 mV.<br>2. Confirm `AUTODISABLE 1` is active (de-energizes coils after 30s idle). |
| **A4988 driver cuts out intermittently** | Driver thermal shutdown (overheating) | 1. Check $V_{ref}$; lower if necessary.<br>2. Attach adhesive aluminum heatsink to A4988 IC.<br>3. Ensure logic VDD is 3.3V, not 5V. |
| **Motor stutters or loses steps at high speed** | Rotor inertia stall (characteristic of 28BYJ-48) | 1. Switch to microstepping (`MICRO 4` or `MICRO 16`).<br>2. Use acceleration ramps rather than instantaneous full-speed jumps. |
| **Serial Monitor displays garbled symbols** | Baud rate mismatch | Set Serial Monitor baud rate to **115200**. |
| **Serial Monitor empty / no port found** | `USB CDC On Boot` disabled | In Arduino IDE Tools menu, set `USB CDC On Boot = Enabled` and re-flash. |

---

## 9. Serial Command Reference

| Command | Arguments | Example | Description |
|---|---|---|---|
| `STEP` | `[<n>]` | `STEP 500` / `STEP -200` | Step Motor A by $N$ microsteps (+ forward, - reverse). |
| `SPEED` | `<sps>` | `SPEED 600` | Run Motor A continuously at specified steps/sec. |
| `STOP` | *None* | `STOP` | Stop continuous motion immediately. |
| `GOTO` | `<pos>` | `GOTO 2048` | Move Motor A to absolute step coordinate. |
| `POS` | *None* | `POS` | Report current step positions, angles, and revolutions. |
| `ZERO` | *None* | `ZERO` | Reset axis position counters to 0 (Home). |
| `DIR` | `<CW\|CCW>` | `DIR CCW` | Invert or restore default Motor A rotation direction. |
| `REV` | `<n>` | `REV 1.5` / `REV -2` | Move Motor A by exact number of output revolutions. |
| `RPM` | *None* | `RPM` | Display current rotational speed in RPM. |
| `MICRO` | `<1\|2\|4\|8\|16>` | `MICRO 16` | Set microstepping mode via MS1/MS2/MS3 pins. |
| `ENABLE` | *None* | `ENABLE` | Assert driver ENABLE pin (active LOW, energized). |
| `DISABLE`| *None* | `DISABLE` | Release driver ENABLE pin (high-Z, coils off). |
| `DUAL STEP` | `<nA> <nB>` | `DUAL STEP 200 400` | Step Motor A and Motor B simultaneously. |
| `DUAL GOTO` | `<xA> <yB>` | `DUAL GOTO 1000 1000` | Coordinated Bresenham move to $(X, Y)$ coordinate. |
| `DUAL SPEED`| `<sA> <sB>` | `DUAL SPEED 600 600` | Configure default individual speeds for A and B. |
| `DUAL CIRCLE`| `<radius> [segs]` | `DUAL CIRCLE 500 48` | Trace parametric circular vector path. |
| `DUAL SQUARE`| `<size>` | `DUAL SQUARE 600` | Trace square vector path of side length $S$. |
| `DUAL HOME`| *None* | `DUAL HOME` | Return both axes back to $(0, 0)$ coordinate. |
| `BACKLASH`| *None* | `BACKLASH` | Run spur gear backlash diagnostic sequence. |
| `STALL` | `<sps> <steps>` | `STALL 1000 2048` | High-speed step generator test & jitter analysis. |
| `TEST` | *None* | `TEST` | Execute full automated hardware self-test suite. |
| `BENCHMARK`| *None* | `BENCHMARK` | Measure ESP32-S3 max pulse generation throughput. |
| `ACCEL` | `<sps2>` | `ACCEL 3000` | Set acceleration parameter (steps/sec$^2$). |
| `AUTODISABLE`| `<0\|1>` | `AUTODISABLE 1` | Enable/disable 30-second idle driver power cut. |
| `STATUS` / `?`| *None* | `STATUS` | Print comprehensive status report in ASCII box. |
| `HELP` / `H`| *None* | `HELP` | Print command cheat sheet and safety notes. |
| `RESET` | *None* | `RESET` | Restore all settings to safe defaults, disable drivers. |
| **`X`** | *Key* | `X` | **Emergency abort** — instantly halts any active move. |
