import React, { useState } from 'react';
import { useMachine } from '../context/MachineContext';

export const CalibrationTab: React.FC = () => {
  const { connected, fresh, snapshot, home, jog, resetFault } = useMachine();
  const [speedSps, setSpeedSps] = useState<number>(100);
  const [jogStepSize, setJogStepSize] = useState<number>(16);
  const [customStepInput, setCustomStepInput] = useState<string>('16');
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

  const handleStepSizeSelect = (steps: number) => {
    setJogStepSize(steps);
    setCustomStepInput(String(steps));
  };

  const handleCustomStepChange = (val: string) => {
    setCustomStepInput(val);
    const num = parseInt(val, 10);
    if (!isNaN(num) && num > 0) {
      setJogStepSize(num);
    }
  };

  const sx = snapshot?.sensors;
  const pos = snapshot?.pos || { x: 0, y: 0 };
  const homed = snapshot?.homed || { x: false, y: false };

  return (
    <div style={{ padding: '24px', display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '24px' }}>
      {/* Left Column: Optical Endstop & Homing Controls */}
      <div style={{ display: 'flex', flexDirection: 'column', gap: '20px' }}>
        {/* Optical Endstop Telemetry */}
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

          <h3 style={{ fontSize: '13px', fontWeight: 600, marginBottom: '8px' }}>Two-Stage Optical Homing Sequence</h3>
          <p style={{ fontSize: '12px', color: 'var(--text-secondary)', marginBottom: '12px' }}>
            FastSeek (120 sps) → PullOff (200 steps) → SlowLatch (40 sps) → Margin PullOff (50 steps) → Zero Coordinate Alignment.
          </p>
          <div style={{ display: 'flex', gap: '10px' }}>
            <button
              onClick={() => runHome('all')}
              disabled={isHoming || !connected}
              style={{ flex: 2, padding: '10px', fontWeight: 600 }}
            >
              {isHoming ? 'HOMING IN PROGRESS...' : 'HOME ALL AXES (X & Y)'}
            </button>
            <button onClick={() => runHome('x')} disabled={isHoming || !connected} style={{ flex: 1 }}>Home X</button>
            <button onClick={() => runHome('y')} disabled={isHoming || !connected} style={{ flex: 1 }}>Home Y</button>
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

        {/* Microstepping Hardware Profile Info */}
        <div style={{ background: 'var(--bg-surface)', border: '1px solid var(--border-subtle)', borderRadius: '6px', padding: '16px' }}>
          <h3 style={{ fontSize: '13px', fontWeight: 600, marginBottom: '8px' }}>A4988 Microstep Hardware Profile</h3>
          <p style={{ fontSize: '12px', color: 'var(--text-secondary)', marginBottom: '8px' }}>
            Default hardware mode: <strong>1/16 Microstepping</strong> (Pins MS1=1, MS2=1, MS3=1).
          </p>
          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap: '8px', fontSize: '11px', textAlign: 'center' }}>
            <div style={{ background: 'var(--bg-app)', padding: '6px', borderRadius: '4px' }}>
              <div style={{ color: 'var(--text-secondary)' }}>Step Angle</div>
              <div style={{ fontWeight: 600 }}>1.8° (200/rev)</div>
            </div>
            <div style={{ background: 'var(--bg-app)', padding: '6px', borderRadius: '4px' }}>
              <div style={{ color: 'var(--text-secondary)' }}>Microstepping</div>
              <div style={{ fontWeight: 600, color: 'var(--accent-ready)' }}>1/16 Mode</div>
            </div>
            <div style={{ background: 'var(--bg-app)', padding: '6px', borderRadius: '4px' }}>
              <div style={{ color: 'var(--text-secondary)' }}>Steps / Rev</div>
              <div style={{ fontWeight: 600 }}>3,200 µsteps</div>
            </div>
            <div style={{ background: 'var(--bg-app)', padding: '6px', borderRadius: '4px' }}>
              <div style={{ color: 'var(--text-secondary)' }}>Resolution</div>
              <div style={{ fontWeight: 600 }}>80 µsteps / mm</div>
            </div>
          </div>
        </div>
      </div>

      {/* Right Column: Manual Bounded Jogging & Microstep Size Selection */}
      <div style={{ background: 'var(--bg-surface)', border: '1px solid var(--border-subtle)', borderRadius: '6px', padding: '20px' }}>
        <h2 style={{ fontSize: '15px', fontWeight: 600, marginBottom: '16px' }}>Manual Axis Jogging & Microstep Control</h2>

        {/* Commanded Coordinates */}
        <div style={{ background: 'var(--bg-surface-elevated)', border: '1px solid var(--border-subtle)', borderRadius: '6px', padding: '14px', marginBottom: '16px' }}>
          <div style={{ fontSize: '12px', color: 'var(--text-secondary)', marginBottom: '4px' }}>Current Position (Open-Loop Steps)</div>
          <div className="mono" style={{ fontSize: '20px', fontWeight: 600 }}>
            X: {pos.x} µsteps ({(pos.x / 80).toFixed(2)} mm) | Y: {pos.y} µsteps ({(pos.y / 80).toFixed(2)} mm)
          </div>
          <div style={{ fontSize: '11px', color: snapshot?.pos_valid ? 'var(--accent-ready)' : 'var(--accent-warning)', marginTop: '4px' }}>
            {snapshot?.pos_valid ? '● Position Calibrated & Valid' : '○ Unhomed / Open-Loop (Homing Recommended)'}
          </div>
        </div>

        {/* Microstep Increment Selection */}
        <div style={{ marginBottom: '18px' }}>
          <label htmlFor="microstep-size" style={{ display: 'block', fontSize: '12px', fontWeight: 600, marginBottom: '8px' }}>
            Microstep Increment per Jog Click:
          </label>
          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: '8px', marginBottom: '8px' }}>
            {[
              { label: '1 µstep (Fine)', steps: 1 },
              { label: '8 µsteps (1/2)', steps: 8 },
              { label: '16 µsteps (1 Full)', steps: 16 },
              { label: '40 µsteps (0.5mm)', steps: 40 },
              { label: '80 µsteps (1.0mm)', steps: 80 },
              { label: '160 µsteps (2.0mm)', steps: 160 },
            ].map((item) => (
              <button
                type="button"
                key={item.steps}
                onClick={() => handleStepSizeSelect(item.steps)}
                style={{
                  padding: '8px 6px',
                  fontSize: '11px',
                  fontWeight: 600,
                  background: jogStepSize === item.steps ? 'var(--text-primary)' : 'var(--bg-surface-elevated)',
                  color: jogStepSize === item.steps ? 'var(--bg-app)' : 'var(--text-primary)',
                  border: '1px solid var(--border-subtle)',
                  borderRadius: '4px'
                }}
              >
                {item.label}
              </button>
            ))}
          </div>

          <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
            <span style={{ fontSize: '12px', color: 'var(--text-secondary)' }}>Custom Microsteps:</span>
            <input
              id="microstep-size"
              type="number"
              min="1"
              max="20000"
              value={customStepInput}
              onChange={(e) => handleCustomStepChange(e.target.value)}
              style={{ width: '100px', padding: '4px 8px', fontSize: '12px' }}
            />
            <span style={{ fontSize: '11px', color: 'var(--text-muted)' }}>
              = {(jogStepSize / 80).toFixed(3)} mm
            </span>
          </div>
        </div>

        {/* Directional Jog Pad */}
        <div style={{ marginBottom: '20px', background: 'var(--bg-app)', padding: '16px', borderRadius: '6px', border: '1px solid var(--border-subtle)' }}>
          <div style={{ fontSize: '12px', fontWeight: 600, marginBottom: '12px', textAlign: 'center' }}>
            Directional Jog (±{jogStepSize} µsteps)
          </div>

          <div style={{ display: 'grid', gridTemplateColumns: '80px 80px 80px', justifyContent: 'center', gap: '8px' }}>
            <div />
            <button
              disabled={isJogDisabled}
              onClick={() => runJog('y', jogStepSize)}
              style={{ padding: '12px 6px', fontWeight: 600, fontSize: '13px' }}
              aria-label={`Jog Y +${jogStepSize} steps`}
            >
              ▲ +Y
            </button>
            <div />

            <button
              disabled={isJogDisabled}
              onClick={() => runJog('x', -jogStepSize)}
              style={{ padding: '12px 6px', fontWeight: 600, fontSize: '13px' }}
              aria-label={`Jog X -${jogStepSize} steps`}
            >
              ◄ -X
            </button>
            <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', fontSize: '11px', color: 'var(--text-muted)' }}>
              XY
            </div>
            <button
              disabled={isJogDisabled}
              onClick={() => runJog('x', jogStepSize)}
              style={{ padding: '12px 6px', fontWeight: 600, fontSize: '13px' }}
              aria-label={`Jog X +${jogStepSize} steps`}
            >
              +X ►
            </button>

            <div />
            <button
              disabled={isJogDisabled}
              onClick={() => runJog('y', -jogStepSize)}
              style={{ padding: '12px 6px', fontWeight: 600, fontSize: '13px' }}
              aria-label={`Jog Y -${jogStepSize} steps`}
            >
              ▼ -Y
            </button>
            <div />
          </div>
        </div>

        {/* Jog Speed Selection */}
        <div style={{ marginBottom: '16px' }}>
          <label style={{ display: 'block', fontSize: '12px', color: 'var(--text-secondary)', marginBottom: '6px' }}>
            Jog Speed (Steps per Second)
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

        {/* Quick Multi-Step Buttons */}
        <div>
          <div style={{ fontSize: '12px', color: 'var(--text-secondary)', marginBottom: '6px' }}>Quick Jumps:</div>
          <div style={{ display: 'flex', gap: '6px', flexWrap: 'wrap' }}>
            {[-200, -50, -10, 10, 50, 200].map((steps) => (
              <button
                key={`qx-${steps}`}
                disabled={isJogDisabled}
                onClick={() => runJog('x', steps)}
                style={{ fontSize: '11px', padding: '4px 8px' }}
              >
                X {steps > 0 ? `+${steps}` : steps}
              </button>
            ))}
            {[-200, -50, -10, 10, 50, 200].map((steps) => (
              <button
                key={`qy-${steps}`}
                disabled={isJogDisabled}
                onClick={() => runJog('y', steps)}
                style={{ fontSize: '11px', padding: '4px 8px' }}
              >
                Y {steps > 0 ? `+${steps}` : steps}
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
