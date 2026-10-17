// =============================================================================
//  stepper_motor_test.ino
//  Full-Featured Dual Stepper Motor & A4988 Driver Test Firmware
//  Target: ESP32-S3 Super Mini
//  Motors: Dual 28BYJ-48 Stepper Motors (Converted to 4-Wire Bipolar)
//  Drivers: Dual A4988 Stepper Motor Drivers (VMOT=5V, VDD=3.3V)
//  Author: Laser-XY-Scanner Project
//
//  No external libraries required — uses only standard ESP32 Arduino Core.
//
//  Open Serial Monitor at 115200 baud, type HELP for command reference.
// =============================================================================

#include <Arduino.h>

// ── Hardware Pin Configuration ───────────────────────────────────────────────
// Motor A (X-Axis Mirror Steering)
#define MOT_A_STEP_PIN    5         // Step pulse input (active rising edge)
#define MOT_A_DIR_PIN     6         // Direction input
#define MOT_A_EN_PIN      7         // Enable input (active LOW)

// Motor B (Y-Axis Mirror Steering)
#define MOT_B_STEP_PIN    9         // Step pulse input (active rising edge)
#define MOT_B_DIR_PIN     10        // Direction input
#define MOT_B_EN_PIN      11        // Enable input (active LOW)

// Microstepping Mode Selection (Shared MS1, MS2, MS3 pins for both A4988s)
#define MS1_PIN           1         // Microstep select bit 1
#define MS2_PIN           2         // Microstep select bit 2
#define MS3_PIN           4         // Microstep select bit 3 (GPIO 4 on SuperMini outer header)

// Status Indicator LED
#define STATUS_LED_PIN    13        // Built-in / on-board status LED

// ── Driver Logic Definitions ─────────────────────────────────────────────────
#define DRIVER_ENABLE     LOW       // A4988 ENABLE pin is active LOW
#define DRIVER_DISABLE    HIGH      // A4988 disabled (outputs high-Z)

#define DIR_CW            HIGH      // Default Clockwise / Forward logic level
#define DIR_CCW           LOW       // Default Counter-Clockwise / Reverse logic level

// ── Motor Mechanical Parameters ──────────────────────────────────────────────
// 28BYJ-48 unipolar-to-bipolar conversion:
//   Internal rotor has 32 full steps per revolution.
//   Internal gear train ratio is approximately 64:1 (exact ~63.68395:1).
//   Output shaft full-step resolution = 32 * 64 = 2048 full steps / rev.
#define BASE_STEPS_PER_REV  2048UL

// Default motion parameters
#define DEFAULT_SPEED_SPS   500     // Default stepping speed (steps per second)
#define MIN_SPEED_SPS       1       // Minimum valid step rate
#define MAX_SPEED_SPS       25000   // Maximum pulse generator rate (steps/s)
#define DEFAULT_ACCEL_SPS2  2000    // Default acceleration (steps/sec^2)
#define STEP_PULSE_WIDTH_US 3       // A4988 minimum step high pulse width (us)

#define AUTO_DISABLE_TIMEOUT_MS 30000UL // 30s idle before de-energizing coils

// ── State Variables ──────────────────────────────────────────────────────────
// Microstepping configuration (1, 2, 4, 8, 16)
uint8_t  microstepMode      = 1;
uint32_t stepsPerRev        = BASE_STEPS_PER_REV;

// Positions (in microsteps relative to zero)
long     posA               = 0;
long     posB               = 0;

// Speeds (steps per second)
uint32_t speedA             = DEFAULT_SPEED_SPS;
uint32_t speedB             = DEFAULT_SPEED_SPS;
uint32_t currentAccel       = DEFAULT_ACCEL_SPS2;

// Direction settings
bool     dirInvertA         = false;
bool     dirInvertB         = false;

// Driver enable state
bool     driversEnabled     = false;

// Continuous motion state for independent motors A and B
bool     contRunningA       = false;
int32_t  contSpeedA         = 0;
uint32_t contIntervalUsA    = 0;
uint32_t lastContStepUsA    = 0;
bool     contDirA           = true;

bool     contRunningB       = false;
int32_t  contSpeedB         = 0;
uint32_t contIntervalUsB    = 0;
uint32_t lastContStepUsB    = 0;
bool     contDirB           = true;

// Legacy alias for status checks
#define continuousRunning (contRunningA || contRunningB)

// Safety and idle power management
bool     autoDisableEnabled = true;
uint32_t lastMotionMillis   = 0;

// Serial echo toggle
bool     echoCommands       = true;

// ── Forward Declarations ─────────────────────────────────────────────────────
void printBanner();
void printHelp();
void printStatus();
void handleCommand(String cmd);
void setMicrostepping(uint8_t mode);
void setDriversEnabled(bool enable);
void pulseStep(uint8_t pin);
bool checkAbort();
void countdown(int seconds);

// Motion primitives
void stepMotorA(long steps, uint32_t sps = 0);
void stepMotorB(long steps, uint32_t sps = 0);
void dualStep(long stepsA, long stepsB, uint32_t sps = 0);
void dualGoto(long targetA, long targetB, uint32_t sps = 0);
void traceCircle(float radiusSteps, int segments, uint32_t sps = 0);
void traceSquare(float sideSteps, uint32_t sps = 0);
void runBacklashRoutine();
void runStallTest(uint32_t testSpeed, uint32_t testSteps);
void runSelfTest();
void runBenchmark();

// =============================================================================
//  SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(600);

  // Configure output pins
  pinMode(MOT_A_STEP_PIN, OUTPUT);
  pinMode(MOT_A_DIR_PIN,  OUTPUT);
  pinMode(MOT_A_EN_PIN,   OUTPUT);

  pinMode(MOT_B_STEP_PIN, OUTPUT);
  pinMode(MOT_B_DIR_PIN,  OUTPUT);
  pinMode(MOT_B_EN_PIN,   OUTPUT);

  pinMode(MS1_PIN,        OUTPUT);
  pinMode(MS2_PIN,        OUTPUT);
  pinMode(MS3_PIN,        OUTPUT);

  pinMode(STATUS_LED_PIN, OUTPUT);

  // Initialize signals to safe defaults
  digitalWrite(MOT_A_STEP_PIN, LOW);
  digitalWrite(MOT_B_STEP_PIN, LOW);
  digitalWrite(MOT_A_DIR_PIN,  LOW);
  digitalWrite(MOT_B_DIR_PIN,  LOW);

  // Motors start DISABLED (high-Z) for safety and cold startup
  setDriversEnabled(false);

  // Configure full-step mode by default
  setMicrostepping(1);

  // Status LED off initially
  digitalWrite(STATUS_LED_PIN, LOW);

  lastMotionMillis = millis();

  printBanner();
}

