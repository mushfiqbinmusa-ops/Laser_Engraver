import React, { createContext, useContext, useEffect, useState, useCallback, useMemo } from 'react';
import { SystemSnapshot, MachineStatusResponse } from '../types/machine';

interface MachineContextType {
  connected: boolean;
  fresh: boolean;
  snapshot: SystemSnapshot | null;
  lastSeen: Date | null;
  homePhase: string | null;
  homeResult: string | null;
  estop: () => Promise<void>;
  stop: () => Promise<void>;
  resetFault: () => Promise<void>;
  home: (axis: 'all' | 'x' | 'y') => Promise<void>;
  jog: (axis: 'x' | 'y', steps: number, speed?: number) => Promise<void>;
  pulse: (duty: number, durationMs: number) => Promise<void>;
  setDriver: (enabled: boolean) => Promise<void>;
  setMicrostepMode: (mode: 1 | 2 | 4 | 8 | 16) => Promise<void>;
  refreshStatus: () => Promise<void>;
}

const MachineContext = createContext<MachineContextType | null>(null);

export const MachineProvider: React.FC<{ children: React.ReactNode }> = ({ children }) => {
  const [connected, setConnected] = useState<boolean>(false);
  const [fresh, setFresh] = useState<boolean>(false);
  const [snapshot, setSnapshot] = useState<SystemSnapshot | null>(null);
  const [lastSeen, setLastSeen] = useState<Date | null>(null);
  const [homePhase, setHomePhase] = useState<string | null>(null);
  const [homeResult, setHomeResult] = useState<string | null>(null);

  const refreshStatus = useCallback(async () => {
    try {
      const res = await fetch('/api/machine/status');
      if (res.ok) {
        const data: MachineStatusResponse = await res.json();
        setConnected(data.connected);
        setFresh(data.fresh);
        if (data.snapshot) {
          setSnapshot(data.snapshot);
          setLastSeen(new Date(data.last_seen_epoch * 1000));
        }
      }
    } catch {
      setConnected(false);
      setFresh(false);
    }
  }, []);

  // WebSocket Connection for Real-Time Telemetry
  useEffect(() => {
    let ws: WebSocket | null = null;
    let timer: number | null = null;
    let isMounted = true;

    const connectWs = () => {
      if (!isMounted) return;
      const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
      const wsUrl = `${protocol}//${window.location.host}/ws`;
      ws = new WebSocket(wsUrl);

      ws.onopen = () => {
        if (isMounted) setConnected(true);
      };

      ws.onmessage = (event) => {
        if (!isMounted) return;
        try {
          const data = JSON.parse(event.data);
          if (data.type === 'system.snapshot') {
            setSnapshot(data as SystemSnapshot);
            setConnected(true);
            setFresh(true);
            setLastSeen(new Date());
          } else if (data.type === 'operation.started' || data.type === 'operation.progress') {
            setHomePhase(data.phase || 'homing');
            if (data.type === 'operation.started') setHomeResult(null);
          } else if (data.type === 'operation.completed') {
            setHomePhase(null);
            setHomeResult(data.result || 'unknown');
          }
        } catch {
          // ignore malformed frame
        }
      };

      ws.onclose = () => {
        if (!isMounted) return;
        setConnected(false);
        setFresh(false);
        timer = window.setTimeout(connectWs, 3000);
      };

      ws.onerror = () => {
        ws?.close();
      };
    };

    connectWs();
    refreshStatus();
    // Fast 500ms fallback poll guarantees live telemetry updates even when WS is quiet
    const pollInterval = window.setInterval(refreshStatus, 500);

    return () => {
      isMounted = false;
      if (timer) window.clearTimeout(timer);
      window.clearInterval(pollInterval);
      if (ws) {
        ws.onclose = null;
        ws.onerror = null;
        ws.close();
      }
    };
  }, [refreshStatus]);

  const estop = useCallback(async () => {
    try {
      const res = await fetch('/api/machine/estop', { method: 'POST' });
      if (!res.ok) {
        const err = await res.json().catch(() => ({}));
        throw new Error(err.detail || `HTTP ${res.status}`);
      }
    } catch (err: any) {
      console.error('CRITICAL: E-Stop dispatch failure:', err);
      window.alert(`CRITICAL: E-STOP COMMAND FAILED TO REACH CONTROLLER! ${err.message}`);
    } finally {
      await refreshStatus();
    }
  }, [refreshStatus]);

  const stop = useCallback(async () => {
    try {
      const res = await fetch('/api/machine/stop', { method: 'POST' });
      if (!res.ok) {
        const err = await res.json().catch(() => ({}));
        throw new Error(err.detail || `HTTP ${res.status}`);
      }
    } catch (err: any) {
      console.error('Stop command failed:', err);
    } finally {
      await refreshStatus();
    }
  }, [refreshStatus]);

  const resetFault = useCallback(async () => {
    await fetch('/api/machine/reset-fault', { method: 'POST' });
    await refreshStatus();
  }, [refreshStatus]);

  const home = useCallback(async (axis: 'all' | 'x' | 'y') => {
    setHomePhase('starting');
    setHomeResult(null);
    try {
      const res = await fetch('/api/machine/home', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ axis })
      });
      if (!res.ok) {
        const err = await res.json().catch(() => ({}));
        throw new Error(err.detail || 'Homing failed');
      }
      await refreshStatus();
    } catch (err) {
      setHomePhase(null);
      throw err;
    } finally {
      setHomePhase((phase) => (phase === 'starting' ? null : phase));
    }
  }, [refreshStatus]);

  const jog = useCallback(async (axis: 'x' | 'y', steps: number, speed_sps: number = 100) => {
    const res = await fetch('/api/machine/jog', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ axis, steps, speed_sps })
    });
    if (!res.ok) {
      const err = await res.json().catch(() => ({}));
      throw new Error(err.detail || 'Jog failed');
    }
  }, []);

  const pulse = useCallback(async (duty: number, duration_ms: number) => {
    const res = await fetch('/api/machine/laser/pulse', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ duty, duration_ms })
    });
    if (!res.ok) {
      const err = await res.json().catch(() => ({}));
      throw new Error(err.detail || 'Laser pulse rejected');
    }
  }, []);

  const setDriver = useCallback(async (enabled: boolean) => {
    const res = await fetch('/api/machine/driver', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ enabled })
    });
    if (!res.ok) {
      const err = await res.json().catch(() => ({}));
      throw new Error(err.detail || 'Driver toggle failed');
    }
    await refreshStatus();
  }, [refreshStatus]);

  const setMicrostepMode = useCallback(async (mode: 1 | 2 | 4 | 8 | 16) => {
    const res = await fetch('/api/machine/microstep', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ mode })
    });
    if (!res.ok) {
      const err = await res.json().catch(() => ({}));
      throw new Error(err.detail || 'Microstepping change failed');
    }
    await refreshStatus();
  }, [refreshStatus]);

  const contextValue = useMemo(() => ({
    connected,
    fresh,
    snapshot,
    lastSeen,
    homePhase,
    homeResult,
    estop,
    stop,
    resetFault,
    home,
    jog,
    pulse,
    setDriver,
    setMicrostepMode,
    refreshStatus
  }), [
    connected,
    fresh,
    snapshot,
    lastSeen,
    homePhase,
    homeResult,
    estop,
    stop,
    resetFault,
    home,
    jog,
    pulse,
    setDriver,
    setMicrostepMode,
    refreshStatus
  ]);

  return (
    <MachineContext.Provider value={contextValue}>
      {children}
    </MachineContext.Provider>
  );
};

export const useMachine = () => {
  const ctx = useContext(MachineContext);
  if (!ctx) throw new Error('useMachine must be used within MachineProvider');
  return ctx;
};
