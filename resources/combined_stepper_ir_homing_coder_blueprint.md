# Coder-Agent Blueprint: Combined Stepper + IR Homing Test

This is an implementation brief, not firmware. Use it together with [the full execution plan](./combined_stepper_ir_homing_test_plan.md). Implement the sketch at `stepper_ir_homing_test/stepper_ir_homing_test.ino` and the operator guide at `stepper_ir_homing_test/TESTING_GUIDE.md`. Do not modify the existing test sketches or production firmware.

## 1. Confirmed scope and hardware map

- Target: ESP32-S3 SuperMini; two bipolar-converted 28BYJ-48 motors driven by A4988 modules; two FC-51/KY-032-type IR sensors.
- Motor A = X; Motor B = Y. Sensor X homes A/X; Sensor Y homes B/Y.
- Motor A: STEP 5, DIR 6, ENABLE 7 (active LOW).
- Motor B: STEP 9, DIR 10, ENABLE 11 (active LOW).
- Shared microstep pins: MS1 1, MS2 2, MS3 4.
- **Combined sensor map:** Sensor X OUT = GPIO 13; Sensor Y OUT = GPIO 8. Both are `INPUT_PULLUP`, active LOW by default, with configurable logical polarity.
- GPIO 3 is not used by this new sketch. GPIO 13 is **not connected to an LED** and must never be configured as an output or used as a status LED.
- No laser, laser PWM, or laser power is connected or controlled.
- Serial: 115200 baud. Optional Wi-Fi TCP server: port 8888. Serial must work with Wi-Fi unavailable.
- This is an A4988 bench fixture, not a production-firmware rewrite. Production wiring/driver pins differ and production `homing.cpp` is presently stubbed.

Before coding, verify the board’s physical header label for GPIO 13 and GPIO 8, the current worktree, and source feature inventory. Never hot-plug a motor.

## 2. Libraries and implementation constraints

Use only the existing ESP32 Arduino core:

```cpp
#include <Arduino.h>
#include <WiFi.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
```

No `AccelStepper`, sensor, web-server, or JSON dependency is required. Prefer fixed-size command buffers over repeated dynamic `String` creation. Do not store Wi-Fi credentials in the sketch or repository; provide local placeholders or a clearly documented local configuration point. Initialize outputs and leave both drivers disabled before attempting Wi-Fi.

Structure the single `.ino` file in named sections: pins/config; enums and state; forward declarations; setup/loop; transport input/output; parser/dispatch; sensor filter/diagnostics; driver and step primitives; motor tests; homing state machine; status/help. Keep command parsing shared by Serial and TCP and keep transport reply routing explicit.

## 3. Constants and state

Use one source of truth for pin definitions and physical behavior. Give all tunable values unit-bearing names. Initial homing speeds and travel bounds must be conservative commissioning values and easy to change; do not invent machine dimensions.

Suggested types:

```cpp
enum class AxisId : uint8_t { X, Y };
enum class Transport : uint8_t { Serial, Tcp };
enum class MotionKind : uint8_t { Idle, Jog, Continuous, Pattern, Homing };
enum class HomePhase : uint8_t {
  Idle, Validate, ClearInitial, FastSeek, PullOff, SlowLatch,
  Complete, Aborted, Failed
};
enum class HomeResult : uint8_t {
  None, Running, Success, Aborted, Busy, InvalidSensor,
  SeekTimeout, SeekLimit, RetractTimeout, RetractLimit,
  LatchTimeout, LatchLimit, DriverUnavailable
};

struct SensorState {
  bool rawHigh;
  bool stableDetected;
  bool candidateDetected;
  uint8_t sameSamples;
  uint32_t candidateSinceMs;
  uint32_t triggers;
  uint32_t releases;
  uint32_t unstableEdges;
  uint32_t lastTransitionMs;
};

struct AxisState {
  int32_t commandedSteps;
  uint32_t speedSps;
  bool invertDirection;
  bool driverEnabled;
  bool homed;
  MotionKind motion;
  SensorState sensor;
};

struct HomeOperation {
  bool active;
  AxisId axis;
  HomePhase phase;
  HomeResult result;
  Transport replyTo;
  uint32_t phaseStartedMs;
  uint32_t phaseSteps;
  uint32_t fastTriggerSteps;
  uint32_t retractSteps;
  uint32_t latchSteps;
};
```

