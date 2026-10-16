# Laser-XY-Scanner Command Reference and Hardware Concepts

This document explains the commands used in this repo, what each one does to the hardware, and the concept behind it.

The repository contains two command families:

1. G-code style motion commands used by the laser engraver firmware (`laser-engraver-firmware`)
2. Serial test commands used by the PWM laser characterization firmware (`laser_pwm_test`)

Both families ultimately convert text commands into electrical actions on motors, laser drivers, and GPIO pins.

---

## 1. How commands work in this project

Every command is received as text over Serial (USB, Wi-Fi, or serial monitor). The firmware then:

- reads the incoming string,
- parses the command name and arguments,
- converts numbers into hardware actions,
- drives GPIO pins, PWM outputs, or stepper motor phases,
- optionally reports status back to the user.

At the lowest level, the hardware responds in one of these ways:

- digital output: ON/OFF signals to a pin,
- PWM output: modulated voltage to control laser brightness,
- stepper drive: energizing the correct coil phases to move a motor,
- homing: moving until a limit switch is reached,
- safety logic: clamping or blocking power above a safe limit.

---

## 2. G-code commands used by the engraver firmware

These are the most important commands in the main engraver logic. The parser accepts G0, G1, M3, M5, G28, and !.

### `G0 X... Y...`

Example:

```gcode
G0 X100 Y50
```

What it does:

- tells the machine to move the laser head rapidly to a new XY position,
- the parser reads the X and Y values,
- the motion controller calculates the delta from the current position,
- the stepper signals are updated to move toward the target.

Hardware behavior:

- the laser is set to OFF during rapid travel,
- the motors move using step pulses,
- the machine does not “draw” while moving rapidly.

Concept:

- `G0` is a non-cutting move. It is used for repositioning the laser head without engraving.
- In the firmware, this is treated as a move only; no laser is enabled.

Why it matters:

- if the laser stayed on during rapid moves, it would burn unwanted material or create unwanted marks.

---

### `G1 X... Y... S...`

Example:

```gcode
G1 X250 Y120 S255
```

What it does:

- moves the machine to a target XY position while the laser is active,
- `S` is the laser power setting,
- the firmware converts X/Y and S into movement + laser duty.

Hardware behavior:

- the laser driver receives a duty value,
- the motion controller interpolates between the current point and the target,
- the stepper motors step along the path,
- the beam stays on during the move.

Concept:

- `G1` is a cutting or engraving move.
- The motion is usually linearly interpolated: start at the current position, move gradually to the endpoint, and turn the laser on while doing so.
- In practical CNC/laser terms, this is the command that actually draws a line.

Why it matters:

- the laser beam is effectively a tool that is switched on and off as the machine moves.
- The `S` value is a power command; the deeper hardware effect is the PWM duty percentage delivered to the laser diode driver.

---

### `M3 S...`

Example:

```gcode
M3 S200
```

What it does:

- turns the laser ON,
- sets the laser power level using the `S` value.

Hardware behavior:

- the firmware calls `laser.on(command.laserDuty)`,
- the laser controller translates the duty value to a PWM signal,
- the laser diode current is modulated accordingly.

Concept:

- `M3` is the conventional G-code “spindle on” command for a laser tool.
- In this repo, the laser is treated as a digitally controlled light source rather than a spindle motor.

Why it matters:

- without this command, the beam would remain off even if the machine moved.

---

### `M5`

Example:

```gcode
M5
```

What it does:

- turns the laser OFF immediately.

Hardware behavior:

- the laser controller disables the PWM output or forces the output to zero duty,
- the motor may continue moving depending on the command sequence,
- no beam is emitted.

Concept:

- `M5` is the opposite of `M3`.
- It is used at the end of a cut, between separate operations, or before a rapid move.

Why it matters:

- this is critical for turning the beam off during travel or when pausing.

---

### `G28`

Example:

```gcode
G28
```

What it does:

- homes the machine,
- tells the axes to move until the endstops are triggered.

Hardware behavior:

- the firmware calls `homing.homeAll()`,
- the endstop pins are read with `digitalRead()`,
- when a switch is pressed, the machine knows it has reached the home position.

Concept:

- homing is a reference move that defines the machine’s “zero position.”
- Endstops physically detect the machine boundary and tell the controller, “this is the home location.”

Why it matters:

- without homing, XY coordinates would drift and lose meaning.
- Every good CNC or laser machine relies on a defined zero reference.

---

### `!`

Example:

```text
!
```

What it does:

- emergency stop,
- immediately disables the laser,
- does not continue normal motion logic.

Hardware behavior:

- the firmware sets laser off,
- the command path exits before further motion processing.

Concept:

- `!` is a safety override for hazards.
- It is not a normal machine command; it is a stop signal used when something is wrong.

Why it matters:

