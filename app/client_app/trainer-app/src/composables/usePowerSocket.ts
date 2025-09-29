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
  let ws: WebSocket | null = null;
  let manualClose = false;
  let reconnectTimer: number | null = null;
  let simTimer: number | null = null;

  function connect() {
    const ip = ipRef().trim();
    if (!ip) { error.value = 'IP fehlt'; return; }
    cleanup();
    manualClose = false;
    error.value = null;
    isConnecting.value = true;
  // Simulation mode (enter 'demo' as host/IP)
  if (ip.toLowerCase() === 'demo') {
      startSimulation();
      return;
    }
    const url = buildWsUrl(ip);
    try {
      ws = new WebSocket(url);
      ws.onopen = () => {
        isConnecting.value = false;
        isConnected.value = true;
        attempts.value = 0;
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
        } catch (e) { /* ignore */ }
      };
      ws.onclose = () => {
        isConnected.value = false;
        isConnecting.value = false;
        if (!manualClose) scheduleReconnect();
      };
      ws.onerror = (ev) => {
        error.value = 'WebSocket Fehler';
        ws?.close();
      };
    } catch (e: any) {
      error.value = e.message || String(e);
      scheduleReconnect();
    }
  }

  function scheduleReconnect() {
    if (manualClose) return;
    attempts.value++;
    const delay = Math.min(reconnectMax, reconnectBase * Math.pow(2, Math.min(attempts.value, 6)));
    if (reconnectTimer) window.clearTimeout(reconnectTimer);
    reconnectTimer = window.setTimeout(() => connect(), delay);
  }

  function disconnect() {
    manualClose = true;
    if (reconnectTimer) { window.clearTimeout(reconnectTimer); reconnectTimer = null; }
    if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) ws.close();
  if (simTimer) { window.clearInterval(simTimer); simTimer = null; }
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

  return { connect, disconnect, isConnected, isConnecting, error, latest, history, power, voltage, current, lastUpdated, sampleCount, minPower, maxPower, minVoltage, maxVoltage };
}

function buildWsUrl(ip: string) {
  if (/^wss?:\/\//i.test(ip)) return ip.replace(/\/$/, '');
  if (/^https?:\/\//i.test(ip)) {
    return ip.replace(/^http/i, 'ws').replace(/\/$/, '').replace(/:80$/, '') + ':81';
  }
  return 'ws://' + ip.replace(/\/$/, '') + ':81';
}
