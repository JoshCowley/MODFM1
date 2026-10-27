// ================================================================ 6. engine bridge, MIDI, main loop
const MID = { PLAITS: 1, BRAIDS: 2, ELEMENTS: 3, RINGS: 10, WARPS: 11, FILTER: 12, PEAKS: 20, TIDES: 21, MARBLES: 22, GRIDS: 23, CLOUDS: 30, DELAY: 31, REVERB: 32, CHORUS: 33 };
const audio = { on: false, ctx: null, node: null, scope: new Float32Array(128) };
let mon = new Float32Array(160);
let watchSlot = -1;
const sent = {};
let dirty = true;
function draw() { dirty = true; syncEngine(); syncLeds(); }
function engine(call) { if (audio.node) audio.node.port.postMessage({ type: "calls", calls: [call] }); }
const meta = (p) => p.t === "list" ? [0, p.opts.length - 1, p.opts.length - 1] : p.t === "bip" ? [-1, 1, 2] : p.t === "int" ? [p.min, p.max, p.max - p.min] : [0, 1, 1];
function srcIndex(name) {
  const b = BUILT_SRC.indexOf(name); if (b >= 0) return b;
  const e = EXTRA_SRC.indexOf(name); if (e >= 0) return 13 + e;
  for (const [k, id] of [[0, "MODA"], [1, "MODB"]]) { const m = MODULES[rack[id].mod]; if (!m) continue; const j = m.outs.findIndex((o) => `${id.slice(-1)} ${o}` === name); if (j >= 0) return 9 + k * 2 + j; }
  return 0;
}
function dstIndex(name) {
  if (name === "PITCH") return [-1, 0];
  if (name === "PAN") return [-1, 1];
  if (name.startsWith("LFO.")) { const j = LFOS.pages.flat().findIndex((p) => `LFO.${p.l}` === name); return j >= 0 ? [8, j] : null; }
  if (name.startsWith("AMP.")) { const j = AMP.pages.flat().findIndex((p) => `AMP.${p.l}` === name); return j >= 0 ? [6, j] : null; }
  for (let si = 0; si < SLOTS.length; si++) {
    const s = SLOTS[si], m = MODULES[rack[s.id].mod]; if (!m) continue;
    const j = m.pages.flat().findIndex((p) => `${s.name}.${p.l}` === name); if (j >= 0) return [si, j];
  }
  return null;
}
function syncEngine() {
  if (!audio.node) return;
  const calls = [];
  const put = (key, call) => { const v = JSON.stringify(call); if (sent[key] !== v) { sent[key] = v; calls.push(call); } };
  SLOTS.forEach((s, si) => {
    const name = rack[s.id].mod, m = MODULES[name];
    put(`mod${si}`, ["set_module", si, m ? MID[name] : 0]);
    if (m) m.pages.flat().forEach((p, pi) => { put(`meta${si}.${pi}`, ["set_meta", si, pi, ...meta(p)]); put(`p${si}.${pi}`, ["set_param", si, pi, rack[s.id].p[pi]]); });
  });
  AMP.pages.flat().forEach((p, pi) => { put(`meta6.${pi}`, ["set_meta", 6, pi, ...meta(p)]); put(`p6.${pi}`, ["set_param", 6, pi, amp.p[pi]]); });
  put("tempo", ["set_param", 7, 0, glo.p[G_TEMPO]]);
  LFOS.pages.flat().forEach((p, pi) => { put(`meta8.${pi}`, ["set_meta", 8, pi, ...meta(p)]); put(`p8.${pi}`, ["set_param", 8, pi, lfo.p[pi]]); });
  matrix.forEach((r, i) => {
    const d = r.dst === "—" ? null : dstIndex(r.dst), sidx = srcIndex(r.src);
    put(`r${i}`, ["set_route", i, d ? sidx : 0, d ? d[0] : 0, d ? d[1] : 0, r.amt, srcIndex(r.via)]);
  });
  macros.forEach((v, i) => put(`m${i}`, ["set_macro", i, v]));
  put("master", ["set_master", master * midiState.expr]);
  put("mfx", ["set_master_fx", glo.p[G_LOW] * 12, glo.p[G_MID] * 10, glo.p[G_HIGH] * 12, glo.p[G_COMP]]);
  put("legato", ["set_legato", amp.p[A_MODE] === 1 ? 0 : 1]);
  put("vintage", ["set_vintage", amp.p[A_VINTAGE]]);
  mseq.lanes.forEach((l, i) => put(`slew${i}`, ["set_ext_slew", i, 1 - l.smooth * .995]));
  const tk = `${glo.p[G_TUNE]}.${glo.p[G_TROOT]}.${glo.p[G_A4]}`;
  if (sent.tune !== tk) { sent.tune = tk; calls.push(...tuningCalls()); }
  put("voices", ["set_voices", Math.max(1, budget().voices)]);
  if (calls.length) audio.node.port.postMessage({ type: "calls", calls });
}
function watchFor(pg) {
  const si = SLOTS.findIndex((s) => s.id === pg);
  if (si >= 0) return si;
  return pg === "AMP" ? 6 : pg === "LFOS" ? 8 : -1;
}
function applyGlobal() {
  document.getElementById("device").style.setProperty("--ledf", (0.45 + 0.6 * glo.p[G_LED]).toFixed(2));
  draw();
}

