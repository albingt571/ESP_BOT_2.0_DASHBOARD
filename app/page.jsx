'use client';

import { useCallback, useEffect, useMemo, useState } from 'react';

function bytes(value) {
  if (value === undefined || value === null) return '—';
  const n = Number(value);
  if (!Number.isFinite(n)) return '—';
  if (n < 1024) return `${n} B`;
  if (n < 1024 ** 2) return `${(n / 1024).toFixed(1)} KB`;
  if (n < 1024 ** 3) return `${(n / 1024 ** 2).toFixed(2)} MB`;
  return `${(n / 1024 ** 3).toFixed(2)} GB`;
}

function duration(seconds) {
  if (seconds === undefined || seconds === null) return '—';
  const s = Math.max(0, Number(seconds));
  const d = Math.floor(s / 86400), h = Math.floor((s % 86400) / 3600), m = Math.floor((s % 3600) / 60), sec = Math.floor(s % 60);
  return d ? `${d}d ${h}h ${m}m` : h ? `${h}h ${m}m ${sec}s` : `${m}m ${sec}s`;
}

function Metric({ label, value, unit, detail, percent, tone = 'green' }) {
  return <section className="metric card">
    <div className="metric-label">{label}</div>
    <div className="metric-value">{value}<small>{unit}</small></div>
    {detail && <div className="metric-detail">{detail}</div>}
    {percent !== undefined && <div className="meter"><span className={`meter-fill ${tone}`} style={{ width: `${Math.max(0, Math.min(100, percent))}%` }} /></div>}
  </section>;
}

function Info({ label, value }) {
  return <div className="info-row"><span>{label}</span><strong>{value ?? '—'}</strong></div>;
}

function HistoryChart({ title, subtitle, value, unit, samples, field, color = 'green', format = v => `${v}${unit}` }) {
  const values = samples.map(s => Number(s[field])).filter(Number.isFinite);
  const current = values.length ? values[values.length - 1] : null;
  const min = values.length ? Math.min(...values) : 0;
  const max = values.length ? Math.max(...values) : 1;
  const span = Math.max(1, max - min);
  const points = values.map((v, i) => `${values.length < 2 ? 50 : (i / (values.length - 1)) * 100},${36 - ((v - min) / span) * 28}`).join(' ');
  return <section className="card chart-card">
    <div className="chart-head"><div><h3>{title}</h3><p>{subtitle}</p></div><strong>{value ?? (current === null ? '—' : format(current))}</strong></div>
    <div className="chart-area">
      <div className="chart-gridline one"/><div className="chart-gridline two"/><div className="chart-gridline three"/>
      {values.length > 1 ? <svg viewBox="0 0 100 42" preserveAspectRatio="none" className={`sparkline ${color}`} aria-label={`${title} history`}><polyline points={points} fill="none" stroke="currentColor" strokeWidth="0.8" vectorEffect="non-scaling-stroke" /></svg> : <div className="chart-empty">Collecting live samples…</div>}
    </div>
    <div className="chart-foot"><span>{samples.length} samples in this session</span><span>{values.length ? `Range ${format(min)} – ${format(max)}` : 'No samples yet'}</span></div>
  </section>;
}

