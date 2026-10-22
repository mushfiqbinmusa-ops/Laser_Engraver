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

// Controller Thermal Management
constexpr uint32_t CPU_FREQ_MHZ         = 160;
constexpr uint32_t IDLE_LOOP_DELAY_MS   = 1;

// Laser Safety Limits
constexpr uint8_t  LEDC_CHANNEL         = 0;
constexpr uint8_t  LEDC_RES_BITS        = 12;
constexpr uint16_t LEDC_MAX_DUTY        = 4095;
constexpr uint32_t DEFAULT_PWM_FREQ_HZ  = 5000;
constexpr uint8_t  SAFETY_MAX_PCT       = 90;   // Hard ceiling (90%)
constexpr uint16_t SAFETY_MAX_DUTY      = (uint16_t)((LEDC_MAX_DUTY * SAFETY_MAX_PCT) / 100); // 3685
constexpr uint32_t LASER_WATCHDOG_MS    = 500;  // Auto-off watchdog timeout in ms
constexpr uint32_t MAX_PULSE_DURATION_MS = 500; // Max duration for laser.pulse command

// Homing Parameters (28BYJ-48 64:1: 2048 full output steps per revolution)
constexpr uint32_t HOMING_FAST_SPS            = 800;   // Reference rate at 1/16 mode (50 full-step/s equivalent)
constexpr uint32_t HOMING_SLOW_SPS            = 200;   // Reference rate at 1/16 mode (12.5 full-step/s equivalent)
constexpr bool     HOMING_SEEK_FORWARD_X      = true;  // +X moves toward the X sensor
constexpr bool     HOMING_SEEK_FORWARD_Y      = true;  // +Y moves toward the Y sensor
constexpr uint32_t HOMING_STEPS_PER_REV_FULL  = 2048;  // Full output steps per motor revolution
constexpr uint32_t HOMING_SWEEP_DEG           = 30;    // Search envelope either side of the start point
constexpr uint32_t HOMING_SWEEP_STEPS_REF     =
  (HOMING_STEPS_PER_REV_FULL * 16UL * HOMING_SWEEP_DEG + 180UL) / 360UL;
constexpr uint32_t HOMING_SEARCH_LEGS         = 12;    // 3 complete 4 x 30° oscillation cycles
constexpr uint32_t HOMING_MAX_SEEK_STEPS      = HOMING_SWEEP_STEPS_REF * HOMING_SEARCH_LEGS;
constexpr uint32_t HOMING_MAX_RETRACT_STEPS   = HOMING_SWEEP_STEPS_REF;
constexpr uint32_t HOMING_MAX_LATCH_STEPS     = HOMING_SWEEP_STEPS_REF;
constexpr uint32_t HOMING_MAX_CLEAR_STEPS     = HOMING_SWEEP_STEPS_REF;
constexpr uint32_t HOMING_FINAL_PULLOFF_STEPS = 200;   // Steps to move off sensor after latch so beam is open
constexpr uint32_t HOMING_PHASE_TIMEOUT_MS    = 60000; // 60s per-phase timeout
constexpr uint8_t  HOME_STABLE_SAMPLES        = 3;
constexpr uint32_t HOME_DEBOUNCE_MS           = 3;

// Protocol Transport Constants
constexpr uint16_t TCP_PORT             = 8888;
constexpr uint32_t SERIAL_BAUD          = 115200;
constexpr size_t   MAX_FRAME_SIZE       = 512;
