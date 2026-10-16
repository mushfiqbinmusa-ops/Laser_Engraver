#include "config.h"
#include "gcode_parser.h"
#include "homing.h"
#include "laser.h"
#include "motion.h"
#include "status.h"
#include "wifi_server.h"

LaserController laser;
MotionController motion;
HomingController homing;
WiFiServerController wifi;
String lastCommand = "";

void setup() {
  Serial.begin(115200);
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, HIGH);

  motion.begin();
  laser.begin();
  homing.begin();
  wifi.begin(WIFI_SSID, WIFI_PASSWORD, TCP_PORT);

  Serial.println("Laser engraver ready");
}

void loop() {
  lastCommand = wifi.readCommand();
  if (lastCommand.length() > 0) {
    Serial.println("Command: " + lastCommand);

    GCodeCommand command;
    String error;
    if (parseCommand(lastCommand, command, error)) {
      if (command.homing) {
        homing.homeAll();
        wifi.sendStatus("ok");
      } else if (command.emergencyStop) {
        laser.off();
        wifi.sendStatus("ok");
      } else {
        if (command.hasLaser) {
          laser.on(command.laserDuty);
        }
        if (command.hasX || command.hasY) {
          int32_t xTarget = command.hasX ? (int32_t)command.x : motion.x();
          int32_t yTarget = command.hasY ? (int32_t)command.y : motion.y();
          motion.moveTo(xTarget, yTarget, command.laserDuty);
          motion.setPosition(xTarget, yTarget);
        }
        wifi.sendStatus("ok");
      }
    } else {
      wifi.sendStatus("error: " + error);
    }
  }

  if (Serial.available()) {
    String serialLine = Serial.readStringUntil('\n');
    serialLine.trim();
    if (serialLine.length() > 0) {
      GCodeCommand command;
      String error;
      if (parseCommand(serialLine, command, error)) {
        Serial.println("ok");
      } else {
        Serial.println("error: " + error);
      }
    }
  }

  delay(10);
}
