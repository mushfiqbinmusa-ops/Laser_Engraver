# Hardware Profile and Safety Boundaries Specification

**Document Version:** 1.0.0  
**Target Hardware:** ESP32-S3 SuperMini  
**Profile ID:** `esp32s3_a4988_v1`  
**Status:** Frozen & Authoritative  

---

## 1. GPIO Allocation and Conflict Resolution

### 1.1 Resolution of GPIO 10 Conflict
In previous exploratory sketches, GPIO 10 was referenced for both Y Direction (`stepper_ir_homing_test.ino`) and Laser PWM (`laser_pwm_test.ino`). 

This conflict is authoritatively resolved as follows:
- **Motor B (Y-Axis) DIR is assigned to GPIO 10**. This preserves the contiguous wiring block for Motor B (STEP = 9, DIR = 10, ENABLE = 11) on the ESP32-S3 SuperMini.
- **Laser Diode PWM is assigned to GPIO 12**. On the ESP32-S3, GPIO 12 is a non-strapping general RTC GPIO that defaults to a safe LOW state at boot/reset without affecting flash or bootloader modes.
- **Sensor X is assigned to GPIO 13**. As identified in [`report.md`](file:///home/mushfiq/Desktop/Laser-XY-Scanner/report.md), GPIO 13 was previously misconfigured as a status LED in prototype sketches, causing electrical contention with the active-LOW LM393 IR comparator. In this production profile, **GPIO 13 is strictly an input with internal pull-up and MUST NEVER be configured as an output**.
- **Sensor Y is assigned to GPIO 8** (`INPUT_PULLUP`, active LOW).
- **GPIO 3 is reserved / unused**.

### 1.2 Complete Pin Mapping Table

| Signal | GPIO | Pin Direction | Electrical Domain | Boot State | Function & Safety Notes |
|---|:---:|:---:|:---:|:---:|---|
| `PIN_MS1` | 1 | OUTPUT | 3.3V Logic | High-Z | Shared microstep bit 1 (HIGH for 1/16) |
| `PIN_MS2` | 2 | OUTPUT | 3.3V Logic | High-Z | Shared microstep bit 2 (HIGH for 1/16) |
| `RESERVED` | 3 | — | 3.3V Logic | High-Z | Unused / reserved |
| `PIN_MS3` | 4 | OUTPUT | 3.3V Logic | High-Z | Shared microstep bit 3 (HIGH for 1/16) |
| `PIN_STEP_X` | 5 | OUTPUT | 3.3V Logic | LOW | Motor A (X) Step pulse (min 3 µs rising edge) |
| `PIN_DIR_X` | 6 | OUTPUT | 3.3V Logic | LOW | Motor A (X) Direction (HIGH = CW, LOW = CCW) |
| `PIN_ENABLE_X` | 7 | OUTPUT | 3.3V Logic | HIGH | Motor A (X) Enable (**Active LOW**; HIGH = disabled) |
| `PIN_SENSOR_Y` | 8 | INPUT | 3.3V Logic (`INPUT_PULLUP`) | Pull-up | Y Optical Endstop (FC-51 / KY-032 LM393, Active LOW) |
| `PIN_STEP_Y` | 9 | OUTPUT | 3.3V Logic | LOW | Motor B (Y) Step pulse (min 3 µs rising edge) |
| `PIN_DIR_Y` | 10 | OUTPUT | 3.3V Logic | LOW | Motor B (Y) Direction (HIGH = CW, LOW = CCW) |
| `PIN_ENABLE_Y` | 11 | OUTPUT | 3.3V Logic | HIGH | Motor B (Y) Enable (**Active LOW**; HIGH = disabled) |
| `PIN_LASER_PWM` | 12 | OUTPUT | 3.3V TTL to Driver | **Hard LOW** | Laser PWM (5 kHz, 12-bit LEDC, ≤ 90% hard clamp) |
| `PIN_SENSOR_X` | 13 | INPUT | 3.3V Logic (`INPUT_PULLUP`) | Pull-up | X Optical Endstop (Active LOW; **NEVER OUTPUT**) |

---

## 2. Safety Invariants and Hardware Watchdogs

1. **Boot Electrical State:** At power-on and MCU reset, the hardware outputs immediately default to:
   - Laser: `LOW` (0V TTL, laser driver completely off).
   - Steppers: `ENABLE_X` = `HIGH`, `ENABLE_Y` = `HIGH` (stepper drivers in High-Z disabled state).
2. **Firmware 90% Hard Laser Clamp:** In 12-bit LEDC mode (`LEDC_MAX_DUTY` = 4095), the maximum permitted duty cycle is clamped to `3685` (`4095 * 90 / 100`). Any command specifying a higher value is rejected with `LASER_DUTY_CLAMPED`.
3. **Laser Auto-Off Watchdog (500 ms):** During active motion or pulse commands, if no refresh occurs within 500 ms, the laser is immediately turned off.
4. **Coordinated Motion Abort:** An incoming Emergency Stop (`system.estop` or `!`) or regular Stop (`system.stop`) interrupts step pulse generation within one step interval (< 5 ms) and turns the laser off immediately.
5. **Position Invalidation:** Any abrupt stop, limit trip, stepper disable, or reboot instantly clears `homed = false` and `pos_valid = false`.
