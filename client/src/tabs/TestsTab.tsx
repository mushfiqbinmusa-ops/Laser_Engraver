import React, { useState, useRef, useEffect } from 'react';
import { useMachine } from '../context/MachineContext';
import { LASER_MAX_DUTY, powerPctToDuty } from '../utils/laser';

export const TestsTab: React.FC = () => {
  const { connected, fresh, snapshot, pulse, setDriver, jog, refreshStatus } = useMachine();

  // Diagnostics State
  const [pingLatency, setPingLatency] = useState<number | null>(null);
  const [pinging, setPinging] = useState(false);
  const [queryingStatus, setQueryingStatus] = useState(false);
  const [statusReportText, setStatusReportText] = useState<string | null>(null);
  const [showStatusDetails, setShowStatusDetails] = useState(false);
  const [diagnosticMsg, setDiagnosticMsg] = useState<string | null>(null);

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
  const pulseDuty = powerPctToDuty(pulsePowerPct);

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
      const res = await fetch('/api/machine/status/report');
      const elapsed = Math.round(performance.now() - start);
      if (res.ok) {
        const data = await res.json();
        setStatusReportText(data.report);
        setShowStatusDetails(true);
        setDiagnosticMsg(`Consolidated status report retrieved in ${elapsed} ms.`);
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

  const handleRunMotorTest = async () => {
    setTestingMotors(true);
    setMotorTestResult(null);
    try {
      // Ensure drivers are energized first
      if (!snapshot?.drivers_enabled) {
        setMotorTestResult({ text: 'Energizing motor drivers before stepping...', isError: false });
        await setDriver(true);
        await new Promise((r) => setTimeout(r, 200));
      }

      const stepDurationMs = Math.round((testSteps / testSpeedSps) * 1000) + 120;

      if (testMode === 'wiggle') {
        setMotorTestResult({ text: `Testing ${testAxis.toUpperCase()} axis: Stepping +${testSteps} steps...`, isError: false });
        if (testAxis === 'x') {
          await jog('x', testSteps, testSpeedSps);
          await new Promise((r) => setTimeout(r, stepDurationMs));
        } else if (testAxis === 'y') {
          await jog('y', testSteps, testSpeedSps);
          await new Promise((r) => setTimeout(r, stepDurationMs));
        } else {
          await jog('x', testSteps, testSpeedSps);
          await new Promise((r) => setTimeout(r, stepDurationMs));
          await jog('y', testSteps, testSpeedSps);
          await new Promise((r) => setTimeout(r, stepDurationMs));
        }

        await new Promise((r) => setTimeout(r, 200));

        setMotorTestResult({ text: `Testing ${testAxis.toUpperCase()} axis: Stepping -${testSteps} steps (returning)...`, isError: false });
        if (testAxis === 'x') {
          await jog('x', -testSteps, testSpeedSps);
          await new Promise((r) => setTimeout(r, stepDurationMs));
        } else if (testAxis === 'y') {
          await jog('y', -testSteps, testSpeedSps);
          await new Promise((r) => setTimeout(r, stepDurationMs));
        } else {
          await jog('x', -testSteps, testSpeedSps);
          await new Promise((r) => setTimeout(r, stepDurationMs));
          await jog('y', -testSteps, testSpeedSps);
          await new Promise((r) => setTimeout(r, stepDurationMs));
        }

        setMotorTestResult({
          text: `PASS: ${testAxis.toUpperCase()} axis completed ±${testSteps} step wiggle test at ${testSpeedSps} sps.`,
          isError: false
        });
      } else if (testMode === 'forward') {
        const delta = testSteps;
        setMotorTestResult({ text: `Stepping ${testAxis.toUpperCase()} axis +${delta} steps...`, isError: false });
        if (testAxis === 'x') {
          await jog('x', delta, testSpeedSps);
          await new Promise((r) => setTimeout(r, stepDurationMs));
        } else if (testAxis === 'y') {
          await jog('y', delta, testSpeedSps);
          await new Promise((r) => setTimeout(r, stepDurationMs));
        } else {
          await jog('x', delta, testSpeedSps);
          await new Promise((r) => setTimeout(r, stepDurationMs));
          await jog('y', delta, testSpeedSps);
          await new Promise((r) => setTimeout(r, stepDurationMs));
        }
        setMotorTestResult({
          text: `PASS: Stepped ${testAxis.toUpperCase()} +${delta} steps successfully.`,
          isError: false
        });
      } else {
        const delta = -testSteps;
        setMotorTestResult({ text: `Stepping ${testAxis.toUpperCase()} axis ${delta} steps...`, isError: false });
        if (testAxis === 'x') {
          await jog('x', delta, testSpeedSps);
          await new Promise((r) => setTimeout(r, stepDurationMs));
        } else if (testAxis === 'y') {
          await jog('y', delta, testSpeedSps);
          await new Promise((r) => setTimeout(r, stepDurationMs));
        } else {
          await jog('x', delta, testSpeedSps);
          await new Promise((r) => setTimeout(r, stepDurationMs));
          await jog('y', delta, testSpeedSps);
          await new Promise((r) => setTimeout(r, stepDurationMs));
        }
        setMotorTestResult({
          text: `PASS: Stepped ${testAxis.toUpperCase()} ${delta} steps successfully.`,
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

  const isMachineFaulted: boolean = Boolean(snapshot?.state === 'faulted' || (snapshot?.fault && snapshot.fault !== 'none'));
  const canFirePulse: boolean = Boolean(connected && fresh && !isMachineFaulted && safetyConfirmed && !pulsing);
  const isSensorXTripped = snapshot ? snapshot.sensors.x_filtered === 1 : false;
  const isSensorYTripped = snapshot ? snapshot.sensors.y_filtered === 1 : false;

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
      setPulseSuccess(`Pulse fired: ${pulseDurationMs}ms at ${pulsePowerPct}% power (${pulseDuty} duty)`);
      pulseTimerRef.current = window.setTimeout(() => setPulseSuccess(null), 3500);
    } catch (e: any) {
      setPulseError(e.message || 'Laser pulse rejected by safety supervisor');
    } finally {
      setPulsing(false);
    }
  };

  return (
    <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '20px', maxWidth: '1280px' }}>
      {/* Left Column: Diagnostics, Driver Control & Stepper Motion Tests */}
      <div style={{ display: 'flex', flexDirection: 'column', gap: '20px' }}>
        
        {/* System Health & Diagnostics Panel */}
        <section
          style={{
            background: 'var(--bg-surface)',
            border: '1px solid var(--border-subtle)',
            borderRadius: '6px',
            padding: '20px',
            boxShadow: 'var(--shadow-subtle)'
          }}
        >
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '14px' }}>
            <h2 style={{ fontSize: '14px', fontWeight: 600, color: 'var(--text-primary)' }}>Communication Diagnostics</h2>
            <span style={{
              fontSize: '11px',
              padding: '2px 8px',
              borderRadius: '4px',
              fontWeight: 600,
              background: 'var(--bg-surface-elevated)',
              color: connected ? 'var(--text-primary)' : 'var(--text-muted)',
              border: '1px solid var(--border-subtle)',
              display: 'inline-flex',
              alignItems: 'center',
              gap: '6px'
            }}>
              <span style={{
                width: '6px',
                height: '6px',
                borderRadius: '50%',
                background: connected ? '#111827' : '#D1D5DB'
              }} />
              {connected ? 'ESP32 ONLINE' : 'ESP32 OFFLINE'}
            </span>
          </div>

          <div style={{ display: 'flex', gap: '8px', alignItems: 'center', marginBottom: '12px', flexWrap: 'wrap' }}>
            <button
              onClick={handlePing}
              disabled={pinging || !connected}
              style={{ fontWeight: 600, padding: '7px 12px', fontSize: '11px' }}
            >
              {pinging ? 'Pinging...' : 'Send Ping'}
            </button>

            <button
              onClick={handleQueryStatus}
              disabled={queryingStatus || !connected}
              style={{ fontWeight: 600, padding: '7px 12px', fontSize: '11px' }}
            >
              {queryingStatus ? 'Querying...' : 'Get Status Report'}
            </button>

            {pingLatency !== null && (
              <span className="mono" style={{ fontSize: '11px', color: 'var(--text-secondary)', marginLeft: '4px' }}>
                Latency: <strong style={{ color: 'var(--text-primary)' }}>{pingLatency} ms</strong>
              </span>
            )}
          </div>

          {diagnosticMsg && (
            <div
              className="mono"
              style={{
                fontSize: '11px',
                padding: '8px 12px',
                background: 'var(--bg-surface-elevated)',
                border: '1px solid var(--border-subtle)',
                borderRadius: '4px',
                color: 'var(--text-secondary)',
                marginBottom: '10px'
              }}
            >
              {diagnosticMsg}
            </div>
          )}

          {/* Consolidated Status Report */}
          {statusReportText && (
            <div style={{ marginTop: '12px' }}>
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '6px' }}>
                <span style={{ fontSize: '11px', fontWeight: 600, color: 'var(--text-secondary)', textTransform: 'uppercase', letterSpacing: '0.03em' }}>
                  Fixture Status Report
                </span>
                <button
                  type="button"
                  onClick={() => setShowStatusDetails(!showStatusDetails)}
                  style={{ fontSize: '11px', padding: '2px 8px' }}
                >
                  {showStatusDetails ? 'Hide Report ▲' : 'Show Report ▼'}
                </button>
              </div>

              {showStatusDetails && (
                <div style={{
                  padding: '12px',
                  background: 'var(--bg-surface-elevated)',
                  borderRadius: '4px',
                  border: '1px solid var(--border-subtle)',
                  overflowX: 'auto'
                }}>
                  <pre
                    className="mono"
                    style={{
                      margin: 0,
                      fontSize: '11px',
                      lineHeight: '1.4',
                      color: 'var(--text-primary)',
                      whiteSpace: 'pre'
                    }}
                  >
                    {statusReportText}
                  </pre>
                </div>
              )}
            </div>
          )}
        </section>

        {/* Stepper Motor Movement Test Panel */}
        <section
          style={{
            background: 'var(--bg-surface)',
            border: '1px solid var(--border-subtle)',
            borderRadius: '6px',
            padding: '20px',
            boxShadow: 'var(--shadow-subtle)'
          }}
        >
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '10px' }}>
            <h2 style={{ fontSize: '14px', fontWeight: 600, color: 'var(--text-primary)' }}>Stepper Motion Diagnostic Test</h2>
            <span style={{ fontSize: '11px', color: 'var(--text-muted)' }}>Non-Destructive Jog</span>
          </div>

          <p style={{ fontSize: '12px', color: 'var(--text-secondary)', marginBottom: '14px', lineHeight: 1.5 }}>
            Verify step pulse timing and direction signals on Motor A (X) and Motor B (Y).
          </p>

          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '12px', marginBottom: '12px' }}>
            {/* Target Axis */}
            <div>
              <label htmlFor="test-axis" style={{ display: 'block', fontSize: '11px', fontWeight: 500, color: 'var(--text-secondary)', marginBottom: '4px' }}>
                Target Axis
              </label>
              <select
                id="test-axis"
                value={testAxis}
                onChange={(e) => setTestAxis(e.target.value as any)}
                style={{ width: '100%' }}
              >
                <option value="x">X-Axis (Motor A - GPIO 5/6)</option>
                <option value="y">Y-Axis (Motor B - GPIO 9/10)</option>
                <option value="both">Both Axes (XY Diagonal)</option>
              </select>
            </div>

            {/* Test Mode */}
            <div>
              <label htmlFor="test-mode" style={{ display: 'block', fontSize: '11px', fontWeight: 500, color: 'var(--text-secondary)', marginBottom: '4px' }}>
                Motion Pattern
              </label>
              <select
                id="test-mode"
                value={testMode}
                onChange={(e) => setTestMode(e.target.value as any)}
                style={{ width: '100%' }}
              >
                <option value="wiggle">Wiggle Test (±Steps & Return)</option>
                <option value="forward">Single Step Forward (+Steps)</option>
                <option value="backward">Single Step Backward (-Steps)</option>
              </select>
            </div>
          </div>

          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '12px', marginBottom: '16px' }}>
            {/* Step Distance */}
            <div>
              <label htmlFor="test-steps" style={{ display: 'block', fontSize: '11px', fontWeight: 500, color: 'var(--text-secondary)', marginBottom: '4px' }}>
                Step Distance
              </label>
              <select
                id="test-steps"
                value={testSteps}
                onChange={(e) => setTestSteps(Number(e.target.value))}
                style={{ width: '100%' }}
              >
                <option value="20">20 µsteps (0.25 mm)</option>
                <option value="50">50 µsteps (0.625 mm)</option>
                <option value="100">100 µsteps (1.25 mm)</option>
                <option value="200">200 µsteps (2.5 mm)</option>
              </select>
            </div>

            {/* Speed */}
            <div>
              <label htmlFor="test-speed" style={{ display: 'block', fontSize: '11px', fontWeight: 500, color: 'var(--text-secondary)', marginBottom: '4px' }}>
                Speed Rate
              </label>
              <select
                id="test-speed"
                value={testSpeedSps}
                onChange={(e) => setTestSpeedSps(Number(e.target.value))}
                style={{ width: '100%' }}
              >
                <option value="50">50 sps (Slow / Smooth)</option>
                <option value="100">100 sps (Recommended)</option>
                <option value="150">150 sps (Max 28BYJ-48)</option>
              </select>
            </div>
          </div>

          <button
            onClick={handleRunMotorTest}
            disabled={testingMotors || !connected || isMachineFaulted}
            style={{
              width: '100%',
              padding: '9px',
              fontWeight: 600,
              fontSize: '12px',
              background: '#111827',
              color: '#FFFFFF',
              border: '1px solid #111827'
            }}
          >
            {testingMotors ? 'Running Stepper Test...' : `Run ${testAxis.toUpperCase()} Motor Test`}
          </button>

          {motorTestResult && (
            <div
              style={{
                marginTop: '12px',
                padding: '8px 12px',
                borderRadius: '4px',
                fontSize: '11px',
                background: motorTestResult.isError ? '#FEF2F2' : 'var(--bg-surface-elevated)',
                color: motorTestResult.isError ? '#991B1B' : 'var(--text-primary)',
                border: `1px solid ${motorTestResult.isError ? '#FCA5A5' : 'var(--border-subtle)'}`
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
            padding: '20px',
            boxShadow: 'var(--shadow-subtle)'
          }}
        >
          <h2 style={{ fontSize: '14px', fontWeight: 600, color: 'var(--text-primary)', marginBottom: '10px' }}>
            Optical Limit Sensors (FC-51)
          </h2>
          <p style={{ fontSize: '12px', color: 'var(--text-secondary)', marginBottom: '14px', lineHeight: 1.5 }}>
            Dual optical interrupters with 3-sample digital debounce filtering. Active LOW (0) when beam is interrupted.
          </p>

          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '12px' }}>
            {/* Sensor X */}
            <div
              style={{
                background: 'var(--bg-surface-elevated)',
                border: '1px solid var(--border-subtle)',
                borderRadius: '4px',
                padding: '12px'
              }}
            >
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '8px' }}>
                <span style={{ fontSize: '12px', fontWeight: 600, color: 'var(--text-primary)' }}>Axis X Sensor</span>
                <span className="mono" style={{ fontSize: '10px', color: 'var(--text-muted)' }}>GPIO 13</span>
              </div>
              <div
                className="mono"
                style={{
                  display: 'inline-block',
                  padding: '2px 6px',
                  borderRadius: '3px',
                  fontSize: '11px',
                  fontWeight: 600,
                  background: isSensorXTripped ? '#FEF2F2' : '#FFFFFF',
                  color: isSensorXTripped ? '#991B1B' : 'var(--text-secondary)',
                  border: `1px solid ${isSensorXTripped ? '#FCA5A5' : 'var(--border-strong)'}`
                }}
              >
                {snapshot ? (isSensorXTripped ? 'BLOCKED / DETECTED (0)' : 'CLEAR / OPEN (1)') : 'UNKNOWN'}
              </div>
            </div>

            {/* Sensor Y */}
            <div
              style={{
                background: 'var(--bg-surface-elevated)',
                border: '1px solid var(--border-subtle)',
                borderRadius: '4px',
                padding: '12px'
              }}
            >
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '8px' }}>
                <span style={{ fontSize: '12px', fontWeight: 600, color: 'var(--text-primary)' }}>Axis Y Sensor</span>
                <span className="mono" style={{ fontSize: '10px', color: 'var(--text-muted)' }}>GPIO 8</span>
              </div>
              <div
                className="mono"
                style={{
                  display: 'inline-block',
                  padding: '2px 6px',
                  borderRadius: '3px',
                  fontSize: '11px',
                  fontWeight: 600,
                  background: isSensorYTripped ? '#FEF2F2' : '#FFFFFF',
                  color: isSensorYTripped ? '#991B1B' : 'var(--text-secondary)',
                  border: `1px solid ${isSensorYTripped ? '#FCA5A5' : 'var(--border-strong)'}`
                }}
              >
                {snapshot ? (isSensorYTripped ? 'BLOCKED / DETECTED (0)' : 'CLEAR / OPEN (1)') : 'UNKNOWN'}
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
            padding: '20px',
            boxShadow: 'var(--shadow-subtle)'
          }}
        >
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '14px' }}>
            <h2 style={{ fontSize: '14px', fontWeight: 600, color: 'var(--text-primary)' }}>Laser Pulse Test Station</h2>
            <span
              className="mono"
              style={{
                fontSize: '10px',
                fontWeight: 600,
                padding: '2px 6px',
                borderRadius: '3px',
                background: 'var(--bg-surface-elevated)',
                color: 'var(--text-secondary)',
                border: '1px solid var(--border-subtle)',
                textTransform: 'uppercase',
                letterSpacing: '0.04em'
              }}
            >
              Class 4 Interlock
            </span>
          </div>

          <div
            style={{
              background: 'var(--bg-surface-elevated)',
              border: '1px solid var(--border-subtle)',
              borderRadius: '4px',
              padding: '12px',
              marginBottom: '16px'
            }}
          >
            <div style={{ fontSize: '11px', fontWeight: 600, color: 'var(--text-secondary)', textTransform: 'uppercase', letterSpacing: '0.03em', marginBottom: '6px' }}>
              Hardware Safety Rules
            </div>
            <ul style={{ fontSize: '11px', color: 'var(--text-secondary)', paddingLeft: '18px', margin: 0, lineHeight: 1.5 }}>
              <li>GPIO 12 Laser PWM is clamped to ≤ 90% (3,685 / 4,095 duty).</li>
              <li>Firmware-enforced 500ms watchdog automatically turns off output if unrefreshed.</li>
              <li>Pulse operations are bounded to ≤ 500ms maximum duration.</li>
              <li>Emergency stop immediately cuts timer channels and sets pin LOW.</li>
            </ul>
          </div>

          {/* Safety Interlock Checkbox */}
          <div
            style={{
              display: 'flex',
              alignItems: 'flex-start',
              gap: '10px',
              padding: '12px',
              background: safetyConfirmed ? 'var(--bg-surface)' : 'var(--bg-surface-elevated)',
              border: `1px solid ${safetyConfirmed ? '#111827' : 'var(--border-subtle)'}`,
              borderRadius: '4px',
              marginBottom: '18px',
              transition: 'border-color 0.15s ease'
            }}
          >
            <input
              type="checkbox"
              id="safety-interlock"
              checked={safetyConfirmed}
              onChange={(e) => setSafetyConfirmed(e.target.checked)}
              style={{ marginTop: '3px' }}
            />
            <label htmlFor="safety-interlock" style={{ fontSize: '12px', cursor: 'pointer', userSelect: 'none', lineHeight: 1.4 }}>
              <strong>Safety Interlock Acknowledged:</strong> Protective eyewear is donned by all personnel in the work area, beam path is shielded, and optical enclosure is clear of reflective hazards.
            </label>
          </div>

          {/* Laser Pulse Parameters */}
          <div style={{ display: 'flex', flexDirection: 'column', gap: '16px', marginBottom: '20px' }}>
            <div>
              <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: '6px' }}>
                <label htmlFor="pulse-power" style={{ fontSize: '12px', fontWeight: 600, color: 'var(--text-primary)' }}>
                  Laser Power: {pulsePowerPct}%
                </label>
                <span className="mono" style={{ fontSize: '11px', color: 'var(--text-muted)' }}>
                  PWM: {pulseDuty} / {LASER_MAX_DUTY}
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
              <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: '10px', color: 'var(--text-muted)', marginTop: '2px' }}>
                <span>1% (Visible Beam)</span>
                <span>45% (Test Mark)</span>
                <span>90% (Safety Ceiling)</span>
              </div>
            </div>

            <div>
              <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: '6px' }}>
                <label htmlFor="pulse-duration" style={{ fontSize: '12px', fontWeight: 600, color: 'var(--text-primary)' }}>
                  Pulse Duration: {pulseDurationMs} ms
                </label>
                <span className="mono" style={{ fontSize: '11px', color: 'var(--text-muted)' }}>
                  Max: 500 ms
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
              <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: '10px', color: 'var(--text-muted)', marginTop: '2px' }}>
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
              padding: '11px',
              fontWeight: 600,
              fontSize: '12px',
              background: canFirePulse ? '#111827' : 'var(--bg-surface-elevated)',
              color: canFirePulse ? '#FFFFFF' : 'var(--text-muted)',
              border: `1px solid ${canFirePulse ? '#111827' : 'var(--border-subtle)'}`,
              borderRadius: '4px',
              cursor: canFirePulse ? 'pointer' : 'not-allowed',
              transition: 'background 0.12s ease'
            }}
          >
            {pulsing
              ? 'EMITTING LASER PULSE...'
              : canFirePulse
                ? `FIRE ${pulseDurationMs}ms TEST PULSE (${pulsePowerPct}% POWER)`
                : 'SAFETY INTERLOCK REQUIRED TO FIRE'}
          </button>

          {/* Status Feedback */}
          {pulseSuccess && (
            <div
              role="status"
              style={{
                marginTop: '14px',
                padding: '8px 12px',
                background: 'var(--bg-surface-elevated)',
                border: '1px solid var(--border-subtle)',
                borderRadius: '4px',
                color: 'var(--text-primary)',
                fontSize: '11px',
                fontWeight: 500
              }}
            >
              ✓ {pulseSuccess}
            </div>
          )}

          {pulseError && (
            <div
              role="alert"
              style={{
                marginTop: '14px',
                padding: '8px 12px',
                background: '#FEF2F2',
                border: '1px solid #FCA5A5',
                borderRadius: '4px',
                color: '#991B1B',
                fontSize: '11px',
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
