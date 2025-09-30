import { ref, computed, onBeforeUnmount } from 'vue';

export interface SocketSample {
  power: number; // raw power value delivered
  voltage: number;
  current?: number | null;
  timestamp: number;
  raw: any;
}

interface UsePowerSocketOptions {
  maxSamples?: number;
  reconnectBaseDelayMs?: number;
  reconnectMaxDelayMs?: number;
}

export function usePowerSocket(ipRef: () => string, options: UsePowerSocketOptions = {}) {
  const maxSamples = options.maxSamples ?? 1000;
  const reconnectBase = options.reconnectBaseDelayMs ?? 500;
  const reconnectMax = options.reconnectMaxDelayMs ?? 8000;

  const isConnected = ref(false);
  const isConnecting = ref(false);
  const error = ref<string | null>(null);
  const latest = ref<SocketSample | null>(null);
  const history = ref<SocketSample[]>([]);
  const minPower = ref<number | null>(null);
  const maxPower = ref<number | null>(null);
  const minVoltage = ref<number | null>(null);
  const maxVoltage = ref<number | null>(null);
  const attempts = ref(0);
  // Diagnostics
  interface WsEvent { t: number; type: string; detail?: any; }
  const eventLog = ref<WsEvent[]>([]);
  const MAX_EVENTS = 120;
  function pushEvent(type: string, detail?: any) {
    eventLog.value.push({ t: Date.now(), type, detail });
    if (eventLog.value.length > MAX_EVENTS) eventLog.value.shift();
  }
  let ws: WebSocket | null = null;
  let manualClose = false;
  let reconnectTimer: number | null = null;
  let simTimer: number | null = null;
  const lastUrl = ref<string | null>(null);
  let triedSecureDowngraded = false;
  let nextAllowedConnect = 0; // timestamp ms throttle
  let connectStart = 0;
  let pingTimer: number | null = null;
  let lastPingSentAt = 0;
  const lastPingRtt = ref<number | null>(null);
  const lastFirstMessageLatency = ref<number | null>(null);
  let firstSampleSeen = false;

  // Potential (theoretical) power estimation (Thevenin style)
  const voc = ref<number|null>(null);         // estimated open-circuit voltage
  const rint = ref<number|null>(null);        // estimated internal resistance
  const rintSamples = ref(0);                 // number of accepted R_int samples
  const potentialPower = ref<number|null>(null); // computed potential Pmax
  let lastVocUpdateTs = 0;
  const I_LOW = 0.02;   // A threshold: treat as near open-circuit if below
  const I_MIN = 0.08;   // A minimum current to accept R_int estimation
  const ALPHA_VOC = 0.8; // smoothing factors
  const ALPHA_RINT = 0.9;
  const MIN_VALID_RINT = 0.01;  // Ohms
  const MAX_VALID_RINT = 50;    // clamp unrealistic

  function updateModel(sample: SocketSample) {
    const V = sample.voltage;
    const I = sample.current ?? 0;
    if (V == null || Number.isNaN(V)) return;
    if (I < I_LOW) {
      // near open circuit, update voc
      if (voc.value == null) voc.value = V; else voc.value = ALPHA_VOC * voc.value + (1-ALPHA_VOC)*V;
      lastVocUpdateTs = sample.timestamp;
    } else if (I >= I_MIN && voc.value && voc.value > V) {
      const candidate = (voc.value - V) / I;
      if (candidate > MIN_VALID_RINT && candidate < MAX_VALID_RINT) {
        rint.value = rint.value == null ? candidate : ALPHA_RINT * rint.value + (1-ALPHA_RINT)*candidate;
        rintSamples.value++;
      }
    }
    // Derive Pmax if we have some confidence
    if (voc.value && rint.value && rintSamples.value >= 3) {
      const pmax = (voc.value * voc.value) / (4 * rint.value);
      // Do not show lower than actual delivered power (avoid confusion)
      const actual = sample.power;
      potentialPower.value = Math.max(pmax, actual);
    }
  }

  function connect() {
    const ip = ipRef().trim();
    if (!ip) { error.value = 'IP fehlt'; return; }
    const now = Date.now();
    if (now < nextAllowedConnect) {
      // schedule a deferred attempt exactly at allowed time if not already pending
      if (!reconnectTimer) {
        const wait = nextAllowedConnect - now;
        reconnectTimer = window.setTimeout(() => { reconnectTimer = null; connect(); }, wait);
      }
      return;
    }
    cleanup();
    manualClose = false;
    error.value = null;
    isConnecting.value = true;
    connectStart = Date.now();
    firstSampleSeen = false;
    pushEvent('connect-attempt', { ip, attempt: attempts.value + 1 });
  // Simulation mode (enter 'demo' as host/IP)
  if (ip.toLowerCase() === 'demo') {
      startSimulation();
      return;
    }
    const url = buildWsUrl(ip, triedSecureDowngraded);
    try {
      ws = new WebSocket(url);
      lastUrl.value = url;
      pushEvent('ws-created', { url });
      ws.onopen = () => {
        isConnecting.value = false;
        isConnected.value = true;
        attempts.value = 0;
        triedSecureDowngraded = false; // reset after success
        pushEvent('open', { url });
        // Start periodic ping (every 15s)
        if (pingTimer) window.clearInterval(pingTimer);
        pingTimer = window.setInterval(() => {
          if (ws && ws.readyState === WebSocket.OPEN) {
            lastPingSentAt = performance.now();
            try { ws.send('p'); pushEvent('ping-sent'); } catch {/* ignore */}
          }
        }, 15000);
      };
      ws.onmessage = (ev) => {
        try {
          const data = JSON.parse(ev.data);
          const power = typeof data.power === 'number' ? data.power : NaN;
          const voltage = typeof data.voltage === 'number' ? data.voltage : NaN;
          const current = typeof data.current === 'number' ? data.current : null;
          if (Number.isNaN(power) || Number.isNaN(voltage)) return; // skip invalid
          const sample: SocketSample = { power, voltage, current, timestamp: Date.now(), raw: data };
          latest.value = sample;
          history.value.push(sample);
          if (history.value.length > maxSamples) history.value.shift();
          minPower.value = minPower.value == null ? power : Math.min(minPower.value, power);
          maxPower.value = maxPower.value == null ? power : Math.max(maxPower.value, power);
          minVoltage.value = minVoltage.value == null ? voltage : Math.min(minVoltage.value, voltage);
          maxVoltage.value = maxVoltage.value == null ? voltage : Math.max(maxVoltage.value, voltage);
          updateModel(sample);
          if (!firstSampleSeen) {
            firstSampleSeen = true;
            lastFirstMessageLatency.value = Date.now() - connectStart;
            pushEvent('first-sample', { latencyMs: lastFirstMessageLatency.value });
          }
        } catch (e) {
          // Maybe it's a simple pong text frame
            if (ev.data === 'pong' && lastPingSentAt) {
              lastPingRtt.value = performance.now() - lastPingSentAt;
              pushEvent('pong', { rtt: lastPingRtt.value });
            }
        }
      };
      ws.onclose = (ev) => {
        isConnected.value = false;
        isConnecting.value = false;
        if (pingTimer) { window.clearInterval(pingTimer); pingTimer = null; }
        pushEvent('close', { code: ev.code, reason: ev.reason, clean: ev.wasClean });
        if (!manualClose) scheduleReconnect();
      };
      ws.onerror = (ev) => {
        // If secure attempt failed (-107 handshake) and we haven't downgraded yet, try plain ws immediately
        if (!manualClose && !triedSecureDowngraded && /^wss:/i.test(url)) {
          triedSecureDowngraded = true;
          if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) ws.close();
          // retry with downgrade after short throttle
          nextAllowedConnect = Date.now() + 250;
          pushEvent('downgrade-to-ws');
          setTimeout(() => connect(), 250);
          return;
        }
        error.value = 'WebSocket Fehler';
        pushEvent('error');
        ws?.close();
      };
    } catch (e: any) {
      error.value = e.message || String(e);
      pushEvent('exception', { message: error.value });
      scheduleReconnect();
    }
  }

  function scheduleReconnect() {
    if (manualClose) return;
    attempts.value++;
    // Fast silent retries for first two attempts (hidden from user perception)
    let finalDelay: number;
    if (attempts.value <= 2) {
      finalDelay = 60; // ~60ms quick retry
      // keep isConnecting true so UI does not flicker to disconnected
      isConnecting.value = true;
      pushEvent('fast-retry', { attempt: attempts.value, delay: finalDelay });
    } else {
      const delay = Math.min(reconnectMax, reconnectBase * Math.pow(2, Math.min(attempts.value, 6)));
      const minDelay = 300; // enforce minimal delay to avoid rapid looping
      finalDelay = Math.max(delay, minDelay);
    }
    nextAllowedConnect = Date.now() + finalDelay;
    if (reconnectTimer) window.clearTimeout(reconnectTimer);
    reconnectTimer = window.setTimeout(() => { reconnectTimer = null; connect(); }, finalDelay);
    pushEvent('reconnect-scheduled', { attempt: attempts.value, delay: finalDelay });
  }

  function disconnect() {
    manualClose = true;
    if (reconnectTimer) { window.clearTimeout(reconnectTimer); reconnectTimer = null; }
    if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) ws.close();
  if (simTimer) { window.clearInterval(simTimer); simTimer = null; }
    if (pingTimer) { window.clearInterval(pingTimer); pingTimer = null; }
    pushEvent('manual-disconnect');
    cleanup();
  }

  function cleanup() {
    isConnected.value = false;
    isConnecting.value = false;
  }

  function pushSample(powerVal: number, voltageVal: number, currentVal: number|null) {
    const sample: SocketSample = { power: powerVal, voltage: voltageVal, current: currentVal, timestamp: Date.now(), raw: { power: powerVal, voltage: voltageVal, current: currentVal } };
    latest.value = sample;
    history.value.push(sample);
    if (history.value.length > maxSamples) history.value.shift();
    minPower.value = minPower.value == null ? powerVal : Math.min(minPower.value, powerVal);
    maxPower.value = maxPower.value == null ? powerVal : Math.max(maxPower.value, powerVal);
    minVoltage.value = minVoltage.value == null ? voltageVal : Math.min(minVoltage.value, voltageVal);
    maxVoltage.value = maxVoltage.value == null ? voltageVal : Math.max(maxVoltage.value, voltageVal);
  }

  function startSimulation() {
    isConnecting.value = false;
    isConnected.value = true;
    attempts.value = 0;
    pushEvent('sim-start');
    let logical = 40 + Math.random()*20; // start mid-range
    simTimer = window.setInterval(() => {
      // Smooth random walk with bounds 2..100 W
      const delta = (Math.random()-0.5)*8;
      logical = Math.min(100, Math.max(2, logical + delta));
      // Fake voltage around 5.00V +/- 0.15, unrelated to power but plausible USB style
      const voltageVal = 5 + (Math.random()-0.5)*0.3;
      pushSample(Number(logical.toFixed(2)), Number(voltageVal.toFixed(3)), null);
    }, 800); // ~0.8s cadence
  }

  const lastUpdated = computed(() => latest.value?.timestamp ?? null);
  const power = computed(() => latest.value?.power ?? null);
  const voltage = computed(() => latest.value?.voltage ?? null);
  const current = computed(() => latest.value?.current ?? null);
  const sampleCount = computed(() => history.value.length);

  onBeforeUnmount(() => disconnect());

  return { connect, disconnect, isConnected, isConnecting, error, latest, history, power, voltage, current, lastUpdated, sampleCount, minPower, maxPower, minVoltage, maxVoltage, lastUrl, attempts, eventLog, lastPingRtt, lastFirstMessageLatency, voc, rint, rintSamples, potentialPower };
}

function buildWsUrl(ip: string, forcedPlain: boolean) {
  if (/^wss?:\/\//i.test(ip)) {
    // ensure path /ws present
    return ip.replace(/\/$/, '').match(/\/ws(\b|$)/) ? ip : ip.replace(/\/$/, '') + '/ws';
  }
  let cleaned = ip.replace(/\/$/, '');
  // Strip protocol if user typed http(s)://
  cleaned = cleaned.replace(/^https?:\/\//i, '');
  // Remove any trailing /ws to re-append normalized
  cleaned = cleaned.replace(/\/ws$/i, '');
  const hostOnly = cleaned.split('/')[0];
  const isLan = /^(localhost|127\.0\.0\.1|10\.|192\.168\.|172\.(1[6-9]|2[0-9]|3[0-1])\.)/i.test(hostOnly) || /^[a-z0-9\-]+\.local$/i.test(hostOnly) || /homeofficetrainer/i.test(hostOnly);
  const secureContext = (typeof window !== 'undefined') && window.location && window.location.protocol === 'https:';
  const useSecure = secureContext && !isLan && !forcedPlain;
  const scheme = useSecure ? 'wss://' : 'ws://';
  return scheme + hostOnly + '/ws';
}
