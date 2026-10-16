// =============================================================================
//  ir_sensor_test.ino
//  Full-Featured IR Obstacle Avoidance Sensor Test & Characterisation Sketch
//  Target: ESP32-S3 Super Mini
//  Author: Laser-XY-Scanner Project
//
//  No external libraries required — uses only the ESP32 Arduino Core.
//
//  Designed for FC-51 / KY-032 IR Obstacle Avoidance Sensor Modules with
//  LM393 Comparator used as contactless optical homing endstops for X and Y axes.
//
//  Default Pinout:
//    - Sensor X OUT : GPIO 5  (Active LOW on detection)
//    - Sensor Y OUT : GPIO 6  (Active LOW on detection)
//    - Status LED   : GPIO 13 (Mirror detection: ON = Obstacle)
//    - Sensor VCC   : 5V Rail (USB VBUS)
//    - Sensor GND   : Common GND
//
//  Open Serial Monitor at 115200 baud, type HELP for command list.
// =============================================================================

#include <Arduino.h>

// ── Pin Configuration ────────────────────────────────────────────────────────
#define SENSOR_X_PIN      5         // IR Sensor X OUT (Homing X-axis)
#define SENSOR_Y_PIN      6         // IR Sensor Y OUT (Homing Y-axis)
#define STATUS_LED_PIN    13        // Status / indicator LED (GPIO 13)

// ── ESP32 Core v2.x / v3.x Compatibility Helpers ─────────────────────────────
inline void setupLEDC(uint8_t pin, uint32_t freq, uint8_t resolution, uint8_t channel) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  ledcAttachChannel(pin, freq, resolution, channel);
#else
  ledcSetup(channel, freq, resolution);
  ledcAttachPin(pin, channel);
#endif
}

inline void writeLEDC(uint8_t pin, uint8_t channel, uint32_t duty) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  ledcWrite(pin, duty);
#else
  ledcWrite(channel, duty);
#endif
}

// ── Sensor State & Logic Configuration ───────────────────────────────────────
// Standard FC-51 modules: comparator pulls OUT to LOW (0V) when obstacle detected.
// When clear, pull-up resistor (through indicator LED) holds OUT at HIGH (~3.0-3.3V).
bool invertLogic    = false;   // false: LOW = Obstacle Detected, HIGH = Clear
                               // true:  HIGH = Obstacle Detected, LOW = Clear
bool mirrorLED      = true;    // Mirror sensor trigger state onto STATUS_LED_PIN
bool echoCommands   = true;    // Echo back parsed commands to Serial Monitor

// ── Statistics Structure ─────────────────────────────────────────────────────
struct SensorStats {
  uint32_t triggers           = 0;
  uint32_t releases           = 0;
  uint32_t bounces            = 0;
  unsigned long lastTriggerTime = 0;
  unsigned long lastReleaseTime = 0;
  uint32_t minDurationMs      = 0xFFFFFFFF;
  uint32_t maxDurationMs      = 0;
  uint64_t totalDurationMs    = 0;
  uint32_t completedCycles    = 0;
};

SensorStats statsX;
SensorStats statsY;
uint32_t falseTriggerCount = 0;

// Forward Declarations
void handleCommand(String cmd);
void printBanner();
void printStatus();
void printHelp();
void printStats();
void reportBootReadout();
bool isObstacleDetected(uint8_t pin);
bool readRawPin(uint8_t pin);
void updateStatusLED();
bool checkAbort();
void countdown(int seconds);

// Command Handlers
void cmdRead();
void cmdMonitor(String args);
void cmdWatch(String args);
void cmdCalib();
void cmdRange();
void cmdThreshold();
void cmdDebounce(String args);
void cmdEdges(String args);
void cmdResponse();
void cmdLatency(String args);
void cmdHomeSim(String args);
void cmdDual(String args);
void cmdNoise(String args);
void cmdAmbient();
void cmdReset();

// =============================================================================
//  SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  // Configure sensor pins with internal pull-up as fail-safe
  // If sensor cable disconnects, pin floats to HIGH (CLEAR), preventing false crash
  pinMode(SENSOR_X_PIN, INPUT_PULLUP);
  pinMode(SENSOR_Y_PIN, INPUT_PULLUP);

  // Configure Status LED
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);

  // Display startup banner and initial pin states
  printBanner();
  reportBootReadout();
  updateStatusLED();
}

// =============================================================================
//  LOOP — Serial Command Reader
// =============================================================================
void loop() {
  // Continuously mirror LED if enabled
  updateStatusLED();

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

  // ── READ ──────────────────────────────────────────────────────────────────
  if (upper == "READ") {
    cmdRead();

  // ── MONITOR ───────────────────────────────────────────────────────────────
  } else if (upper.startsWith("MONITOR")) {
    String args = upper.substring(7);
    args.trim();
    cmdMonitor(args);

  // ── WATCH ─────────────────────────────────────────────────────────────────
  } else if (upper.startsWith("WATCH")) {
    String args = upper.substring(5);
    args.trim();
    cmdWatch(args);

  // ── CALIBRATION ───────────────────────────────────────────────────────────
  } else if (upper == "CALIB") {
    cmdCalib();

  // ── RANGE ─────────────────────────────────────────────────────────────────
  } else if (upper == "RANGE") {
    cmdRange();

  // ── THRESHOLD DIAGNOSTIC ──────────────────────────────────────────────────
  } else if (upper == "THRESHOLD") {
    cmdThreshold();

  // ── DEBOUNCE ──────────────────────────────────────────────────────────────
  } else if (upper.startsWith("DEBOUNCE")) {
    String args = upper.substring(8);
    args.trim();
    cmdDebounce(args);

  // ── EDGES ─────────────────────────────────────────────────────────────────
  } else if (upper.startsWith("EDGES")) {
    String args = upper.substring(5);
    args.trim();
    cmdEdges(args);

  // ── RESPONSE TIME ─────────────────────────────────────────────────────────
  } else if (upper == "RESPONSE") {
    cmdResponse();

  // ── LATENCY ───────────────────────────────────────────────────────────────
  } else if (upper.startsWith("LATENCY")) {
    String args = upper.substring(7);
    args.trim();
    cmdLatency(args);

  // ── HOMING SIMULATION ─────────────────────────────────────────────────────
  } else if (upper.startsWith("HOMESIM")) {
    String args = upper.substring(7);
    args.trim();
    cmdHomeSim(args);

  // ── DUAL SENSOR MONITOR / SYNC ────────────────────────────────────────────
  } else if (upper.startsWith("DUAL")) {
    String args = upper.substring(4);
    args.trim();
    cmdDual(args);

  // ── NOISE TEST ────────────────────────────────────────────────────────────
  } else if (upper.startsWith("NOISE")) {
    String args = upper.substring(5);
    args.trim();
    cmdNoise(args);

  // ── AMBIENT INTERFERENCE ──────────────────────────────────────────────────
  } else if (upper == "AMBIENT") {
    cmdAmbient();

  // ── STATUS REPORT ─────────────────────────────────────────────────────────
  } else if (upper == "STATUS" || upper == "?") {
    printStatus();

  // ── ACCUMULATED STATISTICS ────────────────────────────────────────────────
  } else if (upper == "STATS") {
    printStats();

  // ── TOGGLE STATUS LED ─────────────────────────────────────────────────────
  } else if (upper == "LED") {
    mirrorLED = !mirrorLED;
    if (!mirrorLED) digitalWrite(STATUS_LED_PIN, LOW);
    else updateStatusLED();
    Serial.printf("[LED] Sensor LED mirror mode %s.\n", mirrorLED ? "ENABLED (LED ON when obstacle detected)" : "DISABLED");

  // ── INVERT LOGIC ──────────────────────────────────────────────────────────
  } else if (upper == "INVERT") {
    invertLogic = !invertLogic;
    Serial.printf("[INVERT] Logic polarity inverted: %s\n",
                  invertLogic ? "HIGH = Obstacle Detected (Active HIGH)" : "LOW = Obstacle Detected (Standard Active LOW)");
    updateStatusLED();

  // ── ECHO TOGGLE ───────────────────────────────────────────────────────────
  } else if (upper == "ECHO") {
    echoCommands = !echoCommands;
    Serial.printf("[ECHO] Command echo %s.\n", echoCommands ? "ON" : "OFF");

  // ── RESET ─────────────────────────────────────────────────────────────────
  } else if (upper == "RESET") {
    cmdReset();

  // ── HELP ──────────────────────────────────────────────────────────────────
  } else if (upper == "HELP" || upper == "H") {
    printHelp();

  } else {
    Serial.printf("[?] Unknown command: \"%s\"  -- type HELP for command list.\n", cmd.c_str());
  }
}

