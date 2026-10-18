#pragma once

#include <stdint.h>
#include <stddef.h>

// =============================================================================
//  Machine Limits, Timing & Safety Boundaries
// =============================================================================

// Mechanical Travel Limits (in full microsteps)
constexpr int32_t MAX_TRAVEL_STEPS_X    = 4096;
constexpr int32_t MAX_TRAVEL_STEPS_Y    = 4096;

// Stepping Speed Limits (Steps Per Second)
constexpr uint32_t MIN_SPEED_SPS        = 1;
constexpr uint32_t DEFAULT_SPEED_SPS    = 100;
constexpr uint32_t MAX_SPEED_SPS        = 25000;

// Pulse and Direction Setup Timing (Microseconds)
constexpr uint32_t STEP_PULSE_WIDTH_US  = 3; // Minimum A4988 step high pulse width
constexpr uint32_t DIR_SETUP_TIME_US    = 5; // Minimum A4988 direction setup margin

// Driver Idle Timeout
constexpr uint32_t AUTO_DISABLE_MS      = 30000; // 30s idle auto-disable timeout

// Laser Safety Limits
constexpr uint8_t  LEDC_CHANNEL         = 0;
constexpr uint8_t  LEDC_RES_BITS        = 12;
constexpr uint16_t LEDC_MAX_DUTY        = 4095;
constexpr uint32_t DEFAULT_PWM_FREQ_HZ  = 5000;
constexpr uint8_t  SAFETY_MAX_PCT       = 90;   // Hard ceiling (90%)
constexpr uint16_t SAFETY_MAX_DUTY      = (uint16_t)((LEDC_MAX_DUTY * SAFETY_MAX_PCT) / 100); // 3685
constexpr uint32_t LASER_WATCHDOG_MS    = 500;  // Auto-off watchdog timeout in ms
constexpr uint32_t MAX_PULSE_DURATION_MS = 500; // Max duration for laser.pulse command

// Homing Parameters
constexpr uint32_t HOMING_FAST_SPS          = 120;
constexpr uint32_t HOMING_SLOW_SPS          = 40;
constexpr uint32_t HOMING_MAX_SEEK_STEPS    = 4096;
constexpr uint32_t HOMING_MAX_RETRACT_STEPS = 1024;
constexpr uint32_t HOMING_MAX_LATCH_STEPS   = 1024;
constexpr uint32_t HOMING_MAX_CLEAR_STEPS   = 1024;
constexpr uint32_t HOMING_PHASE_TIMEOUT_MS  = 15000;
constexpr uint8_t  HOME_STABLE_SAMPLES      = 3;
constexpr uint32_t HOME_DEBOUNCE_MS         = 3;

// Protocol Transport Constants
constexpr uint16_t TCP_PORT             = 8888;
constexpr uint32_t SERIAL_BAUD          = 115200;
constexpr size_t   MAX_FRAME_SIZE       = 512;
