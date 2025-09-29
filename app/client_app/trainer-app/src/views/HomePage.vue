<template>
  <ion-page>
    <ion-header :translucent="true">
      <ion-toolbar color="dark">
        <ion-title>HomeOffice Trainer</ion-title>
      </ion-toolbar>
    </ion-header>
    <ion-content :fullscreen="true" class="ion-padding">
      <section class="hero" :class="{ active: activeLatest }">
        <div class="power-display" v-if="activeLatest">
          <div class="power-value">
            <span class="num">{{ fmt(activePower,2) }}</span>
            <span class="unit">Watt</span>
            <div v-if="displayEq" class="best-eq">
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
        <div v-else class="hero-placeholder">Noch keine Messung<br/>IP eingeben & Start.</div>
        <div class="hero-status">
          <div class="status-pill" :class="statusClass">
            <span class="dot" :class="statusDot"></span>
            <span class="text">{{ statusText }}</span>
          </div>
        </div>
      </section>

      <ion-accordion-group expand="inset" class="settings-acc">
        <ion-accordion value="settings">
          <ion-item slot="header" color="dark">
            <ion-label>Einstellungen & Verbindung</ion-label>
          </ion-item>
          <div class="accordion-inner" slot="content">
            <div class="controls">
              <div class="field">
                <label>ESP IP-Adresse</label>
                <ion-input v-model="ip" placeholder="192.168.x.x" @keyup.enter="startMonitoring" />
              </div>
              <!-- Polling + Mode selector removed -->
              <ion-button size="default" :disabled="!validIp || activeRunning" @click="startMonitoring">Start</ion-button>
              <ion-button size="default" color="medium" :disabled="!activeRunning" @click="stopMonitoring">Stop</ion-button>
            </div>
            <div class="endpoint muted small-line">{{ endpointLabel }}: <span>{{ endpointPreview }}</span></div>
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
          </div>
        </ion-accordion>
      </ion-accordion-group>

      <div v-if="activeError" class="error-msg">Fehler: {{ activeError }}</div>

      <section v-if="activeLatest" class="equivalents-section">
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
            <div class="factor" v-if="eq.factor >= 1">≈ {{ eq.factor.toFixed(1) }}x</div>
            <div class="factor" v-else>≈ {{ (eq.factor*100).toFixed(0) }}%</div>
          </div>
        </div>
      </section>

      <div v-else class="placeholder subtle">Noch keine Daten.</div>

      <PowerChart v-if="activeHistory.length > 1" :samples="chartSamples" class="chart-section" />

      <ion-progress-bar v-if="activeLoading" type="indeterminate" />
    </ion-content>
  </ion-page>
</template>

<script setup lang="ts">
import { ref, computed, watch, onMounted } from 'vue';
import { IonContent, IonHeader, IonPage, IonTitle, IonToolbar, IonInput, IonButton, IonCard, IonCardHeader, IonCardTitle, IonCardSubtitle, IonCardContent, IonProgressBar, IonAccordionGroup, IonAccordion, IonItem, IonLabel } from '@ionic/vue';
import { usePowerSocket } from '@/composables/usePowerSocket';
import PowerChart from '@/components/PowerChart.vue';
import { equivalents } from '@/data/powerProfiles';

const ip = ref('homeofficetrainer');
const STORAGE_KEY_IP = 'esp_ip';
// Removed interval & mode storage keys

// WebSocket only now
// Pre-filled hostname for WebSocket only setup

onMounted(() => {
  const savedIp = localStorage.getItem(STORAGE_KEY_IP);
  if (savedIp) ip.value = savedIp;
});
watch(ip, (v) => { if (v) localStorage.setItem(STORAGE_KEY_IP, v); else localStorage.removeItem(STORAGE_KEY_IP); });

// Polling composable
// WebSocket composable
const sock = usePowerSocket(() => ip.value, {});

// Legacy placeholder to keep template minimal changes (not used)
const intervalMsLocal = ref(0);

function startMonitoring(){ if (!validIp.value) return; sock.connect(); statusText.value='Verbunden'; }
function stopMonitoring(){ sock.disconnect(); statusText.value='Gestoppt'; }

const validIp = computed(() => ip.value.trim().length > 0);
const endpointLabel = computed(()=> 'WebSocket');
const endpointPreview = computed(() => validIp.value ? `ws://${ip.value.trim()}:81` : '—');

// Unified reactive projections
const activeLatest = computed(()=> sock.latest.value);
const activePower = computed(()=> sock.power.value);
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

// Status pill
const statusText = ref('Bereit');
const statusDot = computed(()=> {
  if (activeError.value) return 'bad';
  return activeRunning.value ? 'ok' : 'idle';
});
const statusClass = computed(()=> ({ running: activeRunning.value }));

// Formatting helpers
function fmt(n: any, dec = 2){ if(n==null || Number.isNaN(Number(n))) return '—'; return Number(n).toFixed(dec); }
const prettyRaw = computed(() => activeRawJson.value ? JSON.stringify(activeRawJson.value, null, 2) : '—');
const chartSamples = computed(() => activeHistory.value.map((s: any) => ({ power: s.power, voltage: s.voltage, timestamp: s.timestamp })));

const eqListFull = computed(()=> equivalents(activePower.value||0));
const autoEq = computed(()=> {
  const list = eqListFull.value;
  if (!list.length) return null;
  let closest = list[0];
  let diff = Math.abs(list[0].factor - 1);
  for (let i=1;i<list.length;i++) {
    const d = Math.abs(list[i].factor - 1);
    if (d < diff) { diff = d; closest = list[i]; }
  }
  return closest;
});
const selectedEqId = ref<string | null>(null);
function selectEq(id: string) { selectedEqId.value = id; }
const displayEq = computed(()=> {
  if (selectedEqId.value) {
    return eqListFull.value.find(e => e.profile.id === selectedEqId.value) || null;
  }
  return autoEq.value;
});
const eqList = computed(()=> eqListFull.value.slice(0,12));

