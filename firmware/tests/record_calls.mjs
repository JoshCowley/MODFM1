// Records what the FM1 Rack page sends the engine for every factory preset, and what its FM-1 preview
// predicts (voices that fit in RAM and modules it unloads), using Playwright and Chromium.
//   npm install playwright && npx playwright install chromium    (once)
//   node record_calls.mjs [http://localhost:8000/]   with web/ served, e.g. python3 -m http.server 8000 in web/
// Writes presets-calls.json and page-fit.json next to this file. Then run make_trace.mjs.
import { createRequire } from 'module';
const { chromium } = createRequire(import.meta.url)('playwright');   // npm install playwright
import fs from 'fs';
const here = new URL('.', import.meta.url).pathname;
const url = process.argv[2] || 'http://localhost:8000/';
const b = await chromium.launch(); const pg = await b.newPage();
await pg.goto(url); await pg.waitForTimeout(1500);
// the page keeps its code private; this exposes it the way the debug build does
const src = await (await fetch(url)).text().catch(() => null);
const i = src.lastIndexOf('})();');
await pg.setContent(src.slice(0, i) + 'window.__dbg=(f)=>eval(f);' + src.slice(i), { waitUntil: 'load' });
await pg.waitForTimeout(1500);
const out = await pg.evaluate(() => window.__dbg(`(() => {
  const calls = [], fit = [];
  for (let i = 0; i < PRESETS.length; i++) {
    glo.p[G_TARGET] = 1;   // record as the browser (6 voices); the FM-1 build decides for itself
    const cap = []; audio.node = { port: { postMessage: (m) => { if (m.type === "calls") cap.push(...m.calls); } } };
    for (const k of Object.keys(sent)) delete sent[k];
    loadPreset(patchList(true).findIndex((e) => e.f === i)); syncEngine();
    calls.push({ name: PRESETS[i].name, calls: cap.filter((c) => c[0] !== "set_voices"), voices: budget().voices, mode: amp.p[A_MODE] });
    glo.p[G_TARGET] = 0; glo.p[G_RAM] = 1; glo.p[G_VOICES] = 0;
    const ids = Object.fromEntries(SLOTS.map((s) => [s.id, PRESETS[i].slots[s.id] || null]));
    const f = budgetFor(ids, 0);
    fit.push({ name: PRESETS[i].name, dropped: f.dropped, fit: f.ramV ? Math.min(MAXV, Math.floor((f.pool - (f.ram - f.ramV * f.voices)) / f.ramV)) : MAXV, free_ram_kb: f.pool });
  }
  audio.node = null; return { calls, fit };
})()`));
fs.writeFileSync(here + 'presets-calls.json', JSON.stringify(out.calls));
fs.writeFileSync(here + 'page-fit.json', JSON.stringify(out.fit, null, 1));
console.log(`${out.calls.length} presets recorded`);
await b.close();
