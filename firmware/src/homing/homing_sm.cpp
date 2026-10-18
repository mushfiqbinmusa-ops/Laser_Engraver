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
    m_stepIntervalUs(1000000UL / HOMING_FAST_SPS) {}

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

  // Check initial sensor
  if (isCurrentSensorTriggered()) {
    m_phase = HomePhase::ClearInitial;
    m_stepIntervalUs = 1000000UL / HOMING_SLOW_SPS;
  } else {
    m_phase = HomePhase::FastSeek;
    m_stepIntervalUs = 1000000UL / HOMING_FAST_SPS;
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
      // Step away from endstop (reverse direction, false)
      stepCurrentAxis(false);
      m_phaseSteps++;
      if (!isCurrentSensorTriggered()) {
        // Sensor cleared! Proceed to FastSeek
        m_phase = HomePhase::FastSeek;
        m_phaseSteps = 0;
        m_phaseStartMs = nowMs;
        m_stepIntervalUs = 1000000UL / HOMING_FAST_SPS;
      } else if (m_phaseSteps >= HOMING_MAX_CLEAR_STEPS) {
        m_phase = HomePhase::Failed;
        m_result = ErrorCode::SensorStuckActive;
        m_active = false;
        return false;
      }
      break;
    }

    case HomePhase::FastSeek: {
      // Step toward endstop (forward direction, true)
      stepCurrentAxis(true);
      m_phaseSteps++;
      if (isCurrentSensorTriggered()) {
        // Detected! Transition to PullOff
        m_phase = HomePhase::PullOff;
        m_phaseSteps = 0;
        m_phaseStartMs = nowMs;
        m_stepIntervalUs = 1000000UL / HOMING_SLOW_SPS;
      } else if (m_phaseSteps >= HOMING_MAX_SEEK_STEPS) {
        m_phase = HomePhase::Failed;
        m_result = ErrorCode::HomeSeekLimit;
        m_active = false;
        return false;
      }
      break;
    }

    case HomePhase::PullOff: {
      // Retract away from endstop (reverse direction, false)
      stepCurrentAxis(false);
      m_phaseSteps++;
      // Require clearing sensor and advancing at least 20 steps past clearance
      if (!isCurrentSensorTriggered() && m_phaseSteps >= 20) {
        // Cleared! Transition to SlowLatch
        m_phase = HomePhase::SlowLatch;
        m_phaseSteps = 0;
        m_phaseStartMs = nowMs;
        m_stepIntervalUs = 1000000UL / HOMING_SLOW_SPS;
      } else if (m_phaseSteps >= HOMING_MAX_RETRACT_STEPS) {
        m_phase = HomePhase::Failed;
        m_result = ErrorCode::SensorStuckActive;
        m_active = false;
        return false;
      }
      break;
    }

    case HomePhase::SlowLatch: {
      // Precision approach toward endstop
      stepCurrentAxis(true);
      m_phaseSteps++;
      if (isCurrentSensorTriggered()) {
        // Precision latch detected! Now perform FinalPullOff so the optical beam is clear
        m_phase = HomePhase::FinalPullOff;
        m_phaseSteps = 0;
        m_phaseStartMs = nowMs;
        m_stepIntervalUs = 1000000UL / HOMING_SLOW_SPS;
      } else if (m_phaseSteps >= HOMING_MAX_LATCH_STEPS) {
        m_phase = HomePhase::Failed;
        m_result = ErrorCode::HomeSeekLimit;
        m_active = false;
        return false;
      }
      break;
    }

    case HomePhase::FinalPullOff: {
      // Move away from endstop by HOMING_FINAL_PULLOFF_STEPS so the flag is outside the optical sensor
      stepCurrentAxis(false);
      m_phaseSteps++;
      if (m_phaseSteps >= HOMING_FINAL_PULLOFF_STEPS) {
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
