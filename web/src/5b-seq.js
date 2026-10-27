// ================================================================ 5b. chords (SCL), step sequencer with parameter locks (SEQ)

// ---------------------------------------------------------------- scale and chords
const CH_STEPS = { TRIAD: [0, 2, 4], "7TH": [0, 2, 4, 6], SUS2: [0, 1, 4], SUS4: [0, 3, 4], ADD9: [0, 2, 4, 8], "6TH": [0, 2, 4, 5], "9TH": [0, 2, 4, 6, 8], QUARTAL: [0, 3, 6] };
const scaleDegs = () => SCALES[Object.keys(SCALES)[chd.p[C_SCALE]]];
function snapToScale(note) {
  const deg = scaleDegs(), root = chd.p[C_KEY];
  let best = note, dist = 99;
  for (let o = -1; o <= 1; o++) for (const d of deg) { const c = Math.floor((note - root) / 12 + o) * 12 + root + d; const dd = Math.abs(c - note); if (dd < dist || (dd === dist && c < best)) { dist = dd; best = c; } }
  return best;
}
const quantize = (note) => chd.p[C_LOCK] === 1 ? snapToScale(note) : note;
// one key in, a chord out: the root is snapped into KEY + SCALE and the chord is stacked from scale degrees
function chordNotes(raw) {
  const type = CHORD_TYPES[chd.p[C_TYPE]];
  if (type === "OFF") return [quantize(raw)];
  const r = snapToScale(raw), deg = scaleDegs(), key = chd.p[C_KEY], N = deg.length;
  const rel = r - key, o = Math.floor(rel / 12), i = deg.indexOf(((rel % 12) + 12) % 12);
  const dn = (k) => key + 12 * (o + Math.floor((i + k) / N)) + deg[(((i + k) % N) + N) % N];
  let notes = type === "POWER" ? [r, r + 7, r + 12] : CH_STEPS[type].map(dn);
  for (let k = 0; k < Math.min(chd.p[C_INV], notes.length - 1); k++) { notes.sort((a, b) => a - b); notes[0] += 12; }
  notes.sort((a, b) => a - b);
  if (chd.p[C_SPREAD] > .33 && notes.length >= 3) { notes[notes.length - 2] -= 12; notes.sort((a, b) => a - b); }   // drop 2
  if (chd.p[C_SPREAD] > .66) notes[notes.length - 1] += 12;                                                        // open the top
  return [...new Set(notes)].filter((x) => x >= 12 && x <= 120).sort((a, b) => a - b);
}
function chordQuality(iv) {
  const h = (x) => iv.has(x), third = h(4) ? 4 : h(3) ? 3 : 0, used = new Set([0]);
  let name;
  if (third === 4) { name = h(8) && !h(7) ? "aug" : ""; used.add(4); used.add(h(8) && !h(7) ? 8 : 7); }
  else if (third === 3) { name = h(6) && !h(7) ? "dim" : "m"; used.add(3); used.add(h(6) && !h(7) ? 6 : 7); }
  else if (h(7)) { name = h(5) ? "sus4" : h(2) ? "sus2" : "5"; used.add(7); used.add(h(5) ? 5 : 2); }
  else if (h(5) && h(10)) { name = "sus4"; used.add(5); }
  else return null;
  if (h(11)) { used.add(11); name = name === "m" ? "mMaj7" : name === "" ? "maj7" : name + "M7"; }
  else if (h(10)) { used.add(10); name = name === "" ? "7" : name === "dim" ? "m7b5" : name === "sus4" ? "7sus4" : name + "7"; }
  else if (h(9) && third) { used.add(9); name = name === "dim" ? "dim7" : name + "6"; }
  if (h(2) && third) { used.add(2); name = /7/.test(name) ? name.replace("7", "9") : name + "add9"; }
  let hits = 0; for (const x of iv) if (used.has(x)) hits++;
  return { name, score: hits * 2 - (iv.size - hits) * 3 + (third ? 1 : 0) };
}
function chordName(notes) {
  if (!notes || !notes.length) return "—";
  if (notes.length === 1) return noteNameMidi(notes[0]);
  const pc = (x) => ((x % 12) + 12) % 12, pcs = [...new Set(notes.map(pc))], bass = pc(Math.min(...notes));
  let best = null;
  for (const r of pcs) {
    const q = chordQuality(new Set(pcs.map((p) => (p - r + 12) % 12)));
    if (q && (!best || q.score > best.score || (q.score === best.score && r === bass))) best = { r, ...q };
  }
  if (!best) return notes.slice(0, 3).map(noteNameMidi).join(" ");
  return KEYS12[best.r] + best.name + (best.r !== bass ? "/" + KEYS12[bass] : "");
}

