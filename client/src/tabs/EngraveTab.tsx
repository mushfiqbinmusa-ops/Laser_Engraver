import React, { useState, useEffect } from 'react';
import { useMachine } from '../context/MachineContext';
import { Preset, Job } from '../types/jobs';

export const EngraveTab: React.FC = () => {
  const { connected, fresh, snapshot, pulse, home } = useMachine();
  const [presets, setPresets] = useState<Preset[]>([]);
  const [selectedPresetId, setSelectedPresetId] = useState<string>('preset-wood');
  const [overridePower, setOverridePower] = useState<number | ''>('');
  
  const [activeJob, setActiveJob] = useState<Job | null>(null);
  const [jobsHistory, setJobsHistory] = useState<Job[]>([]);
  const [isLoading, setIsLoading] = useState<boolean>(false);
  const [errorMsg, setErrorMsg] = useState<string | null>(null);

  // False Engraving (Dry Run) & Quick Homing State
  const [isDryRun, setIsDryRun] = useState<boolean>(false);
  const [isHoming, setIsHoming] = useState<boolean>(false);

  // Quick Laser Pulse Test State
  const [showPulseStation, setShowPulseStation] = useState<boolean>(false);
  const [pulsePowerPct, setPulsePowerPct] = useState<number>(10);
  const [pulseDurationMs, setPulseDurationMs] = useState<number>(50);
  const [safetyConfirmed, setSafetyConfirmed] = useState<boolean>(false);
  const [pulsing, setPulsing] = useState<boolean>(false);
  const [pulseSuccess, setPulseSuccess] = useState<string | null>(null);
  const [pulseError, setPulseError] = useState<string | null>(null);

  const pulseDuty = Math.min(3685, Math.max(0, Math.round((pulsePowerPct / 90.0) * 3685)));

  const handleQuickPulse = async () => {
    if (!safetyConfirmed) {
      setPulseError('Please confirm safety eyewear interlock before firing.');
      return;
    }
    setPulsing(true);
    setPulseSuccess(null);
    setPulseError(null);
    try {
      await pulse(pulseDuty, pulseDurationMs);
      setPulseSuccess(`Fired ${pulseDurationMs}ms pulse at ${pulsePowerPct}% power (${pulseDuty} duty)`);
      window.setTimeout(() => setPulseSuccess(null), 3500);
    } catch (err: any) {
      setPulseError(err.message || 'Laser pulse rejected');
    } finally {
      setPulsing(false);
    }
  };

  const handleQuickHome = async () => {
    setIsHoming(true);
    setErrorMsg(null);
    try {
      await home('all');
    } catch (err: any) {
      setErrorMsg(err.message || 'Homing failed');
    } finally {
      setIsHoming(false);
    }
  };

  // Parametric shape state
  const [shapeType, setShapeType] = useState<'square' | 'rectangle' | 'circle' | 'grid'>('square');
  const [shapeDimW, setShapeDimW] = useState<number>(20.0);
  const [shapeDimH, setShapeDimH] = useState<number>(15.0);

  const fetchPresetsAndJobs = async () => {
    try {
      const pRes = await fetch('/api/presets');
      if (pRes.ok) setPresets(await pRes.json());

      const jRes = await fetch('/api/jobs');
      if (jRes.ok) {
        const jList: Job[] = await jRes.json();
        setJobsHistory(jList);
        if (jList.length > 0) {
          setActiveJob((current) => current ?? jList[0]);
        }
      }
    } catch (e: any) {
      setErrorMsg(e.message);
    }
  };

  useEffect(() => {
    fetchPresetsAndJobs();
  }, []);

  const handleFileUpload = async (e: React.ChangeEvent<HTMLInputElement>) => {
    if (!e.target.files || e.target.files.length === 0) return;
    const file = e.target.files[0];
    setIsLoading(true);
    setErrorMsg(null);

    const formData = new FormData();
    formData.append('svg_file', file);
    formData.append('preset_id', selectedPresetId);
    if (overridePower !== '') {
      formData.append('override_laser_power_pct', overridePower.toString());
    }

    try {
      const res = await fetch('/api/jobs/upload', {
        method: 'POST',
        body: formData
      });
      if (!res.ok) {
        const err = await res.json();
        throw new Error(err.detail || 'Failed to upload and prepare SVG');
      }
      const newJob: Job = await res.json();
      setActiveJob(newJob);
      fetchPresetsAndJobs();
    } catch (err: any) {
      setErrorMsg(err.message);
    } finally {
      setIsLoading(false);
    }
  };

  const handleCreateShape = async () => {
    setIsLoading(true);
    setErrorMsg(null);
    try {
      const payload: any = {
        shape_type: shapeType,
        preset_id: selectedPresetId,
        width_mm: shapeDimW,
        height_mm: shapeDimH,
        radius_mm: shapeDimW / 2.0
      };
      if (overridePower !== '') {
        payload.override_laser_power_pct = Number(overridePower);
      }
      const res = await fetch('/api/jobs/shape', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
      });
      if (!res.ok) {
        const err = await res.json();
        throw new Error(err.detail || 'Failed to generate shape');
      }
      const newJob: Job = await res.json();
      setActiveJob(newJob);
      fetchPresetsAndJobs();
    } catch (err: any) {
      setErrorMsg(err.message);
    } finally {
      setIsLoading(false);
    }
  };

  const handleStartJob = async () => {
    if (!activeJob) return;
    setIsLoading(true);
    setErrorMsg(null);
    try {
      const res = await fetch(`/api/jobs/${activeJob.id}/start?dry_run=${isDryRun}`, { method: 'POST' });
      if (!res.ok) {
        const err = await res.json();
        throw new Error(err.detail || 'Failed to start job');
      }
      fetchPresetsAndJobs();
    } catch (err: any) {
      setErrorMsg(err.message);
    } finally {
      setIsLoading(false);
    }
  };

  const handleCancelJob = async () => {
    if (!activeJob) return;
    await fetch(`/api/jobs/${activeJob.id}/cancel`, { method: 'POST' });
    fetchPresetsAndJobs();
  };

  // Readiness Checklist
  const isHomed = !!(snapshot?.homed.x && snapshot?.homed.y);
  const noFaults = !snapshot?.fault;
  const isReady = connected && fresh && isHomed && noFaults && !!activeJob;

  return (
    <div style={{ display: 'grid', gridTemplateColumns: '320px 1fr 300px', gap: '20px', padding: '24px' }}>
      {/* Left Pane: Vector Source & Preset Selection */}
      <div style={{ display: 'flex', flexDirection: 'column', gap: '16px' }}>
        <div style={{ background: 'var(--bg-surface)', border: '1px solid var(--border-subtle)', borderRadius: '6px', padding: '16px' }}>
          <h2 style={{ fontSize: '14px', fontWeight: 600, marginBottom: '12px' }}>Vector Input Source</h2>
          
          <label style={{ display: 'block', fontSize: '12px', color: 'var(--text-secondary)', marginBottom: '6px' }}>
            Upload SVG File
          </label>
          <input
            type="file"
            accept=".svg"
            onChange={handleFileUpload}
            disabled={isLoading}
            style={{ width: '100%', marginBottom: '16px' }}
          />

          <hr style={{ borderColor: 'var(--border-subtle)', margin: '12px 0' }} />

          <h3 style={{ fontSize: '13px', fontWeight: 600, marginBottom: '8px' }}>Or Built-In Parametric Shape</h3>
          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '8px', marginBottom: '8px' }}>
            <select
              id="parametric-shape-type"
              aria-label="Select parametric shape type"
              value={shapeType}
              onChange={(e: any) => setShapeType(e.target.value)}
            >
              <option value="square">Square</option>
              <option value="rectangle">Rectangle</option>
              <option value="circle">Circle</option>
              <option value="grid">Grid (5x5)</option>
            </select>
            <input
              id="parametric-shape-width"
              aria-label="Shape width in millimeters"
              type="number"
              value={shapeDimW}
              onChange={(e) => setShapeDimW(Number(e.target.value))}
              placeholder="Width mm"
              step="1"
              min="2"
              max="45"
            />
          </div>
          {(shapeType === 'rectangle' || shapeType === 'grid') && (
            <div style={{ marginBottom: '8px' }}>
              <input
                id="parametric-shape-height"
                aria-label="Shape height in millimeters"
                type="number"
                value={shapeDimH}
                onChange={(e) => setShapeDimH(Number(e.target.value))}
                placeholder="Height mm"
                step="1"
                min="2"
                max="45"
                style={{ width: '100%' }}
              />
            </div>
          )}
          <button onClick={handleCreateShape} disabled={isLoading} style={{ width: '100%' }}>
            Generate Vector Shape
          </button>
        </div>

        {/* Preset Configuration */}
        <div style={{ background: 'var(--bg-surface)', border: '1px solid var(--border-subtle)', borderRadius: '6px', padding: '16px' }}>
          <label htmlFor="engrave-preset-select" style={{ display: 'block', fontSize: '14px', fontWeight: 600, marginBottom: '12px' }}>
            Process Preset
          </label>
          <select
            id="engrave-preset-select"
            aria-label="Select process preset"
            value={selectedPresetId}
            onChange={(e) => setSelectedPresetId(e.target.value)}
            style={{ width: '100%', marginBottom: '12px' }}
          >
            {presets.map((p) => (
              <option key={p.id} value={p.id}>
                {p.name} ({p.laser_power_pct}% power, {p.engrave_speed_sps} sps)
              </option>
            ))}
          </select>

          <label htmlFor="power-override-input" style={{ display: 'block', fontSize: '12px', color: 'var(--text-secondary)', marginBottom: '4px' }}>
            Temporary Job Power Override (0–90%)
          </label>
          <input
            id="power-override-input"
            aria-label="Temporary job power override percentage"
            type="number"
            min="0"
            max="90"
            value={overridePower}
            onChange={(e) => setOverridePower(e.target.value === '' ? '' : Number(e.target.value))}
            placeholder="Preset default"
            style={{ width: '100%' }}
          />
        </div>
      </div>

      {/* Center Pane: High-Fidelity Toolpath Preview */}
      <div style={{ background: 'var(--bg-surface)', border: '1px solid var(--border-subtle)', borderRadius: '6px', padding: '16px', display: 'flex', flexDirection: 'column' }}>
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '12px' }}>
          <h2 style={{ fontSize: '14px', fontWeight: 600 }}>Toolpath Preview & Work-Area Fit</h2>
          {activeJob && (
            <span className="mono" style={{ fontSize: '12px', color: 'var(--text-secondary)' }}>
              {activeJob.name} ({activeJob.total_segments} segments)
            </span>
          )}
        </div>

        <div style={{
          flex: 1,
          minHeight: '420px',
          background: 'var(--bg-app)',
          border: '1px solid var(--border-subtle)',
          borderRadius: '4px',
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center'
        }}>
          {activeJob && activeJob.has_preview ? (
            <img
              src={`/api/jobs/${activeJob.id}/preview?t=${activeJob.updated_at}`}
              alt="Toolpath preview"
              style={{ maxWidth: '100%', maxHeight: '450px', objectFit: 'contain' }}
            />
          ) : (
            <span style={{ color: 'var(--text-muted)' }}>Upload an SVG or generate a shape to preview toolpath</span>
          )}
        </div>

        {errorMsg && (
          <div style={{ marginTop: '12px', padding: '8px 12px', background: 'rgba(224, 49, 49, 0.15)', border: '1px solid var(--accent-estop)', borderRadius: '4px', color: '#FFA8A8', fontSize: '12px' }}>
            {errorMsg}
          </div>
        )}
      </div>

      {/* Right Pane: Readiness Gate & Run Controls */}
      <div style={{ display: 'flex', flexDirection: 'column', gap: '16px' }}>
        <div style={{ background: 'var(--bg-surface)', border: '1px solid var(--border-subtle)', borderRadius: '6px', padding: '16px' }}>
          <h2 style={{ fontSize: '14px', fontWeight: 600, marginBottom: '12px' }}>Readiness Gate</h2>
          
          <ul style={{ listStyle: 'none', display: 'flex', flexDirection: 'column', gap: '8px', fontSize: '12px' }}>
            <li style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
              <span>{connected ? '✓' : '✗'}</span>
              <span style={{ color: connected ? 'var(--text-primary)' : 'var(--accent-warning)' }}>
                Controller Online
              </span>
            </li>
            <li style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
              <span>{fresh ? '✓' : '✗'}</span>
              <span style={{ color: fresh ? 'var(--text-primary)' : 'var(--accent-warning)' }}>
                Telemetry Fresh (&lt;2s)
              </span>
            </li>
            <li style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: '8px' }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
                <span>{isHomed ? '✓' : '✗'}</span>
                <span style={{ color: isHomed ? 'var(--text-primary)' : 'var(--accent-warning)' }}>
                  Both Axes Physically Homed
                </span>
              </div>
              {!isHomed && (
                <button
                  type="button"
                  onClick={handleQuickHome}
                  disabled={isHoming || !connected}
                  style={{
                    fontSize: '11px',
                    padding: '2px 8px',
                    background: 'var(--bg-surface-elevated)',
                    border: '1px solid var(--border-subtle)',
                    borderRadius: '4px',
                    cursor: connected && !isHoming ? 'pointer' : 'not-allowed',
                    color: 'var(--text-primary)'
                  }}
                >
                  {isHoming ? 'Homing...' : 'Home Now'}
                </button>
              )}
            </li>
            <li style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
              <span>{noFaults ? '✓' : '✗'}</span>
              <span style={{ color: noFaults ? 'var(--text-primary)' : 'var(--accent-estop)' }}>
                No Active Hardware Faults
              </span>
            </li>
            <li style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
              <span>{activeJob ? '✓' : '✗'}</span>
              <span style={{ color: activeJob ? 'var(--text-primary)' : 'var(--text-muted)' }}>
                Valid Job Prepared in Bounds
              </span>
            </li>
          </ul>

          <div
            style={{
              marginTop: '14px',
              padding: '10px 12px',
              background: isDryRun ? 'rgba(34, 139, 230, 0.15)' : 'var(--bg-app)',
              border: `1px solid ${isDryRun ? '#339af0' : 'var(--border-subtle)'}`,
              borderRadius: '6px',
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'space-between',
              gap: '10px'
            }}
          >
            <div>
              <label htmlFor="engrave-dry-run-toggle" style={{ fontWeight: 600, fontSize: '12px', display: 'block', cursor: 'pointer', color: isDryRun ? '#74c0fc' : 'var(--text-primary)' }}>
                False Engraving (Motors Only)
              </label>
              <span style={{ fontSize: '11px', color: 'var(--text-secondary)' }}>
                Trace toolpath with steppers, force laser OFF (duty = 0)
              </span>
            </div>
            <input
              id="engrave-dry-run-toggle"
              type="checkbox"
              checked={isDryRun}
              onChange={(e) => setIsDryRun(e.target.checked)}
              style={{ width: '18px', height: '18px', cursor: 'pointer' }}
            />
          </div>

          <hr style={{ borderColor: 'var(--border-subtle)', margin: '14px 0' }} />

          <button
            onClick={handleStartJob}
            disabled={!isReady || isLoading || snapshot?.state === 'running_job'}
            style={{
              width: '100%',
              padding: '12px',
              fontSize: '13px',
              fontWeight: 700,
              background: !isReady
                ? 'var(--bg-surface-elevated)'
                : isDryRun
                  ? '#1971c2'
                  : 'var(--accent-ready)',
              color: isReady ? '#FFFFFF' : 'var(--text-muted)',
              border: 'none',
              borderRadius: '4px',
              cursor: isReady ? 'pointer' : 'not-allowed',
              marginBottom: '8px',
              transition: 'background 0.15s ease'
            }}
          >
            {isDryRun ? 'START FALSE ENGRAVING (MOTORS ONLY)' : 'START LASER ENGRAVING'}
          </button>

          {snapshot?.state === 'running_job' && (
            <button
              onClick={handleCancelJob}
              style={{
                width: '100%',
                background: 'var(--accent-estop)',
                color: '#FFF',
                border: 'none',
                fontWeight: 600
              }}
            >
              CANCEL RUNNING JOB
            </button>
          )}
        </div>

        {/* Quick Laser Pulse Test Station */}
        <div style={{ background: 'var(--bg-surface)', border: '1px solid var(--border-subtle)', borderRadius: '6px', padding: '16px' }}>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: showPulseStation ? '12px' : '0' }}>
            <div>
              <h2 style={{ fontSize: '13px', fontWeight: 600 }}>Laser Pulse Test</h2>
              <span style={{ fontSize: '11px', color: 'var(--text-secondary)' }}>Direct test without switching tabs</span>
            </div>
            <button
              type="button"
              onClick={() => setShowPulseStation(!showPulseStation)}
              style={{
                fontSize: '11px',
                padding: '4px 10px',
                background: 'var(--bg-surface-elevated)',
                border: '1px solid var(--border-subtle)',
                borderRadius: '4px',
                cursor: 'pointer',
                color: 'var(--text-primary)'
              }}
            >
              {showPulseStation ? 'Hide Station' : 'Open Station'}
            </button>
          </div>

          {showPulseStation && (
            <div style={{ display: 'flex', flexDirection: 'column', gap: '12px' }}>
              <div
                style={{
                  display: 'flex',
                  alignItems: 'flex-start',
                  gap: '8px',
                  padding: '8px 10px',
                  background: safetyConfirmed ? 'rgba(47, 158, 68, 0.1)' : 'var(--bg-app)',
                  border: `1px solid ${safetyConfirmed ? 'var(--accent-ready)' : 'var(--border-subtle)'}`,
                  borderRadius: '4px'
                }}
              >
                <input
                  type="checkbox"
                  id="engrave-pulse-safety"
                  checked={safetyConfirmed}
                  onChange={(e) => setSafetyConfirmed(e.target.checked)}
                  style={{ marginTop: '2px', cursor: 'pointer' }}
                />
                <label htmlFor="engrave-pulse-safety" style={{ fontSize: '11px', cursor: 'pointer', userSelect: 'none' }}>
                  <strong>Safety Interlock:</strong> OD4+ 450nm eyewear donned & beam path clear.
                </label>
              </div>

              <div>
                <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: '11px', marginBottom: '4px' }}>
                  <span>Power: {pulsePowerPct}%</span>
                  <span className="mono" style={{ color: 'var(--text-secondary)' }}>Duty: {pulseDuty}/4095</span>
                </div>
                <input
                  type="range"
                  min="1"
                  max="90"
                  value={pulsePowerPct}
                  onChange={(e) => setPulsePowerPct(Number(e.target.value))}
                  style={{ width: '100%' }}
                />
              </div>

              <div>
                <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: '11px', marginBottom: '4px' }}>
                  <span>Duration: {pulseDurationMs} ms</span>
                  <span className="mono" style={{ color: 'var(--text-secondary)' }}>Max: 500ms</span>
                </div>
                <input
                  type="range"
                  min="10"
                  max="500"
                  step="10"
                  value={pulseDurationMs}
                  onChange={(e) => setPulseDurationMs(Number(e.target.value))}
                  style={{ width: '100%' }}
                />
              </div>

              <button
                type="button"
                onClick={handleQuickPulse}
                disabled={!connected || pulsing || !safetyConfirmed}
                style={{
                  width: '100%',
                  padding: '8px',
                  fontSize: '12px',
                  fontWeight: 600,
                  background: !safetyConfirmed || !connected ? 'var(--bg-surface-elevated)' : 'var(--accent-estop)',
                  color: !safetyConfirmed || !connected ? 'var(--text-muted)' : '#FFFFFF',
                  border: 'none',
                  borderRadius: '4px',
                  cursor: !safetyConfirmed || !connected ? 'not-allowed' : 'pointer'
                }}
              >
                {pulsing ? 'FIRING PULSE...' : `FIRE LASER PULSE (${pulseDurationMs}ms @ ${pulsePowerPct}%)`}
              </button>

              {pulseSuccess && (
                <div style={{ fontSize: '11px', color: 'var(--accent-ready)', background: 'rgba(47, 158, 68, 0.15)', padding: '6px 8px', borderRadius: '4px' }}>
                  {pulseSuccess}
                </div>
              )}
              {pulseError && (
                <div style={{ fontSize: '11px', color: 'var(--accent-estop)', background: 'rgba(224, 49, 49, 0.15)', padding: '6px 8px', borderRadius: '4px' }}>
                  {pulseError}
                </div>
              )}
            </div>
          )}
        </div>

        {/* Recent Jobs History */}
        <div style={{ background: 'var(--bg-surface)', border: '1px solid var(--border-subtle)', borderRadius: '6px', padding: '16px', flex: 1 }}>
          <h2 style={{ fontSize: '13px', fontWeight: 600, marginBottom: '8px' }}>Recent Jobs</h2>
          <div style={{ display: 'flex', flexDirection: 'column', gap: '6px', maxHeight: '200px', overflowY: 'auto' }}>
            {jobsHistory.map((j) => (
              <button
                type="button"
                key={j.id}
                onClick={() => setActiveJob(j)}
                style={{
                  display: 'block',
                  width: '100%',
                  textAlign: 'left',
                  padding: '6px 8px',
                  borderRadius: '4px',
                  background: activeJob?.id === j.id ? 'var(--border-strong)' : 'var(--bg-surface-elevated)',
                  border: '1px solid var(--border-subtle)',
                  cursor: 'pointer',
                  fontSize: '12px',
                  color: 'inherit',
                  fontFamily: 'inherit'
                }}
              >
                <div style={{ fontWeight: 600 }}>{j.name}</div>
                <div style={{ fontSize: '11px', color: 'var(--text-secondary)' }}>
                  {j.status.toUpperCase()} · {j.total_segments} segs
                </div>
              </button>
            ))}
          </div>
        </div>
      </div>
    </div>
  );
};
