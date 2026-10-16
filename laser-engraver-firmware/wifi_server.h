#pragma once

#include <Arduino.h>
#include <WiFi.h>

class WiFiServerController {
 public:
  void begin(const char* ssid, const char* password, uint16_t port);
  bool connected() const;
  String readCommand();
  void sendStatus(const String& message);

 private:
  WiFiServer server_{0};
  WiFiClient client_;
};
