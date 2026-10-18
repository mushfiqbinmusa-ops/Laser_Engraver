#include "protocol_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

static const char* findKey(const char* json, const char* key) {
  if (!json || !key) return nullptr;
  char pattern[48];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char* p = strstr(json, pattern);
  if (!p) return nullptr;
  p += strlen(pattern);
  while (*p && isspace((unsigned char)*p)) p++;
  if (*p != ':') return nullptr;
  p++;
  while (*p && isspace((unsigned char)*p)) p++;
  return p;
}

bool ProtocolParser::extractString(const char* json, const char* key, char* outVal, size_t maxLen) {
  const char* p = findKey(json, key);
  if (!p || *p != '"') return false;
  p++; // skip opening quote
  size_t idx = 0;
  while (*p && *p != '"' && idx + 1 < maxLen) {
    if (*p == '\\' && *(p + 1)) p++; // simple escape skip
    outVal[idx++] = *p++;
  }
  outVal[idx] = '\0';
  return (*p == '"');
}

bool ProtocolParser::extractInt(const char* json, const char* key, int32_t& outVal) {
  const char* p = findKey(json, key);
  if (!p) return false;
  char* endp = nullptr;
  long v = strtol(p, &endp, 10);
  if (endp == p) return false;
  outVal = (int32_t)v;
  return true;
}

bool ProtocolParser::extractUInt(const char* json, const char* key, uint32_t& outVal) {
  const char* p = findKey(json, key);
  if (!p) return false;
  if (*p == '-') return false; // negative not allowed for uint
  char* endp = nullptr;
  unsigned long v = strtoul(p, &endp, 10);
  if (endp == p) return false;
  outVal = (uint32_t)v;
  return true;
}

bool ProtocolParser::extractBool(const char* json, const char* key, bool& outVal) {
  const char* p = findKey(json, key);
  if (!p) return false;
  if (strncmp(p, "true", 4) == 0) {
    outVal = true;
    return true;
  }
  if (strncmp(p, "false", 5) == 0) {
    outVal = false;
    return true;
  }
  return false;
}

static bool isValidJsonSyntax(const char* s, const char* end) {
  const char* p = s;
  if (*p != '{') return false;
  p++;
  bool expectKey = true;
  while (p < end) {
    while (p < end && isspace((unsigned char)*p)) p++;
    if (p >= end) break;
    if (*p == '}') return !expectKey || (p == s + 1); // empty object {}
    if (expectKey) {
      if (*p != '"') return false; // Key must start with quote
      p++;
      while (p < end && *p != '"') {
        if (*p == '\\' && p + 1 < end) p++;
        p++;
      }
      if (p >= end || *p != '"') return false;
      p++; // past quote
      while (p < end && isspace((unsigned char)*p)) p++;
      if (p >= end || *p != ':') return false; // Must have colon
      p++; // past colon
      while (p < end && isspace((unsigned char)*p)) p++;
      if (p >= end) return false;
      // Value
      if (*p == '"') {
        p++;
        while (p < end && *p != '"') {
          if (*p == '\\' && p + 1 < end) p++;
          p++;
        }
        if (p >= end || *p != '"') return false;
        p++;
      } else if (*p == '-' || isdigit((unsigned char)*p)) {
        p++;
        while (p < end && (isdigit((unsigned char)*p) || *p == '.')) p++;
      } else if (strncmp(p, "true", 4) == 0) {
        p += 4;
      } else if (strncmp(p, "false", 5) == 0) {
        p += 5;
      } else if (strncmp(p, "null", 4) == 0) {
        p += 4;
      } else {
        return false; // Invalid token!
      }
      expectKey = false;
    } else {
      if (*p == ',') {
        expectKey = true;
        p++;
      } else if (*p == '}') {
        return true;
      } else {
        return false;
      }
    }
  }
  return true;
}

