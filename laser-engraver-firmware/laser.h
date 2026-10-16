#pragma once

#include <Arduino.h>

class LaserController {
 public:
  void begin();
  void setDuty(uint16_t duty);
  void off();
  void on(uint16_t duty);
  uint16_t getDuty() const;

 private:
  uint16_t duty_ = 0;
};
