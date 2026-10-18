#include "stepper_axis.h"

StepperAxis::StepperAxis(uint8_t stepPin, uint8_t dirPin, uint8_t enablePin, bool invertDir)
  : m_stepPin(stepPin),
    m_dirPin(dirPin),
    m_enablePin(enablePin),
    m_invertDir(invertDir),
    m_enabled(false),
    m_homed(false),
    m_position(0),
    m_digitalWrite(nullptr),
    m_delayMicroseconds(nullptr) {}

void StepperAxis::init(DigitalWriteFn writeFn, DelayUsFn delayUs) {
  m_digitalWrite = writeFn;
  m_delayMicroseconds = delayUs;

  if (m_digitalWrite) {
    // Assert safe disabled state
    m_digitalWrite(m_enablePin, DRIVER_DISABLE_LEVEL);
    m_digitalWrite(m_stepPin, 0);
    m_digitalWrite(m_dirPin, 0);
  }
  m_enabled = false;
  m_homed = false;
  m_position = 0;
}

void StepperAxis::setEnabled(bool enabled) {
  m_enabled = enabled;
  if (m_digitalWrite) {
    m_digitalWrite(m_enablePin, enabled ? DRIVER_ENABLE_LEVEL : DRIVER_DISABLE_LEVEL);
  }
}

void StepperAxis::step(bool forward) {
  if (!m_enabled) {
    setEnabled(true);
    if (m_delayMicroseconds) m_delayMicroseconds(50); // wake up charge pump
  }

  bool dirLevel = forward ? (m_invertDir ? DIR_CCW_LEVEL : DIR_CW_LEVEL)
                          : (m_invertDir ? DIR_CW_LEVEL : DIR_CCW_LEVEL);

  if (m_digitalWrite) {
    m_digitalWrite(m_dirPin, dirLevel);
    if (m_delayMicroseconds) m_delayMicroseconds(DIR_SETUP_TIME_US);

    m_digitalWrite(m_stepPin, 1);
    if (m_delayMicroseconds) m_delayMicroseconds(STEP_PULSE_WIDTH_US);
    m_digitalWrite(m_stepPin, 0);
  }

  if (forward) {
    m_position++;
  } else {
    m_position--;
  }
}