export default function Dashboard() {
  const [password, setPassword] = useState('');
  const [authenticated, setAuthenticated] = useState(false);
  const [data, setData] = useState(null);
  const [online, setOnline] = useState(false);
  const [age, setAge] = useState(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState('');
  const [lastUpdated, setLastUpdated] = useState(null);
  const [samples, setSamples] = useState([]);

  const fetchStatus = useCallback(async (requestId) => {
    const response = await fetch(`/api/status?requestId=${encodeURIComponent(requestId)}`, { cache: 'no-store' });
    if (response.status === 401) { setAuthenticated(false); throw new Error('Your session expired. Please sign in again.'); }
    const result = await response.json();
    if (!response.ok) throw new Error(result.error || 'Unable to fetch device status');
    if (!result.ready) return false;
    setData(result.telemetry);
    setOnline(true);
    setAge(0);
    setLastUpdated(new Date());
    setSamples(Array.isArray(result.history) ? result.history : []);
    return true;
  }, []);

  useEffect(() => {
    let active = true;
    fetch('/api/status', { cache: 'no-store' }).then(async response => {
      if (response.ok && active) setAuthenticated(true);
    }).catch(() => {});
    return () => { active = false; };
  }, []);

  async function checkDeviceStatus() {
    setLoading(true); setError('');
    try {
      const queued = await fetch('/api/status', { method: 'POST', cache: 'no-store' });
      const request = await queued.json();
      if (!queued.ok) throw new Error(request.error || 'Could not request device status');
      setError('Request sent. Canda Bot checks for requests every 30 seconds. Waiting for its response…');
      const deadline = Date.now() + 55000;
      while (Date.now() < deadline) {
        await new Promise(resolve => setTimeout(resolve, 2000));
        const ready = await fetchStatus(request.requestId);
        if (ready) { setError(''); return; }
      }
      throw new Error('No response yet. Canda Bot may be offline or waiting for its next 30-second check. Try again shortly.');
    } catch (err) { setError(err.message || 'Status request failed'); }
    finally { setLoading(false); }
  }

  async function login(event) {
    event.preventDefault(); setError(''); setLoading(true);
    try {
      const response = await fetch('/api/login', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ password }) });
      const result = await response.json();
      if (!response.ok) throw new Error(result.error || 'Login failed');
      setAuthenticated(true); setPassword('');
    } catch (err) { setError(err.message || 'Login failed'); }
    finally { setLoading(false); }
  }

  async function logout() {
    await fetch('/api/logout', { method: 'POST' }); setAuthenticated(false); setData(null); setOnline(false); setSamples([]);
  }

  const heapPercent = useMemo(() => data?.heapTotal ? (data.heapUsed / data.heapTotal) * 100 : 0, [data]);
  const sketchPercent = useMemo(() => data?.flashSize ? (data.sketchSize / data.flashSize) * 100 : 0, [data]);
  const signalPercent = useMemo(() => data?.wifiRssi !== undefined ? Math.max(0, Math.min(100, (data.wifiRssi + 100) * 2)) : 0, [data]);

  if (!authenticated) return <main className="login-wrap"><div className="login-card card"><div className="brand"><div className="brand-mark">CB</div><div><div className="brand-name">CANDA BOT</div><div className="brand-sub">BUILDERSPACE / DEVICE LAB</div></div></div><p className="eyebrow">SMALL BOARD. REAL TELEMETRY.</p><h1>A window into your device.</h1><p className="muted">Sign in to explore live ESP32 hardware, memory and network telemetry.</p><form onSubmit={login} className="login-form"><label htmlFor="password">Dashboard password</label><input id="password" type="password" autoComplete="current-password" value={password} onChange={e => setPassword(e.target.value)} placeholder="Enter dashboard password" required/><button disabled={loading}>{loading ? 'Signing in…' : 'Open dashboard'} <span>→</span></button></form>{error && <p className="error-text">{error}</p>}<p className="login-foot">PRIVATE DEVICE TELEMETRY · HTTPS SESSION</p></div></main>;


  return (
    <main className="shell">
      <header className="topbar">
        <div className="brand">
          <div className="brand-mark small">CB</div>
          <div>
            <div className="brand-name">CANDA BOT</div>
            <div className="brand-sub">BUILDERSPACE / ESP32 LAB</div>
          </div>
        </div>

        <nav className="nav-links">
          <a className="active" href="#overview">Overview</a>
          <a href="#resources">Resources</a>
          <a href="#network">Network</a>
          <a href="#device">Device</a>
        </nav>

        <div className="top-actions">
          <span className="live-label">
            <i className={online ? 'dot online' : 'dot'} />
            {online ? 'LAST CHECK ONLINE' : 'NOT CHECKED'}
          </span>
          <button
            className="ghost-button"
            onClick={checkDeviceStatus}
            disabled={loading}
          >
            {loading ? 'Checking…' : 'Check Status'}
          </button>
          <button className="ghost-button logout" onClick={logout}>
            Log out
          </button>
        </div>
      </header>

      <section className="hero" id="overview">
        <div>
          <p className="eyebrow">SMALL BOARD. REAL TELEMETRY.</p>
          <h1>
            A microcontroller.
            <br />
            <span>A window into its world.</span>
          </h1>
          <p className="hero-copy">
            Request fresh insight into Canda Bot’s hardware, memory and connectivity.
            <br />
            Telemetry is sent only when you click Check Status.
          </p>
        </div>

        <div className={`status-pill ${online ? 'good' : 'bad'}`}>
          <i className="dot" />
          {online ? 'DEVICE ONLINE' : 'DEVICE OFFLINE'}
        </div>
      </section>

      {error && <div className="notice">{error}</div>}

      {!data ? (
        <div className="empty card">
          <div className="empty-icon">⌁</div>
          <h2>Ready to check Canda Bot</h2>
          <p>
            Click Check Status to request fresh hardware readings.
            Canda Bot checks for your request every 30 seconds and only
            uploads telemetry when requested.
          </p>
          <button onClick={checkDeviceStatus} disabled={loading}>
            {loading ? 'Waiting for Canda Bot…' : 'Check Status'}
          </button>
        </div>
      ) : (
        <>
          <div className="section-kicker">
            <span>01 / LIVE TELEMETRY</span>
            <span>
              {age === null ? 'Waiting for packet' : `Last packet ${age}s ago`}
            </span>
          </div>

          <section className="metrics-grid">
            <Metric
              label="PROCESSOR LOAD"
              value={Number(data.cpuLoadPercent ?? 0).toFixed(1)}
              unit="%"
              detail={`${data.cpuMHz ?? '—'} MHz · ${data.cores ?? '—'} cores`}
              percent={data.cpuLoadPercent ?? 0}
              tone={data.cpuLoadPercent > 85 ? 'amber' : 'green'}
            />

            <Metric
              label="HEAP MEMORY USED"
              value={heapPercent.toFixed(1)}
              unit="%"
              detail={`${bytes(data.heapUsed)} / ${bytes(data.heapTotal)}`}
              percent={heapPercent}
              tone={heapPercent > 85 ? 'amber' : 'green'}
            />

            <Metric
              label="WI-FI SIGNAL"
              value={data.wifiRssi ?? '—'}
              unit=" dBm"
              detail={
                data.wifiConnected
                  ? 'Connected to local network'
                  : 'Wi-Fi disconnected'
              }
              percent={signalPercent}
            />

            <Metric
              label="CHIP TEMPERATURE"
              value={Number(data.chipTemperatureC ?? 0).toFixed(1)}
              unit=" °C"
              detail="Internal sensor · approximate, not ambient"
            />
          </section>

          <section className="chart-grid" id="resources">
            <HistoryChart
              title="CPU WORKLOAD"
              subtitle="Estimated busy time from FreeRTOS idle-task counters"
              value={`${Number(data.cpuLoadPercent ?? 0).toFixed(1)}%`}
              unit="%"
              samples={samples}
              field="cpuLoadPercent"
              format={v => `${v.toFixed(1)}%`}
            />

            <HistoryChart
              title="HEAP PRESSURE"
              subtitle="Allocated internal heap / available heap capacity"
              value={`${heapPercent.toFixed(1)}%`}
              unit="%"
              samples={samples}
              field="heapPercent"
              format={v => `${v.toFixed(1)}%`}
            />

            <HistoryChart
              title="WI-FI SIGNAL HISTORY"
              subtitle="Received signal strength from the ESP32"
              value={`${data.wifiRssi ?? '—'} dBm`}
              unit=" dBm"
              samples={samples}
              field="wifiRssi"
              color="orange"
              format={v => `${v.toFixed(0)} dBm`}
            />

            <HistoryChart
              title="CHIP TEMPERATURE"
              subtitle="Approximate internal sensor reading"
              value={`${Number(data.chipTemperatureC ?? 0).toFixed(1)} °C`}
              unit=" °C"
              samples={samples}
              field="chipTemperatureC"
              color="orange"
              format={v => `${v.toFixed(1)} °C`}
            />
          </section>

          <div className="section-kicker section-spaced" id="network">
            <span>02 / NETWORK & CONNECTIVITY</span>
            <span>LIVE DEVICE STATE</span>
          </div>

          <section className="detail-grid">
            <section className="card detail-card">
              <div className="detail-heading">
                <div>
                  <p className="eyebrow">NETWORK LINK</p>
                  <h2>Wi-Fi connection</h2>
                </div>
                <span className={`mini-status ${data.wifiConnected ? 'ok' : 'fail'}`}>
                  {data.wifiConnected ? 'CONNECTED' : 'DISCONNECTED'}
                </span>
              </div>

              <div className="signal-meter">
                <span style={{ width: `${signalPercent}%` }} />
              </div>

              <Info label="Signal strength" value={`${data.wifiRssi ?? '—'} dBm`} />
              <Info label="Local IP address" value={data.ip} />
              <Info label="MAC address" value={data.mac} />
              <Info label="Internet access" value={data.internetConnected ? 'Available' : 'Unavailable'} />
              <Info label="Failed connectivity checks" value={data.internetFailures} />
            </section>

            <section className="card detail-card" id="device">
              <div className="detail-heading">
                <div>
                  <p className="eyebrow">BOARD STATUS</p>
                  <h2>Device information</h2>
                </div>
                <span className={`mini-status ${data.networkProblem ? 'fail' : 'ok'}`}>
                  {data.networkProblem ? 'ATTENTION' : 'HEALTHY'}
                </span>
              </div>

              <Info label="Chip model" value={data.chipModel} />
              <Info label="CPU frequency" value={`${data.cpuMHz ?? '—'} MHz · ${data.cores ?? '—'} cores`} />
              <Info label="Processor load" value={`${Number(data.cpuLoadPercent ?? 0).toFixed(1)}% (estimated)`} />
              <Info label="Chip temperature" value={`${Number(data.chipTemperatureC ?? 0).toFixed(1)} °C (approx.)`} />
              <Info label="OLED display" value={data.oledAvailable ? 'Detected' : 'Not detected'} />
              <Info label="Sleep mode" value={data.sleeping ? 'Sleeping' : 'Awake'} />
              <Info label="OTA update" value={data.otaInProgress ? 'In progress' : 'Idle'} />
              <Info label="Uptime" value={duration(data.uptimeSeconds)} />
            </section>
          </section>

          <div className="section-kicker section-spaced">
            <span>03 / MEMORY & STORAGE</span>
            <span>CAPACITY DETAILS</span>
          </div>

          <section className="card storage-card">
            <div className="storage-block">
              <div className="storage-title">
                <div>
                  <h3>Internal heap memory</h3>
                  <p>Runtime memory available to the firmware</p>
                </div>
                <strong>{heapPercent.toFixed(1)}% used</strong>
              </div>

              <div className="wide-meter">
                <span style={{ width: `${heapPercent}%` }} />
              </div>

              <div className="storage-foot">
                <span>Free: {bytes(data.heapFree)}</span>
                <span>Minimum free: {bytes(data.heapMinFree)}</span>
                <span>Total: {bytes(data.heapTotal)}</span>
              </div>
            </div>

            <div className="storage-block">
              <div className="storage-title">
                <div>
                  <h3>Flash & firmware</h3>
                  <p>Application size and available OTA partition space</p>
                </div>
                <strong>{sketchPercent.toFixed(1)}% of flash</strong>
              </div>

              <div className="wide-meter">
                <span style={{ width: `${Math.min(100, sketchPercent)}%` }} />
              </div>

              <div className="storage-foot">
                <span>Firmware: {bytes(data.sketchSize)}</span>
                <span>OTA free: {bytes(data.sketchFree)}</span>
                <span>Flash total: {bytes(data.flashSize)}</span>
              </div>
            </div>

            <div className="storage-block">
              <div className="storage-title">
                <div>
                  <h3>External PSRAM</h3>
                  <p>Additional memory fitted to the board, if available</p>
                </div>
                <strong>{bytes(data.psramTotal)}</strong>
              </div>

              <div className="storage-foot">
                <span>Free: {bytes(data.psramFree)}</span>
                <span>
                  Used: {bytes(Math.max(0, (data.psramTotal || 0) - (data.psramFree || 0)))}
                </span>
              </div>
            </div>
          </section>
        </>
      )}

      <footer className="footer">
        <span>CANDA BOT / ESP32 TELEMETRY</span>
        <span>
          ON-DEMAND TELEMETRY · {lastUpdated
            ? `UPDATED ${lastUpdated.toLocaleTimeString()}`
            : 'AWAITING DATA'}
        </span>
      </footer>
    </main>
  );
}

