#pragma once

#include "stepper_axis.h"
#include <stdlib.h>

class BresenhamPlanner {
public:
  BresenhamPlanner(StepperAxis& axisX, StepperAxis& axisY);

  // Configure a coordinated relative move (dx, dy in steps)
  bool startMove(int32_t dx, int32_t dy, uint32_t speedSps, uint32_t nowUs);

  // Non-blocking tick. Returns true if motion is ongoing, false if completed or idle.
  bool tick(uint32_t nowUs);

  void abort();

  bool isMoving() const { return m_active; }
  uint32_t getStepsRemaining() const { return m_stepsRemaining; }

private:
  StepperAxis& m_axisX;
  StepperAxis& m_axisY;

  bool m_active;
  bool m_xForward;
  bool m_yForward;
  int32_t m_dx;
  int32_t m_dy;
  int32_t m_primarySteps;
  int32_t m_secondarySteps;
  bool m_xIsPrimary;
  int32_t m_error;
  uint32_t m_stepIntervalUs;
  uint32_t m_lastStepUs;
  uint32_t m_stepsRemaining;
};
