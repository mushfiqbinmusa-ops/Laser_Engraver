import React, { useState } from 'react';
import { useMachine } from '../context/MachineContext';

export const CalibrationTab: React.FC = () => {
  const { connected, fresh, snapshot, home, jog, resetFault } = useMachine();
  const [speedSps, setSpeedSps] = useState<number>(100);
  const [isHoming, setIsHoming] = useState<boolean>(false);
  const [errorMsg, setErrorMsg] = useState<string | null>(null);

  const isJogDisabled = Boolean(!connected || !fresh || isHoming || snapshot?.fault || snapshot?.state === 'running_job');

  const runHome = async (axis: 'all' | 'x' | 'y') => {
    setIsHoming(true);
    setErrorMsg(null);
    try {
      await home(axis);
    } catch (err: any) {
      setErrorMsg(err.message);
    } finally {
      setIsHoming(false);
    }
  };

  const runJog = async (axis: 'x' | 'y', steps: number) => {
    setErrorMsg(null);
    try {
      await jog(axis, steps, speedSps);
    } catch (err: any) {
      setErrorMsg(err.message);
    }
  };

  const sx = snapshot?.sensors;
  const pos = snapshot?.pos || { x: 0, y: 0 };
  const homed = snapshot?.homed || { x: false, y: false };

  return (
    <div style={{ padding: '24px', display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '20px' }}>
      {/* Optical Sensor Diagnostics */}
      <div style={{ background: 'var(--bg-surface)', border: '1px solid var(--border-subtle)', borderRadius: '6px', padding: '20px' }}>
        <h2 style={{ fontSize: '15px', fontWeight: 600, marginBottom: '16px' }}>Optical Endstop Telemetry</h2>
        
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '16px', marginBottom: '20px' }}>
          {/* Sensor X */}
          <div style={{ background: 'var(--bg-surface-elevated)', border: '1px solid var(--border-subtle)', borderRadius: '6px', padding: '16px' }}>
            <div style={{ fontSize: '13px', fontWeight: 600, marginBottom: '8px' }}>Sensor X (GPIO 13)</div>
            <div style={{ display: 'flex', flexDirection: 'column', gap: '6px', fontSize: '12px' }}>
              <div>Raw Electrical: <span className="mono">{sx ? (sx.x_raw ? 'HIGH (3.3V)' : 'LOW (0V)') : '—'}</span></div>
              <div>Debounced State: <span className="mono" style={{ fontWeight: 600, color: sx?.x_filtered ? 'var(--accent-warning)' : 'var(--accent-ready)' }}>
                {sx ? (sx.x_filtered ? 'BLOCKED / DETECTED' : 'BEAM CLEAR') : '—'}
              </span></div>
              <div>Axis Home Status: <span className="mono">{homed.x ? 'HOMED ✓' : 'UNHOMED ✗'}</span></div>
            </div>
          </div>

          {/* Sensor Y */}
          <div style={{ background: 'var(--bg-surface-elevated)', border: '1px solid var(--border-subtle)', borderRadius: '6px', padding: '16px' }}>
            <div style={{ fontSize: '13px', fontWeight: 600, marginBottom: '8px' }}>Sensor Y (GPIO 8)</div>
            <div style={{ display: 'flex', flexDirection: 'column', gap: '6px', fontSize: '12px' }}>
              <div>Raw Electrical: <span className="mono">{sx ? (sx.y_raw ? 'HIGH (3.3V)' : 'LOW (0V)') : '—'}</span></div>
              <div>Debounced State: <span className="mono" style={{ fontWeight: 600, color: sx?.y_filtered ? 'var(--accent-warning)' : 'var(--accent-ready)' }}>
                {sx ? (sx.y_filtered ? 'BLOCKED / DETECTED' : 'BEAM CLEAR') : '—'}
              </span></div>
              <div>Axis Home Status: <span className="mono">{homed.y ? 'HOMED ✓' : 'UNHOMED ✗'}</span></div>
            </div>
          </div>
        </div>

        <h3 style={{ fontSize: '13px', fontWeight: 600, marginBottom: '12px' }}>Sensor-Driven Homing Sequence</h3>
        <p style={{ fontSize: '12px', color: 'var(--text-secondary)', marginBottom: '12px' }}>
          Runs the 5-phase hardware state machine: Validate → ClearInitial (if blocked) → FastSeek (120 sps) → PullOff → SlowLatch (40 sps).
        </p>
        <div style={{ display: 'flex', gap: '12px' }}>
          <button onClick={() => runHome('all')} disabled={isHoming} style={{ flex: 1, padding: '10px', fontWeight: 600 }}>
            {isHoming ? 'HOMING IN PROGRESS...' : 'HOME ALL AXES'}
          </button>
          <button onClick={() => runHome('x')} disabled={isHoming}>Home X</button>
          <button onClick={() => runHome('y')} disabled={isHoming}>Home Y</button>
        </div>

        {snapshot?.fault && (
          <div style={{ marginTop: '16px', padding: '10px', background: 'rgba(224, 49, 49, 0.15)', border: '1px solid var(--accent-estop)', borderRadius: '4px' }}>
            <div style={{ color: '#FFA8A8', fontWeight: 600, fontSize: '12px', marginBottom: '6px' }}>
              FAULT: {snapshot.fault}
            </div>
            <button onClick={resetFault} style={{ fontSize: '12px' }}>Reset Machine Fault</button>
          </div>
        )}
      </div>

      {/* Manual Bounded Jogging */}
      <div style={{ background: 'var(--bg-surface)', border: '1px solid var(--border-subtle)', borderRadius: '6px', padding: '20px' }}>
        <h2 style={{ fontSize: '15px', fontWeight: 600, marginBottom: '16px' }}>Manual Axis Jogging</h2>

        <div style={{ background: 'var(--bg-surface-elevated)', border: '1px solid var(--border-subtle)', borderRadius: '6px', padding: '14px', marginBottom: '16px' }}>
          <div style={{ fontSize: '12px', color: 'var(--text-secondary)', marginBottom: '4px' }}>Commanded Coordinates (Open-Loop Steps)</div>
          <div className="mono" style={{ fontSize: '20px', fontWeight: 600 }}>
            X: {pos.x} steps | Y: {pos.y} steps
          </div>
        </div>

        <div style={{ marginBottom: '16px' }}>
          <label style={{ display: 'block', fontSize: '12px', color: 'var(--text-secondary)', marginBottom: '6px' }}>
            Jog Rate (Steps per Second)
          </label>
          <div style={{ display: 'flex', gap: '8px' }}>
            {[40, 100, 200, 500].map((sps) => (
              <button
                key={sps}
                onClick={() => setSpeedSps(sps)}
                style={{
                  background: speedSps === sps ? 'var(--text-primary)' : 'var(--bg-surface-elevated)',
                  color: speedSps === sps ? 'var(--bg-app)' : 'var(--text-primary)'
                }}
              >
                {sps} sps
              </button>
            ))}
          </div>
        </div>

        {/* X Axis Jog */}
        <div style={{ marginBottom: '16px' }}>
          <div style={{ fontSize: '13px', fontWeight: 600, marginBottom: '8px' }}>X-Axis Jog (Motor A)</div>
          <div style={{ display: 'flex', gap: '8px' }}>
            {[-200, -50, -10, -1, 1, 10, 50, 200].map((steps) => (
              <button
                key={steps}
                disabled={isJogDisabled}
                onClick={() => runJog('x', steps)}
                aria-label={`Jog X ${steps > 0 ? '+' : ''}${steps} steps`}
              >
                {steps > 0 ? `+${steps}` : steps}
              </button>
            ))}
          </div>
        </div>

        {/* Y Axis Jog */}
        <div>
          <div style={{ fontSize: '13px', fontWeight: 600, marginBottom: '8px' }}>Y-Axis Jog (Motor B)</div>
          <div style={{ display: 'flex', gap: '8px' }}>
            {[-200, -50, -10, -1, 1, 10, 50, 200].map((steps) => (
              <button
                key={steps}
                disabled={isJogDisabled}
                onClick={() => runJog('y', steps)}
                aria-label={`Jog Y ${steps > 0 ? '+' : ''}${steps} steps`}
              >
                {steps > 0 ? `+${steps}` : steps}
              </button>
            ))}
          </div>
        </div>

        {errorMsg && (
          <div
            role="alert"
            aria-live="assertive"
            style={{
              marginTop: '16px',
              padding: '8px 12px',
              background: 'rgba(224, 49, 49, 0.15)',
              border: '1px solid var(--accent-estop)',
              borderRadius: '4px',
              color: '#FFA8A8',
              fontSize: '12px'
            }}
          >
            {errorMsg}
          </div>
        )}
      </div>
    </div>
  );
};
