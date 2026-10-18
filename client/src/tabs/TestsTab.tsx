import React, { useState, useRef, useEffect } from 'react';
import { useMachine } from '../context/MachineContext';

export const TestsTab: React.FC = () => {
  const { connected, fresh, snapshot, pulse, setDriver, jog, refreshStatus } = useMachine();

  // Diagnostics State
  const [pingLatency, setPingLatency] = useState<number | null>(null);
  const [pinging, setPinging] = useState(false);
  const [queryingStatus, setQueryingStatus] = useState(false);
  const [statusSnapshotData, setStatusSnapshotData] = useState<any | null>(null);
  const [showStatusDetails, setShowStatusDetails] = useState(false);
  const [diagnosticMsg, setDiagnosticMsg] = useState<string | null>(null);

  // Driver Energize State & Feedback
  const [driverFeedback, setDriverFeedback] = useState<{ text: string; isError: boolean } | null>(null);
  const [togglingDriver, setTogglingDriver] = useState(false);

  // Stepper Motor Test State
  const [testAxis, setTestAxis] = useState<'x' | 'y' | 'both'>('x');
  const [testMode, setTestMode] = useState<'wiggle' | 'forward' | 'backward'>('wiggle');
  const [testSteps, setTestSteps] = useState<number>(50);
  const [testSpeedSps, setTestSpeedSps] = useState<number>(100);
  const [testingMotors, setTestingMotors] = useState(false);
  const [motorTestResult, setMotorTestResult] = useState<{ text: string; isError: boolean } | null>(null);

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
        setDiagnosticMsg(`Ping acknowledged by ESP32 in ${elapsed} ms (code: OK)`);
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

  const handleQueryStatus = async () => {
    setQueryingStatus(true);
    setDiagnosticMsg(null);
    const start = performance.now();
    try {
      const res = await fetch('/api/machine/status');
      const elapsed = Math.round(performance.now() - start);
      if (res.ok) {
        const data = await res.json();
        setStatusSnapshotData(data);
        setShowStatusDetails(true);
        setDiagnosticMsg(`Status snapshot retrieved in ${elapsed} ms. State: ${data.snapshot?.state || 'unknown'}`);
      } else {
        const err = await res.json().catch(() => ({}));
        setDiagnosticMsg(`Status query failed: ${err.detail || res.statusText}`);
      }
    } catch (e: any) {
      setDiagnosticMsg(`Network error: ${e.message}`);
    } finally {
      setQueryingStatus(false);
      refreshStatus();
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
      await refreshStatus();
    } catch (e: any) {
      setDriverFeedback({
        text: `Driver control error: ${e.message}`,
        isError: true
      });
    } finally {
      setTogglingDriver(false);
    }
  };

  const handleRunMotorTest = async () => {
    setTestingMotors(true);
    setMotorTestResult(null);
    try {
      // 1. Ensure drivers are energized first
      if (!snapshot?.drivers_enabled) {
        setMotorTestResult({ text: 'Energizing motor drivers before stepping...', isError: false });
        await setDriver(true);
        await new Promise((r) => setTimeout(r, 200));
      }

      if (testMode === 'wiggle') {
        // Step forward then backward
        setMotorTestResult({ text: `Testing ${testAxis.toUpperCase()} axis: Stepping +${testSteps} steps...`, isError: false });
        if (testAxis === 'x' || testAxis === 'both') await jog('x', testSteps, testSpeedSps);
        if (testAxis === 'y' || testAxis === 'both') await jog('y', testSteps, testSpeedSps);

        await new Promise((r) => setTimeout(r, 300));

        setMotorTestResult({ text: `Testing ${testAxis.toUpperCase()} axis: Stepping -${testSteps} steps (returning to origin)...`, isError: false });
        if (testAxis === 'x' || testAxis === 'both') await jog('x', -testSteps, testSpeedSps);
        if (testAxis === 'y' || testAxis === 'both') await jog('y', -testSteps, testSpeedSps);

        setMotorTestResult({
          text: `PASS: ${testAxis.toUpperCase()} axis completed ±${testSteps} step wiggle test at ${testSpeedSps} sps successfully!`,
          isError: false
        });
      } else if (testMode === 'forward') {
        const delta = testSteps;
        setMotorTestResult({ text: `Stepping ${testAxis.toUpperCase()} axis +${delta} steps...`, isError: false });
        if (testAxis === 'x' || testAxis === 'both') await jog('x', delta, testSpeedSps);
        if (testAxis === 'y' || testAxis === 'both') await jog('y', delta, testSpeedSps);
        setMotorTestResult({
          text: `PASS: Stepped ${testAxis.toUpperCase()} +${delta} steps successfully!`,
          isError: false
        });
      } else {
        const delta = -testSteps;
        setMotorTestResult({ text: `Stepping ${testAxis.toUpperCase()} axis ${delta} steps...`, isError: false });
        if (testAxis === 'x' || testAxis === 'both') await jog('x', delta, testSpeedSps);
        if (testAxis === 'y' || testAxis === 'both') await jog('y', delta, testSpeedSps);
        setMotorTestResult({
          text: `PASS: Stepped ${testAxis.toUpperCase()} ${delta} steps successfully!`,
          isError: false
        });
      }
    } catch (e: any) {
      setMotorTestResult({
        text: `Motor Test Failed: ${e.message}`,
        isError: true
      });
    } finally {
      setTestingMotors(false);
      refreshStatus();
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
    <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '24px', maxWidth: '1280px' }}>
      {/* Left Column: Diagnostics, Driver Control & Stepper Motion Tests */}
      <div style={{ display: 'flex', flexDirection: 'column', gap: '20px' }}>
        {/* System Health & Diagnostics Panel */}
        <section
          style={{
            background: 'var(--bg-surface)',
            border: '1px solid var(--border-subtle)',
            borderRadius: '6px',
            padding: '20px'
          }}
        >
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '14px' }}>
            <h2 style={{ fontSize: '15px', fontWeight: 600 }}>Communication & Controller Diagnostics</h2>
            <span style={{
              fontSize: '11px',
              padding: '2px 8px',
              borderRadius: '12px',
              fontWeight: 600,
              background: connected ? 'rgba(47, 158, 68, 0.2)' : 'rgba(224, 49, 49, 0.2)',
              color: connected ? 'var(--accent-ready)' : 'var(--accent-estop)'
            }}>
              {connected ? '● ESP32 ONLINE' : '○ ESP32 OFFLINE'}
            </span>
          </div>

          <div style={{ display: 'flex', gap: '10px', alignItems: 'center', marginBottom: '14px', flexWrap: 'wrap' }}>
            <button
              onClick={handlePing}
              disabled={pinging || !connected}
              style={{ fontWeight: 600, padding: '8px 14px' }}
            >
              {pinging ? 'Pinging...' : 'Send Ping (system.ping)'}
            </button>

            <button
              onClick={handleQueryStatus}
              disabled={queryingStatus || !connected}
              style={{ fontWeight: 600, padding: '8px 14px', background: 'var(--bg-surface-elevated)' }}
            >
              {queryingStatus ? 'Querying...' : 'Get Status (system.snapshot)'}
            </button>

            {pingLatency !== null && (
              <span style={{ fontFamily: 'var(--font-mono)', fontSize: '12px', color: 'var(--text-secondary)' }}>
                Ping: <strong style={{ color: 'var(--accent-ready)' }}>{pingLatency} ms</strong>
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
                color: 'var(--text-secondary)',
                marginBottom: '10px'
              }}
            >
              {diagnosticMsg}
            </div>
          )}

          {/* Expandable Live Snapshot Data */}
          {statusSnapshotData && (
            <div style={{ marginTop: '10px' }}>
              <button
                type="button"
                onClick={() => setShowStatusDetails(!showStatusDetails)}
                style={{ fontSize: '11px', padding: '4px 8px', background: 'transparent', border: '1px solid var(--border-subtle)' }}
              >
                {showStatusDetails ? 'Hide Detailed Telemetry ▲' : 'Show Detailed Telemetry ▼'}
              </button>

              {showStatusDetails && (
                <div style={{
                  marginTop: '8px',
                  padding: '10px',
                  background: 'var(--bg-app)',
                  borderRadius: '4px',
                  border: '1px solid var(--border-subtle)',
                  maxHeight: '180px',
                  overflowY: 'auto'
                }}>
                  <pre style={{ margin: 0, fontSize: '11px', fontFamily: 'var(--font-mono)', color: 'var(--text-primary)' }}>
                    {JSON.stringify(statusSnapshotData.snapshot || statusSnapshotData, null, 2)}
                  </pre>
                </div>
              )}
            </div>
          )}
        </section>

        {/* Stepper Driver Energize / De-energize Management */}
        <section
          style={{
            background: 'var(--bg-surface)',
            border: '1px solid var(--border-subtle)',
            borderRadius: '6px',
            padding: '20px'
          }}
        >
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '12px' }}>
            <h2 style={{ fontSize: '15px', fontWeight: 600 }}>A4988 Driver Power (Coil Energize)</h2>
            <span
              style={{
                fontFamily: 'var(--font-mono)',
                fontSize: '11px',
                padding: '3px 8px',
                borderRadius: '4px',
                fontWeight: 600,
                background: snapshot?.drivers_enabled ? 'rgba(47, 158, 68, 0.25)' : 'rgba(240, 140, 0, 0.2)',
                color: snapshot?.drivers_enabled ? 'var(--accent-ready)' : '#FFD43B',
                border: `1px solid ${snapshot?.drivers_enabled ? 'var(--accent-ready)' : 'rgba(240, 140, 0, 0.4)'}`
              }}
            >
              {snapshot?.drivers_enabled ? '● HOLDING TORQUE ACTIVE' : '○ DRIVERS DISABLED (FREE)'}
            </span>
          </div>

          <p style={{ fontSize: '12px', color: 'var(--text-secondary)', marginBottom: '14px' }}>
            Controls the A4988 ENABLE lines (active LOW). When energized, motor coils hold position with holding torque. When disabled, the carriage can be pushed by hand without mechanical resistance.
          </p>

          <div style={{ display: 'flex', gap: '12px', alignItems: 'center' }}>
            <button
              onClick={handleToggleDriver}
              disabled={!connected || isMachineFaulted || togglingDriver}
              style={{
                fontWeight: 600,
                padding: '10px 16px',
                background: snapshot?.drivers_enabled ? 'var(--bg-surface-elevated)' : 'var(--text-primary)',
                color: snapshot?.drivers_enabled ? 'var(--text-primary)' : 'var(--bg-app)',
                border: '1px solid var(--border-strong)'
              }}
            >
              {togglingDriver
                ? 'Updating Drivers...'
                : snapshot?.drivers_enabled
                  ? 'Disable Drivers (Free Carriage)'
                  : 'Energize Motors (Apply Holding Torque)'}
            </button>
          </div>

          {/* Feedback Popup / Banner for Driver Toggle */}
          {driverFeedback && (
            <div
              style={{
                marginTop: '12px',
                padding: '10px 14px',
                borderRadius: '4px',
                fontSize: '12px',
                fontWeight: 500,
                background: driverFeedback.isError ? 'rgba(224, 49, 49, 0.2)' : 'rgba(47, 158, 68, 0.2)',
                color: driverFeedback.isError ? '#FFA8A8' : '#B2F2BB',
                border: `1px solid ${driverFeedback.isError ? 'var(--accent-estop)' : 'var(--accent-ready)'}`
              }}
            >
              {driverFeedback.text}
            </div>
          )}
        </section>

        {/* Stepper Motor Movement Test Panel */}
        <section
          style={{
            background: 'var(--bg-surface)',
            border: '1px solid var(--border-subtle)',
            borderRadius: '6px',
            padding: '20px'
          }}
        >
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '12px' }}>
            <h2 style={{ fontSize: '15px', fontWeight: 600 }}>Stepper Motor Motion Test</h2>
            <span style={{ fontSize: '11px', color: 'var(--text-secondary)' }}>Diagnostic Jog</span>
          </div>

          <p style={{ fontSize: '12px', color: 'var(--text-secondary)', marginBottom: '14px' }}>
            Perform non-destructive movement tests to verify phase wiring, step pulse generation, and direction signals on Motor A (X) and Motor B (Y).
          </p>

          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '12px', marginBottom: '14px' }}>
            {/* Target Axis */}
            <div>
              <label htmlFor="test-axis" style={{ display: 'block', fontSize: '11px', color: 'var(--text-secondary)', marginBottom: '4px' }}>
                Target Axis
              </label>
              <select
                id="test-axis"
                value={testAxis}
                onChange={(e) => setTestAxis(e.target.value as any)}
                style={{ width: '100%', padding: '6px 8px', background: 'var(--bg-surface-elevated)', color: 'inherit', border: '1px solid var(--border-subtle)', borderRadius: '4px' }}
              >
                <option value="x">X-Axis (Motor A - GPIO 5/6)</option>
                <option value="y">Y-Axis (Motor B - GPIO 9/10)</option>
                <option value="both">Both Axes (Diagonal XY)</option>
              </select>
            </div>

            {/* Test Mode */}
            <div>
              <label htmlFor="test-mode" style={{ display: 'block', fontSize: '11px', color: 'var(--text-secondary)', marginBottom: '4px' }}>
                Test Motion Pattern
              </label>
              <select
                id="test-mode"
                value={testMode}
                onChange={(e) => setTestMode(e.target.value as any)}
                style={{ width: '100%', padding: '6px 8px', background: 'var(--bg-surface-elevated)', color: 'inherit', border: '1px solid var(--border-subtle)', borderRadius: '4px' }}
              >
                <option value="wiggle">Wiggle Test (±Steps & Return)</option>
                <option value="forward">Single Step Forward (+Steps)</option>
                <option value="backward">Single Step Backward (-Steps)</option>
              </select>
            </div>
          </div>

          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '12px', marginBottom: '14px' }}>
            {/* Step Distance */}
            <div>
              <label htmlFor="test-steps" style={{ display: 'block', fontSize: '11px', color: 'var(--text-secondary)', marginBottom: '4px' }}>
                Step Distance (Microsteps)
              </label>
              <select
                id="test-steps"
                value={testSteps}
                onChange={(e) => setTestSteps(Number(e.target.value))}
                style={{ width: '100%', padding: '6px 8px', background: 'var(--bg-surface-elevated)', color: 'inherit', border: '1px solid var(--border-subtle)', borderRadius: '4px' }}
              >
                <option value="20">20 µsteps (0.25 mm)</option>
                <option value="50">50 µsteps (0.625 mm)</option>
                <option value="100">100 µsteps (1.25 mm)</option>
                <option value="200">200 µsteps (2.5 mm)</option>
              </select>
            </div>

            {/* Speed */}
            <div>
              <label htmlFor="test-speed" style={{ display: 'block', fontSize: '11px', color: 'var(--text-secondary)', marginBottom: '4px' }}>
                Speed Rate
              </label>
              <select
                id="test-speed"
                value={testSpeedSps}
                onChange={(e) => setTestSpeedSps(Number(e.target.value))}
                style={{ width: '100%', padding: '6px 8px', background: 'var(--bg-surface-elevated)', color: 'inherit', border: '1px solid var(--border-subtle)', borderRadius: '4px' }}
              >
                <option value="50">50 sps (Slow / Smooth)</option>
                <option value="100">100 sps (Standard)</option>
                <option value="250">250 sps (Fast)</option>
              </select>
            </div>
          </div>

          <button
            onClick={handleRunMotorTest}
            disabled={testingMotors || !connected || isMachineFaulted}
            style={{ width: '100%', padding: '10px', fontWeight: 600 }}
          >
            {testingMotors ? 'Running Stepper Test...' : `Run ${testAxis.toUpperCase()} Motor Test`}
          </button>

          {motorTestResult && (
            <div
              style={{
                marginTop: '12px',
                padding: '10px 14px',
                borderRadius: '4px',
                fontSize: '12px',
                background: motorTestResult.isError ? 'rgba(224, 49, 49, 0.2)' : 'rgba(47, 158, 68, 0.2)',
                color: motorTestResult.isError ? '#FFA8A8' : '#B2F2BB',
                border: `1px solid ${motorTestResult.isError ? 'var(--accent-estop)' : 'var(--accent-ready)'}`
              }}
            >
              {motorTestResult.text}
            </div>
          )}
        </section>

        {/* Optical Limit Sensors (FC-51) */}
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
              marginBottom: '18px'
            }}
          >
            <div style={{ fontSize: '12px', color: 'var(--text-secondary)', lineHeight: 1.5, marginBottom: '8px' }}>
              <strong>Hardware Safety Architecture:</strong>
            </div>
            <ul style={{ fontSize: '12px', color: 'var(--text-secondary)', paddingLeft: '20px', margin: 0, lineHeight: 1.6 }}>
              <li>GPIO 12 Laser PWM is clamped to ≤ 90% (3,685 / 4,095 duty) in firmware logic.</li>
              <li>Firmware-enforced 500ms watchdog automatically turns off output if unrefreshed.</li>
              <li>Pulse operations are bounded to ≤ 500ms maximum duration.</li>
              <li>Emergency stop immediately forces low state and disables timer channels.</li>
            </ul>
          </div>

          {/* Safety Interlock Checkbox */}
          <div
            style={{
              display: 'flex',
              alignItems: 'flex-start',
              gap: '10px',
              padding: '12px',
              background: safetyConfirmed ? 'rgba(47, 158, 68, 0.1)' : 'var(--bg-app)',
              border: `1px solid ${safetyConfirmed ? 'var(--accent-ready)' : 'var(--border-subtle)'}`,
              borderRadius: '4px',
              marginBottom: '20px'
            }}
          >
            <input
              type="checkbox"
              id="safety-interlock"
              checked={safetyConfirmed}
              onChange={(e) => setSafetyConfirmed(e.target.checked)}
              style={{ marginTop: '3px' }}
            />
            <label htmlFor="safety-interlock" style={{ fontSize: '12px', cursor: 'pointer', userSelect: 'none' }}>
              <strong>Safety Interlock Acknowledged:</strong> Protective eyewear is donned by all personnel, the optical enclosure is secured, and the beam path is clear of reflective hazards.
            </label>
          </div>

          {/* Laser Pulse Parameters */}
          <div style={{ display: 'flex', flexDirection: 'column', gap: '16px', marginBottom: '24px' }}>
            <div>
              <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: '6px' }}>
                <label htmlFor="pulse-power" style={{ fontSize: '12px', fontWeight: 600 }}>Laser Power: {pulsePowerPct}%</label>
                <span style={{ fontFamily: 'var(--font-mono)', fontSize: '12px', color: 'var(--text-secondary)' }}>
                  PWM Duty: {pulseDuty} / 4095
                </span>
              </div>
              <input
                id="pulse-power"
                type="range"
                min="1"
                max="90"
                value={pulsePowerPct}
                onChange={(e) => setPulsePowerPct(Number(e.target.value))}
                style={{ width: '100%' }}
              />
              <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: '11px', color: 'var(--text-muted)' }}>
                <span>1% (Visible Beam)</span>
                <span>45% (Test Mark)</span>
                <span style={{ color: 'var(--accent-warning)' }}>90% (Hard Max)</span>
              </div>
            </div>

            <div>
              <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: '6px' }}>
                <label htmlFor="pulse-duration" style={{ fontSize: '12px', fontWeight: 600 }}>Pulse Duration: {pulseDurationMs} ms</label>
                <span style={{ fontFamily: 'var(--font-mono)', fontSize: '12px', color: 'var(--text-secondary)' }}>
                  Watchdog Ceiling: 500 ms
                </span>
              </div>
              <input
                id="pulse-duration"
                type="range"
                min="10"
                max="500"
                step="10"
                value={pulseDurationMs}
                onChange={(e) => setPulseDurationMs(Number(e.target.value))}
                style={{ width: '100%' }}
              />
              <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: '11px', color: 'var(--text-muted)' }}>
                <span>10 ms</span>
                <span>250 ms</span>
                <span>500 ms (Watchdog Cutoff)</span>
              </div>
            </div>
          </div>

          {/* Pulse Fire Action */}
          <button
            onClick={handleFirePulse}
            disabled={!canFirePulse}
            style={{
              width: '100%',
              padding: '12px',
              fontWeight: 700,
              fontSize: '13px',
              letterSpacing: '0.5px',
              background: canFirePulse ? 'var(--accent-estop)' : 'var(--bg-surface-elevated)',
              color: canFirePulse ? '#FFFFFF' : 'var(--text-muted)',
              border: `1px solid ${canFirePulse ? 'var(--accent-estop)' : 'var(--border-subtle)'}`,
              borderRadius: '4px',
              cursor: canFirePulse ? 'pointer' : 'not-allowed',
              transition: 'background 0.15s ease'
            }}
          >
            {pulsing ? 'EMITTING LASER RADIATION...' : canFirePulse ? `FIRE ${pulseDurationMs}ms TEST PULSE` : 'SAFETY INTERLOCK REQUIRED'}
          </button>

          {/* Status Feedback */}
          {pulseSuccess && (
            <div
              role="status"
              style={{
                marginTop: '16px',
                padding: '10px 14px',
                background: 'rgba(47, 158, 68, 0.15)',
                border: '1px solid var(--accent-ready)',
                borderRadius: '4px',
                color: 'var(--accent-ready)',
                fontSize: '12px',
                fontWeight: 500
              }}
            >
              {pulseSuccess}
            </div>
          )}

          {pulseError && (
            <div
              role="alert"
              style={{
                marginTop: '16px',
                padding: '10px 14px',
                background: 'rgba(224, 49, 49, 0.15)',
                border: '1px solid var(--accent-estop)',
                borderRadius: '4px',
                color: '#FFA8A8',
                fontSize: '12px',
                fontWeight: 500
              }}
            >
              {pulseError}
            </div>
          )}
        </section>
      </div>
    </div>
  );
};
