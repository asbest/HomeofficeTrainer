<template>
  <ion-page>
    <ion-header :translucent="true">
      <ion-toolbar color="success">
        <ion-title>HomeOffice Trainer</ion-title>
      </ion-toolbar>
    </ion-header>
    <ion-content :fullscreen="true" class="ion-padding">
      <div class="desktop-split">
        <section class="hero" :class="{ active: activeLatest }">
        <div class="power-display" v-if="activeLatest">
          <div class="power-value">
            <span class="num">{{ fmt(activePower,2) }}</span>
            <span class="unit">Watt</span>
            <div class="actual-line" v-if="displayPotential">Potenzial: {{ fmt(displayPotential,1) }} W<span v-if="rintSamples<3"> (lerne…)</span></div>
            <div v-if="displayEq" class="best-eq" :class="{ pinned: isPinned }">
              <div class="fill" v-if="isPinned" :style="{ width: fillWidth }"></div>
              <span class="icon">{{ displayEq.profile.icon }}</span>
              <span class="text" v-if="displayEq.factor >= 1">≈ {{ displayEq.factor.toFixed(1) }}× {{ displayEq.profile.label }}</span>
              <span class="text" v-else>≈ {{ (displayEq.factor*100).toFixed(0) }}% {{ displayEq.profile.label }}</span>
            </div>
          </div>
          <div class="sub-metrics">
            <div class="mini-block">
              <span class="label">Spannung</span>
              <span class="val">{{ fmt(activeVoltage,2) }} V</span>
            </div>
            <div class="mini-block">
              <span class="label">Samples</span>
              <span class="val">{{ activeSampleCount }}</span>
            </div>
              <div class="mini-block" v-if="activeMinPower!=null">
                <span class="label">Min</span>
                <span class="val">{{ fmt(activeMinPower,1) }}</span>
              </div>
              <div class="mini-block" v-if="activeMaxPower!=null">
                <span class="label">Max</span>
                <span class="val">{{ fmt(activeMaxPower,1) }}</span>
              </div>
            </div>
          </div>
          <div v-else class="hero-placeholder">Verbinden mit Trainer<br/>Verbinde dich zuerst mit dem WLAN <strong>homeofficetrainer</strong> und drücke dann <em>Connect</em>.</div>
          <div class="hero-status">
            <div class="status-pill" :class="statusClass">
              <span class="dot" :class="statusDot"></span>
              <span class="text">{{ statusText }}</span>
            </div>
            <div v-if="activeRunning && rintSamples < 3" class="learn-badge" :title="'Modell lernt… ('+Math.min(rintSamples,3)+'/3)'">
              <span class="icon">🧠</span>
              <span class="num">{{ Math.min(rintSamples,3) }}/3</span>
            </div>
          </div>
        </section>

        <div class="right-col">
          <section v-if="activeConnected" class="equivalents-section">
            <h2 class="section-title">Womit vergleichbar?</h2>
            <div class="eq-grid">
              <div
                v-for="eq in eqList"
                :key="eq.profile.id"
                class="eq-item"
                :class="{ active: selectedEqId === eq.profile.id }"
                @click="selectEq(eq.profile.id)"
              >
                <div class="icon">{{ eq.profile.icon }}</div>
                <div class="label">{{ eq.profile.label }}</div>
                <div v-if="eq.factor!=null" class="factor" :class="{ muted: !activeLatest }">
                  <template v-if="eq.factor >= 1">≈ {{ eq.factor.toFixed(1) }}x</template>
                  <template v-else>≈ {{ (eq.factor*100).toFixed(0) }}%</template>
                </div>
              </div>
            </div>
          </section>

          <ion-accordion-group expand="inset" class="settings-acc">
            <ion-accordion value="settings">
              <ion-item slot="header" color="success">
                <ion-label>Einstellungen & Verbindung</ion-label>
              </ion-item>
              <div class="accordion-inner" slot="content">
                <div class="controls single">
                  <ion-button size="default" :disabled="activeRunning" @click="startMonitoring">Connect</ion-button>
                  <ion-button size="default" color="tertiary" :disabled="activeRunning" @click="startDemo">Demo</ion-button>
                  <ion-button size="default" color="medium" :disabled="!activeRunning" @click="stopMonitoring">Stop</ion-button>
                </div>
                <div class="endpoint muted small-line">Endpoint: <span>{{ endpointPreview }}</span></div>
                <ion-card class="raw-card" v-if="activeRawJson">
                  <ion-card-header>
                    <ion-card-title>Rohdaten</ion-card-title>
                    <ion-card-subtitle>
                      <ion-button size="small" fill="clear" @click="copyJson">Kopieren</ion-button>
                    </ion-card-subtitle>
                  </ion-card-header>
                  <ion-card-content>
                    <pre class="json">{{ prettyRaw }}</pre>
                  </ion-card-content>
                </ion-card>
                <ion-card class="raw-card diag-card" v-if="diagEvents.length">
                  <ion-card-header>
                    <ion-card-title>Diagnostik</ion-card-title>
                    <ion-card-subtitle>
                      <span class="diag-meta">Attempts: {{ sock.attempts.value }} | First msg Latenz: {{ sock.lastFirstMessageLatency.value ?? '—' }} ms | Ping RTT: {{ sock.lastPingRtt.value ? sock.lastPingRtt.value.toFixed(1)+' ms' : '—' }}</span>
                    </ion-card-subtitle>
                  </ion-card-header>
                  <ion-card-content>
                    <div class="diag-log">
                      <div v-for="e in diagEvents" :key="e.t+e.type" class="line">
                        <span class="ts">{{ formatTs(e.t) }}</span>
                        <span class="etype">{{ e.type }}</span>
                        <span class="detail" v-if="e.detail">{{ summarizeDetail(e.detail) }}</span>
                      </div>
                    </div>
                  </ion-card-content>
                </ion-card>
              </div>
            </ion-accordion>
          </ion-accordion-group>

          <div v-if="activeError" class="error-msg">Fehler: {{ activeError }}</div>
        </div>
      </div> <!-- end desktop-split -->

      <PowerChart v-if="activeHistory.length > 1" :samples="chartSamples" class="chart-section" />
      <ion-progress-bar v-if="activeLoading" type="indeterminate" />
    </ion-content>
  </ion-page>