Implementation may adjust fields, but represent raw and filtered sensor state separately; track homing phase and result explicitly; keep per-axis `homed` state; remember which transport requested a long-running `G28`; and distinguish commanded step position from verified mechanical position.

Suggested named constants:

```cpp
constexpr uint8_t PIN_STEP_X = 5, PIN_DIR_X = 6, PIN_ENABLE_X = 7;
constexpr uint8_t PIN_STEP_Y = 9, PIN_DIR_Y = 10, PIN_ENABLE_Y = 11;
constexpr uint8_t PIN_MS1 = 1, PIN_MS2 = 2, PIN_MS3 = 4;
constexpr uint8_t PIN_SENSOR_X = 13, PIN_SENSOR_Y = 8;
constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint16_t TCP_PORT = 8888;
constexpr size_t COMMAND_BUFFER_SIZE = 96;
constexpr uint8_t HOME_STABLE_SAMPLES = 3;
constexpr uint32_t HOME_DEBOUNCE_MS = 3;
```

Do not assign a status LED pin. Verify A4988 setup/pulse timing against the module datasheet and preserve the source sketch’s safe pulse width where valid. Bound every command’s accepted speed, step count, pattern radius/segments, and homing timeout/travel.

## 4. Function map

Implement these responsibilities as small functions. Exact signatures can differ, but keep interfaces testable and purpose-specific.

### Setup, loop, and transport

```cpp
void configureSafePins();
void startSerial();
void beginWifiBounded();             // Must not block Serial or the main loop indefinitely.
void serviceSerialInput();
void serviceTcp();
void serviceInput(Transport source, char *buffer, size_t capacity);
bool readLineNonBlocking(Stream &stream, char *buffer, size_t capacity);
void sendReply(Transport destination, const char *line);
void sendDiagnostic(const char *line); // Serial-only diagnostics must not pollute TCP protocol.
```

Use bounded per-transport line buffers. Accept LF and CRLF, ignore empty lines, normalize command names, reject overlong lines, and never parse a truncated prefix as a valid command. Permit one active TCP client; explicitly reject/close competing clients. No credentials in output. If Wi-Fi fails, Serial stays operational.

`loop()` should be cooperative and frequent:

```text
service serial input
service TCP input
sample/update both sensor filters
service emergency stop immediately
advance active homing state by one bounded non-blocking step
advance active manual motion/test if any
service auto-disable only when no motion/home is active
```

Avoid long `delay()` and blocking `readStringUntil()` in motion or homing. If a finite test is temporarily blocking, it must still service both input channels, sensor reads, abort, and elapsed-time checks.

### Command handling

```cpp
bool parseCommand(char *line, Command &out, ErrorCode &error);
void dispatchCommand(const Command &cmd, Transport source);
bool commandAllowedWhileBusy(const Command &cmd);
void handleEmergencyStop(Transport source);
void printHelp(Transport source);
void printStatus(Transport source);
void resetStatistics();              // Must not zero axes or mark them homed.
```

Parse and validate all fields before enabling drivers or moving. Reject out-of-range values explicitly. While homing, reject ordinary movement/configuration as `error: BUSY`; accept abort/stop/status. `INVERT` and microstep changes are idle-only.

### Sensor subsystem

```cpp
bool readSensorRaw(AxisId axis);      // True means electrical HIGH.
bool sensorDetectedRaw(AxisId axis);
void sampleSensor(AxisId axis, uint32_t nowMs);
bool sensorDetectedStable(AxisId axis);
void reportSensorReadings(Transport source);
void runMonitorTest(Transport source, ...);
void reportSensorStatistics(Transport source);
```

Sensor X must use GPIO 13 and Sensor Y GPIO 8. Default active LOW; report electrical raw level and interpreted stable `DETECTED/CLEAR` separately. Use non-blocking consecutive/time-stable filtering; do not use a delay loop that prevents abort or TCP service. A constant pull-up value is not proof that the sensor is healthy.

