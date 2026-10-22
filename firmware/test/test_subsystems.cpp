#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <cstring>

#include "../src/config/pin_mapping.h"
#include "../src/config/machine_limits.h"
#include "../src/motion/stepper_axis.h"
#include "../src/motion/bresenham_planner.h"
#include "../src/homing/sensor_filter.h"
#include "../src/homing/homing_sm.h"
#include "../src/laser/laser_controller.h"
#include "../src/state/machine_state.h"
#include "../src/protocol/protocol_dispatcher.h"

// Mock GPIO & Timers
static uint32_t s_mockPinStepCountX = 0;
static uint32_t s_mockPinStepCountY = 0;
static uint32_t s_mockLaserPwmVal = 0;

static void mockDigitalWrite(uint8_t pin, uint8_t val) {
  if (pin == PIN_STEP_X && val == 1) s_mockPinStepCountX++;
  if (pin == PIN_STEP_Y && val == 1) s_mockPinStepCountY++;
}

static void mockDelayUs(uint32_t) {}

static void mockPwmWrite(uint8_t pin, uint32_t duty) {
  if (pin == PIN_LASER_PWM) s_mockLaserPwmVal = duty;
}

static std::vector<std::string> s_sentLines;
static void mockSendLine(const char* line) {
  s_sentLines.push_back(line);
}

