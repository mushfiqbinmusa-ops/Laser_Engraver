import React, { useState } from 'react';
import { useMachine } from '../context/MachineContext';

export const CalibrationTab: React.FC = () => {
  const { connected, fresh, snapshot, home, jog, resetFault, setMicrostepMode } = useMachine();
  const [speedSps, setSpeedSps] = useState<number>(100);
  const [jogStepSize, setJogStepSize] = useState<number>(16);
  const [customStepInput, setCustomStepInput] = useState<string>('16');
  const [isHoming, setIsHoming] = useState<boolean>(false);
  const [errorMsg, setErrorMsg] = useState<string | null>(null);
  const [isJogging, setIsJogging] = useState<boolean>(false);
  const [isChangingMicrostep, setIsChangingMicrostep] = useState<boolean>(false);

  const microstepMode = (snapshot?.microstep_mode || 16) as 1 | 2 | 4 | 8 | 16;
  const stepsPerMm = 5 * microstepMode;
  const stepsPerRev = 2048 * microstepMode;

  const handleMicrostepChange = async (mode: 1 | 2 | 4 | 8 | 16) => {
    setIsChangingMicrostep(true);
    setErrorMsg(null);
    try {
      await setMicrostepMode(mode);
    } catch (err: any) {
      setErrorMsg(`Failed to set microstep mode: ${err.message}`);
    } finally {
      setIsChangingMicrostep(false);
    }
  };

  const isJogDisabled = Boolean(!connected || !fresh || isHoming || isJogging || snapshot?.fault || snapshot?.state === 'running_job' || snapshot?.state === 'moving');

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
    setIsJogging(true);
    setErrorMsg(null);
    try {
      await jog(axis, steps, speedSps);
      const estDurationMs = Math.round((Math.abs(steps) / speedSps) * 1000) + 120;
      await new Promise((r) => setTimeout(r, estDurationMs));
    } catch (err: any) {
      setErrorMsg(err.message);
    } finally {
      setIsJogging(false);
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
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '8px' }}>
                <span style={{ fontSize: '13px', fontWeight: 600 }}>Sensor X (GPIO 13)</span>
                <span style={{
                  fontSize: '10px',
                  fontWeight: 700,
                  padding: '2px 6px',
                  borderRadius: '3px',
                  background: sx?.x_filtered ? 'rgba(224, 49, 49, 0.2)' : 'rgba(47, 158, 68, 0.15)',
                  color: sx?.x_filtered ? 'var(--accent-estop)' : 'var(--accent-ready)',
                  border: `1px solid ${sx?.x_filtered ? 'var(--accent-estop)' : 'var(--accent-ready)'}`
                }}>
                  {sx ? (sx.x_filtered ? 'TRIPPED (0V)' : 'BEAM OPEN (3.3V)') : '—'}
                </span>
              </div>
              <div style={{ display: 'flex', flexDirection: 'column', gap: '6px', fontSize: '12px' }}>
                <div>Raw Electrical: <span className="mono">{sx ? (sx.x_raw ? 'HIGH / 3.3V (Unblocked)' : 'LOW / 0V (Obstructed)') : '—'}</span></div>
                <div>Debounced State: <span className="mono" style={{ fontWeight: 600, color: sx?.x_filtered ? 'var(--accent-estop)' : 'var(--accent-ready)' }}>
                  {sx ? (sx.x_filtered ? 'BLOCKED / DETECTED (Flag in slot)' : 'CLEAR (Beam unbroken)') : '—'}
                </span></div>
                <div>Axis Home Status: <span className="mono" style={{ fontWeight: 600, color: homed.x ? 'var(--accent-ready)' : 'var(--text-muted)' }}>
                  {homed.x ? 'HOMED ✓ (Origin Calibrated)' : 'UNHOMED ✗ (Needs Homing)'}
                </span></div>
              </div>
            </div>

            {/* Sensor Y */}
            <div style={{ background: 'var(--bg-surface-elevated)', border: '1px solid var(--border-subtle)', borderRadius: '6px', padding: '16px' }}>
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '8px' }}>
                <span style={{ fontSize: '13px', fontWeight: 600 }}>Sensor Y (GPIO 8)</span>
                <span style={{
                  fontSize: '10px',
                  fontWeight: 700,
                  padding: '2px 6px',
                  borderRadius: '3px',
                  background: sx?.y_filtered ? 'rgba(224, 49, 49, 0.2)' : 'rgba(47, 158, 68, 0.15)',
                  color: sx?.y_filtered ? 'var(--accent-estop)' : 'var(--accent-ready)',
                  border: `1px solid ${sx?.y_filtered ? 'var(--accent-estop)' : 'var(--accent-ready)'}`
                }}>
                  {sx ? (sx.y_filtered ? 'TRIPPED (0V)' : 'BEAM OPEN (3.3V)') : '—'}
                </span>
              </div>
              <div style={{ display: 'flex', flexDirection: 'column', gap: '6px', fontSize: '12px' }}>
                <div>Raw Electrical: <span className="mono">{sx ? (sx.y_raw ? 'HIGH / 3.3V (Unblocked)' : 'LOW / 0V (Obstructed)') : '—'}</span></div>
                <div>Debounced State: <span className="mono" style={{ fontWeight: 600, color: sx?.y_filtered ? 'var(--accent-estop)' : 'var(--accent-ready)' }}>
                  {sx ? (sx.y_filtered ? 'BLOCKED / DETECTED (Flag in slot)' : 'CLEAR (Beam unbroken)') : '—'}
                </span></div>
                <div>Axis Home Status: <span className="mono" style={{ fontWeight: 600, color: homed.y ? 'var(--accent-ready)' : 'var(--text-muted)' }}>
                  {homed.y ? 'HOMED ✓ (Origin Calibrated)' : 'UNHOMED ✗ (Needs Homing)'}
                </span></div>
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

        {/* Microstepping Hardware Mode Selection (A4988 MS1/MS2/MS3 Pins) */}
        <div style={{ background: 'var(--bg-surface)', border: '1px solid var(--border-subtle)', borderRadius: '6px', padding: '16px' }}>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '8px' }}>
            <h3 style={{ fontSize: '13px', fontWeight: 600 }}>A4988 Hardware Microstep Mode</h3>
            <span style={{ fontSize: '11px', fontWeight: 600, color: 'var(--accent-ready)' }}>
              Active: 1/{microstepMode} Mode
            </span>
          </div>
          <p style={{ fontSize: '12px', color: 'var(--text-secondary)', marginBottom: '12px' }}>
            Switch hardware pin states (GPIO 1: MS1, GPIO 2: MS2, GPIO 4: MS3) like in <code>stepper_ir_homing_test</code>.
          </p>

          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(5, 1fr)', gap: '6px', marginBottom: '14px' }}>
            {([
              { mode: 1, label: '1/1 Full', ms: '000', stepsRev: 2048 },
              { mode: 2, label: '1/2 Half', ms: '100', stepsRev: 4096 },
              { mode: 4, label: '1/4 Quad', ms: '010', stepsRev: 8192 },
              { mode: 8, label: '1/8 Octa', ms: '110', stepsRev: 16384 },
              { mode: 16, label: '1/16 Hex', ms: '111', stepsRev: 32768 },
            ] as const).map((m) => (
              <button
                key={m.mode}
                type="button"
                disabled={!connected || isChangingMicrostep}
                onClick={() => handleMicrostepChange(m.mode)}
                style={{
                  padding: '8px 4px',
                  textAlign: 'center',
                  background: microstepMode === m.mode ? 'var(--text-primary)' : 'var(--bg-surface-elevated)',
                  color: microstepMode === m.mode ? 'var(--bg-app)' : 'var(--text-primary)',
                  border: `1px solid ${microstepMode === m.mode ? 'var(--text-primary)' : 'var(--border-subtle)'}`,
                  borderRadius: '4px'
                }}
              >
                <div style={{ fontSize: '12px', fontWeight: 700 }}>{m.label}</div>
                <div style={{ fontSize: '10px', opacity: 0.8 }}>MS: {m.ms}</div>
              </button>
            ))}
          </div>

          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: '8px', fontSize: '11px', textAlign: 'center' }}>
            <div style={{ background: 'var(--bg-app)', padding: '6px', borderRadius: '4px' }}>
              <div style={{ color: 'var(--text-secondary)' }}>Full Step Baseline</div>
              <div style={{ fontWeight: 600 }}>2048 steps / rev</div>
            </div>
            <div style={{ background: 'var(--bg-app)', padding: '6px', borderRadius: '4px' }}>
              <div style={{ color: 'var(--text-secondary)' }}>Effective Resolution</div>
              <div style={{ fontWeight: 600, color: 'var(--accent-ready)' }}>{stepsPerRev} steps / rev</div>
            </div>
            <div style={{ background: 'var(--bg-app)', padding: '6px', borderRadius: '4px' }}>
              <div style={{ color: 'var(--text-secondary)' }}>Linear Scale</div>
              <div style={{ fontWeight: 600 }}>{stepsPerMm} steps / mm</div>
            </div>
          </div>
        </div>
      </div>

      {/* Right Column: Manual Bounded Jogging & Microstep Size Selection */}
      <div style={{ background: 'var(--bg-surface)', border: '1px solid var(--border-subtle)', borderRadius: '6px', padding: '20px' }}>
        <h2 style={{ fontSize: '15px', fontWeight: 600, marginBottom: '16px' }}>Manual Axis Jogging & Microstep Control</h2>

        {/* Commanded Coordinates */}
        <div style={{ background: 'var(--bg-surface-elevated)', border: '1px solid var(--border-subtle)', borderRadius: '6px', padding: '14px', marginBottom: '16px' }}>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '6px' }}>
            <span style={{ fontSize: '12px', color: 'var(--text-secondary)' }}>Axis Position Coordinates</span>
            <span style={{
              fontSize: '11px',
              fontWeight: 700,
              padding: '2px 8px',
              borderRadius: '3px',
              background: (homed.x && homed.y && snapshot?.pos_valid) ? 'rgba(47, 158, 68, 0.2)' : 'rgba(240, 140, 0, 0.2)',
              color: (homed.x && homed.y && snapshot?.pos_valid) ? 'var(--accent-ready)' : '#FFD43B',
              border: `1px solid ${(homed.x && homed.y && snapshot?.pos_valid) ? 'var(--accent-ready)' : 'rgba(240, 140, 0, 0.4)'}`
            }}>
              {(homed.x && homed.y && snapshot?.pos_valid) ? '● ORIGIN HOMED & CALIBRATED' : '○ UNHOMED (OPEN-LOOP BASELINE)'}
            </span>
          </div>
          <div className="mono" style={{ fontSize: '20px', fontWeight: 600, margin: '6px 0' }}>
            X: {pos.x} steps ({(pos.x / stepsPerMm).toFixed(2)} mm) | Y: {pos.y} steps ({(pos.y / stepsPerMm).toFixed(2)} mm)
          </div>
          {(!homed.x || !homed.y || !snapshot?.pos_valid) ? (
            <div style={{ fontSize: '11px', color: '#FFD43B', background: 'rgba(240, 140, 0, 0.1)', padding: '6px 8px', borderRadius: '4px', border: '1px dashed rgba(240, 140, 0, 0.3)' }}>
              ⚠️ Coordinates (0, 0) are an arbitrary open-loop startup reference. Motors are <strong>UNHOMED</strong>. Run <strong>HOME ALL AXES</strong> to locate physical optical endstops.
            </div>
          ) : (
            <div style={{ fontSize: '11px', color: 'var(--accent-ready)' }}>
              ✓ Physical optical endstop origin verified. Absolute coordinate boundary limits enforced.
            </div>
          )}
        </div>

        {/* Microstep Increment Selection */}
        <div style={{ marginBottom: '18px' }}>
          <label htmlFor="microstep-size" style={{ display: 'block', fontSize: '12px', fontWeight: 600, marginBottom: '8px' }}>
            Step Increment per Jog Click:
          </label>
          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: '8px', marginBottom: '8px' }}>
            {[
              { label: `1 step (${(1 / stepsPerMm).toFixed(3)}mm)`, steps: 1 },
              { label: `${microstepMode / 2 || 1} steps (Half)`, steps: Math.max(1, Math.round(microstepMode / 2)) },
              { label: `${microstepMode} steps (1 Full)`, steps: microstepMode },
              { label: `${Math.round(stepsPerMm * 0.5)} steps (0.5mm)`, steps: Math.max(1, Math.round(stepsPerMm * 0.5)) },
              { label: `${Math.round(stepsPerMm * 1.0)} steps (1.0mm)`, steps: Math.max(1, Math.round(stepsPerMm * 1.0)) },
              { label: `${Math.round(stepsPerMm * 2.0)} steps (2.0mm)`, steps: Math.max(1, Math.round(stepsPerMm * 2.0)) },
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
            <span style={{ fontSize: '12px', color: 'var(--text-secondary)' }}>Custom Steps:</span>
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
              = {(jogStepSize / stepsPerMm).toFixed(3)} mm
            </span>
          </div>
        </div>

        {/* Directional Jog Pad */}
        <div style={{ marginBottom: '20px', background: 'var(--bg-app)', padding: '16px', borderRadius: '6px', border: '1px solid var(--border-subtle)' }}>
          <div style={{ fontSize: '12px', fontWeight: 600, marginBottom: '12px', textAlign: 'center' }}>
            Directional Jog (±{jogStepSize} steps)
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
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '6px' }}>
            <label style={{ fontSize: '12px', color: 'var(--text-secondary)' }}>
              Jog Stepping Rate (Steps / Second)
            </label>
            <span style={{ fontSize: '11px', color: 'var(--accent-ready)', fontWeight: 600 }}>
              {speedSps} sps ({(speedSps / stepsPerMm).toFixed(2)} mm/s)
            </span>
          </div>
          <div style={{ display: 'flex', gap: '6px', marginBottom: '6px' }}>
            {[40, 80, 100, 200, 500, 1000].map((sps) => (
              <button
                key={sps}
                onClick={() => setSpeedSps(sps)}
                style={{
                  flex: 1,
                  padding: '6px 4px',
                  fontSize: '11px',
                  fontWeight: 600,
                  background: speedSps === sps ? 'var(--text-primary)' : 'var(--bg-surface-elevated)',
                  color: speedSps === sps ? 'var(--bg-app)' : 'var(--text-primary)'
                }}
              >
                {sps}
              </button>
            ))}
          </div>
          <div style={{ fontSize: '11px', color: 'var(--text-muted)', lineHeight: '1.4' }}>
            💡 <strong>Note on 28BYJ-48 Steppers:</strong> High pulse frequencies (&gt;300 sps in full-step or &gt;1000 sps in microstepping) exceed the 64:1 magnetic rotor pull-in torque limit and cause stalling/cogging, making it appear sluggish. 80–200 sps gives maximum torque and smooth rotation.
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
