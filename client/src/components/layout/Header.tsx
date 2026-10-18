import React from 'react';
import { useMachine } from '../../context/MachineContext';

export const Header: React.FC = () => {
  const { connected, fresh, snapshot, lastSeen, estop, stop } = useMachine();

  const getStatusColor = () => {
    if (!connected) return 'var(--text-muted)';
    if (!fresh) return 'var(--accent-warning)';
    if (snapshot?.fault) return 'var(--accent-estop)';
    if (snapshot?.state === 'idle_homed') return 'var(--accent-ready)';
    if (snapshot?.state === 'idle_unhomed') return '#FFD43B';
    return 'var(--text-primary)';
  };

  const getStatusText = () => {
    if (!connected) return 'CONTROLLER OFFLINE';
    if (!fresh) return 'TELEMETRY STALE';
    if (snapshot?.fault) return `FAULT: ${snapshot.fault}`;
    if (snapshot?.state === 'idle_homed') return 'IDLE (HOMED)';
    if (snapshot?.state === 'idle_unhomed') return 'IDLE (UNHOMED)';
    return snapshot?.state.toUpperCase() || 'UNKNOWN';
  };

  return (
    <header style={{
      background: 'var(--bg-surface)',
      borderBottom: '1px solid var(--border-subtle)',
      padding: '12px 24px',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'space-between',
      gap: '16px'
    }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: '16px' }}>
        <div>
          <h1 style={{ fontSize: '16px', fontWeight: 600, letterSpacing: '0.02em' }}>
            LASER XY SCANNER
          </h1>
          <div style={{ fontSize: '11px', color: 'var(--text-secondary)' }}>
            v1.0 Production Station · Profile: <span className="mono">{snapshot?.profile_id || 'esp32s3_a4988_v1'}</span>
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
          fontSize: '12px'
        }}>
          <span style={{
            width: '8px',
            height: '8px',
            borderRadius: '50%',
            backgroundColor: getStatusColor(),
            display: 'inline-block'
          }} />
          <span className="mono" style={{ fontWeight: 600, color: getStatusColor() }}>
            {getStatusText()}
          </span>
          {lastSeen && (
            <span style={{ fontSize: '11px', color: 'var(--text-muted)', marginLeft: '4px' }}>
              ({lastSeen.toLocaleTimeString()})
            </span>
          )}
        </div>
      </div>

      <div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
        <button
          onClick={stop}
          style={{
            background: 'var(--bg-surface-elevated)',
            border: '1px solid var(--border-strong)',
            fontWeight: 600,
            padding: '8px 16px'
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
            border: 'none',
            fontWeight: 700,
            letterSpacing: '0.04em',
            padding: '8px 20px'
          }}
          title="Immediate Emergency Halt"
        >
          E-STOP
        </button>
      </div>
    </header>
  );
};