// ---------------------------------------------------------------- MIDI over USB: everything in, notes / clock / CCs / programs out
const midiState = { inputs: 0, outputs: 0, denied: false, last: 0, clock: [], clockLock: false, sustain: false, deferred: new Set(), expr: 1, bend: 0, bank: 0 };
const midiNotes = new Map(), midiPlay = new Map();
function midiOn(nn, vel, fromLoop = false) {
  vel = fromLoop ? vel : velCurve(vel);
  const notes = chordNotes(nn);
  if (midiNotes.has(nn)) notesOff(midiNotes.get(nn));
  midiState.deferred.delete(nn);
  if (!fromLoop) seqNoteOn(`m${nn}`, notes, vel);
  midiNotes.set(nn, arpOn() ? { notes, voices: [], at: [], out: false } : notesOn(notes, vel, fromLoop ? "loop" : "midi"));
  if (!fromLoop) recEvent({ type: "midi", n: nn, on: true, vel });
  refreshKeys();
}
function midiOff(nn, fromLoop = false) {
  if (!midiNotes.has(nn)) return;
  if (midiState.sustain && !fromLoop) { midiState.deferred.add(nn); return; }
  notesOff(midiNotes.get(nn)); midiNotes.delete(nn);
  if (!fromLoop) recEvent({ type: "midi", n: nn, on: false });
  refreshKeys(); if (!fromLoop) seqNoteOff(`m${nn}`);
}
// the knob under Knob 1-4 on the page on screen, for CC 70-73 in and out
function pageRef(i) {
  if (FIXED[page] && page !== "AMP" && page !== "LFOS") { const def = FIXED[page][0], p = def.pages[subpage] && def.pages[subpage][i]; return p ? { ref: { w: page, idx: def.pages.slice(0, subpage).flat().length + i }, p } : null; }
  const ref = lockRefFor(SLOTS.find((x) => x.id === page), i); return ref ? { ref, p: refParam(ref) } : null;
}
const toNorm = (p, v) => p.t === "list" ? v / Math.max(1, p.opts.length - 1) : p.t === "int" ? (v - p.min) / (p.max - p.min) : p.t === "bip" ? (v + 1) / 2 : v;
const fromNorm = (p, x) => p.t === "list" ? Math.round(x * (p.opts.length - 1)) : p.t === "int" ? Math.round(p.min + x * (p.max - p.min)) : p.t === "bip" ? x * 2 - 1 : x;
function knobCcOut(ref, v) {
  if (glo.p[G_MOUT] !== 2) return;
  for (let i = 0; i < 4; i++) { const pr = pageRef(i); if (pr && pr.ref.w !== "GLOBAL" && lockKey(pr.ref) === lockKey(ref)) { ccOut(70 + i, toNorm(pr.p, v)); return; } }
}
function allSoundOff() {
  midiNotes.clear(); latched.clear(); held.clear(); sounding.clear(); arp.pool = [];
  engine(["all_off"]); refreshKeys();
}
function resetControllers() {
  midiState.bend = 0; midiState.expr = 1; midiState.sustain = false; midiState.deferred.clear();
  engine(["set_bend", 0]); engine(["set_ext", 4, 0]); engine(["set_ext", 5, 0]); draw();
}
function onMidi(e) {
  const d = e.data, st = d[0];
  midiState.last = performance.now();
  if (st === 0xF8) {
    if (glo.p[G_CLOCK] !== 1) return;
    const ct = midiState.clock; ct.push(performance.now()); if (ct.length > 49) ct.shift();
    if (ct.length >= 25) {     // averaged over up to two beats, so a steady master reads steady
      const bpm = clamp(Math.round(60000 * (ct.length - 1) / 24 / (ct[ct.length - 1] - ct[0])), 60, 180);
      if (bpm !== glo.p[G_TEMPO]) { glo.p[G_TEMPO] = bpm; draw(); }
      midiState.clockLock = true;
    }
    return;
  }
  if (st === 0xFA) { engine(["reset_clock"]); if (hasNotes()) { if (ss.run) seqStop(); seqStart(); } return; }
  if (st === 0xFB) { if (!ss.run && hasNotes()) seqStart(); return; }
  if (st === 0xFC) { if (ss.run) seqStop(); draw(); return; }
  if (st >= 0xF0) return;
  const type = st & 0xF0, ch = (st & 0x0F) + 1, want = glo.p[G_MIDICH];
  if (want && ch !== want) return;
  if (type === 0x90 && d[2] > 0) midiOn(d[1], d[2] / 127);
  else if (type === 0x80 || type === 0x90) midiOff(d[1]);
  else if (type === 0xE0) { midiState.bend = (((d[2] << 7) | d[1]) - 8192) / 8192; engine(["set_bend", midiState.bend * glo.p[G_BEND]]); }
  else if (type === 0xD0) engine(["set_ext", 5, d[1] / 127]);
  else if (type === 0xA0) engine(["set_ext", 5, d[2] / 127]);
  else if (type === 0xC0) {
    if (!glo.p[G_PROG]) return;
    const list = patchList(true), ix = midiState.bank * 128 + d[1];
    if (ix < list.length) { lib.filter = 0; presetIx = ix; loadPreset(ix); engine(["all_off"]); sounding.clear(); refreshKeys(); say(patchName, "HOME"); }
  }
  else if (type === 0xB0) {
    const cc = d[1], v = d[2] / 127;
    if (cc === 0) midiState.bank = d[2];
    else if (cc === 1) { engine(["set_ext", 4, v]); setRef({ w: "macro", i: 0 }, v); learnMacro = 0; draw(); }
    else if (cc === 2) engine(["set_ext", 5, v]);
    else if (cc === 7) { master = v; draw(); }
    else if (cc === 11) { midiState.expr = v; draw(); }
    else if (cc >= 16 && cc <= 19) { setRef({ w: "macro", i: cc - 16 }, v); learnMacro = cc - 16; draw(); }
    else if (cc >= 21 && cc <= 24) { setRef({ w: "macro", i: cc - 21 }, v); learnMacro = cc - 21; draw(); }
    else if (cc >= 70 && cc <= 73) { const pr = pageRef(cc - 70); if (pr) { setRef(pr.ref, fromNorm(pr.p, v)); draw(); } }
    else if (cc === 64) { midiState.sustain = d[2] >= 64; if (!midiState.sustain) { for (const nn of [...midiState.deferred]) { midiState.deferred.delete(nn); midiOff(nn); } } }
    else if (cc === 120) allSoundOff();
    else if (cc === 121) resetControllers();
    else if (cc === 122) { glo.p[G_LOCAL] = d[2] >= 64 ? 0 : 1; storeGlobal(); draw(); }
    else if (cc === 123) { for (const nn of [...midiNotes.keys()]) midiOff(nn); }
  }
}
async function midiStart() {
  if (!navigator.requestMIDIAccess) { midiState.denied = true; return; }
  try {
    const access = await navigator.requestMIDIAccess();
    const bind = () => {
      midiState.inputs = access.inputs.size; midiState.outputs = access.outputs.size;
      for (const input of access.inputs.values()) input.onmidimessage = onMidi;
      mout.outs = [...access.outputs.values()]; draw();
    };
    access.onstatechange = bind; bind();
  } catch { midiState.denied = true; }
}
// MIDI clock out (24 per beat) while the internal clock leads and MIDI OUT is on
const clockOut = { next: 0 };
setInterval(() => {
  if (!audio.on || !glo.p[G_MOUT] || glo.p[G_CLOCK] === 1 || !mout.outs.length) { clockOut.next = 0; return; }
  const now = audioNow(), d = 60 / glo.p[G_TEMPO] / 24;
  if (!clockOut.next || clockOut.next < now - .1) clockOut.next = now + .01;
  while (clockOut.next < now + .1) { midiSend([0xF8], clockOut.next); clockOut.next += d; }
}, 25);

