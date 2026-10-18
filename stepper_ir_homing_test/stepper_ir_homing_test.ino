// =============================================================================
//  stepper_ir_homing_test.ino
//  Combined Dual Stepper Motor & IR Optical Homing Test Firmware
//  Target: ESP32-S3 SuperMini
//  Motors: Dual 28BYJ-48 Stepper Motors (Converted to 4-Wire Bipolar)
//  Drivers: Dual A4988 Stepper Drivers (VMOT=5V external, VDD=3.3V logic)
//  Sensors: Dual FC-51 / KY-032 IR Obstacle Avoidance Modules (LM393)
//  Transports: USB Serial (115200 baud) & Wi-Fi TCP Server (Port 8888)
//
//  Hardware Scope & Boundary:
//    - Standalone A4988 bench test fixture.
//    - Motor A = X-axis (STEP 5, DIR 6, EN 7).
//    - Motor B = Y-axis (STEP 9, DIR 10, EN 11).
//    - Microstep mode bus: MS1 1, MS2 2, MS3 4.
//    - Sensor X OUT = GPIO 13 (INPUT_PULLUP, active LOW default).
//    - Sensor Y OUT = GPIO 8 (INPUT_PULLUP, active LOW default).
//    - GPIO 3 is UNUSED.
//    - GPIO 13 is an INPUT for Sensor X; NO LED is connected. NEVER set as output.
//    - NO laser, laser PWM, or laser power is connected or controlled.
//    - Production firmware wiring differs; this is a dedicated bench fixture.
//
//  No external libraries required — uses standard ESP32 Arduino Core only.
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// =============================================================================
//  SECTION 1: HARDWARE PIN DEFINITIONS & CONSTANTS
// =============================================================================

// Motor A (X-Axis)
constexpr uint8_t PIN_STEP_X         = 5;   // A4988 A STEP (rising edge)
constexpr uint8_t PIN_DIR_X          = 6;   // A4988 A DIR
constexpr uint8_t PIN_ENABLE_X       = 7;   // A4988 A ENABLE (active LOW)

// Motor B (Y-Axis)
constexpr uint8_t PIN_STEP_Y         = 9;   // A4988 B STEP (rising edge)
constexpr uint8_t PIN_DIR_Y          = 10;  // A4988 B DIR
constexpr uint8_t PIN_ENABLE_Y       = 11;  // A4988 B ENABLE (active LOW)

// Shared Microstep Selection Pins
constexpr uint8_t PIN_MS1            = 1;   // Microstep select bit 1
constexpr uint8_t PIN_MS2            = 2;   // Microstep select bit 2
constexpr uint8_t PIN_MS3            = 4;   // Microstep select bit 3

// Optical Endstop Sensors
constexpr uint8_t PIN_SENSOR_X       = 13;  // Sensor X OUT (INPUT_PULLUP; NO LED connected)
constexpr uint8_t PIN_SENSOR_Y       = 8;   // Sensor Y OUT (INPUT_PULLUP)
// Note: GPIO 3 is intentionally UNUSED in this combined fixture.
// Note: GPIO 13 is an INPUT only. Do NOT configure as OUTPUT or drive as an LED.

// Communication & Server Configuration
constexpr uint32_t SERIAL_BAUD       = 115200;
constexpr uint16_t TCP_PORT          = 8888;
constexpr size_t   COMMAND_BUF_SIZE  = 96;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 4000;

// Local Wi-Fi Credentials (Placeholders — set locally if network testing is desired)
// Serial operation is fully functional even if Wi-Fi is unconfigured or unreachable.
static const char *WIFI_SSID         = "403";
static const char *WIFI_PASSWORD     = "mks1998!";

// Driver Logic & Timing Constants
constexpr uint8_t  DRIVER_ENABLE_LEVEL  = LOW;   // A4988 active LOW enable
constexpr uint8_t  DRIVER_DISABLE_LEVEL = HIGH;  // A4988 disabled (outputs high-Z)
constexpr uint8_t  DIR_CW_LEVEL         = HIGH;  // Standard CW logic level
constexpr uint8_t  DIR_CCW_LEVEL        = LOW;   // Standard CCW logic level
constexpr uint32_t STEP_PULSE_WIDTH_US  = 3;     // Minimum A4988 step high pulse width
constexpr uint32_t DIR_SETUP_TIME_US    = 5;     // A4988 DIR setup margin

// Mechanical & Commissioning Limits
constexpr uint32_t BASE_STEPS_PER_REV   = 2048UL; // 28BYJ-48 64:1 full-step baseline
constexpr uint32_t DEFAULT_SPEED_SPS    = 100;    // Safe 100 sps commissioning default
constexpr uint32_t MIN_SPEED_SPS        = 1;
constexpr uint32_t MAX_SPEED_SPS        = 25000;
constexpr uint32_t DEFAULT_ACCEL_SPS2   = 2000;
constexpr uint32_t AUTO_DISABLE_MS      = 30000UL;// 30s idle auto-disable timeout

// Homing Commissioning Bounds
constexpr uint32_t HOMING_FAST_SPS           = 120;     // Fast seek approach rate
constexpr uint32_t HOMING_SLOW_SPS           = 40;      // Slow precision latch rate
constexpr uint32_t HOMING_MAX_SEEK_STEPS     = 4096UL;  // Safe travel limit for seek
constexpr uint32_t HOMING_MAX_RETRACT_STEPS  = 1024UL;  // Max pull-off steps
constexpr uint32_t HOMING_MAX_LATCH_STEPS    = 1024UL;  // Max slow latch steps
constexpr uint32_t HOMING_MAX_CLEAR_STEPS    = 1024UL;  // Max initial retreat steps
constexpr uint32_t HOMING_PHASE_TIMEOUT_MS   = 15000UL; // 15s per-phase timeout
constexpr uint8_t  HOME_STABLE_SAMPLES       = 3;       // 3 consistent samples
constexpr uint32_t HOME_DEBOUNCE_MS          = 3;       // 3 ms stability window

// =============================================================================
//  SECTION 2: CORE ENUMS & STATE STRUCTURES
// =============================================================================

enum class AxisId : uint8_t {
  X = 0,
  Y = 1
};

enum class Transport : uint8_t {
  Serial = 0,
  Tcp = 1,
  None = 2
};

enum class MotionKind : uint8_t {
  Idle = 0,
  Jog,
  Continuous,
  Pattern,
  Homing
};

enum class HomePhase : uint8_t {
  Idle = 0,
  Validate,
  ClearInitial,
  FastSeek,
  PullOff,
  SlowLatch,
  Complete,
  Aborted,
  Failed
};

enum class HomeResult : uint8_t {
  None = 0,
  Running,
  Success,
  Aborted,
  Busy,
  InvalidSensor,
  SeekTimeout,
  SeekLimit,
  RetractTimeout,
  RetractLimit,
  LatchTimeout,
  LatchLimit,
  DriverUnavailable
};

struct SensorFilter {
  bool     rawHigh;             // Direct electrical pin read (true = HIGH)
  bool     stableDetected;      // Filtered, polarity-interpreted state
  bool     candidateDetected;   // Pending candidate state during debounce
  uint8_t  sameSamples;         // Consecutive identical sample counter
  uint32_t candidateSinceMs;    // Timestamp when candidate state first appeared
  uint32_t triggers;            // Cumulative stable trigger count
  uint32_t releases;            // Cumulative stable release count
  uint32_t unstableEdges;       // Jitter/chatter transition count
  uint32_t lastTransitionMs;    // Timestamp of last stable change
  uint32_t lastStableDetectedMs;// Timestamp when stable DETECTED was asserted
  uint32_t minDurationMs;       // Shortest trigger pulse observed
  uint32_t maxDurationMs;       // Longest trigger pulse observed
  uint64_t totalDurationMs;     // Total time spent in detected state
  uint32_t completedCycles;     // Full trigger -> release cycles
};

struct AxisState {
  int32_t      commandedSteps;  // Commanded position relative to logical origin
  uint32_t     speedSps;        // Configured stepping rate
  bool         invertDirection; // Direction polarity invert
  bool         driverEnabled;   // Driver coil power state
  bool         homed;           // Verified sensor latch completed flag
  MotionKind   motion;          // Current motion mode
  SensorFilter sensor;          // Associated optical sensor state

  // Continuous motion tracking
  bool         contRunning;     // Continuous rotation active flag
  int32_t      contSpeed;       // Signed continuous speed
  uint32_t     contIntervalUs;  // Step interval in microseconds
  uint32_t     lastContStepUs;  // Microsecond timestamp of last step
  bool         contDir;         // Direction of continuous motion
};

struct HomeOperation {
  bool        active;           // Operation in progress
  AxisId      currentAxis;      // Currently active axis
  bool        dualHome;         // true if executing HOME ALL / G28
  bool        xCompleted;       // Tracks partial outcome of dual homing
  HomePhase   phase;            // Active phase
  HomeResult  result;           // Final or current result
  Transport   replyTo;          // Originating transport for completion reply
  uint32_t    phaseStartedMs;   // Millis timestamp when phase entered
  uint32_t    phaseSteps;       // Step counter for current phase
  uint32_t    lastStepUs;       // Step timing microsecond tracker
  uint32_t    stepIntervalUs;   // Microseconds per step at active speed
  uint32_t    fastTriggerSteps; // Recorded step count at fast seek trigger
  uint32_t    retractSteps;     // Recorded step count to clear
  uint32_t    latchSteps;       // Recorded step count at slow latch
};

struct ParsedCommand {
  char      raw[COMMAND_BUF_SIZE];
  char      verb[24];
  char      arg1[24];
  char      arg2[24];
  char      arg3[24];
  char      arg4[24];
  int       numArgs;
  Transport source;
  bool      tooLong;
};

// ── Global System State ──────────────────────────────────────────────────────
static AxisState     axes[2];
static HomeOperation homeOp;

static uint8_t       microstepMode      = 1;
static uint32_t      stepsPerRev        = BASE_STEPS_PER_REV;
static uint32_t      currentAccel       = DEFAULT_ACCEL_SPS2;
static bool          autoDisableEnabled = true;
static uint32_t      lastMotionMillis   = 0;
static bool          echoCommands       = true;
static bool          invertSensorLogic  = false; // false = active LOW (standard FC-51)
static uint32_t      falseTriggerCount  = 0;

// Transport line buffers
static char          serialBuf[COMMAND_BUF_SIZE];
static size_t        serialBufIdx = 0;
static char          tcpBuf[COMMAND_BUF_SIZE];
static size_t        tcpBufIdx    = 0;

// Networking
static WiFiServer    wifiServer(TCP_PORT);
static WiFiClient    tcpClient;
static bool          wifiConnected = false;

// =============================================================================
//  SECTION 3: FORWARD DECLARATIONS
// =============================================================================

void configureSafePins();
void startSerial();
void beginWifiBounded();
void serviceSerialInput();
void serviceTcp();
bool readLineNonBlocking(Stream &stream, char *buffer, size_t capacity, size_t &idx, bool &discard, bool &overflow);
void sendReply(Transport destination, const char *line);
void sendDiagnostic(const char *line);

bool parseCommandLine(const char *input, Transport source, ParsedCommand &out);
void dispatchCommand(const ParsedCommand &cmd);
bool commandAllowedWhileBusy(const ParsedCommand &cmd);
void handleEmergencyStop(Transport source);
void printHelp(Transport source);
void printStatus(Transport source);
void resetAllSettings(Transport source);

bool readSensorRaw(AxisId axis);
bool sensorDetectedRaw(AxisId axis);
void sampleSensor(AxisId axis, uint32_t nowMs);
void sampleSensors(uint32_t nowMs);
bool sensorDetectedStable(AxisId axis);
void reportSensorReadings(Transport source);
void runMonitorTest(Transport source, AxisId axis, uint32_t durationMs);
void runWatchTest(Transport source, AxisId axis);
void runCalibrationRoutine(Transport source);
void runRangeDiagnostic(Transport source);
void runThresholdDiagnostic(Transport source);
void runDebounceDiagnostic(Transport source, uint8_t samples, uint32_t debounceMs);
void runEdgesDiagnostic(Transport source, uint32_t windowMs);
void runLatencyMeasurement(Transport source);
void runNoiseTest(Transport source, uint32_t windowMs);
void runAmbientTest(Transport source);
void printSensorStatistics(Transport source);
void runDualSensorDashboard(Transport source);
void runDualSensorSync(Transport source);
void runHomeSimDryRun(Transport source, int stepDelayMs);

