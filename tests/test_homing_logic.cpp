// =============================================================================
//  tests/test_homing_logic.cpp
//  Native Unit Test Suite for Combined Stepper + IR Homing Test Logic
//  Tests: Command Parser, Prefix Resolution, Sensor Debounce Filter,
//         Homing State Machine Transitions, Kinematics Math, & Error Protocol.
// =============================================================================

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ── Mock Arduino Environment ─────────────────────────────────────────────────
static uint32_t mockMillisVal = 0;
static uint32_t mockMicrosVal = 0;
static uint8_t  mockPinModes[40];
static uint8_t  mockPinOutputs[40];
static uint8_t  mockPinInputs[40];

inline uint32_t millis() { return mockMillisVal; }
inline uint32_t micros() { return mockMicrosVal; }
inline void pinMode(uint8_t pin, uint8_t mode) { if (pin < 40) mockPinModes[pin] = mode; }
inline void digitalWrite(uint8_t pin, uint8_t val) { if (pin < 40) mockPinOutputs[pin] = val; }
inline int  digitalRead(uint8_t pin) { return (pin < 40) ? mockPinInputs[pin] : 1; }
inline void delayMicroseconds(uint32_t us) { mockMicrosVal += us; }

#define LOW 0
#define HIGH 1
#define INPUT_PULLUP 2
#define OUTPUT 1

// ── Test Constants & Data Structures ─────────────────────────────────────────
constexpr uint8_t PIN_STEP_X    = 5;
constexpr uint8_t PIN_DIR_X     = 6;
constexpr uint8_t PIN_ENABLE_X  = 7;
constexpr uint8_t PIN_STEP_Y    = 9;
constexpr uint8_t PIN_DIR_Y     = 10;
constexpr uint8_t PIN_ENABLE_Y  = 11;
constexpr uint8_t PIN_MS1       = 1;
constexpr uint8_t PIN_MS2       = 2;
constexpr uint8_t PIN_MS3       = 4;
constexpr uint8_t PIN_SENSOR_X  = 13;
constexpr uint8_t PIN_SENSOR_Y  = 8;

constexpr size_t COMMAND_BUF_SIZE = 96;
constexpr uint8_t HOME_STABLE_SAMPLES = 3;
constexpr uint32_t HOME_DEBOUNCE_MS = 3;
constexpr uint32_t HOMING_FAST_SPS = 120;
constexpr uint32_t HOMING_SLOW_SPS = 40;
constexpr uint32_t HOMING_MAX_SEEK_STEPS = 4096UL;
constexpr uint32_t HOMING_MAX_RETRACT_STEPS = 1024UL;
constexpr uint32_t HOMING_MAX_LATCH_STEPS = 1024UL;
constexpr uint32_t HOMING_MAX_CLEAR_STEPS = 1024UL;
constexpr uint32_t HOMING_PHASE_TIMEOUT_MS = 15000UL;

enum class AxisId : uint8_t { X = 0, Y = 1 };
enum class Transport : uint8_t { Serial = 0, Tcp = 1, None = 2 };
enum class MotionKind : uint8_t { Idle = 0, Jog, Continuous, Pattern, Homing };
enum class HomePhase : uint8_t {
  Idle = 0, Validate, ClearInitial, FastSeek, PullOff, SlowLatch, Complete, Aborted, Failed
};
enum class HomeResult : uint8_t {
  None = 0, Running, Success, Aborted, Busy, InvalidSensor,
  SeekTimeout, SeekLimit, RetractTimeout, RetractLimit,
  LatchTimeout, LatchLimit, DriverUnavailable
};

struct SensorFilter {
  bool     rawHigh;
  bool     stableDetected;
  bool     candidateDetected;
  uint8_t  sameSamples;
  uint32_t candidateSinceMs;
  uint32_t triggers;
  uint32_t releases;
  uint32_t unstableEdges;
  uint32_t lastTransitionMs;
  uint32_t minDurationMs;
  uint32_t maxDurationMs;
  uint64_t totalDurationMs;
  uint32_t completedCycles;
};

struct ParsedCommand {
  char      raw[COMMAND_BUF_SIZE];
  char      verb[24];
  char      arg1[24];
  char      arg2[24];
  char      arg3[24];
  int       numArgs;
  Transport source;
  bool      tooLong;
};

// ── Captured Output Helper for Verification ──────────────────────────────────
static std::string lastSerialReply;
static std::string lastTcpReply;
static std::string lastDiagnostic;

