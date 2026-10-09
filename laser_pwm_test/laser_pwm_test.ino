// =============================================================================
//  laser_pwm_test.ino
//  Full-Featured Laser Characterisation & Test Sketch
//  Target: ESP32-S3 Super Mini
//  Author: Laser-XY-Scanner Project
//
//  No external libraries required — uses only the ESP32 Arduino Core (LEDC).
//
//  Open Serial Monitor at 115200 baud, type HELP for command list.
// =============================================================================

// ── Pin & LEDC Configuration ─────────────────────────────────────────────────
#define LASER_PIN       10        // GPIO connected to laser driver PWM input
#define LEDC_CHANNEL    0         // LEDC channel (0–7 on ESP32-S3)
#define LEDC_RES_BITS   12        // 12-bit resolution → duty 0–4095
#define LEDC_MAX_DUTY   4095      // 100% duty
#define DEFAULT_FREQ_HZ 5000      // Starting PWM frequency (Hz)

// Compatibility helper between ESP32 Arduino Core v2.x and v3.x
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

// ── Safety Limits ─────────────────────────────────────────────────────────────
// Change SAFETY_MAX_PCT to 100 if you want to test full power.
// Keep at 90 during initial characterisation to protect the diode.
#define SAFETY_MAX_PCT  90        // Hard ceiling (% of full scale)
#define SAFETY_MAX_DUTY ((LEDC_MAX_DUTY * SAFETY_MAX_PCT) / 100)

// ── State ─────────────────────────────────────────────────────────────────────
uint32_t currentFreqHz  = DEFAULT_FREQ_HZ;
uint16_t currentDuty    = 0;       // 0–4095
bool     laserOn        = false;
bool     safetyLock     = true;    // Safety ceiling active by default
bool     echoCommands   = true;    // Echo back parsed command

// =============================================================================
//  SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  laserOff();   // Laser hard LOW (GND) on boot

  printBanner();
}