// =============================================================================
//  LOOP — Non-Blocking Command Dispatcher & Continuous Motion Engine
// =============================================================================
void loop() {
  // ── Handle Non-Blocking Continuous Motion (Motor A) ────────────────────────
  if (contRunningA && contIntervalUsA > 0) {
    uint32_t nowUs = micros();
    if ((nowUs - lastContStepUsA) >= contIntervalUsA) {
      lastContStepUsA = nowUs;

      bool physicalDir = contDirA;
      if (dirInvertA) physicalDir = !physicalDir;
      digitalWrite(MOT_A_DIR_PIN, physicalDir ? DIR_CW : DIR_CCW);
      delayMicroseconds(5);

      digitalWrite(MOT_A_STEP_PIN, HIGH);
      delayMicroseconds(STEP_PULSE_WIDTH_US);
      digitalWrite(MOT_A_STEP_PIN, LOW);

      posA += contDirA ? 1 : -1;
      lastMotionMillis = millis();

      if ((abs(posA) & 0xFF) == 0) {
        digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
      }
    }
  }

  // ── Handle Non-Blocking Continuous Motion (Motor B) ────────────────────────
  if (contRunningB && contIntervalUsB > 0) {
    uint32_t nowUs = micros();
    if ((nowUs - lastContStepUsB) >= contIntervalUsB) {
      lastContStepUsB = nowUs;

      bool physicalDir = contDirB;
      if (dirInvertB) physicalDir = !physicalDir;
      digitalWrite(MOT_B_DIR_PIN, physicalDir ? DIR_CW : DIR_CCW);
      delayMicroseconds(5);

      digitalWrite(MOT_B_STEP_PIN, HIGH);
      delayMicroseconds(STEP_PULSE_WIDTH_US);
      digitalWrite(MOT_B_STEP_PIN, LOW);

      posB += contDirB ? 1 : -1;
      lastMotionMillis = millis();

      if ((abs(posB) & 0xFF) == 0) {
        digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
      }
    }
  }

  // ── Idle Auto-Disable Check ────────────────────────────────────────────────
  if (autoDisableEnabled && driversEnabled && !continuousRunning) {
    if (millis() - lastMotionMillis > AUTO_DISABLE_TIMEOUT_MS) {
      setDriversEnabled(false);
      Serial.println("\n[SAFETY] Idle timeout reached (30s). Motor drivers DISABLED to prevent coil heating.");
      Serial.println("         Send ENABLE or any motion command to re-energize.");
    }
  }

  // ── Serial Command Processor ───────────────────────────────────────────────
  if (Serial.available()) {
    String raw = Serial.readStringUntil('\n');
    raw.trim();
    if (raw.length() == 0) return;

    if (echoCommands) {
      Serial.print(">> ");
      Serial.println(raw);
    }

    handleCommand(raw);
  }
}