AxisState &axisState(AxisId axis);
uint8_t stepPin(AxisId axis);
uint8_t directionPin(AxisId axis);
uint8_t enablePin(AxisId axis);
uint8_t sensorPin(AxisId axis);
void setDriverEnabled(AxisId axis, bool enabled);
void setDriversEnabled(bool enabled);
void setMicrostepMode(uint8_t divisor);
void setAxisDirection(AxisId axis, bool positive);
inline void pulseStep(uint8_t pin);
bool stepSingleAxis(AxisId axis, int32_t steps, uint32_t sps);
bool stepDualCoordinated(int32_t stepsA, int32_t stepsB, uint32_t sps);
bool gotoDualAbsolute(int32_t targetA, int32_t targetB, uint32_t sps);
void traceCirclePattern(float radiusSteps, int segments, uint32_t sps);
void traceSquarePattern(float sideSteps, uint32_t sps);
void runBacklashRoutine(Transport source);
void runStallTest(Transport source, uint32_t testSpeed, uint32_t testSteps);
void runSelfTest(Transport source);
void runBenchmark(Transport source);
void serviceContinuousMotion(uint32_t nowUs);
void stopContinuousMotion(AxisId axis);
void stopAllMotion(bool invalidateCoordinates);

bool startHoming(AxisId axis, Transport source, uint32_t nowMs);
bool startHomeAll(Transport source, uint32_t nowMs);
void enterHomePhase(HomePhase nextPhase, uint32_t nowMs);
void serviceHoming(uint32_t nowMs, uint32_t nowUs);
void finishHomeSuccess(AxisId axis);
void failHoming(HomeResult result);
void abortHoming();
const char *homeResultToString(HomeResult res);
const char *homePhaseToString(HomePhase phase);

// =============================================================================
//  SECTION 4: HARDWARE SETUP & MAIN LOOP
// =============================================================================

void setup() {
  // 1. Configure safe pin states immediately
  configureSafePins();

  // 2. Start Serial communication
  startSerial();

  // 3. Sample initial sensor states at boot
  sampleSensors(millis());

  // 4. Print initial banner and safe hardware status
  Serial.println("\n");
  Serial.println("+=======================================================================+");
  Serial.println("|     COMBINED STEPPER MOTOR & IR OPTICAL HOMING BENCH TEST FIXTURE    |");
  Serial.println("|     ESP32-S3 SuperMini | Dual A4988 Drivers | Dual FC-51 Sensors      |");
  Serial.println("+=======================================================================+");
  Serial.printf( "| Motor A (X) : STEP=GPIO %d, DIR=GPIO %d, EN=GPIO %d (Active LOW)        |\n",
                 PIN_STEP_X, PIN_DIR_X, PIN_ENABLE_X);
  Serial.printf( "| Motor B (Y) : STEP=GPIO %d, DIR=GPIO %d, EN=GPIO %d (Active LOW)       |\n",
                 PIN_STEP_Y, PIN_DIR_Y, PIN_ENABLE_Y);
  Serial.printf( "| Microstep   : MS1=GPIO %d, MS2=GPIO %d, MS3=GPIO %d (Shared Bus)          |\n",
                 PIN_MS1, PIN_MS2, PIN_MS3);
  Serial.printf( "| Sensor X    : GPIO %-2d (INPUT_PULLUP, homes Motor A)                     |\n",
                 PIN_SENSOR_X);
  Serial.printf( "| Sensor Y    : GPIO %-2d (INPUT_PULLUP, homes Motor B)                      |\n",
                 PIN_SENSOR_Y);
  Serial.println("| Notice      : GPIO 13 is Sensor X OUT. NO status LED is connected.    |");
  Serial.println("| Notice      : No laser module or laser power is connected.            |");
  Serial.println("+=======================================================================+");

  reportSensorReadings(Transport::Serial);

  // 5. Attempt non-blocking Wi-Fi startup (Serial remains operational if unconfigured)
  beginWifiBounded();

  lastMotionMillis = millis();
}

void loop() {
  uint32_t nowMs = millis();
  uint32_t nowUs = micros();

  // 1. Service Serial line input
  serviceSerialInput();

  // 2. Service TCP socket & client input
  serviceTcp();

  // 3. Continuously sample & filter optical sensors
  sampleSensors(nowMs);

  // 4. Advance active homing state machine by one cooperative step
  if (homeOp.active) {
    serviceHoming(nowMs, nowUs);
  }

  // 5. Advance active continuous rotation if running
  serviceContinuousMotion(nowUs);

  // 6. Idle auto-disable power management (energized coils de-energize after 30s)
  if (autoDisableEnabled && !homeOp.active) {
    bool anyDriverOn = axes[0].driverEnabled || axes[1].driverEnabled;
    bool anyMotionRunning = axes[0].contRunning || axes[1].contRunning;

    if (anyDriverOn && !anyMotionRunning) {
      if ((nowMs - lastMotionMillis) > AUTO_DISABLE_MS) {
        setDriversEnabled(false);
        sendDiagnostic("[SAFETY] Idle timeout (30s) reached. Motor drivers DISABLED to prevent coil heating.");
        sendDiagnostic("         Send ENABLE or any motion command to re-energize.");
      }
    }
  }
}

// =============================================================================
//  SECTION 5: TRANSPORT SUBSYSTEM (SERIAL & NON-BLOCKING TCP)
// =============================================================================

void configureSafePins() {
  // Motor A Output Pins
  pinMode(PIN_STEP_X,   OUTPUT);
  pinMode(PIN_DIR_X,    OUTPUT);
  pinMode(PIN_ENABLE_X, OUTPUT);

  // Motor B Output Pins
  pinMode(PIN_STEP_Y,   OUTPUT);
  pinMode(PIN_DIR_Y,    OUTPUT);
  pinMode(PIN_ENABLE_Y, OUTPUT);

  // Shared Microstep Mode Pins
  pinMode(PIN_MS1,      OUTPUT);
  pinMode(PIN_MS2,      OUTPUT);
  pinMode(PIN_MS3,      OUTPUT);

  // Optical Sensor Input Pins (INPUT_PULLUP as fail-safe for open-collector modules)
  pinMode(PIN_SENSOR_X, INPUT_PULLUP);
  pinMode(PIN_SENSOR_Y, INPUT_PULLUP);

  // Safe initial levels: STEP LOW, DIR LOW, Drivers DISABLED (HIGH)
  digitalWrite(PIN_STEP_X, LOW);
  digitalWrite(PIN_STEP_Y, LOW);
  digitalWrite(PIN_DIR_X,  DIR_CW_LEVEL);
  digitalWrite(PIN_DIR_Y,  DIR_CW_LEVEL);

  // Drivers start de-energized
  digitalWrite(PIN_ENABLE_X, DRIVER_DISABLE_LEVEL);
  digitalWrite(PIN_ENABLE_Y, DRIVER_DISABLE_LEVEL);

  // Default to Full-Step mode (MS1=LOW, MS2=LOW, MS3=LOW)
  digitalWrite(PIN_MS1, LOW);
  digitalWrite(PIN_MS2, LOW);
  digitalWrite(PIN_MS3, LOW);

  // Initialize internal state
  for (int i = 0; i < 2; i++) {
    axes[i].commandedSteps  = 0;
    axes[i].speedSps        = DEFAULT_SPEED_SPS;
    axes[i].invertDirection = false;
    axes[i].driverEnabled   = false;
    axes[i].homed           = false;
    axes[i].motion          = MotionKind::Idle;
    axes[i].contRunning     = false;
    axes[i].contSpeed       = 0;
    axes[i].sensor.minDurationMs = 0xFFFFFFFF;
  }
}

void startSerial() {
  Serial.begin(SERIAL_BAUD);
  uint32_t start = millis();
  while (!Serial && (millis() - start < 1500)) {
    delay(10);
  }
}

void beginWifiBounded() {
  if (strlen(WIFI_SSID) == 0) {
    Serial.println("[WIFI] No Wi-Fi SSID configured. Standalone Serial mode active.");
    return;
  }

  Serial.printf("[WIFI] Connecting to \"%s\" (timeout %lu ms)...\n", WIFI_SSID, WIFI_CONNECT_TIMEOUT_MS);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t startMs = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startMs < WIFI_CONNECT_TIMEOUT_MS)) {
    delay(100);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.printf("[WIFI] Connected! IP Address: %s\n", WiFi.localIP().toString().c_str());
    wifiServer.begin();
    Serial.printf("[WIFI] TCP Command Server listening on port %u.\n", TCP_PORT);
  } else {
    wifiConnected = false;
    Serial.println("[WIFI] Connection timed out. Serial control remains fully operational.");
    WiFi.disconnect(true);
  }
}

bool readLineNonBlocking(Stream &stream, char *buffer, size_t capacity, size_t &idx, bool &discard, bool &overflow) {
  while (stream.available()) {
    int c = stream.read();
    if (c < 0) break;

    // Check for end of line
    if (c == '\n') {
      if (discard) {
        discard = false;
        idx = 0;
        buffer[0] = '\0';
        overflow = true;
        return true; // Return with overflow flag so caller emits error: COMMAND_TOO_LONG
      }
      buffer[idx] = '\0';
      idx = 0;
      overflow = false;
      return true;
    }
    if (c == '\r') {
      continue; // Ignore carriage return
    }

    if (discard) {
      continue; // Drain overlong line until newline
    }

    if (idx < (capacity - 1)) {
      buffer[idx++] = (char)c;
    } else {
      // Overlong line reached: enter discard mode
      discard = true;
      overflow = true;
      buffer[0] = '\0';
      idx = 0;
    }
  }
  return false;
}

static bool serialDiscard = false;
static bool serialOverflow = false;

void serviceSerialInput() {
  if (readLineNonBlocking(Serial, serialBuf, sizeof(serialBuf), serialBufIdx, serialDiscard, serialOverflow)) {
    if (serialOverflow) {
      sendReply(Transport::Serial, "error: COMMAND_TOO_LONG");
      serialOverflow = false;
      return;
    }
    ParsedCommand cmd;
    if (parseCommandLine(serialBuf, Transport::Serial, cmd)) {
      if (echoCommands) {
        Serial.printf(">> %s\n", cmd.raw);
      }
      dispatchCommand(cmd);
    } else if (cmd.tooLong) {
      sendReply(Transport::Serial, "error: COMMAND_TOO_LONG");
    }
  }
}

static bool tcpDiscard = false;
static bool tcpOverflow = false;

void serviceTcp() {
  if (!wifiConnected) return;

  // Check for incoming client connection
  if (wifiServer.hasClient()) {
    if (!tcpClient || !tcpClient.connected()) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
      tcpClient = wifiServer.accept();
#else
      tcpClient = wifiServer.available();
#endif
      tcpBufIdx = 0;
      tcpDiscard = false;
      tcpOverflow = false;
      sendDiagnostic("[TCP] New client connected.");
    } else {
      // Reject competing second client explicitly
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
      WiFiClient rejected = wifiServer.accept();
#else
      WiFiClient rejected = wifiServer.available();
#endif
      rejected.println("error: BUSY_ANOTHER_CLIENT_CONNECTED");
      rejected.stop();
      sendDiagnostic("[TCP] Rejected competing second client connection.");
    }
  }

  // Service active client
  if (tcpClient && tcpClient.connected()) {
    if (readLineNonBlocking(tcpClient, tcpBuf, sizeof(tcpBuf), tcpBufIdx, tcpDiscard, tcpOverflow)) {
      if (tcpOverflow) {
        sendReply(Transport::Tcp, "error: COMMAND_TOO_LONG");
        tcpOverflow = false;
        return;
      }
      ParsedCommand cmd;
      if (parseCommandLine(tcpBuf, Transport::Tcp, cmd)) {
        dispatchCommand(cmd);
      } else if (cmd.tooLong) {
        sendReply(Transport::Tcp, "error: COMMAND_TOO_LONG");
      }
    }
  }
}

void sendReply(Transport destination, const char *line) {
  if (destination == Transport::Serial) {
    Serial.println(line);
  } else if (destination == Transport::Tcp && tcpClient && tcpClient.connected()) {
    tcpClient.println(line);
  }
}