### Motor subsystem

```cpp
AxisState &axisState(AxisId axis);
uint8_t stepPin(AxisId axis);
uint8_t directionPin(AxisId axis);
uint8_t enablePin(AxisId axis);
uint8_t sensorPin(AxisId axis);
void setDriverEnabled(AxisId axis, bool enabled);
void setDriversEnabled(bool enabled);
void setMicrostepMode(uint8_t divisor);
bool setAxisDirection(AxisId axis, bool positive);
void emitStepPulse(AxisId axis);
bool requestRelativeMove(AxisId axis, int32_t delta, uint32_t speedSps);
bool requestDualMove(int32_t dx, int32_t dy, uint32_t speedSps);
void serviceMotion(uint32_t nowUs);
void stopMotion(bool invalidatePosition);
```

Preserve single-axis and coordinated DDA movement, signed direction, position reporting, speed/RPM conversions, microstepping, continuous mode, goto/step/revolution commands, square/circle, and all motor characterization features from the source. Ensure arithmetic is safe for negative values and bounds. A4988 has no encoder: positions are commanded counts only. Changing microstep mode must preserve physical-distance interpretation or be rejected unless the axes are at a documented safe zero/idle state. `ACCEL` must either affect motion or be described as configuration-only; do not claim acceleration that is not implemented.

### Homing engine

```cpp
bool startHoming(AxisId axis, Transport source);
bool startHomeAll(Transport source); // Sequential X then Y; track partial outcome.
void serviceHoming(uint32_t nowMs, uint32_t nowUs);
void enterHomePhase(HomePhase phase, uint32_t nowMs);
void failHoming(HomeResult why);
void abortHoming();
bool homePhaseExpired(uint32_t nowMs);
bool homeStepLimitReached();
void finishHomeSuccess(AxisId axis);
```

Do not implement homing as one long `while` loop. At most one phase action/step is advanced per loop pass so transports and emergency stop remain responsive. Each phase checks both a `millis()` timeout and a step cap. Use wrap-safe elapsed comparisons: `(uint32_t)(now - started) >= timeout`.

State sequence:

1. `Validate`: reject busy/invalid config; clear homed flag for requested axis; enable only its driver; verify sensor configuration.
2. If sensor is already stably detected: `ClearInitial` moves away from the sensor, bounded, until stable clear. Failure to clear is an error.
3. `FastSeek`: move toward sensor at configured fast (still conservative) speed. Trigger only on stable detected state. Enforce seek steps and timeout.
4. `PullOff`: move away until stable clear. Enforce retract steps and timeout.
5. `SlowLatch`: move toward sensor at lower speed until stable detected. Enforce latch steps and timeout.
6. `Complete`: set that axis’s commanded home coordinate to zero, set its `homed=true`, preserve measured phase counts, and return success.
7. Any abort, invalid/stuck input, timeout, or step-bound exit: stop pulses with STEP LOW, set affected `homed=false`, retain provisional counts, return explicit error and release busy state.

`HOME X`, `HOME Y`, `HOME ALL`, and exact `G28` use this real state machine. `G28` and `HOME ALL` home X then Y by default. Return `ok` only after every requested axis succeeds. If X succeeds and Y fails, report the partial state and a failure for Y, never overall `ok`. Initial physical direction must be validated by an operator at low speed; code comments and guide must not assume logical positive is toward home.

## 5. Command parity and command examples

Read `printHelp()` and the handler from the current motor source before porting. Retain these source features and document their final syntax: `ENABLE`, `DISABLE`, `START`, `STOP`, `STEP`, `SPEED`, `GOTO`, `POS`, `ZERO`, `DIR`, `REV`, `RPM`, `MICRO`, `DUAL STEP`, `DUAL GOTO`, `DUAL SPEED`, `DUAL SQUARE`, `DUAL CIRCLE`, legacy `DUAL HOME` (logical coordinate return, distinct from sensor homing), `BACKLASH`, `STALL`, `ACCEL`, `AUTODISABLE`, `TEST`, `BENCHMARK`, `STATUS`, `HELP`, `RESET`, `ECHO`, and abort `X`/`!`. Preserve all sensor diagnostics that help calibrate and qualify homing: `READ`, `MONITOR`/`WATCH`, `CALIB`, `RANGE`, `THRESHOLD`, `DEBOUNCE`, `EDGES`, `RESPONSE`/`LATENCY`, dual sensor view, `NOISE`, `AMBIENT`, `STATS`, `INVERT`. Resolve prefix collisions explicitly.

