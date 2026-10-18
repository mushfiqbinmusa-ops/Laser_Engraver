import React, { useState, useEffect } from 'react';
import { Preset, MachineConfig } from '../types/jobs';

export const SettingsTab: React.FC = () => {
  const [config, setConfig] = useState<MachineConfig | null>(null);
  const [presets, setPresets] = useState<Preset[]>([]);
  const [newPresetName, setNewPresetName] = useState<string>('');
  const [newPowerPct, setNewPowerPct] = useState<number>(20);
  const [newSpeedSps, setNewSpeedSps] = useState<number>(100);
  const [msg, setMsg] = useState<{ text: string; isError: boolean } | null>(null);

  const fetchData = async () => {
    try {
      const cRes = await fetch('/api/presets/config');
      if (cRes.ok) setConfig(await cRes.json());

      const pRes = await fetch('/api/presets');
      if (pRes.ok) setPresets(await pRes.json());
    } catch (e: any) {
      setMsg({ text: e.message, isError: true });
    }
  };

  useEffect(() => {
    fetchData();
  }, []);

  const handleSaveConfig = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!config) return;
    try {
      const res = await fetch('/api/presets/config', {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(config)
      });
      if (!res.ok) throw new Error('Failed to update machine config');
      setMsg({ text: 'Machine configuration saved successfully', isError: false });
    } catch (err: any) {
      setMsg({ text: err.message, isError: true });
    }
  };

  const handleCreatePreset = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!newPresetName.trim()) return;
    try {
      const res = await fetch('/api/presets', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          name: newPresetName.trim(),
          laser_power_pct: newPowerPct,
          engrave_speed_sps: newSpeedSps,
          travel_speed_sps: 200,
          passes: 1
        })
      });
      if (!res.ok) throw new Error('Failed to create preset');
      setNewPresetName('');
      fetchData();
      setMsg({ text: 'Preset created', isError: false });
    } catch (err: any) {
      setMsg({ text: err.message, isError: true });
    }
  };

  const handleDeletePreset = async (presetId: string) => {
    try {
      const res = await fetch(`/api/presets/${presetId}`, { method: 'DELETE' });
      if (!res.ok) {
        const err = await res.json();
        throw new Error(err.detail || 'Failed to delete');
      }
      fetchData();
    } catch (err: any) {
      setMsg({ text: err.message, isError: true });
    }
  };

  return (
    <div style={{ padding: '24px', display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '24px' }}>
      {/* Machine Hardware Configuration */}
      <div style={{ background: 'var(--bg-surface)', border: '1px solid var(--border-subtle)', borderRadius: '6px', padding: '20px' }}>
        <h2 style={{ fontSize: '15px', fontWeight: 600, marginBottom: '16px' }}>Machine & Calibration Limits</h2>
        {config ? (
          <form onSubmit={handleSaveConfig} style={{ display: 'flex', flexDirection: 'column', gap: '12px' }}>
            <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '12px' }}>
              <div>
                <label htmlFor="cfg-steps-x" style={{ display: 'block', fontSize: '12px', color: 'var(--text-secondary)', marginBottom: '4px' }}>
                  X Steps per mm
                </label>
                <input
                  id="cfg-steps-x"
                  type="number"
                  step="0.1"
                  value={config.steps_per_unit_x}
                  onChange={(e) => setConfig({ ...config, steps_per_unit_x: Number(e.target.value) })}
                  style={{ width: '100%' }}
                />
              </div>
              <div>
                <label htmlFor="cfg-steps-y" style={{ display: 'block', fontSize: '12px', color: 'var(--text-secondary)', marginBottom: '4px' }}>
                  Y Steps per mm
                </label>
                <input
                  id="cfg-steps-y"
                  type="number"
                  step="0.1"
                  value={config.steps_per_unit_y}
                  onChange={(e) => setConfig({ ...config, steps_per_unit_y: Number(e.target.value) })}
                  style={{ width: '100%' }}
                />
              </div>
            </div>

            <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '12px' }}>
              <div>
                <label htmlFor="cfg-width" style={{ display: 'block', fontSize: '12px', color: 'var(--text-secondary)', marginBottom: '4px' }}>
                  Work Area Width (mm)
                </label>
                <input
                  id="cfg-width"
                  type="number"
                  step="1"
                  value={config.work_area_width_mm}
                  onChange={(e) => setConfig({ ...config, work_area_width_mm: Number(e.target.value) })}
                  style={{ width: '100%' }}
                />
              </div>
              <div>
                <label htmlFor="cfg-height" style={{ display: 'block', fontSize: '12px', color: 'var(--text-secondary)', marginBottom: '4px' }}>
                  Work Area Height (mm)
                </label>
                <input
                  id="cfg-height"
                  type="number"
                  step="1"
                  value={config.work_area_height_mm}
                  onChange={(e) => setConfig({ ...config, work_area_height_mm: Number(e.target.value) })}
                  style={{ width: '100%' }}
                />
              </div>
            </div>

            <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '12px' }}>
              <div>
                <label htmlFor="cfg-speed" style={{ display: 'block', fontSize: '12px', color: 'var(--text-secondary)', marginBottom: '4px' }}>
                  Default Speed (sps)
                </label>
                <input
                  id="cfg-speed"
                  type="number"
                  value={config.default_speed_sps}
                  onChange={(e) => setConfig({ ...config, default_speed_sps: Number(e.target.value) })}
                  style={{ width: '100%' }}
                />
              </div>
              <div>
                <label htmlFor="cfg-clamp" style={{ display: 'block', fontSize: '12px', color: 'var(--text-secondary)', marginBottom: '4px' }}>
                  Hard Laser Clamp (%)
                </label>
                <input
                  id="cfg-clamp"
                  type="number"
                  value={config.laser_ceiling_pct}
                  disabled
                  title="Enforced in firmware at 90% (3685 duty)"
                  style={{ width: '100%', opacity: 0.6 }}
                />
              </div>
            </div>

            <button type="submit" style={{ marginTop: '8px', padding: '10px', fontWeight: 600 }}>
              Save Machine Settings
            </button>
          </form>
        ) : (
          <div>Loading machine config...</div>
        )}
      </div>

      {/* Preset Management */}
      <div style={{ background: 'var(--bg-surface)', border: '1px solid var(--border-subtle)', borderRadius: '6px', padding: '20px' }}>
        <h2 style={{ fontSize: '15px', fontWeight: 600, marginBottom: '16px' }}>Process Presets</h2>

        <div style={{ display: 'flex', flexDirection: 'column', gap: '8px', marginBottom: '20px', maxHeight: '280px', overflowY: 'auto' }}>
          {presets.map((p) => (
            <div
              key={p.id}
              style={{
                display: 'flex',
                justifyContent: 'space-between',
                alignItems: 'center',
                padding: '10px 12px',
                background: 'var(--bg-surface-elevated)',
                border: '1px solid var(--border-subtle)',
                borderRadius: '4px'
              }}
            >
              <div>
                <div style={{ fontWeight: 600, fontSize: '13px' }}>
                  {p.name} {p.is_builtin && <span style={{ fontSize: '10px', color: 'var(--text-muted)' }}>(BUILT-IN)</span>}
                </div>
                <div style={{ fontSize: '12px', color: 'var(--text-secondary)' }}>
                  Power: {p.laser_power_pct}% ({p.laser_duty} duty) · Engrave: {p.engrave_speed_sps} sps · Passes: {p.passes}
                </div>
              </div>
              {!p.is_builtin && (
                <button
                  onClick={() => handleDeletePreset(p.id)}
                  style={{ color: 'var(--accent-estop)', borderColor: 'var(--accent-estop)', fontSize: '11px' }}
                >
                  Delete
                </button>
              )}
            </div>
          ))}
        </div>

        {/* Add New Preset */}
        <h3 style={{ fontSize: '13px', fontWeight: 600, marginBottom: '8px' }}>Create Custom Preset</h3>
        <form onSubmit={handleCreatePreset} style={{ display: 'flex', flexDirection: 'column', gap: '8px' }}>
          <div>
            <label htmlFor="preset-name" style={{ display: 'block', fontSize: '11px', color: 'var(--text-secondary)', marginBottom: '4px' }}>
              Preset Name
            </label>
            <input
              id="preset-name"
              type="text"
              placeholder="e.g. 3mm Plywood"
              value={newPresetName}
              onChange={(e) => setNewPresetName(e.target.value)}
              style={{ width: '100%' }}
            />
          </div>
          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '8px' }}>
            <div>
              <label htmlFor="preset-power" style={{ display: 'block', fontSize: '11px', color: 'var(--text-secondary)', marginBottom: '4px' }}>
                Power % (0-90)
              </label>
              <input
                id="preset-power"
                type="number"
                min="1"
                max="90"
                value={newPowerPct}
                onChange={(e) => setNewPowerPct(Number(e.target.value))}
                style={{ width: '100%' }}
              />
            </div>
            <div>
              <label htmlFor="preset-speed" style={{ display: 'block', fontSize: '11px', color: 'var(--text-secondary)', marginBottom: '4px' }}>
                Speed (sps)
              </label>
              <input
                id="preset-speed"
                type="number"
                value={newSpeedSps}
                onChange={(e) => setNewSpeedSps(Number(e.target.value))}
                style={{ width: '100%' }}
              />
            </div>
          </div>
          <button type="submit" style={{ padding: '8px', fontWeight: 600 }}>Add Preset</button>
        </form>

        {msg && (
          <div style={{
            marginTop: '12px',
            padding: '8px 12px',
            borderRadius: '4px',
            fontSize: '12px',
            background: msg.isError ? 'rgba(224, 49, 49, 0.15)' : 'rgba(47, 158, 68, 0.15)',
            color: msg.isError ? '#FFA8A8' : '#B2F2BB'
          }}>
            {msg.text}
          </div>
        )}
      </div>
    </div>
  );
};
