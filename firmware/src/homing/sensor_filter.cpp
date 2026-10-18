#include "sensor_filter.h"
#include "../config/pin_mapping.h"

SensorFilter::SensorFilter() {
}

void SensorFilter::filterChannel(SensorChannel& ch, bool rawHigh, uint32_t nowMs) {
  ch.rawHigh = rawHigh;
  // Active LOW: 0V means obstacle / endstop hit
  bool isCandidate = (rawHigh == (SENSOR_ACTIVE_LEVEL != 0));

  if (isCandidate == ch.candidateDetected) {
    if (ch.sameSamples < 255) {
      ch.sameSamples++;
    }
  } else {
    ch.candidateDetected = isCandidate;
    ch.sameSamples = 1;
    ch.candidateSinceMs = nowMs;
  }

  // Stable detection requires consistent sample count and duration window
  if (ch.sameSamples >= HOME_STABLE_SAMPLES && 
      (nowMs - ch.candidateSinceMs >= HOME_DEBOUNCE_MS)) {
    if (ch.stableDetected != ch.candidateDetected) {
      ch.stableDetected = ch.candidateDetected;
      ch.lastTransitionMs = nowMs;
      if (ch.stableDetected) {
        ch.triggers++;
      } else {
        ch.releases++;
      }
    }
  }
}

void SensorFilter::update(bool rawXHigh, bool rawYHigh, uint32_t nowMs) {
  filterChannel(m_channelX, rawXHigh, nowMs);
  filterChannel(m_channelY, rawYHigh, nowMs);
}
