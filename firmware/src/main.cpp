#ifdef ARDUINO
#include <Arduino.h>
#include <WiFi.h>

#include "config/pin_mapping.h"
#include "config/machine_limits.h"
#include "transport/line_reader.h"
#include "protocol/protocol_parser.h"
#include "protocol/protocol_dispatcher.h"
#include "state/machine_state.h"
#include "motion/stepper_axis.h"
#include "motion/bresenham_planner.h"
#include "homing/sensor_filter.h"
#include "homing/homing_sm.h"
#include "laser/laser_controller.h"

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

  // Microstepping bits (1/16 default: MS1=HIGH, MS2=HIGH, MS3=HIGH)
  pinMode(PIN_MS1, OUTPUT);
  digitalWrite(PIN_MS1, HIGH);
  pinMode(PIN_MS2, OUTPUT);
  digitalWrite(PIN_MS2, HIGH);
  pinMode(PIN_MS3, OUTPUT);
  digitalWrite(PIN_MS3, HIGH);

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

  // 4. Start Serial
  Serial.begin(SERIAL_BAUD);

  // 5. Start TCP Server (Wi-Fi in non-blocking mode or softAP if configured)
  s_tcpServer.begin();
  s_tcpServer.setNoDelay(true);

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
        char buf[128];
        ProtocolParser::serializeAck(ack, buf, sizeof(buf));
        sendOutput(buf);
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
          char buf[128];
          ProtocolParser::serializeAck(ack, buf, sizeof(buf));
          sendOutput(buf);
        }
      }
      s_serialLineReader.clearLine();
    }
  }

  // 2. Service TCP client
  if (!s_tcpClient || !s_tcpClient.connected()) {
    WiFiClient newClient = s_tcpServer.available();
    if (newClient) {
      s_tcpClient = newClient;
      s_tcpLineReader.reset();
      s_dispatcher.emitSnapshot(); // Emit snapshot immediately on connect!
    }
  } else {
    while (s_tcpClient.available()) {
      char c = (char)s_tcpClient.read();
      if (s_tcpLineReader.feedByte(c)) {
        if (s_tcpLineReader.hasOverflow()) {
          AckResponse ack;
          ack.v = 1;
          ack.accepted = false;
          ack.code = ErrorCode::CommandTooLong;
          char buf[128];
          ProtocolParser::serializeAck(ack, buf, sizeof(buf));
          sendOutput(buf);
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
            char buf[128];
            ProtocolParser::serializeAck(ack, buf, sizeof(buf));
            sendOutput(buf);
          }
        }
        s_tcpLineReader.clearLine();
      }
    }
  }

  // 3. Update optical sensor readings
  bool rawX = (digitalRead(PIN_SENSOR_X) == HIGH);
  bool rawY = (digitalRead(PIN_SENSOR_Y) == HIGH);
  s_sensors.update(rawX, rawY, nowMs);

  // 4. Update dispatcher (motion ticks, homing state machine, laser watchdog)
  s_dispatcher.update(nowUs, nowMs);
}
#endif
