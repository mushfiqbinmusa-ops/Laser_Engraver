import React, { useState } from 'react';
import { Header } from './components/layout/Header';
import { TabsNav, TabId } from './components/layout/TabsNav';
import { EngraveTab } from './tabs/EngraveTab';
import { CalibrationTab } from './tabs/CalibrationTab';
import { SettingsTab } from './tabs/SettingsTab';
import { TestsTab } from './tabs/TestsTab';
import { useMachine } from './context/MachineContext';

export const App: React.FC = () => {
  const [activeTab, setActiveTab] = useState<TabId>('engrave');
  const { snapshot, resetFault } = useMachine();

  const isFaulted = snapshot?.state === 'Faulted' || (snapshot?.fault && snapshot.fault !== 'none');

  return (
    <div style={{ minHeight: '100vh', display: 'flex', flexDirection: 'column', background: 'var(--bg-app)' }}>
      {/* Global E-Stop / Fault Banner */}
      {isFaulted && (
        <div
          role="alert"
          style={{
            background: 'var(--accent-estop)',
            color: '#FFFFFF',
            padding: '12px 24px',
            display: 'flex',
            justifyContent: 'space-between',
            alignItems: 'center',
            fontWeight: 600
          }}
        >
          <div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
            <span style={{ fontSize: '18px' }}>⚠️</span>
            <span>
              EMERGENCY STOP / SYSTEM FAULT ACTIVE: {snapshot?.fault || 'Hardware Interlock Tripped'}
            </span>
          </div>
          <button
            onClick={() => resetFault()}
            style={{
              background: '#FFFFFF',
              color: 'var(--accent-estop)',
              border: 'none',
              fontWeight: 700,
              padding: '6px 14px'
            }}
          >
            Clear Fault State
          </button>
        </div>
      )}

      {/* Main Header */}
      <Header />

      {/* Primary Navigation */}
      <TabsNav activeTab={activeTab} onTabChange={setActiveTab} />

      {/* Tab Panels */}
      <main style={{ flex: 1, padding: '24px', overflowY: 'auto' }}>
        <div role="tabpanel" id={`panel-${activeTab}`} aria-labelledby={`tab-${activeTab}`}>
          {activeTab === 'engrave' && <EngraveTab />}
          {activeTab === 'calibration' && <CalibrationTab />}
          {activeTab === 'settings' && <SettingsTab />}
          {activeTab === 'tests' && <TestsTab />}
        </div>
      </main>
    </div>
  );
};