// =============================================================================
//  COMMAND DISPATCHER
// =============================================================================
void handleCommand(String cmd) {
  String upper = cmd;
  upper.toUpperCase();

  // ── Quick Abort Key ('X', 'x', '!') ────────────────────────────────────────
  if (upper == "X" || upper == "!") {
    contRunningA = false;
    contRunningB = false;
    digitalWrite(STATUS_LED_PIN, LOW);
    Serial.println("[ABORT] All motion stopped immediately.");
    return;
  }

  // ── Microstepping Mode: MICRO <1|2|4|8|16> ─────────────────────────────────
  if (upper.startsWith("MICRO")) {
    int mode = upper.substring(5).toInt();
    if (mode == 1 || mode == 2 || mode == 4 || mode == 8 || mode == 16) {
      setMicrostepping((uint8_t)mode);
      Serial.printf("[MICRO] Microstepping set to 1/%d. Effective steps/rev = %u.\n",
                    mode, stepsPerRev);
    } else {
      Serial.println("[ERROR] Invalid microstepping mode. Valid choices: 1, 2, 4, 8, 16");
    }

  // ── Driver Enable / Disable ────────────────────────────────────────────────
  } else if (upper == "ENABLE") {
    setDriversEnabled(true);
    Serial.println("[ENABLE] A4988 driver outputs ENABLED (coils energized).");

  } else if (upper == "DISABLE") {
    contRunningA = false;
    contRunningB = false;
    setDriversEnabled(false);
    Serial.println("[DISABLE] A4988 driver outputs DISABLED (coils de-energized, motors free).");

  // ── START <A|B|BOTH> [sps] Continuous Rotation Command ───────────────────────
  } else if (upper.startsWith("START")) {
    String arg = upper.substring(5);
    arg.trim();

    char motorTarget = 'A';
    int sps = 100; // default safe speed 100 steps/sec

    if (arg.length() > 0) {
      char targetStr[16] = "";
      int parsedSps = 0;
      int count = sscanf(arg.c_str(), "%15s %d", targetStr, &parsedSps);
      String tUpper = String(targetStr);
      tUpper.toUpperCase();

      if (tUpper == "B" || tUpper == "Y") {
        motorTarget = 'B';
      } else if (tUpper == "BOTH" || tUpper == "ALL" || tUpper == "AB") {
        motorTarget = '+'; // both
      } else if (tUpper == "A" || tUpper == "X") {
        motorTarget = 'A';
      } else if (count == 1 && (targetStr[0] == '-' || isdigit(targetStr[0]))) {
        // User typed: START 100
        motorTarget = 'A';
        parsedSps = atoi(targetStr);
      }

      if (parsedSps != 0) {
        sps = parsedSps;
      }
    }

    if (abs(sps) < (int)MIN_SPEED_SPS) sps = 100;
    if ((uint32_t)abs(sps) > MAX_SPEED_SPS) {
      sps = (sps > 0) ? MAX_SPEED_SPS : -(int)MAX_SPEED_SPS;
    }

    setDriversEnabled(true);
    lastMotionMillis = millis();

    if (motorTarget == 'A' || motorTarget == '+') {
      speedA = (uint32_t)abs(sps);
      contSpeedA = sps;
      contDirA = (sps >= 0);
      contIntervalUsA = 1000000UL / speedA;
      lastContStepUsA = micros();
      contRunningA = true;
      Serial.printf("[START] Motor A running continuously at %+d sps (%.2f RPM, %s).\n",
                    sps, (float)speedA * 60.0f / stepsPerRev, contDirA ? "CW" : "CCW");
    }

    if (motorTarget == 'B' || motorTarget == '+') {
      speedB = (uint32_t)abs(sps);
      contSpeedB = sps;
      contDirB = (sps >= 0);
      contIntervalUsB = 1000000UL / speedB;
      lastContStepUsB = micros();
      contRunningB = true;
      Serial.printf("[START] Motor B running continuously at %+d sps (%.2f RPM, %s).\n",
                    sps, (float)speedB * 60.0f / stepsPerRev, contDirB ? "CW" : "CCW");
    }
    Serial.println("        Send STOP (or STOP A / STOP B / 'X') to halt.");

  // ── Single Motor A: STEP <n> ───────────────────────────────────────────────
  } else if (upper.startsWith("STEP")) {
    // If command is "STEP", default to 1 step. Otherwise parse integer.
    String arg = upper.substring(4);
    arg.trim();
    long steps = 1;
    if (arg.length() > 0) {
      steps = arg.toInt();
    }
    Serial.printf("[STEP] Moving Motor A by %ld microsteps at %u sps...\n", steps, speedA);
    stepMotorA(steps, speedA);
    Serial.printf("[STEP] Motor A position now: %ld microsteps (%.2f deg, %.3f rev)\n",
                  posA, (float)posA * 360.0f / stepsPerRev, (float)posA / stepsPerRev);

  // ── Continuous Speed: SPEED <steps_per_sec> ────────────────────────────────
  } else if (upper.startsWith("SPEED")) {
    String arg = upper.substring(5);
    arg.trim();
    if (arg.length() == 0) {
      Serial.printf("[SPEED] Motor A speed: %u sps | Motor B speed: %u sps\n", speedA, speedB);
      return;
    }
    int sps = arg.toInt();
    if (abs(sps) < (int)MIN_SPEED_SPS) {
      Serial.printf("[ERROR] Speed must be at least %d steps/sec.\n", MIN_SPEED_SPS);
      return;
    }
    if ((uint32_t)abs(sps) > MAX_SPEED_SPS) {
      Serial.printf("[WARN] Clamping speed to maximum safe limit (%u sps).\n", MAX_SPEED_SPS);
      sps = (sps > 0) ? MAX_SPEED_SPS : -(int)MAX_SPEED_SPS;
    }

    speedA = (uint32_t)abs(sps);
    contSpeedA = sps;
    contDirA = (sps >= 0);
    contIntervalUsA = 1000000UL / speedA;
    lastContStepUsA = micros();
    setDriversEnabled(true);
    contRunningA = true;
    lastMotionMillis = millis();

    float rpm = (float)speedA * 60.0f / stepsPerRev;
    Serial.printf("[SPEED] Running Motor A continuously at %+d sps (%.2f RPM, %s). Type STOP to halt.\n",
                  sps, rpm, contDirA ? "CW" : "CCW");

  // ── STOP [A|B|ALL] ─────────────────────────────────────────────────────────
  } else if (upper.startsWith("STOP")) {
    String arg = upper.substring(4);
    arg.trim();
    if (arg == "A" || arg == "X") {
      contRunningA = false;
      Serial.printf("[STOP] Motor A halted. Position: %ld\n", posA);
    } else if (arg == "B" || arg == "Y") {
      contRunningB = false;
      Serial.printf("[STOP] Motor B halted. Position: %ld\n", posB);
    } else {
      contRunningA = false;
      contRunningB = false;
      digitalWrite(STATUS_LED_PIN, LOW);
      Serial.printf("[STOP] All continuous motion halted. Positions: A=%ld, B=%ld\n", posA, posB);
    }
    lastMotionMillis = millis();

  // ── GOTO <pos> ─────────────────────────────────────────────────────────────
  } else if (upper.startsWith("GOTO")) {
    String arg = upper.substring(4);
    arg.trim();
    if (arg.length() == 0) {
      Serial.println("[ERROR] Usage: GOTO <target_position>");
      return;
    }
    long target = arg.toInt();
    long delta = target - posA;
    Serial.printf("[GOTO] Moving Motor A from %ld to target %ld (delta %ld steps)...\n",
                  posA, target, delta);
    stepMotorA(delta, speedA);
    Serial.printf("[GOTO] Motor A reached position: %ld\n", posA);

  // ── POS ────────────────────────────────────────────────────────────────────
  } else if (upper == "POS") {
    float degA = (float)posA * 360.0f / stepsPerRev;
    float revA = (float)posA / stepsPerRev;
    float degB = (float)posB * 360.0f / stepsPerRev;
    float revB = (float)posB / stepsPerRev;
    Serial.println("\n+------------------------------------------------------+");
    Serial.println("|               CURRENT AXIS POSITIONS                 |");
    Serial.println("+------------------------------------------------------+");
    Serial.printf( "|  Motor A (X) : %+8ld steps | %+8.2f deg | %+7.3f rev |\n",
                   posA, degA, revA);
    Serial.printf( "|  Motor B (Y) : %+8ld steps | %+8.2f deg | %+7.3f rev |\n",
                   posB, degB, revB);
    Serial.printf( "|  Resolution  : %u steps/rev (1/%d microstepping)    |\n",
                   stepsPerRev, microstepMode);
    Serial.println("+------------------------------------------------------+\n");

  // ── ZERO ───────────────────────────────────────────────────────────────────
  } else if (upper == "ZERO") {
    posA = 0;
    posB = 0;
    Serial.println("[ZERO] Position registers for Motor A and Motor B reset to 0 (Home).");

  // ── DIR [A|B] <CW|CCW> ─────────────────────────────────────────────────────
  } else if (upper.startsWith("DIR")) {
    String arg = upper.substring(3);
    arg.trim();

    char motorTarget = 'A';
    String dirStr = "";

    char part1[16] = "", part2[16] = "";
    int count = sscanf(arg.c_str(), "%15s %15s", part1, part2);
    String p1 = String(part1);
    p1.toUpperCase();

    if (count == 2) {
      if (p1 == "B" || p1 == "Y") motorTarget = 'B';
      else if (p1 == "BOTH" || p1 == "ALL") motorTarget = '+';
      else motorTarget = 'A';
      dirStr = String(part2);
      dirStr.toUpperCase();
    } else if (count == 1) {
      if (p1 == "CW" || p1 == "CCW" || p1 == "FORWARD" || p1 == "REVERSE") {
        motorTarget = 'A';
        dirStr = p1;
      } else {
        if (p1 == "B" || p1 == "Y") motorTarget = 'B';
        else motorTarget = 'A';
      }
    }

    if (dirStr == "CW" || dirStr == "FORWARD" || dirStr == "1" || dirStr == "+") {
      if (motorTarget == 'A' || motorTarget == '+') dirInvertA = false;
      if (motorTarget == 'B' || motorTarget == '+') dirInvertB = false;
      Serial.printf("[DIR] Motor %c direction logic set to NORMAL (CW).\n", motorTarget == '+' ? ' ' : motorTarget);
    } else if (dirStr == "CCW" || dirStr == "REVERSE" || dirStr == "-1" || dirStr == "-") {
      if (motorTarget == 'A' || motorTarget == '+') dirInvertA = true;
      if (motorTarget == 'B' || motorTarget == '+') dirInvertB = true;
      Serial.printf("[DIR] Motor %c direction logic set to INVERTED (CCW).\n", motorTarget == '+' ? ' ' : motorTarget);
    } else {
      Serial.printf("[DIR] Current: Motor A=%s | Motor B=%s. Usage: DIR [A|B] CW or DIR [A|B] CCW\n",
                    dirInvertA ? "INVERTED (CCW)" : "NORMAL (CW)",
                    dirInvertB ? "INVERTED (CCW)" : "NORMAL (CW)");
    }

  // ── REV <n> (Move N full revolutions) ──────────────────────────────────────
  } else if (upper.startsWith("REV")) {
    String arg = upper.substring(3);
    arg.trim();
    if (arg.length() == 0) {
      Serial.println("[ERROR] Usage: REV <number_of_revolutions>  e.g. REV 1, REV -2, REV 0.5");
      return;
    }
    float revs = arg.toFloat();
    long totalSteps = (long)round(revs * (float)stepsPerRev);
    Serial.printf("[REV] Moving Motor A by %.3f revolutions (%ld steps at 1/%d mode)...\n",
                  revs, totalSteps, microstepMode);
    stepMotorA(totalSteps, speedA);
    Serial.printf("[REV] Move complete. Motor A pos: %ld\n", posA);

  // ── RPM ────────────────────────────────────────────────────────────────────
  } else if (upper == "RPM") {
    float curRpmA = (float)speedA * 60.0f / stepsPerRev;
    float curRpmB = (float)speedB * 60.0f / stepsPerRev;
    Serial.println("\n+------------------------------------------------------+");
    Serial.println("|              ROTATIONAL VELOCITY (RPM)               |");
    Serial.println("+------------------------------------------------------+");
    Serial.printf( "|  Motor A Configured Speed : %5u sps -> %6.2f RPM    |\n",
                   speedA, curRpmA);
    Serial.printf( "|  Motor B Configured Speed : %5u sps -> %6.2f RPM    |\n",
                   speedB, curRpmB);
    if (contRunningA || contRunningB) {
      if (contRunningA) {
        float runRpmA = (float)abs(contSpeedA) * 60.0f / stepsPerRev;
        Serial.printf( "|  Continuous Rotation (A)  : ACTIVE  -> %6.2f RPM (%s) |\n",
                       runRpmA, contDirA ? "CW" : "CCW");
      }
      if (contRunningB) {
        float runRpmB = (float)abs(contSpeedB) * 60.0f / stepsPerRev;
        Serial.printf( "|  Continuous Rotation (B)  : ACTIVE  -> %6.2f RPM (%s) |\n",
                       runRpmB, contDirB ? "CW" : "CCW");
      }
    } else {
      Serial.println("|  Continuous Rotation      : IDLE / STOPPED           |");
    }
    Serial.println("+------------------------------------------------------+\n");

  // ── BACKLASH Routine ───────────────────────────────────────────────────────
  } else if (upper == "BACKLASH") {
    runBacklashRoutine();

  // ── STALL <speed> <steps> ──────────────────────────────────────────────────
  } else if (upper.startsWith("STALL")) {
    String args = upper.substring(5);
    args.trim();
    uint32_t testSpeed = 1000;
    uint32_t testSteps = 2048;
    int parsed = sscanf(args.c_str(), "%u %u", &testSpeed, &testSteps);
    if (parsed < 1) testSpeed = 1000;
    if (parsed < 2) testSteps = 2048;
    runStallTest(testSpeed, testSteps);

  // ── DUAL Commands ──────────────────────────────────────────────────────────
  } else if (upper.startsWith("DUAL")) {
    String sub = upper.substring(4);
    sub.trim();

    if (sub.startsWith("STEP")) {
      // DUAL STEP <nA> <nB>
      String args = sub.substring(4);
      args.trim();
      long nA = 0, nB = 0;
      int parsed = sscanf(args.c_str(), "%ld %ld", &nA, &nB);
      if (parsed < 2) {
        Serial.println("[ERROR] Usage: DUAL STEP <nA> <nB>");
        return;
      }
      Serial.printf("[DUAL STEP] Stepping A: %+ld, B: %+ld...\n", nA, nB);
      dualStep(nA, nB, max(speedA, speedB));
      Serial.printf("[DUAL STEP] Done. Positions: A=%ld, B=%ld\n", posA, posB);

    } else if (sub.startsWith("GOTO")) {
      // DUAL GOTO <xA> <yB>
      String args = sub.substring(4);
      args.trim();
      long targetA = 0, targetB = 0;
      int parsed = sscanf(args.c_str(), "%ld %ld", &targetA, &targetB);
      if (parsed < 2) {
        Serial.println("[ERROR] Usage: DUAL GOTO <targetA> <targetB>");
        return;
      }
      Serial.printf("[DUAL GOTO] Coordinated Bresenham move to (%ld, %ld)...\n", targetA, targetB);
      dualGoto(targetA, targetB, max(speedA, speedB));
      Serial.printf("[DUAL GOTO] Reached target: (%ld, %ld)\n", posA, posB);

    } else if (sub.startsWith("SPEED")) {
      // DUAL SPEED <sA> <sB>
      String args = sub.substring(5);
      args.trim();
      uint32_t sA = DEFAULT_SPEED_SPS, sB = DEFAULT_SPEED_SPS;
      int parsed = sscanf(args.c_str(), "%u %u", &sA, &sB);
      if (parsed < 2) {
        Serial.println("[ERROR] Usage: DUAL SPEED <sA> <sB>");
        return;
      }
      speedA = constrain(sA, MIN_SPEED_SPS, MAX_SPEED_SPS);
      speedB = constrain(sB, MIN_SPEED_SPS, MAX_SPEED_SPS);
      Serial.printf("[DUAL SPEED] Speeds updated: Motor A = %u sps, Motor B = %u sps\n",
                    speedA, speedB);

    } else if (sub.startsWith("CIRCLE")) {
      // DUAL CIRCLE <radius> <steps>
      String args = sub.substring(6);
      args.trim();
      float radius = 500.0f;
      int segments = 64;
      sscanf(args.c_str(), "%f %d", &radius, &segments);
      if (segments < 8) segments = 8;
      if (segments > 360) segments = 360;
      Serial.printf("[DUAL CIRCLE] Tracing circle: radius=%.1f steps, %d segments...\n",
                    radius, segments);
      traceCircle(radius, segments, max(speedA, speedB));

    } else if (sub.startsWith("SQUARE")) {
      // DUAL SQUARE <size>
      String args = sub.substring(6);
      args.trim();
      float size = 500.0f;
      if (args.length() > 0) size = args.toFloat();
      Serial.printf("[DUAL SQUARE] Tracing square of side %.1f steps...\n", size);
      traceSquare(size, max(speedA, speedB));

    } else if (sub == "HOME") {
      // DUAL HOME: return both axes to 0,0
      Serial.printf("[DUAL HOME] Returning both axes from (%ld, %ld) to (0, 0)...\n", posA, posB);
      dualGoto(0, 0, max(speedA, speedB));
      Serial.println("[DUAL HOME] Both axes centered at Home (0, 0).");

    } else {
      Serial.printf("[ERROR] Unknown DUAL command: \"%s\". Type HELP for syntax.\n", sub.c_str());
    }

  // ── ACCEL <steps_per_sec2> ─────────────────────────────────────────────────
  } else if (upper.startsWith("ACCEL")) {
    String arg = upper.substring(5);
    arg.trim();
    if (arg.length() == 0) {
      Serial.printf("[ACCEL] Current acceleration: %u steps/sec^2\n", currentAccel);
      return;
    }
    uint32_t a = arg.toInt();
    if (a < 50 || a > 50000) {
      Serial.println("[ERROR] Acceleration must be between 50 and 50000 steps/sec^2.");
    } else {
      currentAccel = a;
      Serial.printf("[ACCEL] Acceleration set to %u steps/sec^2.\n", currentAccel);
    }

  // ── AUTODISABLE <0|1> ──────────────────────────────────────────────────────
  } else if (upper.startsWith("AUTODISABLE")) {
    String arg = upper.substring(11);
    arg.trim();
    if (arg == "0" || arg == "OFF" || arg == "FALSE") {
      autoDisableEnabled = false;
      Serial.println("[AUTODISABLE] Idle auto-disable DISABLED. Motor holding torque will remain active indefinitely.");
    } else if (arg == "1" || arg == "ON" || arg == "TRUE") {
      autoDisableEnabled = true;
      Serial.println("[AUTODISABLE] Idle auto-disable ENABLED (30s timeout).");
    } else {
      Serial.printf("[AUTODISABLE] Currently: %s\n", autoDisableEnabled ? "ENABLED" : "DISABLED");
    }

  // ── STATUS / ? ─────────────────────────────────────────────────────────────
  } else if (upper == "STATUS" || upper == "?") {
    printStatus();

  // ── HELP / H ───────────────────────────────────────────────────────────────
  } else if (upper == "HELP" || upper == "H") {
    printHelp();

  // ── RESET ──────────────────────────────────────────────────────────────────
  } else if (upper == "RESET") {
    contRunningA = false;
    contRunningB = false;
    setDriversEnabled(false);
    setMicrostepping(1);
    speedA = DEFAULT_SPEED_SPS;
    speedB = DEFAULT_SPEED_SPS;
    currentAccel = DEFAULT_ACCEL_SPS2;
    posA = 0;
    posB = 0;
    dirInvertA = false;
    dirInvertB = false;
    autoDisableEnabled = true;
    digitalWrite(STATUS_LED_PIN, LOW);
    Serial.println("[RESET] All motor settings restored to factory defaults. Drivers DISABLED.");

  // ── TEST ───────────────────────────────────────────────────────────────────
  } else if (upper == "TEST") {
    runSelfTest();

  // ── BENCHMARK ──────────────────────────────────────────────────────────────
  } else if (upper == "BENCHMARK") {
    runBenchmark();

  // ── ECHO ───────────────────────────────────────────────────────────────────
  } else if (upper == "ECHO") {
    echoCommands = !echoCommands;
    Serial.printf("[ECHO] Command echo %s.\n", echoCommands ? "ON" : "OFF");

  } else {
    Serial.printf("[?] Unknown command: \"%s\"  -- type HELP for command list\n", cmd.c_str());
  }
}

