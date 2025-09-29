import { ref, computed, watch, onBeforeUnmount } from 'vue';

export interface PowerSample {
  voltage: number;
  current?: number | null; // may be absent in legacy endpoint
  power: number; // W (from JSON or computed)
  timestamp: number; // epoch ms
  raw: any; // original JSON
}

interface UsePowerMonitorOptions {
  intervalMs?: number;      // base interval on success
  maxSamples?: number;
  timeoutMs?: number;       // fetch timeout
  maxBackoffMs?: number;    // cap backoff
}

export function usePowerMonitor(ipRef: () => string, options: UsePowerMonitorOptions = {}) {
  const intervalMs = ref(options.intervalMs ?? 500);
  const maxSamples = options.maxSamples ?? 1000;
  const timeoutMs = ref(options.timeoutMs ?? 1500);
  const maxBackoffMs = options.maxBackoffMs ?? 8000;

  const loading = ref(false);
  const error = ref<string | null>(null);
  const latest = ref<PowerSample | null>(null);
  const history = ref<PowerSample[]>([]);
  const isRunning = ref(false);

  // Stats
  const minPower = ref<number | null>(null);
  const maxPower = ref<number | null>(null);
  const minVoltage = ref<number | null>(null);
  const maxVoltage = ref<number | null>(null);

  // control variables
  const _nextTimer = ref<number | null>(null);
  const _inFlight = ref<boolean>(false);
  const _errorCount = ref(0);
  let _lastAbort: AbortController | null = null;

  function start() {
    if (isRunning.value) return;
    resetStats();
    isRunning.value = true;
    schedule(0); // immediate first
  }

  function stop() {
    isRunning.value = false;
    clearNext();
    if (_lastAbort) _lastAbort.abort();
    _inFlight.value = false;
  }

  function clearNext() {
    if (_nextTimer.value != null) {
      clearTimeout(_nextTimer.value);
      _nextTimer.value = null;
    }
  }

  function schedule(delay: number) {
    clearNext();
    if (!isRunning.value) return;
    _nextTimer.value = window.setTimeout(() => tick(), delay);
  }

  function resetStats() {
    history.value = [];
    minPower.value = maxPower.value = minVoltage.value = maxVoltage.value = null;
    _errorCount.value = 0;
  }

  async function tick() {
    if (!isRunning.value) return;
    if (_inFlight.value) return; // guard (should not happen with sequential scheduling)
    const ip = ipRef().trim();
    if (!ip) {
      error.value = 'IP fehlt';
      schedule(intervalMs.value);
      return;
    }

    _inFlight.value = true;
    loading.value = true;
    error.value = null;
    const abort = new AbortController();
    _lastAbort = abort;
    const to = window.setTimeout(() => abort.abort(), timeoutMs.value);

    let success = false;
    try {
      const url = normalizeBase(ip) + '/api/power';
      const resp = await fetch(url, { cache: 'no-store', signal: abort.signal });
      if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
      const data = await resp.json();
      let voltage: number | null = null;
      let current: number | null = null;
      let power: number | null = null;
      if (typeof data.voltage === 'number') voltage = data.voltage;
      if (typeof data.current === 'number') current = data.current;
      if (typeof data.power === 'number') power = data.power;
      if (power == null && voltage != null && current != null) power = voltage * current;
      if (voltage == null || power == null) throw new Error('Ungültiges JSON');

      const sample: PowerSample = { voltage, current, power, timestamp: Date.now(), raw: data };
      latest.value = sample;
      history.value.push(sample);
      if (history.value.length > maxSamples) history.value.shift();
      minPower.value = minPower.value == null ? power : Math.min(minPower.value, power);
      maxPower.value = maxPower.value == null ? power : Math.max(maxPower.value, power);
      minVoltage.value = minVoltage.value == null ? voltage : Math.min(minVoltage.value, voltage);
      maxVoltage.value = maxVoltage.value == null ? voltage : Math.max(maxVoltage.value, voltage);
      _errorCount.value = 0; // reset on success
      success = true;
    } catch (e: any) {
      error.value = e.name === 'AbortError' ? 'Timeout' : (e.message || String(e));
      _errorCount.value++;
    } finally {
      clearTimeout(to);
      loading.value = false;
      _inFlight.value = false;
      const backoff = success ? intervalMs.value : calcBackoff(_errorCount.value, intervalMs.value, maxBackoffMs);
      schedule(backoff);
    }
  }

  function calcBackoff(errors: number, base: number, cap: number) {
    if (errors <= 0) return base;
    const ms = Math.min(cap, base * Math.pow(2, Math.min(errors, 6))); // exponential up to cap
    return ms;
  }

  // React to interval changes (only affects next schedule after success)
  watch(intervalMs, (v) => {
    if (!isRunning.value) return;
    // if idle waiting, reschedule sooner if new interval is shorter
    if (_nextTimer.value != null) {
      const remaining = 0; // simplification, just restart timer
      schedule(Math.min(v, remaining));
    }
  });

  const lastUpdated = computed(() => latest.value?.timestamp ?? null);
  const power = computed(() => latest.value?.power ?? null);
  const voltage = computed(() => latest.value?.voltage ?? null);
  const current = computed(() => latest.value?.current ?? null);
  const rawJson = computed(() => latest.value?.raw ?? null);
  const sampleCount = computed(() => history.value.length);

  onBeforeUnmount(() => stop());

  return {
    // state
    loading, error, latest, history, isRunning,
    // controls
    start, stop,
    // config
    intervalMs, timeoutMs,
    // derived
    lastUpdated, power, voltage, current, rawJson, sampleCount,
    // stats
    minPower, maxPower, minVoltage, maxVoltage
  };
}

function normalizeBase(ip: string): string {
  if (/^https?:\/\//i.test(ip)) return ip.replace(/\/$/, '');
  return 'http://' + ip.replace(/\/$/, '');
}
