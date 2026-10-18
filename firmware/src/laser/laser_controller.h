#pragma once

#include <stdint.h>
#include "../config/machine_limits.h"
#include "../config/pin_mapping.h"

using PwmWriteFn = void (*)(uint8_t pin, uint32_t duty);

class LaserController {
public:
  LaserController();

  void init(PwmWriteFn writeFn);
  
  // Set duty with 90% safety clamp. Refreshes the watchdog.
  void setDuty(uint16_t duty, uint32_t nowMs);

  // Trigger a guarded pulse for up to MAX_PULSE_DURATION_MS
  bool pulse(uint16_t duty, uint32_t durationMs, uint32_t nowMs);

  // Check watchdog and pulse completion. Automatically shuts off laser if expired.
  void checkWatchdog(uint32_t nowMs);

  void forceOff();

  uint16_t getDuty() const { return m_currentDuty; }
  bool isActive() const { return m_currentDuty > 0; }
  bool isHardClamped() const { return m_wasClamped; }

private:
  PwmWriteFn m_pwmWrite;
  uint16_t m_currentDuty;
  uint32_t m_lastRefreshMs;
  uint32_t m_pulseStartMs;
  uint32_t m_pulseDurationMs;
  bool m_pulseActive;
  bool m_wasClamped;
};
