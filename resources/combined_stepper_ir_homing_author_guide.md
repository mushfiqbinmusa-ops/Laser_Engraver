# Author Blueprint: Combined Stepper + IR Homing Test Guide

This is a writing blueprint for the future operator manual at `stepper_ir_homing_test/TESTING_GUIDE.md`. It is paired with [the coder-agent blueprint](./combined_stepper_ir_homing_coder_blueprint.md) and [the full execution plan](./combined_stepper_ir_homing_test_plan.md). Write the final guide only after the sketch’s accepted commands, defaults, and actual hardware behavior are known.

## Audience and promise

Write for a builder who can wire an ESP32 and use a multimeter but may not understand firmware internals. Explain what to connect, what to observe, what each command does, how to stop motion, and what a pass does and does not prove. This is a bench test for two A4988-driven axes and two optical home sensors. It is not production firmware and does not drive a laser.

Use plain language, short procedures, tables, and expected output examples. Label examples as illustrative until captured from a real board. Do not claim a test was physically performed if it was not. Never promise a detection distance, motor speed, repeatability in millimeters, or successful physical motion from commanded step counts alone.

## Mandatory hardware map

Show this exact map prominently and keep it identical in the pin table and diagram:

| ESP32-S3 GPIO | Connects to |
|---:|---|
| 5 | A4988 A STEP (Motor A / X) |
| 6 | A4988 A DIR (Motor A / X) |
| 7 | A4988 A ENABLE (active LOW) |
| 9 | A4988 B STEP (Motor B / Y) |
| 10 | A4988 B DIR (Motor B / Y) |
| 11 | A4988 B ENABLE (active LOW) |
| 1, 2, 4 | Shared A4988 MS1, MS2, MS3 |
| **13** | **Sensor X OUT (INPUT_PULLUP; active LOW default)** |
| **8** | **Sensor Y OUT (INPUT_PULLUP; active LOW default)** |
| 3 | Unused by the combined fixture |

The user confirmed GPIO 13 is not connected to an onboard LED. State this directly: **GPIO 13 is used by Sensor X and must not be wired to or configured as an LED output.** Do not document GPIO 13 as a status LED. The combined sketch has no LED unless a later approved design assigns an unused GPIO. Status is shown in Serial/TCP.

Include board-specific physical header position labels only after verifying the exact SuperMini revision. GPIO number and board silk-screen/header position are not interchangeable.

## Document structure to write

1. **Purpose / limits:** two motors, two sensors, A4988 bench setup, no laser; pulse counts are not encoder feedback; production firmware is different.
2. **Safety before wiring:** laser disconnected; VMOT off during wiring; never hot-plug motors; current limit set from the actual driver/sense resistor; common ground; capacitor polarity; sensor OUT voltage measured before GPIO connection.
3. **Combined system diagram:** Mermaid for host/Serial/TCP → ESP32 → A4988/motors and sensors. Mark GPIO 13 as Sensor X, GPIO 8 as Sensor Y, and show no LED/laser.
4. **Pin and power tables:** MCU to drivers, coils, sensors, logic supply, VMOT supply, and common ground. Explain the sensor level-shifter/divider requirement if output can exceed the MCU input rating; do not assume a 5 V module output is safe.
5. **Pre-power checklist:** inspect shorts, motor coil pairs, RESET/SLEEP, MS pins, grounds, bulk capacitor, sensor output voltage, and GPIO mapping.
6. **Upload and first boot:** exact board/core/serial settings from successful implementation; expected startup banner and safe disabled drivers; local-only Wi-Fi setup; never publish credentials.
7. **Sensor verification:** raw HIGH/LOW versus interpreted DETECTED/CLEAR, `READ`, `MONITOR`/`WATCH`, polarity, trim-pot/range, debounce/edges, noise/ambient, stats. Calibrate using the real reflective flag.
8. **Motor-only bring-up:** one motor at a time, low-speed direction check, step/position/microstep, dual-axis motion, patterns, diagnostics. Explain that stall detection and physical movement are not electronically verified without feedback.
9. **Home commissioning:** confirm physical seek direction at low speed; set safe travel/timeout; explain fast seek → pull-off → slow latch; test each axis separately before `HOME ALL`.
10. **Serial and TCP operation:** baud 115200; discover board IP; port 8888; send newline-terminated `HOME X`, `HOME Y`, `HOME ALL`, or `G28`; show success and failure response examples. State that TCP acceptance in the test fixture does not make production firmware integrated.
11. **Abort, failure recovery, troubleshooting:** abort `X` or `!`; sensor initially blocked; sensor never triggers; cannot clear; wrong direction; busy; Wi-Fi unavailable; motor buzz/overheat; wrong axis sensor; line-format errors.
12. **Test log and acceptance checklist:** record board, core, sensor module, target material, power/current setting, speed, debounce, seek/retract/latch counts/time, repeatability spread, transport, pass/fail/blocked, and unverified steps.