</template>

 <script setup lang="ts">
import { ref, computed, watch, onMounted } from 'vue';
import { IonContent, IonHeader, IonPage, IonTitle, IonToolbar, IonButton, IonCard, IonCardHeader, IonCardTitle, IonCardSubtitle, IonCardContent, IonProgressBar, IonAccordionGroup, IonAccordion, IonItem, IonLabel } from '@ionic/vue';
import { usePowerSocket } from '@/composables/usePowerSocket';
import PowerChart from '@/components/PowerChart.vue';
// Removed unused equivalents import
// import { equivalents } from '@/data/powerProfiles';

// Fixed AP host (open AP mode): 192.168.4.1
const FIXED_HOST = '192.168.4.1';
// Active host (can switch to 'demo')
const host = ref<string>(FIXED_HOST);

// Polling composable
// WebSocket composable
const sock = usePowerSocket(() => host.value, {});

// Legacy placeholder to keep template minimal changes (not used)
const intervalMsLocal = ref(0);

function startMonitoring(){ host.value = FIXED_HOST; sock.connect(); statusText.value='Verbinden…'; }
function startDemo(){ host.value = 'demo'; sock.connect(); statusText.value='Demo läuft'; }
function stopMonitoring(){ sock.disconnect(); host.value = FIXED_HOST; statusText.value='Gestoppt'; }
const endpointPreview = computed(() => host.value.toLowerCase() === 'demo' ? 'Demo-Modus' : (sock.lastUrl?.value || `ws://${host.value}/ws`));

