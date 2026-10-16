#pragma once

#include <Arduino.h>

#define WIFI_SSID "YourSSID"
#define WIFI_PASSWORD "YourPassword"
#define TCP_PORT 8888

#define STEP_X_IN1 1
#define STEP_X_IN2 2
#define STEP_X_IN3 38
#define STEP_X_IN4 39

#define STEP_Y_IN1 40
#define STEP_Y_IN2 41
#define STEP_Y_IN3 42
#define STEP_Y_IN4 9

#define LASER_PWM_PIN 10
#define LASER_LEDC_CH 0
#define LASER_FREQ_HZ 5000
#define LASER_RES_BITS 12
#define LASER_MAX_DUTY 4095

#define ENDSTOP_X_PIN 11
#define ENDSTOP_Y_PIN 12
#define STATUS_LED_PIN 13

#define STEPS_PER_REV 4096
#define MAX_SPEED 500
#define HOMING_SPEED 200
#define HOMING_TIMEOUT_MS 10000
