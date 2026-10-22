#include <Arduino.h>
#include <WiFi.h>

#include "src/config/pin_mapping.h"
#include "src/config/machine_limits.h"
#include "src/config/wifi_config.h"
#include "src/transport/line_reader.h"
#include "src/protocol/protocol_parser.h"
#include "src/protocol/protocol_dispatcher.h"
#include "src/state/machine_state.h"
#include "src/motion/stepper_axis.h"
#include "src/motion/bresenham_planner.h"
#include "src/homing/sensor_filter.h"
#include "src/homing/homing_sm.h"
#include "src/laser/laser_controller.h"

// Forward declarations & hardware adapters
static void nativeDigitalWrite(uint8_t pin, uint8_t val) {
  digitalWrite(pin, val);
}

static void nativeDelayUs(uint32_t us) {
  delayMicroseconds(us);
}

static void nativePwmWrite(uint8_t pin, uint32_t duty) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  ledcWrite(pin, duty);
#else
  ledcWrite(LEDC_CHANNEL, duty);
#endif
}

static void nativeSetMicrostepMode(uint8_t divisor) {
  switch (divisor) {
    case 2: // 1/2 Half step
      digitalWrite(PIN_MS1, HIGH);
      digitalWrite(PIN_MS2, LOW);
      digitalWrite(PIN_MS3, LOW);
      break;
    case 4: // 1/4 Quarter step
      digitalWrite(PIN_MS1, LOW);
      digitalWrite(PIN_MS2, HIGH);
      digitalWrite(PIN_MS3, LOW);
      break;
    case 8: // 1/8 Eighth step
      digitalWrite(PIN_MS1, HIGH);
      digitalWrite(PIN_MS2, HIGH);
      digitalWrite(PIN_MS3, LOW);
      break;
    case 16: // 1/16 Sixteenth step
      digitalWrite(PIN_MS1, HIGH);
      digitalWrite(PIN_MS2, HIGH);
      digitalWrite(PIN_MS3, HIGH);
      break;
    case 1: // 1/1 Full step
    default:
      digitalWrite(PIN_MS1, LOW);
      digitalWrite(PIN_MS2, LOW);
      digitalWrite(PIN_MS3, LOW);
      break;
  }
}

// Global Subsystems
static MachineStateManager s_machineState;
static StepperAxis s_axisX(PIN_STEP_X, PIN_DIR_X, PIN_ENABLE_X, false);
static StepperAxis s_axisY(PIN_STEP_Y, PIN_DIR_Y, PIN_ENABLE_Y, false);
static BresenhamPlanner s_planner(s_axisX, s_axisY);
static SensorFilter s_sensors;
static HomingStateMachine s_homing(s_axisX, s_axisY, s_sensors);
static LaserController s_laser;
static ProtocolDispatcher s_dispatcher(s_machineState, s_axisX, s_axisY, s_planner, s_homing, s_laser, s_sensors);

static LineReader s_serialLineReader;
static LineReader s_tcpLineReader;
static WiFiServer s_tcpServer(TCP_PORT);
static WiFiClient s_tcpClient;

static void sendOutput(const char* line) {
  Serial.print(line);
  if (s_tcpClient && s_tcpClient.connected()) {
    s_tcpClient.print(line);
  }
}