// ---------------------------------------------------------------- timed engine calls (scheduled against the audio clock)
const audioNow = () => (audio.ctx ? audio.ctx.currentTime : 0);
function engineAt(at, calls, tag = "n") { if (audio.node) audio.node.port.postMessage({ type: "timed", at, calls, tag }); }
let lastChord = { notes: [], t: -9 };
// a chord from the keys: strummed low to high when STRUM is up
function notesOn(notes, vel) {
  const strum = chd.p[C_STRUM] * .08, now = audioNow();
  const at = notes.map((_, i) => now + i * strum);
  notes.forEach((nt, i) => {
    if (!strum || !audio.ctx) engine(["note_on", nt, vel]); else engineAt(at[i], [["note_on", nt, vel]]);
    if (i && strum) setTimeout(() => onNote(nt, vel), i * strum * 1000); else onNote(nt, vel);
  });
  if (notes.length > 1 || CHORD_TYPES[chd.p[C_TYPE]] !== "OFF") lastChord = { notes, t: animT };
  return { notes, at };
}
// note-offs never overtake a strummed note-on that is still waiting
function notesOff(e) {
  const now = audioNow();
  e.notes.forEach((nt, i) => { if (e.at[i] > now) engineAt(e.at[i] + .003, [["note_off", nt]]); else engine(["note_off", nt]); });
}

// ---------------------------------------------------------------- sequencer
const ss = { run: false, gridT: 0, step: 0, cur: -1, curT: 0, dur: .125, tied: null, active: new Map(), on: new Set(),
             rec: false, lock: false, cursor: 0, entry: null, recGroup: null, touched: new Map(), flash: new Array(16).fill(-9) };
