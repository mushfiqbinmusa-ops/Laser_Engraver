#pragma once

#include <stdint.h>
#include "../config/machine_limits.h"

struct SensorChannel {
  bool rawHigh = true;
  bool stableDetected = false;
  bool candidateDetected = false;
  uint8_t sameSamples = 0;
  uint32_t candidateSinceMs = 0;
  uint32_t triggers = 0;
  uint32_t releases = 0;
  uint32_t lastTransitionMs = 0;
};

class SensorFilter {
public:
  SensorFilter();
  
  // Updates state using raw electrical pin levels (true = HIGH/3.3V, false = LOW/0V)
  void update(bool rawXHigh, bool rawYHigh, uint32_t nowMs);

  const SensorChannel& getChannelX() const { return m_channelX; }
  const SensorChannel& getChannelY() const { return m_channelY; }

  bool isDetectedX() const { return m_channelX.stableDetected; }
  bool isDetectedY() const { return m_channelY.stableDetected; }

private:
  void filterChannel(SensorChannel& ch, bool rawHigh, uint32_t nowMs);

  SensorChannel m_channelX;
  SensorChannel m_channelY;
};
