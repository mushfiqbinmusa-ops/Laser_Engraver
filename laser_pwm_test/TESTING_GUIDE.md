# Laser Test Firmware — Step-by-Step Guide

**Sketch location:** `laser_pwm_test/laser_pwm_test.ino`  
**Target board:** ESP32-S3 Super Mini  
**No extra libraries needed** — only the ESP32 Arduino Core is required.

---

## Table of Contents

1. [Wiring Before You Flash](#step-0-wiring)
2. [Arduino IDE Setup](#step-1-arduino-ide)
3. [Board & Port Configuration](#step-2-board-port)
4. [Upload the Sketch](#step-3-upload)
5. [Open the Serial Monitor](#step-4-serial)
6. [Command-by-Command Testing Workflow](#step-5-testing)
   - [Stage A — Safety Check (No Laser)](#stage-a)
   - [Stage B — Dead Band Discovery](#stage-b)
   - [Stage C — Power Characterisation](#stage-c)
   - [Stage D — Frequency Sensitivity](#stage-d)
   - [Stage E — Thermal Stress / Soak Test](#stage-e)
   - [Stage F — Pulse & Burst Behaviour](#stage-f)
   - [Stage G — Full Unlock Test (Optional)](#stage-g)
7. [Recording Your Results](#step-6-results)
8. [All Commands — Quick Reference Card](#step-7-reference)

---

<a id="step-0-wiring"></a>
## Step 0 — Wiring Before You Flash

Connect **only these three wires** for the test (no steppers, no endstops needed):

```
ESP32-S3 Super Mini         Laser Driver Board
────────────────────         ──────────────────
GPIO 10         ──────────→  PWM IN
GND             ──────────→  GND  (SHARE ground between ESP32 and laser supply)

Laser 12V Supply
────────────────
12V positive    ──────────→  VIN on laser driver
12V negative    ──────────→  GND on laser driver (same GND rail as ESP32)
```

> **IMPORTANT — Shared Ground**  
> The ESP32-S3 GND and the 12V supply GND **must be connected together**.  
> Without a shared ground, the PWM signal has no reference and the laser behaves erratically or not at all.

> **Do NOT power the laser from the ESP32 5V pin.**  
> Laser modules draw 1–5 A. The ESP32's 5V rail (from USB) can only supply ~500 mA safely.

**Physical safety before powering on:**
- [ ] Laser is pointed at a dark, non-reflective, non-flammable surface (black card, anodized aluminum, dark wood)
- [ ] Laser safety goggles (OD 5+ at your laser's wavelength) are on your face
- [ ] No one else is in the room, or they also have goggles
- [ ] The 12V supply is OFF for now (you'll turn it on at Stage B)

---

<a id="step-1-arduino-ide"></a>
## Step 1 — Arduino IDE Setup

### 1.1 Install Arduino IDE 2.x

Download from: https://www.arduino.cc/en/software  
Install and launch it.

### 1.2 Add ESP32 Board Support

1. Open **File → Preferences**
2. In the **"Additional boards manager URLs"** field, paste:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Click **OK**
4. Open **Tools → Board → Boards Manager**
5. Search for `esp32`
6. Find **"esp32 by Espressif Systems"** — install version **2.0.14** or newer
7. Wait for the install to finish (it downloads ~250 MB)

### 1.3 No Extra Libraries Needed

The sketch uses **only** `ledcSetup`, `ledcAttachPin`, `ledcWrite` — all built into the ESP32 Arduino Core you just installed. You do **not** need to install anything from the Library Manager.

---

<a id="step-2-board-port"></a>
## Step 2 — Board & Port Configuration

### 2.1 Select the Board

Go to **Tools → Board → esp32 → ESP32S3 Dev Module**

> The "Super Mini" doesn't have its own board entry — "ESP32S3 Dev Module" is the correct choice.

### 2.2 Board Settings (Tools menu)

| Setting | Value |
|---------|-------|
| Board | ESP32S3 Dev Module |
| USB Mode | **USB-OTG (TinyUSB)** |
| USB CDC On Boot | **Enabled** |
| Upload Mode | **UART0 / Hardware CDC** |
| Flash Size | 8MB (or 4MB if your module has 4MB) |
| Partition Scheme | Default 4MB with spiffs (or 8MB App) |
| PSRAM | Disabled |
| CPU Frequency | 240MHz |

> **USB CDC On Boot = Enabled** is critical. It makes the Serial Monitor work over the same USB cable you use to flash. Without this, you'll see nothing in the monitor.

### 2.3 Put the Board into Download Mode

The Super Mini sometimes needs help entering flash mode:

1. Hold the **BOOT** button (the one labeled `BOOT` or `0`)
2. Press and release the **RST** button
3. Release **BOOT**
4. The board is now in download mode

### 2.4 Select the Port

Go to **Tools → Port** and select the port that appeared when you plugged in the ESP32-S3.  
On Linux it will look like `/dev/ttyACM0` or `/dev/ttyUSB0`.  
On Windows: `COM3`, `COM4`, etc.

> If no port appears, try the BOOT+RST method above, or install the CH340 driver (if your board uses that USB chip).

---

<a id="step-3-upload"></a>
## Step 3 — Upload the Sketch

1. Open the folder `laser_pwm_test/` in Arduino IDE (**File → Open** → navigate to `laser_pwm_test.ino`)
2. Click the **→ Upload** button (right arrow at top left)
3. Watch the output window at the bottom

You should see something like:
```
Connecting...
Chip is ESP32-S3 (revision v0.1)
Uploading stub...
Running stub...
Stub running...
Changing baud rate to 921600
...
Writing at 0x00010000... (100%)
Leaving...
Hard resetting via RTS pin...
```

If you see `Failed to connect`, try the BOOT+RST method from Step 2.3 again just as the "Connecting..." message appears.

---

<a id="step-4-serial"></a>
## Step 4 — Open the Serial Monitor

1. Click **Tools → Serial Monitor** (or press `Ctrl+Shift+M`)
2. Set baud rate to **115200** (bottom-right dropdown in the monitor)
3. Set line ending to **Newline** (the dropdown next to baud rate)

You should immediately see the boot banner:

```
+==================================================+
|    LASER CHARACTERISATION TEST FIRMWARE          |
|    ESP32-S3 Super Mini  |  12-bit LEDC PWM       |
+==================================================+
|  PWM Pin     : GPIO 10                          |
|  Frequency   : 5000 Hz default                  |
|  Resolution  : 12-bit (0-4095)                  |
|  Safety Lock : ON (max 90%)                     |
+==================================================+
|  LASER IS OFF at boot.                          |
|  Type HELP for command list.                    |
+==================================================+
```

If you see garbled text, the baud rate is wrong — set it to 115200.

Type `HELP` and press Enter to confirm communication is working.

---

<a id="step-5-testing"></a>
## Step 5 — Command-by-Command Testing Workflow

Work through these stages **in order**. Don't skip Stage A.

---

<a id="stage-a"></a>
### Stage A — Safety Check (Laser 12V Supply OFF)

**Goal:** Verify the ESP32 is responding and that GPIO 10 is outputting a PWM signal — without any risk of the laser firing.

**Keep the 12V laser supply OFF for this entire stage.**

**A1. Check firmware is alive:**
```
?
```
Expected output:
```
+======================================+
|       LASER STATUS REPORT            |
+======================================+
|  Laser State    : OFF                |
|  Power (%)      : 0.0                |
|  Raw Duty       : 0    / 4095        |
|  PWM Frequency  : 5000 Hz            |
|  Safety Lock    : ON  (<=90%)        |
|  GPIO Pin       : 10                 |
+======================================+
```

**A2. Set 50% power (GPIO 10 will output PWM, but no laser fires yet):**
```
S50
```
Expected:
```
>> S50
[S] Power set to 50% -> duty=2047/4095
```

Use a multimeter on GPIO 10 (AC voltage mode or oscilloscope if you have one):
- You should read approximately 1.65V AC average (3.3V × 50% duty = 1.65V average)

**A3. Turn off again:**
```
OFF
```

**A4. Verify safety ceiling:**
```
S95
```
Expected:
```
[SAFETY] 95% exceeds ceiling (90%). Type UNLOCK to bypass.
```
Good — the safety ceiling is working.

---

<a id="stage-b"></a>
### Stage B — Dead Band Discovery

**Goal:** Find the minimum PWM % at which your laser driver actually activates the laser diode.

**Put on your laser safety goggles. Now turn on the 12V supply.**

Point the laser at the test surface (black card, ~20 cm away) in a dim room so you can see the faintest glow.

**B1. Start from 0 and go up slowly in small steps:**

```
S0
```
```
S5
```
```
S8
```
```
S10
```
```
S12
```
```
S15
```
```
S20
```

At each step, look for:
- Any visible dot/spot on the target surface
- Any glow from the laser aperture (do NOT look directly — look at the target)

**Record the first % where you see any output at all.** This is your **dead band threshold**.

For most Chinese PWM laser drivers, the dead band is 5–20%.

**B2. Find exact threshold more precisely:**

Once you know roughly where it starts (say, at S15), try:
```
S13
S14
S15
S16
```
Note the exact % where it first fires.

**B3. Turn laser off:**
```
OFF
```

**Write down:** `Dead band threshold = ____%`

---

<a id="stage-c"></a>
### Stage C — Power Characterisation Sweep

**Goal:** Map the full power curve from dead band to maximum.

**C1. Run the quick sweep (automated):**

```
QSWEEP
```

This runs 0% → 100% in 5% steps, 1 second per step, with a 3-second countdown warning. Watch the target surface and note at which steps:
- Light first appears (confirms dead band from Stage B)
- Light noticeably increases
- A visible mark appears on the black card surface (this is your **engraving threshold**)
- A burn/char appears in under 1 second (this is your **cutting/engraving max**)

The output looks like:
```
[SWEEP] 0% -> 100%, step=5%, dwell=1000ms
        SAFETY: Wear goggles! Laser fires in 3s...
  3...
  2...
  1...
    0%  duty=   0  [....................]
    5%  duty= 204  [....................]
   10%  duty= 409  [#...................]
   15%  duty= 614  [###.................]
   20%  duty= 819  [####................]
   ...
   90%  duty=3686  [##################.]
[SWEEP] Done. Laser OFF.
```

**C2. Fine-grained sweep around the engraving threshold:**

If your engraving threshold is around 30%, do:
```
SWEEP 25 40 1 2000
```
(25% to 40%, 1% per step, 2 seconds per step — enough time to see burn marks)

**C3. Test a specific power level for 3 seconds on fresh card stock:**
```
PULSE 30 3000
```
```
PULSE 50 3000
```
```
PULSE 70 3000
```

Move the laser to a fresh spot between pulses. Note the burn depth/appearance.

**Write down:**
```
Engraving threshold  = ____%  (first visible surface mark)
Medium engrave power = ____%  (clean consistent mark)
Max safe engrave pct = ____%  (before paper/card ignites or damage too deep)
```

---

<a id="stage-d"></a>
### Stage D — PWM Frequency Sensitivity

**Goal:** Find out if your laser driver cares about PWM frequency. Some drivers only respond correctly in a specific range.

**D1. Hold laser at 50% power and change frequency:**

```
S50
FREQ 1000
```
Does brightness change? Note it.

```
FREQ 2000
FREQ 5000
FREQ 10000
FREQ 20000
FREQ 40000
```

Observe:
- At some frequencies the laser may flicker (visible in a dark room)
- At very high frequencies, the driver's low-pass filter averages the signal and you get smooth analog-like dimming
- At very low frequencies, the laser pulses visibly

**D2. Run the automated frequency sweep:**

```
FSWEEP 500 40000 10 2000
```

(Sweeps from 500 Hz to 40 kHz in 10 steps, 2 seconds each, while holding 50% power)

**D3. Reset back to the standard 5000 Hz:**

```
FREQ 5000
OFF
```

**Write down:** `Optimal PWM frequency = ____Hz` (the one giving cleanest, most consistent output)

---

<a id="stage-e"></a>
### Stage E — Thermal Stress / Soak Test

**Goal:** Check that your laser module's cooling (heatsink + fan) can sustain continuous operation without overheating.

> This test runs the laser at a fixed power for an extended period. Touch the heatsink (not the laser!) with your hand during the test to feel if it gets dangerously hot (>60°C = too hot to hold for more than 2 seconds).

**E1. 30-second soak at 50% power:**

```
SOAK 50 30
```

Output ticks every second:
```
[SOAK] Holding 50% for 30 seconds. Send 'X' to abort.
  t=0s / 30s  (50.0%)
  t=1s / 30s  (50.0%)
  ...
  t=30s / 30s  (50.0%)
[SOAK] Done. Laser OFF.
```

After it finishes, immediately touch the heatsink. Warm is fine. If you can't hold your finger on it for 3 seconds, the cooling is insufficient.

**E2. 60-second soak at 70%:**

```
SOAK 70 60
```

If the fan runs during this (listen for it), that's normal and good. If the heatsink gets too hot, abort with `X`.

**E3. Check status after soak:**

```
?
```

Verify free heap hasn't changed drastically (memory leak check).

**Write down:** `Sustained safe power = ____% for ____min without overheating`

---

<a id="stage-f"></a>
### Stage F — Pulse & Burst Behaviour

**Goal:** Understand how your driver handles rapid on/off switching (critical for engraving speed).

**F1. Single test pulse — find minimum usable pulse:**
```
PULSE 50 500
PULSE 50 200
PULSE 50 100
PULSE 50 50
PULSE 50 20
PULSE 50 10
```

Note the shortest pulse that still makes a mark on the target surface.

**F2. Burst test — simulate engraving pattern:**
```
BURST 50 100 100 20
```
(50% power, 100ms ON, 100ms OFF, 20 times)

Watch the surface. Each burst should make a dot. The spacing between dots shows off-time.

**F3. High-frequency burst (simulating fast raster engraving):**
```
BURST 60 20 20 50
```

**F4. Ramp up smoothly (simulates machine accelerating into a cut):**
```
RAMPUP 10 80 50
```
(1% per step, 50ms per step → smooth 3.5-second ramp from 10% to 80%)

**F5. Ramp down:**
```
RAMPDN 80 10 50
```

**Write down:** `Minimum effective pulse = ____ms at ____%`

---

<a id="stage-g"></a>
### Stage G — Full Unlock Test (Optional)

**Goal:** Test the region from 90% to 100% — only do this if you're confident in your cooling and eye protection.

> This is optional. The 90% ceiling already gives you full characterisation data. Only unlock if you specifically need to test 100% power.

**G1. Remove the safety ceiling:**
```
UNLOCK
```

**G2. Step up carefully:**
```
S92
S95
S98
S100
```

At each step, check:
- Heatsink temperature
- Mark depth on test surface
- Laser module fan noise (should be running at full speed)

**G3. Immediately re-lock after testing:**
```
OFF
LOCK
```

---

<a id="step-6-results"></a>
## Step 6 — Recording Your Results

Fill in this table based on your tests. Copy the values into `laser_config.py` in the server project.

```
┌─────────────────────────────────────────────────────┐
│           LASER CHARACTERISATION RESULTS            │
├───────────────────────────────┬─────────────────────┤
│ Parameter                     │ Your Value          │
├───────────────────────────────┼─────────────────────┤
│ Dead band (no output below)   │ _____%              │
│ First visible output          │ _____%              │
│ Engraving threshold           │ _____%              │
│ Medium engraving power        │ _____%              │
│ Maximum safe sustained power  │ _____%              │
│ Optimal PWM frequency         │ _____Hz             │
│ Minimum effective pulse       │ _____ms at _____%   │
│ Safe soak duration at 70%     │ _____min (thermal)  │
│ Full power (100%) tested?     │ Yes / No            │
└───────────────────────────────┴─────────────────────┘
```

Then in `laser-engraver-server/laser_config.py`:

```python
LASER_MIN_DUTY_PCT    = __    # Dead band threshold
LASER_ENGRAVE_PCT     = __    # Engraving threshold
LASER_MED_POWER_PCT   = __    # Medium engraving
LASER_MAX_SAFE_PCT    = __    # Max sustained safe
LASER_PWM_FREQ_HZ     = __    # Optimal frequency
LASER_MIN_PULSE_MS    = __    # Shortest effective pulse
```

---

<a id="step-7-reference"></a>
## Step 7 — All Commands Quick Reference Card

```
POWER CONTROL
  ON                    Laser on at last/10% power
  OFF                   Laser off immediately
  S<0-100>              Set power by percent        e.g.  S50
  D<0-4095>             Set raw 12-bit duty         e.g.  D2048

SWEEP
  QSWEEP                Quick 0->100%, 5%/step, 1s each
  SWEEP                 Full sweep, defaults (0->100, 5%, 1s)
  SWEEP s e step dwell  Custom: start% end% step% dwell_ms
                                       e.g.  SWEEP 20 80 2 500

SMOOTH RAMP
  RAMPUP from to ms     Ramp up 1% per step        e.g.  RAMPUP 10 80 30
  RAMPDN from to ms     Ramp down 1% per step      e.g.  RAMPDN 80 10 30

SINGLE PULSE
  PULSE power_% ms      One shot                   e.g.  PULSE 50 200

BURST (Rapid on/off)
  BURST pwr% on_ms off_ms count                    e.g.  BURST 60 50 50 30

STROBE (Square wave)
  STROBE pwr% freq_hz duration_s                   e.g.  STROBE 80 10 5

PWM FREQUENCY
  FREQ <hz>             Set frequency              e.g.  FREQ 10000
  FSWEEP s e steps ms   Freq sweep                 e.g.  FSWEEP 1000 40000 8 2000

THERMAL SOAK
  SOAK power_% seconds  Hold steady                e.g.  SOAK 70 60

SYSTEM
  ?  or  STATUS         Print current status
  UNLOCK                Remove 90% safety ceiling
  LOCK                  Re-enable ceiling
  ECHO                  Toggle command echo
  RESET                 Reset everything, laser off
  HELP  or  H           Show help

ABORT
  X                     Send during any running test to stop immediately
```

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| No serial output at boot | Check baud = 115200, line ending = Newline, USB CDC On Boot = Enabled |
| Upload fails "Failed to connect" | Hold BOOT, press/release RST, release BOOT — then click Upload |
| Laser doesn't respond to any command | Check GPIO 10 → PWM IN wiring; verify shared GND between ESP32 and laser supply |
| Laser stuck ON | Type `OFF` — if no response, power-cycle the 12V supply and RST the ESP32 |
| Safety ceiling not working | Type `LOCK` to ensure it's enabled, then `?` to confirm |
| Garbled serial text | Baud rate is wrong — switch to 115200 |
| Board not appearing in port list | Try different USB cable (data cable, not charge-only) |