const stepDur = () => 60 / glo.p[G_TEMPO] / 4 * RATE_MUL[seq.p[2]];
const seqLen = () => seq.p[1];
function seqStart() {
  if (!audio.on) { seq.p[0] = 0; say("SWITCH ON FIRST", "SEQ"); return; }
  ss.run = true; seq.p[0] = 1; ss.step = 0; ss.cur = -1; ss.tied = null;
  ss.gridT = audioNow() + .06;
  engineAt(ss.gridT, [["reset_clock"]], "seq");          // Grids and the gate bus start on step 1 too
  say("SEQ PLAY", "SEQ"); seqTick();
}
function seqStop() {
  ss.run = false; seq.p[0] = 0; ss.rec = false;
  if (audio.node) audio.node.port.postMessage({ type: "cancel", tag: "seq" });
  for (const nt of ss.on) engine(["note_off", nt]);
  ss.on.clear(); ss.tied = null; ss.cur = -1;
  for (const [, ref] of ss.active) { const c = refCall(ref, refBase(ref)); if (c) engine(c); }
  ss.active.clear(); say("SEQ STOP", "SEQ");
}
function seqTick() {
  if (!ss.run || !audio.on) return;
  if (ss.step >= seqLen()) ss.step = 0;
  const ahead = audioNow() + .12;
  while (ss.gridT < ahead) {
    const d = stepDur(), i = ss.step;
    scheduleStep(i, ss.gridT + (i % 2 ? seq.p[3] * .5 * d : 0), d);
    ss.gridT += d; ss.step = (i + 1) % seqLen();
  }
}
setInterval(seqTick, 25);
const sameNotes = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);
function scheduleStep(i, t, d) {
  const s = seq.steps[i], wait = Math.max(0, (t - audioNow()) * 1000);
  setTimeout(() => { ss.cur = i; ss.curT = t; ss.dur = d; dirty = true; }, wait);
  // parameter locks: a trig step sets its locks; anything the last step locked goes back to the knob
  const want = new Map();
  if (s.trig) for (const [k, l] of Object.entries(s.locks)) want.set(k, l);
  for (const [k, ref] of ss.active) if (!want.has(k)) { const c = refCall(ref, refBase(ref)); if (c) engineAt(t, [c], "seq"); }
  ss.active = new Map();
  for (const [k, l] of want) { const c = refCall(l.ref, l.v); if (c) { engineAt(t, [c], "seq"); ss.active.set(k, l.ref); } }
  const skip = s.skipAt && Math.abs(s.skipAt - t) < d / 2;
  if (skip) s.skipAt = 0;
  const play = s.trig && s.notes.length && !skip && Math.random() < s.chance;
  if (!play) return;
  const same = ss.tied && sameNotes(ss.tied, s.notes);
  if (ss.tied && !same) engineAt(t, ss.tied.map((nt) => ["note_off", nt]), "seq");
  if (!same) {
    engineAt(t + .001, s.notes.map((nt) => ["note_on", nt, s.vel]), "seq");
    s.notes.forEach((nt) => ss.on.add(nt));
    setTimeout(() => { s.notes.forEach((nt) => onNote(nt, s.vel)); seqLight(s.notes); }, wait);
  }
  setTimeout(() => { ss.flash[i] = animT; }, wait);
  if (STEP_LENS[s.len] === "TIE") ss.tied = [...s.notes];
  else { engineAt(t + STEP_LEN_F[s.len] * d, s.notes.map((nt) => ["note_off", nt]), "seq"); ss.tied = null; }
}
// light the keybed under sequenced notes
function seqLight(notes) {
  for (const nt of notes) {
    const el = keyEls[nt - 53 - 12 * octave]; if (!el) continue;
    el.classList.add("seq"); setTimeout(() => el.classList.remove("seq"), Math.max(90, ss.dur * 600));
  }
}
// where a note played now lands: this step if early in it, else the next one
function nowStep() {
  if (ss.cur < 0) return { i: 0, next: false };
  return audioNow() - ss.curT > ss.dur / 2 ? { i: (ss.cur + 1) % seqLen(), next: true } : { i: ss.cur, next: false };
}
// keys and MIDI notes on the SEQ pages: step entry; with live REC: written into the running pattern
function seqInput(notes, vel) {
  if (ss.rec && ss.run) {
    const { i, next } = nowStep(), st = seq.steps[i];
    if (next) st.skipAt = ss.curT + ss.dur;                // it already sounds: don't play it twice this pass
    if (!ss.recGroup || ss.recGroup.step !== i) { ss.recGroup = { step: i }; st.notes = []; }
    for (const nt of notes) if (!st.notes.includes(nt)) st.notes.push(nt);
    st.notes.sort((a, b) => a - b); st.trig = 1; st.vel = vel; dirty = true; return;
  }
  if (page !== "SEQ" && page !== "STEP") return;
  if (!ss.entry) { ss.entry = { step: ss.cursor }; seq.steps[ss.cursor].notes = []; }
  const st = seq.steps[ss.entry.step];
  for (const nt of notes) if (!st.notes.includes(nt)) st.notes.push(nt);
  st.notes.sort((a, b) => a - b); st.trig = 1; st.vel = vel; dirty = true;
}
function seqRelease() {
  if (held.size || midiNotes.size) return;
  if (ss.entry) { if (page === "SEQ") ss.cursor = (ss.entry.step + 1) % seqLen(); ss.entry = null; dirty = true; }
  ss.recGroup = null;
}
function seqEncoder(role, k) {
  if (role === "select") { ss.cursor = clamp(ss.cursor + Math.sign(k), 0, 15); return; }
  if (role === "algo") {
    if (page === "SEQ") {
      const dir = Math.sign(k);
      for (const st of seq.steps) st.notes = st.notes.map((x) => clamp(x + dir, 12, 120));
      say(`PATTERN ${dir > 0 ? "UP" : "DOWN"} 1`, "SEQ");
    } else {
      const st = seq.steps[ss.cursor];
      if (k > 0) { say("TURN LEFT TO CLEAR", "STEP"); return; }
      if (Object.keys(st.locks).length) { st.locks = {}; say(`STEP ${ss.cursor + 1} LOCKS CLEARED`, "STEP"); }
      else { Object.assign(st, blankStep()); say(`STEP ${ss.cursor + 1} CLEARED`, "STEP"); }
    }
    return;
  }
  const i = role;
  if (page === "SEQ") {
    const p = SEQDEF.pages[0][i], v = stepParam(p, seq.p[i], k);
    if (i === 0) { if (v && !ss.run) seqStart(); else if (!v && ss.run) seqStop(); }
    else seq.p[i] = v;
    if (i === 1) ss.cursor = Math.min(ss.cursor, 15);
  } else {
    const st = seq.steps[ss.cursor], key = ["trig", "vel", "len", "chance"][i];
    st[key] = stepParam(STEPDEF.pages[0][i], st[key], k);
  }
}

