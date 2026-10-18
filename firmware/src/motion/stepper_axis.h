#pragma once

#include <stdint.h>
#include "../config/pin_mapping.h"
#include "../config/machine_limits.h"

using DigitalWriteFn = void (*)(uint8_t pin, uint8_t val);
using DelayUsFn = void (*)(uint32_t us);

class StepperAxis {
public:
  StepperAxis(uint8_t stepPin, uint8_t dirPin, uint8_t enablePin, bool invertDir = false);

  void init(DigitalWriteFn writeFn, DelayUsFn delayUs);
  void setEnabled(bool enabled);
  bool isEnabled() const { return m_enabled; }

  // Step 1 pulse in specified direction (true = forward/CW, false = backward/CCW)
  void step(bool forward);

  int32_t getPosition() const { return m_position; }
  void setPosition(int32_t pos) { m_position = pos; }

  bool isHomed() const { return m_homed; }
  void setHomed(bool homed) { m_homed = homed; }

private:
  uint8_t m_stepPin;
  uint8_t m_dirPin;
  uint8_t m_enablePin;
  bool m_invertDir;
  bool m_enabled;
  bool m_homed;
  int32_t m_position;

  DigitalWriteFn m_digitalWrite;
  DelayUsFn m_delayMicroseconds;
};