ErrorCode ProtocolParser::parseLine(const char* line, size_t len, CommandRequest& req) {
  if (len > MAX_FRAME_SIZE) {
    return ErrorCode::CommandTooLong;
  }
  if (!line || len == 0) {
    return ErrorCode::ParseError;
  }
  
  // Basic JSON structure check
  const char* start = line;
  while (*start && isspace((unsigned char)*start)) start++;
  if (*start != '{') {
    return ErrorCode::ParseError;
  }
  const char* end = line + len - 1;
  while (end > start && isspace((unsigned char)*end)) end--;
  if (*end != '}') {
    return ErrorCode::ParseError;
  }

  // Strict syntax verification
  if (!isValidJsonSyntax(start, end + 1)) {
    return ErrorCode::ParseError;
  }

  // Version check
  int32_t version = 0;
  if (!extractInt(line, "v", version)) {
    return ErrorCode::ParseError;
  }
  if (version != 1) {
    return ErrorCode::UnsupportedVersion;
  }
  req.v = (uint8_t)version;

  // Request ID
  if (!extractString(line, "id", req.id, sizeof(req.id)) || strlen(req.id) == 0) {
    return ErrorCode::InvalidArgument;
  }

  // Command Type
  char typeStr[32] = {0};
  if (!extractString(line, "type", typeStr, sizeof(typeStr)) || strlen(typeStr) == 0) {
    return ErrorCode::InvalidArgument;
  }

  if (strcmp(typeStr, "system.ping") == 0) {
    req.type = CommandType::SystemPing;
  } else if (strcmp(typeStr, "system.snapshot") == 0) {
    req.type = CommandType::SystemSnapshot;
  } else if (strcmp(typeStr, "system.stop") == 0) {
    req.type = CommandType::SystemStop;
  } else if (strcmp(typeStr, "system.estop") == 0) {
    req.type = CommandType::SystemEStop;
  } else if (strcmp(typeStr, "system.reset_fault") == 0) {
    req.type = CommandType::SystemResetFault;
  } else if (strcmp(typeStr, "driver.set_enabled") == 0) {
    req.type = CommandType::DriverSetEnabled;
    if (!extractBool(line, "enabled", req.boolArg)) {
      return ErrorCode::InvalidArgument;
    }
  } else if (strcmp(typeStr, "driver.set_microstep") == 0) {
    req.type = CommandType::DriverSetMicrostep;
    uint32_t mode = 0;
    if (!extractUInt(line, "mode", mode)) {
      return ErrorCode::InvalidArgument;
    }
    if (mode != 1 && mode != 2 && mode != 4 && mode != 8 && mode != 16) {
      return ErrorCode::InvalidArgument;
    }
    req.microstepMode = (uint8_t)mode;
  } else if (strcmp(typeStr, "home.start") == 0) {
    req.type = CommandType::HomeStart;
    char axisStr[16] = {0};
    if (!extractString(line, "axis", axisStr, sizeof(axisStr))) {
      return ErrorCode::InvalidArgument;
    }
    if (strcmp(axisStr, "all") == 0) req.axis = AxisTarget::All;
    else if (strcmp(axisStr, "x") == 0) req.axis = AxisTarget::X;
    else if (strcmp(axisStr, "y") == 0) req.axis = AxisTarget::Y;
    else return ErrorCode::InvalidArgument;
  } else if (strcmp(typeStr, "motion.jog") == 0) {
    req.type = CommandType::MotionJog;
    char axisStr[16] = {0};
    if (!extractString(line, "axis", axisStr, sizeof(axisStr))) return ErrorCode::InvalidArgument;
    if (strcmp(axisStr, "x") == 0) req.axis = AxisTarget::X;
    else if (strcmp(axisStr, "y") == 0) req.axis = AxisTarget::Y;
    else return ErrorCode::InvalidArgument;

    if (!extractInt(line, "steps", req.steps)) return ErrorCode::InvalidArgument;
    
    uint32_t sps = 0;
    if (extractUInt(line, "speed_sps", sps)) {
      if (sps == 0) return ErrorCode::InvalidArgument;
      if (sps > MAX_SPEED_SPS) return ErrorCode::OutOfBounds;
      req.speedSps = sps;
    } else {
      req.speedSps = DEFAULT_SPEED_SPS;
    }
  } else if (strcmp(typeStr, "motion.move_abs") == 0) {
    req.type = CommandType::MotionMoveAbs;
    if (!extractInt(line, "x", req.targetX) || !extractInt(line, "y", req.targetY)) {
      return ErrorCode::InvalidArgument;
    }
    if (req.targetX < 0 || req.targetX > MAX_TRAVEL_STEPS_X ||
        req.targetY < 0 || req.targetY > MAX_TRAVEL_STEPS_Y) {
      return ErrorCode::OutOfBounds;
    }
    uint32_t sps = 0;
    if (extractUInt(line, "speed_sps", sps)) {
      if (sps == 0) return ErrorCode::InvalidArgument;
      if (sps > MAX_SPEED_SPS) return ErrorCode::OutOfBounds;
      req.speedSps = sps;
    } else {
      req.speedSps = DEFAULT_SPEED_SPS;
    }
  } else if (strcmp(typeStr, "laser.pulse") == 0) {
    req.type = CommandType::LaserPulse;
    uint32_t duty = 0;
    if (!extractUInt(line, "duty", duty)) return ErrorCode::InvalidArgument;
    if (duty > SAFETY_MAX_DUTY) return ErrorCode::LaserDutyClamped;
    req.duty = (uint16_t)duty;

    uint32_t dur = 0;
    if (!extractUInt(line, "duration_ms", dur)) return ErrorCode::InvalidArgument;
    if (dur == 0 || dur > MAX_PULSE_DURATION_MS) return ErrorCode::InvalidArgument;
    req.durationMs = dur;
  } else if (strcmp(typeStr, "laser.set_duty") == 0) {
    req.type = CommandType::LaserSetDuty;
    uint32_t duty = 0;
    if (!extractUInt(line, "duty", duty)) return ErrorCode::InvalidArgument;
    if (duty > SAFETY_MAX_DUTY) return ErrorCode::LaserDutyClamped;
    req.duty = (uint16_t)duty;
  } else if (strcmp(typeStr, "job.begin") == 0) {
    req.type = CommandType::JobBegin;
    if (!extractString(line, "job_id", req.jobId, sizeof(req.jobId)) || strlen(req.jobId) == 0) {
      return ErrorCode::InvalidArgument;
    }
    if (!extractUInt(line, "total_segments", req.totalSegments)) {
      return ErrorCode::InvalidArgument;
    }
  } else if (strcmp(typeStr, "job.segment") == 0) {
    req.type = CommandType::JobSegment;
    if (!extractString(line, "job_id", req.jobId, sizeof(req.jobId))) return ErrorCode::InvalidArgument;
    if (!extractUInt(line, "seg_idx", req.segIdx)) return ErrorCode::InvalidArgument;
    if (!extractInt(line, "dx", req.dx) || !extractInt(line, "dy", req.dy)) return ErrorCode::InvalidArgument;
    
    uint32_t sps = 0;
    if (!extractUInt(line, "speed_sps", sps) || sps == 0 || sps > MAX_SPEED_SPS) {
      return ErrorCode::InvalidArgument;
    }
    req.speedSps = sps;

    uint32_t duty = 0;
    if (!extractUInt(line, "duty", duty)) return ErrorCode::InvalidArgument;
    if (duty > SAFETY_MAX_DUTY) return ErrorCode::LaserDutyClamped;
    req.duty = (uint16_t)duty;
  } else if (strcmp(typeStr, "job.end") == 0) {
    req.type = CommandType::JobEnd;
    if (!extractString(line, "job_id", req.jobId, sizeof(req.jobId))) {
      return ErrorCode::InvalidArgument;
    }
  } else {
    return ErrorCode::UnknownCommand;
  }

  return ErrorCode::Ok;
}