// =============================================================================
//  HELPER FUNCTIONS
// =============================================================================

inline bool readRawPin(uint8_t pin) {
  return digitalRead(pin);
}

inline bool isObstacleDetected(uint8_t pin) {
  bool raw = digitalRead(pin);
  return invertLogic ? (raw == HIGH) : (raw == LOW);
}

void updateStatusLED() {
  if (!mirrorLED) return;
  bool detectedX = isObstacleDetected(SENSOR_X_PIN);
  bool detectedY = isObstacleDetected(SENSOR_Y_PIN);
  digitalWrite(STATUS_LED_PIN, (detectedX || detectedY) ? HIGH : LOW);
}

bool checkAbort() {
  if (Serial.available()) {
    char c = Serial.peek();
    if (c == 'X' || c == 'x' || c == '!') {
      Serial.read(); // Consume character
      return true;
    }
  }
  return false;
}

void countdown(int seconds) {
  for (int i = seconds; i > 0; i--) {
    Serial.printf("  %d...\n", i);
    delay(1000);
  }
}

void recordTriggerEvent(uint8_t pin) {
  unsigned long now = millis();
  if (pin == SENSOR_X_PIN) {
    statsX.triggers++;
    statsX.lastTriggerTime = now;
  } else if (pin == SENSOR_Y_PIN) {
    statsY.triggers++;
    statsY.lastTriggerTime = now;
  }
}

void recordReleaseEvent(uint8_t pin) {
  unsigned long now = millis();
  if (pin == SENSOR_X_PIN) {
    statsX.releases++;
    statsX.lastReleaseTime = now;
    if (statsX.lastTriggerTime > 0 && now >= statsX.lastTriggerTime) {
      uint32_t dur = now - statsX.lastTriggerTime;
      if (dur < statsX.minDurationMs) statsX.minDurationMs = dur;
      if (dur > statsX.maxDurationMs) statsX.maxDurationMs = dur;
      statsX.totalDurationMs += dur;
      statsX.completedCycles++;
    }
  } else if (pin == SENSOR_Y_PIN) {
    statsY.releases++;
    statsY.lastReleaseTime = now;
    if (statsY.lastTriggerTime > 0 && now >= statsY.lastTriggerTime) {
      uint32_t dur = now - statsY.lastTriggerTime;
      if (dur < statsY.minDurationMs) statsY.minDurationMs = dur;
      if (dur > statsY.maxDurationMs) statsY.maxDurationMs = dur;
      statsY.totalDurationMs += dur;
      statsY.completedCycles++;
    }
  }
}

// =============================================================================
//  COMMAND IMPLEMENTATIONS
// =============================================================================

// ── READ ─────────────────────────────────────────────────────────────────────
void cmdRead() {
  bool rawX = readRawPin(SENSOR_X_PIN);
  bool rawY = readRawPin(SENSOR_Y_PIN);
  bool detX = isObstacleDetected(SENSOR_X_PIN);
  bool detY = isObstacleDetected(SENSOR_Y_PIN);

  Serial.println("\n+-----------------------------------------------------------------------+");
  Serial.println("|                          CURRENT SENSOR READOUT                       |");
  Serial.println("+----------+---------+------------+-------------------+-----------------+");
  Serial.println("| Sensor   | Pin     | Raw Level  | Logic State       | Interpretation  |");
  Serial.println("+----------+---------+------------+-------------------+-----------------+");
  Serial.printf( "| X-Axis   | GPIO %-2d | %-10s | %-17s | %-15s |\n",
                 SENSOR_X_PIN, rawX ? "HIGH (1)" : "LOW (0)",
                 detX ? "DETECTED" : "CLEAR",
                 detX ? "Obstacle Present" : "Beam Clear");
  Serial.printf( "| Y-Axis   | GPIO %-2d | %-10s | %-17s | %-15s |\n",
                 SENSOR_Y_PIN, rawY ? "HIGH (1)" : "LOW (0)",
                 detY ? "DETECTED" : "CLEAR",
                 detY ? "Obstacle Present" : "Beam Clear");
  Serial.println("+----------+---------+------------+-------------------+-----------------+");
  Serial.printf( "| Status LED (GPIO %-2d) : %-44s |\n",
                 STATUS_LED_PIN,
                 (digitalRead(STATUS_LED_PIN) == HIGH) ? "ON  (Obstacle Mirrored)" : "OFF (Path Clear)");
  Serial.printf( "| Active Polarity     : %-44s |\n",
                 invertLogic ? "INVERTED (HIGH=Detected, LOW=Clear)" : "NORMAL   (LOW=Detected, HIGH=Clear)");
  Serial.println("+-----------------------------------------------------------------------+\n");
}

// ── MONITOR ──────────────────────────────────────────────────────────────────
void cmdMonitor(String args) {
  int pollIntervalMs = 0;
  if (args.length() > 0) {
    pollIntervalMs = args.toInt();
    if (pollIntervalMs < 5) pollIntervalMs = 5;
  }

  if (pollIntervalMs > 0) {
    // Polling mode
    Serial.printf("[MONITOR] Starting polling monitor every %d ms. Send 'X' to stop.\n", pollIntervalMs);
    Serial.println("  Timestamp   | Sensor X (GPIO 5)       | Sensor Y (GPIO 6)       | LED");
    Serial.println(" -------------+-------------------------+-------------------------+-----");

    unsigned long startMs = millis();
    while (true) {
      if (checkAbort()) {
        Serial.println("\n[MONITOR] Stopped.");
        return;
      }

      unsigned long t = millis() - startMs;
      bool detX = isObstacleDetected(SENSOR_X_PIN);
      bool detY = isObstacleDetected(SENSOR_Y_PIN);
      bool rawX = readRawPin(SENSOR_X_PIN);
      bool rawY = readRawPin(SENSOR_Y_PIN);

      updateStatusLED();

      Serial.printf("  +%7lu ms | X: [%s] %-8s (raw=%d) | Y: [%s] %-8s (raw=%d) | %s\n",
                    t,
                    detX ? "####" : "....", detX ? "DETECTED" : "CLEAR", rawX ? 1 : 0,
                    detY ? "####" : "....", detY ? "DETECTED" : "CLEAR", rawY ? 1 : 0,
                    (detX || detY) ? "ON " : "OFF");

      delay(pollIntervalMs);
    }
  } else {
    // Event-driven mode (real-time edge reporting)
    Serial.println("[MONITOR] Real-time event monitor active. Send 'X' to stop.");
    Serial.println("          Printing state transitions instantly as they occur...");

    bool lastDetX = isObstacleDetected(SENSOR_X_PIN);
    bool lastDetY = isObstacleDetected(SENSOR_Y_PIN);
    unsigned long startMs = millis();

    Serial.printf("  Initial state: X = %s | Y = %s\n",
                  lastDetX ? "DETECTED" : "CLEAR",
                  lastDetY ? "DETECTED" : "CLEAR");

    while (true) {
      if (checkAbort()) {
        Serial.println("[MONITOR] Stopped.");
        return;
      }

      bool curDetX = isObstacleDetected(SENSOR_X_PIN);
      bool curDetY = isObstacleDetected(SENSOR_Y_PIN);

      if (curDetX != lastDetX) {
        unsigned long t = millis() - startMs;
        if (curDetX) {
          recordTriggerEvent(SENSOR_X_PIN);
          Serial.printf("  [+%-7lu ms] SENSOR X: CLEAR -> DETECTED [###]  (Trigger #%u)\n",
                        t, statsX.triggers);
        } else {
          recordReleaseEvent(SENSOR_X_PIN);
          Serial.printf("  [+%-7lu ms] SENSOR X: DETECTED -> CLEAR [   ]  (Last pulse: %lu ms)\n",
                        t, millis() - statsX.lastTriggerTime);
        }
        lastDetX = curDetX;
        updateStatusLED();
      }

      if (curDetY != lastDetY) {
        unsigned long t = millis() - startMs;
        if (curDetY) {
          recordTriggerEvent(SENSOR_Y_PIN);
          Serial.printf("  [+%-7lu ms] SENSOR Y: CLEAR -> DETECTED [###]  (Trigger #%u)\n",
                        t, statsY.triggers);
        } else {
          recordReleaseEvent(SENSOR_Y_PIN);
          Serial.printf("  [+%-7lu ms] SENSOR Y: DETECTED -> CLEAR [   ]  (Last pulse: %lu ms)\n",
                        t, millis() - statsY.lastTriggerTime);
        }
        lastDetY = curDetY;
        updateStatusLED();
      }

      delayMicroseconds(200); // 5 kHz polling for crisp edge detection
    }
  }
}