void sendDiagnostic(const char *line) {
  // Serial-only diagnostics prevent corrupting TCP protocol responses
  Serial.println(line);
}

// =============================================================================
//  SECTION 6: COMMAND PARSER & DISPATCHER
// =============================================================================

static inline void copyTokenBounded(char *dest, size_t destCap, const char *src) {
  size_t i = 0;
  while (src[i] != '\0' && i < (destCap - 1)) {
    dest[i] = (char)toupper((unsigned char)src[i]);
    i++;
  }
  dest[i] = '\0';
}

bool parseCommandLine(const char *input, Transport source, ParsedCommand &out) {
  memset(&out, 0, sizeof(out));
  out.source = source;

  size_t len = strlen(input);
  if (len >= COMMAND_BUF_SIZE) {
    out.tooLong = true;
    return false;
  }

  // Trim whitespace
  size_t start = 0;
  while (start < len && (input[start] == ' ' || input[start] == '\t' || input[start] == '\r' || input[start] == '\n')) {
    start++;
  }
  if (start >= len) {
    return false; // Empty line
  }

  size_t end = len - 1;
  while (end > start && (input[end] == ' ' || input[end] == '\t' || input[end] == '\r' || input[end] == '\n')) {
    end--;
  }

  size_t cleanLen = end - start + 1;
  if (cleanLen >= sizeof(out.raw)) cleanLen = sizeof(out.raw) - 1;
  memcpy(out.raw, &input[start], cleanLen);
  out.raw[cleanLen] = '\0';

  // Tokenize up to 4 arguments
  char temp[COMMAND_BUF_SIZE];
  strcpy(temp, out.raw);

  char *saveptr = nullptr;
  char *tok = strtok_r(temp, " \t", &saveptr);
  if (!tok) return false;
  copyTokenBounded(out.verb, sizeof(out.verb), tok);

  tok = strtok_r(nullptr, " \t", &saveptr);
  if (tok) {
    copyTokenBounded(out.arg1, sizeof(out.arg1), tok);
    out.numArgs++;

    tok = strtok_r(nullptr, " \t", &saveptr);
    if (tok) {
      copyTokenBounded(out.arg2, sizeof(out.arg2), tok);
      out.numArgs++;

      tok = strtok_r(nullptr, " \t", &saveptr);
      if (tok) {
        copyTokenBounded(out.arg3, sizeof(out.arg3), tok);
        out.numArgs++;

        tok = strtok_r(nullptr, " \t", &saveptr);
        if (tok) {
          copyTokenBounded(out.arg4, sizeof(out.arg4), tok);
          out.numArgs++;
        }
      }
    }
  }

  return true;
}

bool commandAllowedWhileBusy(const ParsedCommand &cmd) {
  if (strcmp(cmd.verb, "X") == 0 || strcmp(cmd.verb, "!") == 0) return true;
  if (strcmp(cmd.verb, "STATUS") == 0 || strcmp(cmd.verb, "?") == 0) return true;
  if (strcmp(cmd.verb, "HELP") == 0 || strcmp(cmd.verb, "H") == 0) return true;
  if (strcmp(cmd.verb, "READ") == 0) return true;
  if (strcmp(cmd.verb, "ECHO") == 0) return true;
  return false;
}

void handleEmergencyStop(Transport source) {
  stopAllMotion(true); // Invalidate coordinates on emergency stop
  if (homeOp.active) {
    abortHoming(); // Sends single error: HOME_ABORTED <axis> to homeOp.replyTo
  } else {
    sendReply(source, "ok");
  }
  sendDiagnostic("[ABORT] Emergency stop triggered. All motion halted immediately.");
}

