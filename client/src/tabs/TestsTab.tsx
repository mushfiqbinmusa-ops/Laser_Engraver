import React, { useState, useRef, useEffect } from 'react';
import { useMachine } from '../context/MachineContext';

export const TestsTab: React.FC = () => {
  const { connected, fresh, snapshot, pulse, setDriver, refreshStatus } = useMachine();

  // Diagnostics State
  const [pingLatency, setPingLatency] = useState<number | null>(null);
  const [pinging, setPinging] = useState(false);
  const [diagnosticMsg, setDiagnosticMsg] = useState<string | null>(null);

  // Laser Pulse State
  const [safetyConfirmed, setSafetyConfirmed] = useState(false);
  const [pulsePowerPct, setPulsePowerPct] = useState(10);
  const [pulseDurationMs, setPulseDurationMs] = useState(50);
  const [pulsing, setPulsing] = useState(false);
  const [pulseSuccess, setPulseSuccess] = useState<string | null>(null);
  const [pulseError, setPulseError] = useState<string | null>(null);
  const pulseTimerRef = useRef<number | null>(null);

  useEffect(() => {
    return () => {
      if (pulseTimerRef.current) {
        window.clearTimeout(pulseTimerRef.current);
      }
    };
  }, []);

  // Calculate duty (90% ceiling = 3685)
  const pulseDuty = Math.min(3685, Math.max(0, Math.round((pulsePowerPct / 90.0) * 3685)));

  const handlePing = async () => {
    setPinging(true);
    setDiagnosticMsg(null);
    const start = performance.now();
    try {
      const res = await fetch('/api/machine/ping', { method: 'POST' });
      const elapsed = Math.round(performance.now() - start);
      if (res.ok) {
        setPingLatency(elapsed);
        setDiagnosticMsg(`Ping acknowledged in ${elapsed} ms`);
      } else {
        const err = await res.json().catch(() => ({}));
        setDiagnosticMsg(`Ping failed: ${err.detail || res.statusText}`);
      }
    } catch (e: any) {
      setDiagnosticMsg(`Network error: ${e.message}`);
    } finally {
      setPinging(false);
      refreshStatus();
    }
  };

  const handleToggleDriver = async () => {
    if (!snapshot) return;
    try {
      await setDriver(!snapshot.drivers_enabled);
    } catch (e: any) {
      setDiagnosticMsg(`Driver toggle error: ${e.message}`);
    }
  };

  const isMachineFaulted: boolean = Boolean(snapshot?.state === 'Faulted' || (snapshot?.fault && snapshot.fault !== 'none'));
  const canFirePulse: boolean = Boolean(connected && fresh && !isMachineFaulted && safetyConfirmed && !pulsing);
  const isSensorXTripped = snapshot ? snapshot.sensors.x_filtered === 0 : false;
  const isSensorYTripped = snapshot ? snapshot.sensors.y_filtered === 0 : false;

  const handleFirePulse = async () => {
    if (!canFirePulse) return;
    setPulsing(true);
    setPulseSuccess(null);
    setPulseError(null);

    if (pulseTimerRef.current) {
      window.clearTimeout(pulseTimerRef.current);
    }

    try {
      await pulse(pulseDuty, pulseDurationMs);
      setPulseSuccess(`Fired ${pulseDurationMs}ms pulse at ${pulsePowerPct}% power (${pulseDuty} duty)`);
      pulseTimerRef.current = window.setTimeout(() => setPulseSuccess(null), 3000);
    } catch (e: any) {
      setPulseError(e.message || 'Laser pulse rejected by safety supervisor');
    } finally {
      setPulsing(false);
    }
  };

  return (
    <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '24px', maxWidth: '1200px' }}>
      {/* Left Column: Diagnostics, Sensors, & Drivers */}
      <div style={{ display: 'flex', flexDirection: 'column', gap: '20px' }}>
        {/* System Health Panel */}
        <section
          style={{
            background: 'var(--bg-surface)',
            border: '1px solid var(--border-subtle)',
            borderRadius: '6px',
            padding: '20px'
          }}
        >
          <h2 style={{ fontSize: '15px', fontWeight: 600, marginBottom: '14px' }}>Communication & Bridge Diagnostics</h2>
          <div style={{ display: 'flex', gap: '12px', alignItems: 'center', marginBottom: '14px' }}>
            <button
              onClick={handlePing}
              disabled={pinging || !connected}
              style={{ fontWeight: 500 }}
            >
              {pinging ? 'Pinging...' : 'Send Ping (system.ping)'}
            </button>
            {pingLatency !== null && (
              <span style={{ fontFamily: 'var(--font-mono)', fontSize: '13px', color: 'var(--text-secondary)' }}>
                Roundtrip: <strong style={{ color: 'var(--accent-ready)' }}>{pingLatency} ms</strong>
              </span>
            )}
          </div>
          {diagnosticMsg && (
            <div
              style={{
                fontFamily: 'var(--font-mono)',
                fontSize: '12px',
                padding: '8px 12px',
                background: 'var(--bg-app)',
                border: '1px solid var(--border-subtle)',
                borderRadius: '4px',
                color: 'var(--text-secondary)'
              }}
            >
              {diagnosticMsg}
            </div>
          )}
        </section>

        {/* Optical Endstop Sensors */}
        <section
          style={{
            background: 'var(--bg-surface)',
            border: '1px solid var(--border-subtle)',
            borderRadius: '6px',
            padding: '20px'
          }}
        >
          <h2 style={{ fontSize: '15px', fontWeight: 600, marginBottom: '12px' }}>Optical Limit Sensors (FC-51)</h2>
          <p style={{ fontSize: '12px', color: 'var(--text-secondary)', marginBottom: '16px' }}>
            Dual LM393 optical photo-interrupters with 3-sample/3ms digital debounce filtering. Active LOW (0) when beam is interrupted.
          </p>

          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '16px' }}>
            {/* Sensor X */}
            <div
              style={{
                background: 'var(--bg-app)',
                border: '1px solid var(--border-subtle)',
                borderRadius: '4px',
                padding: '14px'
              }}
            >
              <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: '8px' }}>
                <span style={{ fontWeight: 600 }}>Axis X Sensor</span>
                <span style={{ fontFamily: 'var(--font-mono)', fontSize: '11px', color: 'var(--text-muted)' }}>GPIO 13</span>
              </div>
              <div
                style={{
                  display: 'inline-block',
                  padding: '3px 8px',
                  borderRadius: '3px',
                  fontSize: '12px',
                  fontWeight: 600,
                  fontFamily: 'var(--font-mono)',
                  background: isSensorXTripped ? 'rgba(224, 49, 49, 0.2)' : 'rgba(47, 158, 68, 0.15)',
                  color: isSensorXTripped ? 'var(--accent-estop)' : 'var(--accent-ready)',
                  border: `1px solid ${isSensorXTripped ? 'var(--accent-estop)' : 'var(--accent-ready)'}`
                }}
              >
                {snapshot ? (isSensorXTripped ? 'TRIPPED / BLOCKED (0)' : 'CLEAR / OPEN (1)') : 'UNKNOWN'}
              </div>
            </div>

            {/* Sensor Y */}
            <div
              style={{
                background: 'var(--bg-app)',
                border: '1px solid var(--border-subtle)',
                borderRadius: '4px',
                padding: '14px'
              }}
            >
              <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: '8px' }}>
                <span style={{ fontWeight: 600 }}>Axis Y Sensor</span>
                <span style={{ fontFamily: 'var(--font-mono)', fontSize: '11px', color: 'var(--text-muted)' }}>GPIO 8</span>
              </div>
              <div
                style={{
                  display: 'inline-block',
                  padding: '3px 8px',
                  borderRadius: '3px',
                  fontSize: '12px',
                  fontWeight: 600,
                  fontFamily: 'var(--font-mono)',
                  background: isSensorYTripped ? 'rgba(224, 49, 49, 0.2)' : 'rgba(47, 158, 68, 0.15)',
                  color: isSensorYTripped ? 'var(--accent-estop)' : 'var(--accent-ready)',
                  border: `1px solid ${isSensorYTripped ? 'var(--accent-estop)' : 'var(--accent-ready)'}`
                }}
              >
                {snapshot ? (isSensorYTripped ? 'TRIPPED / BLOCKED (0)' : 'CLEAR / OPEN (1)') : 'UNKNOWN'}
              </div>
            </div>
          </div>
        </section>

        {/* Stepper Driver Management */}
        <section
          style={{
            background: 'var(--bg-surface)',
            border: '1px solid var(--border-subtle)',
            borderRadius: '6px',
            padding: '20px'
          }}
        >
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '12px' }}>
            <h2 style={{ fontSize: '15px', fontWeight: 600 }}>A4988 Driver Power</h2>
            <span
              style={{
                fontFamily: 'var(--font-mono)',
                fontSize: '11px',
                padding: '2px 6px',
                borderRadius: '3px',
                background: snapshot?.drivers_enabled ? 'rgba(47, 158, 68, 0.2)' : 'rgba(107, 114, 128, 0.2)',
                color: snapshot?.drivers_enabled ? 'var(--accent-ready)' : 'var(--text-muted)'
              }}
            >
              {snapshot?.drivers_enabled ? 'HOLDING TORQUE ACTIVE' : 'DRIVERS DISABLED'}
            </span>
          </div>
          <p style={{ fontSize: '12px', color: 'var(--text-secondary)', marginBottom: '16px' }}>
            Controlling ENABLE lines (GPIO 11 and GPIO 4). Disabling drivers allows manual axis movement without resistance, but invalidates calibrated coordinates.
          </p>
          <button
            onClick={handleToggleDriver}
            disabled={!connected || isMachineFaulted}
            style={{ fontWeight: 500 }}
          >
            {snapshot?.drivers_enabled ? 'Disable Drivers (Free Carriage)' : 'Enable Drivers (Energize Coils)'}
          </button>
        </section>
      </div>

      {/* Right Column: Guarded Laser Station */}
      <div>
        <section
          style={{
            background: 'var(--bg-surface)',
            border: '1px solid var(--border-subtle)',
            borderRadius: '6px',
            padding: '20px'
          }}
        >
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '14px' }}>
            <h2 style={{ fontSize: '15px', fontWeight: 600 }}>Guarded Laser Pulse Station</h2>
            <span
              style={{
                fontFamily: 'var(--font-mono)',
                fontSize: '11px',
                padding: '2px 6px',
                borderRadius: '3px',
                background: 'rgba(224, 49, 49, 0.15)',
                color: 'var(--accent-estop)',
                border: '1px solid var(--accent-estop)'
              }}
            >
              CLASS 4 DIRECT RADIATION
            </span>
          </div>

          <div
            style={{
              background: 'var(--bg-app)',
              border: '1px solid var(--border-subtle)',
              borderRadius: '4px',
              padding: '14px',
              marginBottom: '20px'
            }}
          >
            <label style={{ display: 'flex', gap: '12px', alignItems: 'flex-start', cursor: 'pointer' }}>
              <input
                type="checkbox"
                checked={safetyConfirmed}
                onChange={(e) => setSafetyConfirmed(e.target.checked)}
                style={{ marginTop: '3px', cursor: 'pointer', width: '16px', height: '16px' }}
              />
              <span style={{ fontSize: '13px', lineHeight: 1.4, color: 'var(--text-primary)' }}>
                <strong>Safety Confirmation:</strong> I confirm that certified 450nm wavelength protective laser safety eyewear is worn by all persons in the room, the target material is non-reflective, and the workspace is supervised.
              </span>
            </label>
          </div>

          {/* Pulse Controls */}
          <div style={{ display: 'flex', flexDirection: 'column', gap: '18px', marginBottom: '24px' }}>
            {/* Laser Power */}
            <div>
              <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: '6px' }}>
                <label htmlFor="laser-power-slider" style={{ fontSize: '13px', fontWeight: 500 }}>
                  Laser Power: {pulsePowerPct}%
                </label>
                <span style={{ fontFamily: 'var(--font-mono)', fontSize: '12px', color: 'var(--text-secondary)' }}>
                  Duty: {pulseDuty} / 3685 (90% clamp)
                </span>
              </div>
              <input
                id="laser-power-slider"
                type="range"
                min={5}
                max={90}
                step={5}
                value={pulsePowerPct}
                onChange={(e) => setPulsePowerPct(Number(e.target.value))}
                style={{ width: '100%' }}
                aria-label={`Laser power percentage: ${pulsePowerPct}%`}
              />
            </div>

            {/* Pulse Duration */}
            <div>
              <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: '6px' }}>
                <span id="pulse-duration-label" style={{ fontSize: '13px', fontWeight: 500 }}>
                  Pulse Duration: {pulseDurationMs} ms
                </span>
                <span style={{ fontFamily: 'var(--font-mono)', fontSize: '12px', color: 'var(--text-secondary)' }}>
                  Max safe pulse: 100 ms
                </span>
              </div>
              <div
                role="group"
                aria-labelledby="pulse-duration-label"
                style={{ display: 'flex', gap: '8px' }}
              >
                {[10, 25, 50, 75, 100].map((dur) => (
                  <button
                    key={dur}
                    type="button"
                    role="button"
                    aria-pressed={pulseDurationMs === dur}
                    onClick={() => setPulseDurationMs(dur)}
                    style={{
                      flex: 1,
                      padding: '6px 0',
                      fontSize: '12px',
                      background: pulseDurationMs === dur ? 'var(--text-primary)' : 'var(--bg-surface-elevated)',
                      color: pulseDurationMs === dur ? 'var(--bg-app)' : 'var(--text-primary)',
                      border: '1px solid var(--border-subtle)'
                    }}
                  >
                    {dur} ms
                  </button>
                ))}
              </div>
            </div>
          </div>

          {/* Action Button */}
          <button
            onClick={handleFirePulse}
            disabled={!canFirePulse}
            aria-live="polite"
            style={{
              width: '100%',
              padding: '12px',
              fontSize: '14px',
              fontWeight: 600,
              background: canFirePulse ? 'var(--accent-estop)' : 'var(--bg-surface-elevated)',
              color: canFirePulse ? '#FFFFFF' : 'var(--text-muted)',
              border: `1px solid ${canFirePulse ? 'var(--accent-estop)' : 'var(--border-subtle)'}`,
              opacity: canFirePulse ? 1 : 0.6
            }}
          >
            {pulsing ? 'EMITTING LASER PULSE...' : `FIRE ${pulseDurationMs}ms TEST PULSE`}
          </button>

          {pulseSuccess && (
            <div
              role="status"
              aria-live="polite"
              style={{
                marginTop: '14px',
                padding: '10px 14px',
                borderRadius: '4px',
                background: 'rgba(47, 158, 68, 0.15)',
                color: 'var(--accent-ready)',
                border: '1px solid var(--accent-ready)',
                fontSize: '13px'
              }}
            >
              ✓ {pulseSuccess}
            </div>
          )}

          {pulseError && (
            <div
              role="alert"
              aria-live="assertive"
              style={{
                marginTop: '14px',
                padding: '10px 14px',
                borderRadius: '4px',
                background: 'rgba(224, 49, 49, 0.15)',
                color: 'var(--accent-estop)',
                border: '1px solid var(--accent-estop)',
                fontSize: '13px'
              }}
            >
              ✗ {pulseError}
            </div>
          )}

          <div style={{ marginTop: '20px', padding: '12px', background: 'var(--bg-app)', borderRadius: '4px', fontSize: '11px', color: 'var(--text-muted)', lineHeight: 1.5 }}>
            <strong>Safety Invariants:</strong> The ESP32 firmware LEDC pulse controller independently enforces a maximum 500ms pulse ceiling and a 500ms safety watchdog. Any pulse is immediately terminated if the connection drops or E-Stop is triggered.
          </div>
        </section>
      </div>
    </div>
  );
};