// Unified reactive projections
const activeLatest = computed(()=> sock.latest.value);
const activePower = computed(()=> sock.power.value);
// Potential power model values
const potentialPower = computed(()=> sock.potentialPower?.value ?? null);
const rintSamples = computed(()=> sock.rintSamples?.value ?? 0);
const displayPotential = computed(()=> potentialPower.value && rintSamples.value >= 3 ? potentialPower.value : null);
const activeVoltage = computed(()=> sock.voltage.value);
const activeMinPower = computed(()=> sock.minPower.value);
const activeMaxPower = computed(()=> sock.maxPower.value);
const activeMinVoltage = computed(()=> sock.minVoltage.value);
const activeMaxVoltage = computed(()=> sock.maxVoltage.value);
const activeLastUpdated = computed(()=> sock.lastUpdated.value);
const activeSampleCount = computed(()=> sock.sampleCount.value);
const activeRawJson = computed(()=> sock.latest.value?.raw ?? null);
const activeError = computed(()=> sock.error.value);
const activeHistory = computed(()=> sock.history.value);
const activeRunning = computed(()=> sock.isConnected.value || sock.isConnecting.value);
const activeLoading = computed(()=> (!sock.isConnected.value && sock.isConnecting.value));
const activeConnected = computed(()=> sock.isConnected.value);

// Status pill
const statusText = ref('Bereit');
const statusDot = computed(()=> {
  if (activeError.value) return 'bad';
  return activeRunning.value ? 'ok' : 'idle';
});
const statusClass = computed(()=> ({ running: activeRunning.value }));

// Auto status updates
watch(() => sock.isConnected.value, (v) => {
  if (v) statusText.value = 'Verbunden';
});
watch(() => sock.latest.value, (v) => {
  if (v && statusText.value.startsWith('Verbinden')) statusText.value = 'Verbunden';
});
watch(activeError, (e) => { if (e) statusText.value = 'Fehler'; });

// Formatting helpers
function fmt(n: any, dec = 2){ if(n==null || Number.isNaN(Number(n))) return '—'; return Number(n).toFixed(dec); }
const prettyRaw = computed(() => activeRawJson.value ? JSON.stringify(activeRawJson.value, null, 2) : '—');
const chartSamples = computed(() => activeHistory.value.map((s: any) => ({ power: s.power, voltage: s.voltage, timestamp: s.timestamp })));

import { powerProfiles } from '@/data/powerProfiles';
const eqListFull = computed(()=> {
  const p = activePower.value;
  // Always show all profiles ordered by required watts; compute factor from current power if available
  return [...powerProfiles]
    .sort((a,b) => a.watts - b.watts)
    .map(pr => ({ profile: pr, factor: (p && pr.watts) ? (p / pr.watts) : null as any }));
});
const autoEq = computed(()=> {
  const p = activePower.value;
  if (!p || p <= 0) return null;
  const list = eqListFull.value.filter(e => typeof e.factor === 'number' && !Number.isNaN(e.factor));
  if (!list.length) return null;
  let closest = list[0];
  let diff = Math.abs((list[0].factor as number) - 1);
  for (let i=1;i<list.length;i++) {
    const d = Math.abs((list[i].factor as number) - 1);
    if (d < diff) { diff = d; closest = list[i]; }
  }
  return closest;
});
const selectedEqId = ref<string | null>(null);
function selectEq(id: string) {
  if (selectedEqId.value === id) {
    // deselect -> return to automatic mode
    selectedEqId.value = null;
  } else {
    selectedEqId.value = id;
  }
}
const displayEq = computed(()=> {
  if (selectedEqId.value) {
    return eqListFull.value.find(e => e.profile.id === selectedEqId.value) || null;
  }
  return autoEq.value;
});
const eqList = computed(()=> eqListFull.value);
const isPinned = computed(()=> !!selectedEqId.value && displayEq.value && selectedEqId.value === displayEq.value.profile.id);
// fill percent: clamp factor (<=1) to percent, if over 1, show 100%
const fillWidth = computed(()=> {
  if (!displayEq.value) return '0%';
  const f = displayEq.value.factor;
  if (f >= 1) return '100%';
  return `${Math.min(100, Math.max(0, f*100)).toFixed(1)}%`;
});

