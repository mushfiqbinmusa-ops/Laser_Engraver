import React, { useState } from 'react';
import { useMachine } from '../context/MachineContext';

export const CalibrationTab: React.FC = () => {
  const { connected, snapshot, home, jog, setDriver, setMicrostepMode, resetFault, homePhase, homeResult } = useMachine();
  const [jogStepSize, setJogStepSize] = useState<number>(100);
  const [customStepInput, setCustomStepInput] = useState<string>('100');
  const [speedSps, setSpeedSps] = useState<number>(100);
  const [isHoming, setIsHoming] = useState<boolean>(false);
  const [isChangingMicrostep, setIsChangingMicrostep] = useState<boolean>(false);
  const [errorMsg, setErrorMsg] = useState<string | null>(null);
  const [driverFeedback, setDriverFeedback] = useState<{ text: string; isError: boolean } | null>(null);
  const [togglingDriver, setTogglingDriver] = useState(false);

  const homed = snapshot?.homed || { x: false, y: false };
  const pos = snapshot?.pos || { x: 0, y: 0 };
  const sx = snapshot?.sensors;
  const microstepMode = snapshot?.microstep_mode || 1;
  const isBusy = snapshot?.state === 'running_job' || snapshot?.state === 'moving' || isHoming;
  const isJogDisabled = !connected || isBusy || Boolean(snapshot?.fault);
  const isMachineFaulted = Boolean(snapshot?.state === 'faulted' || (snapshot?.fault && snapshot.fault !== 'none'));

  // Scaled linear math: baseline is 80 steps/mm at 1/16 mode
  const stepsPerMm = Math.max(1, Math.round((80.0 * microstepMode) / 16.0));
  const stepsPerRev = 2048 * microstepMode;
  const liveMaxTravel = Math.round((4096 * microstepMode) / 16);

  const runHome = async (axis: 'all' | 'x' | 'y') => {
    setIsHoming(true);
    setErrorMsg(null);
    try {
      await home(axis);
    } catch (e: any) {
      setErrorMsg(e.message || 'Homing failed');
    } finally {
      setIsHoming(false);
    }
  };

  const runJog = async (axis: 'x' | 'y', steps: number) => {
    setErrorMsg(null);
    try {
      await jog(axis, steps, speedSps);
    } catch (e: any) {
      setErrorMsg(e.message || 'Jog rejected');
    }
  };

  const handleToggleDriver = async () => {
    setTogglingDriver(true);
    setDriverFeedback(null);
    const targetState = !snapshot?.drivers_enabled;
    try {
      await setDriver(targetState);
      setDriverFeedback({
        text: targetState
          ? 'MOTORS ENERGIZED: Holding torque applied to coils. Position is locked.'
          : 'DRIVERS DISABLED: High-impedance mode. Coils unpowered, carriage can be moved by hand.',
        isError: false
      });
    } catch (e: any) {
      setDriverFeedback({ text: `Driver control error: ${e.message}`, isError: true });
    } finally {
      setTogglingDriver(false);
    }
  };

  const handleMicrostepChange = async (mode: 1 | 2 | 4 | 8 | 16) => {
    if (mode === microstepMode) return;
    setIsChangingMicrostep(true);
    setErrorMsg(null);
    try {
      await setMicrostepMode(mode);
    } catch (e: any) {
      setErrorMsg(e.message || 'Failed to switch microstep mode');
    } finally {
      setIsChangingMicrostep(false);
    }
  };

  const handleStepSizeSelect = (steps: number) => {
    setJogStepSize(steps);
    setCustomStepInput(String(steps));
  };

  const handleCustomStepChange = (val: string) => {
    setCustomStepInput(val);
    const parsed = parseInt(val, 10);
    if (!isNaN(parsed) && parsed > 0 && parsed <= liveMaxTravel) {
      setJogStepSize(parsed);
    }
  };

  return (
    <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '20px', maxWidth: '1440px', margin: '0 auto' }}>
      {/* Left Column: Optical Endstop & Homing Controls */}
      <div style={{ display: 'flex', flexDirection: 'column', gap: '16px' }}>
        {/* Optical Endstop Telemetry */}
        <div style={{ background: 'var(--bg-surface)', border: '1px solid var(--border-subtle)', borderRadius: '4px', padding: '16px', boxShadow: 'var(--shadow-subtle)' }}>
          <h2 style={{ fontSize: '12px', textTransform: 'uppercase', letterSpacing: '0.04em', fontWeight: 600, color: 'var(--text-primary)', marginBottom: '14px' }}>
            Optical Endstop Telemetry
          </h2>
          
          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '12px', marginBottom: '16px' }}>
            {/* Sensor X */}
            <div style={{ background: 'var(--bg-surface-elevated)', border: '1px solid var(--border-subtle)', borderRadius: '4px', padding: '12px' }}>
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '8px' }}>
                <span style={{ fontSize: '12px', fontWeight: 600, color: 'var(--text-primary)' }}>Sensor X (GPIO 13)</span>
                <span style={{
                  fontSize: '10px',
                  fontWeight: 600,
                  padding: '2px 6px',
                  borderRadius: '3px',
                  background: sx?.x_filtered ? '#FEF2F2' : '#F3F4F6',
                  color: sx?.x_filtered ? '#991B1B' : '#374151',
                  border: `1px solid ${sx?.x_filtered ? '#FCA5A5' : '#E5E7EB'}`
                }}>
                  {sx ? (sx.x_filtered ? 'TRIPPED (0V)' : 'BEAM OPEN (3.3V)') : '—'}
                </span>
              </div>
              <div style={{ display: 'flex', flexDirection: 'column', gap: '4px', fontSize: '11px', color: 'var(--text-secondary)' }}>
                <div>Raw Electrical: <span className="mono" style={{ color: 'var(--text-primary)' }}>{sx ? (sx.x_raw ? 'HIGH / 3.3V (Open)' : 'LOW / 0V (Blocked)') : '—'}</span></div>
                <div>Debounced State: <span className="mono" style={{ fontWeight: 600, color: 'var(--text-primary)' }}>
                  {sx ? (sx.x_filtered ? 'BLOCKED / DETECTED' : 'CLEAR') : '—'}
                </span></div>
                <div>Axis Homed: <span className="mono" style={{ fontWeight: 600, color: homed.x ? 'var(--text-primary)' : 'var(--text-muted)' }}>
                  {homed.x ? 'CALIBRATED ✓' : 'UNHOMED'}
                </span></div>
              </div>
            </div>

            {/* Sensor Y */}
            <div style={{ background: 'var(--bg-surface-elevated)', border: '1px solid var(--border-subtle)', borderRadius: '4px', padding: '12px' }}>
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '8px' }}>
                <span style={{ fontSize: '12px', fontWeight: 600, color: 'var(--text-primary)' }}>Sensor Y (GPIO 8)</span>
                <span style={{
                  fontSize: '10px',
                  fontWeight: 600,
                  padding: '2px 6px',
                  borderRadius: '3px',
                  background: sx?.y_filtered ? '#FEF2F2' : '#F3F4F6',
                  color: sx?.y_filtered ? '#991B1B' : '#374151',
                  border: `1px solid ${sx?.y_filtered ? '#FCA5A5' : '#E5E7EB'}`
                }}>
                  {sx ? (sx.y_filtered ? 'TRIPPED (0V)' : 'BEAM OPEN (3.3V)') : '—'}
                </span>
              </div>
              <div style={{ display: 'flex', flexDirection: 'column', gap: '4px', fontSize: '11px', color: 'var(--text-secondary)' }}>
                <div>Raw Electrical: <span className="mono" style={{ color: 'var(--text-primary)' }}>{sx ? (sx.y_raw ? 'HIGH / 3.3V (Open)' : 'LOW / 0V (Blocked)') : '—'}</span></div>
                <div>Debounced State: <span className="mono" style={{ fontWeight: 600, color: 'var(--text-primary)' }}>
                  {sx ? (sx.y_filtered ? 'BLOCKED / DETECTED' : 'CLEAR') : '—'}
                </span></div>
                <div>Axis Homed: <span className="mono" style={{ fontWeight: 600, color: homed.y ? 'var(--text-primary)' : 'var(--text-muted)' }}>
                  {homed.y ? 'CALIBRATED ✓' : 'UNHOMED'}
                </span></div>
              </div>
            </div>
          </div>

          <h3 style={{ fontSize: '11px', textTransform: 'uppercase', letterSpacing: '0.03em', fontWeight: 600, color: 'var(--text-secondary)', marginBottom: '6px' }}>
            5-Phase Optical Homing
          </h3>
          <p style={{ fontSize: '11px', color: 'var(--text-muted)', marginBottom: '12px', lineHeight: '1.4' }}>
            ClearInitial → FastSeek → PullOff → SlowLatch → FinalPullOff → Zero Origin. Seek directions: X +, Y -.
          </p>
          <div style={{ display: 'flex', gap: '8px' }}>
            <button
              onClick={() => runHome('all')}
              disabled={isHoming || !connected}
              style={{
                flex: 2,
                padding: '8px 12px',
                fontWeight: 600,
                fontSize: '12px',
                background: '#111827',
                color: '#FFFFFF',
                border: '1px solid #111827'
              }}
            >
              {isHoming ? 'HOMING IN PROGRESS...' : 'HOME ALL AXES (X & Y)'}
            </button>
            <button onClick={() => runHome('x')} disabled={isHoming || !connected} style={{ flex: 1, fontSize: '11px' }}>Home X</button>
            <button onClick={() => runHome('y')} disabled={isHoming || !connected} style={{ flex: 1, fontSize: '11px' }}>Home Y</button>
          </div>

          {homePhase && (
            <div style={{ marginTop: '12px', padding: '8px 10px', background: 'var(--bg-surface-elevated)', border: '1px solid var(--border-subtle)', borderRadius: '4px', fontSize: '11px', color: 'var(--text-secondary)' }}>
              Homing phase: <span className="mono" style={{ fontWeight: 600, color: 'var(--text-primary)' }}>{homePhase}</span>
            </div>
          )}

          {homeResult && homeResult !== 'success' && (
            <div style={{ marginTop: '12px', padding: '8px 10px', background: '#FEF2F2', border: '1px solid #FCA5A5', borderRadius: '4px', fontSize: '11px', color: '#991B1B' }}>
              Last homing result: <span className="mono" style={{ fontWeight: 600 }}>{homeResult}</span>
            </div>
          )}

          {snapshot?.fault && (
            <div style={{ marginTop: '14px', padding: '10px 12px', background: '#FEF2F2', border: '1px solid #FCA5A5', borderRadius: '4px' }}>
              <div style={{ color: '#991B1B', fontWeight: 600, fontSize: '11px', marginBottom: '6px' }}>
                FAULT: {snapshot.fault}
              </div>
              <button onClick={resetFault} style={{ fontSize: '11px' }}>Reset Machine Fault</button>
            </div>
          )}
        </div>

        {/* A4988 Driver Energize / De-energize */}
        <div style={{ background: 'var(--bg-surface)', border: '1px solid var(--border-subtle)', borderRadius: '4px', padding: '16px', boxShadow: 'var(--shadow-subtle)' }}>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '10px' }}>
            <h2 style={{ fontSize: '12px', textTransform: 'uppercase', letterSpacing: '0.04em', fontWeight: 600, color: 'var(--text-primary)' }}>
              A4988 Driver Power (Coil Energize)
            </h2>
            <span
              className="mono"
              style={{
                fontSize: '10px',
                padding: '2px 8px',
                borderRadius: '4px',
                fontWeight: 600,
                background: 'var(--bg-surface-elevated)',
                color: 'var(--text-primary)',
                border: '1px solid var(--border-subtle)',
                display: 'inline-flex',
                alignItems: 'center',
                gap: '6px'
              }}
            >
              <span style={{ width: '6px', height: '6px', borderRadius: '50%', background: snapshot?.drivers_enabled ? '#111827' : '#9CA3AF' }} />
              {snapshot?.drivers_enabled ? 'HOLDING TORQUE ACTIVE' : 'DRIVERS DISABLED'}
            </span>
          </div>

          <p style={{ fontSize: '11px', color: 'var(--text-secondary)', marginBottom: '12px', lineHeight: 1.5 }}>
            Controls the A4988 ENABLE lines (active LOW). Energized coils hold position with holding torque. Disabled coils let the carriage move freely by hand.
          </p>

          <button
            onClick={handleToggleDriver}
            disabled={!connected || isMachineFaulted || togglingDriver}
            style={{
              fontWeight: 600,
              padding: '8px 14px',
              fontSize: '12px',
              background: snapshot?.drivers_enabled ? 'var(--bg-surface)' : '#111827',
              color: snapshot?.drivers_enabled ? 'var(--text-primary)' : '#FFFFFF',
              border: `1px solid ${snapshot?.drivers_enabled ? 'var(--border-strong)' : '#111827'}`
            }}
          >
            {togglingDriver
              ? 'Updating Drivers...'
              : snapshot?.drivers_enabled
                ? 'Disable Drivers (Free Carriage)'
                : 'Energize Motors (Apply Holding Torque)'}
          </button>

          {driverFeedback && (
            <div
              style={{
                marginTop: '10px',
                padding: '8px 12px',
                borderRadius: '4px',
                fontSize: '11px',
                fontWeight: 500,
                background: driverFeedback.isError ? '#FEF2F2' : 'var(--bg-surface-elevated)',
                color: driverFeedback.isError ? '#991B1B' : 'var(--text-secondary)',
                border: `1px solid ${driverFeedback.isError ? '#FCA5A5' : 'var(--border-subtle)'}`
              }}
            >
              {driverFeedback.text}
            </div>
          )}
        </div>

        {/* Microstepping Hardware Mode Selection (A4988 MS1/MS2/MS3 Pins) */}
        <div style={{ background: 'var(--bg-surface)', border: '1px solid var(--border-subtle)', borderRadius: '4px', padding: '16px', boxShadow: 'var(--shadow-subtle)' }}>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '8px' }}>
            <h3 style={{ fontSize: '12px', textTransform: 'uppercase', letterSpacing: '0.04em', fontWeight: 600, color: 'var(--text-primary)' }}>
              Microstepping Hardware Mode
            </h3>
            <span style={{ fontSize: '11px', fontWeight: 600, color: 'var(--text-primary)' }}>
              Active: 1/{microstepMode}
            </span>
          </div>
          <p style={{ fontSize: '11px', color: 'var(--text-secondary)', marginBottom: '12px' }}>
            Switch hardware pin states (GPIO 1: MS1, GPIO 2: MS2, GPIO 4: MS3). Rescales coordinate plan automatically.
          </p>

          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(5, 1fr)', gap: '6px', marginBottom: '14px' }}>
            {([
              { mode: 1, label: '1/1 Full', ms: '000' },
              { mode: 2, label: '1/2 Half', ms: '100' },
              { mode: 4, label: '1/4 Quad', ms: '010' },
              { mode: 8, label: '1/8 Octa', ms: '110' },
              { mode: 16, label: '1/16 Hex', ms: '111' },
            ] as const).map((m) => (
              <button
                key={m.mode}
                type="button"
                disabled={!connected || isChangingMicrostep || isBusy}
                onClick={() => handleMicrostepChange(m.mode)}
                style={{
                  padding: '6px 4px',
                  textAlign: 'center',
                  background: microstepMode === m.mode ? '#111827' : '#FFFFFF',
                  color: microstepMode === m.mode ? '#FFFFFF' : 'var(--text-primary)',
                  border: `1px solid ${microstepMode === m.mode ? '#111827' : 'var(--border-strong)'}`,
                  borderRadius: '4px',
                  boxShadow: 'none'
                }}
              >
                <div style={{ fontSize: '11px', fontWeight: 600 }}>{m.label}</div>
                <div style={{ fontSize: '9px', opacity: 0.7 }}>MS: {m.ms}</div>
              </button>
            ))}
          </div>

          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: '8px', fontSize: '11px', textAlign: 'center' }}>
            <div style={{ background: 'var(--bg-surface-elevated)', padding: '6px 8px', borderRadius: '4px', border: '1px solid var(--border-subtle)' }}>
              <div style={{ color: 'var(--text-muted)', fontSize: '10px' }}>Full Step Baseline</div>
              <div style={{ fontWeight: 600, color: 'var(--text-primary)' }}>2048 steps/rev</div>
            </div>
            <div style={{ background: 'var(--bg-surface-elevated)', padding: '6px 8px', borderRadius: '4px', border: '1px solid var(--border-subtle)' }}>
              <div style={{ color: 'var(--text-muted)', fontSize: '10px' }}>Effective Resolution</div>
              <div style={{ fontWeight: 600, color: 'var(--text-primary)' }}>{stepsPerRev} steps/rev</div>
            </div>
            <div style={{ background: 'var(--bg-surface-elevated)', padding: '6px 8px', borderRadius: '4px', border: '1px solid var(--border-subtle)' }}>
              <div style={{ color: 'var(--text-muted)', fontSize: '10px' }}>Linear Scale</div>
              <div style={{ fontWeight: 600, color: 'var(--text-primary)' }}>{stepsPerMm} steps/mm</div>
            </div>
          </div>
        </div>
      </div>

      {/* Right Column: Manual Bounded Jogging & Microstep Size Selection */}
      <div style={{ background: 'var(--bg-surface)', border: '1px solid var(--border-subtle)', borderRadius: '4px', padding: '16px', boxShadow: 'var(--shadow-subtle)' }}>
        <h2 style={{ fontSize: '12px', textTransform: 'uppercase', letterSpacing: '0.04em', fontWeight: 600, color: 'var(--text-primary)', marginBottom: '14px' }}>
          Manual Axis Jogging & Microstep Control
        </h2>

        {/* Commanded Coordinates */}
        <div style={{ background: 'var(--bg-surface-elevated)', border: '1px solid var(--border-subtle)', borderRadius: '4px', padding: '12px', marginBottom: '14px' }}>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '4px' }}>
            <span style={{ fontSize: '11px', color: 'var(--text-secondary)' }}>Axis Position Coordinates</span>
            <span style={{
              fontSize: '10px',
              fontWeight: 600,
              padding: '2px 6px',
              borderRadius: '3px',
              background: (homed.x && homed.y && snapshot?.pos_valid) ? '#F3F4F6' : '#FEF3C7',
              color: (homed.x && homed.y && snapshot?.pos_valid) ? '#111827' : '#92400E',
              border: `1px solid ${(homed.x && homed.y && snapshot?.pos_valid) ? '#D1D5DB' : '#FCD34D'}`
            }}>
              {(homed.x && homed.y && snapshot?.pos_valid) ? '● HOMED & CALIBRATED' : '○ UNHOMED'}
            </span>
          </div>
          <div className="mono" style={{ fontSize: '18px', fontWeight: 600, margin: '4px 0', color: 'var(--text-primary)' }}>
            X: {pos.x} steps ({(pos.x / stepsPerMm).toFixed(2)} mm) | Y: {pos.y} steps ({(pos.y / stepsPerMm).toFixed(2)} mm)
          </div>
          {(!homed.x || !homed.y || !snapshot?.pos_valid) ? (
            <div style={{ fontSize: '10px', color: '#92400E', background: '#FEF3C7', padding: '4px 8px', borderRadius: '3px', border: '1px solid #FCD34D' }}>
              Motors are <strong>UNHOMED</strong>. Run <strong>HOME ALL AXES</strong> to locate physical optical endstops.
            </div>
          ) : (
            <div style={{ fontSize: '10px', color: 'var(--text-secondary)' }}>
              Physical optical endstops calibrated. Coordinate boundaries enforced.
            </div>
          )}
        </div>

        {/* Microstep Increment Selection */}
        <div style={{ marginBottom: '16px' }}>
          <label htmlFor="microstep-size" style={{ display: 'block', fontSize: '11px', fontWeight: 600, marginBottom: '6px', color: 'var(--text-primary)' }}>
            Step Increment per Jog Click:
          </label>
          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: '6px', marginBottom: '8px' }}>
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
                  padding: '6px 4px',
                  fontSize: '11px',
                  fontWeight: 500,
                  background: jogStepSize === item.steps ? '#111827' : '#FFFFFF',
                  color: jogStepSize === item.steps ? '#FFFFFF' : 'var(--text-primary)',
                  border: `1px solid ${jogStepSize === item.steps ? '#111827' : 'var(--border-strong)'}`,
                  borderRadius: '3px',
                  boxShadow: 'none'
                }}
              >
                {item.label}
              </button>
            ))}
          </div>

          <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
            <span style={{ fontSize: '11px', color: 'var(--text-secondary)' }}>Custom Steps:</span>
            <input
              id="microstep-size"
              type="number"
              min="1"
              max={liveMaxTravel}
              value={customStepInput}
              onChange={(e) => handleCustomStepChange(e.target.value)}
              style={{ width: '90px', padding: '3px 6px', fontSize: '11px' }}
            />
            <span style={{ fontSize: '11px', color: 'var(--text-muted)' }}>
              = {(jogStepSize / stepsPerMm).toFixed(3)} mm
            </span>
          </div>
        </div>

        {/* Directional Jog Pad */}
        <div style={{ marginBottom: '16px', background: '#FAFAFA', padding: '12px', borderRadius: '4px', border: '1px solid var(--border-subtle)' }}>
          <div style={{ fontSize: '11px', fontWeight: 600, marginBottom: '10px', textAlign: 'center', color: 'var(--text-primary)' }}>
            Directional Jog (±{jogStepSize} steps)
          </div>

          <div style={{ display: 'grid', gridTemplateColumns: '70px 70px 70px', justifyContent: 'center', gap: '6px' }}>
            <div />
            <button
              disabled={isJogDisabled}
              onClick={() => runJog('y', jogStepSize)}
              style={{ padding: '10px 4px', fontWeight: 600, fontSize: '12px' }}
              aria-label={`Jog Y +${jogStepSize} steps`}
            >
              ▲ +Y
            </button>
            <div />

            <button
              disabled={isJogDisabled}
              onClick={() => runJog('x', -jogStepSize)}
              style={{ padding: '10px 4px', fontWeight: 600, fontSize: '12px' }}
              aria-label={`Jog X -${jogStepSize} steps`}
            >
              ◄ -X
            </button>
            <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', fontSize: '10px', color: 'var(--text-muted)', fontFamily: 'var(--font-mono)' }}>
              XY
            </div>
            <button
              disabled={isJogDisabled}
              onClick={() => runJog('x', jogStepSize)}
              style={{ padding: '10px 4px', fontWeight: 600, fontSize: '12px' }}
              aria-label={`Jog X +${jogStepSize} steps`}
            >
              +X ►
            </button>

            <div />
            <button
              disabled={isJogDisabled}
              onClick={() => runJog('y', -jogStepSize)}
              style={{ padding: '10px 4px', fontWeight: 600, fontSize: '12px' }}
              aria-label={`Jog Y -${jogStepSize} steps`}
            >
              ▼ -Y
            </button>
            <div />
          </div>
        </div>

        {/* Jog Speed Selection */}
        <div style={{ marginBottom: '14px' }}>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '6px' }}>
            <label style={{ fontSize: '11px', color: 'var(--text-secondary)' }}>
              Jog Stepping Rate (Steps / Second)
            </label>
            <span className="mono" style={{ fontSize: '11px', color: 'var(--text-primary)', fontWeight: 600 }}>
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
                  padding: '5px 2px',
                  fontSize: '11px',
                  fontWeight: 600,
                  background: speedSps === sps ? '#111827' : '#FFFFFF',
                  color: speedSps === sps ? '#FFFFFF' : 'var(--text-primary)',
                  border: `1px solid ${speedSps === sps ? '#111827' : 'var(--border-strong)'}`,
                  boxShadow: 'none'
                }}
              >
                {sps}
              </button>
            ))}
          </div>
        </div>

        {/* Quick Jumps */}
        <div>
          <div style={{ fontSize: '11px', color: 'var(--text-secondary)', marginBottom: '6px' }}>Quick Travel Jumps:</div>
          <div style={{ display: 'flex', gap: '6px', flexWrap: 'wrap' }}>
            {[-200, -50, -10, 10, 50, 200].map((steps) => (
              <button
                key={`qx-${steps}`}
                disabled={isJogDisabled}
                onClick={() => runJog('x', steps)}
                style={{ fontSize: '10px', padding: '3px 8px' }}
              >
                X {steps > 0 ? `+${steps}` : steps}
              </button>
            ))}
            {[-200, -50, -10, 10, 50, 200].map((steps) => (
              <button
                key={`qy-${steps}`}
                disabled={isJogDisabled}
                onClick={() => runJog('y', steps)}
                style={{ fontSize: '10px', padding: '3px 8px' }}
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
              marginTop: '14px',
              padding: '8px 12px',
              background: '#FEF2F2',
              border: '1px solid #FCA5A5',
              borderRadius: '4px',
              color: '#991B1B',
              fontSize: '11px'
            }}
          >
            {errorMsg}
          </div>
        )}
      </div>
    </div>
  );
};
