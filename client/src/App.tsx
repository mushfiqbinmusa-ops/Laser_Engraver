import React, { useState } from 'react';
import { Header } from './components/layout/Header';
import { TabsNav, TabId } from './components/layout/TabsNav';
import { EngraveTab } from './tabs/EngraveTab';
import { CalibrationTab } from './tabs/CalibrationTab';
import { SettingsTab } from './tabs/SettingsTab';
import { TestsTab } from './tabs/TestsTab';
import { useMachine } from './context/MachineContext';
import { SerialConsoleDrawer } from './components/console/SerialConsoleDrawer';

export const App: React.FC = () => {
  const [activeTab, setActiveTab] = useState<TabId>('engrave');
  const { snapshot, resetFault } = useMachine();

  const isFaulted = snapshot?.state === 'faulted' || (snapshot?.fault && snapshot.fault !== 'none');

  return (
    <div style={{ minHeight: '100vh', display: 'flex', flexDirection: 'column', background: 'var(--bg-app)', paddingBottom: '34px' }}>
      {/* Global E-Stop / Fault Banner */}
      {isFaulted && (
        <div
          role="alert"
          style={{
            background: '#FEF2F2',
            borderBottom: '1px solid #FCA5A5',
            color: '#991B1B',
            padding: '10px 24px',
            display: 'flex',
            justifyContent: 'space-between',
            alignItems: 'center',
            fontSize: '12px',
            fontWeight: 600
          }}
        >
          <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
            <span style={{ fontSize: '10px', color: '#DC2626' }}>●</span>
            <span>
              INTERLOCK / HARDWARE FAULT ACTIVE: {snapshot?.fault || 'Hardware Interlock Tripped'}
            </span>
          </div>
          <button
            onClick={() => resetFault()}
            style={{
              background: '#991B1B',
              color: '#FFFFFF',
              border: '1px solid #7F1D1D',
              fontWeight: 600,
              padding: '4px 10px',
              fontSize: '11px',
              borderRadius: '4px'
            }}
          >
            Reset Fault
          </button>
        </div>
      )}

      {/* Main Header */}
      <Header />

      {/* Primary Navigation */}
      <TabsNav activeTab={activeTab} onTabChange={setActiveTab} />

      {/* Tab Panels */}
      <main style={{ flex: 1, padding: '20px 24px', overflowY: 'auto' }}>
        <div role="tabpanel" id={`panel-${activeTab}`} aria-labelledby={`tab-${activeTab}`}>
          {activeTab === 'engrave' && <EngraveTab />}
          {activeTab === 'calibration' && <CalibrationTab />}
          {activeTab === 'settings' && <SettingsTab />}
          {activeTab === 'tests' && <TestsTab />}
        </div>
      </main>

      <SerialConsoleDrawer />
    </div>
  );
};