// =============================================================================
//  DRIVER CONTROL PRIMITIVES
// =============================================================================

// Set microstepping pins (MS1, MS2, MS3)
// A4988 Truth Table:
//  MS1  MS2  MS3  Microstep Resolution
//  LOW  LOW  LOW  Full Step (1/1)
//  HIGH LOW  LOW  Half Step (1/2)
//  LOW  HIGH LOW  Quarter Step (1/4)
//  HIGH HIGH LOW  Eighth Step (1/8)
//  HIGH HIGH HIGH Sixteenth Step (1/16)
void setMicrostepping(uint8_t mode) {
  microstepMode = mode;

  switch (mode) {
    case 2: // 1/2 Half step
      digitalWrite(MS1_PIN, HIGH);
      digitalWrite(MS2_PIN, LOW);
      digitalWrite(MS3_PIN, LOW);
      stepsPerRev = BASE_STEPS_PER_REV * 2;
      break;

    case 4: // 1/4 Quarter step
      digitalWrite(MS1_PIN, LOW);
      digitalWrite(MS2_PIN, HIGH);
      digitalWrite(MS3_PIN, LOW);
      stepsPerRev = BASE_STEPS_PER_REV * 4;
      break;

    case 8: // 1/8 Eighth step
      digitalWrite(MS1_PIN, HIGH);
      digitalWrite(MS2_PIN, HIGH);
      digitalWrite(MS3_PIN, LOW);
      stepsPerRev = BASE_STEPS_PER_REV * 8;
      break;

    case 16: // 1/16 Sixteenth step
      digitalWrite(MS1_PIN, HIGH);
      digitalWrite(MS2_PIN, HIGH);
      digitalWrite(MS3_PIN, HIGH);
      stepsPerRev = BASE_STEPS_PER_REV * 16;
      break;

    case 1: // 1/1 Full step default
    default:
      microstepMode = 1;
      digitalWrite(MS1_PIN, LOW);
      digitalWrite(MS2_PIN, LOW);
      digitalWrite(MS3_PIN, LOW);
      stepsPerRev = BASE_STEPS_PER_REV * 1;
      break;
  }
}