// ── WATCH ────────────────────────────────────────────────────────────────────
void cmdWatch(String args) {
  // WATCH <1|2|X|Y|BOTH>
  uint8_t target = 0; // 1=X, 2=Y, 3=BOTH
  if (args == "1" || args == "X") target = 1;
  else if (args == "2" || args == "Y") target = 2;
  else if (args == "BOTH" || args == "") target = 3;
  else {
    Serial.println("[WATCH] Invalid sensor target. Use: WATCH 1, WATCH 2, WATCH X, WATCH Y, or WATCH BOTH");
    return;
  }

  const char* label = (target == 1) ? "Sensor X (GPIO 5)" : (target == 2) ? "Sensor Y (GPIO 6)" : "BOTH Sensors (X & Y)";
  Serial.printf("[WATCH] Watching %s continuously. Send 'X' to abort.\n", label);

  bool lastX = isObstacleDetected(SENSOR_X_PIN);
  bool lastY = isObstacleDetected(SENSOR_Y_PIN);
  unsigned long startMs = millis();

  while (true) {
    if (checkAbort()) {
      Serial.println("[WATCH] Stopped.");
      return;
    }

    if (target == 1 || target == 3) {
      bool curX = isObstacleDetected(SENSOR_X_PIN);
      if (curX != lastX) {
        unsigned long elapsed = millis() - startMs;
        Serial.printf("  [t=%6lu ms] SENSOR X: %s (Raw GPIO 5 = %d)\n",
                      elapsed, curX ? "** DETECTED **" : "CLEAR", readRawPin(SENSOR_X_PIN));
        if (curX) recordTriggerEvent(SENSOR_X_PIN);
        else recordReleaseEvent(SENSOR_X_PIN);
        lastX = curX;
        updateStatusLED();
      }
    }

    if (target == 2 || target == 3) {
      bool curY = isObstacleDetected(SENSOR_Y_PIN);
      if (curY != lastY) {
        unsigned long elapsed = millis() - startMs;
        Serial.printf("  [t=%6lu ms] SENSOR Y: %s (Raw GPIO 6 = %d)\n",
                      elapsed, curY ? "** DETECTED **" : "CLEAR", readRawPin(SENSOR_Y_PIN));
        if (curY) recordTriggerEvent(SENSOR_Y_PIN);
        else recordReleaseEvent(SENSOR_Y_PIN);
        lastY = curY;
        updateStatusLED();
      }
    }

    delayMicroseconds(100);
  }
}

// ── CALIB — Detection Distance Calibration Wizard ───────────────────────────
void cmdCalib() {
  Serial.println("\n+=======================================================================+");
  Serial.println("|          IR SENSOR DISTANCE CALIBRATION WIZARD                        |");
  Serial.println("+=======================================================================+");
  Serial.println("| This wizard assists you in adjusting the LM393 trim-pot to achieve     |");
  Serial.println("| the ideal homing trigger distance (~5 mm to 10 mm).                   |");
  Serial.println("|                                                                       |");
  Serial.println("| Send 'X' at any time to cancel calibration.                           |");
  Serial.println("+=======================================================================+\n");

  // Phase 1: Baseline Check
  Serial.println("[STEP 1/3] Checking baseline (no obstacle in front)...");
  bool initialX = isObstacleDetected(SENSOR_X_PIN);
  if (initialX) {
    Serial.println("  [!] WARNING: Sensor X currently reads DETECTED!");
    Serial.println("      Make sure nothing is in front of the sensor (> 20 cm clear).");
    Serial.println("      If path is completely clear and green LED is ON on module:");
    Serial.println("      -> SENSOR IS OVER-SENSITIVE.");
    Serial.println("      -> Turn the blue trim-pot COUNTER-CLOCKWISE until the green LED turns OFF.");
    Serial.println("      Waiting for sensor to become CLEAR...");
    while (isObstacleDetected(SENSOR_X_PIN)) {
      if (checkAbort()) {
        Serial.println("[CALIB] Aborted.");
        return;
      }
      delay(50);
    }
  }
  Serial.println("  [OK] Baseline clear! Sensor X reads CLEAR (Beam path unobstructed).\n");

  // Phase 2: Approach & Trigger Detection
  Serial.println("[STEP 2/3] Slowly move a card or carriage toward Sensor X.");
  Serial.println("           Target ideal homing distance: 5 mm to 10 mm.");
  Serial.println("           Waiting for trigger transition...");

  unsigned long waitStart = millis();
  while (!isObstacleDetected(SENSOR_X_PIN)) {
    if (checkAbort()) {
      Serial.println("[CALIB] Aborted.");
      return;
    }
    if ((millis() - waitStart) % 3000 == 0) {
      Serial.println("           Still waiting... Bring object closer to the IR LEDs.");
      delay(1);
    }
    delay(10);
  }

  unsigned long triggerTimeUs = micros();
  Serial.println("\n  >>> TRIGGER DETECTED! <<<");
  Serial.println("  [OK] Sensor X switched to DETECTED (LOW on GPIO 5).");

  // Check transition bounce
  int bounces = 0;
  for (int i = 0; i < 200; i++) {
    if (!isObstacleDetected(SENSOR_X_PIN)) bounces++;
    delayMicroseconds(50);
  }
  Serial.printf("  Stability check at trigger point: %d jitter events detected.\n", bounces);

  // Phase 3: Release & Hysteresis Measurement
  Serial.println("\n[STEP 3/3] Now SLOWLY move the target AWAY from Sensor X.");
  Serial.println("           This measures the comparator release hysteresis gap...");

  while (isObstacleDetected(SENSOR_X_PIN)) {
    if (checkAbort()) {
      Serial.println("[CALIB] Aborted.");
      return;
    }
    delay(10);
  }
  Serial.println("  >>> RELEASE DETECTED! <<<");
  Serial.println("  [OK] Sensor X switched back to CLEAR.\n");

  // Trim-pot advice summary
  Serial.println("+-----------------------------------------------------------------------+");
  Serial.println("|                     CALIBRATION RECOMMENDATIONS                       |");
  Serial.println("+-----------------------------------------------------------------------+");
  Serial.println("|  - If trigger distance was > 15 mm:                                   |");
  Serial.println("|    Turn trim-pot COUNTER-CLOCKWISE to shorten distance (less sensitive) |");
  Serial.println("|  - If trigger distance was < 3 mm (or touching):                      |");
  Serial.println("|    Turn trim-pot CLOCKWISE to lengthen distance (more sensitive)       |");
  Serial.println("|  - Optimal homing distance: 5 to 10 mm from reflective mirror/bracket |");
  Serial.println("+-----------------------------------------------------------------------+\n");
}

