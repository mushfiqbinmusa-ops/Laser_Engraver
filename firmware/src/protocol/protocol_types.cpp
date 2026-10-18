#include "protocol_types.h"

const char* errorCodeToString(ErrorCode code) {
  switch (code) {
    case ErrorCode::Ok: return "OK";
    case ErrorCode::ParseError: return "PARSE_ERROR";
    case ErrorCode::CommandTooLong: return "COMMAND_TOO_LONG";
    case ErrorCode::UnsupportedVersion: return "UNSUPPORTED_VERSION";
    case ErrorCode::UnknownCommand: return "UNKNOWN_COMMAND";
    case ErrorCode::InvalidArgument: return "INVALID_ARGUMENT";
    case ErrorCode::OutOfBounds: return "OUT_OF_BOUNDS";
    case ErrorCode::NotHomed: return "NOT_HOMED";
    case ErrorCode::Busy: return "BUSY";
    case ErrorCode::SafetyInterlockTripped: return "SAFETY_INTERLOCK_TRIPPED";
    case ErrorCode::LaserDutyClamped: return "LASER_DUTY_CLAMPED";
    case ErrorCode::SensorStuckActive: return "SENSOR_STUCK_ACTIVE";
    case ErrorCode::HomeSeekLimit: return "HOME_SEEK_LIMIT";
    case ErrorCode::HomeTimeout: return "HOME_TIMEOUT";
    case ErrorCode::MotionAborted: return "MOTION_ABORTED";
    case ErrorCode::WatchdogTimeout: return "WATCHDOG_TIMEOUT";
    default: return "UNKNOWN_ERROR";
  }
}

ErrorCode stringToErrorCode(const char* str) {
  if (!str) return ErrorCode::Ok;
  if (strcmp(str, "OK") == 0) return ErrorCode::Ok;
  if (strcmp(str, "PARSE_ERROR") == 0) return ErrorCode::ParseError;
  if (strcmp(str, "COMMAND_TOO_LONG") == 0) return ErrorCode::CommandTooLong;
  if (strcmp(str, "UNSUPPORTED_VERSION") == 0) return ErrorCode::UnsupportedVersion;
  if (strcmp(str, "UNKNOWN_COMMAND") == 0) return ErrorCode::UnknownCommand;
  if (strcmp(str, "INVALID_ARGUMENT") == 0) return ErrorCode::InvalidArgument;
  if (strcmp(str, "OUT_OF_BOUNDS") == 0) return ErrorCode::OutOfBounds;
  if (strcmp(str, "NOT_HOMED") == 0) return ErrorCode::NotHomed;
  if (strcmp(str, "BUSY") == 0) return ErrorCode::Busy;
  if (strcmp(str, "SAFETY_INTERLOCK_TRIPPED") == 0) return ErrorCode::SafetyInterlockTripped;
  if (strcmp(str, "LASER_DUTY_CLAMPED") == 0) return ErrorCode::LaserDutyClamped;
  if (strcmp(str, "SENSOR_STUCK_ACTIVE") == 0) return ErrorCode::SensorStuckActive;
  if (strcmp(str, "HOME_SEEK_LIMIT") == 0) return ErrorCode::HomeSeekLimit;
  if (strcmp(str, "HOME_TIMEOUT") == 0) return ErrorCode::HomeTimeout;
  if (strcmp(str, "MOTION_ABORTED") == 0) return ErrorCode::MotionAborted;
  if (strcmp(str, "WATCHDOG_TIMEOUT") == 0) return ErrorCode::WatchdogTimeout;
  return ErrorCode::ParseError;
}

const char* machineStateToString(MachineStateEnum state) {
  switch (state) {
    case MachineStateEnum::Booting: return "booting";
    case MachineStateEnum::IdleUnhomed: return "idle_unhomed";
    case MachineStateEnum::IdleHomed: return "idle_homed";
    case MachineStateEnum::Homing: return "homing";
    case MachineStateEnum::Moving: return "moving";
    case MachineStateEnum::RunningJob: return "running_job";
    case MachineStateEnum::Paused: return "paused";
    case MachineStateEnum::Stopping: return "stopping";
    case MachineStateEnum::Faulted: return "faulted";
    default: return "unknown";
  }
}