- if the laser or motion goes wrong, you need a direct and immediate halt.

---

## 3. ESP32 laser test commands

The `laser_pwm_test.ino` firmware is a serial test bench for a laser diode. It exposes commands to control power, frequency, sweeps, pulses, and safety.

### The important hardware concept

The laser is driven by a PWM signal generated by the ESP32 LEDC peripheral.

- `LEDC_RES_BITS = 12` gives a 12-bit resolution,
- the duty value range is `0..4095`,
- the duty ratio is:

```text
duty_percent = (duty / 4095) * 100
```

So a command such as `S75` or `D3070` corresponds to a specific time-average power level.

---

### `ON`

What it does:

- turns the laser on at the current stored duty value,
- if current duty is zero, it defaults to 10% power.

Hardware effect:

- sets PWM output to a nonzero value,
- laser diode turns on with the configured duty cycle.

Concept:

- it uses the current state, not a new value, unless the duty was zero.

---

### `OFF`

What it does:

- turns the laser off immediately.

Hardware effect:

- output is forced low / zero duty,
- no current flows into the laser driver control path.

Concept:

- “off” is a hard electrical shutdown, not just a software mode.

---

### `S<0-100>`

Example:

```text
S75
```

What it does:

- sets a power percentage,
- converts the percent to a PWM duty cycle,
- applies the safety ceiling if enabled.

Hardware effect:

- the LEDC driver output changes to match the target duty.

Concept:

- percentage is a human-friendly interface layer.
- the firmware converts `%` into raw duty counts internally.

---

### `D<0-4095>`

Example:

```text
D2048
```

What it does:

- sets the raw 12-bit duty value directly.

Hardware effect:

- the ESP32 directly writes a duty count to the PWM output.

Concept:

- this is the precise hardware-level control.
- it is a lower-level version of `S<percent>`.

---

### `FREQ <hz>`

Example:

```text
FREQ 10000
```

What it does:

- sets the PWM frequency of the laser drive signal.

Hardware effect:

- LEDC reconfigures the output timer frequency,
- the laser diode sees a different modulation frequency.

Concept:

- frequency controls how fast the output pulses turn on and off,
- duty controls how long each pulse stays on during a cycle.

In short:

- higher frequency = faster switching,
- duty ratio = average brightness.

---

### `FSWEEP <start_hz> <end_hz> <steps> <dwell_ms>`

Example:

```text
FSWEEP 100 40000 10 1000
```

What it does:

- sweeps the PWM frequency across a range while holding the laser at 50% power.

Hardware effect:

- for each step, the firmware changes the LEDC frequency,
- then keeps the laser output active at a fixed duty value.

Concept:

- this is used to characterize the laser’s response across frequency.
- the laser may behave differently at different modulation rates.

---

### `QSWEEP`

What it does:

- quick laser sweep from 0% to 100% in 5% increments,
- default dwell is 1 second per step.

Hardware effect:

- each step sets a new duty value,
- the laser is turned on for a brief interval at that level,
- the process repeats until the end value is reached.

Concept:

- it reveals the laser’s response curve and threshold behavior.

---

### `SWEEP`

Examples:

```text
SWEEP
SWEEP 80
SWEEP 20 80
SWEEP 20 80 2
SWEEP 20 80 2 500
```

What it does:

- sweeps the output power from one value to another.

Hardware effect:

- each step writes a new duty cycle,
- the laser stays on long enough for the dwell time,
- then steps again.

Concept:

- this is used to map brightness versus duty and to test safe operating windows.

---

### `RAMPUP`

Example:

```text
RAMPUP 0 90 50
```

What it does:

- increases laser power gradually from one percent to another.

Hardware effect:

- the duty is increased step-by-step,
- each step is held for a short time before the next increase.

Concept:

- this is a controlled soft start.
- it reduces thermal or electrical shock compared with jumping immediately to full power.

---

### `RAMPDN`

Example:

```text
RAMPDN 90 0 50
```

What it does:

- decreases laser power gradually.

Hardware effect:

- the duty is ramped down in small increments.

Concept:

- used for safe shutdown or thermal control.

---

### `PULSE <power%> <duration_ms>`

Example:

```text
PULSE 50 1000
```

What it does:

- fires a single pulse at the requested power for a given duration.

Hardware effect:

- sets the duty to the target value,
- waits the specified time,
- turns the laser off.

Concept:

- a pulse test tells you how the laser responds in a single emission event.
- very useful for energy, response, and timing validation.

---

### `BURST <power%> <on_ms> <off_ms> <count>`

Example:

```text
BURST 50 100 100 20
```

What it does:

- emits a burst of a fixed number of pulses.

Hardware effect:

- laser turns on for `on_ms`, off for `off_ms`, repeated `count` times.

Concept:

- this is a repeated pulse train used for fatigue, response, and thermal testing.
- it is effectively a low-level modulated output pattern.