size_t ProtocolParser::serializeAck(const AckResponse& ack, char* outBuf, size_t maxLen) {
  if (!outBuf || maxLen == 0) return 0;
  int written = 0;
  if (strlen(ack.operationId) > 0) {
    written = snprintf(outBuf, maxLen,
      "{\"v\":%u,\"id\":\"%s\",\"type\":\"ack\",\"accepted\":%s,\"code\":\"%s\",\"operation_id\":\"%s\"}\n",
      ack.v, ack.id, ack.accepted ? "true" : "false", errorCodeToString(ack.code), ack.operationId);
  } else {
    written = snprintf(outBuf, maxLen,
      "{\"v\":%u,\"id\":\"%s\",\"type\":\"ack\",\"accepted\":%s,\"code\":\"%s\"}\n",
      ack.v, ack.id, ack.accepted ? "true" : "false", errorCodeToString(ack.code));
  }
  return (written > 0 && (size_t)written < maxLen) ? (size_t)written : 0;
}

size_t ProtocolParser::serializeOperationEvent(const OperationEvent& ev, char* outBuf, size_t maxLen) {
  if (!outBuf || maxLen == 0) return 0;
  int written = 0;
  if (strcmp(ev.eventType, "operation.started") == 0) {
    written = snprintf(outBuf, maxLen,
      "{\"v\":%u,\"type\":\"operation.started\",\"operation_id\":\"%s\",\"kind\":\"%s\"}\n",
      ev.v, ev.operationId, ev.phase);
  } else if (strcmp(ev.eventType, "operation.progress") == 0) {
    written = snprintf(outBuf, maxLen,
      "{\"v\":%u,\"type\":\"operation.progress\",\"operation_id\":\"%s\",\"phase\":\"%s\",\"steps_completed\":%u}\n",
      ev.v, ev.operationId, ev.phase, ev.stepsCompleted);
  } else if (strcmp(ev.eventType, "operation.completed") == 0) {
    written = snprintf(outBuf, maxLen,
      "{\"v\":%u,\"type\":\"operation.completed\",\"operation_id\":\"%s\",\"result\":\"%s\",\"homed\":{\"x\":%s,\"y\":%s},\"pos\":{\"x\":%d,\"y\":%d}}\n",
      ev.v, ev.operationId, ev.result,
      ev.homedX ? "true" : "false", ev.homedY ? "true" : "false",
      ev.posX, ev.posY);
  }
  return (written > 0 && (size_t)written < maxLen) ? (size_t)written : 0;
}

