#include "line_reader.h"
#include <string.h>

LineReader::LineReader() : m_length(0), m_overflow(false) {
  m_buffer[0] = '\0';
}

void LineReader::reset() {
  m_length = 0;
  m_overflow = false;
  m_buffer[0] = '\0';
}

bool LineReader::feedByte(char c) {
  if (c == '\r') {
    return false; // Ignore carriage return
  }

  if (c == '\n') {
    if (m_overflow) {
      // Overlong frame boundary reached.
      m_buffer[0] = '\0';
      m_length = 0;
      m_overflow = false;
      return true; // Signal caller that an overlong command ended
    }
    m_buffer[m_length] = '\0';
    return true; // Complete line ready
  }

  if (m_overflow) {
    return false; // Discard until '\n'
  }

  if (m_length < MAX_FRAME_SIZE) {
    m_buffer[m_length++] = c;
    m_buffer[m_length] = '\0';
    return false;
  } else {
    // Exceeded MAX_FRAME_SIZE without receiving '\n'
    m_overflow = true;
    m_buffer[0] = '\0';
    m_length = 0;
    return false;
  }
}

void LineReader::clearLine() {
  m_length = 0;
  m_buffer[0] = '\0';
}
