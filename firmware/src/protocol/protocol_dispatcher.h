#pragma once

#include "protocol_parser.h"
#include "../state/machine_state.h"
#include "../motion/stepper_axis.h"
#include "../motion/bresenham_planner.h"
#include "../homing/homing_sm.h"
#include "../laser/laser_controller.h"

using SendLineFn = void (*)(const char* line);

class ProtocolDispatcher {
public:
  ProtocolDispatcher(MachineStateManager& state,
                     StepperAxis& axisX,
                     StepperAxis& axisY,
                     BresenhamPlanner& planner,
                     HomingStateMachine& homing,
                     LaserController& laser,
                     SensorFilter& sensors);

  void setSendCallback(SendLineFn sendFn) { m_sendFn = sendFn; }

  // Dispatch an incoming command request. Emits immediate ACK and triggers action.
  void dispatch(const CommandRequest& req, uint32_t nowUs, uint32_t nowMs);

  // Cooperative loop update (serves motion, homing, watchdog, and event emission)
  void update(uint32_t nowUs, uint32_t nowMs);

  void emitSnapshot();

private:
  void sendAck(const char* reqId, bool accepted, ErrorCode code, const char* opId = nullptr);
  void sendOperationEvent(const char* type, const char* opId, const char* phase, uint32_t steps = 0);
  void sendOperationCompleted(const char* opId, const char* result);

  MachineStateManager& m_state;
  StepperAxis& m_axisX;
  StepperAxis& m_axisY;
  BresenhamPlanner& m_planner;
  HomingStateMachine& m_homing;
  LaserController& m_laser;
  SensorFilter& m_sensors;

  SendLineFn m_sendFn;
  char m_currentOpId[32];
  uint32_t m_opCounter;
  bool m_opRunning;
};
