#include "motion.h"
#include "config.h"

void MotionController::begin() {
  pinMode(STEP_X_IN1, OUTPUT);
  pinMode(STEP_X_IN2, OUTPUT);
  pinMode(STEP_X_IN3, OUTPUT);
  pinMode(STEP_X_IN4, OUTPUT);
  pinMode(STEP_Y_IN1, OUTPUT);
  pinMode(STEP_Y_IN2, OUTPUT);
  pinMode(STEP_Y_IN3, OUTPUT);
  pinMode(STEP_Y_IN4, OUTPUT);
}

void MotionController::setPosition(int32_t x, int32_t y) {
  x_ = x;
  y_ = y;
}

int32_t MotionController::x() const {
  return x_;
}

int32_t MotionController::y() const {
  return y_;
}

void MotionController::moveTo(int32_t xTarget, int32_t yTarget, uint16_t laserDuty) {
  int32_t dx = xTarget - x_;
  int32_t dy = yTarget - y_;
  int32_t steps = max(abs(dx), abs(dy));
  if (steps == 0) {
    return;
  }

  for (int32_t i = 0; i <= steps; ++i) {
    float ratio = (float)i / (float)steps;
    int32_t xStep = round((float)dx * ratio);
    int32_t yStep = round((float)dy * ratio);

    if (xStep != x_ || yStep != y_) {
      x_ = xStep;
      y_ = yStep;
      digitalWrite(STEP_X_IN1, (x_ % 2 == 0) ? HIGH : LOW);
      digitalWrite(STEP_Y_IN1, (y_ % 2 == 0) ? HIGH : LOW);
      delayMicroseconds(500);
    }
  }

  // Laser is controlled independently in main sketch.
  (void)laserDuty;
}
