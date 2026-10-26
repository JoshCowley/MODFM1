// Renders each factory patch through rack.wasm and reports its level.
const fs = require("fs");
const { MODULES, AMP, LFOS, SLOTS, GLOBAL } = require("./catalogue.js");
const PATCHES = require("./patches.js");
const MID = { PLAITS: 1, BRAIDS: 2, ELEMENTS: 3, RINGS: 10, WARPS: 11, FILTER: 12, PEAKS: 20, TIDES: 21, MARBLES: 22, GRIDS: 23, CLOUDS: 30, DELAY: 31, REVERB: 32, CHORUS: 33 };
const BUILT_SRC = ["—", "M1", "M2", "M3", "M4", "VELO", "KEY TRK", "RANDOM", "AMP ENV"], EXTRA_SRC = ["LFO 1", "LFO 2", "LFO 3", "MOD ENV"];
const meta = (p) => p.t === "list" ? [0, p.opts.length - 1, p.opts.length - 1] : p.t === "bip" ? [-1, 1, 2] : p.t === "int" ? [p.min, p.max, p.max - p.min] : [0, 1, 1];
const blank = (def) => def ? def.pages.flat().map((p) => p.def) : [];
function apply(def, values, over, where) {
  if (!over) return;
  const flat = def.pages.flat();
  for (const [label, v] of Object.entries(over)) {
    const j = flat.findIndex((p) => p.l === label);
    if (j < 0) throw new Error(`${where}: no param ${label}`);
    const p = flat[j];
    if (p.t === "list") { const k = p.opts.indexOf(String(v)); if (k < 0) throw new Error(`${where}: ${label} has no option ${v}`); values[j] = k; }
    else values[j] = v;
  }
}
function build(pr) {
  const rack = {};
  for (const s of SLOTS) { const m = pr.slots[s.id]; rack[s.id] = { mod: m, p: blank(MODULES[m]) }; apply(MODULES[m] || { pages: [] }, rack[s.id].p, pr.p && pr.p[s.id], `${pr.name}.${s.id}`); }
  const amp = blank(AMP); apply(AMP, amp, pr.amp, `${pr.name}.AMP`);
  const lfo = blank(LFOS); apply(LFOS, lfo, pr.lfo, `${pr.name}.LFO`);
  return { rack, amp, lfo };
}
module.exports = { build, apply, blank };

if (require.main === module) (async () => {
  const { instance } = await WebAssembly.instantiate(fs.readFileSync(process.argv[2] || require("path").join(__dirname, "../web/rack.wasm")), {});
  const x = instance.exports; x._initialize?.();
  const levels = {};
  for (const pr of PATCHES) {
    x.init();
    const st = build(pr);
    let shared = 0, poly = AMP.cpu;
    SLOTS.forEach((s, si) => {
      const m = MODULES[st.rack[s.id].mod];
      x.set_module(si, m ? MID[st.rack[s.id].mod] : 0);
      if (m) { if (m.poly) poly += m.cpu; else shared += m.cpu; m.pages.flat().forEach((p, pi) => { x.set_meta(si, pi, ...meta(p)); x.set_param(si, pi, st.rack[s.id].p[pi]); }); }
    });
    AMP.pages.flat().forEach((p, pi) => { x.set_meta(6, pi, ...meta(p)); x.set_param(6, pi, st.amp[pi]); });
    LFOS.pages.flat().forEach((p, pi) => { x.set_meta(8, pi, ...meta(p)); x.set_param(8, pi, st.lfo[pi]); });
    x.set_param(7, 0, 112);
    const voices = Math.max(1, Math.min(6, Math.floor((80 - shared) / poly)));
    x.set_voices(voices);
    const srcIndex = (name) => {
      const b = BUILT_SRC.indexOf(name); if (b >= 0) return b;
      const e = EXTRA_SRC.indexOf(name); if (e >= 0) return 13 + e;
      for (const [k, id] of [[0, "MODA"], [1, "MODB"]]) { const m = MODULES[st.rack[id].mod]; if (!m) continue; const j = m.outs.findIndex((o) => `${id.slice(-1)} ${o}` === name); if (j >= 0) return 9 + k * 2 + j; }
      throw new Error(`${pr.name}: no source ${name}`);
    };
    const dstIndex = (name) => {
      if (name === "PITCH") return [-1, 0]; if (name === "PAN") return [-1, 1];
      if (name.startsWith("AMP.")) return [6, AMP.pages.flat().findIndex((p) => `AMP.${p.l}` === name)];
      if (name.startsWith("LFO.")) return [8, LFOS.pages.flat().findIndex((p) => `LFO.${p.l}` === name)];
      for (let si = 0; si < SLOTS.length; si++) { const s = SLOTS[si], m = MODULES[st.rack[s.id].mod]; if (!m) continue; const j = m.pages.flat().findIndex((p) => `${s.name}.${p.l}` === name); if (j >= 0) return [si, j]; }
      throw new Error(`${pr.name}: no destination ${name}`);
    };
    pr.matrix.forEach((r, i) => { const d = dstIndex(r[1]); x.set_route(i, srcIndex(r[0]), d[0], d[1], r[2], r[3] ? srcIndex(r[3]) : 0); });
    (pr.macros || [.5, .5, .5, .5]).forEach((v, i) => x.set_macro(i, v));
    x.set_master(0.7);
    const notes = pr.play === "chord" ? [60, 64, 67] : pr.play === "low" ? [36] : pr.play === "hold" ? [57] : [57];
    const mem = () => new Float32Array(x.memory.buffer);
    let sum = 0, n = 0, peak = 0;
    const t0 = Date.now();
    for (let b = 0; b < 48000 * 4 / 128; b++) {
      const t = b * 128 / 48000;
      if (b === 0) notes.forEach((nn) => x.note_on(nn, 0.8));
      if (pr.play === "low" && Math.abs(t - 1) < 0.001) { x.note_off(36); x.note_on(43, 0.8); }
      if (pr.play === "chord" && b === Math.round(2.5 * 48000 / 128)) notes.forEach((nn) => x.note_off(nn));
      x.render(128);
      const L = mem().subarray(x.out_l() / 4, x.out_l() / 4 + 128), R = mem().subarray(x.out_r() / 4, x.out_r() / 4 + 128);
      for (let i = 0; i < 128; i++) { if (!isFinite(L[i])) throw new Error(pr.name + " NaN"); if (t > 0.2 && t < 2.5) { sum += L[i] * L[i] + R[i] * R[i]; n += 2; } peak = Math.max(peak, Math.abs(L[i]), Math.abs(R[i])); }
    }
    const rms = Math.sqrt(sum / n);
    levels[pr.name] = { rms, peak, voices };
    console.log(pr.name.padEnd(13), "voices", voices, "rms", rms.toFixed(3), "peak", peak.toFixed(2), "cpu ms/4s", Date.now() - t0);
  }
  fs.writeFileSync("levels.json", JSON.stringify(levels, null, 1));
})().catch((e) => { console.error(e.message); process.exit(1); });