// =============================================================================
//  LOOP — Serial Command Reader
// =============================================================================
void loop() {
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

  // ── Laser ON / OFF ──────────────────────────────────────────────────────
  if (upper == "ON") {
    laserSetDuty(currentDuty == 0 ? pctToDuty(10) : currentDuty);
    Serial.printf("[ON] Laser ON at duty=%d (%.1f%%)\n",
                  currentDuty, dutyToPct(currentDuty));

  } else if (upper == "OFF") {
    laserOff();
    Serial.println("[OFF] Laser OFF.");

  // ── Set Power by Percentage: S<0-100> ──────────────────────────────────
  } else if (upper.startsWith("S")) {
    int pct = upper.substring(1).toInt();
    if (applySafetyCheck(pct)) {
      uint16_t duty = pctToDuty(pct);
      laserSetDuty(duty);
      Serial.printf("[S] Power set to %d%% -> duty=%d/4095\n", pct, duty);
    }

  // ── Set Raw Duty: D<0-4095> ────────────────────────────────────────────
  } else if (upper.startsWith("D")) {
    int raw = upper.substring(1).toInt();
    raw = constrain(raw, 0, LEDC_MAX_DUTY);
    if (safetyLock && raw > SAFETY_MAX_DUTY) {
      Serial.printf("[SAFETY] Clamped %d -> %d (%.0f%% ceiling). "
                    "Type UNLOCK to bypass.\n", raw, SAFETY_MAX_DUTY,
                    (float)SAFETY_MAX_PCT);
      raw = SAFETY_MAX_DUTY;
    }
    laserSetDuty(raw);
    Serial.printf("[D] Raw duty=%d (%.1f%%)\n", raw, dutyToPct(raw));

  // ── Set PWM Frequency: FREQ <Hz> ──────────────────────────────────────
  } else if (upper.startsWith("FREQ ")) {
    uint32_t hz = upper.substring(5).toInt();
    if (hz < 100 || hz > 80000) {
      Serial.println("[FREQ] Valid range: 100-80000 Hz.");
    } else {
      currentFreqHz = hz;
      setupLEDC(LASER_PIN, currentFreqHz, LEDC_RES_BITS, LEDC_CHANNEL);
      writeLEDC(LASER_PIN, LEDC_CHANNEL, currentDuty);
      Serial.printf("[FREQ] PWM frequency set to %u Hz.\n", currentFreqHz);
    }

  // ── Sweep: SWEEP <start%> <end%> <step%> <dwell_ms> ──────────────────
  } else if (upper.startsWith("SWEEP")) {
    parseSweep(cmd);

  // ── Quick Sweep alias ─────────────────────────────────────────────────
  } else if (upper == "QSWEEP") {
    Serial.println("[QSWEEP] Quick sweep 0->100%, 5%/step, 1s per step.");
    sweepCore(0, 100, 5, 1000, true);

  // ── Single Pulse: PULSE <power%> <duration_ms> ────────────────────────
  } else if (upper.startsWith("PULSE")) {
    parsePulse(cmd);

  // ── Burst Mode: BURST <power%> <on_ms> <off_ms> <count> ──────────────
  } else if (upper.startsWith("BURST")) {
    parseBurst(cmd);

  // ── Ramp Up: RAMPUP <from%> <to%> <step_ms> ──────────────────────────
  } else if (upper.startsWith("RAMPUP")) {
    parseRamp(cmd, true);

  // ── Ramp Down: RAMPDN <from%> <to%> <step_ms> ────────────────────────
  } else if (upper.startsWith("RAMPDN")) {
    parseRamp(cmd, false);

  // ── Frequency Sweep: FSWEEP <start_hz> <end_hz> <steps> <dwell_ms> ───
  } else if (upper.startsWith("FSWEEP")) {
    parseFreqSweep(cmd);

  // ── Thermal Soak: SOAK <power%> <duration_s> ─────────────────────────
  // Holds laser on at a steady power level and reports time elapsed.
  } else if (upper.startsWith("SOAK")) {
    parseSoak(cmd);

  // ── Strobe: STROBE <power%> <hz> <duration_s> ────────────────────────
  } else if (upper.startsWith("STROBE")) {
    parseStrobe(cmd);

  // ── Status Report ─────────────────────────────────────────────────────
  } else if (upper == "STATUS" || upper == "?") {
    printStatus();

  // ── Safety Unlock (remove 90% ceiling) ───────────────────────────────
  } else if (upper == "UNLOCK") {
    safetyLock = false;
    Serial.println("[UNLOCK] Safety ceiling REMOVED. Full 0-100% range active.");
    Serial.println("         Type LOCK to re-enable the 90% ceiling.");

  } else if (upper == "LOCK") {
    safetyLock = true;
    if (currentDuty > SAFETY_MAX_DUTY) {
      laserSetDuty(SAFETY_MAX_DUTY);
      Serial.printf("[LOCK] Power clamped to %d%% ceiling.\n", SAFETY_MAX_PCT);
    }
    Serial.println("[LOCK] Safety ceiling re-enabled.");

  // ── Echo toggle ───────────────────────────────────────────────────────
  } else if (upper == "ECHO") {
    echoCommands = !echoCommands;
    Serial.printf("[ECHO] Command echo %s.\n", echoCommands ? "ON" : "OFF");

  // ── Reset ─────────────────────────────────────────────────────────────
  } else if (upper == "RESET") {
    laserOff();
    currentFreqHz = DEFAULT_FREQ_HZ;
    safetyLock = true;
    setupLEDC(LASER_PIN, currentFreqHz, LEDC_RES_BITS, LEDC_CHANNEL);
    Serial.println("[RESET] All settings reset to defaults. Laser OFF.");

  // ── Help ──────────────────────────────────────────────────────────────
  } else if (upper == "HELP" || upper == "H") {
    printHelp();

  } else {
    Serial.printf("[?] Unknown command: \"%s\"  -- type HELP\n", cmd.c_str());
  }
}

// =============================================================================
//  COMMAND IMPLEMENTATIONS
// =============================================================================

