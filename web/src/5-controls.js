// ================================================================ 5. controls
const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
const KNOBS_L = [["MASTER", "master"], ["SELECT", "select"], ["PRESETS", "presets"], ["ALGORITHM", "algo"]];
const KNOBS_R = [["KNOB 1", 0], ["KNOB 2", 1], ["KNOB 3", 2], ["KNOB 4", 3]];
const angle = {};
function makeKnob(parent, label, role, isMaster = false) {
  const d = document.createElement("div");
  d.className = "knob" + (isMaster ? " master" : ""); d.tabIndex = 0;
  d.setAttribute("role", "slider"); d.setAttribute("aria-label", label);
  d.innerHTML = `<b>${label}</b><div class="cap"><i></i></div>`;
  parent.appendChild(d);
  const ind = d.querySelector("i");
  angle[label] = isMaster ? -135 + 270 * master : 0;
  const paint = () => { ind.style.transform = `rotate(${angle[label]}deg)`; };
  paint();
  const turn = (k) => {
    if (!k) return;
    if (isMaster) { master = clamp(master + k * .02, 0, 1); angle[label] = -135 + 270 * master; say(`VOLUME ${Math.round(master * 100)}`, "OUT"); }
    else { angle[label] = (angle[label] + k * 18) % 360; encoder(role, k); }
    paint();
  };
  let drag = null, wheel = 0;
  d.addEventListener("pointerdown", (e) => { e.preventDefault(); d.setPointerCapture(e.pointerId); drag = { y: e.clientY, acc: 0 }; d.focus(); });
  d.addEventListener("pointermove", (e) => { if (!drag) return; drag.acc += drag.y - e.clientY; drag.y = e.clientY; const k = Math.trunc(drag.acc / 7); if (k) { drag.acc -= k * 7; turn(k); } });
  const end = () => { drag = null; };
  d.addEventListener("pointerup", end); d.addEventListener("pointercancel", end);
  d.addEventListener("wheel", (e) => { e.preventDefault(); wheel -= e.deltaMode === 1 ? e.deltaY * 33 : e.deltaY; const k = Math.trunc(wheel / 25); if (k) { wheel -= k * 25; turn(k); } }, { passive: false });
  d.addEventListener("keydown", (e) => { const k = { ArrowUp: 1, ArrowRight: 1, ArrowDown: -1, ArrowLeft: -1 }[e.key]; if (k) { e.preventDefault(); e.stopPropagation(); turn(k); } });
}
KNOBS_L.forEach(([l, r]) => makeKnob(document.getElementById("leftKnobs"), l, r, r === "master"));
KNOBS_R.forEach(([l, r]) => makeKnob(document.getElementById("rightKnobs"), l, r));

function stepParam(p, v, k) {
  if (p.t === "list") return clamp(Math.round(v) + k, 0, p.opts.length - 1);
  if (p.t === "int") return clamp(Math.round(v) + k, p.min, p.max);
  if (p.t === "bip") return clamp(v + k * .02, -1, 1);
  return clamp(v + k * .01 * (Math.abs(k) > 1 ? 2 : 1), 0, 1);
}
function cycle(list, cur, k) { const i = Math.max(0, list.indexOf(cur)); return list[(i + k + list.length * 8) % list.length]; }
const FIXED = { AMP: [AMP, amp], GLOBAL: [GLOBAL, glo], SCALE: [SCALE, scl], LFOS: [LFOS, lfo] };