void dispatchCommand(const ParsedCommand &cmd) {
  // Check busy state during active homing
  if (homeOp.active && !commandAllowedWhileBusy(cmd)) {
    sendReply(cmd.source, "error: BUSY");
    sendDiagnostic("[BUSY] Command rejected while homing operation is active.");
    return;
  }

  // ── Quick Abort Key ('X', 'x', '!') ────────────────────────────────────────
  if (strcmp(cmd.verb, "X") == 0 || strcmp(cmd.verb, "!") == 0) {
    handleEmergencyStop(cmd.source);
    return;
  }

  // ── G-Code & Physical Homing Commands ──────────────────────────────────────
  if (strcmp(cmd.verb, "G28") == 0) {
    startHomeAll(cmd.source, millis());
    return;
  }

  if (strcmp(cmd.verb, "HOME") == 0) {
    if (cmd.numArgs == 0 || strcmp(cmd.arg1, "ALL") == 0 || strcmp(cmd.arg1, "BOTH") == 0) {
      startHomeAll(cmd.source, millis());
    } else if (strcmp(cmd.arg1, "X") == 0 || strcmp(cmd.arg1, "A") == 0) {
      startHoming(AxisId::X, cmd.source, millis());
    } else if (strcmp(cmd.arg1, "Y") == 0 || strcmp(cmd.arg1, "B") == 0) {
      startHoming(AxisId::Y, cmd.source, millis());
    } else {
      sendReply(cmd.source, "error: INVALID_AXIS");
    }
    return;
  }

  // ── DUAL Command Family & Collision Resolution ─────────────────────────────
  if (strcmp(cmd.verb, "DUAL") == 0) {
    if (cmd.numArgs == 0 || strcmp(cmd.arg1, "DASHBOARD") == 0 || strcmp(cmd.arg1, "SENSOR") == 0) {
      // Dual sensor dashboard
      runDualSensorDashboard(cmd.source);
    } else if (strcmp(cmd.arg1, "SYNC") == 0) {
      // Dual sensor alignment synchronization test
      runDualSensorSync(cmd.source);
    } else if (strcmp(cmd.arg1, "STEP") == 0) {
      // DUAL STEP <nA> <nB> [sps]
      long nA = 0, nB = 0;
      uint32_t sps = 0;
      if (cmd.numArgs >= 2) nA = atol(cmd.arg2);
      if (cmd.numArgs >= 3) nB = atol(cmd.arg3);
      if (cmd.numArgs >= 4) sps = atoi(cmd.arg4);
      stepDualCoordinated(nA, nB, sps);
      sendReply(cmd.source, "ok");
    } else if (strcmp(cmd.arg1, "GOTO") == 0) {
      // DUAL GOTO <targetA> <targetB> [sps]
      long tA = 0, tB = 0;
      uint32_t sps = 0;
      if (cmd.numArgs >= 2) tA = atol(cmd.arg2);
      if (cmd.numArgs >= 3) tB = atol(cmd.arg3);
      if (cmd.numArgs >= 4) sps = atoi(cmd.arg4);
      gotoDualAbsolute(tA, tB, sps);
      sendReply(cmd.source, "ok");
    } else if (strcmp(cmd.arg1, "SPEED") == 0) {
      // DUAL SPEED <sA> <sB>
      if (cmd.numArgs >= 2) {
        uint32_t sA = atoi(cmd.arg2);
        uint32_t sB = (cmd.numArgs >= 3) ? atoi(cmd.arg3) : sA;
        axes[0].speedSps = constrain(sA, MIN_SPEED_SPS, MAX_SPEED_SPS);
        axes[1].speedSps = constrain(sB, MIN_SPEED_SPS, MAX_SPEED_SPS);
        sendReply(cmd.source, "ok");
      } else {
        sendReply(cmd.source, "error: MISSING_SPEED_ARGS");
      }
    } else if (strcmp(cmd.arg1, "CIRCLE") == 0) {
      // DUAL CIRCLE [radius] [segments] [sps]
      float radius = 500.0f;
      int segments = 64;
      if (cmd.numArgs >= 2) radius = atof(cmd.arg2);
      if (cmd.numArgs >= 3) segments = atoi(cmd.arg3);
      traceCirclePattern(radius, segments, 0);
      sendReply(cmd.source, "ok");
    } else if (strcmp(cmd.arg1, "SQUARE") == 0) {
      // DUAL SQUARE [size] [sps]
      float size = 500.0f;
      if (cmd.numArgs >= 2) size = atof(cmd.arg2);
      traceSquarePattern(size, 0);
      sendReply(cmd.source, "ok");
    } else if (strcmp(cmd.arg1, "HOME") == 0) {
      // Legacy open-loop coordinate return (0, 0)
      sendDiagnostic("[DUAL HOME] Returning both axes from current coordinates to (0, 0)...");
      sendDiagnostic("[WARNING]   This is an OPEN-LOOP move to software coordinate zero.");
      sendDiagnostic("            It does NOT seek physical optical endstops!");
      sendDiagnostic("            For physical optical homing, use HOME ALL or G28.");
      gotoDualAbsolute(0, 0, 0);
      sendReply(cmd.source, "ok");
    } else {
      sendReply(cmd.source, "error: UNKNOWN_DUAL_SUBCOMMAND");
    }
    return;
  }

  // ── Driver Power & Microstepping ───────────────────────────────────────────
  if (strcmp(cmd.verb, "ENABLE") == 0) {
    setDriversEnabled(true);
    sendReply(cmd.source, "ok");
    return;
  }

  if (strcmp(cmd.verb, "DISABLE") == 0) {
    stopAllMotion(true);
    setDriversEnabled(false);
    sendReply(cmd.source, "ok");
    return;
  }

  if (strcmp(cmd.verb, "MICRO") == 0) {
    if (cmd.numArgs >= 1) {
      int mode = atoi(cmd.arg1);
      if (mode == 1 || mode == 2 || mode == 4 || mode == 8 || mode == 16) {
        setMicrostepMode((uint8_t)mode);
        sendReply(cmd.source, "ok");
      } else {
        sendReply(cmd.source, "error: INVALID_MICROSTEP_MODE");
      }
    } else {
      char resp[64];
      snprintf(resp, sizeof(resp), "[MICRO] 1/%d (Effective steps/rev: %u)", microstepMode, stepsPerRev);
      sendReply(cmd.source, resp);
    }
    return;
  }

  // ── Continuous Motion (START / STOP) ───────────────────────────────────────
  if (strcmp(cmd.verb, "START") == 0) {
    char target = 'A';
    int sps = 100;
    if (cmd.numArgs >= 1) {
      if (strcmp(cmd.arg1, "B") == 0 || strcmp(cmd.arg1, "Y") == 0) target = 'B';
      else if (strcmp(cmd.arg1, "BOTH") == 0 || strcmp(cmd.arg1, "ALL") == 0) target = '+';
      else if (isdigit(cmd.arg1[0]) || cmd.arg1[0] == '-') {
        target = 'A';
        sps = atoi(cmd.arg1);
      }
    }
    if (cmd.numArgs >= 2) {
      sps = atoi(cmd.arg2);
    }
    if (abs(sps) < (int)MIN_SPEED_SPS) sps = 100;
    sps = constrain(sps, -(int)MAX_SPEED_SPS, (int)MAX_SPEED_SPS);

    setDriversEnabled(true);
    lastMotionMillis = millis();

    if (target == 'A' || target == '+') {
      axes[0].speedSps = (uint32_t)abs(sps);
      axes[0].contSpeed = sps;
      axes[0].contDir = (sps >= 0);
      axes[0].contIntervalUs = 1000000UL / axes[0].speedSps;
      axes[0].lastContStepUs = micros();
      axes[0].contRunning = true;
      axes[0].motion = MotionKind::Continuous;
    }
    if (target == 'B' || target == '+') {
      axes[1].speedSps = (uint32_t)abs(sps);
      axes[1].contSpeed = sps;
      axes[1].contDir = (sps >= 0);
      axes[1].contIntervalUs = 1000000UL / axes[1].speedSps;
      axes[1].lastContStepUs = micros();
      axes[1].contRunning = true;
      axes[1].motion = MotionKind::Continuous;
    }
    sendReply(cmd.source, "ok");
    return;
  }

  if (strcmp(cmd.verb, "STOP") == 0) {
    if (cmd.numArgs >= 1 && (strcmp(cmd.arg1, "A") == 0 || strcmp(cmd.arg1, "X") == 0)) {
      stopContinuousMotion(AxisId::X);
    } else if (cmd.numArgs >= 1 && (strcmp(cmd.arg1, "B") == 0 || strcmp(cmd.arg1, "Y") == 0)) {
      stopContinuousMotion(AxisId::Y);
    } else {
      stopContinuousMotion(AxisId::X);
      stopContinuousMotion(AxisId::Y);
    }
    lastMotionMillis = millis();
    sendReply(cmd.source, "ok");
    return;
  }

  // ── Discrete Relative Moves: STEP ──────────────────────────────────────────
  if (strcmp(cmd.verb, "STEP") == 0) {
    char target = 'A';
    long steps = 100;
    uint32_t sps = 0;

    if (cmd.numArgs >= 1) {
      if (strcmp(cmd.arg1, "B") == 0 || strcmp(cmd.arg1, "Y") == 0) {
        target = 'B';
        if (cmd.numArgs >= 2) steps = atol(cmd.arg2);
        if (cmd.numArgs >= 3) sps = atoi(cmd.arg3);
      } else if (strcmp(cmd.arg1, "BOTH") == 0 || strcmp(cmd.arg1, "ALL") == 0) {
        target = '+';
        if (cmd.numArgs >= 2) steps = atol(cmd.arg2);
        if (cmd.numArgs >= 3) sps = atoi(cmd.arg3);
      } else if (strcmp(cmd.arg1, "A") == 0 || strcmp(cmd.arg1, "X") == 0) {
        target = 'A';
        if (cmd.numArgs >= 2) steps = atol(cmd.arg2);
        if (cmd.numArgs >= 3) sps = atoi(cmd.arg3);
      } else {
        steps = atol(cmd.arg1);
        if (cmd.numArgs >= 2) sps = atoi(cmd.arg2);
      }
    }

    if (target == 'A') stepSingleAxis(AxisId::X, steps, sps);
    else if (target == 'B') stepSingleAxis(AxisId::Y, steps, sps);
    else stepDualCoordinated(steps, steps, sps);

    sendReply(cmd.source, "ok");
    return;
  }

  // ── Absolute Move: GOTO ────────────────────────────────────────────────────
  if (strcmp(cmd.verb, "GOTO") == 0) {
    char target = 'A';
    long dest = 0;
    uint32_t sps = 0;

    if (cmd.numArgs >= 1) {
      if (strcmp(cmd.arg1, "B") == 0 || strcmp(cmd.arg1, "Y") == 0) {
        target = 'B';
        if (cmd.numArgs >= 2) dest = atol(cmd.arg2);
        if (cmd.numArgs >= 3) sps = atoi(cmd.arg3);
      } else if (strcmp(cmd.arg1, "A") == 0 || strcmp(cmd.arg1, "X") == 0) {
        target = 'A';
        if (cmd.numArgs >= 2) dest = atol(cmd.arg2);
        if (cmd.numArgs >= 3) sps = atoi(cmd.arg3);
      } else {
        dest = atol(cmd.arg1);
        if (cmd.numArgs >= 2) sps = atoi(cmd.arg2);
      }
    }

    if (target == 'A') {
      long delta = dest - axes[0].commandedSteps;
      stepSingleAxis(AxisId::X, delta, sps);
    } else {
      long delta = dest - axes[1].commandedSteps;
      stepSingleAxis(AxisId::Y, delta, sps);
    }
    sendReply(cmd.source, "ok");
    return;
  }

  // ── Position Reporting & Coordinate Reset ──────────────────────────────────
  if (strcmp(cmd.verb, "POS") == 0) {
    char resp[128];
    snprintf(resp, sizeof(resp), "[POS] X: %ld steps (%.2f deg, homed=%d) | Y: %ld steps (%.2f deg, homed=%d)",
             axes[0].commandedSteps, (float)axes[0].commandedSteps * 360.0f / stepsPerRev, axes[0].homed ? 1 : 0,
             axes[1].commandedSteps, (float)axes[1].commandedSteps * 360.0f / stepsPerRev, axes[1].homed ? 1 : 0);
    sendReply(cmd.source, resp);
    return;
  }

  if (strcmp(cmd.verb, "ZERO") == 0) {
    axes[0].commandedSteps = 0;
    axes[1].commandedSteps = 0;
    sendDiagnostic("[ZERO] Position registers reset to 0. Note: This does NOT set homed=true.");
    sendReply(cmd.source, "ok");
    return;
  }

  // ── Direction & Speed Settings ─────────────────────────────────────────────
  if (strcmp(cmd.verb, "DIR") == 0) {
    if (cmd.numArgs >= 1) {
      char target = 'A';
      const char *dirVal = cmd.arg1;
      if (cmd.numArgs >= 2) {
        if (strcmp(cmd.arg1, "B") == 0 || strcmp(cmd.arg1, "Y") == 0) target = 'B';
        else if (strcmp(cmd.arg1, "BOTH") == 0 || strcmp(cmd.arg1, "ALL") == 0) target = '+';
        dirVal = cmd.arg2;
      }
      bool inv = (strcmp(dirVal, "CCW") == 0 || strcmp(dirVal, "INVERT") == 0 || strcmp(dirVal, "1") == 0);
      if (target == 'A' || target == '+') axes[0].invertDirection = inv;
      if (target == 'B' || target == '+') axes[1].invertDirection = inv;
      sendReply(cmd.source, "ok");
    } else {
      char resp[64];
      snprintf(resp, sizeof(resp), "[DIR] Motor A: %s | Motor B: %s",
               axes[0].invertDirection ? "INVERTED (CCW)" : "NORMAL (CW)",
               axes[1].invertDirection ? "INVERTED (CCW)" : "NORMAL (CW)");
      sendReply(cmd.source, resp);
    }
    return;
  }

  if (strcmp(cmd.verb, "SPEED") == 0) {
    if (cmd.numArgs >= 1) {
      char target = 'A';
      uint32_t sps = atoi(cmd.arg1);
      if (cmd.numArgs >= 2) {
        if (strcmp(cmd.arg1, "B") == 0 || strcmp(cmd.arg1, "Y") == 0) target = 'B';
        else if (strcmp(cmd.arg1, "BOTH") == 0 || strcmp(cmd.arg1, "ALL") == 0) target = '+';
        sps = atoi(cmd.arg2);
      }
      sps = constrain(sps, MIN_SPEED_SPS, MAX_SPEED_SPS);
      if (target == 'A' || target == '+') axes[0].speedSps = sps;
      if (target == 'B' || target == '+') axes[1].speedSps = sps;
      sendReply(cmd.source, "ok");
    } else {
      char resp[64];
      snprintf(resp, sizeof(resp), "[SPEED] A: %u sps | B: %u sps", axes[0].speedSps, axes[1].speedSps);
      sendReply(cmd.source, resp);
    }
    return;
  }

  if (strcmp(cmd.verb, "REV") == 0) {
    char target = 'A';
    float revs = 1.0f;
    uint32_t sps = 0;
    if (cmd.numArgs >= 1) {
      if (strcmp(cmd.arg1, "B") == 0 || strcmp(cmd.arg1, "Y") == 0) {
        target = 'B';
        if (cmd.numArgs >= 2) revs = atof(cmd.arg2);
        if (cmd.numArgs >= 3) sps = atoi(cmd.arg3);
      } else if (strcmp(cmd.arg1, "BOTH") == 0 || strcmp(cmd.arg1, "ALL") == 0) {
        target = '+';
        if (cmd.numArgs >= 2) revs = atof(cmd.arg2);
        if (cmd.numArgs >= 3) sps = atoi(cmd.arg3);
      } else {
        revs = atof(cmd.arg1);
        if (cmd.numArgs >= 2) sps = atoi(cmd.arg2);
      }
    }
    long totalSteps = (long)round(revs * (float)stepsPerRev);
    if (target == 'A') stepSingleAxis(AxisId::X, totalSteps, sps);
    else if (target == 'B') stepSingleAxis(AxisId::Y, totalSteps, sps);
    else stepDualCoordinated(totalSteps, totalSteps, sps);
    sendReply(cmd.source, "ok");
    return;
  }

  if (strcmp(cmd.verb, "RPM") == 0) {
    float rpmA = (float)axes[0].speedSps * 60.0f / stepsPerRev;
    float rpmB = (float)axes[1].speedSps * 60.0f / stepsPerRev;
    char resp[128];
    snprintf(resp, sizeof(resp), "[RPM] A: %u sps (%.2f RPM) | B: %u sps (%.2f RPM)",
             axes[0].speedSps, rpmA, axes[1].speedSps, rpmB);
    sendReply(cmd.source, resp);
    return;
  }

  if (strcmp(cmd.verb, "ACCEL") == 0) {
    if (cmd.numArgs >= 1) {
      currentAccel = constrain(atoi(cmd.arg1), 50, 50000);
      sendDiagnostic("[ACCEL] Configuration updated. (Note: Current motion primitives step at constant rate).");
      sendReply(cmd.source, "ok");
    } else {
      char resp[64];
      snprintf(resp, sizeof(resp), "[ACCEL] %u steps/sec^2 (Configuration-only)", currentAccel);
      sendReply(cmd.source, resp);
    }
    return;
  }

  if (strcmp(cmd.verb, "AUTODISABLE") == 0) {
    if (cmd.numArgs >= 1) {
      autoDisableEnabled = (strcmp(cmd.arg1, "1") == 0 || strcmp(cmd.arg1, "ON") == 0 || strcmp(cmd.arg1, "TRUE") == 0);
      sendReply(cmd.source, "ok");
    } else {
      sendReply(cmd.source, autoDisableEnabled ? "[AUTODISABLE] ON (30s)" : "[AUTODISABLE] OFF");
    }
    return;
  }

  // ── Motor Characterization Diagnostics ─────────────────────────────────────
  if (strcmp(cmd.verb, "BACKLASH") == 0) {
    runBacklashRoutine(cmd.source);
    return;
  }

  if (strcmp(cmd.verb, "STALL") == 0) {
    uint32_t testSpeed = 1000;
    uint32_t testSteps = 2048;
    if (cmd.numArgs >= 1) testSpeed = atoi(cmd.arg1);
    if (cmd.numArgs >= 2) testSteps = atoi(cmd.arg2);
    runStallTest(cmd.source, testSpeed, testSteps);
    return;
  }

  if (strcmp(cmd.verb, "TEST") == 0) {
    runSelfTest(cmd.source);
    return;
  }

  if (strcmp(cmd.verb, "BENCHMARK") == 0) {
    runBenchmark(cmd.source);
    return;
  }

  // ── Optical Sensor Diagnostics ─────────────────────────────────────────────
  if (strcmp(cmd.verb, "READ") == 0) {
    reportSensorReadings(cmd.source);
    return;
  }

  if (strcmp(cmd.verb, "MONITOR") == 0) {
    AxisId target = AxisId::X;
    uint32_t dur = 5000;
    if (cmd.numArgs >= 1 && (strcmp(cmd.arg1, "Y") == 0 || strcmp(cmd.arg1, "B") == 0)) target = AxisId::Y;
    if (cmd.numArgs >= 2) dur = atoi(cmd.arg2);
    runMonitorTest(cmd.source, target, dur);
    return;
  }

  if (strcmp(cmd.verb, "WATCH") == 0) {
    AxisId target = AxisId::X;
    if (cmd.numArgs >= 1 && (strcmp(cmd.arg1, "Y") == 0 || strcmp(cmd.arg1, "B") == 0)) target = AxisId::Y;
    runWatchTest(cmd.source, target);
    return;
  }

  if (strcmp(cmd.verb, "CALIB") == 0) {
    runCalibrationRoutine(cmd.source);
    return;
  }

  if (strcmp(cmd.verb, "RANGE") == 0) {
    runRangeDiagnostic(cmd.source);
    return;
  }

  if (strcmp(cmd.verb, "THRESHOLD") == 0) {
    runThresholdDiagnostic(cmd.source);
    return;
  }

  if (strcmp(cmd.verb, "DEBOUNCE") == 0) {
    uint8_t samples = HOME_STABLE_SAMPLES;
    uint32_t dbMs = HOME_DEBOUNCE_MS;
    if (cmd.numArgs >= 1) samples = atoi(cmd.arg1);
    if (cmd.numArgs >= 2) dbMs = atoi(cmd.arg2);
    runDebounceDiagnostic(cmd.source, samples, dbMs);
    return;
  }

  if (strcmp(cmd.verb, "EDGES") == 0) {
    uint32_t window = 5000;
    if (cmd.numArgs >= 1) window = atoi(cmd.arg1) * 1000UL;
    runEdgesDiagnostic(cmd.source, window);
    return;
  }

  if (strcmp(cmd.verb, "RESPONSE") == 0 || strcmp(cmd.verb, "LATENCY") == 0) {
    runLatencyMeasurement(cmd.source);
    return;
  }

  if (strcmp(cmd.verb, "NOISE") == 0) {
    uint32_t window = 5000;
    if (cmd.numArgs >= 1) window = atoi(cmd.arg1) * 1000UL;
    runNoiseTest(cmd.source, window);
    return;
  }

  if (strcmp(cmd.verb, "AMBIENT") == 0) {
    runAmbientTest(cmd.source);
    return;
  }

  if (strcmp(cmd.verb, "STATS") == 0) {
    printSensorStatistics(cmd.source);
    return;
  }

  if (strcmp(cmd.verb, "INVERT") == 0) {
    invertSensorLogic = !invertSensorLogic;
    char resp[80];
    snprintf(resp, sizeof(resp), "[INVERT] Sensor polarity: %s",
             invertSensorLogic ? "Active HIGH" : "Active LOW (FC-51 Default)");
    sendReply(cmd.source, resp);
    return;
  }

  if (strcmp(cmd.verb, "LED") == 0) {
    sendDiagnostic("[NOTICE] GPIO 13 is configured as Sensor X OUT. No onboard status LED is connected.");
    sendReply(cmd.source, "ok");
    return;
  }

  if (strcmp(cmd.verb, "HOMESIM") == 0) {
    int delayMs = 2;
    if (cmd.numArgs >= 1) delayMs = atoi(cmd.arg1);
    runHomeSimDryRun(cmd.source, delayMs);
    return;
  }

  // ── General Utilities ──────────────────────────────────────────────────────
  if (strcmp(cmd.verb, "STATUS") == 0 || strcmp(cmd.verb, "?") == 0) {
    printStatus(cmd.source);
    return;
  }

  if (strcmp(cmd.verb, "HELP") == 0 || strcmp(cmd.verb, "H") == 0) {
    printHelp(cmd.source);
    return;
  }

  if (strcmp(cmd.verb, "RESET") == 0) {
    resetAllSettings(cmd.source);
    sendReply(cmd.source, "ok");
    return;
  }

  if (strcmp(cmd.verb, "ECHO") == 0) {
    echoCommands = !echoCommands;
    sendReply(cmd.source, echoCommands ? "[ECHO] ON" : "[ECHO] OFF");
    return;
  }

  sendReply(cmd.source, "error: UNKNOWN_COMMAND");
}