async function copyJson(){ if (!activeRawJson.value) return; try { await navigator.clipboard.writeText(JSON.stringify(activeRawJson.value, null, 2)); statusText.value = 'JSON kopiert'; } catch { statusText.value = 'Copy Fehler'; } }

// Diagnostics mapping
const diagEvents = computed(()=> (sock.eventLog?.value || []).slice(-60));
function formatTs(t:number){
  const d = new Date(t); return d.toLocaleTimeString();
}
function summarizeDetail(d:any){
  if (!d) return '';
  if (d.latencyMs) return `lat=${d.latencyMs}ms`;
  if (d.rtt) return `rtt=${d.rtt.toFixed(1)}ms`;
  if (d.delay) return `retry in ${d.delay}ms (#${d.attempt})`;
  if (d.code!=null) return `code=${d.code}${d.reason? ' '+d.reason:''}`;
  if (d.url) return d.url;
  return '';
}
</script>

<style scoped>
.hero { position:relative; margin:-16px -16px 24px; padding:48px 24px 56px; text-align:center; background: radial-gradient(circle at 40% 35%, var(--ion-color-primary) 0%, var(--ion-color-primary-shade) 70%); color:#fff; transition:0.4s background; display:flex; flex-direction:column; justify-content:center; }
.hero.active { background: radial-gradient(circle at 60% 40%, var(--ion-color-secondary) 0%, var(--ion-color-tertiary) 70%); }
.hero-placeholder { font-size:0.95rem; opacity:0.85; line-height:1.4; }
.power-display { display:flex; flex-direction:column; align-items:center; gap:28px; }
.power-value { font-size: clamp(3.2rem, 9vw, 5.5rem); font-weight:800; letter-spacing:-2px; display:flex; flex-direction:column; line-height:0.9; }
.power-value .unit { font-size: clamp(0.9rem,2vw,1.3rem); font-weight:500; letter-spacing:0; margin-top:12px; opacity:0.9; }
.power-value .actual-line { margin-top:10px; font-size:0.95rem; font-weight:500; letter-spacing: 0; opacity:0.9; }
.power-value .best-eq { margin-top:14px; display:flex; align-items:center; gap:10px; justify-content:center; font-size:0.9rem; font-weight:600; background:rgba(255,255,255,0.12); padding:8px 14px 9px; border-radius:999px; backdrop-filter:blur(6px); position:relative; overflow:hidden; }
.power-value .best-eq.pinned { background:rgba(34,197,94,0.18); }
.power-value .best-eq .fill { position:absolute; left:0; top:0; bottom:0; background:linear-gradient(90deg,#16a34a,#22c55e); opacity:0.55; pointer-events:none; transition:width .6s cubic-bezier(.4,.0,.2,1); }
.power-value .best-eq.pinned .text, .power-value .best-eq.pinned .icon { position:relative; z-index:1; }
.power-value .best-eq .icon { font-size:1.4rem; line-height:1; filter:drop-shadow(0 2px 4px rgba(0,0,0,0.4)); }
.power-value .best-eq .text { white-space:nowrap; letter-spacing:.5px; }
.sub-metrics { display:grid; grid-template-columns:repeat(auto-fit,minmax(90px,1fr)); gap:12px; width:100%; max-width:640px; }
.mini-block { background:rgba(255,255,255,0.12); padding:10px 8px 12px; border-radius:12px; backdrop-filter: blur(4px); }
.mini-block .label { display:block; font-size:11px; text-transform:uppercase; letter-spacing:1px; opacity:.7; }
.mini-block .val { font-size:1.05rem; font-weight:600; margin-top:4px; font-variant-numeric: tabular-nums; }
.hero-status { position:absolute; top:12px; right:16px; }
.learn-badge { display:inline-flex; align-items:center; gap:6px; margin-left:10px; padding:4px 8px; border-radius:999px; background:rgba(0,0,0,0.28); color:#fff; font-size:11px; border:1px solid var(--ion-color-step-200,#2e2e2e); vertical-align:middle; }
.learn-badge .icon { font-size:13px; line-height:1; filter:drop-shadow(0 1px 2px rgba(0,0,0,0.4)); }
.learn-badge .num { font-weight:600; font-variant-numeric: tabular-nums; opacity:0.9; }

.settings-acc { margin-top:16px; margin-bottom:8px; }
.accordion-inner { padding:16px 12px 8px; }
.small-line { font-size:11px; margin-top:6px; }

.equivalents-section { margin-top:8px; }
.section-title { font-size:1.05rem; font-weight:600; letter-spacing:.5px; margin:8px 4px 12px; }
.eq-grid { display:grid; grid-template-columns:repeat(auto-fill,minmax(110px,1fr)); gap:14px; }
.eq-item { background:var(--ion-color-step-100,#202020); padding:14px 10px 16px; border-radius:18px; display:flex; flex-direction:column; align-items:center; position:relative; overflow:hidden; min-height:130px; box-shadow:0 4px 10px -4px rgba(0,0,0,0.5); cursor:pointer; transition:.25s box-shadow, .25s transform, .25s background; }
.eq-item:hover { box-shadow:0 6px 14px -4px rgba(0,0,0,0.65); transform:translateY(-2px); }
.eq-item.active { outline:2px solid var(--ion-color-primary); box-shadow:0 0 0 2px rgba(0,166,82,0.6), 0 8px 18px -6px rgba(0,0,0,0.65); background:linear-gradient(135deg,var(--ion-color-tertiary),var(--ion-color-primary-shade)); }
.eq-item .icon { font-size:2.2rem; filter:drop-shadow(0 3px 4px rgba(0,0,0,0.4)); }
.eq-item .label { margin-top:10px; text-align:center; font-size:.75rem; line-height:1.1; font-weight:500; }
.eq-item .factor { margin-top:6px; font-size:.7rem; padding:4px 8px; background:linear-gradient(90deg,var(--ion-color-primary),var(--ion-color-secondary)); border-radius:999px; color:#fff; font-weight:600; letter-spacing:.5px; }
.placeholder.subtle { opacity:.35; }

/* Reuse existing status pill but adjust inside hero */
.status-pill { display:flex; align-items:center; gap:6px; padding:6px 12px; border-radius:999px; border:1px solid var(--ion-color-step-200,#2e2e2e); font-size:12px; background:rgba(0,0,0,0.35); color:#fff; }
.status-pill .dot { width:8px; height:8px; border-radius:50%; background:#a3a3a3; }
.status-pill .dot.ok { background:#34d399; }
.status-pill .dot.bad { background:#f87171; }
.status-pill .dot.idle { background:#a3a3a3; }

/* Raw JSON panel inside accordion */
.raw-card pre.json { background: var(--ion-color-step-100,#222); padding:12px; border-radius:8px; font-size:12px; overflow:auto; max-height:220px; }

.controls { display:flex; flex-wrap:wrap; gap:12px; align-items:center; }
.controls.single { gap:16px; }
/* removed field styles (IP input gone) */

.chart-section { margin-top:28px; }

/* Simple toolbar (inherits Ionic dark color) */

@media (min-width: 900px) {
  .desktop-split { display:grid; grid-template-columns: minmax(420px, 54%) 1fr; gap: 32px; align-items: stretch; }
  .hero { margin:-16px 0 32px; border-radius:32px; height:100%; min-height:100%; padding-bottom:48px; }
  .equivalents-section { margin-top:0; padding-top:8px; }
  .eq-grid { gap:16px; grid-template-columns:repeat(auto-fill,minmax(140px,1fr)); }
  .eq-item { min-height:150px; }
  .power-value { font-size: clamp(4rem,6vw,6rem); }
  .right-col { margin-top:-8px; }
  .right-col .settings-acc { margin-top:20px; }
  .right-col .placeholder.subtle { margin-top:12px; }
}

@media (min-width: 1300px) {
  .desktop-split { grid-template-columns: minmax(520px, 50%) 1fr; gap:48px; }
  .eq-grid { gap:20px; }
}
</style>