// ---------------------------------------------------------------- parameter locks
function refCall(ref, v) {
  if (ref.w === "macro") return ["set_macro", ref.i, v];
  if (ref.w === "AMP") return ["set_param", 6, ref.idx, v];
  if (ref.w === "LFOS") return ["set_param", 8, ref.idx, v];
  if (ref.w === "slot") return rack[ref.slot].mod === ref.mod ? ["set_param", SLOTS.findIndex((s) => s.id === ref.slot), ref.idx, v] : null;
  return null;
}
function refBase(ref) {
  if (ref.w === "macro") return macros[ref.i];
  if (ref.w === "AMP") return amp.p[ref.idx];
  if (ref.w === "LFOS") return lfo.p[ref.idx];
  return rack[ref.slot].p[ref.idx];
}
function refParam(ref) {
  if (ref.w === "macro") return { l: `M${ref.i + 1}`, t: "num" };
  if (ref.w === "AMP") return AMP.pages.flat()[ref.idx];
  if (ref.w === "LFOS") return LFOS.pages.flat()[ref.idx];
  return MODULES[ref.mod].pages.flat()[ref.idx];
}
function refLabel(ref) {
  const p = refParam(ref);
  if (ref.w === "macro") return p.l;
  if (ref.w === "AMP") return `AMP.${p.l}`;
  if (ref.w === "LFOS") return `LFO.${p.l}`;
  return `${SLOTS.find((s) => s.id === ref.slot).name}.${p.l}`;
}
// the step that knob turns lock: the cursor step while SEQ is held, the playing step while live-recording
function lockTarget() { if (ss.lock) return ss.cursor; if (ss.rec && ss.run) return nowStep().i; return -1; }
function lockRefFor(slot, i) {
  if (page === "PERFORM") return { w: "macro", i };
  if (page === "AMP" || page === "LFOS") { const def = page === "AMP" ? AMP : LFOS, p = def.pages[subpage][i]; return p ? { w: page, idx: def.pages.slice(0, subpage).flat().length + i } : null; }
  if (slot) { const st = rack[slot.id], m = MODULES[st.mod]; if (!m || !m.pages[subpage][i]) return null; return { w: "slot", slot: slot.id, mod: st.mod, idx: m.pages.slice(0, subpage).flat().length + i }; }
  return null;
}
function lockTurn(ref, k) {
  const i = lockTarget(), st = seq.steps[i], key = lockKey(ref), p = refParam(ref);
  const cur = st.locks[key] ? st.locks[key].v : refBase(ref);
  const v = ref.w === "macro" ? clamp(cur + k * .01, 0, 1) : stepParam(p, cur, k);
  st.locks[key] = { ref, v }; st.trig = 1;
  const c = refCall(ref, v); if (c) engine(c);
  ss.touched.set(key, ref); if (ss.run) ss.active.set(key, ref);
  say(`STEP ${i + 1} · ${refLabel(ref)} ${fmt(p, v)}`, "LOCK");
}
function setSeqLock(on) {
  ss.lock = on;
  if (!on) { if (!ss.run) for (const [, ref] of ss.touched) { const c = refCall(ref, refBase(ref)); if (c) engine(c); } ss.touched.clear(); }
  say(on ? `LOCK STEP ${ss.cursor + 1}` : "LOCKS DONE", "LOCK");
}
// what a cell shows while locking: the step's value, marked
function lockView(makeRef, count, base) {
  const i = lockTarget(); if (i < 0) return { vals: base, marks: null };
  const st = seq.steps[i], marks = [], vals = base.map((v, j) => {
    const ref = j < count ? makeRef(j) : null, l = ref && st.locks[lockKey(ref)];
    marks.push(!!l); return l ? l.v : v;
  });
  return { vals, marks };
}