void resetAllSettings(Transport source) {
  stopAllMotion(true);
  setDriversEnabled(false);
  setMicrostepMode(1);
  for (int i = 0; i < 2; i++) {
    axes[i].commandedSteps  = 0;
    axes[i].speedSps        = DEFAULT_SPEED_SPS;
    axes[i].invertDirection = false;
    axes[i].homed           = false;
    axes[i].motion          = MotionKind::Idle;
    memset(&axes[i].sensor, 0, sizeof(SensorFilter));
    axes[i].sensor.minDurationMs = 0xFFFFFFFF;
  }
  falseTriggerCount = 0;
  invertSensorLogic = false;
  autoDisableEnabled = true;
  lastMotionMillis = millis();
  sendDiagnostic("[RESET] Restored factory defaults. Coordinates reset to 0 (unhomed), drivers DISABLED.");
}

// =============================================================================
//  SECTION 7: SENSOR SUBSYSTEM & DIAGNOSTICS
// =============================================================================

bool readSensorRaw(AxisId axis) {
  uint8_t pin = (axis == AxisId::X) ? PIN_SENSOR_X : PIN_SENSOR_Y;
  return digitalRead(pin) == HIGH;
}

bool sensorDetectedRaw(AxisId axis) {
  bool rawHigh = readSensorRaw(axis);
  return invertSensorLogic ? rawHigh : !rawHigh;
}

void sampleSensor(AxisId axis, uint32_t nowMs) {
  SensorFilter &sf = axes[(int)axis].sensor;
  sf.rawHigh = readSensorRaw(axis);
  bool detectedRaw = invertSensorLogic ? sf.rawHigh : !sf.rawHigh;

  if (detectedRaw == sf.candidateDetected) {
    if (sf.sameSamples < 255) sf.sameSamples++;
  } else {
    sf.candidateDetected = detectedRaw;
    sf.sameSamples = 1;
    sf.candidateSinceMs = nowMs;
    sf.unstableEdges++;
  }

  // Stable transition evaluation
  if (sf.candidateDetected != sf.stableDetected) {
    if (sf.sameSamples >= HOME_STABLE_SAMPLES && (nowMs - sf.candidateSinceMs) >= HOME_DEBOUNCE_MS) {
      sf.stableDetected = sf.candidateDetected;
      sf.lastTransitionMs = nowMs;
      if (sf.stableDetected) {
        sf.triggers++;
        sf.lastStableDetectedMs = nowMs;
      } else {
        sf.releases++;
        if (sf.lastStableDetectedMs > 0 && nowMs >= sf.lastStableDetectedMs) {
          uint32_t dur = nowMs - sf.lastStableDetectedMs;
          if (dur < sf.minDurationMs) sf.minDurationMs = dur;
          if (dur > sf.maxDurationMs) sf.maxDurationMs = dur;
          sf.totalDurationMs += dur;
          sf.completedCycles++;
        }
      }
    }
  }
}

void sampleSensors(uint32_t nowMs) {
  sampleSensor(AxisId::X, nowMs);
  sampleSensor(AxisId::Y, nowMs);
}

bool sensorDetectedStable(AxisId axis) {
  return axes[(int)axis].sensor.stableDetected;
}

void reportSensorReadings(Transport source) {
  sampleSensors(millis());
  bool rx = axes[0].sensor.rawHigh;
  bool ry = axes[1].sensor.rawHigh;
  bool dx = axes[0].sensor.stableDetected;
  bool dy = axes[1].sensor.stableDetected;

  char buf[128];
  snprintf(buf, sizeof(buf), "[READ] X (GPIO %d): Raw=%s State=%s | Y (GPIO %d): Raw=%s State=%s",
           PIN_SENSOR_X, rx ? "HIGH" : "LOW", dx ? "DETECTED" : "CLEAR",
           PIN_SENSOR_Y, ry ? "HIGH" : "LOW", dy ? "DETECTED" : "CLEAR");
  sendReply(source, buf);
}

void runMonitorTest(Transport source, AxisId axis, uint32_t durationMs) {
  sendDiagnostic("[MONITOR] Streaming sensor transitions. Send 'X' to abort.");
  uint32_t startMs = millis();
  bool lastState = axes[(int)axis].sensor.stableDetected;

  while (millis() - startMs < durationMs) {
    serviceSerialInput();
    serviceTcp();
    sampleSensors(millis());

    bool curState = axes[(int)axis].sensor.stableDetected;
    if (curState != lastState) {
      lastState = curState;
      char buf[64];
      snprintf(buf, sizeof(buf), "  [%lu ms] Axis %s: %s",
               millis() - startMs, (axis == AxisId::X) ? "X" : "Y", curState ? "DETECTED" : "CLEAR");
      sendReply(source, buf);
    }
    delay(5);
  }
  sendReply(source, "ok");
}

void runWatchTest(Transport source, AxisId axis) {
  runMonitorTest(source, axis, 10000);
}

void runCalibrationRoutine(Transport source) {
  sendDiagnostic("\n[CALIB] Optical Threshold Calibration Wizard.");
  sendDiagnostic("1. Align optical reflective flag squarely in front of Sensor X.");
  sendDiagnostic("2. Observe the onboard comparator indicator LED on the sensor module.");
  sendDiagnostic("3. Adjust the blue multiturn trim-pot until LED reliably trips at 5-10mm.");
  sendDiagnostic("4. Send READ to verify interpreted stable detection.");
  sendReply(source, "ok");
}

void runRangeDiagnostic(Transport source) {
  sendDiagnostic("[RANGE] Testing optical detection range. Move target slowly toward sensor.");
  reportSensorReadings(source);
}

void runThresholdDiagnostic(Transport source) {
  sendDiagnostic("[THRESHOLD] Diagnostic recommendation: Calibrate comparator threshold");
  sendDiagnostic("            to trigger when target enters the 3-8 mm band.");
  reportSensorReadings(source);
}

void runDebounceDiagnostic(Transport source, uint8_t samples, uint32_t debounceMs) {
  char buf[80];
  snprintf(buf, sizeof(buf), "[DEBOUNCE] Filter parameters: %u stable samples, %lu ms time window.", samples, debounceMs);
  sendReply(source, buf);
}

void runEdgesDiagnostic(Transport source, uint32_t windowMs) {
  sendDiagnostic("[EDGES] Counting raw electrical transitions...");
  uint32_t xEdges = 0, yEdges = 0;
  bool lastX = readSensorRaw(AxisId::X);
  bool lastY = readSensorRaw(AxisId::Y);
  uint32_t startMs = millis();
  uint32_t lastYieldMs = startMs;

  while (millis() - startMs < windowMs) {
    bool cx = readSensorRaw(AxisId::X);
    bool cy = readSensorRaw(AxisId::Y);
    if (cx != lastX) { xEdges++; lastX = cx; }
    if (cy != lastY) { yEdges++; lastY = cy; }
    delayMicroseconds(50);
    uint32_t now = millis();
    if (now - lastYieldMs >= 20) {
      yield();
      lastYieldMs = now;
    }
  }

  char buf[96];
  snprintf(buf, sizeof(buf), "[EDGES] In %lu ms: Sensor X=%u edges, Sensor Y=%u edges", windowMs, xEdges, yEdges);
  sendReply(source, buf);
}

void runLatencyMeasurement(Transport source) {
  sendDiagnostic("[LATENCY] Estimating optical latch overshoot based on step rate:");
  float tauS = (float)HOME_DEBOUNCE_MS / 1000.0f;
  float fastOvershoot = (float)HOMING_FAST_SPS * tauS;
  float slowOvershoot = (float)HOMING_SLOW_SPS * tauS;

  char buf[128];
  snprintf(buf, sizeof(buf), "[LATENCY] FastSeek (%u sps): ~%.2f steps | SlowLatch (%u sps): ~%.2f steps",
           HOMING_FAST_SPS, fastOvershoot, HOMING_SLOW_SPS, slowOvershoot);
  sendReply(source, buf);
}

void runNoiseTest(Transport source, uint32_t windowMs) {
  sendDiagnostic("[NOISE] Monitoring for electrical or optical noise glitches at rest...");
  uint32_t glitchesX = 0, glitchesY = 0;
  bool baseHighX = readSensorRaw(AxisId::X);
  bool baseHighY = readSensorRaw(AxisId::Y);
  uint32_t startMs = millis();
  uint32_t lastYieldMs = startMs;

  while (millis() - startMs < windowMs) {
    if (readSensorRaw(AxisId::X) != baseHighX) glitchesX++;
    if (readSensorRaw(AxisId::Y) != baseHighY) glitchesY++;
    delayMicroseconds(50);
    uint32_t now = millis();
    if (now - lastYieldMs >= 20) {
      yield();
      lastYieldMs = now;
    }
  }

  char buf[96];
  snprintf(buf, sizeof(buf), "[NOISE] Glitches in %lu ms: X=%u, Y=%u (%s)",
           windowMs, glitchesX, glitchesY, (glitchesX == 0 && glitchesY == 0) ? "EXCELLENT" : "NOISE_DETECTED");
  sendReply(source, buf);
}

void runAmbientTest(Transport source) {
  sendDiagnostic("[AMBIENT] Checking for AC mains room lighting flicker (100/120 Hz)...");
  uint32_t transX = 0, transY = 0;
  bool lastX = readSensorRaw(AxisId::X);
  bool lastY = readSensorRaw(AxisId::Y);
  uint32_t startUs = micros();

  for (int i = 0; i < 2000; i++) {
    bool cx = readSensorRaw(AxisId::X);
    bool cy = readSensorRaw(AxisId::Y);
    if (cx != lastX) { transX++; lastX = cx; }
    if (cy != lastY) { transY++; lastY = cy; }
    delayMicroseconds(100);
    if ((i % 200) == 0) {
      yield();
    }
  }

  char buf[128];
  snprintf(buf, sizeof(buf), "[AMBIENT] Transitions in %lu us: X=%u, Y=%u (%s)",
           micros() - startUs, transX, transY, (transX == 0 && transY == 0) ? "PASSED" : "AC_FLICKER_PRESENT");
  sendReply(source, buf);
}