// ── SWEEP ────────────────────────────────────────────────────────────────────
void parseSweep(String cmd) {
  // SWEEP                       -> 0->100, step=5, dwell=1000ms
  // SWEEP <end>                 -> 0->end, step=5, dwell=1000ms
  // SWEEP <start> <end>         -> start->end, step=5, dwell=1000ms
  // SWEEP <start> <end> <step>  -> custom step, dwell=1000ms
  // SWEEP <start> <end> <step> <dwell_ms>
  String upper = cmd;
  upper.toUpperCase();
  String args = upper.substring(5);
  args.trim();

  int s = 0, e = 100, step = 5, dwell = 1000;
  int n = sscanf(args.c_str(), "%d %d %d %d", &s, &e, &step, &dwell);
  if (n == 0) { s = 0; e = 100; step = 5; dwell = 1000; }
  else if (n == 1) { e = s; s = 0; }

  step = max(1, abs(step));
  dwell = max(100, dwell);

  Serial.printf("[SWEEP] %d%% -> %d%%, step=%d%%, dwell=%dms\n",
                s, e, step, dwell);
  Serial.println("        SAFETY: Wear goggles! Laser fires in 3s...");
  countdown(3);
  sweepCore(s, e, step, dwell, true);
}

void sweepCore(int fromPct, int toPct, int stepPct,
               int dwellMs, bool printEach) {
  int direction = (toPct >= fromPct) ? 1 : -1;
  for (int pct = fromPct;
       direction > 0 ? pct <= toPct : pct >= toPct;
       pct += direction * stepPct) {

    // Allow early abort via 'X' in serial
    if (Serial.available()) {
      char c = Serial.read();
      if (c == 'X' || c == 'x' || c == '!') {
        laserOff();
        Serial.println("\n[ABORT] Sweep aborted. Laser OFF.");
        return;
      }
    }
    int p = constrain(pct, 0, safetyLock ? SAFETY_MAX_PCT : 100);
    uint16_t duty = pctToDuty(p);
    laserSetDuty(duty);
    if (printEach) {
      Serial.printf("  %3d%%  duty=%4d  [", p, duty);
      int bars = p / 5;
      for (int b = 0; b < 20; b++) Serial.print(b < bars ? "#" : ".");
      Serial.println("]");
    }
    delay(dwellMs);
  }
  laserOff();
  Serial.println("[SWEEP] Done. Laser OFF.");
}

// ── PULSE ────────────────────────────────────────────────────────────────────
void parsePulse(String cmd) {
  // PULSE <power%> <duration_ms>
  String upper = cmd;
  upper.toUpperCase();
  String args = upper.substring(5);
  args.trim();
  int pct = 50, dur = 500;
  sscanf(args.c_str(), "%d %d", &pct, &dur);
  pct = constrain(pct, 0, safetyLock ? SAFETY_MAX_PCT : 100);
  dur = constrain(dur, 1, 30000);

  Serial.printf("[PULSE] %d%% for %dms... ", pct, dur);
  uint16_t duty = pctToDuty(pct);
  laserSetDuty(duty);
  delay(dur);
  laserOff();
  Serial.println("done. Laser OFF.");
}

// ── BURST ────────────────────────────────────────────────────────────────────
void parseBurst(String cmd) {
  // BURST <power%> <on_ms> <off_ms> <count>
  String upper = cmd;
  upper.toUpperCase();
  String args = upper.substring(5);
  args.trim();
  int pct = 50, onMs = 100, offMs = 100, count = 10;
  sscanf(args.c_str(), "%d %d %d %d", &pct, &onMs, &offMs, &count);
  pct   = constrain(pct,   0, safetyLock ? SAFETY_MAX_PCT : 100);
  onMs  = constrain(onMs,  1, 10000);
  offMs = constrain(offMs, 1, 10000);
  count = constrain(count, 1, 1000);

  Serial.printf("[BURST] %d%% power | ON=%dms OFF=%dms x %d pulses\n",
                pct, onMs, offMs, count);
  uint16_t duty = pctToDuty(pct);
  for (int i = 0; i < count; i++) {
    if (Serial.available() && Serial.peek() == 'X') {
      laserOff();
      Serial.println("[ABORT]");
      return;
    }
    laserSetDuty(duty);
    delay(onMs);
    laserOff();
    delay(offMs);
    if ((i + 1) % 10 == 0) Serial.printf("  Pulse %d/%d\n", i + 1, count);
  }
  Serial.println("[BURST] Done.");
}