// ---------------------------------------------------------------- CHORD page band: a keyboard lighting the chord, and its name
function visChord(X, Y, W, H, c) {
  const deg = scaleDegs(), key = chd.p[C_KEY], type = CHORD_TYPES[chd.p[C_TYPE]];
  const inScale = (pc) => deg.includes((pc - key + 12) % 12);
  const fresh = animT - lastChord.t < 1.2 && lastChord.notes.length;
  const show = fresh ? lastChord.notes : type === "OFF" ? [] : chordNotes(60 + key);
  const lit = new Map();
  for (const p of recentPulses(.7)) lit.set(((p.note % 12) + 12) % 12, Math.max(lit.get(p.note % 12) || 0, 1 - (animT - p.t) / .7));
  const pcsShown = new Set(show.map((x) => ((x % 12) + 12) % 12));
  // two octaves of keys
  const kb = 148, whites = [0, 2, 4, 5, 7, 9, 11], kw = kb / 14, x0 = X + 4, y0 = Y + 6, kh = H - 12;
  for (let o = 0; o < 2; o++) whites.forEach((pc, i) => {
    const x = x0 + (o * 7 + i) * kw, on = pcsShown.has(pc), glowA = lit.get(pc) || 0;
    rect(x + .5, y0, kw - 1, kh, on ? rgba(c, .55 + .45 * glowA) : inScale(pc) ? (pc === key ? "#3b3524" : "#1e221c") : "#101010", 2);
    if (on) dot(x + kw / 2, y0 + kh - 5, 1.4 + glowA, "#fff", glowA > .1);
    else if (inScale(pc)) rect(x + kw / 2 - 1, y0 + kh - 6, 2, 2, pc === key ? C.gold : "#4a5a44");
  });
  const blk = [[1, 0], [3, 1], [6, 3], [8, 4], [10, 5]];
  for (let o = 0; o < 2; o++) for (const [pc, after] of blk) {
    const x = x0 + (o * 7 + after + 1) * kw - kw * .32, on = pcsShown.has(pc), glowA = lit.get(pc) || 0;
    rect(x, y0, kw * .64, kh * .56, on ? rgba(c, .6 + .4 * glowA) : inScale(pc) ? (pc === key ? "#5a4a26" : "#2f3a2b") : "#050505", 2);
    if (on) dot(x + kw * .32, y0 + kh * .56 - 4, 1.2 + glowA, "#fff", false);
  }
  // the chord's name, with a ring that pulses on each new chord
  const nx = X + kb + 12, cx = nx + (W - kb - 16) / 2, age = animT - lastChord.t;
  if (age < .6) { g.strokeStyle = rgba(c, (.6 - age) * 1.4); g.lineWidth = 1.5; g.beginPath(); g.arc(cx, Y + H / 2 - 4, 14 + age * 40, 0, TAU); g.stroke(); }
  const name = type === "OFF" ? "SINGLE" : type === "QUARTAL" && show.length ? `${KEYS12[((show[0] % 12) + 12) % 12]} 4THS` : chordName(show);
  T(cx, Y + H / 2 + 2, name.length > 7 ? name.slice(0, 7) : name, { a: "center", size: name.length > 5 ? 13 : 17, c: type === "OFF" ? C.dim : C.fg });
  T(cx, Y + H / 2 + 16, `${KEYS12[key]} ${Object.keys(SCALES)[chd.p[C_SCALE]]}`, { a: "center", size: 8, c: chd.p[C_LOCK] ? c : C.dim });
  const nv = budget().voices;
  if (show.length > nv) T(cx, Y + 12, `${nv} VOICE${nv > 1 ? "S" : ""}!`, { a: "center", size: 8, c: C.warn });
}

