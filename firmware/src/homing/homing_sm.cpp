#include "homing_sm.h"

const char* homePhaseToString(HomePhase phase) {
  switch (phase) {
    case HomePhase::Idle: return "idle";
    case HomePhase::Validate: return "validate";
    case HomePhase::ClearInitial: return "clear_initial";
    case HomePhase::FastSeek: return "fast_seek";
    case HomePhase::PullOff: return "pull_off";
    case HomePhase::SlowLatch: return "slow_latch";
    case HomePhase::FinalPullOff: return "final_pull_off";
    case HomePhase::Complete: return "complete";
    case HomePhase::Aborted: return "aborted";
    case HomePhase::Failed: return "failed";
    default: return "unknown";
  }
}

HomingStateMachine::HomingStateMachine(StepperAxis& axisX, StepperAxis& axisY, SensorFilter& sensorFilter)
  : m_axisX(axisX),
    m_axisY(axisY),
    m_sensorFilter(sensorFilter),
    m_active(false),
    m_target(AxisTarget::None),
    m_currentAxis(AxisTarget::None),
    m_phase(HomePhase::Idle),
    m_result(ErrorCode::Ok),
    m_phaseStartMs(0),
    m_phaseSteps(0),
    m_lastStepUs(0),
    m_stepIntervalUs(1000000UL / HOMING_FAST_SPS),
    m_microstepMode(1),
    m_sweepOrigin(0),
    m_seekTravelSteps(0),
    m_seekDirection(1) {}

uint32_t HomingStateMachine::scaledRate(uint32_t referenceSps) const {
  uint32_t scaled = (referenceSps * m_microstepMode) / 16U;
  return scaled < 1U ? 1U : scaled;
}

uint32_t HomingStateMachine::scaledSteps(uint32_t referenceSteps) const {
  uint32_t scaled = (referenceSteps * m_microstepMode) / 16U;
  return scaled < 1U ? 1U : scaled;
}

bool HomingStateMachine::start(AxisTarget target, uint32_t nowMs) {
  if (target == AxisTarget::None) return false;
  m_target = target;
  m_active = true;
  m_result = ErrorCode::Ok;

  if (m_target == AxisTarget::All || m_target == AxisTarget::X) {
    startAxis(AxisTarget::X, nowMs);
  } else {
    startAxis(AxisTarget::Y, nowMs);
  }
  return true;
}

void HomingStateMachine::startAxis(AxisTarget axis, uint32_t nowMs) {
  m_currentAxis = axis;
  m_phase = HomePhase::Validate;
  m_phaseStartMs = nowMs;
  m_phaseSteps = 0;

  // Invalidate position on start of homing
  if (axis == AxisTarget::X) m_axisX.setHomed(false);
  if (axis == AxisTarget::Y) m_axisY.setHomed(false);

  m_sweepOrigin = currentPosition();
  m_seekTravelSteps = 0;
  m_seekDirection = seekForward() ? 1 : -1;

  // Check initial sensor
  if (isCurrentSensorTriggered()) {
    m_phase = HomePhase::ClearInitial;
    m_stepIntervalUs = 1000000UL / scaledRate(HOMING_SLOW_SPS);
  } else {
    m_phase = HomePhase::FastSeek;
    m_stepIntervalUs = 1000000UL / scaledRate(HOMING_FAST_SPS);
  }
}

bool HomingStateMachine::isCurrentSensorTriggered() const {
  if (m_currentAxis == AxisTarget::X) {
    return m_sensorFilter.isDetectedX();
  } else if (m_currentAxis == AxisTarget::Y) {
    return m_sensorFilter.isDetectedY();
  }
  return false;
}

void HomingStateMachine::stepCurrentAxis(bool forward) {
  if (m_currentAxis == AxisTarget::X) {
    m_axisX.step(forward);
  } else if (m_currentAxis == AxisTarget::Y) {
    m_axisY.step(forward);
  }
}

bool HomingStateMachine::seekForward() const {
  if (m_currentAxis == AxisTarget::X) return HOMING_SEEK_FORWARD_X;
  if (m_currentAxis == AxisTarget::Y) return HOMING_SEEK_FORWARD_Y;
  return true;
}

int32_t HomingStateMachine::currentPosition() const {
  if (m_currentAxis == AxisTarget::X) return m_axisX.getPosition();
  if (m_currentAxis == AxisTarget::Y) return m_axisY.getPosition();
  return 0;
}

bool HomingStateMachine::atOrBeyondSweepBound(int8_t directionSign) const {
  const int32_t sweep = static_cast<int32_t>(scaledSteps(HOMING_SWEEP_STEPS_REF));
  const int32_t position = currentPosition();
  if (directionSign > 0) return position >= m_sweepOrigin + sweep;
  return position <= m_sweepOrigin - sweep;
}

void HomingStateMachine::stepAwayFromSensor() {
  stepCurrentAxis(!seekForward());
}