// ── RAMP ─────────────────────────────────────────────────────────────────────
void parseRamp(String cmd, bool upward) {
  // RAMPUP <from%> <to%> <step_ms>
  // RAMPDN <from%> <to%> <step_ms>
  String upper = cmd;
  upper.toUpperCase();
  String tag   = upward ? "RAMPUP" : "RAMPDN";
  String args  = upper.substring(tag.length());
  args.trim();
  int frm = upward ? 0 : 90, to = upward ? 90 : 0, stepMs = 30;
  sscanf(args.c_str(), "%d %d %d", &frm, &to, &stepMs);
  frm    = constrain(frm,    0, safetyLock ? SAFETY_MAX_PCT : 100);
  to     = constrain(to,     0, safetyLock ? SAFETY_MAX_PCT : 100);
  stepMs = constrain(stepMs, 1, 5000);

  Serial.printf("[%s] %d%% -> %d%%, %dms/step\n",
                tag.c_str(), frm, to, stepMs);
  Serial.println("        Laser fires in 3s...");
  countdown(3);
  sweepCore(frm, to, 1, stepMs, false);
  Serial.printf("[%s] Done. Laser OFF.\n", tag.c_str());
}

// ── FSWEEP — PWM Frequency Sweep ────────────────────────────────────────────
void parseFreqSweep(String cmd) {
  // FSWEEP <start_hz> <end_hz> <steps> <dwell_ms>
  String upper = cmd;
  upper.toUpperCase();
  String args = upper.substring(6);
  args.trim();
  int startHz = 100, endHz = 40000, steps = 10, dwellMs = 1000;
  sscanf(args.c_str(), "%d %d %d %d", &startHz, &endHz, &steps, &dwellMs);
  startHz = constrain(startHz, 100, 80000);
  endHz   = constrain(endHz,   100, 80000);
  steps   = constrain(steps,   2, 100);
  dwellMs = constrain(dwellMs, 100, 10000);

  Serial.printf("[FSWEEP] %dHz -> %dHz | %d steps | %dms each\n",
                startHz, endHz, steps, dwellMs);
  Serial.printf("         Laser held at 50%% during sweep.\n");
  laserSetDuty(pctToDuty(50));

  for (int i = 0; i < steps; i++) {
    uint32_t hz = startHz + (uint32_t)(i) * (endHz - startHz) / (steps - 1);
    currentFreqHz = hz;
    setupLEDC(LASER_PIN, hz, LEDC_RES_BITS, LEDC_CHANNEL);
    writeLEDC(LASER_PIN, LEDC_CHANNEL, currentDuty);
    Serial.printf("  Step %2d/%d -> %u Hz\n", i + 1, steps, hz);
    delay(dwellMs);
  }
  laserOff();
  Serial.println("[FSWEEP] Done. Laser OFF.");
}

// ── SOAK — Thermal Soak Hold ─────────────────────────────────────────────────
void parseSoak(String cmd) {
  // SOAK <power%> <duration_s>
  String upper = cmd;
  upper.toUpperCase();
  String args = upper.substring(4);
  args.trim();
  int pct = 50, durS = 30;
  sscanf(args.c_str(), "%d %d", &pct, &durS);
  pct  = constrain(pct,  0, safetyLock ? SAFETY_MAX_PCT : 100);
  durS = constrain(durS, 1, 3600);

  Serial.printf("[SOAK] Holding %d%% for %d seconds. Send 'X' to abort.\n",
                pct, durS);
  Serial.println("       SAFETY: Laser fires in 3s...");
  countdown(3);
  laserSetDuty(pctToDuty(pct));

  unsigned long startMs = millis();
  int lastTick = -1;
  while (millis() - startMs < (unsigned long)durS * 1000UL) {
    int elapsed = (millis() - startMs) / 1000;
    if (elapsed != lastTick) {
      lastTick = elapsed;
      Serial.printf("  t=%ds / %ds  (%.1f%%)\n", elapsed, durS, (float)pct);
    }
    if (Serial.available()) {
      char c = Serial.read();
      if (c == 'X' || c == 'x' || c == '!') {
        laserOff();
        Serial.println("[SOAK] Aborted. Laser OFF.");
        return;
      }
    }
    delay(100);
  }
  laserOff();
  Serial.println("[SOAK] Done. Laser OFF.");
}