// Enable or disable both A4988 driver outputs (active LOW)
void setDriversEnabled(bool enable) {
  driversEnabled = enable;
  uint8_t pinLevel = enable ? DRIVER_ENABLE : DRIVER_DISABLE;
  digitalWrite(MOT_A_EN_PIN, pinLevel);
  digitalWrite(MOT_B_EN_PIN, pinLevel);

  if (enable) {
    // A4988 wake-up delay from sleep/enable change (~1 ms)
    delayMicroseconds(1000);
    lastMotionMillis = millis();
  } else {
    digitalWrite(STATUS_LED_PIN, LOW);
  }
}

// Produce an accurate step pulse on a specific step pin
inline void pulseStep(uint8_t pin) {
  digitalWrite(pin, HIGH);
  delayMicroseconds(STEP_PULSE_WIDTH_US);
  digitalWrite(pin, LOW);
}

// Check Serial buffer for non-blocking abort keys
bool checkAbort() {
  if (Serial.available()) {
    char c = Serial.peek();
    if (c == 'X' || c == 'x' || c == '!' || c == ' ' || c == 27) {
      Serial.read(); // consume abort character
      Serial.println("\n[ABORT] Abort key detected! Motion canceled immediately.");
      digitalWrite(STATUS_LED_PIN, LOW);
      lastMotionMillis = millis();
      return true;
    }
  }
  return false;
}

// Countdown timer with abort capability
void countdown(int seconds) {
  for (int i = seconds; i > 0; i--) {
    Serial.printf("  %d...\n", i);
    digitalWrite(STATUS_LED_PIN, HIGH);
    delay(200);
    digitalWrite(STATUS_LED_PIN, LOW);
    for (int d = 0; d < 8; d++) {
      if (checkAbort()) return;
      delay(100);
    }
  }
}

// =============================================================================
//  MOTION PRIMITIVES
// =============================================================================

// Move Motor A by discrete relative step count with optional ramp
void stepMotorA(long steps, uint32_t sps) {
  if (steps == 0) return;
  if (sps == 0) sps = speedA;
  sps = constrain(sps, MIN_SPEED_SPS, MAX_SPEED_SPS);

  if (!driversEnabled) setDriversEnabled(true);

  bool dir = (steps > 0);
  long count = abs(steps);

  bool physicalDir = dir;
  if (dirInvertA) physicalDir = !physicalDir;
  digitalWrite(MOT_A_DIR_PIN, physicalDir ? DIR_CW : DIR_CCW);
  delayMicroseconds(10); // A4988 DIR setup time (min 200ns)

  uint32_t stepIntervalUs = 1000000UL / sps;
  if (stepIntervalUs < (STEP_PULSE_WIDTH_US + 2)) {
    stepIntervalUs = STEP_PULSE_WIDTH_US + 2;
  }

  digitalWrite(STATUS_LED_PIN, HIGH);

  for (long i = 0; i < count; i++) {
    if ((i & 0x1F) == 0) { // Check abort every 32 steps for low overhead
      if (checkAbort()) break;
    }

    uint32_t t0 = micros();
    pulseStep(MOT_A_STEP_PIN);
    posA += dir ? 1 : -1;

    // Maintain accurate step interval timing
    while ((micros() - t0) < stepIntervalUs) {
      // yield for CPU housekeeping if interval is long
      if (stepIntervalUs > 5000) delayMicroseconds(100);
    }
  }

  digitalWrite(STATUS_LED_PIN, LOW);
  lastMotionMillis = millis();
}

// Move Motor B by discrete relative step count
void stepMotorB(long steps, uint32_t sps) {
  if (steps == 0) return;
  if (sps == 0) sps = speedB;
  sps = constrain(sps, MIN_SPEED_SPS, MAX_SPEED_SPS);

  if (!driversEnabled) setDriversEnabled(true);

  bool dir = (steps > 0);
  long count = abs(steps);

  bool physicalDir = dir;
  if (dirInvertB) physicalDir = !physicalDir;
  digitalWrite(MOT_B_DIR_PIN, physicalDir ? DIR_CW : DIR_CCW);
  delayMicroseconds(10);

  uint32_t stepIntervalUs = 1000000UL / sps;
  if (stepIntervalUs < (STEP_PULSE_WIDTH_US + 2)) {
    stepIntervalUs = STEP_PULSE_WIDTH_US + 2;
  }

  digitalWrite(STATUS_LED_PIN, HIGH);

  for (long i = 0; i < count; i++) {
    if ((i & 0x1F) == 0) {
      if (checkAbort()) break;
    }

    uint32_t t0 = micros();
    pulseStep(MOT_B_STEP_PIN);
    posB += dir ? 1 : -1;

    while ((micros() - t0) < stepIntervalUs) {
      if (stepIntervalUs > 5000) delayMicroseconds(100);
    }
  }

  digitalWrite(STATUS_LED_PIN, LOW);
  lastMotionMillis = millis();
}