void clearCapturedReplies() {
  lastSerialReply.clear();
  lastTcpReply.clear();
  lastDiagnostic.clear();
}

void sendReply(Transport dest, const char *line) {
  if (dest == Transport::Serial) {
    lastSerialReply = line;
  } else if (dest == Transport::Tcp) {
    lastTcpReply = line;
  }
}

void sendDiagnostic(const char *line) {
  lastDiagnostic += line;
  lastDiagnostic += "\n";
}

// ── Parser Implementation Under Test ─────────────────────────────────────────
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

  // Trim whitespace / CR / LF
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

  // Tokenize into verb and arguments (up to 3)
  char temp[COMMAND_BUF_SIZE];
  strcpy(temp, out.raw);

  char *saveptr = nullptr;
  char *tok = strtok_r(temp, " \t", &saveptr);
  if (!tok) return false;
  copyTokenBounded(out.verb, sizeof(out.verb), tok);

  // Arg 1
  tok = strtok_r(nullptr, " \t", &saveptr);
  if (tok) {
    copyTokenBounded(out.arg1, sizeof(out.arg1), tok);
    out.numArgs++;

    // Arg 2
    tok = strtok_r(nullptr, " \t", &saveptr);
    if (tok) {
      copyTokenBounded(out.arg2, sizeof(out.arg2), tok);
      out.numArgs++;

      // Arg 3
      tok = strtok_r(nullptr, " \t", &saveptr);
      if (tok) {
        copyTokenBounded(out.arg3, sizeof(out.arg3), tok);
        out.numArgs++;
      }
    }
  }

  return true;
}

// ── Sensor Filter Implementation Under Test ──────────────────────────────────
void updateSensorFilter(SensorFilter &sf, bool rawHigh, bool invertLogic, uint32_t nowMs) {
  sf.rawHigh = rawHigh;
  bool detectedRaw = invertLogic ? rawHigh : !rawHigh;

  if (detectedRaw == sf.candidateDetected) {
    if (sf.sameSamples < 255) sf.sameSamples++;
  } else {
    sf.candidateDetected = detectedRaw;
    sf.sameSamples = 1;
    sf.candidateSinceMs = nowMs;
    sf.unstableEdges++;
  }

  // Stable transition check
  if (sf.candidateDetected != sf.stableDetected) {
    if (sf.sameSamples >= HOME_STABLE_SAMPLES && (nowMs - sf.candidateSinceMs) >= HOME_DEBOUNCE_MS) {
      sf.stableDetected = sf.candidateDetected;
      sf.lastTransitionMs = nowMs;
      if (sf.stableDetected) {
        sf.triggers++;
      } else {
        sf.releases++;
        if (sf.lastTransitionMs > 0 && sf.lastTransitionMs >= sf.candidateSinceMs) {
          uint32_t dur = nowMs - sf.candidateSinceMs;
          if (dur < sf.minDurationMs) sf.minDurationMs = dur;
          if (dur > sf.maxDurationMs) sf.maxDurationMs = dur;
          sf.totalDurationMs += dur;
          sf.completedCycles++;
        }
      }
    }
  }
}

// ── Homing State Machine Logic Under Test ─────────────────────────────────────
struct MockHomingEngine {
  bool        active = false;
  AxisId      currentAxis = AxisId::X;
  bool        dualHome = false;
  bool        xCompleted = false;
  HomePhase   phase = HomePhase::Idle;
  HomeResult  result = HomeResult::None;
  Transport   replyTo = Transport::Serial;
  uint32_t    phaseStartedMs = 0;
  uint32_t    phaseSteps = 0;
  uint32_t    fastTriggerSteps = 0;
  uint32_t    retractSteps = 0;
  uint32_t    latchSteps = 0;

  bool        axisHomed[2] = {false, false};
  int32_t     axisPos[2]   = {1000, 2000};
  bool        driverEn[2]  = {false, false};

  void enterPhase(HomePhase next, uint32_t nowMs) {
    phase = next;
    phaseStartedMs = nowMs;
    phaseSteps = 0;
  }

  bool startHoming(AxisId axis, Transport source, uint32_t nowMs) {
    if (active) {
      sendReply(source, "error: BUSY");
      return false;
    }
    active = true;
    currentAxis = axis;
    dualHome = false;
    xCompleted = false;
    replyTo = source;
    result = HomeResult::Running;
    axisHomed[(int)axis] = false;
    driverEn[(int)axis] = true;
    enterPhase(HomePhase::Validate, nowMs);
    return true;
  }