// ── STROBE ───────────────────────────────────────────────────────────────────
void parseStrobe(String cmd) {
  // STROBE <power%> <hz> <duration_s>
  String upper = cmd;
  upper.toUpperCase();
  String args = upper.substring(6);
  args.trim();
  int pct = 80, hz = 10, durS = 5;
  sscanf(args.c_str(), "%d %d %d", &pct, &hz, &durS);
  pct  = constrain(pct, 1, safetyLock ? SAFETY_MAX_PCT : 100);
  hz   = constrain(hz,  1, 100);
  durS = constrain(durS, 1, 60);

  uint32_t halfPeriodUs = 500000UL / hz;
  uint32_t totalUs = (uint32_t)durS * 1000000UL;
  uint16_t duty = pctToDuty(pct);

  Serial.printf("[STROBE] %d%% at %dHz for %ds (half-period=%luus)\n",
                pct, hz, durS, halfPeriodUs);
  Serial.println("         Flashing light warning! Send 'X' to abort.");
  countdown(3);

  unsigned long startUs = micros();
  while (micros() - startUs < totalUs) {
    if (Serial.available() && (Serial.peek() == 'X' || Serial.peek() == 'x')) {
      laserOff();
      Serial.println("[ABORT]");
      return;
    }
    laserSetDuty(duty);
    delayMicroseconds(halfPeriodUs);
    laserOff();
    delayMicroseconds(halfPeriodUs);
  }
  laserOff();
  Serial.println("[STROBE] Done. Laser OFF.");
}

// =============================================================================
//  HELPERS
// =============================================================================

void laserSetDuty(uint16_t duty) {
  duty = constrain(duty, 0,
         safetyLock ? (uint16_t)SAFETY_MAX_DUTY : (uint16_t)LEDC_MAX_DUTY);
  currentDuty = duty;
  laserOn = (duty > 0);

  if (duty == 0) {
    laserOff();
  } else {
    // Re-attach LEDC if it was detached for hard LOW
    setupLEDC(LASER_PIN, currentFreqHz, LEDC_RES_BITS, LEDC_CHANNEL);
    writeLEDC(LASER_PIN, LEDC_CHANNEL, duty);
  }
}

void laserOff() {
  currentDuty = 0;
  laserOn = false;
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  ledcDetach(LASER_PIN);
#else
  ledcDetachPin(LASER_PIN);
#endif
  pinMode(LASER_PIN, OUTPUT);
  digitalWrite(LASER_PIN, LOW); // Hard pull to GND
}

uint16_t pctToDuty(int pct) {
  return (uint16_t)((uint32_t)constrain(pct, 0, 100) * LEDC_MAX_DUTY / 100);
}

float dutyToPct(uint16_t duty) {
  return (float)duty * 100.0f / LEDC_MAX_DUTY;
}

bool applySafetyCheck(int pct) {
  if (safetyLock && pct > SAFETY_MAX_PCT) {
    Serial.printf("[SAFETY] %d%% exceeds ceiling (%d%%). "
                  "Type UNLOCK to bypass.\n", pct, SAFETY_MAX_PCT);
    return false;
  }
  return true;
}

void countdown(int seconds) {
  for (int i = seconds; i > 0; i--) {
    Serial.printf("  %d...\n", i);
    delay(1000);
  }
}

// =============================================================================
//  STATUS REPORT
// =============================================================================
void printStatus() {
  Serial.println("\n+======================================+");
  Serial.println("|       LASER STATUS REPORT            |");
  Serial.println("+======================================+");
  Serial.printf( "|  Laser State    : %-18s|\n", laserOn ? "ON (FIRING)" : "OFF");
  Serial.printf( "|  Power (%%)      : %-18.1f|\n", dutyToPct(currentDuty));
  Serial.printf( "|  Raw Duty       : %-4d / 4095          |\n", currentDuty);
  Serial.printf( "|  PWM Frequency  : %-6u Hz            |\n", currentFreqHz);
  Serial.printf( "|  Resolution     : 12-bit (0-4095)    |\n");
  Serial.printf( "|  Safety Lock    : %-18s|\n",
                 safetyLock ? "ON  (<=90% ceiling)" : "OFF (full 0-100%)");
  Serial.printf( "|  GPIO Pin       : %-18d|\n", LASER_PIN);
  Serial.printf( "|  Free Heap      : %-8u bytes      |\n",
                 (unsigned)ESP.getFreeHeap());
  Serial.println("+======================================+\n");
}