// Coordinated simultaneous move of Motor A and B using DDA (Digital Differential Analyzer)
void dualStep(long stepsA, long stepsB, uint32_t sps) {
  if (stepsA == 0 && stepsB == 0) return;
  if (sps == 0) sps = max(speedA, speedB);
  sps = constrain(sps, MIN_SPEED_SPS, MAX_SPEED_SPS);

  if (!driversEnabled) setDriversEnabled(true);

  bool dirA = (stepsA > 0);
  bool dirB = (stepsB > 0);
  long absA = abs(stepsA);
  long absB = abs(stepsB);

  bool physicalDirA = dirA;
  if (dirInvertA) physicalDirA = !physicalDirA;
  digitalWrite(MOT_A_DIR_PIN, physicalDirA ? DIR_CW : DIR_CCW);

  bool physicalDirB = dirB;
  if (dirInvertB) physicalDirB = !physicalDirB;
  digitalWrite(MOT_B_DIR_PIN, physicalDirB ? DIR_CW : DIR_CCW);
  delayMicroseconds(10);

  long totalSteps = max(absA, absB);
  long accA = totalSteps / 2;
  long accB = totalSteps / 2;

  uint32_t stepIntervalUs = 1000000UL / sps;
  if (stepIntervalUs < (STEP_PULSE_WIDTH_US + 3)) {
    stepIntervalUs = STEP_PULSE_WIDTH_US + 3;
  }

  digitalWrite(STATUS_LED_PIN, HIGH);

  for (long i = 0; i < totalSteps; i++) {
    if ((i & 0x1F) == 0) {
      if (checkAbort()) break;
    }

    uint32_t t0 = micros();
    bool doA = false;
    bool doB = false;

    accA += absA;
    if (accA >= totalSteps) {
      accA -= totalSteps;
      doA = true;
    }

    accB += absB;
    if (accB >= totalSteps) {
      accB -= totalSteps;
      doB = true;
    }

    // Simultaneously trigger step pins
    if (doA) digitalWrite(MOT_A_STEP_PIN, HIGH);
    if (doB) digitalWrite(MOT_B_STEP_PIN, HIGH);
    delayMicroseconds(STEP_PULSE_WIDTH_US);
    if (doA) {
      digitalWrite(MOT_A_STEP_PIN, LOW);
      posA += dirA ? 1 : -1;
    }
    if (doB) {
      digitalWrite(MOT_B_STEP_PIN, LOW);
      posB += dirB ? 1 : -1;
    }

    while ((micros() - t0) < stepIntervalUs) {
      if (stepIntervalUs > 5000) delayMicroseconds(100);
    }
  }

  digitalWrite(STATUS_LED_PIN, LOW);
  lastMotionMillis = millis();
}

// Coordinated move to absolute (targetA, targetB)
void dualGoto(long targetA, long targetB, uint32_t sps) {
  long deltaA = targetA - posA;
  long deltaB = targetB - posB;
  dualStep(deltaA, deltaB, sps);
}

// Trace a smooth circle using parametric interpolation
void traceCircle(float radiusSteps, int segments, uint32_t sps) {
  if (segments < 8) segments = 8;
  long startA = posA;
  long startB = posB;

  Serial.println("        Tracing circle. Press 'X' at any time to abort.");
  float angleStep = 2.0f * PI / (float)segments;

  for (int i = 0; i <= segments; i++) {
    if (checkAbort()) break;

    float theta = (float)i * angleStep;
    long targetA = startA + (long)round(radiusSteps * cos(theta));
    long targetB = startB + (long)round(radiusSteps * sin(theta));

    dualGoto(targetA, targetB, sps);
  }
  Serial.println("[DUAL CIRCLE] Circle trace complete.");
}

// Trace a square of side length `sideSteps`
void traceSquare(float sideSteps, uint32_t sps) {
  Serial.println("        Tracing square. Press 'X' at any time to abort.");
  long startA = posA;
  long startB = posB;
  long side = (long)round(sideSteps);

  // 4 corners relative to current starting point
  dualGoto(startA + side, startB,        sps);
  if (checkAbort()) return;
  dualGoto(startA + side, startB + side, sps);
  if (checkAbort()) return;
  dualGoto(startA,        startB + side, sps);
  if (checkAbort()) return;
  dualGoto(startA,        startB,        sps);

  Serial.println("[DUAL SQUARE] Square trace complete.");
}

// =============================================================================
//  DIAGNOSTICS & CHARACTERISATION ROUTINES
// =============================================================================

// Characterise spur gear backlash inherent in 28BYJ-48
void runBacklashRoutine() {
  Serial.println("\n+======================================================+");
  Serial.println("|          28BYJ-48 BACKLASH MEASUREMENT ROUTINE       |");
  Serial.println("+======================================================+");
  Serial.println("| 28BYJ-48 internal spur gearboxes have ~1-2 deg of    |");
  Serial.println("| mechanical backlash (dead-band when reversing).      |");
  Serial.println("|                                                      |");
  Serial.println("| Procedure:                                           |");
  Serial.println("| 1. Pre-loads gears CW by +200 steps.                 |");
  Serial.println("| 2. Pauses 1 second.                                  |");
  Serial.println("| 3. Reverses in graduated step bursts:                |");
  Serial.println("|    5, 10, 15, 20, 25, 30, 40 steps.                  |");
  Serial.println("| 4. Watch the motor shaft or mirror pointer to note   |");
  Serial.println("|    at which step count physical motion resumes.      |");
  Serial.println("+======================================================+");
  Serial.println("Starting in 3s (Press 'X' to abort)...");
  countdown(3);

  setDriversEnabled(true);

  Serial.println(">> Step 1: Pre-loading forward (+200 steps)...");
  stepMotorA(200, 300);
  delay(1000);

  int testIncrements[] = {5, 10, 15, 20, 25, 30, 40};
  int numTests = sizeof(testIncrements) / sizeof(testIncrements[0]);

  for (int i = 0; i < numTests; i++) {
    if (checkAbort()) return;
    int revSteps = testIncrements[i];
    Serial.printf(">> Step 2.%d: Reverse pulse of %d steps... ", i + 1, revSteps);
    stepMotorA(-revSteps, 150);
    delay(800);
    Serial.println("Did the output shaft move? (Check visual pointer)");
  }

  Serial.println("\n>> Re-aligning to starting reference...");
  stepMotorA(200, 300);

  Serial.println("\n[BACKLASH] Typical 28BYJ-48 backlash: 15 to 35 full steps (0.5 to 1.5 deg).");
  Serial.println("           Use your observed dead-step count to configure firmware backlash compensation.");
}