  bool startHomeAll(Transport source, uint32_t nowMs) {
    if (active) {
      sendReply(source, "error: BUSY");
      return false;
    }
    active = true;
    currentAxis = AxisId::X;
    dualHome = true;
    xCompleted = false;
    replyTo = source;
    result = HomeResult::Running;
    axisHomed[0] = false;
    axisHomed[1] = false;
    driverEn[0] = true;
    enterPhase(HomePhase::Validate, nowMs);
    return true;
  }

  void abort(uint32_t /*nowMs*/) {
    if (!active) return;
    phase = HomePhase::Aborted;
    result = HomeResult::Aborted;
    axisHomed[(int)currentAxis] = false;
    active = false;
    char err[64];
    snprintf(err, sizeof(err), "error: HOME_ABORTED %s", (currentAxis == AxisId::X) ? "X" : "Y");
    sendReply(replyTo, err);
  }

  void fail(HomeResult why) {
    phase = HomePhase::Failed;
    result = why;
    axisHomed[(int)currentAxis] = false;
    active = false;
    const char *code = "UNKNOWN";
    if (why == HomeResult::SeekTimeout) code = "SEEK_TIMEOUT";
    else if (why == HomeResult::SeekLimit) code = "SEEK_LIMIT";
    else if (why == HomeResult::RetractTimeout) code = "RETRACT_TIMEOUT";
    else if (why == HomeResult::RetractLimit) code = "RETRACT_LIMIT";
    else if (why == HomeResult::LatchTimeout) code = "LATCH_TIMEOUT";
    else if (why == HomeResult::LatchLimit) code = "LATCH_LIMIT";

    char err[64];
    snprintf(err, sizeof(err), "error: HOME_TIMEOUT %s %s", (currentAxis == AxisId::X) ? "X" : "Y", code);
    sendReply(replyTo, err);
  }

  void advanceStep(bool sensorDetected, uint32_t nowMs) {
    if (!active) return;

    if (nowMs - phaseStartedMs >= HOMING_PHASE_TIMEOUT_MS) {
      if (phase == HomePhase::FastSeek) fail(HomeResult::SeekTimeout);
      else if (phase == HomePhase::PullOff || phase == HomePhase::ClearInitial) fail(HomeResult::RetractTimeout);
      else if (phase == HomePhase::SlowLatch) fail(HomeResult::LatchTimeout);
      return;
    }

    switch (phase) {
      case HomePhase::Validate:
        if (sensorDetected) {
          enterPhase(HomePhase::ClearInitial, nowMs);
        } else {
          enterPhase(HomePhase::FastSeek, nowMs);
        }
        break;

      case HomePhase::ClearInitial:
        phaseSteps++;
        if (!sensorDetected) {
          enterPhase(HomePhase::PullOff, nowMs);
        } else if (phaseSteps >= HOMING_MAX_CLEAR_STEPS) {
          fail(HomeResult::RetractLimit);
        }
        break;

      case HomePhase::FastSeek:
        phaseSteps++;
        if (sensorDetected) {
          fastTriggerSteps = phaseSteps;
          enterPhase(HomePhase::PullOff, nowMs);
        } else if (phaseSteps >= HOMING_MAX_SEEK_STEPS) {
          fail(HomeResult::SeekLimit);
        }
        break;

      case HomePhase::PullOff:
        phaseSteps++;
        if (!sensorDetected && phaseSteps >= 10) { // 10 steps safety clear margin
          retractSteps = phaseSteps;
          enterPhase(HomePhase::SlowLatch, nowMs);
        } else if (phaseSteps >= HOMING_MAX_RETRACT_STEPS) {
          fail(HomeResult::RetractLimit);
        }
        break;

      case HomePhase::SlowLatch:
        phaseSteps++;
        if (sensorDetected) {
          latchSteps = phaseSteps;
          // Completed for currentAxis
          axisPos[(int)currentAxis] = 0;
          axisHomed[(int)currentAxis] = true;

          if (dualHome && currentAxis == AxisId::X) {
            xCompleted = true;
            currentAxis = AxisId::Y;
            fastTriggerSteps = 0;
            retractSteps = 0;
            latchSteps = 0;
            driverEn[1] = true;
            enterPhase(HomePhase::Validate, nowMs);
          } else {
            phase = HomePhase::Complete;
            result = HomeResult::Success;
            active = false;
            sendReply(replyTo, "ok");
          }
        } else if (phaseSteps >= HOMING_MAX_LATCH_STEPS) {
          fail(HomeResult::LatchLimit);
        }
        break;

      default:
        break;
    }
  }
};

