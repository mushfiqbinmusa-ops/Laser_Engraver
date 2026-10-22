import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';

type ConsoleKind = 'command' | 'ack' | 'event' | 'telemetry' | 'device_log' | 'frame';
type ConsoleFilter = 'human' | 'commands' | 'telemetry' | 'events' | 'device' | 'all';

interface ConsoleEntry {
  seq: number;
  ts: number;
  direction: 'rx' | 'tx';
  kind: ConsoleKind;
  text: string;
}

const FILTERS: { id: ConsoleFilter; label: string }[] = [
  { id: 'human', label: 'Human' },
  { id: 'commands', label: 'Commands' },
  { id: 'telemetry', label: 'Telemetry' },
  { id: 'events', label: 'Events' },
  { id: 'device', label: 'Boot/Debug' },
  { id: 'all', label: 'Raw' }
];

function matchesFilter(entry: ConsoleEntry, filter: ConsoleFilter): boolean {
  switch (filter) {
    case 'human':
      return entry.kind !== 'telemetry';
    case 'commands':
      return entry.kind === 'command' || entry.kind === 'ack';
    case 'telemetry':
      return entry.kind === 'telemetry';
    case 'events':
      return entry.kind === 'event';
    case 'device':
      return entry.kind === 'device_log' || entry.kind === 'frame';
    case 'all':
      return true;
    default:
      return true;
  }
}

function compactTelemetry(text: string): string {
  try {
    const data = JSON.parse(text);
    const homed = data.homed ?? {};
    const pos = data.pos ?? {};
    const sensors = data.sensors ?? {};
    return [
      `state=${data.state}`,
      `pos=(${pos.x ?? '?'},${pos.y ?? '?'})`,
      `homed=(${homed.x ? 'X' : '-'}${homed.y ? 'Y' : '-'})`,
      `sensors=(${sensors.x_filtered ? 'X' : '-'}${sensors.y_filtered ? 'Y' : '-'})`,
      `drivers=${data.drivers_enabled ? 'on' : 'off'}`,
      data.fault ? `fault=${data.fault}` : ''
    ].filter(Boolean).join(' ');
  } catch {
    return text;
  }
}