function encoder(role, k) {
  if (role === "presets") {
    if (page === "SAVE") return;
    const list = patchList();
    presetIx = (presetIx + k + list.length * 8) % list.length; loadPreset(presetIx); picker = null; engine(["all_off"]); sounding.clear(); refreshKeys();
    say(patchName, "HOME"); return;
  }
  if (page === "SAVE") {
    const st = saveState;
    if (role === "select") st.cursor = clamp(st.cursor + Math.sign(k), 0, 9);
    else if (role === "algo" || role === 0) { const c = CHARS.indexOf(st.name[st.cursor]); st.name[st.cursor] = CHARS[(Math.max(0, c) + Math.sign(k) + CHARS.length) % CHARS.length]; }
    else if (role === 1) st.slot = clamp(st.slot + Math.sign(k), 0, USER_SLOTS - 1);
    draw(); return;
  }
  const slot = SLOTS.find((s) => s.id === page);
  if (role === "algo") {
    if (!slot) { say("GO TO A SLOT TO SWAP MODULES"); return; }
    if (!picker || picker.slot !== slot.id) picker = { slot: slot.id, ix: Math.max(0, slot.choices.indexOf(rack[slot.id].mod)), t: performance.now() };
    picker.ix = clamp(picker.ix + Math.sign(k), 0, slot.choices.length - 1); draw(); return;
  }
  if (picker) picker = null;
  if (role === "select") {
    if (page === "MATRIX") mxRow = clamp(mxRow + Math.sign(k), 0, 11);
    else if (slot && MODULES[rack[slot.id].mod]) subpage = clamp(subpage + Math.sign(k), 0, MODULES[rack[slot.id].mod].pages.length - 1);
    else if (FIXED[page]) subpage = clamp(subpage + Math.sign(k), 0, FIXED[page][0].pages.length - 1);
    else if (page === "PERFORM" || page === "RACK") { page = page === "PERFORM" ? "RACK" : "PERFORM"; }
    draw(); return;
  }
  const i = role;   // knob 0..3
  if (learn && (slot || page === "AMP" || page === "LFOS")) { learnTurn(slot, i, k); draw(); return; }
  if (page === "PERFORM") {
    learnMacro = i;
    if (!learn) setRef({ w: "macro", i }, clamp(macros[i] + k * .01, 0, 1));
  } else if (page === "MATRIX") {
    const r = matrix[mxRow];
    if (i === 0) r.src = cycle(sources(), r.src, Math.sign(k));
    if (i === 1) r.dst = cycle(destinations(), r.dst, Math.sign(k));
    if (i === 2) r.amt = clamp(Math.round((r.amt + k * .01) * 100) / 100, -1, 1);
    if (i === 3) r.via = cycle(sources(), r.via, Math.sign(k));
  } else if (FIXED[page]) {
    const [def, st] = FIXED[page];
    const off = def.pages.slice(0, subpage).flat().length, p = def.pages[subpage][i];
    if (p) setRef({ w: page, idx: off + i }, stepParam(p, st.p[off + i], k));
  } else if (slot) {
    const st = rack[slot.id], m = MODULES[st.mod]; if (!m) return;
    const off = m.pages.slice(0, subpage).flat().length, p = m.pages[subpage][i];
    if (p) setRef({ w: "slot", slot: slot.id, mod: st.mod, idx: off + i }, stepParam(p, st.p[off + i], k));
  } else if (page === "RACK") { say("HOME FOR MACROS", "HOME"); }
  draw();
}

function setRef(ref, v, record = true) {
  if (ref.w === "macro") macros[ref.i] = v;
  else if (ref.w === "AMP") amp.p[ref.idx] = v;
  else if (ref.w === "GLOBAL") { glo.p[ref.idx] = v; storeGlobal(); applyGlobal(); return; }
  else if (ref.w === "SCALE") { scl.p[ref.idx] = v; return; }
  else if (ref.w === "LFOS") lfo.p[ref.idx] = v;
  else if (ref.w === "slot") { if (rack[ref.slot].mod !== ref.mod) return; rack[ref.slot].p[ref.idx] = v; }
  if (record) recEvent({ type: "set", ref, v });
}
function learnTurn(slot, i, k) {
  let p, dst;
  if (page === "AMP") { p = AMP.pages[subpage][i]; dst = p && `AMP.${p.l}`; }
  else if (page === "LFOS") { p = LFOS.pages[subpage][i]; dst = p && `LFO.${p.l}`; }
  else if (slot) { const m = MODULES[rack[slot.id].mod]; p = m && m.pages[subpage][i]; dst = p && `${slot.name}.${p.l}`; }
  if (!p) return;
  if (p.mod === false) { say(`${p.l} CAN'T BE MODULATED`); return; }
  const src = `M${learnMacro + 1}`;
  let r = matrix.find((x) => x.src === src && x.dst === dst);
  if (!r) { r = matrix.find((x) => x.src === "—"); if (!r) { say("MATRIX FULL · FREE A ROW", "MATRIX"); return; } Object.assign(r, { src, dst, amt: 0, via: "—" }); }
  r.amt = clamp(Math.round((r.amt + k * .02) * 100) / 100, -1, 1);
  say(`${src} → ${dst} ${r.amt >= 0 ? "+" : ""}${Math.round(r.amt * 100)}`, "LEARN");
}
function setLearn(on) { learn = on; say(on ? `LEARN M${learnMacro + 1}` : "LEARN DONE", "LEARN"); }