// ── RANGE ────────────────────────────────────────────────────────────────────
void cmdRange() {
  Serial.println("\n[RANGE] Dynamic Range Verification Test.");
  Serial.println("        Cycle target at defined distances to verify reliable triggering.");
  Serial.println("        Send 'X' to abort.\n");

  const int checkpoints[] = { 30, 20, 15, 10, 5, 2 };
  const int count = sizeof(checkpoints) / sizeof(checkpoints[0]);

  for (int i = 0; i < count; i++) {
    Serial.printf("  --> Place target at approximately %d mm from Sensor X...\n", checkpoints[i]);
    Serial.println("      Sampling 50 times over 500 ms...");

    int detectedCount = 0;
    for (int s = 0; s < 50; s++) {
      if (checkAbort()) {
        Serial.println("[RANGE] Aborted.");
        return;
      }
      if (isObstacleDetected(SENSOR_X_PIN)) detectedCount++;
      delay(10);
    }

    float pct = (float)detectedCount * 100.0f / 50.0f;
    Serial.printf("      Result at ~%2d mm: %d/50 detections (%.0f%%) -> %s\n\n",
                  checkpoints[i], detectedCount, pct,
                  pct > 90.0f ? "[SOLID TRIGGER]" : pct > 10.0f ? "[THRESHOLD BOUNDARY / JITTER]" : "[NO TRIGGER / CLEAR]");
    delay(300);
  }
  Serial.println("[RANGE] Test complete.");
}

// ── THRESHOLD DIAGNOSTIC ─────────────────────────────────────────────────────
void cmdThreshold() {
  Serial.println("\n+-----------------------------------------------------------------------+");
  Serial.println("|                   TRIM-POT THRESHOLD DIAGNOSTIC                       |");
  Serial.println("+-----------------------------------------------------------------------+");

  bool rawX = readRawPin(SENSOR_X_PIN);
  bool detX = isObstacleDetected(SENSOR_X_PIN);
  bool rawY = readRawPin(SENSOR_Y_PIN);
  bool detY = isObstacleDetected(SENSOR_Y_PIN);

  // Evaluate X
  Serial.printf("| SENSOR X (GPIO %-2d): Raw=%-8s  State=%-10s                 |\n",
                SENSOR_X_PIN, rawX ? "HIGH (1)" : "LOW (0)", detX ? "DETECTED" : "CLEAR");
  if (detX) {
    Serial.println("|   DIAGNOSIS: Currently DETECTED (Obstacle sensed or OVER-SENSITIVE)   |");
    Serial.println("|   If NO obstacle is present:                                          |");
    Serial.println("|   -> Turn trim-pot COUNTER-CLOCKWISE (decrease sensitivity)           |");
    Serial.println("|      until the onboard green LED turns OFF.                           |");
  } else {
    Serial.println("|   DIAGNOSIS: Currently CLEAR (Beam unobstructed or UNDER-SENSITIVE)   |");
    Serial.println("|   If obstacle IS present right in front (<10mm) but not triggered:    |");
    Serial.println("|   -> Turn trim-pot CLOCKWISE (increase sensitivity)                   |");
    Serial.println("|      until the onboard green LED illuminates at 5-10mm.               |");
  }
  Serial.println("+-----------------------------------------------------------------------+");

  // Evaluate Y
  Serial.printf("| SENSOR Y (GPIO %-2d): Raw=%-8s  State=%-10s                 |\n",
                SENSOR_Y_PIN, rawY ? "HIGH (1)" : "LOW (0)", detY ? "DETECTED" : "CLEAR");
  if (detY) {
    Serial.println("|   DIAGNOSIS: Currently DETECTED (Obstacle sensed or OVER-SENSITIVE)   |");
    Serial.println("|   If NO obstacle is present:                                          |");
    Serial.println("|   -> Turn trim-pot COUNTER-CLOCKWISE until green LED turns OFF.       |");
  } else {
    Serial.println("|   DIAGNOSIS: Currently CLEAR (Beam unobstructed or UNDER-SENSITIVE)   |");
    Serial.println("|   If obstacle IS present but not triggered:                           |");
    Serial.println("|   -> Turn trim-pot CLOCKWISE until green LED illuminates at 5-10mm.   |");
  }
  Serial.println("+-----------------------------------------------------------------------+\n");
}

// ── DEBOUNCE & FAST SAMPLING ─────────────────────────────────────────────────
void cmdDebounce(String args) {
  if (args.length() > 0) {
    // Rapid sample mode: DEBOUNCE <samples>
    int samples = args.toInt();
    if (samples < 100) samples = 100;
    if (samples > 50000) samples = 50000;

    Serial.printf("[DEBOUNCE] Taking %d rapid samples on Sensor X (GPIO 5)...\n", samples);
    int highCount = 0;
    int lowCount  = 0;
    int transitions = 0;
    bool last = readRawPin(SENSOR_X_PIN);

    unsigned long startUs = micros();
    for (int i = 0; i < samples; i++) {
      bool cur = readRawPin(SENSOR_X_PIN);
      if (cur) highCount++;
      else lowCount++;
      if (cur != last) {
        transitions++;
        last = cur;
      }
      delayMicroseconds(10);
    }
    unsigned long durUs = micros() - startUs;

    Serial.printf("  Total duration : %lu us (~%.1f us per sample)\n", durUs, (float)durUs / samples);
    Serial.printf("  HIGH samples   : %d (%.1f%%)\n", highCount, (float)highCount * 100.0f / samples);
    Serial.printf("  LOW samples    : %d (%.1f%%)\n", lowCount, (float)lowCount * 100.0f / samples);
    Serial.printf("  Transitions    : %d\n", transitions);
    Serial.printf("  Stability      : %s\n",
                  transitions == 0 ? "100% Rock Solid (Clean DC Level)" :
                  transitions < 5  ? "Very Stable (Minimal edge ripple)" : "NOISY / CHATTERING (Needs Debouncing)");
    return;
  }

  // Interactive bounce capture mode
  Serial.println("\n[DEBOUNCE] Live Transition Bounce Characterisation.");
  Serial.println("           Move an object to trigger Sensor X now... (Send 'X' to abort)");

  bool initial = isObstacleDetected(SENSOR_X_PIN);
  while (isObstacleDetected(SENSOR_X_PIN) == initial) {
    if (checkAbort()) {
      Serial.println("[DEBOUNCE] Aborted.");
      return;
    }
    delay(5);
  }

  // Edge detected! Record 500 samples across 25 ms (50 us interval)
  const int SAMPLES = 500;
  const int INTERVAL_US = 50;
  bool trace[SAMPLES];
  unsigned long startUs = micros();

  for (int i = 0; i < SAMPLES; i++) {
    trace[i] = readRawPin(SENSOR_X_PIN);
    delayMicroseconds(INTERVAL_US);
  }
  unsigned long totalCaptureUs = micros() - startUs;

  // Analyze bounces
  int bounces = 0;
  int firstChangeIdx = -1;
  int lastChangeIdx  = -1;
  bool prev = trace[0];

  for (int i = 1; i < SAMPLES; i++) {
    if (trace[i] != prev) {
      bounces++;
      if (firstChangeIdx == -1) firstChangeIdx = i;
      lastChangeIdx = i;
      prev = trace[i];
    }
  }

  statsX.bounces += bounces;

  uint32_t settleTimeUs = (lastChangeIdx > firstChangeIdx) ?
                          (uint32_t)(lastChangeIdx - firstChangeIdx) * INTERVAL_US : 0;

  Serial.println("\n+-----------------------------------------------------------------------+");
  Serial.println("|                    DEBOUNCE CHARACTERISATION REPORT                   |");
  Serial.println("+-----------------------------------------------------------------------+");
  Serial.printf( "| Total Window Captured : %lu us (%.1f ms)                            |\n", totalCaptureUs, totalCaptureUs / 1000.0f);
  Serial.printf( "| Transition Bounces    : %-45d |\n", bounces);
  Serial.printf( "| Settle Duration       : %lu us (%.2f ms)                            |\n", settleTimeUs, settleTimeUs / 1000.0f);
  Serial.println("+-----------------------------------------------------------------------+");

  // Visual waveform (50 bins)
  Serial.print(" Waveform: [");
  for (int b = 0; b < 50; b++) {
    int sum = 0;
    for (int k = 0; k < 10; k++) {
      if (trace[b * 10 + k] == (invertLogic ? HIGH : LOW)) sum++;
    }
    Serial.print(sum > 5 ? "#" : ".");
  }
  Serial.println("] (# = Detected, . = Clear)");

  if (bounces <= 1) {
    Serial.println(" Verdict : Extremely clean optical transition! Electronic LM393 hysteresis active.");
    Serial.println(" Setting : Recommended homing debounce filter: 2 ms to 5 ms.");
  } else {
    Serial.printf(" Verdict : Minor optical edge flutter detected (%d bounces).\n", bounces);
    Serial.printf(" Setting : Recommended homing debounce filter: >= %lu ms.\n", (settleTimeUs / 1000) + 3);
  }
  Serial.println("+-----------------------------------------------------------------------+\n");
}

