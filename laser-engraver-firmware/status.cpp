#include "status.h"

String serializeStatus(const MachineStatus& status) {
  String json = "{";
  json += "\"x\":" + String(status.x, 2) + ",";
  json += "\"y\":" + String(status.y, 2) + ",";
  json += "\"laser\":" + String(status.laser) + ",";
  json += "\"state\":\"" + String(status.state) + "\",";
  json += "\"progress\":" + String(status.progress) + ",";
  json += "\"endstop_x\":" + String(status.endstop_x ? "true" : "false") + ",";
  json += "\"endstop_y\":" + String(status.endstop_y ? "true" : "false") + "}";
  return json;
}
