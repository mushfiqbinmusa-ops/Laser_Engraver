#pragma once

#include <Arduino.h>

struct MachineStatus {
  float x;
  float y;
  int laser;
  const char* state;
  bool endstop_x;
  bool endstop_y;
  int progress;
};

String serializeStatus(const MachineStatus& status);