size_t ProtocolParser::serializeSnapshot(const SnapshotEvent& snap, char* outBuf, size_t maxLen) {
  if (!outBuf || maxLen == 0) return 0;
  char faultJson[64] = "null";
  if (snap.fault && strlen(snap.fault) > 0) {
    snprintf(faultJson, sizeof(faultJson), "\"%s\"", snap.fault);
  }

  int written = snprintf(outBuf, maxLen,
    "{\"v\":%u,\"type\":\"system.snapshot\",\"state\":\"%s\",\"firmware_version\":\"%s\",\"protocol_version\":%u,\"profile_id\":\"%s\",\"homed\":{\"x\":%s,\"y\":%s},\"pos\":{\"x\":%d,\"y\":%d},\"pos_valid\":%s,\"sensors\":{\"x_raw\":%u,\"x_filtered\":%u,\"y_raw\":%u,\"y_filtered\":%u},\"laser\":{\"duty\":%u,\"active\":%s,\"hard_clamped\":%s},\"drivers_enabled\":%s,\"microstep_mode\":%u,\"fault\":%s}\n",
    snap.v,
    machineStateToString(snap.state),
    snap.firmwareVersion,
    snap.protocolVersion,
    snap.profileId,
    snap.homedX ? "true" : "false", snap.homedY ? "true" : "false",
    snap.posX, snap.posY,
    snap.posValid ? "true" : "false",
    snap.sensorXRaw ? 1 : 0, snap.sensorXFiltered ? 1 : 0,
    snap.sensorYRaw ? 1 : 0, snap.sensorYFiltered ? 1 : 0,
    snap.laserDuty, snap.laserActive ? "true" : "false", snap.laserHardClamped ? "true" : "false",
    snap.driversEnabled ? "true" : "false",
    snap.microstepMode,
    faultJson);

  return (written > 0 && (size_t)written < maxLen) ? (size_t)written : 0;
}