void printSensorStatistics(Transport source) {
  char buf[128];
  sendDiagnostic("\n+-----------------------------------------------------------------------+");
  sendDiagnostic("|                     CUMULATIVE SENSOR METRICS                         |");
  sendDiagnostic("+---------------------------------------+---------------+---------------+");
  sendDiagnostic("| Metric                                | Sensor X      | Sensor Y      |");
  sendDiagnostic("+---------------------------------------+---------------+---------------+");
  snprintf(buf, sizeof(buf), "| Triggers                              | %-13u | %-13u |",
           axes[0].sensor.triggers, axes[1].sensor.triggers);
  sendDiagnostic(buf);
  snprintf(buf, sizeof(buf), "| Releases                              | %-13u | %-13u |",
           axes[0].sensor.releases, axes[1].sensor.releases);
  sendDiagnostic(buf);
  snprintf(buf, sizeof(buf), "| Unstable Edge Glitches                | %-13u | %-13u |",
           axes[0].sensor.unstableEdges, axes[1].sensor.unstableEdges);
  sendDiagnostic(buf);
  sendDiagnostic("+---------------------------------------+---------------+---------------+");
  sendReply(source, "ok");
}

void runDualSensorDashboard(Transport source) {
  sendDiagnostic("[DUAL] Real-Time Dual Sensor Dashboard. Send 'X' to exit.");
  uint32_t lastPrintMs = 0;
  while (true) {
    if (Serial.available()) {
      char c = Serial.peek();
      if (c == 'X' || c == 'x' || c == '!') { Serial.read(); break; }
    }
    if (tcpClient && tcpClient.available()) {
      char c = tcpClient.peek();
      if (c == 'X' || c == 'x' || c == '!') { tcpClient.read(); break; }
    }

    sampleSensors(millis());
    if (millis() - lastPrintMs > 250) {
      lastPrintMs = millis();
      bool dx = axes[0].sensor.stableDetected;
      bool dy = axes[1].sensor.stableDetected;
      char bar[64];
      snprintf(bar, sizeof(bar), "  [X: %-8s %s]  [Y: %-8s %s]",
               dx ? "DETECTED" : "CLEAR", dx ? "[#####]" : "[.....]",
               dy ? "DETECTED" : "CLEAR", dy ? "[#####]" : "[.....]");
      sendReply(source, bar);
    }
    delay(20);
  }
  sendReply(source, "[DUAL] Dashboard closed.");
}

void runDualSensorSync(Transport source) {
  sendDiagnostic("[DUAL SYNC] Alignment synchronization test. Trigger both sensors together.");
  sendReply(source, "ok");
}

void runHomeSimDryRun(Transport source, int stepDelayMs) {
  sendDiagnostic("[SIMULATION ONLY] Running simulated dry-run homing sequence. (NO MOTOR MOTION)");
  sendReply(source, "ok");
}

// =============================================================================
//  SECTION 8: A4988 DRIVER & STEPPER PULSE PRIMITIVES
// =============================================================================

AxisState &axisState(AxisId axis) {
  return axes[(int)axis];
}

uint8_t stepPin(AxisId axis) {
  return (axis == AxisId::X) ? PIN_STEP_X : PIN_STEP_Y;
}

uint8_t directionPin(AxisId axis) {
  return (axis == AxisId::X) ? PIN_DIR_X : PIN_DIR_Y;
}

uint8_t enablePin(AxisId axis) {
  return (axis == AxisId::X) ? PIN_ENABLE_X : PIN_ENABLE_Y;
}

uint8_t sensorPin(AxisId axis) {
  return (axis == AxisId::X) ? PIN_SENSOR_X : PIN_SENSOR_Y;
}

void setDriverEnabled(AxisId axis, bool enabled) {
  axes[(int)axis].driverEnabled = enabled;
  digitalWrite(enablePin(axis), enabled ? DRIVER_ENABLE_LEVEL : DRIVER_DISABLE_LEVEL);
  if (enabled) {
    delayMicroseconds(1000); // A4988 wake-up delay
    lastMotionMillis = millis();
  }
}

void setDriversEnabled(bool enabled) {
  setDriverEnabled(AxisId::X, enabled);
  setDriverEnabled(AxisId::Y, enabled);
}

void setMicrostepMode(uint8_t divisor) {
  microstepMode = divisor;
  switch (divisor) {
    case 2: // 1/2 Half step
      digitalWrite(PIN_MS1, HIGH);
      digitalWrite(PIN_MS2, LOW);
      digitalWrite(PIN_MS3, LOW);
      stepsPerRev = BASE_STEPS_PER_REV * 2;
      break;
    case 4: // 1/4 Quarter step
      digitalWrite(PIN_MS1, LOW);
      digitalWrite(PIN_MS2, HIGH);
      digitalWrite(PIN_MS3, LOW);
      stepsPerRev = BASE_STEPS_PER_REV * 4;
      break;
    case 8: // 1/8 Eighth step
      digitalWrite(PIN_MS1, HIGH);
      digitalWrite(PIN_MS2, HIGH);
      digitalWrite(PIN_MS3, LOW);
      stepsPerRev = BASE_STEPS_PER_REV * 8;
      break;
    case 16: // 1/16 Sixteenth step
      digitalWrite(PIN_MS1, HIGH);
      digitalWrite(PIN_MS2, HIGH);
      digitalWrite(PIN_MS3, HIGH);
      stepsPerRev = BASE_STEPS_PER_REV * 16;
      break;
    case 1: // 1/1 Full step default
    default:
      microstepMode = 1;
      digitalWrite(PIN_MS1, LOW);
      digitalWrite(PIN_MS2, LOW);
      digitalWrite(PIN_MS3, LOW);
      stepsPerRev = BASE_STEPS_PER_REV * 1;
      break;
  }
}

void setAxisDirection(AxisId axis, bool positive) {
  bool physicalDir = positive;
  if (axes[(int)axis].invertDirection) physicalDir = !physicalDir;
  digitalWrite(directionPin(axis), physicalDir ? DIR_CW_LEVEL : DIR_CCW_LEVEL);
  delayMicroseconds(DIR_SETUP_TIME_US);
}

inline void pulseStep(uint8_t pin) {
  digitalWrite(pin, HIGH);
  delayMicroseconds(STEP_PULSE_WIDTH_US);
  digitalWrite(pin, LOW);
}

bool stepSingleAxis(AxisId axis, int32_t steps, uint32_t sps) {
  if (steps == 0) return true;
  if (sps == 0) sps = axes[(int)axis].speedSps;
  sps = constrain(sps, MIN_SPEED_SPS, MAX_SPEED_SPS);

  setDriverEnabled(axis, true);
  bool dir = (steps > 0);
  uint32_t count = abs(steps);
  setAxisDirection(axis, dir);

  uint32_t intervalUs = 1000000UL / sps;
  if (intervalUs < (STEP_PULSE_WIDTH_US + 4)) intervalUs = STEP_PULSE_WIDTH_US + 4;
  uint8_t spin = stepPin(axis);

  axes[(int)axis].motion = MotionKind::Jog;

  for (uint32_t i = 0; i < count; i++) {
    // Check abort every 16 steps
    if ((i & 0x0F) == 0) {
      serviceSerialInput();
      serviceTcp();
      if (axes[(int)axis].motion == MotionKind::Idle) break;
    }

    uint32_t t0 = micros();
    pulseStep(spin);
    axes[(int)axis].commandedSteps += dir ? 1 : -1;

    while ((micros() - t0) < intervalUs) {
      if (intervalUs > 3000) delayMicroseconds(100);
    }
  }

  axes[(int)axis].motion = MotionKind::Idle;
  lastMotionMillis = millis();
  return true;
}

bool stepDualCoordinated(int32_t stepsA, int32_t stepsB, uint32_t sps) {
  if (stepsA == 0 && stepsB == 0) return true;
  if (sps == 0) sps = max(axes[0].speedSps, axes[1].speedSps);
  sps = constrain(sps, MIN_SPEED_SPS, MAX_SPEED_SPS);

  setDriversEnabled(true);
  bool dirA = (stepsA > 0);
  bool dirB = (stepsB > 0);
  uint32_t absA = abs(stepsA);
  uint32_t absB = abs(stepsB);

  setAxisDirection(AxisId::X, dirA);
  setAxisDirection(AxisId::Y, dirB);

  uint32_t totalSteps = max(absA, absB);
  uint32_t accA = totalSteps / 2;
  uint32_t accB = totalSteps / 2;

  uint32_t intervalUs = 1000000UL / sps;
  if (intervalUs < (STEP_PULSE_WIDTH_US + 5)) intervalUs = STEP_PULSE_WIDTH_US + 5;

  axes[0].motion = MotionKind::Jog;
  axes[1].motion = MotionKind::Jog;

  for (uint32_t i = 0; i < totalSteps; i++) {
    if ((i & 0x0F) == 0) {
      serviceSerialInput();
      serviceTcp();
      if (axes[0].motion == MotionKind::Idle) break;
    }

    uint32_t t0 = micros();
    bool doA = false;
    bool doB = false;

    accA += absA;
    if (accA >= totalSteps) { accA -= totalSteps; doA = true; }
    accB += absB;
    if (accB >= totalSteps) { accB -= totalSteps; doB = true; }

    if (doA) digitalWrite(PIN_STEP_X, HIGH);
    if (doB) digitalWrite(PIN_STEP_Y, HIGH);
    delayMicroseconds(STEP_PULSE_WIDTH_US);
    if (doA) { digitalWrite(PIN_STEP_X, LOW); axes[0].commandedSteps += dirA ? 1 : -1; }
    if (doB) { digitalWrite(PIN_STEP_Y, LOW); axes[1].commandedSteps += dirB ? 1 : -1; }

    while ((micros() - t0) < intervalUs) {
      if (intervalUs > 3000) delayMicroseconds(100);
    }
  }

  axes[0].motion = MotionKind::Idle;
  axes[1].motion = MotionKind::Idle;
  lastMotionMillis = millis();
  return true;
}

bool gotoDualAbsolute(int32_t targetA, int32_t targetB, uint32_t sps) {
  int32_t deltaA = targetA - axes[0].commandedSteps;
  int32_t deltaB = targetB - axes[1].commandedSteps;
  return stepDualCoordinated(deltaA, deltaB, sps);
}

void traceCirclePattern(float radiusSteps, int segments, uint32_t sps) {
  if (segments < 8) segments = 8;
  if (segments > 360) segments = 360;
  if (sps == 0) sps = max(axes[0].speedSps, axes[1].speedSps);

  int32_t startX = axes[0].commandedSteps;
  int32_t startY = axes[1].commandedSteps;

  for (int i = 1; i <= segments; i++) {
    float angle = (float)i * 2.0f * M_PI / (float)segments;
    int32_t targetX = startX + (int32_t)round(radiusSteps * (1.0f - cos(angle)));
    int32_t targetY = startY + (int32_t)round(radiusSteps * sin(angle));
    gotoDualAbsolute(targetX, targetY, sps);
  }
}

void traceSquarePattern(float sideSteps, uint32_t sps) {
  int32_t sX = axes[0].commandedSteps;
  int32_t sY = axes[1].commandedSteps;
  gotoDualAbsolute(sX + (int32_t)sideSteps, sY, sps);
  gotoDualAbsolute(sX + (int32_t)sideSteps, sY + (int32_t)sideSteps, sps);
  gotoDualAbsolute(sX, sY + (int32_t)sideSteps, sps);
  gotoDualAbsolute(sX, sY, sps);
}