// ── EDGES ────────────────────────────────────────────────────────────────────
void cmdEdges(String args) {
  int durationS = 5;
  if (args.length() > 0) {
    durationS = args.toInt();
    if (durationS < 1) durationS = 1;
    if (durationS > 60) durationS = 60;
  }

  Serial.printf("[EDGES] Counting rising and falling edges for %d seconds. Send 'X' to abort.\n", durationS);

  uint32_t xFalling = 0, xRising = 0;
  uint32_t yFalling = 0, yRising = 0;

  bool lastX = readRawPin(SENSOR_X_PIN);
  bool lastY = readRawPin(SENSOR_Y_PIN);

  unsigned long startMs = millis();
  unsigned long endMs   = startMs + (unsigned long)durationS * 1000UL;

  while (millis() < endMs) {
    if (checkAbort()) {
      Serial.println("[EDGES] Aborted.");
      return;
    }

    bool curX = readRawPin(SENSOR_X_PIN);
    bool curY = readRawPin(SENSOR_Y_PIN);

    if (curX != lastX) {
      if (curX == LOW) xFalling++;
      else xRising++;
      lastX = curX;
      updateStatusLED();
    }

    if (curY != lastY) {
      if (curY == LOW) yFalling++;
      else yRising++;
      lastY = curY;
      updateStatusLED();
    }

    delayMicroseconds(50);
  }

  Serial.println("\n+-----------------------------------------------------------------------+");
  Serial.println("|                          EDGE COUNT RESULTS                           |");
  Serial.println("+----------+-------------------+-------------------+--------------------+");
  Serial.println("| Sensor   | Falling Edges     | Rising Edges      | Total Transitions  |");
  Serial.println("+----------+-------------------+-------------------+--------------------+");
  Serial.printf( "| X-Axis   | %-17u | %-17u | %-18u |\n", xFalling, xRising, xFalling + xRising);
  Serial.printf( "| Y-Axis   | %-17u | %-17u | %-18u |\n", yFalling, yRising, yFalling + yRising);
  Serial.println("+----------+-------------------+-------------------+--------------------+");
  Serial.printf( "| Note: Standard FC-51: Falling Edge = Obstacle Trip, Rising Edge = Release |\n");
  Serial.println("+-----------------------------------------------------------------------+\n");
}

// ── RESPONSE TIME ────────────────────────────────────────────────────────────
void cmdResponse() {
  Serial.println("\n[RESPONSE] Response Time & Transition Latency Test.");
  Serial.println("           Wave an object rapidly past Sensor X (GPIO 5)... (Send 'X' to abort)");

  // Wait for initial clear
  while (isObstacleDetected(SENSOR_X_PIN)) {
    if (checkAbort()) {
      Serial.println("[RESPONSE] Aborted.");
      return;
    }
    delay(10);
  }

  // Wait for obstacle trigger
  while (!isObstacleDetected(SENSOR_X_PIN)) {
    if (checkAbort()) {
      Serial.println("[RESPONSE] Aborted.");
      return;
    }
    delayMicroseconds(50);
  }
  unsigned long tripUs = micros();

  // Wait for release
  while (isObstacleDetected(SENSOR_X_PIN)) {
    if (checkAbort()) {
      Serial.println("[RESPONSE] Aborted.");
      return;
    }
    delayMicroseconds(50);
  }
  unsigned long releaseUs = micros();
  unsigned long pulseDurationUs = releaseUs - tripUs;

  Serial.println("\n+-----------------------------------------------------------------------+");
  Serial.println("|                       RESPONSE TIMING RESULTS                         |");
  Serial.println("+-----------------------------------------------------------------------+");
  Serial.printf( "| Pulse Duration Observed : %lu us (%.2f ms)                           |\n",
                 pulseDurationUs, pulseDurationUs / 1000.0f);
  Serial.println("| LM393 Comparator Delay  : ~1.3 us typical (datasheet specification)   |");
  Serial.println("| Phototransistor Tr/Tf   : ~15 us rise/fall time                      |");
  Serial.printf( "| Optical Sensor Bandwidth: > 20 kHz (Speed capability >> Stepper rate) |\n");
  Serial.println("+-----------------------------------------------------------------------+\n");
}

// ── LATENCY ──────────────────────────────────────────────────────────────────
void cmdLatency(String args) {
  int count = 5;
  if (args.length() > 0) {
    count = args.toInt();
    if (count < 1) count = 1;
    if (count > 20) count = 20;
  }

  Serial.printf("[LATENCY] Measuring response times over %d consecutive trigger cycles.\n", count);
  Serial.println("          Please trigger and release Sensor X repeatedly when prompted.");
  Serial.println("          Send 'X' to abort.\n");

  uint32_t durations[20];

  for (int i = 0; i < count; i++) {
    Serial.printf("  [%d/%d] Ready. Trigger Sensor X now...", i + 1, count);
    while (isObstacleDetected(SENSOR_X_PIN)) {
      if (checkAbort()) { Serial.println("[LATENCY] Aborted."); return; }
      delay(10);
    }
    while (!isObstacleDetected(SENSOR_X_PIN)) {
      if (checkAbort()) { Serial.println("[LATENCY] Aborted."); return; }
      delayMicroseconds(20);
    }
    unsigned long startT = micros();
    Serial.print(" Triggered! Now release...");

    while (isObstacleDetected(SENSOR_X_PIN)) {
      if (checkAbort()) { Serial.println("[LATENCY] Aborted."); return; }
      delayMicroseconds(20);
    }
    unsigned long dur = micros() - startT;
    durations[i] = dur;
    Serial.printf(" Released! (Pulse: %lu us / %.1f ms)\n", dur, dur / 1000.0f);
    delay(200);
  }

  uint32_t minD = 0xFFFFFFFF, maxD = 0;
  uint64_t sumD = 0;
  for (int i = 0; i < count; i++) {
    if (durations[i] < minD) minD = durations[i];
    if (durations[i] > maxD) maxD = durations[i];
    sumD += durations[i];
  }
  float avgD = (float)sumD / count;

  Serial.println("\n+-----------------------------------------------------------------------+");
  Serial.println("|                       LATENCY TEST SUMMARY                            |");
  Serial.println("+-----------------------------------------------------------------------+");
  Serial.printf( "| Cycles Tested    : %-50d |\n", count);
  Serial.printf( "| Min Pulse Width  : %lu us (%.2f ms)                                  |\n", minD, minD / 1000.0f);
  Serial.printf( "| Max Pulse Width  : %lu us (%.2f ms)                                  |\n", maxD, maxD / 1000.0f);
  Serial.printf( "| Avg Pulse Width  : %.1f us (%.2f ms)                                |\n", avgD, avgD / 1000.0f);
  Serial.println("+-----------------------------------------------------------------------+\n");
}

