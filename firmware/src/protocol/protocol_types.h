#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// =============================================================================
//  Protocol v1 Types, Enums & Structs
// =============================================================================

enum class CommandType : uint8_t {
  Unknown = 0,
  SystemPing,
  SystemSnapshot,
  SystemStop,
  SystemEStop,
  SystemResetFault,
  DriverSetEnabled,
  HomeStart,
  MotionJog,
  MotionMoveAbs,
  LaserPulse,
  LaserSetDuty,
  JobBegin,
  JobSegment,
  JobEnd
};

enum class ErrorCode : uint8_t {
  Ok = 0,
  ParseError,
  CommandTooLong,
  UnsupportedVersion,
  UnknownCommand,
  InvalidArgument,
  OutOfBounds,
  NotHomed,
  Busy,
  SafetyInterlockTripped,
  LaserDutyClamped,
  SensorStuckActive,
  HomeSeekLimit,
  HomeTimeout,
  MotionAborted,
  WatchdogTimeout
};

const char* errorCodeToString(ErrorCode code);
ErrorCode stringToErrorCode(const char* str);

enum class AxisTarget : uint8_t {
  None = 0,
  X,
  Y,
  All
};

enum class MachineStateEnum : uint8_t {
  Booting = 0,
  IdleUnhomed,
  IdleHomed,
  Homing,
  Moving,
  RunningJob,
  Paused,
  Stopping,
  Faulted
};

const char* machineStateToString(MachineStateEnum state);

// Parsed Incoming Request
struct CommandRequest {
  uint8_t v = 0;
  char id[32] = {0};
  CommandType type = CommandType::Unknown;
  
  // Arguments
  bool boolArg = false;
  AxisTarget axis = AxisTarget::None;
  int32_t steps = 0;
  int32_t targetX = 0;
  int32_t targetY = 0;
  uint32_t speedSps = 0;
  uint16_t duty = 0;
  uint32_t durationMs = 0;
  
  // Job streaming
  char jobId[32] = {0};
  uint32_t totalSegments = 0;
  uint32_t segIdx = 0;
  int32_t dx = 0;
  int32_t dy = 0;
};

// Outgoing Acknowledgment Frame
struct AckResponse {
  uint8_t v = 1;
  char id[32] = {0};
  bool accepted = false;
  ErrorCode code = ErrorCode::Ok;
  char operationId[32] = {0};
};

// Outgoing Operation Event Frame
struct OperationEvent {
  uint8_t v = 1;
  char eventType[24] = {0}; // "operation.started", "operation.progress", "operation.completed"
  char operationId[32] = {0};
  char result[16] = {0};    // "success", "aborted", "failed"
  char phase[24] = {0};
  uint32_t stepsCompleted = 0;
  bool homedX = false;
  bool homedY = false;
  int32_t posX = 0;
  int32_t posY = 0;
};

// Full Telemetry Snapshot
struct SnapshotEvent {
  uint8_t v = 1;
  MachineStateEnum state = MachineStateEnum::IdleUnhomed;
  const char* firmwareVersion = "1.0.0";
  uint8_t protocolVersion = 1;
  const char* profileId = "esp32s3_a4988_v1";
  bool homedX = false;
  bool homedY = false;
  int32_t posX = 0;
  int32_t posY = 0;
  bool posValid = false;
  bool sensorXRaw = true;
  bool sensorXFiltered = false;
  bool sensorYRaw = true;
  bool sensorYFiltered = false;
  uint16_t laserDuty = 0;
  bool laserActive = false;
  bool laserHardClamped = false;
  bool driversEnabled = false;
  const char* fault = nullptr;
};