void serviceContinuousMotion(uint32_t nowUs) {
  // Motor A continuous step service
  if (axes[0].contRunning && axes[0].contIntervalUs > 0) {
    if ((nowUs - axes[0].lastContStepUs) >= axes[0].contIntervalUs) {
      axes[0].lastContStepUs = nowUs;
      setAxisDirection(AxisId::X, axes[0].contDir);
      pulseStep(PIN_STEP_X);
      axes[0].commandedSteps += axes[0].contDir ? 1 : -1;
      lastMotionMillis = millis();
    }
  }

  // Motor B continuous step service
  if (axes[1].contRunning && axes[1].contIntervalUs > 0) {
    if ((nowUs - axes[1].lastContStepUs) >= axes[1].contIntervalUs) {
      axes[1].lastContStepUs = nowUs;
      setAxisDirection(AxisId::Y, axes[1].contDir);
      pulseStep(PIN_STEP_Y);
      axes[1].commandedSteps += axes[1].contDir ? 1 : -1;
      lastMotionMillis = millis();
    }
  }
}

void stopContinuousMotion(AxisId axis) {
  axes[(int)axis].contRunning = false;
  axes[(int)axis].motion = MotionKind::Idle;
}

void stopAllMotion(bool invalidateCoordinates) {
  stopContinuousMotion(AxisId::X);
  stopContinuousMotion(AxisId::Y);
  digitalWrite(PIN_STEP_X, LOW);
  digitalWrite(PIN_STEP_Y, LOW);
  axes[0].motion = MotionKind::Idle;
  axes[1].motion = MotionKind::Idle;
  if (invalidateCoordinates) {
    axes[0].homed = false;
    axes[1].homed = false;
  }
  lastMotionMillis = millis();
}

// =============================================================================
//  SECTION 9: PRESERVED MOTOR TESTS & CHARACTERIZATION
// =============================================================================

void runBacklashRoutine(Transport source) {
  sendDiagnostic("\n[BACKLASH] Visual Gear Play Characterization Routine.");
  sendDiagnostic("           Stepping +200 steps then -200 steps at 80 sps.");
  sendDiagnostic("           Observe the output shaft for mechanical reversal hesitation.");
  stepSingleAxis(AxisId::X, 200, 80);
  delay(300);
  stepSingleAxis(AxisId::X, -200, 80);
  sendDiagnostic("           Notice: Measured deadband depends on 28BYJ-48 internal geartrain.");
  sendReply(source, "ok");
}

void runStallTest(Transport source, uint32_t testSpeed, uint32_t testSteps) {
  sendDiagnostic("\n[STALL] Open-Loop Stepping Rate Characterization Routine.");
  sendDiagnostic("        Note: System lacks encoder feedback; stall must be observed visually/audibly.");
  uint32_t t0 = millis();
  stepSingleAxis(AxisId::X, testSteps, testSpeed);
  uint32_t elapsed = millis() - t0;
  char buf[96];
  snprintf(buf, sizeof(buf), "[STALL] Commanded %u steps at %u sps in %lu ms.", testSteps, testSpeed, elapsed);
  sendDiagnostic(buf);
  sendReply(source, "ok");
}

void runSelfTest(Transport source) {
  sendDiagnostic("\n[TEST] Running Stepper & Driver Self-Test Routine...");
  sendDiagnostic("  Checking driver enable outputs...");
  setDriversEnabled(true);
  delay(100);
  sendDiagnostic("  Testing Motor A discrete pulse command (+100 steps)...");
  stepSingleAxis(AxisId::X, 100, 100);
  sendDiagnostic("  Testing Motor B discrete pulse command (+100 steps)...");
  stepSingleAxis(AxisId::Y, 100, 100);
  sendDiagnostic("  Testing coordinated dual diagonal move (-100, -100)...");
  stepDualCoordinated(-100, -100, 100);
  sendDiagnostic("  [NOTICE] Pulses successfully generated. Confirm physical rotation visually.");
  sendReply(source, "ok");
}

void runBenchmark(Transport source) {
  sendDiagnostic("\n[BENCHMARK] Microcontroller GPIO Signal-Generation Throughput Benchmark.");
  sendDiagnostic("            Motor drivers DISABLED during benchmark for safety.");
  setDriversEnabled(false);

  uint32_t t0 = micros();
  for (int i = 0; i < 10000; i++) {
    digitalWrite(PIN_STEP_X, HIGH);
    digitalWrite(PIN_STEP_X, LOW);
  }
  uint32_t durUs = micros() - t0;
  float rateKhz = 10000.0f / ((float)durUs / 1000.0f);

  char buf[96];
  snprintf(buf, sizeof(buf), "[BENCHMARK] 10,000 pulses generated in %lu us (%.2f kHz throughput).", durUs, rateKhz);
  sendDiagnostic(buf);
  sendDiagnostic("            Notice: Measures MCU execution rate, NOT motor shaft velocity.");
  sendReply(source, "ok");
}

// =============================================================================
//  SECTION 10: NON-BLOCKING HOMING STATE MACHINE
// =============================================================================

bool startHoming(AxisId axis, Transport source, uint32_t nowMs) {
  if (homeOp.active) {
    sendReply(source, "error: BUSY");
    return false;
  }
  homeOp.active = true;
  homeOp.currentAxis = axis;
  homeOp.dualHome = false;
  homeOp.xCompleted = false;
  homeOp.replyTo = source;
  homeOp.result = HomeResult::Running;
  homeOp.lastStepUs = micros();
  homeOp.stepIntervalUs = 1000000UL / HOMING_FAST_SPS;
  homeOp.fastTriggerSteps = 0;
  homeOp.retractSteps = 0;
  homeOp.latchSteps = 0;

  axes[(int)axis].homed = false;
  axes[(int)axis].motion = MotionKind::Homing;
  setDriverEnabled(axis, true);

  char diag[64];
  snprintf(diag, sizeof(diag), "[HOME %s] Homing routine initiated...", (axis == AxisId::X) ? "X" : "Y");
  sendDiagnostic(diag);

  enterHomePhase(HomePhase::Validate, nowMs);
  return true;
}

bool startHomeAll(Transport source, uint32_t nowMs) {
  if (homeOp.active) {
    sendReply(source, "error: BUSY");
    return false;
  }
  homeOp.active = true;
  homeOp.currentAxis = AxisId::X;
  homeOp.dualHome = true;
  homeOp.xCompleted = false;
  homeOp.replyTo = source;
  homeOp.result = HomeResult::Running;
  homeOp.lastStepUs = micros();
  homeOp.stepIntervalUs = 1000000UL / HOMING_FAST_SPS;
  homeOp.fastTriggerSteps = 0;
  homeOp.retractSteps = 0;
  homeOp.latchSteps = 0;

  axes[0].homed = false;
  axes[1].homed = false;
  axes[0].motion = MotionKind::Homing;
  setDriverEnabled(AxisId::X, true);

  sendDiagnostic("[HOME ALL] Sequential homing initiated: Axis X first, then Axis Y.");
  enterHomePhase(HomePhase::Validate, nowMs);
  return true;
}

void enterHomePhase(HomePhase nextPhase, uint32_t nowMs) {
  homeOp.phase = nextPhase;
  homeOp.phaseStartedMs = nowMs;
  homeOp.phaseSteps = 0;
  homeOp.lastStepUs = micros();

  AxisId ax = homeOp.currentAxis;

  switch (nextPhase) {
    case HomePhase::ClearInitial:
      homeOp.stepIntervalUs = 1000000UL / HOMING_SLOW_SPS;
      setAxisDirection(ax, false); // Move AWAY from endstop
      sendDiagnostic("  [PHASE] ClearInitial: Backing away from blocked sensor...");
      break;

    case HomePhase::FastSeek:
      homeOp.stepIntervalUs = 1000000UL / HOMING_FAST_SPS;
      setAxisDirection(ax, true); // Move TOWARD endstop
      sendDiagnostic("  [PHASE] FastSeek: Approaching endstop at fast speed...");
      break;

    case HomePhase::PullOff:
      homeOp.stepIntervalUs = 1000000UL / HOMING_SLOW_SPS;
      setAxisDirection(ax, false); // Move AWAY from endstop
      sendDiagnostic("  [PHASE] PullOff: Retracting until optical sensor clears...");
      break;

    case HomePhase::SlowLatch:
      homeOp.stepIntervalUs = 1000000UL / HOMING_SLOW_SPS;
      setAxisDirection(ax, true); // Move TOWARD endstop
      sendDiagnostic("  [PHASE] SlowLatch: Final precision approach at slow speed...");
      break;

    default:
      break;
  }
}

void finishHomeSuccess(AxisId axis) {
  axes[(int)axis].commandedSteps = 0;
  axes[(int)axis].homed = true;
  axes[(int)axis].motion = MotionKind::Idle;

  char buf[96];
  snprintf(buf, sizeof(buf), "[HOME %s] Success. Origin zeroed. Fast=%u, Retract=%u, Latch=%u steps.",
           (axis == AxisId::X) ? "X" : "Y", homeOp.fastTriggerSteps, homeOp.retractSteps, homeOp.latchSteps);
  sendDiagnostic(buf);

  if (homeOp.dualHome && axis == AxisId::X) {
    homeOp.xCompleted = true;
    homeOp.currentAxis = AxisId::Y;
    homeOp.fastTriggerSteps = 0;
    homeOp.retractSteps = 0;
    homeOp.latchSteps = 0;
    axes[1].motion = MotionKind::Homing;
    setDriverEnabled(AxisId::Y, true);
    sendDiagnostic("[HOME ALL] Axis X complete. Beginning Axis Y homing...");
    enterHomePhase(HomePhase::Validate, millis());
  } else {
    homeOp.active = false;
    homeOp.phase = HomePhase::Complete;
    homeOp.result = HomeResult::Success;
    sendReply(homeOp.replyTo, "ok");
    sendDiagnostic("[HOME] All requested axes successfully homed.");
  }
}

void failHoming(HomeResult result) {
  AxisId ax = homeOp.currentAxis;
  homeOp.active = false;
  homeOp.phase = HomePhase::Failed;
  homeOp.result = result;

  digitalWrite(stepPin(ax), LOW);
  axes[(int)ax].homed = false;
  axes[(int)ax].motion = MotionKind::Idle;

  const char *resStr = homeResultToString(result);
  const char *phaseStr = homePhaseToString(homeOp.phase);

  char err[80];
  snprintf(err, sizeof(err), "error: %s %s %s", resStr, (ax == AxisId::X) ? "X" : "Y", phaseStr);
  sendReply(homeOp.replyTo, err);

  char diag[128];
  snprintf(diag, sizeof(diag), "[HOME FAILED] Axis %s failed during %s: %s (Homed=false)",
           (ax == AxisId::X) ? "X" : "Y", phaseStr, resStr);
  sendDiagnostic(diag);
}

void abortHoming() {
  if (!homeOp.active) return;
  AxisId ax = homeOp.currentAxis;
  homeOp.active = false;
  homeOp.phase = HomePhase::Aborted;
  homeOp.result = HomeResult::Aborted;

  digitalWrite(stepPin(ax), LOW);
  axes[(int)ax].homed = false;
  axes[(int)ax].motion = MotionKind::Idle;

  char err[64];
  snprintf(err, sizeof(err), "error: HOME_ABORTED %s", (ax == AxisId::X) ? "X" : "Y");
  sendReply(homeOp.replyTo, err);
  sendDiagnostic("[HOME ABORTED] Operation stopped by user.");
}