// ---------------------------------------------------------------- SEQ and STEP pages
function stepLabel(st) { return st.notes.length >= 3 ? chordName(st.notes) : st.notes.map(noteNameMidi).join(" "); }
function drawSeqGrid() {
  const c = KC.seq, len = seqLen(), lo = 36, hi = 96;
  for (let i = 0; i < 16; i++) {
    const st = seq.steps[i], x = 6 + (i % 8) * 28.5, y = 29 + Math.floor(i / 8) * 31, w = 26, h = 28, inLen = i < len;
    const fl = Math.max(0, 1 - (animT - ss.flash[i]) * 4);
    rect(x, y, w, h, !inLen ? "#070707" : st.trig ? "#1b1a19" : "#111110", 4);
    if (fl > 0 && inLen) rect(x, y, w, h, rgba(c, fl * .5), 4);
    if (inLen && i % 4 === 0) rect(x + 2, y + h - 3, 4, 1, C.dim);
    // notes as pitch marks
    for (const nt of st.notes) {
      const yy = y + h - 5 - ((clamp(nt, lo, hi) - lo) / (hi - lo)) * (h - 9);
      rect(x + 4, yy, w - 8, 2, rgba(st.trig ? c : C.dim, inLen ? (st.trig ? .35 + .65 * st.vel : .35) : .15), 1);
    }
    if (st.trig && !st.notes.length) { g.fillStyle = rgba(C.gold, inLen ? .8 : .3); g.beginPath(); g.moveTo(x + w / 2, y + 9); g.lineTo(x + w / 2 + 5, y + 14); g.lineTo(x + w / 2, y + 19); g.lineTo(x + w / 2 - 5, y + 14); g.fill(); }
    if (Object.keys(st.locks).length) dot(x + w - 4, y + 4, 1.6, C.gold, false);
    if (st.trig && STEP_LENS[st.len] === "TIE" && inLen) rect(x + w - 2, y + h / 2 - 1, 5, 2, c);
    if (st.trig && st.chance < 1) for (let k = 0; k < 5; k++) if (k / 5 < st.chance) rect(x + 4 + k * 4, y + 3, 2, 1, C.dim);
    if (i === ss.cur && ss.run) frame(x - 1, y - 1, w + 2, h + 2, rgba(c, .9), 5);
    if (i === ss.cursor && (Math.floor(animT * 3) % 2 || ss.lock)) frame(x, y, w, h, ss.lock ? C.gold : "#fff", 4);
    if (i === ss.cursor && !(Math.floor(animT * 3) % 2) && !ss.lock) frame(x, y, w, h, "#666", 4);
  }
  // a playhead gliding across the pattern
  rect(6, 91, 228, 2, C.faint, 1);
  if (ss.run && ss.cur >= 0) {
    const f = clamp((audioNow() - ss.curT) / ss.dur, 0, 1), px = 6 + ((ss.cur + f) / len) * 228;
    rect(6, 91, px - 6, 2, rgba(c, .5), 1); dot(px, 92, 1.8, c);
  }
}
function drawSeq() {
  const c = KC.seq, rec = ss.rec && ss.run;
  if (page === "SEQ") {
    header("SEQ", c, `${ss.run ? "PLAY" : "STOP"} · ${SEQ_RATES[seq.p[2]]}`, "SEQ");
    drawSeqGrid();
    cells(SEQDEF.pages[0], seq.p, null, c);
  } else {
    const st = seq.steps[ss.cursor], nl = Object.keys(st.locks).length;
    header("STEP", c, `${ss.cursor + 1} · ${st.notes.length ? stepLabel(st) : "EMPTY"}${nl ? ` · ${nl}L` : ""}`, "STEP");
    drawSeqGrid();
    cells(STEPDEF.pages[0], [st.trig, st.vel, st.len, st.chance], null, c);
  }
  if (rec && Math.floor(animT * 3) % 2) dot(214, 13, 3, C.warn, false);
  footer();
}
