#include "laser_controller.h"

LaserController::LaserController()
  : m_pwmWrite(nullptr),
    m_currentDuty(0),
    m_lastRefreshMs(0),
    m_pulseEndMs(0),
    m_pulseActive(false),
    m_wasClamped(false) {}

void LaserController::init(PwmWriteFn writeFn) {
  m_pwmWrite = writeFn;
  forceOff();
}

void LaserController::forceOff() {
  m_currentDuty = 0;
  m_pulseActive = false;
  if (m_pwmWrite) {
    m_pwmWrite(PIN_LASER_PWM, 0);
  }
}

void LaserController::setDuty(uint16_t duty, uint32_t nowMs) {
  if (duty > SAFETY_MAX_DUTY) {
    duty = SAFETY_MAX_DUTY;
    m_wasClamped = true;
  } else {
    m_wasClamped = false;
  }

  m_currentDuty = duty;
  m_lastRefreshMs = nowMs;
  m_pulseActive = false;

  if (m_pwmWrite) {
    m_pwmWrite(PIN_LASER_PWM, m_currentDuty);
  }
}

bool LaserController::pulse(uint16_t duty, uint32_t durationMs, uint32_t nowMs) {
  if (durationMs == 0 || durationMs > MAX_PULSE_DURATION_MS) {
    return false;
  }
  setDuty(duty, nowMs);
  m_pulseActive = true;
  m_pulseEndMs = nowMs + durationMs;
  return true;
}

void LaserController::checkWatchdog(uint32_t nowMs) {
  if (m_currentDuty == 0) return;

  // Check pulse expiration
  if (m_pulseActive && nowMs >= m_pulseEndMs) {
    forceOff();
    return;
  }

  // Check 500ms watchdog
  if (nowMs - m_lastRefreshMs > LASER_WATCHDOG_MS) {
    forceOff();
  }
}
