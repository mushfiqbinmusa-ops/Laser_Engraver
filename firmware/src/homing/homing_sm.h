#pragma once

#include "sensor_filter.h"
#include "../motion/stepper_axis.h"
#include "../protocol/protocol_types.h"

enum class HomePhase : uint8_t {
  Idle = 0,
  Validate,
  ClearInitial,
  FastSeek,
  PullOff,
  SlowLatch,
  FinalPullOff,
  Complete,
  Aborted,
  Failed
};

const char* homePhaseToString(HomePhase phase);

class HomingStateMachine {
public:
  HomingStateMachine(StepperAxis& axisX, StepperAxis& axisY, SensorFilter& sensorFilter);

  bool start(AxisTarget target, uint32_t nowMs);
  
  // Non-blocking tick. Returns true while homing is in progress.
  bool tick(uint32_t nowUs, uint32_t nowMs);

  void abort();

  void setMicrostepMode(uint8_t mode) { m_microstepMode = mode ? mode : 1; }

  bool isActive() const { return m_active; }
  HomePhase getPhase() const { return m_phase; }
  ErrorCode getResult() const { return m_result; }
  AxisTarget getCurrentAxis() const { return m_currentAxis; }
  uint32_t getPhaseSteps() const { return m_phaseSteps; }

private:
  void startAxis(AxisTarget axis, uint32_t nowMs);
  void stepCurrentAxis(bool forward);
  void stepAwayFromSensor();
  bool seekForward() const;
  int32_t currentPosition() const;
  bool atOrBeyondSweepBound(int8_t directionSign) const;
  uint32_t scaledSteps(uint32_t referenceSteps) const;
  bool isCurrentSensorTriggered() const;
  uint32_t scaledRate(uint32_t referenceSps) const;

  StepperAxis& m_axisX;
  StepperAxis& m_axisY;
  SensorFilter& m_sensorFilter;

  bool m_active;
  AxisTarget m_target;
  AxisTarget m_currentAxis;
  HomePhase m_phase;
  ErrorCode m_result;

  uint32_t m_phaseStartMs;
  uint32_t m_phaseSteps;
  uint32_t m_lastStepUs;
  uint32_t m_stepIntervalUs;
  uint8_t m_microstepMode;

  // Bounded oscillation search state.
  int32_t m_sweepOrigin;
  uint32_t m_seekTravelSteps;
  int8_t m_seekDirection;
};
