#pragma once

#include <Arduino.h>

struct GCodeCommand {
  bool hasX = false;
  bool hasY = false;
  bool hasLaser = false;
  float x = 0.0f;
  float y = 0.0f;
  int laserDuty = 0;
  bool homing = false;
  bool emergencyStop = false;
};

bool parseCommand(const String& line, GCodeCommand& command, String& error);
