import React from 'react';

export type TabId = 'engrave' | 'calibration' | 'settings' | 'tests';

interface TabsNavProps {
  activeTab: TabId;
  onTabChange: (tab: TabId) => void;
}

export const TabsNav: React.FC<TabsNavProps> = ({ activeTab, onTabChange }) => {
  const tabs: { id: TabId; label: string }[] = [
    { id: 'engrave', label: '1. Engrave' },
    { id: 'calibration', label: '2. Calibration & Homing' },
    { id: 'settings', label: '3. Settings & Presets' },
    { id: 'tests', label: '4. Tests & Diagnostics' }
  ];

  const handleKeyDown = (e: React.KeyboardEvent, index: number) => {
    if (e.key === 'ArrowRight') {
      const nextIndex = (index + 1) % tabs.length;
      onTabChange(tabs[nextIndex].id);
    } else if (e.key === 'ArrowLeft') {
      const prevIndex = (index - 1 + tabs.length) % tabs.length;
      onTabChange(tabs[prevIndex].id);
    }
  };

  return (
    <nav
      role="tablist"
      aria-label="Scanner Controls Navigation"
      style={{
        display: 'flex',
        background: 'var(--bg-surface)',
        borderBottom: '1px solid var(--border-subtle)',
        padding: '0 24px',
        gap: '4px'
      }}
    >
      {tabs.map((tab, idx) => {
        const isActive = activeTab === tab.id;
        return (
          <button
            key={tab.id}
            id={`tab-${tab.id}`}
            role="tab"
            aria-selected={isActive}
            aria-controls={`panel-${tab.id}`}
            tabIndex={isActive ? 0 : -1}
            onClick={() => onTabChange(tab.id)}
            onKeyDown={(e) => handleKeyDown(e, idx)}
            style={{
              background: 'transparent',
              border: 'none',
              borderBottom: isActive ? '2px solid var(--text-primary)' : '2px solid transparent',
              borderRadius: 0,
              color: isActive ? 'var(--text-primary)' : 'var(--text-secondary)',
              fontWeight: isActive ? 600 : 400,
              padding: '12px 16px',
              fontSize: '13px'
            }}
          >
            {tab.label}
          </button>
        );
      })}
    </nav>
  );
};
