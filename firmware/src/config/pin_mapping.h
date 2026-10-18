#pragma once

#include <stdint.h>

// =============================================================================
//  ESP32-S3 SuperMini Authoritative Pin Mapping (`esp32s3_a4988_v1`)
//  Resolves GPIO 10 conflict:
//    - Motor B (Y) DIR is GPIO 10
//    - Laser Diode PWM is GPIO 12
//    - Sensor X is GPIO 13 (INPUT ONLY; NO LED output)
// =============================================================================

// Microstep Mode Selection (Shared across Motor A and Motor B A4988 drivers)
constexpr uint8_t PIN_MS1            = 1;   // Microstep select bit 1
constexpr uint8_t PIN_MS2            = 2;   // Microstep select bit 2
constexpr uint8_t PIN_MS3            = 4;   // Microstep select bit 3

// Motor A (X-Axis) Pins
constexpr uint8_t PIN_STEP_X         = 5;   // A4988 Step pulse (active rising edge)
constexpr uint8_t PIN_DIR_X          = 6;   // A4988 Direction
constexpr uint8_t PIN_ENABLE_X       = 7;   // A4988 Enable (Active LOW, HIGH=Disabled)

// Optical Endstop Sensors (Active LOW with internal pull-up)
constexpr uint8_t PIN_SENSOR_Y       = 8;   // Y-Axis IR Sensor (FC-51 / KY-032 LM393)
constexpr uint8_t PIN_SENSOR_X       = 13;  // X-Axis IR Sensor (INPUT_PULLUP; NEVER OUTPUT)

// Motor B (Y-Axis) Pins
constexpr uint8_t PIN_STEP_Y         = 9;   // A4988 Step pulse (active rising edge)
constexpr uint8_t PIN_DIR_Y          = 10;  // A4988 Direction
constexpr uint8_t PIN_ENABLE_Y       = 11;  // A4988 Enable (Active LOW, HIGH=Disabled)

// Laser Diode PWM Output
constexpr uint8_t PIN_LASER_PWM      = 12;  // Laser Driver PWM (5 kHz, 12-bit LEDC, ≤ 90% hard clamp)

// Logic Levels
constexpr uint8_t DRIVER_ENABLE_LEVEL  = 0; // LOW = Drivers energized
constexpr uint8_t DRIVER_DISABLE_LEVEL = 1; // HIGH = Outputs high-impedance
constexpr uint8_t DIR_CW_LEVEL         = 1; // HIGH = Clockwise / Positive travel
constexpr uint8_t DIR_CCW_LEVEL        = 0; // LOW  = Counter-Clockwise / Negative travel
constexpr uint8_t SENSOR_ACTIVE_LEVEL  = 0; // LOW  = Beam broken / obstacle detected
constexpr uint8_t SENSOR_CLEAR_LEVEL   = 1; // HIGH = Beam clear
constexpr uint8_t LASER_OFF_LEVEL      = 0; // LOW  = 0V TTL (Laser disabled)
