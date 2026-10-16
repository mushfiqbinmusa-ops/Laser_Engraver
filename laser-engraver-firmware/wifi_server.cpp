#include "wifi_server.h"

void WiFiServerController::begin(const char* ssid, const char* password, uint16_t port) {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  server_ = WiFiServer(port);
  server_.begin();
}

bool WiFiServerController::connected() const {
  return client_.connected();
}

String WiFiServerController::readCommand() {
  if (!server_.hasClient()) {
    return "";
  }
  client_ = server_.available();
  if (!client_) {
    return "";
  }

  String line = client_.readStringUntil('\n');
  line.trim();
  return line;
}

void WiFiServerController::sendStatus(const String& message) {
  if (client_.connected()) {
    client_.println(message);
  }
}
