#pragma once

#include <stddef.h>
#include <stdint.h>
#include "../config/machine_limits.h"

class LineReader {
public:
  LineReader();
  void reset();
  
  // Feed a single byte. Returns true if a full line is ready to read.
  bool feedByte(char c);

  const char* getLine() const { return m_buffer; }
  size_t getLength() const { return m_length; }
  bool hasOverflow() const { return m_overflow; }
  void clearLine();

private:
  char m_buffer[MAX_FRAME_SIZE + 1];
  size_t m_length;
  bool m_overflow;
};