// ---------------------------------------------------------------- switching on
async function switchOn() {
  const btn = document.getElementById("power");
  btn.disabled = true; btn.querySelector("b").textContent = "Starting…";
  try {
    const ctx = new AudioContext({ sampleRate: 48000, latencyHint: "interactive" });
    const [wasm] = await Promise.all([
      fetch("rack.wasm").then((r) => { if (!r.ok) throw new Error("rack.wasm " + r.status); return r.arrayBuffer(); }),
      ctx.audioWorklet.addModule("worklet.js")]);
    const node = new AudioWorkletNode(ctx, "fm1-rack", { numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2] });
    node.connect(ctx.destination);
    await new Promise((res, rej) => {
      node.port.onmessage = (e) => {
        const m = e.data;
        if (m.type === "ready") res();
        else if (m.type === "error") rej(new Error(m.message));
        else if (m.type === "mon") { audio.scope = m.scope; mon = m.mon; if (m.cpu) { audio.cpu = m.cpu; if (cpuHist.push(m.cpu[0]) > HIST) cpuHist.shift(); } }
      };
      node.port.postMessage({ type: "load", wasm }, [wasm]);
    });
    await ctx.resume();
    Object.assign(audio, { on: true, ctx, node });
    for (const k in sent) delete sent[k];
    watchSlot = -2;
    btn.hidden = true;
    bootAt = performance.now();
    sounding.clear(); refreshKeys(); draw();
    midiStart();
  } catch (err) {
    btn.disabled = false; btn.querySelector("b").textContent = "Switch on";
    btn.querySelector("span").textContent = "Couldn't start: " + (err.message || err);
  }
}
document.getElementById("power").addEventListener("click", switchOn);

// ---------------------------------------------------------------- main loop: phrase playback every frame, screen at the ANIMATION rate
let lastT = performance.now(), lastFrame = 0;
function loop(now) {
  requestAnimationFrame(loop);
  const dt = Math.min(.1, (now - lastT) / 1000); lastT = now;
  if (ss.run || ss.free) syncLeds();
  const mode = glo.p[G_ANIM] || 0;
  animT += dt * [1, .45, 0][mode];
  const interval = [1000 / 30, 1000 / 20, 1000 / 6][mode];
  if (now - lastFrame < (dirty ? 14 : interval)) return;
  lastFrame = now; dirty = false;
  const ws = watchFor(page);
  if (ws !== watchSlot) { watchSlot = ws; if (audio.node) audio.node.port.postMessage({ type: "watch", slot: ws }); }
  pushHist();
  render();
}

// ---------------------------------------------------------------- start
loadPreset(0);
applyGlobal();
requestAnimationFrame(loop);
document.fonts?.load(`12px "DotGothic16"`).then(draw, () => {});
