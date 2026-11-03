// Plays a trace through rack.wasm (the browser engine) and writes the same report as rack_cli --json.
//   node replay.mjs <trace> <rack.wasm> <out.json> [--hw] [--fresh]
//   --hw: the page's FM-1 preview, set_hw(1).  --fresh: a new engine instance for every patch, so no module
//   state carries over from the patch before (the FM-1 build zeroes its arena on every layout, which matches this).
import fs from 'fs';
const [trace, wasmPath, outPath] = process.argv.slice(2);
const hw = process.argv.includes('--hw');
const fresh = process.argv.includes('--fresh'), mod = await WebAssembly.compile(fs.readFileSync(wasmPath));
let x;
const start = async () => { x = (await WebAssembly.instantiate(mod, {})).exports; x._initialize?.(); x.init(); };
await start();
const res = {}; let name = '', frame = 0, L = [], R = [];
const renderTo = (t) => { while (frame < t) { const n = Math.min(128, t - frame); x.render(n);
  const l = new Float32Array(x.memory.buffer, x.out_l(), n), r = new Float32Array(x.memory.buffer, x.out_r(), n);
  for (let i = 0; i < n; i++) { L.push(l[i]); R.push(r[i]); } frame += n; } };
for (const line of fs.readFileSync(trace, 'utf8').split('\n')) {
  if (!line || line[0] === '#') continue;
  if (line.startsWith('patch ')) { name = line.slice(6); frame = 0; L = []; R = []; if (fresh) await start(); continue; }
  if (line.startsWith('end ')) {
    renderTo(+line.slice(4));
    let sum = 0, peak = 0, bad = false; const env = [], W = 4800;
    for (let i = 0; i < L.length; i++) {
      if (!isFinite(L[i]) || !isFinite(R[i])) bad = true;
      sum += L[i] * L[i] + R[i] * R[i]; peak = Math.max(peak, Math.abs(L[i]), Math.abs(R[i]));
      if (i % W === W - 1) { let s = 0; for (let k = i + 1 - W; k <= i; k++) s += L[k] * L[k] + R[k] * R[k]; env.push(Math.sqrt(s / (2 * W))); }
    }
    res[name] = { rms: Math.sqrt(sum / (2 * L.length)), peak, bad, env };
    continue;
  }
  const [at, fn, ...args] = line.split(' ');
  renderTo(+at);
  if (fn === 'set_hw') continue;
  if (fn === 'init' && hw) { x.init(); x.set_hw(1); continue; }
  if (typeof x[fn] !== 'function') { console.error('unknown call', fn); continue; }
  x[fn](...args.map(Number));
}
fs.writeFileSync(outPath, JSON.stringify(res, null, 1));
console.log(`${Object.keys(res).length} patches -> ${outPath}`);