---

### `STROBE <power%> <hz> <duration_s>`

Example:

```text
STROBE 80 10 5
```

What it does:

- blinks the laser at a set frequency for a fixed duration.

Hardware effect:

- the PWM is rapidly toggled on and off in a periodic pattern.

Concept:

- a strobe is a periodic pulse train.
- `hz` sets the cycle rate, and duration tells how long to keep it running.

---

### `SOAK <power%> <duration_s>`

Example:

```text
SOAK 75 30
```

What it does:

- holds the laser at a fixed power for a period of time.

Hardware effect:

- sets the duty to a steady level,
- reports elapsed time,
- can be aborted by sending `X`, `x`, or `!` from the serial monitor.

Concept:

- this is for thermal testing or device stabilization.
- you keep power constant and observe how the unit behaves over time.

---

### `STATUS` or `?`

What it does:

- prints the current machine state.

Hardware effect:

- reads software variables such as current duty, frequency, lock state, and laser state,
- prints them over Serial.

Concept:

- it is a diagnostic command,
- the system reports what it currently believes its configuration is.

---

### `UNLOCK`

What it does:

- disables the 90% safety ceiling.

Hardware effect:

- the firmware allows duty values from 0% to 100%.

Concept:

- this is intentionally dangerous mode used only when the operator intentionally wants full power.
- it bypasses the default protective limit.

---

### `LOCK`

What it does:

- re-enables the safety ceiling.

Hardware effect:

- if the current duty exceeds the allowed maximum, it is clamped back down.

Concept:

- safety is restored to default protective operation.

---

### `ECHO`

What it does:

- toggles whether commands are echoed back to the serial terminal.

Hardware effect:

- a simple flag in firmware changes serial output behavior.

Concept:

- this is for user convenience and debugging.
- it lets you see what command was parsed.

---

### `RESET`

What it does:

- resets the firmware to default values.

Hardware effect:

- laser turns off,
- PWM frequency resets to 5000 Hz,
- safety lock is restored,
- hardware state is returned to its initial default.

Concept:

- “reset” is an operational reboot of the software state without physically rebooting the microcontroller.

---

### `HELP` or `H`

What it does:

- displays the full command list and usage text.

Hardware effect:

- prints the command reference over Serial.

Concept:

- it is the built-in documentation for the firmware.

---

## 4. Underlying hardware concepts behind the commands

### 4.1 PWM (Pulse Width Modulation)

PWM is the main concept behind the laser power commands.

The ESP32 does not vary the laser voltage continuously. Instead it rapidly switches the output on and off. The ratio of on-time to period is the duty cycle.

Example:

- 50% duty = output is ON half the time,
- 75% duty = output is ON 75% of the time,
- the average power is roughly proportional to duty cycle.

This is how the firmware controls laser brightness without analog hardware.

---

### 4.2 Stepper motors and motion control

In the engraver firmware, commands like `G0` and `G1` do not directly power a DC motor shaft. Instead they:

- calculate XY displacement,
- convert displacement into step counts,
- energize the correct motor coil phases,
- move the laser head to the target coordinate.

The motion controller uses step pulses and interpolation to make a smooth path.

---

### 4.3 Endstop homing

`G28` depends on physical switches called endstops.

When a stepper axis moves toward a switch:

- the switch closes or opens the circuit,
- the firmware sees a LOW signal on the endstop input,
- it stops motion and treats that as the known home point.

This makes “zero” consistent every time the machine powers on or is homed.

---

### 4.4 Safety logic

The firmware adds a strict hardware safety rule:

- default maximum is 90% of full PWM duty,
- if the operator issues `UNLOCK`, this limit is removed,
- `LOCK` re-applies the limit.

This matters because laser diodes can be damaged by overdriving.

---

## 5. Summary of command purpose

In one sentence:

- `G0` and `G1` move the machine,
- `M3` and `M5` control the laser state,
- `G28` sets the machine origin,
- `!` stops the machine immediately,
- the PWM test commands control laser brightness, timing, sweeping, and safety.

The real idea is that all of these commands are just text sent to a microcontroller; the microcontroller converts that text into precise electrical actions on motors, drivers, and laser PWM outputs.

---

## 6. Practical safety reminder

This project uses a powerful laser source. The hardware commands are not just software conveniences; they directly drive real optical power and motion systems.

Always:

- wear appropriate laser-safe eyewear,
- operate with a proper enclosure or beam path,
- use the safety lock unless you are intentionally testing higher power,
- keep the machine under controlled conditions.

---

## 7. Final interpretation

The commands are not magic—they are the interface layer between a human operator and physical hardware. The firmware translates command text into:

- motor step pulses,
- laser duty modulation,
- timing loops,
- GPIO states,
- safety checks.

That is the core concept behind every command in this repository.