// ---------------------------------------------------------------- phrase recorder: keys, MIDI notes and knob moves, looped
const MAXLEN = 16000;
function recEvent(e) {
  if (phrase.state === "armed") { phrase.state = "rec"; phrase.t0 = performance.now(); phrase.events = []; }
  if (phrase.state === "rec") phrase.events.push({ t: performance.now() - phrase.t0, ...e });
}
function closeLoop() {
  const raw = Math.max(250, performance.now() - phrase.t0);
  const barMs = 240000 / glo.p[G_TEMPO];                       // 4/4 at the GLOBAL tempo
  const bars = clamp(2 ** Math.round(Math.log2(Math.max(raw / barMs, 1))), 1, 8);
  phrase.len = bars * barMs; phrase.bars = bars;
  phrase.events = phrase.events.filter((e) => e.t < phrase.len);
  for (const k of [...held]) phrase.events.push({ t: phrase.len - 1, type: "key", k, on: false });
  phrase.state = "loop"; phrase.start = performance.now(); phrase.last = 0;
  say(`LOOP ${bars} BAR${bars > 1 ? "S" : ""} · ${phrase.events.length} EVENTS`, "REC");
}
function clearPhrase() {
  phrase.state = "idle"; phrase.events = []; playKeys.clear();
  for (const [nn] of midiPlay) midiOff(nn, true); midiPlay.clear();
  refreshKeys();
}
function phraseTick() {
  if (phrase.state === "rec" && performance.now() - phrase.t0 > MAXLEN) closeLoop();
  if (phrase.state !== "loop") return;
  const pos = (performance.now() - phrase.start) % phrase.len;
  const wrapped = pos < phrase.last;
  let keys = false;
  for (const e of phrase.events) {
    const due = wrapped ? (e.t > phrase.last || e.t <= pos) : (e.t > phrase.last && e.t <= pos);
    if (!due) continue;
    if (e.type === "key") { keys = true; if (e.on) { playKeys.add(e.k); keyVel[e.k] = e.vel ?? keyVel[e.k]; } else playKeys.delete(e.k); }
    else if (e.type === "midi") { if (e.on) { midiPlay.set(e.n, 1); midiOn(e.n, e.vel, true); } else { midiPlay.delete(e.n); midiOff(e.n, true); } }
    else setRef(e.ref, e.v, false);
  }
  phrase.last = pos;
  if (keys) refreshKeys();
}