// =============================================================================
//  TEST RUNNER & ASSERTIONS
// =============================================================================
static int testsRun = 0;
static int testsPassed = 0;

#define TEST_ASSERT(cond, msg) do { \
  testsRun++; \
  if (!(cond)) { \
    printf("  [FAIL] Line %d: %s\n", __LINE__, msg); \
  } else { \
    testsPassed++; \
  } \
} while (0)

void testParser() {
  printf("Running Test Suite 1: Command Parser & Prefix Collisions...\n");
  ParsedCommand cmd;

  // T1.1: Basic verbs in various cases
  TEST_ASSERT(parseCommandLine("home x", Transport::Serial, cmd) && strcmp(cmd.verb, "HOME") == 0 && strcmp(cmd.arg1, "X") == 0, "Parse 'home x'");
  TEST_ASSERT(parseCommandLine("G28\r\n", Transport::Tcp, cmd) && strcmp(cmd.verb, "G28") == 0, "Parse 'G28'");
  TEST_ASSERT(parseCommandLine("status", Transport::Serial, cmd) && strcmp(cmd.verb, "STATUS") == 0, "Parse 'status'");
  TEST_ASSERT(parseCommandLine("X", Transport::Tcp, cmd) && strcmp(cmd.verb, "X") == 0, "Parse 'X'");
  TEST_ASSERT(parseCommandLine("!", Transport::Serial, cmd) && strcmp(cmd.verb, "!") == 0, "Parse '!'");

  // T1.2: Multi-argument commands
  TEST_ASSERT(parseCommandLine("DUAL STEP -500 1000 200", Transport::Serial, cmd), "Parse DUAL STEP");
  TEST_ASSERT(strcmp(cmd.verb, "DUAL") == 0 && strcmp(cmd.arg1, "STEP") == 0 && strcmp(cmd.arg2, "-500") == 0 && strcmp(cmd.arg3, "1000") == 0, "Check DUAL STEP args");

  // T1.3: Buffer bounds (overlong rejection)
  std::string overlong(COMMAND_BUF_SIZE + 10, 'A');
  TEST_ASSERT(!parseCommandLine(overlong.c_str(), Transport::Serial, cmd) && cmd.tooLong, "Reject overlong line");

  // T1.4: Prefix collisions
  // Case A: DUAL with no args -> sensor dashboard
  TEST_ASSERT(parseCommandLine("DUAL", Transport::Serial, cmd) && cmd.numArgs == 0, "Parse DUAL alone");
  // Case B: DUAL SYNC -> sensor sync
  TEST_ASSERT(parseCommandLine("DUAL SYNC", Transport::Serial, cmd) && strcmp(cmd.arg1, "SYNC") == 0, "Parse DUAL SYNC");
  // Case C: DUAL HOME -> legacy open-loop return
  TEST_ASSERT(parseCommandLine("DUAL HOME", Transport::Serial, cmd) && strcmp(cmd.arg1, "HOME") == 0, "Parse DUAL HOME legacy");
  // Case D: HOME ALL -> physical sensor homing
  TEST_ASSERT(parseCommandLine("HOME ALL", Transport::Serial, cmd) && strcmp(cmd.verb, "HOME") == 0 && strcmp(cmd.arg1, "ALL") == 0, "Parse HOME ALL");

  // T1.5: Token bounds (bounded copy without buffer overflow)
  TEST_ASSERT(parseCommandLine("TEST A_VERY_LONG_ARGUMENT_THAT_EXCEEDS_CAPACITY_BOUNDS", Transport::Serial, cmd), "Parse long token");
  TEST_ASSERT(strlen(cmd.arg1) == sizeof(cmd.arg1) - 1, "Long token bounded to buffer size - 1");
  TEST_ASSERT(cmd.arg1[sizeof(cmd.arg1) - 1] == '\0', "Long token properly null-terminated");
}