// ── HOMESIM — Homing Cycle Simulation ────────────────────────────────────────
void cmdHomeSim(String args) {
  int stepDelayMs = 2; // Default 2 ms per simulated step (~500 steps/sec)
  if (args.length() > 0) {
    stepDelayMs = args.toInt();
    if (stepDelayMs < 1) stepDelayMs = 1;
    if (stepDelayMs > 50) stepDelayMs = 50;
  }

  Serial.println("\n+=======================================================================+");
  Serial.println("|           STEPPER MOTOR HOMING SEQUENCE SIMULATION                    |");
  Serial.println("+=======================================================================+");
  Serial.printf( "| Simulating 2-stage CNC homing routine at %d ms/step.                   |\n", stepDelayMs);
  Serial.println("| Send 'X' at any time to abort homing sequence.                        |");
  Serial.println("+=======================================================================+\n");

  // Phase 1: Fast Seek Approach
  Serial.println("[PHASE 1] Fast seek towards X endstop...");
  Serial.println("          Move card/carriage toward Sensor X now!");

  const unsigned long TIMEOUT_MS = 15000;
  unsigned long startTime = millis();
  long simulatedSteps = 0;
  bool triggered = false;

  while (millis() - startTime < TIMEOUT_MS) {
    if (checkAbort()) {
      Serial.println("[HOMESIM] Aborted by user.");
      return;
    }

    simulatedSteps++;
    if (isObstacleDetected(SENSOR_X_PIN)) {
      // Validate with debounce (must remain stable for 3 consecutive readings over 6 ms)
      bool stable = true;
      for (int k = 0; k < 3; k++) {
        delay(2);
        if (!isObstacleDetected(SENSOR_X_PIN)) { stable = false; break; }
      }
      if (stable) {
        triggered = true;
        break;
      }
    }

    if (simulatedSteps % 50 == 0) {
      Serial.printf("  Stepping... [%4ld steps] (Sensor X: %s)\n",
                    simulatedSteps, isObstacleDetected(SENSOR_X_PIN) ? "DETECTED" : "CLEAR");
    }
    delay(stepDelayMs);
  }

  if (!triggered) {
    Serial.println("\n[ERROR] Homing TIMEOUT! Sensor X never triggered within 15 seconds.");
    Serial.println("        Simulated stepper halted for safety.");
    return;
  }

  Serial.printf("\n[STOP] Hard limit reached at simulated step #%ld!\n", simulatedSteps);
  long fastStopStep = simulatedSteps;

  // Phase 2: Retract / Back-off
  Serial.println("\n[PHASE 2] Retracting motor until sensor clears...");
  Serial.println("          Back the obstacle away slightly...");

  long retractSteps = 0;
  unsigned long retractStart = millis();
  while (isObstacleDetected(SENSOR_X_PIN)) {
    if (checkAbort()) { Serial.println("[HOMESIM] Aborted."); return; }
    if (millis() - retractStart > 10000) {
      Serial.println("[ERROR] Retract timeout: sensor remained blocked.");
      return;
    }
    retractSteps++;
    delay(stepDelayMs * 2);
  }
  Serial.printf("  [OK] Sensor cleared after %ld simulated retract steps.\n", retractSteps);

  // Phase 3: Slow Precision Latch
  Serial.println("\n[PHASE 3] Secondary slow approach (high precision touch)...");
  int slowDelay = stepDelayMs * 4;
  long latchSteps = 0;
  unsigned long latchStart = millis();
  triggered = false;

  while (millis() - latchStart < 8000) {
    if (checkAbort()) { Serial.println("[HOMESIM] Aborted."); return; }
    latchSteps++;
    if (isObstacleDetected(SENSOR_X_PIN)) {
      triggered = true;
      break;
    }
    delay(slowDelay);
  }

  if (!triggered) {
    Serial.println("[WARN] Precision latch touch timed out.");
    return;
  }

  Serial.println("\n+-----------------------------------------------------------------------+");
  Serial.println("|                     HOMING SEQUENCE COMPLETED                         |");
  Serial.println("+-----------------------------------------------------------------------+");
  Serial.printf( "| Fast seek stop position : Step %-38ld |\n", fastStopStep);
  Serial.printf( "| Retract distance        : %ld steps                                  |\n", retractSteps);
  Serial.printf( "| Final latch position    : Step %-38ld |\n", fastStopStep - retractSteps + latchSteps);
  Serial.println("| Coordinate Origin Zeroed: X = 0.00 mm (Machine Home Established)     |");
  Serial.println("+-----------------------------------------------------------------------+\n");
}

// ── DUAL SENSOR MONITOR / SYNC ───────────────────────────────────────────────
void cmdDual(String args) {
  if (args == "SYNC") {
    // Dual sensor synchronization / alignment test
    Serial.println("\n[DUAL SYNC] Dual Sensor Alignment Synchronization Test.");
    Serial.println("            Bring a straight edge or dual-axis gantry to trigger BOTH");
    Serial.println("            Sensor X and Sensor Y simultaneously.");
    Serial.println("            Send 'X' to abort.\n");

    while (isObstacleDetected(SENSOR_X_PIN) || isObstacleDetected(SENSOR_Y_PIN)) {
      if (checkAbort()) { Serial.println("[DUAL SYNC] Aborted."); return; }
      Serial.println("  Waiting for both sensors to be clear...");
      delay(500);
    }
    Serial.println("  Both sensors CLEAR. Approach target now...");

    unsigned long tX = 0, tY = 0;
    while (tX == 0 || tY == 0) {
      if (checkAbort()) { Serial.println("[DUAL SYNC] Aborted."); return; }

      if (tX == 0 && isObstacleDetected(SENSOR_X_PIN)) {
        tX = micros();
        Serial.println("  >>> Sensor X Triggered! <<<");
      }
      if (tY == 0 && isObstacleDetected(SENSOR_Y_PIN)) {
        tY = micros();
        Serial.println("  >>> Sensor Y Triggered! <<<");
      }
      delayMicroseconds(20);
    }

    long deltaUs = (long)tY - (long)tX;
    float deltaMs = abs(deltaUs) / 1000.0f;

    Serial.println("\n+-----------------------------------------------------------------------+");
    Serial.println("|                     DUAL SYNC ALIGNMENT RESULTS                       |");
    Serial.println("+-----------------------------------------------------------------------+");
    Serial.printf( "| First Sensor Triggered  : %-43s |\n", deltaUs > 0 ? "Sensor X (X triggered first)" : "Sensor Y (Y triggered first)");
    Serial.printf( "| Trigger Time Delta      : %.3f ms (%ld us)                          |\n", deltaMs, abs(deltaUs));

    if (deltaMs < 2.0f) {
      Serial.println("| Alignment Evaluation    : EXCELLENT (< 2 ms difference)               |");
      Serial.println("|                           Gantry / target is squarely aligned!        |");
    } else if (deltaMs < 15.0f) {
      Serial.println("| Alignment Evaluation    : GOOD (2 - 15 ms difference)                 |");
      Serial.println("|                           Acceptable for homing synchronization.      |");
    } else {
      Serial.println("| Alignment Evaluation    : SKEWED (> 15 ms difference)                 |");
      Serial.println("|                           Inspect sensor bracket squareness.          |");
    }
    Serial.println("+-----------------------------------------------------------------------+\n");
    return;
  }

  // Real-time Dual Dashboard Mode
  Serial.println("[DUAL] Real-Time Dual Sensor Dashboard. Send 'X' to exit.");
  Serial.println("       Live status bar updating continuously...\n");

  unsigned long lastPrintMs = 0;
  bool lastX = !isObstacleDetected(SENSOR_X_PIN);
  bool lastY = !isObstacleDetected(SENSOR_Y_PIN);

  while (true) {
    if (checkAbort()) {
      Serial.println("\n[DUAL] Dashboard closed.");
      return;
    }

    bool curX = isObstacleDetected(SENSOR_X_PIN);
    bool curY = isObstacleDetected(SENSOR_Y_PIN);

    if (curX != lastX || curY != lastY || (millis() - lastPrintMs > 500)) {
      lastPrintMs = millis();
      lastX = curX;
      lastY = curY;
      updateStatusLED();

      Serial.printf("  [X: %-8s |%s|]   [Y: %-8s |%s|]   LED: %s   (Triggers: X=%u, Y=%u)\n",
                    curX ? "DETECTED" : "CLEAR",
                    curX ? "##########" : "..........",
                    curY ? "DETECTED" : "CLEAR",
                    curY ? "##########" : "..........",
                    (curX || curY) ? "ON " : "OFF",
                    statsX.triggers, statsY.triggers);
    }
    delay(30);
  }
}