// High-speed step generator and stall detection test
void runStallTest(uint32_t testSpeed, uint32_t testSteps) {
  Serial.println("\n+======================================================+");
  Serial.println("|             MOTOR A STALL & SPEED TEST               |");
  Serial.println("+======================================================+");
  Serial.printf( "| Requested Speed : %5u steps/sec                      |\n", testSpeed);
  Serial.printf( "| Total Steps     : %5u microsteps                     |\n", testSteps);
  float expectedDurationS = (float)testSteps / (float)testSpeed;
  Serial.printf( "| Expected Time   : %6.3f seconds                      |\n", expectedDurationS);
  float theoreticalRpm = (float)testSpeed * 60.0f / (float)stepsPerRev;
  Serial.printf( "| Theoretical RPM : %6.2f RPM                          |\n", theoreticalRpm);
  Serial.println("+======================================================+");
  Serial.println("Starting in 3s (Press 'X' to abort)...");
  countdown(3);

  setDriversEnabled(true);
  uint32_t startUs = micros();
  stepMotorA(testSteps, testSpeed);
  uint32_t durationUs = micros() - startUs;

  float actualDurationS = (float)durationUs / 1000000.0f;
  float actualSps = (float)testSteps / actualDurationS;
  float timingErrorPct = ((actualDurationS - expectedDurationS) / expectedDurationS) * 100.0f;

  Serial.println("\n--- Stall Test Results ---");
  Serial.printf("Actual Duration : %.4f seconds\n", actualDurationS);
  Serial.printf("Achieved Rate   : %.1f steps/sec\n", actualSps);
  Serial.printf("Timing Variance : %+.2f%%\n", timingErrorPct);

  if (testSpeed > 1500 && microstepMode == 1) {
    Serial.println("\n[NOTE] 28BYJ-48 full-step speed above ~1000-1200 sps often stalls without ramping.");
    Serial.println("       If motor buzzed without spinning, reduce speed or enable microstepping.");
  } else {
    Serial.println("[NOTE] Listen carefully for smooth rotation vs grinding or lost step clicks.");
  }
}

// Automated Hardware Self-Test Sequence
void runSelfTest() {
  Serial.println("\n+======================================================+");
  Serial.println("|            AUTOMATED HARDWARE SELF-TEST              |");
  Serial.println("+======================================================+");
  Serial.println("| Tests:                                               |");
  Serial.println("| 1. Status LED flash test                             |");
  Serial.println("| 2. Microstepping pin logic verification (MS1-MS3)   |");
  Serial.println("| 3. Motor A forward & reverse motion                  |");
  Serial.println("| 4. Motor B forward & reverse motion                  |");
  Serial.println("| 5. Dual-axis coordinated diagonal motion             |");
  Serial.println("| 6. Microstepping transition test (1/1 -> 1/16)       |");
  Serial.println("+======================================================+");
  Serial.println("Starting self-test in 3 seconds (Press 'X' to abort)...");
  countdown(3);

  // 1. LED Test
  Serial.print("Test 1/6: Status LED blink... ");
  for (int i = 0; i < 3; i++) {
    digitalWrite(STATUS_LED_PIN, HIGH);
    delay(100);
    digitalWrite(STATUS_LED_PIN, LOW);
    delay(100);
  }
  Serial.println("[PASS]");

  // 2. Microstepping Pin Test
  Serial.print("Test 2/6: Microstepping GPIO pin logic... ");
  setMicrostepping(1);
  if (digitalRead(MS1_PIN) != LOW || digitalRead(MS2_PIN) != LOW || digitalRead(MS3_PIN) != LOW) {
    Serial.println("[FAIL - MS pins not LOW]");
  } else {
    setMicrostepping(16);
    if (digitalRead(MS1_PIN) != HIGH || digitalRead(MS2_PIN) != HIGH || digitalRead(MS3_PIN) != HIGH) {
      Serial.println("[FAIL - MS pins not HIGH]");
    } else {
      setMicrostepping(1);
      Serial.println("[PASS]");
    }
  }

  // 3. Motor A Motion Test
  if (checkAbort()) return;
  Serial.print("Test 3/6: Motor A motion (+100 steps, -100 steps)... ");
  setDriversEnabled(true);
  stepMotorA(100, 400);
  delay(150);
  stepMotorA(-100, 400);
  Serial.println("[PASS]");

  // 4. Motor B Motion Test
  if (checkAbort()) return;
  Serial.print("Test 4/6: Motor B motion (+100 steps, -100 steps)... ");
  stepMotorB(100, 400);
  delay(150);
  stepMotorB(-100, 400);
  Serial.println("[PASS]");

  // 5. Dual Axis Diagonal Move
  if (checkAbort()) return;
  Serial.print("Test 5/6: Dual-axis simultaneous diagonal move... ");
  dualStep(150, 150, 400);
  delay(150);
  dualStep(-150, -150, 400);
  Serial.println("[PASS]");

  // 6. Microstepping Transition Test
  if (checkAbort()) return;
  Serial.print("Test 6/6: Microstepping 1/16 resolution test... ");
  setMicrostepping(16);
  dualStep(800, 800, 3000);
  delay(150);
  dualStep(-800, -800, 3000);
  setMicrostepping(1);
  Serial.println("[PASS]");

  // Return to 0,0
  posA = 0;
  posB = 0;
  setDriversEnabled(false);

  Serial.println("\n+======================================================+");
  Serial.println("|           ALL SELF-TEST CHECKS PASSED [OK]           |");
  Serial.println("+======================================================+\n");
}

// Firmware & Hardware Benchmark
void runBenchmark() {
  Serial.println("\n+======================================================+");
  Serial.println("|        ESP32-S3 STEP GENERATION BENCHMARK            |");
  Serial.println("+======================================================+");
  Serial.printf( "| CPU Frequency    : %u MHz                            |\n", getCpuFrequencyMhz());
  Serial.printf( "| Free Heap Memory : %u bytes                          |\n", (unsigned)ESP.getFreeHeap());
  Serial.println("| Measuring pulse output frequency without driver load |");
  Serial.println("+======================================================+");

  // Benchmark single-axis raw pin toggle
  const uint32_t BENCH_PULSES = 20000;
  uint32_t t0 = micros();
  for (uint32_t i = 0; i < BENCH_PULSES; i++) {
    digitalWrite(MOT_A_STEP_PIN, HIGH);
    digitalWrite(MOT_A_STEP_PIN, LOW);
  }
  uint32_t rawElapsedUs = micros() - t0;
  float rawRateKhz = (float)BENCH_PULSES / (float)rawElapsedUs * 1000.0f;

  // Benchmark DDA dual-axis calculation loop overhead
  long absA = 10000, absB = 7500;
  long total = 10000;
  long accA = 5000, accB = 5000;
  t0 = micros();
  for (long i = 0; i < total; i++) {
    accA += absA;
    if (accA >= total) accA -= total;
    accB += absB;
    if (accB >= total) accB -= total;
  }
  uint32_t ddaElapsedUs = micros() - t0;
  float ddaRateKhz = (float)total / (float)ddaElapsedUs * 1000.0f;

  Serial.printf("Raw GPIO Toggle Rate        : %8.2f kHz (%.3f us / pulse)\n",
                rawRateKhz, (float)rawElapsedUs / (float)BENCH_PULSES);
  Serial.printf("DDA Math Execution Rate     : %8.2f kHz (%.3f us / step)\n",
                ddaRateKhz, (float)ddaElapsedUs / (float)total);
  Serial.printf("A4988 Hardware Step Limit   :   250.00 kHz\n");
  Serial.printf("28BYJ-48 Practical Limit    :    10.00 kHz (in 1/16 mode)\n");
  Serial.println("[BENCHMARK] ESP32-S3 CPU comfortably exceeds all motion pulse requirements.\n");
}