// ---------------------------------------------------------------- buttons, with icons
const SVG = {
  FX: '<path d="M4.5 12.5h7a3 3 0 0 0 0-6 4 4 0 0 0-7.6 1 2.5 2.5 0 0 0 .6 5z"/>',
  SHP: '<path d="M1 5.5h7c2 0 2.5-2 3.5-2S13 9 15 13.5"/>',
  AMP: '<path d="M1 14 4 3l3 6h4l3 5"/>',
  LFO: '<path d="M1 8c1.7-5 3.5-5 5.2 0s3.5 5 5.3 0S14 4 15 6"/>',
  SRC: '<path d="M1 12V4h4v8h4V4h4v8h2"/>',
  GLO: '<circle cx="8" cy="8" r="2.5"/><path d="M8 1.5v2M8 12.5v2M1.5 8h2M12.5 8h2M3.4 3.4l1.4 1.4M11.2 11.2l1.4 1.4M3.4 12.6l1.4-1.4M11.2 4.8l1.4-1.4"/>',
  HOME: '<circle cx="4.5" cy="4.5" r="2.5"/><circle cx="11.5" cy="4.5" r="2.5"/><circle cx="4.5" cy="11.5" r="2.5"/><circle cx="11.5" cy="11.5" r="2.5"/>',
  SAVE: '<path d="M2 2h9l3 3v9H2z"/><path d="M5 2v4h5V2M5 14v-4h6v4"/>',
  MATRIX: '<circle cx="3.5" cy="4" r="2"/><circle cx="12.5" cy="4" r="2"/><path d="M3.5 6c0 7 9 7 9 0"/>',
  SCALE: '<path d="M2 3h12v10H2zM5.5 13V8.5M8 13V8.5M10.5 13V8.5"/><path d="M4.5 3v5h2V3M9.5 3v5h2V3" fill="currentColor" stroke="none"/>',
  HOLD: '<rect x="3" y="7" width="10" height="7" rx="1"/><path d="M5 7V5a3 3 0 0 1 6 0v2"/>',
  REC: '<circle cx="8" cy="8" r="4.5" fill="currentColor"/>',
  DN: '<path d="M4 6l4 4 4-4"/>', UP: '<path d="M4 10l4-4 4 4"/>',
};
const svg = (k) => `<svg viewBox="0 0 16 16" aria-hidden="true">${SVG[k]}</svg>`;
document.getElementById("octDn").innerHTML = `${svg("DN")}<span>Oct −</span><small>OCT-</small>`;
document.getElementById("octUp").innerHTML = `${svg("UP")}<span>Oct +</span><small>OCT+</small>`;
// [rack label, printed label, action, colour kind, icon]
const BTNS = [
  ["FX 1·2", "FX", "FXBTN", "fx", "FX"], ["Shaper", "SCL", "SHP", "shp", "SHP"], ["Amp", "ENV", "AMP", "amp", "AMP"], ["Mod · LFO", "LFO", "MODBTN", "mod", "LFO"], ["Source", "EDIT", "SRC", "src", "SRC"], ["Global", "GLO", "GLOBAL", "glo", "GLO"],
  ["Home", "HOME", "HOME", null, "HOME"], ["Save", "SAVE", "SAVE", null, "SAVE"], ["Matrix", "ARP", "MATRIX", null, "MATRIX"], ["Scale", "SEQ", "SCALE", null, "SCALE"], ["Hold", "PLAY", "HOLD", null, "HOLD"], ["Rec", "REC", "REC", null, "REC"],
];
const btnEls = {};
const bwrap = document.getElementById("btns");
for (const [lab, printed, act, kind, ic] of BTNS) {
  const el = document.createElement("button");
  el.type = "button"; el.className = "btn"; if (kind) el.dataset.kind = kind;
  el.innerHTML = `${svg(ic)}<span>${lab}</span><small>${printed}</small>`;
  el.setAttribute("aria-label", `${lab} (printed ${printed})`);
  if (act === "HOME") {
    let timer = 0, long = false;
    el.addEventListener("pointerdown", (e) => { long = false; if (e.shiftKey) return; timer = setTimeout(() => { long = true; if (!learn) setLearn(true); }, 500); });
    const cancel = () => clearTimeout(timer);
    el.addEventListener("pointerup", cancel); el.addEventListener("pointerleave", cancel); el.addEventListener("pointercancel", cancel);
    el.addEventListener("click", (e) => { if (long) return; if (e.shiftKey) { setLearn(!learn); return; } if (learn) { setLearn(false); return; } press("HOME"); });
  } else el.addEventListener("click", () => press(act));
  bwrap.appendChild(el); btnEls[act] = el;
}
function resolve(act) {
  const cyc = { FXBTN: ["FX1", "FX2"], MODBTN: ["MODA", "MODB", "LFOS"] }[act];
  if (!cyc) return act;
  if (picker && cyc.includes(picker.slot)) return picker.slot;
  const i = cyc.indexOf(page);
  return i < 0 ? cyc[0] : cyc[(i + 1) % cyc.length];
}
function press(act) {
  act = resolve(act);
  const slot = SLOTS.find((s) => s.id === act);
  if (slot) {
    if (picker && picker.slot === slot.id) {
      const ch = slot.choices[picker.ix];
      const f = fitOf(slot.id, ch);
      if (!f.ok) { const free = POOL - (budget().ram - (MODULES[rack[slot.id].mod]?.ram || 0)); say(f.why === "CPU" ? `${ch} NEEDS MORE CPU` : f.why.startsWith("IN ") ? `${ch} IS ALREADY ${f.why}` : `${ch} NEEDS ${MODULES[ch].ram}K, ${free}K FREE`); return; }
      rack[slot.id] = { mod: ch, p: blankParams(MODULES[ch]) }; picker = null; subpage = 0;
      delete vs.rings; delete vs.clouds; delete vs.verb;
      say(ch ? `${ch} LOADED · ${budget().voices} VOICES` : `${slot.name} EMPTIED`, ch || "EMPTY"); return;
    }
    picker = null; if (page !== slot.id) subpage = 0; page = slot.id; draw(); return;
  }
  picker = null;
  if (act === "HOME") { if (page === "SAVE") { saveState = null; page = "PERFORM"; say("SAVE CANCELLED", "SAVE"); } else page = page === "PERFORM" ? "RACK" : "PERFORM"; }
  else if (FIXED[act]) { if (page !== act) subpage = 0; page = act; }
  else if (act === "MATRIX") page = "MATRIX";
  else if (act === "SAVE") { if (page === "SAVE") commitSave(); else openSave(); }
  else if (act === "HOLD") { hold = !hold; if (!hold) latched.clear(); say(hold ? "HOLD ON" : "HOLD OFF", "HOLD"); refreshKeys(); }
  else if (act === "REC") {
    if (phrase.state === "idle") { phrase.state = "armed"; say("REC ARMED · PLAY OR TURN", "REC"); }
    else if (phrase.state === "armed") { phrase.state = "idle"; say("REC CANCELLED", "REC"); }
    else if (phrase.state === "rec") closeLoop();
    else { clearPhrase(); say("PHRASE CLEARED", "REC"); }
  }
  draw();
}
document.getElementById("octDn").addEventListener("click", () => { octave = clamp(octave - 1, -3, 3); say(`OCTAVE ${octave >= 0 ? "+" : ""}${octave}`); });
document.getElementById("octUp").addEventListener("click", () => { octave = clamp(octave + 1, -3, 3); say(`OCTAVE ${octave >= 0 ? "+" : ""}${octave}`); });