bool HomingStateMachine::tick(uint32_t nowUs, uint32_t nowMs) {
  if (!m_active) return false;

  // Phase timeout check
  if (nowMs - m_phaseStartMs > HOMING_PHASE_TIMEOUT_MS) {
    m_phase = HomePhase::Failed;
    m_result = ErrorCode::HomeTimeout;
    m_active = false;
    return false;
  }

  // Rate limiter
  if (nowUs - m_lastStepUs < m_stepIntervalUs) {
    return true;
  }
  m_lastStepUs = nowUs;

  switch (m_phase) {
    case HomePhase::ClearInitial: {
      // Step away from the endstop so the sensor clears.
      stepAwayFromSensor();
      m_phaseSteps++;
      if (!isCurrentSensorTriggered()) {
        // Sensor cleared! Proceed to bounded oscillating seek.
        m_phase = HomePhase::FastSeek;
        m_phaseSteps = 0;
        m_phaseStartMs = nowMs;
        m_seekTravelSteps = 0;
        m_seekDirection = seekForward() ? 1 : -1;
        m_stepIntervalUs = 1000000UL / scaledRate(HOMING_FAST_SPS);
      } else if (m_phaseSteps >= scaledSteps(HOMING_MAX_CLEAR_STEPS)) {
        m_phase = HomePhase::Failed;
        m_result = ErrorCode::SensorStuckActive;
        m_active = false;
        return false;
      }
      break;
    }

    case HomePhase::FastSeek: {
      // Bounded oscillation: sweep toward the endstop first, then reverse at
      // +/-HOMING_SWEEP_DEG around the position where the search began.
      if (atOrBeyondSweepBound(m_seekDirection)) {
        m_seekDirection = -m_seekDirection;
      }
      stepCurrentAxis(m_seekDirection > 0);
      m_phaseSteps++;
      m_seekTravelSteps++;
      if (isCurrentSensorTriggered()) {
        // Detected! Transition to PullOff
        m_phase = HomePhase::PullOff;
        m_phaseSteps = 0;
        m_phaseStartMs = nowMs;
        m_stepIntervalUs = 1000000UL / scaledRate(HOMING_SLOW_SPS);
      } else if (m_seekTravelSteps >= scaledSteps(HOMING_MAX_SEEK_STEPS)) {
        m_phase = HomePhase::Failed;
        m_result = ErrorCode::HomeSeekLimit;
        m_active = false;
        return false;
      }
      break;
    }

    case HomePhase::PullOff: {
      // Retract away from the endstop.
      stepAwayFromSensor();
      m_phaseSteps++;
      // Require clearing sensor and advancing at least 20 steps past clearance
      if (!isCurrentSensorTriggered() && m_phaseSteps >= scaledSteps(20)) {
        // Cleared! Transition to SlowLatch
        m_phase = HomePhase::SlowLatch;
        m_phaseSteps = 0;
        m_phaseStartMs = nowMs;
        m_stepIntervalUs = 1000000UL / scaledRate(HOMING_SLOW_SPS);
      } else if (m_phaseSteps >= scaledSteps(HOMING_MAX_RETRACT_STEPS)) {
        m_phase = HomePhase::Failed;
        m_result = ErrorCode::SensorStuckActive;
        m_active = false;
        return false;
      }
      break;
    }

    case HomePhase::SlowLatch: {
      // Precision approach toward the endstop. The latch must re-trigger
      // inside the same bounded sweep window.
      if (atOrBeyondSweepBound(m_seekDirection)) {
        m_phase = HomePhase::Failed;
        m_result = ErrorCode::HomeSeekLimit;
        m_active = false;
        return false;
      }
      stepCurrentAxis(m_seekDirection > 0);
      m_phaseSteps++;
      if (isCurrentSensorTriggered()) {
        // Precision latch detected! Now perform FinalPullOff so the optical beam is clear
        m_phase = HomePhase::FinalPullOff;
        m_phaseSteps = 0;
        m_phaseStartMs = nowMs;
        m_stepIntervalUs = 1000000UL / scaledRate(HOMING_SLOW_SPS);
      } else if (m_phaseSteps >= scaledSteps(HOMING_MAX_LATCH_STEPS)) {
        m_phase = HomePhase::Failed;
        m_result = ErrorCode::HomeSeekLimit;
        m_active = false;
        return false;
      }
      break;
    }

    case HomePhase::FinalPullOff: {
      // Move away so the flag is outside the optical sensor.
      stepAwayFromSensor();
      m_phaseSteps++;
      if (m_phaseSteps >= scaledSteps(HOMING_FINAL_PULLOFF_STEPS)) {
        // Axis homing complete! Zero position and mark homed
        if (m_currentAxis == AxisTarget::X) {
          m_axisX.setPosition(0);
          m_axisX.setHomed(true);
        } else if (m_currentAxis == AxisTarget::Y) {
          m_axisY.setPosition(0);
          m_axisY.setHomed(true);
        }

        // If All was requested and we just finished X, move to Y
        if (m_target == AxisTarget::All && m_currentAxis == AxisTarget::X) {
          startAxis(AxisTarget::Y, nowMs);
        } else {
          m_phase = HomePhase::Complete;
          m_result = ErrorCode::Ok;
          m_active = false;
          return false;
        }
      }
      break;
    }

    default:
      m_active = false;
      return false;
  }

  return true;
}

void HomingStateMachine::abort() {
  m_active = false;
  m_phase = HomePhase::Aborted;
  m_result = ErrorCode::MotionAborted;
}