async function copyJson(){ if (!activeRawJson.value) return; try { await navigator.clipboard.writeText(JSON.stringify(activeRawJson.value, null, 2)); statusText.value = 'JSON kopiert'; } catch { statusText.value = 'Copy Fehler'; } }
</script>

<style scoped>
.hero { position:relative; margin:-16px -16px 24px; padding:48px 24px 56px; text-align:center; background: radial-gradient(circle at 40% 35%, #3b82f6 0%, #1e3a8a 70%); color:#fff; transition:0.4s background; }
.hero.active { background: radial-gradient(circle at 60% 40%, #06b6d4 0%, #0f172a 70%); }
.hero-placeholder { font-size:0.95rem; opacity:0.85; line-height:1.4; }
.power-display { display:flex; flex-direction:column; align-items:center; gap:28px; }
.power-value { font-size: clamp(3.2rem, 9vw, 5.5rem); font-weight:800; letter-spacing:-2px; display:flex; flex-direction:column; line-height:0.9; }
.power-value .unit { font-size: clamp(0.9rem,2vw,1.3rem); font-weight:500; letter-spacing:0; margin-top:12px; opacity:0.9; }
.power-value .best-eq { margin-top:14px; display:flex; align-items:center; gap:10px; justify-content:center; font-size:0.9rem; font-weight:600; background:rgba(255,255,255,0.12); padding:8px 14px 9px; border-radius:999px; backdrop-filter:blur(6px); }
.power-value .best-eq .icon { font-size:1.4rem; line-height:1; filter:drop-shadow(0 2px 4px rgba(0,0,0,0.4)); }
.power-value .best-eq .text { white-space:nowrap; letter-spacing:.5px; }
.sub-metrics { display:grid; grid-template-columns:repeat(auto-fit,minmax(90px,1fr)); gap:12px; width:100%; max-width:640px; }
.mini-block { background:rgba(255,255,255,0.12); padding:10px 8px 12px; border-radius:12px; backdrop-filter: blur(4px); }
.mini-block .label { display:block; font-size:11px; text-transform:uppercase; letter-spacing:1px; opacity:.7; }
.mini-block .val { font-size:1.05rem; font-weight:600; margin-top:4px; font-variant-numeric: tabular-nums; }
.hero-status { position:absolute; top:12px; right:16px; }

.settings-acc { margin-top:-8px; margin-bottom:8px; }
.accordion-inner { padding:16px 12px 8px; }
.small-line { font-size:11px; margin-top:6px; }

.equivalents-section { margin-top:8px; }
.section-title { font-size:1.05rem; font-weight:600; letter-spacing:.5px; margin:8px 4px 12px; }
.eq-grid { display:grid; grid-template-columns:repeat(auto-fill,minmax(110px,1fr)); gap:14px; }
.eq-item { background:var(--ion-color-step-100,#202020); padding:14px 10px 16px; border-radius:18px; display:flex; flex-direction:column; align-items:center; position:relative; overflow:hidden; min-height:130px; box-shadow:0 4px 10px -4px rgba(0,0,0,0.5); cursor:pointer; transition:.25s box-shadow, .25s transform, .25s background; }
.eq-item:hover { box-shadow:0 6px 14px -4px rgba(0,0,0,0.65); transform:translateY(-2px); }
.eq-item.active { outline:2px solid #3b82f6; box-shadow:0 0 0 2px rgba(59,130,246,0.6), 0 8px 18px -6px rgba(0,0,0,0.65); background:linear-gradient(135deg,#1e3a8a,#0f172a); }
.eq-item .icon { font-size:2.2rem; filter:drop-shadow(0 3px 4px rgba(0,0,0,0.4)); }
.eq-item .label { margin-top:10px; text-align:center; font-size:.75rem; line-height:1.1; font-weight:500; }
.eq-item .factor { margin-top:6px; font-size:.7rem; padding:4px 8px; background:linear-gradient(90deg,#2563eb,#0891b2); border-radius:999px; color:#fff; font-weight:600; letter-spacing:.5px; }
.placeholder.subtle { opacity:.35; }

/* Reuse existing status pill but adjust inside hero */
.status-pill { display:flex; align-items:center; gap:6px; padding:6px 12px; border-radius:999px; border:1px solid var(--ion-color-step-200,#2e2e2e); font-size:12px; background:rgba(0,0,0,0.35); color:#fff; }
.status-pill .dot { width:8px; height:8px; border-radius:50%; background:#a3a3a3; }
.status-pill .dot.ok { background:#34d399; }
.status-pill .dot.bad { background:#f87171; }
.status-pill .dot.idle { background:#a3a3a3; }

/* Raw JSON panel inside accordion */
.raw-card pre.json { background: var(--ion-color-step-100,#222); padding:12px; border-radius:8px; font-size:12px; overflow:auto; max-height:220px; }

.controls { display:flex; flex-wrap:wrap; gap:12px; align-items:flex-end; }
.field { display:flex; flex-direction:column; min-width:160px; }
.field.small { max-width:140px; }
.field label { font-size:12px; opacity:.7; margin-bottom:4px; }

.chart-section { margin-top:28px; }

/* Simple toolbar (inherits Ionic dark color) */

@media (min-width: 900px) {
  .hero { margin:-16px -32px 32px; border-bottom-left-radius:32px; border-bottom-right-radius:32px; }
  .eq-grid { gap:18px; }
  .eq-item { min-height:150px; }
  .power-value { font-size: clamp(4rem,7vw,6rem); }
}
</style>