function syncLeds() {
  const pairOn = { FXBTN: ["FX1", "FX2"], MODBTN: ["MODA", "MODB", "LFOS"] };
  for (const [act, el] of Object.entries(btnEls)) {
    let on = act === page || (act === "HOME" && (page === "PERFORM" || page === "RACK")) || (pairOn[act] && pairOn[act].includes(page));
    if (act === "HOLD" || act === "REC") on = false;
    el.classList.toggle("lit", !!on);
    el.classList.toggle("hold", (act === "HOLD" && hold) || (act === "REC" && phrase.state === "loop") || (act === "SCALE" && !on && scl.p[0] === 1));
    el.classList.toggle("rec", act === "REC" && (phrase.state === "rec" || phrase.state === "armed"));
    el.classList.toggle("blink", (act === "REC" && phrase.state === "armed") || (act === "HOME" && learn));
    el.setAttribute("aria-pressed", act === "HOLD" ? String(hold) : String(!!on));
  }
  document.getElementById("octDn").classList.toggle("lit", octave < 0);
  document.getElementById("octUp").classList.toggle("lit", octave > 0);
}

// ---------------------------------------------------------------- keys (F3..G5, as FM1 Modular)
const WHITE = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26];
const BLACK = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25];
const BLACK_AFTER = [0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14];
const noteNameMidi = (m) => KEYS12[((Math.round(m) % 12) + 12) % 12] + (Math.floor(Math.round(m) / 12) - 1);
const noteName = (k) => noteNameMidi(keyToNote(k));
const bed = document.getElementById("keybed"), keyEls = [];
function placeKey(k, leftPct, topPct) {
  const el = document.createElement("button");
  el.type = "button"; el.className = "key";
  el.style.left = `calc(${leftPct}% - (100% / 16 * .36))`; el.style.top = `calc(${topPct}% - var(--kh) / 2)`;
  el.setAttribute("aria-label", `Key ${KEYS12[(53 + k) % 12]}`);
  el.addEventListener("pointerdown", (e) => { e.preventDefault(); el.setPointerCapture(e.pointerId); const r = el.getBoundingClientRect(); keyDown(k, 0.35 + 0.65 * clamp((e.clientY - r.top) / r.height, 0, 1)); });
  const up = () => keyUp(k);
  el.addEventListener("pointerup", up); el.addEventListener("pointercancel", up); el.addEventListener("lostpointercapture", up);
  bed.appendChild(el); keyEls[k] = el;
}
WHITE.forEach((k, i) => placeKey(k, (i + .5) / 16 * 100, 74));
BLACK_AFTER.forEach((a, j) => placeKey(BLACK[j], (a + 1) / 16 * 100, 28));
const velCurve = (v) => clamp(v, 0, 1) ** (2 ** ((0.5 - glo.p[G_VEL]) * 3));
function keyDown(k, vel = 0.75) { vel = velCurve(vel); keyVel[k] = vel; held.add(k); if (hold) { if (latched.has(k)) latched.delete(k); else latched.add(k); } recEvent({ type: "key", k, on: true, vel }); refreshKeys(); }
function keyUp(k) { if (!held.has(k)) return; held.delete(k); recEvent({ type: "key", k, on: false }); refreshKeys(); }
const keyVel = [], sounding = new Map();
function refreshKeys() {
  const midiLit = new Set([...midiNotes.keys()].map((nn) => nn - 53 - 12 * octave));
  keyEls.forEach((el, k) => { if (!el) return; el.classList.toggle("on", held.has(k) || latched.has(k) || playKeys.has(k) || midiLit.has(k)); el.classList.toggle("held", held.has(k)); });
  // what should sound: held, latched or replayed keys
  const want = new Set([...held, ...latched, ...playKeys]);
  for (const [k, note] of sounding) if (!want.has(k)) { sounding.delete(k); engine(["note_off", note]); }
  for (const k of want) if (!sounding.has(k)) { const note = keyToNote(k); sounding.set(k, note); engine(["note_on", note, keyVel[k] ?? 0.75]); onNote(note, keyVel[k] ?? 0.75); }
  draw();
}
function quantize(note) {
  if (scl.p[0] !== 1) return note;
  const deg = SCALES[Object.keys(SCALES)[scl.p[2]]], root = scl.p[1];
  let best = note, dist = 99;
  for (let o = -1; o <= 1; o++) for (const d of deg) { const c = Math.floor((note - root) / 12 + o) * 12 + root + d; const dd = Math.abs(c - note); if (dd < dist || (dd === dist && c < best)) { dist = dd; best = c; } }
  return best;
}
const keyToNote = (k) => quantize(53 + k + 12 * octave);                     // key 0 is F3
const KEYMAP = {};
["KeyZ", "KeyX", "KeyC", "KeyV", "KeyB", "KeyN", "KeyM", "Comma", "Period", "Slash"].forEach((c, i) => KEYMAP[c] = WHITE[i]);
[["KeyS", 1], ["KeyD", 3], ["KeyF", 5], ["KeyH", 8], ["KeyJ", 10], ["KeyL", 13], ["Semicolon", 15], ["Quote", 17]].forEach(([c, k]) => KEYMAP[c] = k);
["KeyQ", "KeyW", "KeyE", "KeyR", "KeyT", "KeyY", "KeyU", "KeyI", "KeyO"].forEach((c, i) => KEYMAP[c] = 18 + i);
addEventListener("keydown", (e) => {
  if (e.metaKey || e.ctrlKey || e.altKey || e.repeat) return;
  if (e.target.closest?.(".knob") || e.target.closest?.("input,textarea")) return;
  if (e.code === "Minus" || e.code === "Equal") { e.preventDefault(); document.getElementById(e.code === "Minus" ? "octDn" : "octUp").click(); return; }
  const k = KEYMAP[e.code]; if (k !== undefined) { e.preventDefault(); keyDown(k); }
});
addEventListener("keyup", (e) => { const k = KEYMAP[e.code]; if (k !== undefined) keyUp(k); });
