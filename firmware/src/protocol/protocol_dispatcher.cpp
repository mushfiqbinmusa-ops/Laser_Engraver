#include "protocol_dispatcher.h"
#include <stdio.h>

ProtocolDispatcher::ProtocolDispatcher(MachineStateManager& state,
                                       StepperAxis& axisX,
                                       StepperAxis& axisY,
                                       BresenhamPlanner& planner,
                                       HomingStateMachine& homing,
                                       LaserController& laser,
                                       SensorFilter& sensors)
  : m_state(state),
    m_axisX(axisX),
    m_axisY(axisY),
    m_planner(planner),
    m_homing(homing),
    m_laser(laser),
    m_sensors(sensors),
    m_sendFn(nullptr),
    m_opCounter(0),
    m_opRunning(false) {
  m_currentOpId[0] = '\0';
}

void ProtocolDispatcher::sendAck(const char* reqId, bool accepted, ErrorCode code, const char* opId) {
  if (!m_sendFn) return;
  AckResponse ack;
  ack.v = 1;
  strncpy(ack.id, reqId ? reqId : "", sizeof(ack.id) - 1);
  ack.accepted = accepted;
  ack.code = code;
  if (opId) {
    strncpy(ack.operationId, opId, sizeof(ack.operationId) - 1);
  }
  char buf[512];
  if (ProtocolParser::serializeAck(ack, buf, sizeof(buf)) > 0) {
    m_sendFn(buf);
  }
}

void ProtocolDispatcher::sendOperationEvent(const char* type, const char* opId, const char* phase, uint32_t steps) {
  if (!m_sendFn) return;
  OperationEvent ev;
  ev.v = 1;
  strncpy(ev.eventType, type, sizeof(ev.eventType) - 1);
  strncpy(ev.operationId, opId, sizeof(ev.operationId) - 1);
  strncpy(ev.phase, phase, sizeof(ev.phase) - 1);
  ev.stepsCompleted = steps;
  char buf[512];
  if (ProtocolParser::serializeOperationEvent(ev, buf, sizeof(buf)) > 0) {
    m_sendFn(buf);
  }
}

void ProtocolDispatcher::sendOperationCompleted(const char* opId, const char* result) {
  if (!m_sendFn) return;
  OperationEvent ev;
  ev.v = 1;
  strcpy(ev.eventType, "operation.completed");
  strncpy(ev.operationId, opId, sizeof(ev.operationId) - 1);
  strncpy(ev.result, result, sizeof(ev.result) - 1);
  ev.homedX = m_axisX.isHomed();
  ev.homedY = m_axisY.isHomed();
  ev.posX = m_axisX.getPosition();
  ev.posY = m_axisY.getPosition();
  char buf[512];
  if (ProtocolParser::serializeOperationEvent(ev, buf, sizeof(buf)) > 0) {
    m_sendFn(buf);
  }
}

void ProtocolDispatcher::emitSnapshot() {
  if (!m_sendFn) return;
  SnapshotEvent snap;
  snap.v = 1;
  snap.state = m_state.getState();
  snap.firmwareVersion = "1.0.0";
  snap.protocolVersion = 1;
  snap.profileId = "esp32s3_a4988_v1";
  snap.homedX = m_axisX.isHomed();
  snap.homedY = m_axisY.isHomed();
  snap.posX = m_axisX.getPosition();
  snap.posY = m_axisY.getPosition();
  snap.posValid = m_state.isPositionValid();
  
  const SensorChannel& sx = m_sensors.getChannelX();
  const SensorChannel& sy = m_sensors.getChannelY();
  snap.sensorXRaw = sx.rawHigh;
  snap.sensorXFiltered = sx.stableDetected;
  snap.sensorYRaw = sy.rawHigh;
  snap.sensorYFiltered = sy.stableDetected;

  snap.laserDuty = m_laser.getDuty();
  snap.laserActive = m_laser.isActive();
  snap.laserHardClamped = m_laser.isHardClamped();
  snap.driversEnabled = m_axisX.isEnabled();
  snap.fault = m_state.getFault();

  char buf[512];
  if (ProtocolParser::serializeSnapshot(snap, buf, sizeof(buf)) > 0) {
    m_sendFn(buf);
  }
}

