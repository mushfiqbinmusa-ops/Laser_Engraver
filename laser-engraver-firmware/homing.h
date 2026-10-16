#pragma once

#include <Arduino.h>

class HomingController {
 public:
  void begin();
  bool homeX();
  bool homeY();
  bool homeAll();

 private:
  bool readEndstop(uint8_t pin) const;
};