## Homing explanation and diagrams

Use a Mermaid sequence diagram with: host request → controller validates → selected driver enables → initial sensor check → fast seek → stable trigger → pull-off to stable clear → slower latch → origin set → one final reply. Add branches for initial-active sensor, timeout/step bound, disconnect policy if defined, and abort. Do not depict success before the final latch.

Define the terms:

- **Raw level:** electrical GPIO HIGH or LOW.
- **Detected/clear:** interpreted state after configured polarity and stability filtering.
- **Fast seek:** bounded move toward the sensor at commissioning speed.
- **Pull-off:** bounded move away until sensor is stably clear.
- **Slow latch:** slower bounded approach that confirms the home point.
- **Homed:** a stable sensor latch completed for that axis; not proof of continued physical position after the motor is moved or de-energized.
- **ZERO:** software coordinate reset only; not physical homing.

Use a formula only to clarify measured behavior, not to promise accuracy. For example, note that approximate step overshoot grows with step rate and response/filter time: \(n \approx v\tau\), where \(v\) is commanded steps/s and \(\tau\) is measured response seconds. State that mechanical inertia, backlash, optical geometry, and missed steps are not captured by that estimate.

## Command documentation rules

Copy command syntax from the implemented `HELP` output, not from this draft. Include a table with command, purpose, safe prerequisites, whether it moves motors, and an example. Separate legacy `DUAL HOME` (open-loop return to logical zero if retained) from sensor-driven `HOME ALL`/`G28`. Identify Serial-only commands if any. Explain that `G28` over TCP returns `ok` only after all requested axes finish successfully; show stable `error:` examples for abort and bounded failure.

For all long operations, state how to abort and what state remains afterward. Explain that an aborted, timed-out, or unhomed axis must be homed again. Do not hide partial dual-home failure.

## Safety language and editorial checks

- Use explicit warnings before power and motion steps, not only in a general disclaimer.
- Always say to switch off motor power before wiring changes.
- Do not tell a user to touch exposed energized driver terminals.
- Do not prescribe universal Vref without exact sense resistor/module verification.
- Do not call a sensor input safe until its voltage has been checked against ESP32-S3 limits.
- Do not claim the test validates laser control; laser is absent.
- Avoid saying “the motor moved” when code only sent pulses; ask for visual confirmation or measurement.
- Mark conservative settings as commissioning defaults and tell the user to validate travel and direction.
- Document actual Wi-Fi timeout/connection policy and do not print passwords.
- Provide expected outputs as templates until actual hardware output is captured.

Before delivery, compare the manual line-by-line with the code’s pin constants, help text, timing limits, direction semantics, Wi-Fi behavior, success/error messages, reset rules, and homing state machine. Recheck that Sensor X is always GPIO 13, Sensor Y is GPIO 8, GPIO 3 is unused, and no section calls GPIO 13 an LED. Render the Mermaid diagrams and check links. End with a clear checklist that separates compile verification, logic tests, and physical bench tests.
