#include "machine_state.h"
#include <string.h>

MachineStateManager::MachineStateManager()
  : m_state(MachineStateEnum::IdleUnhomed),
    m_homedX(false),
    m_homedY(false) {
  m_fault[0] = '\0';
}

void MachineStateManager::setState(MachineStateEnum state) {
  m_state = state;
}

void MachineStateManager::setFault(const char* reason) {
  m_state = MachineStateEnum::Faulted;
  if (reason) {
    strncpy(m_fault, reason, sizeof(m_fault) - 1);
    m_fault[sizeof(m_fault) - 1] = '\0';
  } else {
    strcpy(m_fault, "UNSPECIFIED_FAULT");
  }
  // Fault always invalidates homing
  m_homedX = false;
  m_homedY = false;
}

void MachineStateManager::clearFault() {
  m_fault[0] = '\0';
  m_state = MachineStateEnum::IdleUnhomed;
}

void MachineStateManager::setHomed(bool homedX, bool homedY) {
  m_homedX = homedX;
  m_homedY = homedY;
  if (m_state != MachineStateEnum::Faulted && m_state != MachineStateEnum::Moving) {
    m_state = (m_homedX && m_homedY) ? MachineStateEnum::IdleHomed : MachineStateEnum::IdleUnhomed;
  }
}

bool MachineStateManager::canJog() const {
  return (m_state == MachineStateEnum::IdleUnhomed || m_state == MachineStateEnum::IdleHomed);
}

bool MachineStateManager::canMoveAbs() const {
  return (m_state == MachineStateEnum::IdleHomed);
}

bool MachineStateManager::canStartJob() const {
  return (m_state == MachineStateEnum::IdleHomed);
}

bool MachineStateManager::canHome() const {
  return (m_state == MachineStateEnum::IdleUnhomed || m_state == MachineStateEnum::IdleHomed);
}
