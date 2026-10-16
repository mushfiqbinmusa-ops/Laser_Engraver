#pragma once

#include <Arduino.h>

class MotionController {
 public:
  void begin();
  void moveTo(int32_t xTarget, int32_t yTarget, uint16_t laserDuty);
  void setPosition(int32_t x, int32_t y);
  int32_t x() const;
  int32_t y() const;

 private:
  int32_t x_ = 0;
  int32_t y_ = 0;
};