void testSensorDebounce() {
  printf("Running Test Suite 2: Sensor Debounce & Filtering...\n");
  SensorFilter sf{};
  sf.minDurationMs = 0xFFFFFFFF;

  // Active LOW default (raw LOW = DETECTED, raw HIGH = CLEAR)
  bool invertLogic = false;

  // Initial state: RAW HIGH (CLEAR)
  updateSensorFilter(sf, true, invertLogic, 100);
  TEST_ASSERT(!sf.stableDetected, "Initial state should be CLEAR");

  // Glitch: 1 sample of raw LOW for 1 ms -> should NOT trigger
  updateSensorFilter(sf, false, invertLogic, 101);
  TEST_ASSERT(!sf.stableDetected, "1 sample glitch should not trigger");

  // Release back to HIGH
  updateSensorFilter(sf, true, invertLogic, 102);
  TEST_ASSERT(!sf.stableDetected, "Glitch released, still CLEAR");

  // Sustained transition: 3 samples spanning >= 3 ms
  updateSensorFilter(sf, false, invertLogic, 110);
  TEST_ASSERT(!sf.stableDetected, "Sustained sample 1 -> still not stable");
  updateSensorFilter(sf, false, invertLogic, 111);
  TEST_ASSERT(!sf.stableDetected, "Sustained sample 2 -> still not stable");
  updateSensorFilter(sf, false, invertLogic, 114);
  TEST_ASSERT(sf.stableDetected, "Sustained sample 3 (dt=4ms) -> STABLE TRIGGERED");
  TEST_ASSERT(sf.triggers == 1, "Trigger count should be 1");

  // Sustained release: 3 samples spanning >= 3 ms
  updateSensorFilter(sf, true, invertLogic, 160);
  updateSensorFilter(sf, true, invertLogic, 161);
  updateSensorFilter(sf, true, invertLogic, 164);
  TEST_ASSERT(!sf.stableDetected, "Sustained release sample 3 -> STABLE RELEASED");
  TEST_ASSERT(sf.releases == 1, "Release count should be 1");
  TEST_ASSERT(sf.completedCycles == 1, "Completed 1 full cycle");

  // Polarity inversion test
  invertLogic = true; // Now HIGH = DETECTED
  updateSensorFilter(sf, true, invertLogic, 200);
  updateSensorFilter(sf, true, invertLogic, 201);
  updateSensorFilter(sf, true, invertLogic, 205);
  TEST_ASSERT(sf.stableDetected, "With invertLogic=true, raw HIGH triggers DETECTED");
}