void ProtocolDispatcher::dispatch(const CommandRequest& req, uint32_t nowUs, uint32_t nowMs) {
  switch (req.type) {
    case CommandType::SystemPing: {
      sendAck(req.id, true, ErrorCode::Ok);
      break;
    }

    case CommandType::SystemSnapshot: {
      sendAck(req.id, true, ErrorCode::Ok);
      emitSnapshot();
      break;
    }

    case CommandType::SystemStop: {
      m_planner.abort();
      m_homing.abort();
      m_laser.forceOff();
      m_state.setState(m_state.isPositionValid() ? MachineStateEnum::IdleHomed : MachineStateEnum::IdleUnhomed);
      if (m_opRunning) {
        sendOperationCompleted(m_currentOpId, "aborted");
        m_opRunning = false;
      }
      sendAck(req.id, true, ErrorCode::Ok);
      break;
    }

    case CommandType::SystemEStop: {
      m_planner.abort();
      m_homing.abort();
      m_laser.forceOff();
      m_axisX.setEnabled(false);
      m_axisY.setEnabled(false);
      m_state.setFault("ESTOP_ACTIVATED");
      if (m_opRunning) {
        sendOperationCompleted(m_currentOpId, "aborted");
        m_opRunning = false;
      }
      sendAck(req.id, true, ErrorCode::Ok);
      emitSnapshot();
      break;
    }

    case CommandType::SystemResetFault: {
      m_state.clearFault();
      m_laser.forceOff();
      sendAck(req.id, true, ErrorCode::Ok);
      emitSnapshot();
      break;
    }

    case CommandType::DriverSetEnabled: {
      m_axisX.setEnabled(req.boolArg);
      m_axisY.setEnabled(req.boolArg);
      if (!req.boolArg) {
        m_axisX.setHomed(false);
        m_axisY.setHomed(false);
        m_state.setHomed(false, false);
      }
      sendAck(req.id, true, ErrorCode::Ok);
      emitSnapshot();
      break;
    }

    case CommandType::HomeStart: {
      if (!m_state.canHome()) {
        sendAck(req.id, false, ErrorCode::Busy);
        return;
      }
      snprintf(m_currentOpId, sizeof(m_currentOpId), "op-home-%u", ++m_opCounter);
      m_state.setState(MachineStateEnum::Homing);
      m_homing.start(req.axis, nowMs);
      m_opRunning = true;
      sendAck(req.id, true, ErrorCode::Ok, m_currentOpId);
      sendOperationEvent("operation.started", m_currentOpId, "homing");
      break;
    }

    case CommandType::MotionJog: {
      if (!m_state.canJog()) {
        sendAck(req.id, false, m_state.getState() == MachineStateEnum::Faulted ? ErrorCode::SafetyInterlockTripped : ErrorCode::Busy);
        return;
      }
      if (m_planner.isMoving()) {
        sendAck(req.id, false, ErrorCode::Busy);
        return;
      }
      if (m_state.isPositionValid()) {
        int32_t currentX = m_axisX.getPosition();
        int32_t currentY = m_axisY.getPosition();
        int32_t targetX = (req.axis == AxisTarget::X) ? (currentX + req.steps) : currentX;
        int32_t targetY = (req.axis == AxisTarget::Y) ? (currentY + req.steps) : currentY;
        if (targetX < 0 || targetX > MAX_TRAVEL_STEPS_X || targetY < 0 || targetY > MAX_TRAVEL_STEPS_Y) {
          sendAck(req.id, false, ErrorCode::OutOfBounds);
          return;
        }
      }
      int32_t dx = (req.axis == AxisTarget::X) ? req.steps : 0;
      int32_t dy = (req.axis == AxisTarget::Y) ? req.steps : 0;
      m_state.setState(MachineStateEnum::Moving);
      m_planner.startMove(dx, dy, req.speedSps, nowUs);
      sendAck(req.id, true, ErrorCode::Ok);
      break;
    }

    case CommandType::MotionMoveAbs: {
      if (m_state.getState() == MachineStateEnum::Faulted) {
        sendAck(req.id, false, ErrorCode::SafetyInterlockTripped);
        return;
      }
      if (!m_state.isPositionValid()) {
        sendAck(req.id, false, ErrorCode::NotHomed);
        return;
      }
      if (m_planner.isMoving() || m_state.getState() == MachineStateEnum::Moving || m_state.getState() == MachineStateEnum::RunningJob) {
        sendAck(req.id, false, ErrorCode::Busy);
        return;
      }
      int32_t dx = req.targetX - m_axisX.getPosition();
      int32_t dy = req.targetY - m_axisY.getPosition();
      m_state.setState(MachineStateEnum::Moving);
      m_planner.startMove(dx, dy, req.speedSps, nowUs);
      sendAck(req.id, true, ErrorCode::Ok);
      break;
    }

    case CommandType::LaserPulse: {
      if (m_state.getState() == MachineStateEnum::Faulted) {
        sendAck(req.id, false, ErrorCode::SafetyInterlockTripped);
        return;
      }
      bool ok = m_laser.pulse(req.duty, req.durationMs, nowMs);
      sendAck(req.id, ok, ok ? ErrorCode::Ok : ErrorCode::InvalidArgument);
      break;
    }

    case CommandType::LaserSetDuty: {
      if (m_state.getState() == MachineStateEnum::Faulted) {
        sendAck(req.id, false, ErrorCode::SafetyInterlockTripped);
        return;
      }
      m_laser.setDuty(req.duty, nowMs);
      sendAck(req.id, true, ErrorCode::Ok);
      break;
    }

    case CommandType::JobBegin: {
      if (!m_state.canStartJob()) {
        sendAck(req.id, false, ErrorCode::NotHomed);
        return;
      }
      m_state.setState(MachineStateEnum::RunningJob);
      sendAck(req.id, true, ErrorCode::Ok);
      break;
    }

    case CommandType::JobSegment: {
      if (m_state.getState() != MachineStateEnum::RunningJob) {
        sendAck(req.id, false, ErrorCode::NotHomed);
        return;
      }
      if (m_planner.isMoving()) {
        sendAck(req.id, false, ErrorCode::Busy);
        return;
      }
      if (m_state.isPositionValid()) {
        int32_t targetX = m_axisX.getPosition() + req.dx;
        int32_t targetY = m_axisY.getPosition() + req.dy;
        if (targetX < 0 || targetX > MAX_TRAVEL_STEPS_X || targetY < 0 || targetY > MAX_TRAVEL_STEPS_Y) {
          sendAck(req.id, false, ErrorCode::OutOfBounds);
          return;
        }
      }
      m_laser.setDuty(req.duty, nowMs);
      m_planner.startMove(req.dx, req.dy, req.speedSps, nowUs);
      sendAck(req.id, true, ErrorCode::Ok);
      break;
    }

    case CommandType::JobEnd: {
      m_laser.forceOff();
      m_state.setState(MachineStateEnum::IdleHomed);
      sendAck(req.id, true, ErrorCode::Ok);
      break;
    }

    default:
      sendAck(req.id, false, ErrorCode::UnknownCommand);
      break;
  }
}

void ProtocolDispatcher::update(uint32_t nowUs, uint32_t nowMs) {
  m_laser.checkWatchdog(nowMs);

  if (m_homing.isActive()) {
    bool ongoing = m_homing.tick(nowUs, nowMs);
    if (!ongoing) {
      // Homing finished or failed
      if (m_homing.getResult() == ErrorCode::Ok) {
        m_state.setHomed(m_axisX.isHomed(), m_axisY.isHomed());
        if (m_opRunning) {
          sendOperationCompleted(m_currentOpId, "success");
          m_opRunning = false;
        }
      } else {
        m_state.setFault(errorCodeToString(m_homing.getResult()));
        if (m_opRunning) {
          sendOperationCompleted(m_currentOpId, "failed");
          m_opRunning = false;
        }
      }
    }
  } else if (m_planner.isMoving()) {
    bool ongoing = m_planner.tick(nowUs);
    if (!ongoing) {
      if (m_state.getState() == MachineStateEnum::Moving) {
        m_state.setState(m_state.isPositionValid() ? MachineStateEnum::IdleHomed : MachineStateEnum::IdleUnhomed);
      }
    }
  }
}
