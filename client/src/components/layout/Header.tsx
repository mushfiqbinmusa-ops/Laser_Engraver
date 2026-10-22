import React from 'react';
import { useMachine } from '../../context/MachineContext';

export const Header: React.FC = () => {
  const { connected, fresh, snapshot, lastSeen, estop, stop } = useMachine();

  const getStatusDotColor = () => {
    if (!connected) return '#9CA3AF';
    if (!fresh) return '#D97706';
    if (snapshot?.fault) return '#DC2626';
    if (snapshot?.state === 'idle_homed') return '#15803D';
    if (snapshot?.state === 'idle_unhomed') return '#D97706';
    return '#111827';
  };

  const getStatusText = () => {
    if (!connected) return 'OFFLINE';
    if (!fresh) return 'TELEMETRY STALE';
    if (snapshot?.fault) return `FAULT: ${snapshot.fault}`;
    if (snapshot?.state === 'idle_homed') return 'IDLE · HOMED';
    if (snapshot?.state === 'idle_unhomed') return 'IDLE · UNHOMED';
    return snapshot?.state.toUpperCase() || 'UNKNOWN';
  };

  return (
    <header style={{
      background: 'var(--bg-surface)',
      borderBottom: '1px solid var(--border-subtle)',
      padding: '10px 24px',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'space-between',
      gap: '16px'
    }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: '20px' }}>
        <div>
          <div style={{ fontSize: '14px', fontWeight: 700, letterSpacing: '0.03em', color: 'var(--text-primary)' }}>
            LASER XY SCANNER
          </div>
          <div style={{ fontSize: '11px', color: 'var(--text-secondary)' }}>
            System Controller · <span className="mono">{snapshot?.profile_id || 'esp32s3_a4988_v1'}</span>
          </div>
        </div>

        <div style={{
          display: 'flex',
          alignItems: 'center',
          gap: '8px',
          padding: '4px 10px',
          background: 'var(--bg-surface-elevated)',
          borderRadius: '4px',
          border: '1px solid var(--border-subtle)',
          fontSize: '11px'
        }}>
          <span style={{
            width: '6px',
            height: '6px',
            borderRadius: '50%',
            backgroundColor: getStatusDotColor(),
            display: 'inline-block'
          }} />
          <span className="mono" style={{ fontWeight: 600, color: 'var(--text-primary)' }}>
            {getStatusText()}
          </span>
          {lastSeen && (
            <span style={{ fontSize: '11px', color: 'var(--text-muted)', marginLeft: '2px' }}>
              · {lastSeen.toLocaleTimeString()}
            </span>
          )}
        </div>
      </div>

      <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
        <button
          onClick={stop}
          style={{
            background: 'var(--bg-surface)',
            border: '1px solid var(--border-strong)',
            color: 'var(--text-primary)',
            fontWeight: 600,
            padding: '6px 14px',
            fontSize: '11px',
            letterSpacing: '0.02em'
          }}
          title="Stop motion safely and cut laser output"
        >
          STOP
        </button>

        <button
          onClick={estop}
          style={{
            background: 'var(--accent-estop)',
            color: '#FFFFFF',
            border: '1px solid var(--accent-estop-hover)',
            fontWeight: 700,
            letterSpacing: '0.04em',
            padding: '6px 14px',
            fontSize: '11px'
          }}
          title="Immediate Emergency Halt"
        >
          E-STOP
        </button>
      </div>
    </header>
  );
};
