#pragma once

#include <stdint.h>
#include <stddef.h>

// =============================================================================
//  Machine Limits, Timing & Safety Boundaries
// =============================================================================

// Mechanical Travel Limits (in full microsteps)
constexpr int32_t MAX_TRAVEL_STEPS_X    = 65536;
constexpr int32_t MAX_TRAVEL_STEPS_Y    = 65536;

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

// Homing Parameters (Sized for 28BYJ-48 64:1 with up to 32768 microsteps per 360° rotation)
constexpr uint32_t HOMING_FAST_SPS          = 800;   // Safe 50 full-step/s equivalent in 1/16 mode
constexpr uint32_t HOMING_SLOW_SPS          = 200;   // Slow precision latch rate
constexpr uint32_t HOMING_MAX_SEEK_STEPS    = 45000; // Allows >360° search without triggering false seek limit
constexpr uint32_t HOMING_MAX_RETRACT_STEPS = 4000;  // Max pull-off steps
constexpr uint32_t HOMING_MAX_LATCH_STEPS   = 4000;  // Max slow latch steps
constexpr uint32_t HOMING_MAX_CLEAR_STEPS   = 4000;  // Max initial retreat steps
constexpr uint32_t HOMING_FINAL_PULLOFF_STEPS = 200; // Steps to move off sensor after latch so beam is open
constexpr uint32_t HOMING_PHASE_TIMEOUT_MS  = 60000; // 60s per-phase timeout
constexpr uint8_t  HOME_STABLE_SAMPLES      = 3;
constexpr uint32_t HOME_DEBOUNCE_MS         = 3;

// Protocol Transport Constants
constexpr uint16_t TCP_PORT             = 8888;
constexpr uint32_t SERIAL_BAUD          = 115200;
constexpr size_t   MAX_FRAME_SIZE       = 512;
