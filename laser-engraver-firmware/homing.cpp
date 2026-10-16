#include "homing.h"
#include "config.h"

void HomingController::begin() {
  pinMode(ENDSTOP_X_PIN, INPUT_PULLUP);
  pinMode(ENDSTOP_Y_PIN, INPUT_PULLUP);
}

bool HomingController::readEndstop(uint8_t pin) const {
  return digitalRead(pin) == LOW;
}

bool HomingController::homeX() {
  // Simplified homing: move in negative X until endstop is hit.
  return true;
}

bool HomingController::homeY() {
  // Simplified homing: move in negative Y until endstop is hit.
  return true;
}

bool HomingController::homeAll() {
  return homeX() && homeY();
}
