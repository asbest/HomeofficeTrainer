<template>
  <div class="chart-wrap">
    <canvas ref="canvas" class="chart" />
  </div>
</template>

<script setup lang="ts">
import { onMounted, onBeforeUnmount, ref, watch, computed } from 'vue';

interface Sample { power: number; voltage: number; timestamp: number; }
const props = defineProps<{ samples: Sample[]; maxPoints?: number }>();
const canvas = ref<HTMLCanvasElement | null>(null);
const maxPoints = computed(() => props.maxPoints ?? 240);
let ctx: CanvasRenderingContext2D | null = null;

function resize() {
  if (!canvas.value) return;
  const dpr = Math.max(1, window.devicePixelRatio || 1);
  const rect = canvas.value.getBoundingClientRect();
  canvas.value.width = Math.floor(rect.width * dpr);
  canvas.value.height = Math.floor(rect.height * dpr);
  ctx = canvas.value.getContext('2d');
  if (ctx) ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  draw();
}

function draw() {
  if (!ctx || !canvas.value) return;
  const localCtx = ctx; // non-null assertion handled by guard
  const data = props.samples.slice(-maxPoints.value);
  const W = canvas.value.width / (window.devicePixelRatio || 1);
  const H = canvas.value.height / (window.devicePixelRatio || 1);
  localCtx.clearRect(0,0,W,H);
  // grid
  localCtx.save();
  localCtx.globalAlpha = 0.5;
  localCtx.lineWidth = 1;
  localCtx.strokeStyle = '#1a3d2a';
  for (let x=0; x<=W; x+=90){ localCtx.beginPath(); localCtx.moveTo(x,0); localCtx.lineTo(x,H); localCtx.stroke(); }
  for (let y=0; y<=H; y+=48){ localCtx.beginPath(); localCtx.moveTo(0,y); localCtx.lineTo(W,y); localCtx.stroke(); }
  localCtx.restore();
  if (data.length < 2) return;
  const pVals = data.map(d=>d.power);
  const vVals = data.map(d=>d.voltage);
  const pMin = Math.min(...pVals), pMax = Math.max(...pVals);
  const vMin = Math.min(...vVals), vMax = Math.max(...vVals);
  const pad = 8;
  const yScale = (val:number,min:number,max:number)=> max===min ? H/2 : H - pad - ((val-min)/(max-min)) * (H-2*pad);
  // power
  localCtx.lineWidth = 2; localCtx.strokeStyle = '#6ee7b7'; localCtx.beginPath();
  data.forEach((d,i)=>{ const x = (i/(data.length-1))*W; const y=yScale(d.power,pMin,pMax); if(i===0) localCtx.moveTo(x,y); else localCtx.lineTo(x,y); }); localCtx.stroke();
  // voltage
  localCtx.lineWidth = 2; localCtx.strokeStyle = '#4ade80'; localCtx.beginPath();
  data.forEach((d,i)=>{ const x = (i/(data.length-1))*W; const y=yScale(d.voltage,vMin,vMax); if(i===0) localCtx.moveTo(x,y); else localCtx.lineTo(x,y); }); localCtx.stroke();
  // legends
  localCtx.fillStyle = '#8a93a6'; localCtx.font = '12px ui-monospace, monospace';
  localCtx.fillText(`Power (min ${pMin.toFixed(2)} / max ${pMax.toFixed(2)})`, 10, 16);
  localCtx.fillText(`Voltage (min ${vMin.toFixed(3)} / max ${vMax.toFixed(3)})`, 10, 32);
}

let raf: number | null = null;
function schedule(){ if (raf) cancelAnimationFrame(raf); raf = requestAnimationFrame(draw); }

watch(() => props.samples.length, schedule);
window.addEventListener('resize', resize);

onMounted(() => { resize(); });
onBeforeUnmount(() => { window.removeEventListener('resize', resize); if (raf) cancelAnimationFrame(raf); });
</script>

<style scoped>
.chart-wrap { width: 100%; height: 240px; background:#0d1f15; border:1px solid var(--ion-color-step-150,#1a3d2a); border-radius:14px; overflow:hidden; }
.chart { width:100%; height:100%; display:block; }
</style>
