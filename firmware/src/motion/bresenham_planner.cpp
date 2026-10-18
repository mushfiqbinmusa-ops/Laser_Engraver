#include "bresenham_planner.h"

BresenhamPlanner::BresenhamPlanner(StepperAxis& axisX, StepperAxis& axisY)
  : m_axisX(axisX),
    m_axisY(axisY),
    m_active(false),
    m_xForward(true),
    m_yForward(true),
    m_dx(0),
    m_dy(0),
    m_primarySteps(0),
    m_secondarySteps(0),
    m_xIsPrimary(true),
    m_error(0),
    m_stepIntervalUs(10000),
    m_lastStepUs(0),
    m_stepsRemaining(0) {}

bool BresenhamPlanner::startMove(int32_t dx, int32_t dy, uint32_t speedSps, uint32_t nowUs) {
  if (dx == 0 && dy == 0) {
    m_active = false;
    return false;
  }

  if (speedSps < MIN_SPEED_SPS) speedSps = MIN_SPEED_SPS;
  if (speedSps > MAX_SPEED_SPS) speedSps = MAX_SPEED_SPS;

  m_xForward = (dx >= 0);
  m_yForward = (dy >= 0);
  m_dx = abs(dx);
  m_dy = abs(dy);

  if (m_dx >= m_dy) {
    m_xIsPrimary = true;
    m_primarySteps = m_dx;
    m_secondarySteps = m_dy;
  } else {
    m_xIsPrimary = false;
    m_primarySteps = m_dy;
    m_secondarySteps = m_dx;
  }

  m_error = m_primarySteps / 2;
  m_stepsRemaining = m_primarySteps;
  m_stepIntervalUs = 1000000UL / speedSps;
  m_lastStepUs = nowUs;
  m_active = true;

  // Ensure drivers enabled
  m_axisX.setEnabled(true);
  m_axisY.setEnabled(true);

  return true;
}

bool BresenhamPlanner::tick(uint32_t nowUs) {
  if (!m_active) return false;

  if (nowUs - m_lastStepUs < m_stepIntervalUs) {
    return true; // Wait for next tick
  }
  m_lastStepUs = nowUs;

  if (m_xIsPrimary) {
    m_axisX.step(m_xForward);
    m_error -= m_secondarySteps;
    if (m_error < 0) {
      m_axisY.step(m_yForward);
      m_error += m_primarySteps;
    }
  } else {
    m_axisY.step(m_yForward);
    m_error -= m_secondarySteps;
    if (m_error < 0) {
      m_axisX.step(m_xForward);
      m_error += m_primarySteps;
    }
  }

  m_stepsRemaining--;
  if (m_stepsRemaining == 0) {
    m_active = false;
    return false; // Move complete
  }

  return true;
}

void BresenhamPlanner::abort() {
  m_active = false;
  m_stepsRemaining = 0;
}