void testHomingFSM() {
  printf("Running Test Suite 3: Non-blocking Homing State Machine...\n");
  MockHomingEngine fsm;
  clearCapturedReplies();

  // T3.1: Nominal Homing Cycle for Axis X
  uint32_t t = 1000;
  bool ok = fsm.startHoming(AxisId::X, Transport::Tcp, t);
  TEST_ASSERT(ok && fsm.active && fsm.phase == HomePhase::Validate, "Start homing X");

  // Sensor initially clear
  fsm.advanceStep(false, t);
  TEST_ASSERT(fsm.phase == HomePhase::FastSeek, "Transition to FastSeek");

  // Step 50 steps toward sensor without trigger
  for (int i = 0; i < 50; i++) {
    t += 10;
    fsm.advanceStep(false, t);
  }
  TEST_ASSERT(fsm.phase == HomePhase::FastSeek && fsm.phaseSteps == 50, "Stepping in FastSeek");

  // Trigger optical sensor!
  t += 10;
  fsm.advanceStep(true, t);
  TEST_ASSERT(fsm.phase == HomePhase::PullOff, "Triggered FastSeek -> enters PullOff");
  TEST_ASSERT(fsm.fastTriggerSteps == 51, "Recorded fast trigger step count");

  // Pull-off: retract away until clear + 10 margin steps
  for (int i = 0; i < 9; i++) {
    t += 10;
    fsm.advanceStep(false, t);
  }
  TEST_ASSERT(fsm.phase == HomePhase::PullOff, "Still in pull-off margin");
  t += 10;
  fsm.advanceStep(false, t); // 10th step clear
  TEST_ASSERT(fsm.phase == HomePhase::SlowLatch, "Pull-off cleared -> enters SlowLatch");

  // Slow latch: step until sensor triggers again
  for (int i = 0; i < 15; i++) {
    t += 25;
    fsm.advanceStep(false, t);
  }
  t += 25;
  fsm.advanceStep(true, t); // Latch trigger!
  TEST_ASSERT(fsm.phase == HomePhase::Complete && !fsm.active, "SlowLatch completed");
  TEST_ASSERT(fsm.axisHomed[0] == true, "Axis X marked homed");
  TEST_ASSERT(fsm.axisPos[0] == 0, "Axis X position zeroed to origin");
  TEST_ASSERT(lastTcpReply == "ok", "TCP reply was 'ok'");

  // T3.2: Initial Blocked Sensor Handling
  clearCapturedReplies();
  t = 5000;
  fsm.startHoming(AxisId::Y, Transport::Serial, t);
  fsm.advanceStep(true, t); // sensor is already blocked at start
  TEST_ASSERT(fsm.phase == HomePhase::ClearInitial, "Initial blocked sensor enters ClearInitial");

  // Clears after 20 steps
  for (int i = 0; i < 19; i++) {
    t += 25;
    fsm.advanceStep(true, t);
  }
  t += 25;
  fsm.advanceStep(false, t); // cleared!
  TEST_ASSERT(fsm.phase == HomePhase::PullOff, "ClearInitial success -> enters PullOff");
  fsm.abort(t + 10); // Abort T3.2 to reset state for T3.3
  TEST_ASSERT(!fsm.active, "T3.2 aborted, FSM idle");

  // T3.3: Emergency Stop Interruption
  clearCapturedReplies();
  t = 8000;
  fsm.startHoming(AxisId::X, Transport::Serial, t);
  fsm.advanceStep(false, t); // enters FastSeek
  TEST_ASSERT(fsm.phase == HomePhase::FastSeek, "In FastSeek");
  fsm.abort(t + 50);
  TEST_ASSERT(!fsm.active && fsm.result == HomeResult::Aborted, "Homing aborted");
  TEST_ASSERT(!fsm.axisHomed[0], "Axis X unhomed after abort");
  TEST_ASSERT(lastSerialReply.find("error: HOME_ABORTED") != std::string::npos, "Abort error sent to Serial");

  // T3.4: Dual Homing (G28) Partial Outcome
  clearCapturedReplies();
  t = 12000;
  fsm.startHomeAll(Transport::Tcp, t);
  TEST_ASSERT(fsm.active && fsm.dualHome && fsm.currentAxis == AxisId::X, "G28 starts with X");

  // Complete X
  fsm.advanceStep(false, t); // FastSeek
  fsm.advanceStep(true, t + 10); // PullOff
  for (int i = 0; i < 10; i++) fsm.advanceStep(false, t + 20 + i*10); // SlowLatch
  fsm.advanceStep(true, t + 200); // X Complete!
  TEST_ASSERT(fsm.xCompleted && fsm.axisHomed[0], "X homed successfully");
  TEST_ASSERT(fsm.currentAxis == AxisId::Y && fsm.phase == HomePhase::Validate, "Advances to Y Validate");
  TEST_ASSERT(fsm.fastTriggerSteps == 0 && fsm.retractSteps == 0 && fsm.latchSteps == 0, "Axis Y metrics reset before homing begins");

  // Now Y times out in FastSeek!
  fsm.advanceStep(false, t + 210); // FastSeek
  fsm.advanceStep(false, t + 210 + HOMING_PHASE_TIMEOUT_MS + 100); // Timeout!
  TEST_ASSERT(!fsm.active && fsm.result == HomeResult::SeekTimeout, "Y timed out");
  TEST_ASSERT(fsm.axisHomed[0] == true, "Partial state: X remains homed");
  TEST_ASSERT(fsm.axisHomed[1] == false, "Partial state: Y is NOT homed");
  TEST_ASSERT(lastTcpReply != "ok", "TCP reply MUST NOT be 'ok' on partial failure!");
  TEST_ASSERT(lastTcpReply.find("error: HOME_TIMEOUT Y") != std::string::npos, "TCP reply reported Y timeout error");
}

int main() {
  printf("===================================================================\n");
  printf("  Antigravity C++ TDD Unit Tests for Combined Stepper + IR Homing  \n");
  printf("===================================================================\n\n");

  testParser();
  testSensorDebounce();
  testHomingFSM();

  printf("\n-------------------------------------------------------------------\n");
  printf("  Test Summary: %d / %d assertions passed (%d%%)\n",
         testsPassed, testsRun, (testsRun > 0) ? (testsPassed * 100 / testsRun) : 0);
  printf("-------------------------------------------------------------------\n");

  if (testsPassed == testsRun && testsRun > 0) {
    printf("  ALL TESTS PASSED SUCCESSFULLY! (GREEN)\n\n");
    return 0;
  } else {
    printf("  TESTS FAILED!\n\n");
    return 1;
  }
}