export const SerialConsoleDrawer: React.FC = () => {
  const [open, setOpen] = useState(false);
  const [entries, setEntries] = useState<ConsoleEntry[]>([]);
  const [filter, setFilter] = useState<ConsoleFilter>('human');
  const [paused, setPaused] = useState(false);
  const [autoScroll, setAutoScroll] = useState(true);
  const [connected, setConnected] = useState(false);
  const scrollRef = useRef<HTMLDivElement | null>(null);
  const pausedRef = useRef(paused);
  pausedRef.current = paused;

  const appendEntries = useCallback((incoming: ConsoleEntry[]) => {
    if (pausedRef.current || incoming.length === 0) return;
    setEntries((current) => {
      const known = new Set(current.map((entry) => entry.seq));
      const fresh = incoming.filter((entry) => !known.has(entry.seq));
      if (fresh.length === 0) return current;
      return [...current, ...fresh].slice(-2000);
    });
  }, []);

  useEffect(() => {
    let ws: WebSocket | null = null;
    let retryTimer: number | null = null;
    let mounted = true;

    const connect = () => {
      if (!mounted) return;
      const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
      ws = new WebSocket(`${protocol}//${window.location.host}/ws/console`);

      ws.onopen = () => {
        if (mounted) setConnected(true);
      };

      ws.onmessage = (event) => {
        try {
          const message = JSON.parse(event.data);
          if (message.type === 'console.history') {
            setEntries((message.entries as ConsoleEntry[]).slice(-2000));
          } else if (message.type === 'console.entry') {
            appendEntries([message.entry as ConsoleEntry]);
          }
        } catch {
          // Ignore malformed console frames
        }
      };

      ws.onclose = () => {
        if (!mounted) return;
        setConnected(false);
        retryTimer = window.setTimeout(connect, 2000);
      };

      ws.onerror = () => {
        ws?.close();
      };
    };

    fetch('/api/console/history?limit=500')
      .then((res) => (res.ok ? res.json() : null))
      .then((data) => {
        if (mounted && data?.entries) setEntries(data.entries.slice(-2000));
      })
      .catch(() => undefined);

    connect();

    return () => {
      mounted = false;
      if (retryTimer) window.clearTimeout(retryTimer);
      if (ws) {
        ws.onclose = null;
        ws.onerror = null;
        ws.close();
      }
    };
  }, [appendEntries]);

  const visibleEntries = useMemo(
    () => entries.filter((entry) => matchesFilter(entry, filter)),
    [entries, filter]
  );

  useEffect(() => {
    if (autoScroll && scrollRef.current) {
      scrollRef.current.scrollTop = scrollRef.current.scrollHeight;
    }
  }, [visibleEntries, autoScroll, open]);

  const download = () => {
    const body = entries
      .map((entry) => `${new Date(entry.ts * 1000).toISOString()} ${entry.direction.toUpperCase()} [${entry.kind}] ${entry.text}`)
      .join('\n');
    const blob = new Blob([body], { type: 'text/plain' });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = `laser-xy-serial-${new Date().toISOString().replace(/[:.]/g, '-')}.log`;
    link.click();
    URL.revokeObjectURL(url);
  };

  return (
    <div
      style={{
        position: 'fixed',
        left: 0,
        right: 0,
        bottom: 0,
        zIndex: 60,
        background: '#0F172A',
        color: '#E2E8F0',
        borderTop: '1px solid #334155',
        boxShadow: '0 -8px 30px rgba(0,0,0,0.18)'
      }}
    >
      <button
        type="button"
        onClick={() => setOpen((value) => !value)}
        aria-expanded={open}
        style={{
          width: '100%',
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'space-between',
          gap: '12px',
          padding: '7px 16px',
          background: '#111827',
          color: '#E2E8F0',
          border: 'none',
          borderRadius: 0,
          boxShadow: 'none',
          fontWeight: 600,
          fontSize: '11px',
          letterSpacing: '0.04em'
        }}
      >
        <span style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
          <span
            style={{
              width: '7px',
              height: '7px',
              borderRadius: '50%',
              background: connected ? '#22C55E' : '#F59E0B',
              display: 'inline-block'
            }}
          />
          SERIAL CONSOLE · {entries.length} lines · {connected ? 'LIVE' : 'RECONNECTING'}
        </span>
        <span>{open ? 'COLLAPSE ▼' : 'EXPAND ▲'}</span>
      </button>

      {open && (
        <div style={{ padding: '8px 12px 10px' }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: '6px', flexWrap: 'wrap', marginBottom: '8px' }}>
            {FILTERS.map((item) => (
              <button
                key={item.id}
                type="button"
                onClick={() => setFilter(item.id)}
                style={{
                  padding: '3px 9px',
                  fontSize: '10px',
                  fontWeight: 600,
                  background: filter === item.id ? '#E2E8F0' : '#1E293B',
                  color: filter === item.id ? '#0F172A' : '#CBD5E1',
                  border: '1px solid #334155',
                  borderRadius: '3px',
                  boxShadow: 'none'
                }}
              >
                {item.label}
              </button>
            ))}
            <span style={{ flex: 1 }} />
            <button
              type="button"
              onClick={() => setPaused((value) => !value)}
              style={{ padding: '3px 9px', fontSize: '10px', background: paused ? '#F59E0B' : '#1E293B', color: paused ? '#0F172A' : '#CBD5E1', border: '1px solid #334155', borderRadius: '3px', boxShadow: 'none' }}
            >
              {paused ? 'RESUME' : 'PAUSE'}
            </button>
            <button
              type="button"
              onClick={() => setAutoScroll((value) => !value)}
              style={{ padding: '3px 9px', fontSize: '10px', background: autoScroll ? '#1E293B' : '#111827', color: autoScroll ? '#CBD5E1' : '#64748B', border: '1px solid #334155', borderRadius: '3px', boxShadow: 'none' }}
            >
              AUTOSCROLL {autoScroll ? 'ON' : 'OFF'}
            </button>
            <button
              type="button"
              onClick={download}
              style={{ padding: '3px 9px', fontSize: '10px', background: '#1E293B', color: '#CBD5E1', border: '1px solid #334155', borderRadius: '3px', boxShadow: 'none' }}
            >
              DOWNLOAD
            </button>
            <button
              type="button"
              onClick={() => setEntries([])}
              style={{ padding: '3px 9px', fontSize: '10px', background: '#1E293B', color: '#CBD5E1', border: '1px solid #334155', borderRadius: '3px', boxShadow: 'none' }}
            >
              CLEAR
            </button>
          </div>

          <div
            ref={scrollRef}
            className="mono"
            style={{
              height: '230px',
              overflowY: 'auto',
              background: '#020617',
              border: '1px solid #1E293B',
              borderRadius: '3px',
              padding: '6px 8px',
              fontSize: '11px',
              lineHeight: 1.55
            }}
          >
            {visibleEntries.length === 0 && (
              <div style={{ color: '#64748B' }}>No console lines for this filter.</div>
            )}
            {visibleEntries.map((entry) => {
              const time = new Date(entry.ts * 1000).toLocaleTimeString();
              const isTx = entry.direction === 'tx';
              const text = entry.kind === 'telemetry' ? compactTelemetry(entry.text) : entry.text;
              const color =
                entry.kind === 'device_log' ? '#93C5FD'
                  : entry.kind === 'ack' ? '#86EFAC'
                    : entry.kind === 'event' ? '#FDE68A'
                      : entry.kind === 'telemetry' ? '#94A3B8'
                        : isTx ? '#FCA5A5'
                          : '#E2E8F0';
              return (
                <div key={entry.seq} style={{ display: 'flex', gap: '8px', whiteSpace: 'pre-wrap', wordBreak: 'break-word' }}>
                  <span style={{ color: '#475569', flexShrink: 0 }}>{time}</span>
                  <span style={{ color: isTx ? '#F87171' : '#38BDF8', flexShrink: 0 }}>{isTx ? 'TX' : 'RX'}</span>
                  <span style={{ color }}>{text}</span>
                </div>
              );
            })}
          </div>
        </div>
      )}
    </div>
  );
};
