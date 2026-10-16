#include "gcode_parser.h"

bool parseCommand(const String& line, GCodeCommand& command, String& error) {
  String trimmed = line;
  trimmed.trim();
  if (trimmed.length() == 0) {
    error = "empty command";
    return false;
  }

  if (trimmed == "G28" || trimmed == "g28") {
    command.homing = true;
    return true;
  }

  if (trimmed == "!" || trimmed == "!\r") {
    command.emergencyStop = true;
    return true;
  }

  int xPos = trimmed.indexOf('X');
  int yPos = trimmed.indexOf('Y');
  int sPos = trimmed.indexOf('S');

  if (xPos >= 0) {
    command.hasX = true;
    command.x = trimmed.substring(xPos + 1).toFloat();
  }
  if (yPos >= 0) {
    command.hasY = true;
    command.y = trimmed.substring(yPos + 1).toFloat();
  }
  if (sPos >= 0) {
    command.hasLaser = true;
    command.laserDuty = trimmed.substring(sPos + 1).toInt();
  }

  if (trimmed.startsWith("G0") || trimmed.startsWith("G1") || trimmed.startsWith("M3")) {
    return true;
  }

  error = "unsupported command";
  return false;
}
