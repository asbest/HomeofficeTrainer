// Approximate wattage examples for relatable comparisons.
// You can adjust or localize labels freely.
export interface PowerProfile {
  id: string;
  label: string;
  watts: number; // typical consumption
  icon: string;  // large emoji/icon for quick visual mapping
  details?: string;
}

export const powerProfiles: PowerProfile[] = [
  { id: 'led-bulb', label: 'LED Lampe', watts: 8, icon: '💡', details: 'LED ~8W' },
  { id: 'phone-charge', label: 'Smartphone laden', watts: 12, icon: '📱', details: 'Ladegerät 5V/2A' },
  { id: 'wifi-router', label: 'WLAN Router', watts: 15, icon: '📶', details: 'Kleinrouter' },
  { id: 'notebook', label: 'Laptop leicht', watts: 45, icon: '💻', details: 'Office Nutzung' },
  { id: 'monitor', label: 'Monitor 24"', watts: 30, icon: '🖥️', details: 'LED Panel' },
  { id: 'raspi4', label: 'Raspberry Pi 4', watts: 7, icon: '🧪', details: 'Idle ~3-7W' },
  { id: 'speaker', label: 'Bluetooth Speaker', watts: 10, icon: '🔊', details: 'Laden/Playback' },
  { id: 'gaming-laptop', label: 'Gaming Laptop', watts: 120, icon: '🎮', details: 'Durchschnitt Last' },
  { id: 'drill', label: 'Akkuschrauber Laden', watts: 60, icon: '🛠️' },
  { id: 'tv', label: 'TV 42"', watts: 75, icon: '📺' },
  { id: 'coffee', label: 'Kaffeemaschine', watts: 1000, icon: '☕', details: 'Aufheizen Spitze' },
  { id: 'toaster', label: 'Toaster', watts: 800, icon: '🍞' },
  { id: 'fan', label: 'Ventilator', watts: 40, icon: '🌀' },
  { id: 'fridge', label: 'Kühlschrank Ø', watts: 150, icon: '🧊' },
  { id: 'console', label: 'Spielkonsole', watts: 90, icon: '🕹️' },
];

export function equivalents(currentWatts: number) {
  if (!currentWatts || currentWatts <= 0) return [] as {profile: PowerProfile; factor: number}[];
  return powerProfiles
    .map(p => ({ profile: p, factor: currentWatts / p.watts }))
    .filter(e => e.factor >= 0.2) // drop items that are far larger than current power
    .sort((a,b) => a.factor - b.factor);
}