// =============================================================================
//  HELP TEXT
// =============================================================================
void printHelp() {
  Serial.println("\n+------------------------------------------------------+");
  Serial.println("|          LASER TEST -- COMMAND REFERENCE             |");
  Serial.println("+------------------+-----------------------------------+");
  Serial.println("| POWER CONTROL    |                                   |");
  Serial.println("|  ON              | Laser on (last duty / 10%)        |");
  Serial.println("|  OFF             | Laser off immediately             |");
  Serial.println("|  S<0-100>        | Set power by percent   S75        |");
  Serial.println("|  D<0-4095>       | Set raw 12-bit duty    D2048      |");
  Serial.println("+------------------+-----------------------------------+");
  Serial.println("| SWEEP / RAMP     |                                   |");
  Serial.println("|  QSWEEP          | Quick 0->100%, 5%/step, 1s each   |");
  Serial.println("|  SWEEP           | Full sweep with defaults           |");
  Serial.println("|  SWEEP s e st dw | start end step_% dwell_ms         |");
  Serial.println("|  RAMPUP f t ms   | Smooth ramp up   (1%/step)        |");
  Serial.println("|  RAMPDN f t ms   | Smooth ramp down (1%/step)        |");
  Serial.println("+------------------+-----------------------------------+");
  Serial.println("| PULSE / BURST    |                                   |");
  Serial.println("|  PULSE p ms      | Single pulse: pwr%, duration_ms   |");
  Serial.println("|  BURST p on f ct | pwr%, on_ms, off_ms, count        |");
  Serial.println("|  STROBE p hz s   | pwr%, freq_Hz, duration_s         |");
  Serial.println("+------------------+-----------------------------------+");
  Serial.println("| FREQUENCY        |                                   |");
  Serial.println("|  FREQ <hz>       | Set PWM freq  e.g. FREQ 10000     |");
  Serial.println("|  FSWEEP s e n ms | start_hz end_hz steps dwell_ms    |");
  Serial.println("+------------------+-----------------------------------+");
  Serial.println("| LONG-DURATION    |                                   |");
  Serial.println("|  SOAK p secs     | Hold power% for N seconds         |");
  Serial.println("+------------------+-----------------------------------+");
  Serial.println("| SYSTEM           |                                   |");
  Serial.println("|  STATUS  or  ?   | Print current status              |");
  Serial.println("|  UNLOCK          | Remove 90% safety ceiling         |");
  Serial.println("|  LOCK            | Re-enable safety ceiling          |");
  Serial.println("|  ECHO            | Toggle command echo on/off        |");
  Serial.println("|  RESET           | Reset all settings, laser off     |");
  Serial.println("|  HELP  or  H     | Show this help                    |");
  Serial.println("+------------------+-----------------------------------+");
  Serial.println("| ABORT any test   | Send the letter X during a test   |");
  Serial.println("+------------------+-----------------------------------+");
  Serial.println("|  SAFETY: Always wear OD5+ laser goggles!             |");
  Serial.println("+------------------------------------------------------+\n");
}

// =============================================================================
//  BOOT BANNER
// =============================================================================
void printBanner() {
  Serial.println("\n");
  Serial.println("+==================================================+");
  Serial.println("|    LASER CHARACTERISATION TEST FIRMWARE          |");
  Serial.println("|    ESP32-S3 Super Mini  |  12-bit LEDC PWM       |");
  Serial.println("+==================================================+");
  Serial.printf( "|  PWM Pin     : GPIO %-2d                          |\n",
                 LASER_PIN);
  Serial.printf( "|  Frequency   : %u Hz default                  |\n",
                 currentFreqHz);
  Serial.println("|  Resolution  : 12-bit (0-4095)                 |");
  Serial.printf( "|  Safety Lock : ON (max %d%%)                    |\n",
                 SAFETY_MAX_PCT);
  Serial.println("+==================================================+");
  Serial.println("|  LASER IS OFF at boot.                          |");
  Serial.println("|  Type HELP for command list.                    |");
  Serial.println("+==================================================+\n");
}