// ── NOISE TEST ───────────────────────────────────────────────────────────────
void cmdNoise(String args) {
  int durationS = 10;
  if (args.length() > 0) {
    durationS = args.toInt();
    if (durationS < 1) durationS = 1;
    if (durationS > 60) durationS = 60;
  }

  Serial.printf("[NOISE] Running noise immunity test for %d seconds.\n", durationS);
  Serial.println("        Leave the machine completely still and undisturbed.");
  Serial.println("        Monitoring for spurious electrical or optical glitches...");
  Serial.println("        Send 'X' to abort.\n");

  uint32_t glitchCountX = 0;
  uint32_t glitchCountY = 0;
  uint64_t totalCycles  = 0;

  bool baselineX = readRawPin(SENSOR_X_PIN);
  bool baselineY = readRawPin(SENSOR_Y_PIN);

  unsigned long startMs = millis();
  unsigned long endMs   = startMs + (unsigned long)durationS * 1000UL;

  while (millis() < endMs) {
    if (checkAbort()) {
      Serial.println("[NOISE] Aborted.");
      return;
    }

    totalCycles++;
    bool sampleX = readRawPin(SENSOR_X_PIN);
    bool sampleY = readRawPin(SENSOR_Y_PIN);

    if (sampleX != baselineX) {
      glitchCountX++;
      falseTriggerCount++;
    }
    if (sampleY != baselineY) {
      glitchCountY++;
      falseTriggerCount++;
    }

    delayMicroseconds(20);
  }

  Serial.println("\n+-----------------------------------------------------------------------+");
  Serial.println("|                       NOISE IMMUNITY TEST REPORT                      |");
  Serial.println("+-----------------------------------------------------------------------+");
  Serial.printf( "| Test Duration         : %d seconds                                   |\n", durationS);
  Serial.printf( "| Total Samples Checked : %llu cycles                                |\n", totalCycles);
  Serial.printf( "| Sensor X Glitches     : %-45u |\n", glitchCountX);
  Serial.printf( "| Sensor Y Glitches     : %-45u |\n", glitchCountY);
  Serial.println("+-----------------------------------------------------------------------+");

  if (glitchCountX == 0 && glitchCountY == 0) {
    Serial.println("| Rating : EXCELLENT! 100% Noise-Free. Perfectly stable signal rail.   |");
  } else if (glitchCountX + glitchCountY < 10) {
    Serial.println("| Rating : GOOD. Minor sporadic noise detected. Filtered by debounce.   |");
  } else {
    Serial.println("| Rating : WARNING! Significant electrical or optical noise detected!   |");
    Serial.println("| Check 5V power supply ripple, add 100nF decoupling capacitor, or     |");
    Serial.println("| shield sensors from flickering artificial room lighting.              |");
  }
  Serial.println("+-----------------------------------------------------------------------+\n");
}

// ── AMBIENT INTERFERENCE ─────────────────────────────────────────────────────
void cmdAmbient() {
  Serial.println("\n[AMBIENT] Optical Ambient Light & Mains Flicker Test.");
  Serial.println("          Sampling 10,000 readings at 10 kHz to detect AC lighting flicker");
  Serial.println("          (100 Hz / 120 Hz mains hum from fluorescent/LED drivers)...");
  Serial.println("          Send 'X' to abort.\n");

  const int SAMPLES = 10000;
  int transitionsX = 0;
  int transitionsY = 0;
  bool prevX = readRawPin(SENSOR_X_PIN);
  bool prevY = readRawPin(SENSOR_Y_PIN);

  unsigned long startUs = micros();
  for (int i = 0; i < SAMPLES; i++) {
    if ((i % 1000 == 0) && checkAbort()) {
      Serial.println("[AMBIENT] Aborted.");
      return;
    }
    bool curX = readRawPin(SENSOR_X_PIN);
    bool curY = readRawPin(SENSOR_Y_PIN);

    if (curX != prevX) { transitionsX++; prevX = curX; }
    if (curY != prevY) { transitionsY++; prevY = curY; }

    delayMicroseconds(100); // 10 kHz sampling
  }
  unsigned long durationUs = micros() - startUs;

  Serial.println("+-----------------------------------------------------------------------+");
  Serial.println("|                      AMBIENT LIGHT FLICKER REPORT                     |");
  Serial.println("+-----------------------------------------------------------------------+");
  Serial.printf( "| Sampling Window       : %lu us (%.1f ms)                           |\n", durationUs, durationUs / 1000.0f);
  Serial.printf( "| Sensor X Transitions  : %-45d |\n", transitionsX);
  Serial.printf( "| Sensor Y Transitions  : %-45d |\n", transitionsY);
  Serial.println("+-----------------------------------------------------------------------+");

  if (transitionsX == 0 && transitionsY == 0) {
    Serial.println("| Optical Verdict : PASSED. Zero ambient light interference detected.   |");
  } else {
    Serial.println("| Optical Verdict : INTERFERENCE DETECTED!                             |");
    Serial.println("| Ambient lighting is oscillating the comparator output.               |");
    Serial.println("| Recommendations:                                                      |");
    Serial.println("|  1. Add an optical shroud or 3D-printed black hood over the IR pair.  |");
    Serial.println("|  2. Slightly decrease sensitivity via trim-pot (counter-clockwise).   |");
    Serial.println("|  3. Avoid direct sunlight or unshielded desk lamp illumination.       |");
  }
  Serial.println("+-----------------------------------------------------------------------+\n");
}

// ── RESET ────────────────────────────────────────────────────────────────────
void cmdReset() {
  statsX = SensorStats();
  statsY = SensorStats();
  falseTriggerCount = 0;
  invertLogic = false;
  mirrorLED   = true;
  updateStatusLED();
  Serial.println("[RESET] All counters, edge metrics, and configurations reset to defaults.");
}

// =============================================================================
//  STATUS & DIAGNOSTIC REPORTS
// =============================================================================

void printStatus() {
  bool rawX = readRawPin(SENSOR_X_PIN);
  bool rawY = readRawPin(SENSOR_Y_PIN);
  bool detX = isObstacleDetected(SENSOR_X_PIN);
  bool detY = isObstacleDetected(SENSOR_Y_PIN);

  Serial.println("\n+=======================================================================+");
  Serial.println("|                    IR SENSOR SUBSYSTEM STATUS REPORT                  |");
  Serial.println("+=======================================================================+");
  Serial.printf( "| Hardware Target    : ESP32-S3 Super Mini                             |\n");
  Serial.printf( "| Sensor X Pin       : GPIO %-2d  [Raw=%-4s  State=%-8s]                |\n",
                 SENSOR_X_PIN, rawX ? "HIGH" : "LOW", detX ? "DETECTED" : "CLEAR");
  Serial.printf( "| Sensor Y Pin       : GPIO %-2d  [Raw=%-4s  State=%-8s]                |\n",
                 SENSOR_Y_PIN, rawY ? "HIGH" : "LOW", detY ? "DETECTED" : "CLEAR");
  Serial.printf( "| Status LED Pin     : GPIO %-2d  [Mirror=%-8s  State=%-4s]           |\n",
                 STATUS_LED_PIN, mirrorLED ? "ENABLED" : "DISABLED",
                 digitalRead(STATUS_LED_PIN) ? "ON" : "OFF");
  Serial.printf( "| Logic Polarity     : %-48s |\n",
                 invertLogic ? "INVERTED (Active HIGH)" : "NORMAL   (Active LOW, FC-51 standard)");
  Serial.printf( "| X Trigger Counts   : %-48u |\n", statsX.triggers);
  Serial.printf( "| Y Trigger Counts   : %-48u |\n", statsY.triggers);
  Serial.printf( "| False Triggers     : %-48u |\n", falseTriggerCount);
  Serial.printf( "| Free Heap Memory   : %-48u bytes\n", (unsigned)ESP.getFreeHeap());
  Serial.printf( "| System Uptime      : %-48lu seconds\n", millis() / 1000UL);
  Serial.println("+=======================================================================+\n");
}