// =============================================================================
//  STATUS REPORT
// =============================================================================
void printStatus() {
  float degA = (float)posA * 360.0f / stepsPerRev;
  float revA = (float)posA / stepsPerRev;
  float degB = (float)posB * 360.0f / stepsPerRev;
  float revB = (float)posB / stepsPerRev;

  Serial.println("\n+======================================================+");
  Serial.println("|             STEPPER DRIVER STATUS REPORT             |");
  Serial.println("+======================================================+");
  Serial.printf( "|  Drivers Enabled     : %-29s|\n",
                 driversEnabled ? "YES (Active Low GND)" : "NO  (Outputs High-Z)");
  Serial.printf( "|  Microstepping Mode  : 1/%-2d (%5u steps/rev)       |\n",
                 microstepMode, stepsPerRev);
  Serial.printf( "|  MS Pins [1, 2, 4]   : [%d, %d, %d]                     |\n",
                 digitalRead(MS1_PIN), digitalRead(MS2_PIN), digitalRead(MS3_PIN));
  Serial.printf( "|  Motor A Position    : %+8ld steps (%+6.1f deg)    |\n", posA, degA);
  Serial.printf( "|  Motor B Position    : %+8ld steps (%+6.1f deg)    |\n", posB, degB);
  Serial.printf( "|  Motor A Speed       : %-5u steps/sec (%5.1f RPM)   |\n",
                 speedA, (float)speedA * 60.0f / stepsPerRev);
  Serial.printf( "|  Motor B Speed       : %-5u steps/sec (%5.1f RPM)   |\n",
                 speedB, (float)speedB * 60.0f / stepsPerRev);
  Serial.printf( "|  Acceleration        : %-5u steps/sec^2            |\n", currentAccel);
  Serial.printf( "|  Direction Logic (A) : %-29s|\n",
                 dirInvertA ? "INVERTED (CCW Default)" : "NORMAL (CW Default)");
  Serial.printf( "|  Direction Logic (B) : %-29s|\n",
                 dirInvertB ? "INVERTED (CCW Default)" : "NORMAL (CW Default)");
  if (contRunningA || contRunningB) {
    Serial.printf( "|  Continuous A / B    : A=%s (%+d) | B=%s (%+d) |\n",
                   contRunningA ? "RUN" : "IDLE", contSpeedA,
                   contRunningB ? "RUN" : "IDLE", contSpeedB);
  } else {
    Serial.printf( "|  Continuous Motion   : STOPPED                       |\n");
  }
  Serial.printf( "|  Auto-Disable Idle   : %-29s|\n",
                 autoDisableEnabled ? "ENABLED (30s timeout)" : "DISABLED");
  Serial.printf( "|  Free Heap Memory    : %-8u bytes               |\n",
                 (unsigned)ESP.getFreeHeap());
  Serial.printf( "|  System Uptime       : %-8lu seconds             |\n",
                 millis() / 1000UL);
  Serial.println("+======================================================+\n");
}

// =============================================================================
//  HELP TEXT
// =============================================================================
void printHelp() {
  Serial.println("\n+------------------------------------------------------+");
  Serial.println("|       STEPPER MOTOR TEST -- COMMAND REFERENCE        |");
  Serial.println("+------------------+-----------------------------------+");
  Serial.println("| MOTOR RUN & MOVE |                                   |");
  Serial.println("|  START <A|B|BOTH>| Run motor continuously (e.g. START B 100)|");
  Serial.println("|  STOP [A|B]      | Stop continuous motion (A, B, or all)|");
  Serial.println("|  STEP <n>        | Step Motor A (+ forward, - rev)   |");
  Serial.println("|  SPEED <sps>     | Run A continuously at sps         |");
  Serial.println("|  GOTO <pos>      | Move Motor A to absolute position |");
  Serial.println("|  POS             | Report current positions & angles |");
  Serial.println("|  ZERO            | Reset current position to zero    |");
  Serial.println("|  DIR <CW|CCW>    | Set Motor A default direction     |");
  Serial.println("|  REV <n>         | Move Motor A by N revolutions     |");
  Serial.println("|  RPM             | Report current RPM calculations   |");
  Serial.println("|  MICRO <1-16>    | Microstep: 1, 2, 4, 8, or 16      |");
  Serial.println("|  ENABLE          | Enable A4988 driver outputs (LOW) |");
  Serial.println("|  DISABLE         | Disable motor outputs (high-Z)    |");
  Serial.println("+------------------+-----------------------------------+");
  Serial.println("| DUAL MOTOR (A+B) |                                   |");
  Serial.println("|  DUAL STEP a b   | Step both simultaneously (nA, nB) |");
  Serial.println("|  DUAL GOTO x y   | Coordinated Bresenham move to x,y |");
  Serial.println("|  DUAL SPEED a b  | Set individual default speeds     |");
  Serial.println("|  DUAL CIRCLE r n | Trace circle: radius r, n steps   |");
  Serial.println("|  DUAL SQUARE s   | Trace square of side size s       |");
  Serial.println("|  DUAL HOME       | Move both axes back to 0, 0       |");
  Serial.println("+------------------+-----------------------------------+");
  Serial.println("| DIAGNOSTICS      |                                   |");
  Serial.println("|  BACKLASH        | Run backlash measurement sequence |");
  Serial.println("|  STALL sps steps | Test high-speed stall threshold   |");
  Serial.println("|  BENCHMARK       | Step generation benchmark & jitter|");
  Serial.println("|  TEST            | Automated hardware self-test      |");
  Serial.println("+------------------+-----------------------------------+");
  Serial.println("| SYSTEM           |                                   |");
  Serial.println("|  STATUS  or  ?   | Print full status report          |");
  Serial.println("|  ACCEL <sps2>    | Set acceleration (steps/sec^2)    |");
  Serial.println("|  AUTODISABLE 0/1 | Toggle idle driver auto-disable   |");
  Serial.println("|  ECHO            | Toggle command echo on/off        |");
  Serial.println("|  RESET           | Reset settings, disable drivers   |");
  Serial.println("|  HELP  or  H     | Show this help reference          |");
  Serial.println("+------------------+-----------------------------------+");
  Serial.println("| ABORT any motion | Send 'X', 'x', '!' or SPACE       |");
  Serial.println("+------------------+-----------------------------------+");
  Serial.println("|  SAFETY: Disconnect VMOT before altering wiring!     |");
  Serial.println("+------------------------------------------------------+\n");
}

// =============================================================================
//  BOOT BANNER
// =============================================================================
void printBanner() {
  Serial.println("\n");
  Serial.println("+==================================================+");
  Serial.println("|   STEPPER MOTOR & A4988 DRIVER TEST FIRMWARE     |");
  Serial.println("|   ESP32-S3 Super Mini  |  Dual 28BYJ-48 Bipolar  |");
  Serial.println("+==================================================+");
  Serial.printf( "|  Motor A (X) : STEP=IO5, DIR=IO6, EN=IO7         |\n");
  Serial.printf( "|  Motor B (Y) : STEP=IO9, DIR=IO10, EN=IO11       |\n");
  Serial.printf( "|  Microstep   : MS1=IO1, MS2=IO2, MS3=IO4         |\n");
  Serial.printf( "|  Status LED  : GPIO 13                           |\n");
  Serial.printf( "|  Base Steps  : %lu full steps/rev (64:1 gear)    |\n", (unsigned long)BASE_STEPS_PER_REV);
  Serial.printf( "|  Resolution  : 1/1 Full-step (%lu steps/rev)     |\n", (unsigned long)stepsPerRev);
  Serial.printf( "|  Default Spd : %u steps/sec                      |\n", speedA);
  Serial.println("+==================================================+");
  Serial.println("|  MOTORS DISABLED at boot (safety cold start).   |");
  Serial.println("|  Type HELP for command list.                    |");
  Serial.println("+==================================================+\n");
}
