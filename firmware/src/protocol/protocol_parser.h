#pragma once

#include "protocol_types.h"
#include "../config/machine_limits.h"

class ProtocolParser {
public:
  static ErrorCode parseLine(const char* line, size_t len, CommandRequest& outReq);

  static size_t serializeAck(const AckResponse& ack, char* outBuf, size_t maxLen);
  static size_t serializeOperationEvent(const OperationEvent& ev, char* outBuf, size_t maxLen);
  static size_t serializeSnapshot(const SnapshotEvent& snap, char* outBuf, size_t maxLen);

private:
  static bool extractString(const char* json, const char* key, char* outVal, size_t maxLen);
  static bool extractInt(const char* json, const char* key, int32_t& outVal);
  static bool extractUInt(const char* json, const char* key, uint32_t& outVal);
  static bool extractBool(const char* json, const char* key, bool& outVal);
};