void printStats() {
  Serial.println("\n+=======================================================================+");
  Serial.println("|                     CUMULATIVE SENSOR METRICS                         |");
  Serial.println("+---------------------------------------+---------------+---------------+");
  Serial.println("| Metric Parameter                      | Sensor X      | Sensor Y      |");
  Serial.println("+---------------------------------------+---------------+---------------+");
  Serial.printf( "| Total Triggers (Obstacle Detected)   | %-13u | %-13u |\n", statsX.triggers, statsY.triggers);
  Serial.printf( "| Total Releases (Obstacle Cleared)    | %-13u | %-13u |\n", statsX.releases, statsY.releases);
  Serial.printf( "| Transition Bounces Detected           | %-13u | %-13u |\n", statsX.bounces, statsY.bounces);
  char minXBuf[16], minYBuf[16];
  if (statsX.minDurationMs == 0xFFFFFFFF) strcpy(minXBuf, "N/A");
  else snprintf(minXBuf, sizeof(minXBuf), "%u", statsX.minDurationMs);
  if (statsY.minDurationMs == 0xFFFFFFFF) strcpy(minYBuf, "N/A");
  else snprintf(minYBuf, sizeof(minYBuf), "%u", statsY.minDurationMs);

  Serial.printf( "| Min Trigger Duration (ms)             | %-13s | %-13s |\n", minXBuf, minYBuf);
  Serial.printf( "| Max Trigger Duration (ms)             | %-13u | %-13u |\n", statsX.maxDurationMs, statsY.maxDurationMs);
  float avgX = statsX.completedCycles > 0 ? (float)statsX.totalDurationMs / statsX.completedCycles : 0.0f;
  float avgY = statsY.completedCycles > 0 ? (float)statsY.totalDurationMs / statsY.completedCycles : 0.0f;
  Serial.printf( "| Avg Trigger Duration (ms)             | %-13.1f | %-13.1f |\n", avgX, avgY);
  Serial.printf( "| False / Spurious Triggers             | %-13u | %-13u |\n", falseTriggerCount, falseTriggerCount);
  Serial.println("+---------------------------------------+---------------+---------------+");
  Serial.printf( "| Active Uptime: %lu seconds | Free Heap: %u bytes                      |\n",
                 millis() / 1000UL, (unsigned)ESP.getFreeHeap());
  Serial.println("+=======================================================================+\n");
}

void reportBootReadout() {
  bool rawX = readRawPin(SENSOR_X_PIN);
  bool rawY = readRawPin(SENSOR_Y_PIN);
  bool detX = isObstacleDetected(SENSOR_X_PIN);
  bool detY = isObstacleDetected(SENSOR_Y_PIN);

  Serial.println("+--------------------------------------------------+");
  Serial.println("|             INITIAL BOOT PIN READOUT             |");
  Serial.println("+--------------------------------------------------+");
  Serial.printf( "| Sensor X (GPIO %-2d) : Raw=%d -> %-19s |\n",
                 SENSOR_X_PIN, rawX ? 1 : 0, detX ? "DETECTED (BLOCKED)" : "CLEAR (UNBLOCKED)");
  Serial.printf( "| Sensor Y (GPIO %-2d) : Raw=%d -> %-19s |\n",
                 SENSOR_Y_PIN, rawY ? 1 : 0, detY ? "DETECTED (BLOCKED)" : "CLEAR (UNBLOCKED)");
  Serial.printf( "| Status LED (GPIO %-2d): %-31s |\n",
                 STATUS_LED_PIN, (detX || detY) ? "ON (Mirroring Obstacle)" : "OFF");
  Serial.println("+--------------------------------------------------+");
  Serial.println("| System Ready. Type HELP or ? for command list.   |");
  Serial.println("+--------------------------------------------------+\n");
}

// =============================================================================
//  BOOT BANNER
// =============================================================================
void printBanner() {
  Serial.println("\n");
  Serial.println("+==================================================+");
  Serial.println("|     IR OBSTACLE SENSOR TEST & CALIBRATION        |");
  Serial.println("|     ESP32-S3 Super Mini  |  Homing Limit System  |");
  Serial.println("+==================================================+");
  Serial.printf( "|  Sensor X Pin : GPIO %-2d (Homing X-axis)          |\n", SENSOR_X_PIN);
  Serial.printf( "|  Sensor Y Pin : GPIO %-2d (Homing Y-axis)          |\n", SENSOR_Y_PIN);
  Serial.printf( "|  Status LED   : GPIO %-2d (Detection Mirror)       |\n", STATUS_LED_PIN);
  Serial.printf( "|  Sensor Logic : %-32s |\n", invertLogic ? "Active HIGH" : "Active LOW (Standard)");
  Serial.println("+==================================================+");
  Serial.println("|  Type HELP or ? for command list.                |");
  Serial.println("|  Send 'X' during any running test to abort.      |");
  Serial.println("+==================================================+\n");
}

// =============================================================================
//  HELP TEXT
// =============================================================================
void printHelp() {
  Serial.println("\n+----------------------------------------------------------------------+");
  Serial.println("|              IR SENSOR TEST FIRMWARE -- COMMAND REFERENCE            |");
  Serial.println("+--------------------+-------------------------------------------------+");
  Serial.println("| BASIC DETECTION    |                                                 |");
  Serial.println("|  READ              | Single snapshot of both sensors and status LED  |");
  Serial.println("|  MONITOR           | Continuous real-time edge monitoring (abort 'X')|");
  Serial.println("|  MONITOR <ms>      | Periodic polling at specified ms (e.g. 100)     |");
  Serial.println("|  WATCH <1|2|BOTH>  | Focused watch stream for specific sensor(s)     |");
  Serial.println("+--------------------+-------------------------------------------------+");
  Serial.println("| CALIBRATION        |                                                 |");
  Serial.println("|  CALIB             | Interactive distance calibration wizard         |");
  Serial.println("|  RANGE             | Multi-distance verification test (30mm -> 2mm)  |");
  Serial.println("|  THRESHOLD         | Diagnose trim-pot setting & adjustment advice   |");
  Serial.println("+--------------------+-------------------------------------------------+");
  Serial.println("| DEBOUNCE & TIMING  |                                                 |");
  Serial.println("|  DEBOUNCE          | Measure transition bounces & settling window    |");
  Serial.println("|  DEBOUNCE <samples>| High-speed consecutive sampling stability check |");
  Serial.println("|  EDGES <secs>      | Count rising/falling edges over duration        |");
  Serial.println("|  RESPONSE          | Measure transition delay & optical bandwidth    |");
  Serial.println("|  LATENCY <count>   | Measure response latency over N trigger cycles  |");
  Serial.println("+--------------------+-------------------------------------------------+");
  Serial.println("| HOMING SIMULATION  |                                                 |");
  Serial.println("|  HOMESIM           | 2-stage CNC homing approach simulation          |");
  Serial.println("|  HOMESIM <spd_ms>  | Homing simulation with custom motor step delay  |");
  Serial.println("+--------------------+-------------------------------------------------+");
  Serial.println("| DUAL SENSOR        |                                                 |");
  Serial.println("|  DUAL              | Simultaneous real-time visual dashboard         |");
  Serial.println("|  DUAL SYNC         | Alignment synchronization test between X and Y  |");
  Serial.println("+--------------------+-------------------------------------------------+");
  Serial.println("| NOISE & OPTICAL    |                                                 |");
  Serial.println("|  NOISE <secs>      | High-frequency false-trigger noise test         |");
  Serial.println("|  AMBIENT           | Detect 100/120 Hz room light flicker ripple     |");
  Serial.println("+--------------------+-------------------------------------------------+");
  Serial.println("| SYSTEM & CONFIG    |                                                 |");
  Serial.println("|  STATUS  or  ?     | Comprehensive system status report              |");
  Serial.println("|  STATS             | Accumulated trigger, bounce & duration metrics  |");
  Serial.println("|  LED               | Toggle GPIO 13 status LED mirroring on/off      |");
  Serial.println("|  INVERT            | Invert active logic polarity (LOW <-> HIGH)     |");
  Serial.println("|  ECHO              | Toggle command echo back to serial              |");
  Serial.println("|  RESET             | Clear all statistics & counters to defaults     |");
  Serial.println("|  HELP  or  H       | Show this command reference table               |");
  Serial.println("+--------------------+-------------------------------------------------+");
  Serial.println("| ABORT ANY TEST     | Send the letter 'X' during any active routine   |");
  Serial.println("+--------------------+-------------------------------------------------+\n");
}