int main() {
  std::cout << "[TEST] Running Firmware Subsystems Native Unit Tests..." << std::endl;

  // 1. StepperAxis Test
  {
    StepperAxis axisX(PIN_STEP_X, PIN_DIR_X, PIN_ENABLE_X, false);
    axisX.init(mockDigitalWrite, mockDelayUs);
    assert(axisX.getPosition() == 0);
    assert(!axisX.isEnabled());

    axisX.step(true); // forward
    assert(axisX.getPosition() == 1);
    assert(axisX.isEnabled());

    axisX.step(false); // backward
    assert(axisX.getPosition() == 0);
    std::cout << "  ✓ StepperAxis primitive verified" << std::endl;
  }

  // 2. Bresenham Motion Planner Test
  {
    s_mockPinStepCountX = 0;
    s_mockPinStepCountY = 0;
    StepperAxis axisX(PIN_STEP_X, PIN_DIR_X, PIN_ENABLE_X, false);
    StepperAxis axisY(PIN_STEP_Y, PIN_DIR_Y, PIN_ENABLE_Y, false);
    axisX.init(mockDigitalWrite, mockDelayUs);
    axisY.init(mockDigitalWrite, mockDelayUs);

    BresenhamPlanner planner(axisX, axisY);
    uint32_t timeUs = 1000;
    bool ok = planner.startMove(100, 50, 1000, timeUs);
    assert(ok);
    assert(planner.isMoving());
    assert(planner.getStepsRemaining() == 100);

    // Run until complete
    while (planner.isMoving()) {
      timeUs += 1000;
      planner.tick(timeUs);
    }
    assert(axisX.getPosition() == 100);
    assert(axisY.getPosition() == 50);
    assert(s_mockPinStepCountX == 100);
    assert(s_mockPinStepCountY == 50);
    std::cout << "  ✓ BresenhamPlanner coordinated moves verified" << std::endl;
  }

  // 3. Laser Controller with Safety Clamp & Watchdog
  {
    LaserController laser;
    laser.init(mockPwmWrite);
    assert(laser.getDuty() == 0);
    assert(!laser.isActive());

    // 90% hard clamp test
    laser.setDuty(4095, 1000); // Exceeds SAFETY_MAX_DUTY (3685)
    assert(laser.getDuty() == SAFETY_MAX_DUTY);
    assert(laser.isHardClamped());
    assert(s_mockLaserPwmVal == SAFETY_MAX_DUTY);

    // Watchdog auto-off test
    laser.checkWatchdog(1000 + 400); // 400ms elapsed, still active
    assert(laser.getDuty() == SAFETY_MAX_DUTY);

    laser.checkWatchdog(1000 + 501); // 501ms elapsed, watchdog MUST cut output!
    assert(laser.getDuty() == 0);
    assert(!laser.isActive());
    assert(s_mockLaserPwmVal == 0);
    std::cout << "  ✓ LaserController 90% safety clamp and 500ms watchdog verified" << std::endl;

    // Guarded pulse auto-off test
    laser.pulse(1000, 50, 2000);
    assert(laser.getDuty() == 1000);
    laser.checkWatchdog(2000 + 40);
    assert(laser.getDuty() == 1000);
    laser.checkWatchdog(2000 + 51); // Pulse expired
    assert(laser.getDuty() == 0);
    std::cout << "  ✓ Laser guarded pulse auto-off verified" << std::endl;
  }

  // 4. Sensor Filter Debounce Test
  {
    SensorFilter filter;
    // Initial: rawHigh = true (clear)
    filter.update(true, true, 100);
    assert(!filter.isDetectedX());
    assert(!filter.isDetectedY());

    // Trigger candidate (raw = false, 0V)
    filter.update(false, true, 101); // sample 1
    assert(!filter.isDetectedX()); // Not yet stable

    filter.update(false, true, 102); // sample 2
    assert(!filter.isDetectedX());

    filter.update(false, true, 104); // sample 3 and >= 3ms
    assert(filter.isDetectedX()); // Stable detected!
    std::cout << "  ✓ SensorFilter 3-sample/3ms debounce verified" << std::endl;
  }

  // 5. Homing State Machine Multi-Phase Test
  {
    StepperAxis axisX(PIN_STEP_X, PIN_DIR_X, PIN_ENABLE_X, false);
    StepperAxis axisY(PIN_STEP_Y, PIN_DIR_Y, PIN_ENABLE_Y, false);
    axisX.init(mockDigitalWrite, mockDelayUs);
    axisY.init(mockDigitalWrite, mockDelayUs);
    SensorFilter filter;

    HomingStateMachine homing(axisX, axisY, filter);
    uint32_t nowUs = 10000;
    uint32_t nowMs = 10;

    // Start homing X
    filter.update(true, true, nowMs); // sensor clear
    bool started = homing.start(AxisTarget::X, nowMs);
    assert(started);
    assert(homing.isActive());
    assert(homing.getPhase() == HomePhase::FastSeek);

    // Step forward until sensor triggers
    for (int i = 0; i < 20; i++) {
      nowUs += 100000;
      nowMs += 100;
      homing.tick(nowUs, nowMs);
    }
    // Simulate optical beam break
    filter.update(false, true, nowMs);
    filter.update(false, true, nowMs + 1);
    filter.update(false, true, nowMs + 4);
    assert(filter.isDetectedX());

    // Next tick transitions to PullOff
    nowUs += 100000;
    nowMs += 100;
    homing.tick(nowUs, nowMs);
    assert(homing.getPhase() == HomePhase::PullOff);

    // Simulate beam clear during pull-off (must advance >= 20 steps)
    filter.update(true, true, nowMs);
    filter.update(true, true, nowMs + 1);
    filter.update(true, true, nowMs + 4);
    for (int s = 0; s < 25; s++) {
      nowUs += 100000;
      nowMs += 100;
      homing.tick(nowUs, nowMs);
    }
    assert(homing.getPhase() == HomePhase::SlowLatch);

    // Simulate precision latch
    filter.update(false, true, nowMs);
    filter.update(false, true, nowMs + 1);
    filter.update(false, true, nowMs + 4);
    nowUs += 100000;
    nowMs += 100;
    homing.tick(nowUs, nowMs);
    assert(homing.getPhase() == HomePhase::FinalPullOff);

    // Simulate final pull-off clearing the slot (HOMING_FINAL_PULLOFF_STEPS = 200)
    for (int s = 0; s < 205; s++) {
      nowUs += 100000;
      nowMs += 100;
      homing.tick(nowUs, nowMs);
    }
    assert(!homing.isActive());
    assert(homing.getPhase() == HomePhase::Complete);
    assert(axisX.isHomed());
    assert(axisX.getPosition() == 0);
    std::cout << "  ✓ HomingStateMachine 5-phase execution verified" << std::endl;
  }

  // 5b. Bounded Oscillation Search Test (no sensor trigger)
  {
    StepperAxis axisX(PIN_STEP_X, PIN_DIR_X, PIN_ENABLE_X, false);
    StepperAxis axisY(PIN_STEP_Y, PIN_DIR_Y, PIN_ENABLE_Y, false);
    axisX.init(mockDigitalWrite, mockDelayUs);
    axisY.init(mockDigitalWrite, mockDelayUs);
    SensorFilter filter;
    HomingStateMachine homing(axisX, axisY, filter);

    filter.update(true, true, 0); // sensor clear
    assert(homing.start(AxisTarget::X, 0));
    assert(homing.getPhase() == HomePhase::FastSeek);

    const int32_t sweep = static_cast<int32_t>((HOMING_SWEEP_STEPS_REF * 1U) / 16U);
    int32_t minPos = 0;
    int32_t maxPos = 0;
    uint32_t nowUs = 0;
    uint32_t nowMs = 0;
    for (int i = 0; i < 2400 && homing.isActive(); i++) {
      nowUs += 20000;
      nowMs += 20;
      homing.tick(nowUs, nowMs);
      const int32_t pos = axisX.getPosition();
      if (pos < minPos) minPos = pos;
      if (pos > maxPos) maxPos = pos;
    }

    assert(!homing.isActive());
    assert(homing.getResult() == ErrorCode::HomeSeekLimit);
    assert(maxPos <= sweep);
    assert(minPos >= -sweep);
    assert(maxPos >= sweep - 1);
    assert(minPos <= -sweep + 1);
    std::cout << "  ✓ Homing bounded +/-30 degree oscillation verified" << std::endl;
  }

  // 5c. Stuck Active Sensor Must Fail Within the Sweep Envelope
  {
    StepperAxis axisX(PIN_STEP_X, PIN_DIR_X, PIN_ENABLE_X, false);
    StepperAxis axisY(PIN_STEP_Y, PIN_DIR_Y, PIN_ENABLE_Y, false);
    axisX.init(mockDigitalWrite, mockDelayUs);
    axisY.init(mockDigitalWrite, mockDelayUs);
    SensorFilter filter;
    HomingStateMachine homing(axisX, axisY, filter);

    filter.update(false, true, 0);
    filter.update(false, true, 1);
    filter.update(false, true, 4); // stable detected
    assert(filter.isDetectedX());
    assert(homing.start(AxisTarget::X, 10));
    assert(homing.getPhase() == HomePhase::ClearInitial);

    uint32_t nowUs = 10000;
    uint32_t nowMs = 10;
    for (int i = 0; i < 900 && homing.isActive(); i++) {
      nowUs += 20000;
      nowMs += 20;
      homing.tick(nowUs, nowMs);
      filter.update(false, true, nowMs); // remain blocked
    }

    assert(!homing.isActive());
    assert(homing.getResult() == ErrorCode::SensorStuckActive);
    const int32_t sweep = static_cast<int32_t>((HOMING_SWEEP_STEPS_REF * 1U) / 16U);
    assert(axisX.getPosition() >= -sweep);
    std::cout << "  ✓ Stuck-active sensor fails inside sweep envelope" << std::endl;
  }

  // 6. Protocol Dispatcher Integration Test
  {
    s_sentLines.clear();
    MachineStateManager state;
    StepperAxis axisX(PIN_STEP_X, PIN_DIR_X, PIN_ENABLE_X, false);
    StepperAxis axisY(PIN_STEP_Y, PIN_DIR_Y, PIN_ENABLE_Y, false);
    axisX.init(mockDigitalWrite, mockDelayUs);
    axisY.init(mockDigitalWrite, mockDelayUs);
    BresenhamPlanner planner(axisX, axisY);
    SensorFilter filter;
    HomingStateMachine homing(axisX, axisY, filter);
    LaserController laser;
    laser.init(mockPwmWrite);

    ProtocolDispatcher dispatcher(state, axisX, axisY, planner, homing, laser, filter);
    dispatcher.setSendCallback(mockSendLine);

    CommandRequest req;
    req.v = 1;
    strcpy(req.id, "test-ping-1");
    req.type = CommandType::SystemPing;

    dispatcher.dispatch(req, 1000, 10);
    assert(s_sentLines.size() == 1);
    assert(s_sentLines[0].find("\"code\":\"OK\"") != std::string::npos);

    // Move Abs when not homed -> rejected with NOT_HOMED
    s_sentLines.clear();
    CommandRequest reqMove;
    reqMove.v = 1;
    strcpy(reqMove.id, "test-move-1");
    reqMove.type = CommandType::MotionMoveAbs;
    reqMove.targetX = 100;
    reqMove.targetY = 100;
    reqMove.speedSps = 100;

    dispatcher.dispatch(reqMove, 1000, 10);
    assert(s_sentLines.size() == 1);
    assert(s_sentLines[0].find("\"code\":\"NOT_HOMED\"") != std::string::npos);
    std::cout << "  ✓ Dispatcher state gating verified (MoveAbs rejected when unhomed)" << std::endl;

    // Home start must invalidate the state manager consistently.
    s_sentLines.clear();
    CommandRequest reqHome;
    reqHome.v = 1;
    strcpy(reqHome.id, "test-home-1");
    reqHome.type = CommandType::HomeStart;
    reqHome.axis = AxisTarget::X;
    dispatcher.dispatch(reqHome, 1000, 10);
    assert(state.getState() == MachineStateEnum::Homing);
    assert(!state.isPositionValid());

    // STOP mid-home must leave the interrupted axis unhomed.
    s_sentLines.clear();
    CommandRequest reqStop;
    reqStop.v = 1;
    strcpy(reqStop.id, "test-stop-1");
    reqStop.type = CommandType::SystemStop;
    dispatcher.dispatch(reqStop, 2000, 20);
    assert(state.getState() == MachineStateEnum::IdleUnhomed);
    assert(!state.isPositionValid());
    assert(!axisX.isHomed());
    std::cout << "  ✓ STOP mid-home invalidates position verified" << std::endl;

    // Simulate a fully homed machine, then E-Stop.
    axisX.setHomed(true);
    axisY.setHomed(true);
    state.setHomed(true, true);
    assert(state.getState() == MachineStateEnum::IdleHomed);

    s_sentLines.clear();
    CommandRequest reqEstop;
    reqEstop.v = 1;
    strcpy(reqEstop.id, "test-estop");
    reqEstop.type = CommandType::SystemEStop;

    dispatcher.dispatch(reqEstop, 3000, 30);
    assert(state.getState() == MachineStateEnum::Faulted);
    assert(strcmp(state.getFault(), "ESTOP_ACTIVATED") == 0);
    assert(!axisX.isHomed());
    assert(!axisY.isHomed());
    assert(!state.isPositionValid());

    // Homing while faulted must be rejected as a safety interlock, not BUSY.
    s_sentLines.clear();
    CommandRequest reqHomeFault;
    reqHomeFault.v = 1;
    strcpy(reqHomeFault.id, "test-home-fault");
    reqHomeFault.type = CommandType::HomeStart;
    reqHomeFault.axis = AxisTarget::X;
    dispatcher.dispatch(reqHomeFault, 3100, 31);
    assert(s_sentLines.size() == 1);
    assert(s_sentLines[0].find("SAFETY_INTERLOCK_TRIPPED") != std::string::npos);

    // Reset Fault returns to a clean unhomed state; homing is allowed again.
    s_sentLines.clear();
    CommandRequest reqReset;
    reqReset.v = 1;
    strcpy(reqReset.id, "test-reset");
    reqReset.type = CommandType::SystemResetFault;
    dispatcher.dispatch(reqReset, 3200, 32);
    assert(state.getState() == MachineStateEnum::IdleUnhomed);
    assert(!state.isPositionValid());

    s_sentLines.clear();
    CommandRequest reqHomeAfterReset;
    reqHomeAfterReset.v = 1;
    strcpy(reqHomeAfterReset.id, "test-home-2");
    reqHomeAfterReset.type = CommandType::HomeStart;
    reqHomeAfterReset.axis = AxisTarget::X;
    dispatcher.dispatch(reqHomeAfterReset, 3300, 33);
    assert(!s_sentLines.empty());
    assert(s_sentLines[0].find("\"code\":\"OK\"") != std::string::npos);
    assert(state.getState() == MachineStateEnum::Homing);
    std::cout << "  ✓ E-Stop invalidation, interlock, and reset verified" << std::endl;
  }

  std::cout << "[SUCCESS] 100% of Firmware Subsystem tests passed!" << std::endl;
  return 0;
}