Add or reserve the following real homing commands:

```text
READ                 # Raw and stable X/Y input states
STATUS               # Axis, sensor, driver, motion, home result, and network state
HOME X               # Physically home X with Sensor X / Motor A
HOME Y               # Physically home Y with Sensor Y / Motor B
HOME ALL              # Sequential physical home X, then Y
G28                  # Same as HOME ALL; accepted over Serial and TCP
X or !               # Emergency stop / abort
```

TCP requests are newline delimited. Example host-level exchange; this is expected format, not a captured bench result:

```text
Client -> board: G28\n
Board -> client: ok\n
```

On a bounded failure, return a stable error such as `error: HOME_TIMEOUT Y FAST_SEEK\n`. Do not reply `ok` when the request is merely accepted; `ok` is sent only after the sequence completes. Ensure the client can remain connected during the operation. Provide a way to send abort on TCP, e.g. `X\n` or `!\n`, and preserve Serial abort even if TCP disconnects.

## 6. Motor feature honesty

Do not mechanically copy misleading success output from the source:

- Self-test can prove GPIO setup and that pulses were commanded; without feedback, physical motion needs explicit operator confirmation.
- `STALL` is rate/timing plus operator-observed stall behavior, not electronic stall detection.
- `BENCHMARK` measures GPIO/DDA throughput; it does not establish shaft speed.
- `ZERO` changes the logical coordinate only; it is not sensor homing and must not set `homed=true`.
- `DUAL HOME` in the legacy motor test returns to logical `(0,0)`; distinguish it from physical `HOME ALL`.
- Idle disable or abort can invalidate the physical coordinate on an open-loop machine; state the chosen `homed` invalidation policy.

The shared MS pins affect both drivers. The single onboard LED assumptions must be removed: GPIO 13 is Sensor X input. Do not keep the IR `LED` command or write GPIO 13. Use serial/TCP status; only add an indicator on a different GPIO after verifying it is unused and asking for scope approval.

## 7. Transport and failure rules

- Same parser and command semantics over Serial and TCP.
- Replies route to the requesting transport. Serial diagnostics must not accidentally become TCP protocol lines.
- Bounded line length; reject overlong, malformed, or out-of-range commands without motion.
- At most one TCP client controls the machine; explicitly reject a second.
- Wi-Fi connection attempt must be bounded/non-blocking; no credentials printed.
- While busy, ordinary commands return `BUSY`; stop/abort and status remain serviceable.
- Any home timeout, step limit, abort, sensor stuck-active, or no-trigger condition returns an error and leaves the relevant axis unhomed.
- Never silently assume clear because a sensor wire is disconnected; fail by bounds when it does not trigger.
- No broad silent failure or success-shaped fallback.

## 8. Coding and verification order

1. Review the current dirty worktree; do not edit source tests or production files.
2. Inventory exact current motor command behavior and record parity checklist.
3. Create the new sketch with pin constants and safe initialization. Verify GPIO 13 sensor input/GPIO 8 sensor input and no GPIO 13 LED write.
4. Implement bounded Serial/TCP line handling and a shared parser; compile.
5. Implement raw/stable sensor reads and sensor-only diagnostics; test with VMOT off.
6. Port the motor primitives and commands; make motion cooperative/abortable; compile and verify motor tests.
7. Add the non-blocking homing state machine and per-axis result state.
8. Integrate `G28`, `HOME X/Y/ALL`, TCP replies, busy behavior, disconnect, and abort.
9. Write the operator guide from actual code defaults and commands.
10. Compile for the documented ESP32-S3 board/core. Perform staged bench tests: logic-only, one motor, one sensor, each axis, dual home, failure/abort, then TCP.

Do not claim hardware tests that were not physically performed. Final report must separate compiled behavior from verified bench behavior and disclose any unmet parity items.