void setup() {
  setCpuFrequencyMhz(CPU_FREQ_MHZ);

  // 1. NON-NEGOTIABLE SAFETY FIRST: Force outputs safe before Wi-Fi or Serial
  pinMode(PIN_LASER_PWM, OUTPUT);
  digitalWrite(PIN_LASER_PWM, LOW);

  pinMode(PIN_ENABLE_X, OUTPUT);
  digitalWrite(PIN_ENABLE_X, HIGH); // Disable stepper driver

  pinMode(PIN_ENABLE_Y, OUTPUT);
  digitalWrite(PIN_ENABLE_Y, HIGH); // Disable stepper driver

  pinMode(PIN_STEP_X, OUTPUT);
  digitalWrite(PIN_STEP_X, LOW);
  pinMode(PIN_DIR_X, OUTPUT);
  digitalWrite(PIN_DIR_X, LOW);

  pinMode(PIN_STEP_Y, OUTPUT);
  digitalWrite(PIN_STEP_Y, LOW);
  pinMode(PIN_DIR_Y, OUTPUT);
  digitalWrite(PIN_DIR_Y, LOW);

  // Microstepping bits (MS1, MS2, MS3)
  pinMode(PIN_MS1, OUTPUT);
  pinMode(PIN_MS2, OUTPUT);
  pinMode(PIN_MS3, OUTPUT);
  nativeSetMicrostepMode(1); // Default full-step mode for low-cost motors

  // Sensors (INPUT_PULLUP)
  pinMode(PIN_SENSOR_X, INPUT_PULLUP);
  pinMode(PIN_SENSOR_Y, INPUT_PULLUP);

  // 2. Setup LEDC PWM
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  ledcAttachChannel(PIN_LASER_PWM, DEFAULT_PWM_FREQ_HZ, LEDC_RES_BITS, LEDC_CHANNEL);
#else
  ledcSetup(LEDC_CHANNEL, DEFAULT_PWM_FREQ_HZ, LEDC_RES_BITS);
  ledcAttachPin(PIN_LASER_PWM, LEDC_CHANNEL);
#endif
  nativePwmWrite(PIN_LASER_PWM, 0);

  // 3. Initialize Subsystems
  s_axisX.init(nativeDigitalWrite, nativeDelayUs);
  s_axisY.init(nativeDigitalWrite, nativeDelayUs);
  s_laser.init(nativePwmWrite);
  s_dispatcher.setSendCallback(sendOutput);
  s_dispatcher.setMicrostepCallback(nativeSetMicrostepMode);

  // 4. Start Serial
  Serial.begin(SERIAL_BAUD);

  // 5. Initialize Wi-Fi (Station Mode with SoftAP Fallback)
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("Laser-XY-Scanner");
  WiFi.setSleep(true);
  WiFi.setTxPower(WIFI_POWER_11dBm);
  delay(100);
  bool connected = false;

  // Check if custom credentials were provided
  if (strlen(WIFI_SSID) > 0 && strcmp(WIFI_SSID, "YOUR_WIFI_SSID") != 0) {
    Serial.printf("\n[Wi-Fi] Connecting to '%s'...\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    uint32_t startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startAttemptTime) < 25000) {
      delay(400);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
      connected = true;
      Serial.println("\n[Wi-Fi] Connected successfully!");
      Serial.print("[Wi-Fi] IP Address: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.printf("\n[Wi-Fi] Station connection timed out (status: %d).\n", (int)WiFi.status());
    }
  }

  // If station is not connected, either bring up SoftAP or disable the radio
  if (!connected) {
#if WIFI_AP_FALLBACK
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
    Serial.println("[Wi-Fi] SoftAP Access Point started.");
    Serial.printf("[Wi-Fi] Connect your PC to SSID: %s (Password: %s)\n", WIFI_AP_SSID, WIFI_AP_PASS);
    Serial.print("[Wi-Fi] ESP32 AP IP Address: ");
    Serial.println(WiFi.softAPIP());
#else
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("[Wi-Fi] Station unavailable; serial-only mode (radio off).");
#endif
  }

  // 6. Start TCP Machine Control Socket
  s_tcpServer.begin(WIFI_TCP_PORT);
  s_tcpServer.setNoDelay(true);
  Serial.printf("[TCP] Machine control socket listening on port %d\n", WIFI_TCP_PORT);

  // Ready!
  s_machineState.setState(MachineStateEnum::IdleUnhomed);
}

void loop() {
  uint32_t nowUs = micros();
  uint32_t nowMs = millis();

  // 1. Service Serial input
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (s_serialLineReader.feedByte(c)) {
      if (s_serialLineReader.hasOverflow()) {
        AckResponse ack;
        ack.v = 1;
        ack.accepted = false;
        ack.code = ErrorCode::CommandTooLong;
        char buf[MAX_FRAME_SIZE] = {0};
        if (ProtocolParser::serializeAck(ack, buf, sizeof(buf)) > 0) {
          sendOutput(buf);
        }
      } else {
        CommandRequest req;
        ErrorCode err = ProtocolParser::parseLine(s_serialLineReader.getLine(), s_serialLineReader.getLength(), req);
        if (err == ErrorCode::Ok) {
          s_dispatcher.dispatch(req, nowUs, nowMs);
        } else {
          AckResponse ack;
          ack.v = 1;
          ack.accepted = false;
          ack.code = err;
          char buf[MAX_FRAME_SIZE] = {0};
          if (ProtocolParser::serializeAck(ack, buf, sizeof(buf)) > 0) {
            sendOutput(buf);
          }
        }
      }
      s_serialLineReader.clearLine();
    }
  }

  // 2. Service TCP client
  if (s_tcpServer.hasClient()) {
    WiFiClient incoming = s_tcpServer.accept();
    if (incoming) {
      if (s_tcpClient) {
        s_tcpClient.stop();
      }
      s_tcpClient = incoming;
      s_tcpClient.setNoDelay(true);
      s_tcpLineReader.reset();
      s_dispatcher.emitSnapshot(); // Emit snapshot immediately on connect!
    }
  }

  if (s_tcpClient) {
    if (!s_tcpClient.connected()) {
      s_tcpClient.stop();
    } else {
      while (s_tcpClient.available()) {
        char c = (char)s_tcpClient.read();
        if (s_tcpLineReader.feedByte(c)) {
          if (s_tcpLineReader.hasOverflow()) {
            AckResponse ack;
            ack.v = 1;
            ack.accepted = false;
            ack.code = ErrorCode::CommandTooLong;
            char buf[MAX_FRAME_SIZE] = {0};
            if (ProtocolParser::serializeAck(ack, buf, sizeof(buf)) > 0) {
              sendOutput(buf);
            }
          } else {
            CommandRequest req;
            ErrorCode err = ProtocolParser::parseLine(s_tcpLineReader.getLine(), s_tcpLineReader.getLength(), req);
            if (err == ErrorCode::Ok) {
              s_dispatcher.dispatch(req, nowUs, nowMs);
            } else {
              AckResponse ack;
              ack.v = 1;
              ack.accepted = false;
              ack.code = err;
              char buf[MAX_FRAME_SIZE] = {0};
              if (ProtocolParser::serializeAck(ack, buf, sizeof(buf)) > 0) {
                sendOutput(buf);
              }
            }
          }
          s_tcpLineReader.clearLine();
        }
      }
    }
  }

  // 3. Update optical sensor readings
  bool rawX = (digitalRead(PIN_SENSOR_X) == HIGH);
  bool rawY = (digitalRead(PIN_SENSOR_Y) == HIGH);
  s_sensors.update(rawX, rawY, nowMs);

  // 4. Update dispatcher (motion ticks, homing state machine, laser watchdog)
  s_dispatcher.update(nowUs, nowMs);

  if (!s_dispatcher.isActive()) {
    delay(IDLE_LOOP_DELAY_MS);
  }
}