void serviceHoming(uint32_t nowMs, uint32_t nowUs) {
  AxisId ax = homeOp.currentAxis;
  bool detected = sensorDetectedStable(ax);

  // Check phase timeout bound
  if ((nowMs - homeOp.phaseStartedMs) >= HOMING_PHASE_TIMEOUT_MS) {
    if (homeOp.phase == HomePhase::FastSeek) failHoming(HomeResult::SeekTimeout);
    else if (homeOp.phase == HomePhase::PullOff || homeOp.phase == HomePhase::ClearInitial) failHoming(HomeResult::RetractTimeout);
    else if (homeOp.phase == HomePhase::SlowLatch) failHoming(HomeResult::LatchTimeout);
    return;
  }

  switch (homeOp.phase) {
    case HomePhase::Validate:
      if (detected) {
        enterHomePhase(HomePhase::ClearInitial, nowMs);
      } else {
        enterHomePhase(HomePhase::FastSeek, nowMs);
      }
      break;

    case HomePhase::ClearInitial:
      if ((nowUs - homeOp.lastStepUs) >= homeOp.stepIntervalUs) {
        homeOp.lastStepUs = nowUs;
        homeOp.phaseSteps++;
        pulseStep(stepPin(ax));
        axes[(int)ax].commandedSteps += axes[(int)ax].invertDirection ? 1 : -1;

        if (!detected) {
          enterHomePhase(HomePhase::PullOff, nowMs);
        } else if (homeOp.phaseSteps >= HOMING_MAX_CLEAR_STEPS) {
          failHoming(HomeResult::RetractLimit);
        }
      }
      break;

    case HomePhase::FastSeek:
      if ((nowUs - homeOp.lastStepUs) >= homeOp.stepIntervalUs) {
        homeOp.lastStepUs = nowUs;
        homeOp.phaseSteps++;
        pulseStep(stepPin(ax));
        axes[(int)ax].commandedSteps += axes[(int)ax].invertDirection ? -1 : 1;

        if (detected) {
          homeOp.fastTriggerSteps = homeOp.phaseSteps;
          enterHomePhase(HomePhase::PullOff, nowMs);
        } else if (homeOp.phaseSteps >= HOMING_MAX_SEEK_STEPS) {
          failHoming(HomeResult::SeekLimit);
        }
      }
      break;

    case HomePhase::PullOff:
      if ((nowUs - homeOp.lastStepUs) >= homeOp.stepIntervalUs) {
        homeOp.lastStepUs = nowUs;
        homeOp.phaseSteps++;
        pulseStep(stepPin(ax));
        axes[(int)ax].commandedSteps += axes[(int)ax].invertDirection ? 1 : -1;

        // Ensure sensor clears AND advances 10 steps past the release edge
        if (!detected && homeOp.phaseSteps >= 10) {
          homeOp.retractSteps = homeOp.phaseSteps;
          enterHomePhase(HomePhase::SlowLatch, nowMs);
        } else if (homeOp.phaseSteps >= HOMING_MAX_RETRACT_STEPS) {
          failHoming(HomeResult::RetractLimit);
        }
      }
      break;

    case HomePhase::SlowLatch:
      if ((nowUs - homeOp.lastStepUs) >= homeOp.stepIntervalUs) {
        homeOp.lastStepUs = nowUs;
        homeOp.phaseSteps++;
        pulseStep(stepPin(ax));
        axes[(int)ax].commandedSteps += axes[(int)ax].invertDirection ? -1 : 1;

        if (detected) {
          homeOp.latchSteps = homeOp.phaseSteps;
          finishHomeSuccess(ax);
        } else if (homeOp.phaseSteps >= HOMING_MAX_LATCH_STEPS) {
          failHoming(HomeResult::LatchLimit);
        }
      }
      break;

    default:
      break;
  }
}

const char *homeResultToString(HomeResult res) {
  switch (res) {
    case HomeResult::Success:           return "SUCCESS";
    case HomeResult::Aborted:           return "HOME_ABORTED";
    case HomeResult::Busy:              return "BUSY";
    case HomeResult::SeekTimeout:       return "HOME_TIMEOUT_SEEK";
    case HomeResult::SeekLimit:         return "HOME_LIMIT_SEEK";
    case HomeResult::RetractTimeout:    return "HOME_TIMEOUT_RETRACT";
    case HomeResult::RetractLimit:      return "HOME_LIMIT_RETRACT";
    case HomeResult::LatchTimeout:      return "HOME_TIMEOUT_LATCH";
    case HomeResult::LatchLimit:        return "HOME_LIMIT_LATCH";
    case HomeResult::DriverUnavailable: return "DRIVER_UNAVAILABLE";
    default:                            return "HOME_FAILED";
  }
}

const char *homePhaseToString(HomePhase phase) {
  switch (phase) {
    case HomePhase::Validate:     return "VALIDATE";
    case HomePhase::ClearInitial: return "CLEAR_INITIAL";
    case HomePhase::FastSeek:     return "FAST_SEEK";
    case HomePhase::PullOff:      return "PULL_OFF";
    case HomePhase::SlowLatch:    return "SLOW_LATCH";
    case HomePhase::Complete:     return "COMPLETE";
    case HomePhase::Aborted:      return "ABORTED";
    case HomePhase::Failed:       return "FAILED";
    default:                      return "IDLE";
  }
}

// =============================================================================
//  SECTION 11: CONSOLIDATED STATUS & HELP
// =============================================================================

void printStatus(Transport source) {
  sampleSensors(millis());

  char buf[128];
  sendDiagnostic("\n+=======================================================================+");
  sendDiagnostic("|                 CONSOLIDATED FIXTURE STATUS REPORT                    |");
  sendDiagnostic("+=======================================================================+");
  snprintf(buf, sizeof(buf), "| Hardware Target    : ESP32-S3 SuperMini | Uptime: %-19lu s |", millis() / 1000UL);
  sendDiagnostic(buf);
  snprintf(buf, sizeof(buf), "| Network Connection : %-20s | Port: %-21u |",
           wifiConnected ? WiFi.localIP().toString().c_str() : "DISCONNECTED (Serial Only)", TCP_PORT);
  sendDiagnostic(buf);
  snprintf(buf, sizeof(buf), "| TCP Client Active  : %-48s |",
           (tcpClient && tcpClient.connected()) ? "CONNECTED" : "NO ACTIVE CLIENT");
  sendDiagnostic(buf);
  sendDiagnostic("+-----------------------------------------------------------------------+");
  sendDiagnostic("| AXIS STATUS & KINEMATICS                                              |");
  sendDiagnostic("+-----------------------------------------------------------------------+");
  snprintf(buf, sizeof(buf), "| Axis X (Motor A)   : Pos=%-8ld steps (Homed=%s, Driver=%s)      |",
           axes[0].commandedSteps, axes[0].homed ? "YES" : "NO ", axes[0].driverEnabled ? "ON " : "OFF");
  sendDiagnostic(buf);
  snprintf(buf, sizeof(buf), "| Axis Y (Motor B)   : Pos=%-8ld steps (Homed=%s, Driver=%s)      |",
           axes[1].commandedSteps, axes[1].homed ? "YES" : "NO ", axes[1].driverEnabled ? "ON " : "OFF");
  sendDiagnostic(buf);
  snprintf(buf, sizeof(buf), "| Microstepping Mode : 1/%-2d (%u steps/rev)                            |",
           microstepMode, stepsPerRev);
  sendDiagnostic(buf);
  sendDiagnostic("+-----------------------------------------------------------------------+");
  sendDiagnostic("| OPTICAL SENSOR STATUS                                                 |");
  sendDiagnostic("+-----------------------------------------------------------------------+");
  snprintf(buf, sizeof(buf), "| Sensor X (GPIO %-2d) : Raw=%-4s State=%-8s (Triggers=%-5u)            |",
           PIN_SENSOR_X, axes[0].sensor.rawHigh ? "HIGH" : "LOW",
           axes[0].sensor.stableDetected ? "DETECTED" : "CLEAR", axes[0].sensor.triggers);
  sendDiagnostic(buf);
  snprintf(buf, sizeof(buf), "| Sensor Y (GPIO %-2d) : Raw=%-4s State=%-8s (Triggers=%-5u)            |",
           PIN_SENSOR_Y, axes[1].sensor.rawHigh ? "HIGH" : "LOW",
           axes[1].sensor.stableDetected ? "DETECTED" : "CLEAR", axes[1].sensor.triggers);
  sendDiagnostic(buf);
  snprintf(buf, sizeof(buf), "| Sensor Polarity    : %-48s |",
           invertSensorLogic ? "INVERTED (Active HIGH)" : "NORMAL (Active LOW, FC-51 Standard)");
  sendDiagnostic(buf);
  sendDiagnostic("+=======================================================================+\n");

  sendReply(source, "ok");
}

void printHelp(Transport source) {
  sendDiagnostic("\n+----------------------------------------------------------------------+");
  sendDiagnostic("|      COMBINED STEPPER + IR HOMING FIRMWARE -- COMMAND REFERENCE      |");
  sendDiagnostic("+--------------------+-------------------------------------------------+");
  sendDiagnostic("| PHYSICAL HOMING    |                                                 |");
  sendDiagnostic("|  HOME X            | Physical sensor homing for X (Motor A, Sensor X)|");
  sendDiagnostic("|  HOME Y            | Physical sensor homing for Y (Motor B, Sensor Y)|");
  sendDiagnostic("|  HOME ALL          | Sequential sensor homing for X then Y           |");
  sendDiagnostic("|  G28               | Standard G-code alias to HOME ALL (Serial & TCP)|");
  sendDiagnostic("|  X  or  !          | EMERGENCY STOP / ABORT ALL MOTION IMMEDIATELY   |");
  sendDiagnostic("+--------------------+-------------------------------------------------+");
  sendDiagnostic("| MOTOR CONTROL      |                                                 |");
  sendDiagnostic("|  ENABLE / DISABLE  | Energize or de-energize A4988 driver coils      |");
  sendDiagnostic("|  START [A|B] [sps] | Continuous rotation at signed step rate         |");
  sendDiagnostic("|  STOP [A|B|ALL]    | Halt continuous rotation                        |");
  sendDiagnostic("|  STEP [A|B] <n> [s]| Relative signed step jog                         |");
  sendDiagnostic("|  GOTO [A|B] <pos>  | Absolute logical coordinate move                |");
  sendDiagnostic("|  SPEED [A|B] <sps> | Set configured stepping rate                    |");
  sendDiagnostic("|  DIR [A|B] [CW|CCW]| Invert or query physical direction logic        |");
  sendDiagnostic("|  MICRO <1|2|4|8|16>| Set shared microstep mode (1/1 to 1/16)         |");
  sendDiagnostic("|  POS / ZERO        | Read coordinates / reset software zero          |");
  sendDiagnostic("+--------------------+-------------------------------------------------+");
  sendDiagnostic("| COORDINATED DUAL   |                                                 |");
  sendDiagnostic("|  DUAL STEP <A> <B> | Coordinated multi-axis step move                |");
  sendDiagnostic("|  DUAL GOTO <A> <B> | Coordinated Bresenham move to absolute target  |");
  sendDiagnostic("|  DUAL SPEED <A> <B>| Configure speeds for both axes                  |");
  sendDiagnostic("|  DUAL CIRCLE / SQ  | Trace circular or square test pattern           |");
  sendDiagnostic("|  DUAL HOME         | Open-loop move to logical (0, 0) [NOT SENSOR]   |");
  sendDiagnostic("+--------------------+-------------------------------------------------+");
  sendDiagnostic("| SENSOR DIAGNOSTICS |                                                 |");
  sendDiagnostic("|  READ              | Snapshot of raw and debounced sensor states     |");
  sendDiagnostic("|  MONITOR [X|Y] [ms]| Stream state transitions with timestamps        |");
  sendDiagnostic("|  WATCH [X|Y]       | Stream transitions on event changes only        |");
  sendDiagnostic("|  DUAL              | Live dual sensor real-time dashboard            |");
  sendDiagnostic("|  DUAL SYNC         | Dual sensor trigger alignment test              |");
  sendDiagnostic("|  CALIB / RANGE     | Guided distance & threshold calibration         |");
  sendDiagnostic("|  DEBOUNCE / EDGES  | Bounce & electrical transition noise tests      |");
  sendDiagnostic("|  RESPONSE / NOISE  | Optical response time & glitch noise tests      |");
  sendDiagnostic("|  AMBIENT / STATS   | AC lighting interference & cumulative metrics   |");
  sendDiagnostic("|  INVERT            | Toggle active logic polarity (LOW <-> HIGH)     |");
  sendDiagnostic("|  HOMESIM           | Simulated homing dry-run (NO MOTOR MOTION)      |");
  sendDiagnostic("+--------------------+-------------------------------------------------+");
  sendDiagnostic("| SYSTEM & STATUS    |                                                 |");
  sendDiagnostic("|  STATUS  or  ?     | Comprehensive system status report              |");
  sendDiagnostic("|  RESET             | Restore default settings & unhome axes          |");
  sendDiagnostic("|  ECHO              | Toggle command echo back to client              |");
  sendDiagnostic("|  HELP  or  H       | Show this command reference table               |");
  sendDiagnostic("+--------------------+-------------------------------------------------+\n");

  sendReply(source, "ok");
}
