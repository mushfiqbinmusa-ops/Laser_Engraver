#pragma once

#include "../protocol/protocol_types.h"

class MachineStateManager {
public:
  MachineStateManager();

  MachineStateEnum getState() const { return m_state; }
  void setState(MachineStateEnum state);

  void setFault(const char* reason);
  void clearFault();
  const char* getFault() const { return m_fault[0] ? m_fault : nullptr; }

  bool isHomedX() const { return m_homedX; }
  bool isHomedY() const { return m_homedY; }
  void setHomed(bool homedX, bool homedY);

  bool isPositionValid() const { return m_homedX && m_homedY && (m_state != MachineStateEnum::Faulted); }

  bool canJog() const;
  bool canMoveAbs() const;
  bool canStartJob() const;
  bool canHome() const;

private:
  MachineStateEnum m_state;
  char m_fault[48];
  bool m_homedX;
  bool m_homedY;
};
