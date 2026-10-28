// ================================================================ 5d. the sequencer, after Clipper / SLOOP 2.7.2
// SEQ tapped: NOTES (a piano roll), PATTERN, SPICE, GEN. Held buttons are layers: SEQ steps, EDIT erase, ARP note repeat,
// GLO fills and tap tempo, SEL (the SCL key) key and chords. PLAY starts and stops; REC records into the pattern.

const SEQ_PAGES = ["NOTES", "PATTERN", "SPICE", "GEN", "BANKS", "SONG"];
const isSeqPage = () => SEQ_PAGES.includes(page);
const REP_RATES = ["1/8", "1/16", "1/32", "32T", "1/64"], REP_16THS = [2, 1, .5, 1 / 3, .25];
const SHEET_ROWS = ["SLIDE", "NUDGE", "FILL", "MOVE", "COPY", "DELETE"];
const ss = {
  run: false, gridT: 0, step: 0, pos16: 0, cur: -1, curT: 0, curGridT: 0, dur: .125, active: new Map(), offs: new Map(), on: new Set(), offId: 0,
  rec: false, armed: false, free: null, recMode: 0, recBars: 1, recNotes: new Map(), clearT: 0,
  cursor: 0, noteIx: -1, zoom: 16, entry: null, entryHeld: new Set(), sheet: null, flash: new Array(MAXSTEPS).fill(-9),
  layer: null, latched: false, layerT: 0, heldSteps: new Set(), stepEdited: false, seqPage: 0, setNotes: [60],
  fillHeld: false, fillBar: -1, fillPending: false, taps: [], repeat: null, repRate: 1, repNext: 0, erase: null,
  lockRef: null, gen: { tool: 0, a: .5, b: .3, seed: 1 },
  // 1.6 (after Melodee): banks, a queued bank, the song, the jam log, capture and quantize-on-take
  queue: null, queueArm: false, loopFirst: true, songOn: false, songRow: 0, songRep: 0, songCur: 0, trn: 0, jam: [],
  bankAct: 0, bankTgt: 1, queueNow: false, recQ: true, cap: [],
};
const seqLen = () => seq.len;
const s16 = () => 60 / glo.p[G_TEMPO] / 4;
const stepDur = () => s16() * DIV_16THS[seq.div];
const triplet = () => seq.div === 4 || seq.div === 5;
const hasNotes = () => seq.steps.slice(0, seq.len).some((x) => x.notes.length);

// ---------------------------------------------------------------- transport
function seqStart(at) {
  if (!audio.on) { say("SWITCH ON FIRST", "SEQ"); return; }
  ss.run = true; ss.step = 0; ss.pos16 = 0; ss.fillBar = -1;
  ss.gridT = at ?? audioNow() + .06;
  ss.loopFirst = true; ss.jam = [];
  if (ss.songOn) songRow(firstSongRow());
  ss.cur = 0; ss.curT = ss.curGridT = ss.gridT; ss.dur = stepDur();
  engineAt(ss.gridT, [["reset_clock"]], "seq");          // Grids, the gate bus and the mod lanes start on step 1 too
  msRealign(ss.gridT); midiSend([0xFA], ss.gridT);
  if (ss.armed) { ss.armed = false; ss.rec = true; }
  say(ss.rec ? "REC" : "PLAY", ss.rec ? "REC" : "SEQ"); seqTick();
}
function seqStop() {
  ss.run = false; ss.rec = false; ss.repeat = null;
  if (audio.node) { audio.node.port.postMessage({ type: "cancel", tag: "seq" }); for (const [, o] of ss.offs) audio.node.port.postMessage({ type: "cancel", tag: o.tag }); }
  for (const nt of ss.on) engine(["note_off", nt]);
  noteOut([...ss.on].map(Math.round), 0, null, audioNow());
  ss.on.clear(); ss.offs.clear(); ss.cur = -1; ss.recNotes.clear();
  for (const [, ref] of ss.active) { const c = refCall(ref, refBase(ref)); if (c) engine(c); }
  ss.active.clear(); midiSend([0xFC]); say("STOP", "SEQ"); draw();
}
const seqToggle = () => (ss.run ? seqStop() : seqStart());
function fillActive(pos16) { return ss.fillHeld || Math.floor(pos16 / 16) === ss.fillBar; }

// ---------------------------------------------------------------- scheduling
function seqTick() {
  if (!ss.run || !audio.on) return;
  if (ss.step >= seq.len) ss.step = 0;
  const ahead = audioNow() + .12;
  while (ss.gridT < ahead) {
    if (ss.step === 0) { if (ss.loopFirst) ss.loopFirst = false; else loopEnd(); }
    const d = stepDur(), i = ss.step, sw = !triplet() && i % 2 ? seq.swing * .5 * d : 0;
    if (ss.fillPending && ss.pos16 % 16 < DIV_16THS[seq.div] - 1e-6) { ss.fillBar = Math.floor(ss.pos16 / 16); ss.fillPending = false; }
    scheduleStep(i, ss.gridT, ss.gridT + sw, d, fillActive(ss.pos16));
    ss.gridT += d; ss.pos16 += DIV_16THS[seq.div]; ss.step = (i + 1) % seq.len;
  }
}
setInterval(seqTick, 25);
// ---------------------------------------------------------------- banks and song (1.6)
const BANK_ACTS = ["COPY TO", "CLEAR", "DOUBLE", "HALVE"];
const bankEmpty = (i) => !banks[i].steps.slice(0, banks[i].len).some((x) => x.notes.length || Object.keys(x.locks).length);
const firstSongRow = () => Math.max(0, song.rows.findIndex((r) => r.bank >= 0));
function songRow(r) { const row = song.rows[r]; ss.songRow = r; ss.songRep = 0; ss.trn = row.trn; if (row.bank >= 0) useBank(row.bank); }
// the end of each pass: log it, then follow the song or take the queued bank
function loopEnd() {
  ss.jam.push(bankIx); if (ss.jam.length > 128) ss.jam.shift();
  if (ss.songOn) {
    if (++ss.songRep >= song.rows[ss.songRow].rep) {
      let r = ss.songRow + 1; while (r < 16 && song.rows[r].bank < 0) r++;
      songRow(r < 16 ? r : firstSongRow());
    }
    return;
  }
  if (ss.queue !== null) { useBank(ss.queue); ss.queue = null; if (ss.queueArm) { ss.rec = true; ss.queueArm = false; } dirty = true; }
}
// a bank key or Knob 1 on BANKS: now when stopped (an empty bank arms REC, as Melodee), else at the end of the pass
function selectBank(i) {
  const empty = bankEmpty(i);
  if (ss.songOn) { ss.songOn = false; ss.trn = 0; say("SONG OFF", "SEQ"); }
  if (!ss.run || ss.queueNow) {
    useBank(i); ss.step = ss.step % seq.len; ss.cursor = Math.min(ss.cursor, seq.len - 1);
    if (empty && !ss.run) { ss.armed = true; say(`BANK ${BANK_NAMES[i]} · EMPTY · REC READY`, "REC"); } else say(`BANK ${BANK_NAMES[i]}`, "SEQ");
  } else { ss.queue = i === bankIx ? null : i; ss.queueArm = empty; say(ss.queue === null ? "QUEUE CLEARED" : `BANK ${BANK_NAMES[i]} NEXT${empty ? " · REC" : ""}`, "SEQ"); }
  draw();
}
function bankAction() {
  const act = BANK_ACTS[ss.bankAct], b = banks[bankIx];
  if (act === "COPY TO") { if (ss.bankTgt === bankIx) { say("PICK ANOTHER BANK", "SEQ"); return; } banks[ss.bankTgt] = JSON.parse(JSON.stringify(b)); say(`${BANK_NAMES[bankIx]} COPIED TO ${BANK_NAMES[ss.bankTgt]}`, "SEQ"); }
  if (act === "CLEAR") { const L = b.len; b.steps = Array.from({ length: MAXSTEPS }, blankStep); b.len = L; say(`BANK ${BANK_NAMES[bankIx]} CLEARED`, "SEQ"); }
  if (act === "DOUBLE") { if (b.len * 2 > MAXSTEPS) { say("64 STEPS AT MOST", "SEQ"); return; } for (let i = 0; i < b.len; i++) b.steps[b.len + i] = JSON.parse(JSON.stringify(b.steps[i])); b.len *= 2; say(`LENGTH ×2 · ${b.len}`, "SEQ"); }
  if (act === "HALVE") { b.len = Math.max(1, Math.floor(b.len / 2)); say(`LENGTH ½ · ${b.len}`, "SEQ"); }
  noteEdit(`BANK ${act}`); draw();
}
// the jam log (each pass's bank) into song rows, repeats folded together
function jamToSong() {
  if (!ss.jam.length) { say("PLAY SOME BANKS FIRST", "SEQ"); return; }
  const rows = [];
  for (const b of ss.jam) { const last = rows[rows.length - 1]; if (last && last.bank === b && last.rep < 16) last.rep++; else rows.push({ bank: b, rep: 1, trn: 0 }); }
  song = blankSong(); rows.slice(0, 16).forEach((r, i) => { song.rows[i] = r; });
  noteEdit("JAM TO SONG"); say(`SONG FROM JAM · ${Math.min(16, rows.length)} ROWS`, "SEQ"); draw();
}
function songToggle() {
  if (song.rows.every((r) => r.bank < 0)) { say("SONG IS EMPTY", "SEQ"); return; }
  ss.songOn = !ss.songOn; ss.queue = null;
  if (ss.songOn) { songRow(firstSongRow()); if (ss.run) { seqStop(); seqStart(); } } else ss.trn = 0;
  say(ss.songOn ? "SONG ON" : "SONG OFF", "SEQ"); draw();
}
// CAPTURE (hold REC): what you just played without recording goes into the pattern
function capNote(id, notes, vel) { ss.cap.push({ id, notes, vel, t: audioNow(), off: null, pos: ss.run ? nowPos() : null }); if (ss.cap.length > 400) ss.cap.shift(); }
function capOff(id) { const e = [...ss.cap].reverse().find((x) => x.id === id && x.off === null); if (e) e.off = audioNow(); }
function capture() {
  const now = audioNow();
  if (ss.run) {
    const win = seq.len * stepDur(), ev = ss.cap.filter((e) => e.pos !== null && e.t >= now - win);
    if (!ev.length) { say("NOTHING TO CAPTURE", "REC"); return; }
    const L = seq.len;
    for (const e of ev) {
      const raw = Math.round(e.pos), i = ((raw % L) + L) % L, st = seq.steps[i];
      const len = clamp(Math.round(((e.off ?? now) - e.t) / stepDur() * 4) / 4, .25, MAXSTEPS);
      for (const x of e.notes) if (st.notes.length < 6 && !st.notes.some((o) => o.n === x)) st.notes.push({ n: x, len, lvl: 2, vel: e.vel });
      if (!ss.recQ) st.nudge = clamp(Math.round((e.pos - raw) * 64), -32, 31);
      st.notes.sort((a, b) => a.n - b.n);
    }
    noteEdit("CAPTURE"); say(`CAPTURED ${ev.length} NOTES`, "REC"); ss.cap = []; draw(); return;
  }
  // stopped: the last phrase (back to a 2 s gap) becomes a loop, as a free take does
  const ev = []; for (let j = ss.cap.length - 1; j >= 0; j--) { const e = ss.cap[j]; if (ev.length && ev[0].t - (e.off ?? e.t) > 2) break; ev.unshift(e); }
  if (!ev.length) { say("NOTHING TO CAPTURE", "REC"); return; }
  const on = ev.map((e) => e.t), gap = on.length > 1 ? (on[on.length - 1] - on[0]) / (on.length - 1) : .5;
  ss.cap = [];
  closeFreeTake({ t0: on[0], events: ev }, on[on.length - 1] + gap);
}
function hash01(a, b) { let h = (a * 374761393 + b * 668265263) | 0; h = (h ^ (h >>> 13)) * 1274126177; return ((h ^ (h >>> 16)) >>> 0) / 4294967296; }
// SPICE: the same variation each time round, until DICE is turned; what is recorded never changes
function spiceFor(i) {
  const sp = seq.spice || 0, dc = seq.dice || 0;
  if (!sp) return { rest: false, oct: 0, vel: 0, gate: 0 };
  return { rest: hash01(dc, i * 4) < sp * .3, oct: hash01(dc, i * 4 + 1) < sp * .25 ? (hash01(dc, i * 4 + 2) < .5 ? -12 : 12) : 0,
           vel: (hash01(dc, i * 4 + 3) - .5) * sp * .6, gate: (hash01(dc + 7, i) - .5) * sp };
}
function voiceOn(v, vel, t, offT) {
  const prev = ss.offs.get(v);                 // the same pitch still ringing: end it just before this one
  if (prev && prev.t > t) { if (audio.node) audio.node.port.postMessage({ type: "cancel", tag: prev.tag }); engineAt(t, [["note_off", v]], "seq"); }
  const tag = `o${++ss.offId}`;
  engineAt(t + .001, [["note_on", v, vel]], "seq");
  engineAt(offT, [["note_off", v]], tag);
  ss.offs.set(v, { t: offT, tag }); ss.on.add(v);
}
function revertLocks(t, keep = new Map()) {
  for (const [k, ref] of ss.active) if (!keep.has(k)) { const c = refCall(ref, refBase(ref)); if (c) engineAt(t, [c], "seq"); }
  ss.active = new Map();
}
function scheduleStep(i, tg, t0, d, fill) {
  const s = seq.steps[i], now = audioNow(), wait = Math.max(0, (tg - now) * 1000);
  setTimeout(() => { ss.cur = i; ss.curT = t0; ss.curGridT = tg; ss.dur = d; dirty = true; eraseTick(i); }, wait);
  const condOK = s.fill === 0 || (s.fill === 1 ? fill : !fill);
  if (!condOK) { revertLocks(t0); return; }                  // a skipped step is silent whole: no note, no lock
  const want = new Map(Object.entries(s.locks));
  revertLocks(t0, want);
  for (const [k, l] of want) { const c = refCall(l.ref, l.v); if (c) { engineAt(t0, [c], "seq"); ss.active.set(k, l.ref); } }
  const skip = s.skipAt && Math.abs(s.skipAt - tg) < d / 2;
  if (skip) s.skipAt = 0;
  const sp = spiceFor(i);
  if (!s.notes.length || skip || sp.rest || Math.random() >= s.chance) return;
  const t = t0 + (s.nudge || 0) / 64 * d, r = Math.max(1, s.ratchet || 1), outNotes = [];
  for (const nt of s.notes) {
    const vel = clamp((LEVEL_VEL[nt.lvl] ?? nt.vel) + sp.vel, .05, 1), pitch = nt.n + sp.oct + (ss.songOn ? ss.trn : 0);
    for (let k = 0; k < r; k++) {
      const tr = t + k * d / r;
      let len = r > 1 ? d / r * .55 : Math.max(.02, nt.len * d * (1 + sp.gate * .5));
      if (s.slide && r === 1) len = Math.max(len, d) + .03;          // slide: overlap the next note, so a mono voice glides
      for (const v of expandUni([pitch])) voiceOn(v, vel, tr, tr + len);
      noteOut([pitch], vel, tr + .001, tr + len);
    }
    outNotes.push(pitch);
  }
  setTimeout(() => { s.notes.forEach((nt) => onNote(nt.n, LEVEL_VEL[nt.lvl] ?? nt.vel)); seqLight(outNotes, d * r); ss.flash[i] = animT; }, Math.max(0, (t - now) * 1000));
}
function seqLight(notes, d = .12) {
  for (const nt of notes) {
    const el = keyEls[Math.round(nt) - 53 - 12 * octave]; if (!el) continue;
    el.classList.add("seq"); setTimeout(() => el.classList.remove("seq"), Math.max(90, d * 600));
  }
}

// ---------------------------------------------------------------- where now is, in steps
function nowPos() { return ss.cur < 0 ? 0 : ss.cur + (audioNow() - .012 - ss.curGridT) / ss.dur; }   // ~12 ms of key latency taken back

// ---------------------------------------------------------------- notes coming in: step entry, recording, the free take
function seqNoteOn(id, notes, vel) {
  if (!ss.free && !(ss.rec && ss.run)) capNote(id, notes, vel);
  if (ss.free) { ss.free.t0 ??= audioNow(); ss.free.events.push({ id, notes, vel, t: audioNow(), off: null }); dirty = true; return; }
  if (ss.armed && !ss.run) seqStart(audioNow() + .005);
  if (ss.rec && ss.run) { recNoteOn(id, notes, vel); return; }
  if (page !== "NOTES") return;
  if (!ss.entry) { ss.entry = { step: ss.cursor }; seq.steps[ss.cursor].notes = []; }
  const st = seq.steps[ss.entry.step];
  for (const x of notes) if (!st.notes.some((o) => o.n === x) && st.notes.length < 6) st.notes.push({ n: x, len: seq.gate, lvl: 2, vel });
  st.notes.sort((a, b) => a.n - b.n); ss.noteIx = 0; ss.entryHeld.add(id); dirty = true;
}
function seqNoteOff(id) {
  capOff(id);
  if (ss.free) { const e = [...ss.free.events].reverse().find((x) => x.id === id && x.off === null); if (e) e.off = audioNow(); return; }
  if (ss.recNotes.has(id)) recNoteOff(id);
  ss.entryHeld.delete(id);
  if (ss.entry && !ss.entryHeld.size) {
    if (glo.p[G_ADV] !== 1) { ss.cursor = (ss.entry.step + 1) % seq.len; ss.noteIx = seq.steps[ss.cursor].notes.length ? 0 : -1; }
    ss.entry = null; noteEdit("STEP ENTRY"); dirty = true;
  }
}
function recNoteOn(id, notes, vel) {
  const L = seq.len, pos = nowPos(), raw = Math.round(pos), i = ((raw % L) + L) % L, st = seq.steps[i];
  if (raw > ss.cur) st.skipAt = ss.curGridT + (raw - ss.cur) * ss.dur;   // it sounds now: don't play it twice this pass
  // a note pressed over a held one on a MONO or LEGATO sound plays back legato
  if (amp.p[A_MODE] === 1 || amp.p[A_MODE] === 2) for (const [, r] of ss.recNotes) seq.steps[r.step].slide = true;
  const objs = [];
  for (const x of notes) {
    st.notes = st.notes.filter((o) => o.n !== x);
    if (st.notes.length >= 6) st.notes.shift();
    const o = { n: x, len: seq.gate, lvl: 2, vel }; st.notes.push(o); objs.push(o);
  }
  st.notes.sort((a, b) => a.n - b.n);
  if (!ss.recQ) st.nudge = clamp(Math.round((pos - raw) * 64), -32, 31);     // QNT OFF: the timing you played, as a nudge
  ss.recNotes.set(id, { objs, start: pos, step: i });
  ss.flash[i] = animT; dirty = true;
}
function recNoteOff(id) {
  const r = ss.recNotes.get(id); ss.recNotes.delete(id); if (!r) return;
  let dur = nowPos() - r.start; if (dur < 0) dur += seq.len;
  const len = clamp(Math.round(dur * 4) / 4, .25, MAXSTEPS);
  for (const o of r.objs) o.len = len;
  noteEdit("REC");
}
// REC: playing → record now / stop; stopped → arm (the first note starts), or on an empty pattern a free take
function recPress() {
  if (ss.free) { closeFreeTake(); return; }
  if (ss.run) { ss.rec = !ss.rec; ss.recNotes.clear(); say(ss.rec ? "REC" : "REC OFF", "REC"); draw(); return; }
  if (ss.armed) { ss.armed = false; say("REC OFF", "REC"); draw(); return; }
  if (!hasNotes() && ss.recMode === 1) { ss.free = { t0: null, events: [] }; say("FREE TAKE · PLAY FREELY", "REC"); }
  else { if (!hasNotes()) { seq.len = Math.min(MAXSTEPS, Math.round(ss.recBars * 16 / DIV_16THS[seq.div])); } ss.armed = true; say("REC READY · PLAY", "REC"); }
  draw();
}
// the loop follows you: 1, 2 or 4 bars at the tempo nearest the one set
function closeFreeTake(given, endT) {
  const F = given || ss.free; if (!given) ss.free = null;
  if (!F.t0 || !F.events.length) { say("NOTHING PLAYED", "REC"); draw(); return; }
  const raw = Math.max(.5, (endT ?? audioNow()) - F.t0), cur = glo.p[G_TEMPO];
  let best = null;
  for (const bars of [1, 2, 4]) { const bpm = bars * 240 / raw; if (bpm < 50 || bpm > 200) continue; const d = Math.abs(bpm - cur); if (!best || d < best.d) best = { bars, bpm, d }; }
  if (!best) best = { bars: 2, bpm: clamp(480 / raw, 60, 180) };
  const bpm = Math.abs(best.bpm - cur) / cur < .03 ? cur : clamp(Math.round(best.bpm), 60, 180);
  glo.p[G_TEMPO] = bpm; storeGlobal();
  banks[bankIx] = blankSeq(); seq = banks[bankIx]; seq.div = 2; seq.len = best.bars * 16;
  const q = 60 / bpm / 4;
  for (const e of F.events) {
    const i = ((Math.round((e.t - F.t0) / q) % seq.len) + seq.len) % seq.len, st = seq.steps[i];
    const len = clamp(Math.round(((e.off ?? (endT ?? audioNow())) - e.t) / q * 4) / 4, .25, MAXSTEPS);
    for (const x of e.notes) if (st.notes.length < 6 && !st.notes.some((o) => o.n === x)) st.notes.push({ n: x, len, lvl: 2, vel: e.vel });
  }
  noteEdit(given ? "CAPTURE" : "FREE TAKE"); say(`${given ? "CAPTURED · " : ""}${best.bars} BAR${best.bars > 1 ? "S" : ""} · ${bpm} BPM`, "REC");
  seqStart(audioNow() + .02);
}
function clearPattern() { const L = seq.len; seq.steps = Array.from({ length: MAXSTEPS }, blankStep); seq.len = L; noteEdit("CLEAR PATTERN"); say("PATTERN CLEARED", "REC"); draw(); }

// ---------------------------------------------------------------- note repeat (ARP held + a key), on the grid; recorded as ratchets
function repeatTick() {
  const R = ss.repeat; if (!R || !audio.on) return;
  const now = audioNow(), per = s16() * REP_16THS[ss.repRate];
  if (!ss.repNext || ss.repNext < now - .05) {
    const base = ss.run ? ss.curGridT : now;
    ss.repNext = base + Math.ceil((now + .01 - base) / per) * per;
  }
  while (ss.repNext < now + .1) {
    const t = ss.repNext;
    for (const x of R.notes) for (const v of expandUni([x])) voiceOn(v, R.vel, t, t + per * .5);
    noteOut(R.notes, R.vel, t, t + per * .5);
    if (ss.rec && ss.run) {
      const pos = ss.cur + (t - ss.curGridT) / ss.dur, raw = Math.round(pos - .49), i = ((raw % seq.len) + seq.len) % seq.len, st = seq.steps[i];
      st.notes = R.notes.map((x) => ({ n: x, len: .5, lvl: 2, vel: R.vel }));
      st.ratchet = clamp(Math.round(ss.dur / per), 1, 4);
      if (raw > ss.cur) st.skipAt = ss.curGridT + (raw - ss.cur) * ss.dur;
    }
    setTimeout(() => { R.notes.forEach((x) => onNote(x, R.vel)); seqLight(R.notes, per); }, Math.max(0, (t - now) * 1000));
    ss.repNext += per;
  }
}
setInterval(repeatTick, 20);
// EDIT held + a key while playing: that note leaves each step the playhead passes
function eraseTick(i) {
  if (!ss.erase) return;
  const st = seq.steps[i], before = st.notes.length;
  st.notes = st.notes.filter((o) => !ss.erase.includes(o.n));
  if (st.notes.length !== before) { ss.flash[i] = animT; say("ERASED", "SEQ"); }
}

// ---------------------------------------------------------------- layers: hold a button, the white keys and knobs change job
const LAYER_COL = { SEQ: () => KC.seq, EDIT: () => KC.src, ARP: () => KC.arp, GLO: () => KC.glo, SEL: () => KC.chd };
function layerEnter(name) { ss.layer = name; ss.latched = false; ss.layerT = performance.now(); ss.heldSteps.clear(); draw(); }
function layerExit() { ss.layer = null; ss.latched = false; ss.heldSteps.clear(); ss.repeat = null; ss.erase = null; ss.fillHeld = false; draw(); }
const layerShown = () => ss.layer && (ss.latched || performance.now() - ss.layerT > 140);
const whiteIx = (k) => WHITE.indexOf(k), blackIx = (k) => BLACK.indexOf(k);
function previewNotes(notes, vel = .75) {
  const e = notesOn(notes, vel, "preview");
  setTimeout(() => notesOff(e), 160);
}
function layerKey(k, down, vel = .75) {
  const wi = whiteIx(k), bi = blackIx(k);
  if (ss.layer === "SEQ") {
    if (bi >= 0 && bi < 4 && down) { if (bi * 16 < seq.len) { ss.seqPage = bi; say(`STEPS ${bi * 16 + 1}–${bi * 16 + 16}`, "SEQ"); } return; }
    if (wi < 0) return;
    const step = ss.seqPage * 16 + wi;
    if (down) { ss.heldSteps.add(step); ss.stepEdited = false; ss.cursor = step; if (seq.steps[step].notes.length) previewNotes(seq.steps[step].notes.map((o) => o.n)); return; }
    if (!ss.heldSteps.has(step)) return;
    ss.heldSteps.delete(step);
    if (ss.stepEdited) return;
    const st = seq.steps[step];
    if (st.notes.length) { Object.assign(st, blankStep()); say(`STEP ${step + 1} CLEARED`, "SEQ"); }
    else { if (step >= seq.len) seq.len = Math.min(MAXSTEPS, step + 1); st.notes = ss.setNotes.map((x) => ({ n: x, len: seq.gate, lvl: 2, vel: .75 })); previewNotes(ss.setNotes); }
    noteEdit(`STEP ${step + 1}`);
  } else if (ss.layer === "EDIT") {
    if (wi < 0 && bi < 0) return;
    if (!down) { ss.erase = null; return; }
    const pitches = chordNotes(keyToNote(k));
    if (ss.run) ss.erase = pitches;
    else { let n2 = 0; for (const st of seq.steps) { const b2 = st.notes.length; st.notes = st.notes.filter((o) => !pitches.includes(o.n)); n2 += b2 - st.notes.length; } say(n2 ? `ERASED ${n2}` : "NOT IN THE PATTERN", "SEQ"); noteEdit("ERASE"); }
  } else if (ss.layer === "ARP") {
    if (down) { ss.repeat = { notes: chordNotes(keyToNote(k)), vel, k }; ss.repNext = 0; }
    else if (ss.repeat && ss.repeat.k === k) ss.repeat = null;
  } else if (ss.layer === "GLO") {
    if (wi === 8) { ss.fillHeld = down; say(down ? "FILL" : "FILL OFF", "SEQ"); }
    else if (wi === 9 && down) { ss.fillPending = !ss.fillPending; say(ss.fillPending ? "FILL NEXT BAR" : "FILL BAR CANCELLED", "SEQ"); }
    else if (wi === 15 && down) {
      const t = performance.now(); ss.taps = ss.taps.filter((x) => t - x < 2000); ss.taps.push(t);
      if (ss.taps.length >= 2) { const iv = (ss.taps[ss.taps.length - 1] - ss.taps[0]) / (ss.taps.length - 1); glo.p[G_TEMPO] = clamp(Math.round(60000 / iv), 60, 180); storeGlobal(); say(`TAP ${glo.p[G_TEMPO]} BPM`, "GLOBAL"); }
      else say("TAP", "GLOBAL");
    }
  } else if (ss.layer === "SEL") {
    if (!down) return;
    chd.p[C_KEY] = ((keyToNote(k) % 12) + 12) % 12; storeChord(); say(`KEY ${KEYS12[chd.p[C_KEY]]}`, "CHORD");
  }
  draw();
}
// every parameter a lock can sit on, for ALGORITHM with a step held
function lockableRefs() {
  const out = [];
  for (const s of SLOTS) { const m = MODULES[rack[s.id].mod]; if (m) m.pages.flat().forEach((p, idx) => out.push({ w: "slot", slot: s.id, mod: rack[s.id].mod, idx })); }
  AMP.pages.slice(0, 3).flat().forEach((p, idx) => out.push({ w: "AMP", idx }));
  LFOS.pages.flat().forEach((p, idx) => out.push({ w: "LFOS", idx }));
  for (let i = 0; i < 4; i++) out.push({ w: "macro", i });
  return out;
}
// the lock parameter: the last knob you turned on a page (before that, the filter's cutoff or the source's third control)
function currentLockRef() {
  const all = lockableRefs();
  if (ss.lockRef && all.some((r) => lockKey(r) === lockKey(ss.lockRef))) return ss.lockRef;
  if (rack.SHP.mod === "FILTER") return { w: "slot", slot: "SHP", mod: "FILTER", idx: 1 };
  return all.find((r) => r.w === "slot" && r.slot === "SRC" && r.idx === 2) || all[0] || { w: "macro", i: 0 };
}
function layerEncoder(role, k) {
  const d = Math.sign(k), L = ss.layer;
  if (role === "select") { glo.p[G_TEMPO] = clamp(glo.p[G_TEMPO] + d, 60, 180); storeGlobal(); say(`${glo.p[G_TEMPO]} BPM`, "GLOBAL"); return; }
  if (L === "SEQ") {
    const steps = [...ss.heldSteps];
    if (steps.length) {
      ss.stepEdited = true;
      for (const i of steps) {
        const st = seq.steps[i];
        if (role === 0) st.notes.forEach((o) => { o.n = clamp(o.n + d, 12, 120); });
        if (role === 1) st.notes.forEach((o) => { o.lvl = clamp(o.lvl + d, 0, 4); });
        if (role === 2) st.ratchet = clamp((st.ratchet || 1) + d, 1, 4);
        if (role === 3) st.nudge = clamp((st.nudge || 0) + d, -32, 31);
        if (role === "presets") {
          const ref = currentLockRef(), key = lockKey(ref), p = refParam(ref);
          const cur = st.locks[key] ? st.locks[key].v : refBase(ref);
          const v = ref.w === "macro" || p.t === "num" ? clamp(cur + k * .01, 0, 1) : stepParam(p, cur, d);
          st.locks[key] = { ref, v };
          say(`LOCK ${refLabel(ref)} ${fmt(p, v)}`, "LOCK");
        }
      }
      if (role === "algo") { const all = lockableRefs(), j = all.findIndex((r) => lockKey(r) === lockKey(currentLockRef())); ss.lockRef = all[(j + d + all.length) % all.length]; say(`LOCK ON ${refLabel(ss.lockRef)}`, "LOCK"); }
      if (role === 0) previewNotes(seq.steps[steps[0]].notes.map((o) => o.n));
      noteEdit("STEP EDIT"); return;
    }
    if (role === 0) { ss.setNotes = ss.setNotes.map((x) => clamp(x + d, 12, 120)); previewNotes(ss.setNotes); }
    if (role === 1) seq.div = clamp(seq.div + d, 0, SEQ_DIVS.length - 1);
    if (role === 2) seq.swing = clamp(seq.swing + k * .01, 0, 1);
    if (role === 3) seq.len = clamp(seq.len + d, 1, MAXSTEPS);
    return;
  }
  if (L === "EDIT") {
    const len = seq.len;
    if (role === 0) { const part = seq.steps.slice(0, len), r = d > 0 ? 1 : len - 1; part.forEach((st, i) => { seq.steps[(i + r) % len] = st; }); say(d > 0 ? "SHIFT LATER" : "SHIFT EARLIER", "SEQ"); }
    if (role === 1) {
      if (d > 0) { if (len * 2 > MAXSTEPS) { say("64 STEPS AT MOST", "SEQ"); return; } for (let i = 0; i < len; i++) seq.steps[len + i] = JSON.parse(JSON.stringify(seq.steps[i])); seq.len = len * 2; say(`LENGTH ×2 · ${seq.len}`, "SEQ"); }
      else { seq.len = Math.max(1, Math.floor(len / 2)); say(`LENGTH ½ · ${seq.len}`, "SEQ"); }
    }
    if (role === 2) { for (const st of seq.steps) st.notes.forEach((o) => { o.n = clamp(o.n + d, 12, 120); }); say(`TRANSPOSE ${d > 0 ? "+1" : "−1"}`, "SEQ"); }
    noteEdit("EDIT"); return;
  }
  if (L === "ARP" && role === 0) { ss.repRate = clamp(ss.repRate + d, 0, REP_RATES.length - 1); say(`REPEAT ${REP_RATES[ss.repRate]}`, "ARP"); return; }
  if (L === "GLO" && role === 0) { setRef({ w: "AMP", idx: 7 }, clamp(amp.p[7] + k * .01, 0, 1)); return; }
  if (L === "SEL") {
    if (role === 0) chd.p[C_TYPE] = clamp(chd.p[C_TYPE] + d, 0, CHORD_TYPES.length - 1);
    if (role === 1) chd.p[C_SCALE] = clamp(chd.p[C_SCALE] + d, 0, Object.keys(SCALES).length - 1);
    if (role === 2) chd.p[C_QNT] = clamp(chd.p[C_QNT] + d, 0, QNT_MODES.length - 1);
    if (role === 3) chd.p[C_VOICE] = clamp(chd.p[C_VOICE] + d, 0, VOICINGS.length - 1);
    storeChord();
  }
}
// OCT- / OCT+ inside a layer; true when the layer used it
function layerOct(dir) {
  if (ss.layer === "SEQ") {
    const steps = [...ss.heldSteps];
    if (steps.length) {
      ss.stepEdited = true;
      for (const i of steps) { const st = seq.steps[i]; if (dir > 0) st.fill = (st.fill + 1) % 3; else { st.nudge = 0; st.locks = {}; st.fill = 0; } }
      say(dir > 0 ? `FILL: ${FILLS[seq.steps[steps[0]].fill]}` : "NUDGE, LOCKS, FILL CLEARED", "SEQ"); noteEdit("STEP EDIT"); return true;
    }
    const pages = Math.ceil(seq.len / 16); ss.seqPage = clamp(ss.seqPage + dir, 0, pages - 1); say(`STEPS ${ss.seqPage * 16 + 1}–${ss.seqPage * 16 + 16}`, "SEQ"); return true;
  }
  if (ss.layer === "EDIT") { histGo(dir < 0 ? -1 : 1); return true; }
  return false;
}

// ---------------------------------------------------------------- the NOTES page: a piano roll
// positions KNOB 1 walks: each note of each step (a chord's one by one), empty steps, never the inside of a long note
function notePositions() {
  const out = []; let until = -1;
  for (let i = 0; i < seq.len; i++) {
    const st = seq.steps[i];
    if (st.notes.length) { st.notes.forEach((_, j) => out.push([i, j])); until = Math.max(until, i + Math.max(...st.notes.map((o) => o.len)) - 1e-6); }
    else if (i > until) out.push([i, -1]);
  }
  return out;
}
const selNote = () => { const st = seq.steps[ss.cursor]; return st && ss.noteIx >= 0 ? st.notes[ss.noteIx] : null; };
function deleteSelNote() {
  const st = seq.steps[ss.cursor], o = selNote(); if (!o) { say("NO NOTE HERE", "SEQ"); return; }
  st.notes.splice(ss.noteIx, 1); ss.noteIx = st.notes.length ? Math.min(ss.noteIx, st.notes.length - 1) : -1;
  noteEdit(`DELETE ${noteNameMidi(o.n)}`); say(`${noteNameMidi(o.n)} DELETED · EDIT+OCT− UNDOES`, "SEQ");
}
function sheetDo() {
  const row = SHEET_ROWS[ss.sheet.row], st = seq.steps[ss.cursor], o = selNote();
  if (row === "COPY") {
    if (!o) return;
    const to = (ss.cursor + Math.max(1, Math.ceil(o.len))) % seq.len, ts = seq.steps[to];
    if (ts.notes.length >= 6) { say("NO ROOM", "SEQ"); return; }
    ts.notes.push({ ...o }); ts.notes.sort((a, b) => a.n - b.n); ss.cursor = to; ss.noteIx = ts.notes.indexOf(ts.notes.find((x) => x.n === o.n));
    noteEdit("COPY"); say("COPIED", "SEQ");
  } else if (row === "DELETE") deleteSelNote();
  else say("TURN KNOB 2", "SEQ");
  void st;
}
function sheetEncoder(role, k) {
  const d = Math.sign(k), st = seq.steps[ss.cursor], o = selNote();
  if (role === 0) { ss.sheet.row = clamp(ss.sheet.row + d, 0, SHEET_ROWS.length - 1); return; }
  if (role !== 1) return;
  const row = SHEET_ROWS[ss.sheet.row];
  if (row === "SLIDE") st.slide = d > 0;
  if (row === "NUDGE") st.nudge = clamp((st.nudge || 0) + d, -32, 31);
  if (row === "FILL") st.fill = (st.fill + (d > 0 ? 1 : 2)) % 3;
  if (row === "MOVE" && o) {
    const to = ((ss.cursor + d) % seq.len + seq.len) % seq.len, ts = seq.steps[to];
    if (ts.notes.length >= 6 || ts.notes.some((x) => x.n === o.n)) { say("NO ROOM", "SEQ"); return; }
    st.notes.splice(ss.noteIx, 1); ts.notes.push(o); ts.notes.sort((a, b) => a.n - b.n);
    ss.cursor = to; ss.noteIx = ts.notes.indexOf(o);
  }
  noteEdit(`SHEET ${row}`);
}

// ---------------------------------------------------------------- the SEQ pages' knobs
const GEN_TOOLS = ["EUCLID", "RANDOM", "ACID", "ARP UP", "ARP UPDN", "BERLIN", "OCTAVES", "OFFBEAT", "ROLLING", "STABS", "PROGRESS", "GATES", "SHIFT >", "SHIFT <", "REVERSE", "DOUBLE", "HALF", "HUMANIZE", "CLEAR"];
const GEN_AB = { EUCLID: ["PULSES", "ROTATE"], RANDOM: ["DENSITY", "LEAPS"], ACID: ["DENSITY", "SLIDES"], "ARP UP": ["OCTAVES", "GATE"], "ARP UPDN": ["OCTAVES", "GATE"],
  BERLIN: ["VARIANT", "GATE"], OCTAVES: ["DENSITY", "GATE"], OFFBEAT: ["VARIANT", "GATE"], ROLLING: ["DENSITY", "GATE"], STABS: ["DENSITY", "7THS"],
  PROGRESS: ["VARIANT", "7THS"], GATES: ["DENSITY", "VARIANT"], HUMANIZE: ["VELO", "NUDGE"] };
function mulberry(a) { return () => { a |= 0; a = (a + 0x6D2B79F5) | 0; let t = Math.imul(a ^ (a >>> 15), 1 | a); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }; }
function scaleNote(R, k) { const deg = scaleDegs(), N = deg.length; return R + 12 * Math.floor(k / N) + deg[((k % N) + N) % N]; }
function genApply() {
  const L = seq.len, G = ss.gen, tool = GEN_TOOLS[G.tool], A = G.a, B = G.b, rng = mulberry(G.seed * 7919 + G.tool * 101);
  const key = chd.p[C_KEY], N = scaleDegs().length, bass = 36 + key, mid = 48 + key, gate = [.125, .25, .5, .75, .97][Math.round(B * 4)];
  const set = (i, notes, vel = .75, len = .5, extra = {}) => { const st = seq.steps[i % MAXSTEPS]; Object.assign(st, blankStep(), extra); st.notes = notes.map((x) => ({ n: clamp(Math.round(x), 12, 120), len, lvl: vel >= .95 ? 4 : 2, vel })); };
  const clearAll = () => { for (let i = 0; i < MAXSTEPS; i++) Object.assign(seq.steps[i], blankStep()); };
  const chordAt = (R, k, sev) => (sev ? [0, 2, 4, 6] : [0, 2, 4]).map((x) => scaleNote(R, k + x));
  switch (tool) {
    case "EUCLID": {
      const k = 1 + Math.round(A * (L - 1)), rot = Math.round(B * (L - 1));
      const old = seq.steps.map((x) => (x.notes.length ? x.notes.map((o) => ({ ...o })) : null)), fb = old.find(Boolean) || [{ n: mid, len: .5, lvl: 2, vel: .75 }];
      for (let i = 0; i < L; i++) { const j = (i + rot) % L, hit = (i * k) % L < k; seq.steps[j].notes = hit ? (old[j] || fb.map((o) => ({ ...o }))) : []; }
      break;
    }
    case "RANDOM": { clearAll(); let dd = 0; for (let i = 0; i < L; i++) if (rng() < .25 + A * .7) { dd = clamp(dd + Math.round((rng() * 2 - 1) * (1 + B * 5)), -N, 2 * N); set(i, [scaleNote(mid, dd)], .55 + rng() * .45, rng() < .12 ? 2 : rng() < .5 ? .5 : .25); } break; }
    case "ACID": { clearAll(); const degs = [0, 0, 0, 0, 2, 4, N, -1, 1]; for (let i = 0; i < L; i++) if (rng() < .45 + A * .5) { const sl = rng() < B * .6; set(i, [scaleNote(bass, degs[Math.floor(rng() * degs.length)]) + (rng() < .2 ? 12 : 0)], rng() < .3 ? 1 : .65, sl ? 1 : rng() < .5 ? .25 : .5, { slide: sl }); } break; }
    case "ARP UP": case "ARP UPDN": {
      clearAll(); const oct = 1 + Math.round(A * 2), up = []; for (let o = 0; o < oct; o++) for (const x of [0, 2, 4]) up.push(scaleNote(mid, x + o * N));
      const arr = tool === "ARP UP" ? up : up.concat(up.slice(1, -1).reverse());
      for (let i = 0; i < L; i++) set(i, [arr[i % arr.length]], i % 4 ? .65 : .85, gate); break;
    }
    case "BERLIN": {
      clearAll(); const V = [[0, 4, 7, 4, 0, 4, 9, 4, 0, 4, 7, 4, 2, 4, 9, 4], [0, 7, 2, 7, 4, 7, 2, 7, 0, 7, 2, 7, 5, 7, 4, 7], [0, 2, 4, 7, 9, 7, 4, 2, 0, 2, 4, 7, 11, 7, 4, 2]][Math.min(2, Math.floor(A * 3))];
      for (let i = 0; i < L; i++) set(i, [scaleNote(bass + 12, V[i % 16])], i % 2 ? .6 : .8, gate); break;
    }
    case "OCTAVES": { clearAll(); for (let i = 0; i < L; i++) if (rng() < .6 + A * .4) set(i, [bass + (i % 2 ? 12 : 0)], i % 2 ? .6 : .9, gate); break; }
    case "OFFBEAT": { clearAll(); for (let i = 2; i < L; i += 4) set(i, A < .5 ? [scaleNote(mid, 0)] : chordAt(mid, 0, false), .8, gate); break; }
    case "ROLLING": { clearAll(); for (let i = 0; i < L; i++) if (i % 4 && rng() < .6 + A * .4) set(i, [bass], i % 4 === 1 ? .9 : .6, gate); break; }
    case "STABS": { clearAll(); [0, 3, 6, 10, 12, 14].forEach((h, n2) => { if (h < L && (n2 < 4 || rng() < A)) set(h, chordAt(mid, n2 % 2 ? 4 : 0, B > .5), .8, .25); }); break; }
    case "PROGRESS": {
      clearAll(); const P = [[0, 5, 3, 4], [0, 3, 4, 0], [5, 3, 0, 4], [0, 4, 5, 3]][Math.min(3, Math.floor(A * 4))], span = Math.max(1, Math.floor(L / 4));
      P.forEach((dg, n2) => { if (n2 * span < L) set(n2 * span, chordAt(mid, dg, B > .5), .7, span); }); break;
    }
    case "GATES": { clearAll(); for (let i = 0; i < L; i++) if (rng() < .4 + A * .6) set(i, [mid], .5 + rng() * .5, [.125, .25, .5, .75][Math.floor(rng() * (2 + B * 2))]); break; }
    case "SHIFT >": case "SHIFT <": { const part = seq.steps.slice(0, L), r = tool === "SHIFT >" ? 1 : L - 1; part.forEach((st, i) => { seq.steps[(i + r) % L] = st; }); break; }
    case "REVERSE": seq.steps.splice(0, L, ...seq.steps.slice(0, L).reverse()); break;
    case "DOUBLE": if (L * 2 > MAXSTEPS) { say("64 STEPS AT MOST", "SEQ"); return; } for (let i = 0; i < L; i++) seq.steps[L + i] = JSON.parse(JSON.stringify(seq.steps[i])); seq.len = L * 2; break;
    case "HALF": seq.len = Math.max(1, Math.floor(L / 2)); break;
    case "HUMANIZE": for (const st of seq.steps) if (st.notes.length) { st.notes.forEach((o) => { o.vel = clamp(o.vel + (rng() - .5) * A * .5, .1, 1); }); st.nudge = Math.round((rng() - .5) * B * 24); } break;
    case "CLEAR": clearAll(); break;
  }
  noteEdit(`GEN ${tool}`); say(`${tool} · ALGO LEFT TO UNDO`, "SEQ");
}
function seqEncoder(role, k) {
  const d = Math.sign(k);
  if (ss.sheet) { sheetEncoder(role, k); return; }
  if (role === "select" && page === "SONG") { ss.songCur = clamp(ss.songCur + d, 0, 15); return; }
  if (role === "select" && page === "BANKS") { selectBank(clamp((ss.queue ?? bankIx) + d, 0, 7)); return; }
  if (role === "select") { ss.cursor = clamp(ss.cursor + d, 0, seq.len - 1); ss.noteIx = seq.steps[ss.cursor].notes.length ? 0 : -1; return; }
  if (page === "NOTES") {
    if (role === "presets") { const Z = [8, 16, 32, 64]; ss.zoom = Z[clamp(Z.indexOf(ss.zoom) + d, 0, 3)]; say(`ZOOM ${ss.zoom} STEPS`, "SEQ"); return; }
    if (role === "algo") { ss.cursor = clamp(ss.cursor + d * 4, 0, seq.len - 1); ss.noteIx = seq.steps[ss.cursor].notes.length ? 0 : -1; return; }
    if (role === 0) {
      const P = notePositions(); let j = P.findIndex(([i, n]) => i === ss.cursor && n === ss.noteIx);
      if (j < 0) j = P.findIndex(([i]) => i >= ss.cursor) - (d > 0 ? 1 : 0);
      const q = P[clamp(j + d, 0, P.length - 1)]; if (q) { ss.cursor = q[0]; ss.noteIx = q[1]; }
      const o = selNote(); if (o) previewNotes([o.n], LEVEL_VEL[o.lvl] ?? o.vel);
      return;
    }
    const o = selNote(); if (!o) { say("PLAY A NOTE TO WRITE THIS STEP", "SEQ"); return; }
    if (role === 1) { o.n = clamp(o.n + d, 12, 120); seq.steps[ss.cursor].notes.sort((a, b) => a.n - b.n); ss.noteIx = seq.steps[ss.cursor].notes.indexOf(o); previewNotes([o.n]); }
    if (role === 2) o.len = clamp(Math.round((o.len + k * .25) * 4) / 4, .25, MAXSTEPS);
    if (role === 3) o.lvl = clamp(o.lvl + d, 0, 4);
    noteEdit(`NOTE ${["", "PITCH", "LENGTH", "FORCE"][role]}`); return;
  }
  if (page === "BANKS") {
    if (role === "algo") { if (d > 0) bankAction(); else histGo(-1); return; }
    if (role === 0) selectBank(clamp((ss.queue ?? bankIx) + d, 0, 7));
    if (role === 1) ss.bankAct = clamp(ss.bankAct + d, 0, BANK_ACTS.length - 1);
    if (role === 2) ss.bankTgt = clamp(ss.bankTgt + d, 0, 7);
    if (role === 3) ss.queueNow = d > 0;
    return;
  }
  if (page === "SONG") {
    const row = song.rows[ss.songCur];
    if (role === "algo") { if (d > 0) songToggle(); else jamToSong(); return; }
    if (role === 0) ss.songCur = clamp(ss.songCur + d, 0, 15);
    if (role === 1) row.bank = clamp(row.bank + d, -1, 7);
    if (role === 2) row.rep = clamp(row.rep + d, 1, 16);
    if (role === 3) row.trn = clamp(row.trn + d, -12, 12);
    if (role !== 0) noteEdit(`SONG ROW ${ss.songCur + 1}`);
    return;
  }
  if (role === "algo") { if (page === "GEN") { if (d > 0) genApply(); else histGo(-1); } return; }
  if (role === "presets") return;
  if (page === "PATTERN") {
    if (role === 0) seq.len = clamp(seq.len + d, 1, MAXSTEPS);
    if (role === 1) seq.div = clamp(seq.div + d, 0, SEQ_DIVS.length - 1);
    if (role === 2) seq.swing = clamp(seq.swing + k * .01, 0, 1);
    if (role === 3) seq.gate = clamp(seq.gate + k * .01, .05, 1);
    ss.cursor = Math.min(ss.cursor, seq.len - 1);
  } else if (page === "SPICE") {
    const st = seq.steps[ss.cursor];
    if (role === 0) seq.spice = clamp(seq.spice + k * .01, 0, 1);
    if (role === 1) { seq.dice = clamp(seq.dice + d, 0, 99); say(`DICE ${seq.dice}: ANOTHER VARIATION`, "SEQ"); }
    if (role === 2) st.chance = clamp(st.chance + k * .01, 0, 1);
    if (role === 3) st.fill = (st.fill + (d > 0 ? 1 : 2)) % 3;
  } else if (page === "GEN") {
    const G = ss.gen;
    if (role === 0) G.tool = clamp(G.tool + d, 0, GEN_TOOLS.length - 1);
    if (role === 1) G.a = clamp(G.a + k * .01, 0, 1);
    if (role === 2) G.b = clamp(G.b + k * .01, 0, 1);
    if (role === 3) G.seed = clamp(G.seed + d, 0, 99);
  }
}
// the REC READY screen: MODE and LENGTH, before the first note
function recEncoder(role, k) {
  const d = Math.sign(k);
  if (role === 0) ss.recMode = d > 0 ? 1 : 0;
  if (role === 2) { ss.recQ = d < 0 ? false : true; say(`QNT ${ss.recQ ? "ON" : "OFF"}`, "REC"); }
  if (role === 1) { const B = [1, 2, 4]; ss.recBars = B[clamp(B.indexOf(ss.recBars) + d, 0, 2)]; if (!hasNotes()) seq.len = Math.min(MAXSTEPS, Math.round(ss.recBars * 16 / DIV_16THS[seq.div])); }
  if (ss.recMode === 1 && !hasNotes() && ss.armed) { ss.armed = false; ss.free = { t0: null, events: [] }; }
  else if (ss.recMode === 0 && ss.free && !ss.free.t0) { ss.free = null; ss.armed = true; }
}
// knob turns while recording become locks on the step that is playing
function lockTarget() { if (ss.rec && ss.run) return ((Math.round(nowPos()) % seq.len) + seq.len) % seq.len; return -1; }
function lockTurn(ref, k) {
  const i = lockTarget(), st = seq.steps[i], key = lockKey(ref), p = refParam(ref);
  const cur = st.locks[key] ? st.locks[key].v : refBase(ref);
  const v = ref.w === "macro" ? clamp(cur + k * .01, 0, 1) : stepParam(p, cur, k);
  st.locks[key] = { ref, v };
  const c = refCall(ref, v); if (c) engine(c);
  if (ss.run) ss.active.set(key, ref);
  say(`STEP ${i + 1} · ${refLabel(ref)} ${fmt(p, v)}`, "LOCK");
}
function lockView(makeRef, count, base) {
  const steps = ss.layer === "SEQ" ? [...ss.heldSteps] : [];
  const i = steps.length ? steps[0] : lockTarget(); if (i < 0) return { vals: base, marks: null };
  const st = seq.steps[i], marks = [], vals = base.map((v, j) => { const ref = j < count ? makeRef(j) : null, l = ref && st.locks[lockKey(ref)]; marks.push(!!l); return l ? l.v : v; });
  return { vals, marks };
}

// ---------------------------------------------------------------- drawing
function stepLabel(st) { const n = st.notes.map((o) => o.n); return n.length >= 3 ? chordName(n) : n.map(noteNameMidi).join(" "); }
const LVL_ALPHA = [.28, .5, .8, 1, 1];
function drawRoll(X, Y, W, H) {
  const c = KC.seq, L = seq.len, Z = Math.min(ss.zoom, Math.max(8, L));
  const start = clamp(Math.floor(ss.cursor / Z) * Z, 0, Math.max(0, L - 1)), sw = W / Z;
  const all = seq.steps.slice(0, L).flatMap((s) => s.notes.map((o) => o.n));
  let lo = all.length ? Math.min(...all) - 2 : 52, hi = all.length ? Math.max(...all) + 2 : 72; if (hi - lo < 14) { const m = (hi + lo) / 2; lo = m - 7; hi = m + 7; }
  const yOf = (n2) => Y + H - 4 - ((n2 - lo) / (hi - lo)) * (H - 8);
  rect(X, Y, W, H, C.well, 6);
  for (let j = 0; j <= Z; j++) { const i = start + j; if (i % 4 === 0) rect(X + j * sw, Y + 2, 1, H - 4, i % 16 === 0 ? "#2b2a27" : "#181816"); if (i >= L) rect(X + j * sw, Y + 1, sw, H - 2, "#060606"); }
  // notes as bars as long as they last
  for (let j = 0; j < Z; j++) {
    const i = start + j; if (i >= L) break;
    const st = seq.steps[i], fl = Math.max(0, 1 - (animT - ss.flash[i]) * 4), x0 = X + j * sw + (st.nudge || 0) / 64 * sw;
    if (Object.keys(st.locks).length) dot(X + j * sw + sw / 2, Y + 4, 1.4, C.gold, false);
    if (st.fill) T(X + j * sw + 1, Y + 9, st.fill === 1 ? "F" : "×", { size: 7, c: C.gold });
    st.notes.forEach((o, nj) => {
      const y = yOf(o.n), w = Math.max(2, o.len * sw - 1), sel = i === ss.cursor && nj === ss.noteIx;
      const a = LVL_ALPHA[o.lvl] * (.7 + .3 * fl);
      if (o.lvl === 1) { g.strokeStyle = rgba(c, .8); g.lineWidth = 1; g.strokeRect(x0 + .5, y - 1.5, w - 1, 3); } else rect(x0, y - 2, w, 4, rgba(c, a), 1);
      if (o.lvl === 4) rect(x0, y - 3, Math.min(w, 4), 2, "#fff");
      if ((st.ratchet || 1) > 1) for (let r = 1; r < st.ratchet; r++) rect(x0 + r * sw / st.ratchet, y - 2, 1, 4, C.bg);
      if (st.slide) rect(x0 + w - 1, y - 1, 4, 2, C.fg);
      if (fl > 0) rect(x0, y - 3, w, 6, rgba("#ffffff", fl * .25), 1);
      if (sel) { frame(x0 - 1.5, y - 4, w + 3, 8, "#fff", 2); T(clamp(x0, X + 2, X + W - 30), y - 6 < Y + 10 ? y + 12 : y - 6, noteNameMidi(o.n), { size: 8, c: "#fff" }); }
    });
    if (i === ss.cursor && ss.noteIx < 0 && Math.floor(animT * 3) % 2) frame(X + j * sw, Y + 1, sw, H - 2, "#bbb", 2);
  }
  if (ss.run && ss.cur >= start && ss.cur < start + Z) {
    const f = clamp((audioNow() - ss.curT) / ss.dur, 0, 1), px = X + (ss.cur - start + f) * sw;
    rect(px, Y + 1, 1, H - 2, rgba(c, .9)); dot(px, Y + H - 2, 1.6, c);
  }
  T(X + W - 3, Y + 9, `${start + 1}–${Math.min(L, start + Z)}/${L}`, { a: "right", size: 7, c: C.dim });
}
function drawOverview(X, Y, W, H) {
  const c = KC.seq, L = seq.len, sw = W / MAXSTEPS;
  rect(X, Y, W, H, C.well, 6);
  for (let i = 0; i < MAXSTEPS; i++) {
    const st = seq.steps[i], x = X + i * sw, on = st.notes.length, fl = Math.max(0, 1 - (animT - ss.flash[i]) * 4);
    rect(x + .5, Y + 4, sw - 1, H - 8, i >= L ? "#070707" : on ? rgba(c, .35 + .5 * fl + .15 * (st.ratchet > 1)) : i % 4 === 0 ? "#1b1b19" : "#121211", 1);
    if (i === ss.cursor) frame(x, Y + 2, sw, H - 4, "#fff", 1);
  }
  if (ss.run && ss.cur >= 0) dot(X + (ss.cur + .5) * sw, Y + H - 3, 1.6, c);
}
function drawSeq() {
  const c = KC.seq, pi = SEQ_PAGES.indexOf(page), st = seq.steps[ss.cursor];
  const status = ss.free ? "FREE TAKE" : ss.rec && ss.run ? "REC" : ss.armed ? "REC READY" : ss.run ? "PLAY" : "STOP";
  if (page === "NOTES") {
    const o = selNote();
    header(`NOTES ${BANK_NAMES[bankIx]}`, c, `${status} · ${ss.cursor + 1}${o ? " · " + noteNameMidi(o.n) : ""}`, "SEQ", SEQ_PAGES.length, pi);
    drawRoll(VX, VY, VW, VH);
    const nj = st.notes.length;
    cells([{ l: "NOTE", t: "list", opts: [o ? `${ss.noteIx + 1}/${nj}` : "EMPTY"] }, { l: "PITCH", t: "list", opts: [o ? noteNameMidi(o.n) : "—"] }, { l: "LENGTH", t: "list", opts: [o ? `${o.len}` : "—"] }, { l: "FORCE", t: "list", opts: LEVELS }],
      [0, 0, 0, o ? o.lvl : 2], null, c);
    if (ss.sheet) drawSheet();
  } else if (page === "PATTERN") {
    header("PATTERN", c, `${status} · ${seq.len} × ${SEQ_DIVS[seq.div]}`, "SEQ", SEQ_PAGES.length, pi);
    drawOverview(VX, VY, VW, VH);
    cells([I("LENGTH", 1, MAXSTEPS, 16), L("DIV", SEQ_DIVS), n("SWING"), n("GATE")], [seq.len, seq.div, seq.swing, seq.gate], null, c);
  } else if (page === "SPICE") {
    header("SPICE", c, `STEP ${ss.cursor + 1} · ${st.notes.length ? stepLabel(st) : "EMPTY"}`, "SEQ", SEQ_PAGES.length, pi);
    drawOverview(VX, VY, VW, VH);
    if (seq.spice) for (let i = 0; i < seq.len; i++) { const sp = spiceFor(i), x = VX + (i + .5) * (VW / MAXSTEPS); if (seq.steps[i].notes.length && sp.rest) rect(x - 1, VY + VH / 2, 2, 2, C.warn); else if (seq.steps[i].notes.length && sp.oct) T(x, VY + 12, sp.oct > 0 ? "+" : "−", { a: "center", size: 8, c: C.gold }); }
    cells([n("SPICE"), I("DICE", 0, 99, 0), n("CHANCE"), L("FILL", FILLS)], [seq.spice, seq.dice, st.chance, st.fill], null, c);
  } else if (page === "BANKS") {
    header("BANKS", c, `${status} · ${BANK_NAMES[bankIx]}${ss.queue !== null ? " → " + BANK_NAMES[ss.queue] : ""}${ss.songOn ? " · SONG" : ""}`, "SEQ", SEQ_PAGES.length, pi);
    drawBanks(VX, VY, VW, VH);
    cells([{ l: "BANK", t: "list", opts: BANK_NAMES }, { l: "ACTION", t: "list", opts: BANK_ACTS }, { l: "TARGET", t: "list", opts: BANK_NAMES }, { l: "SWITCH", t: "list", opts: ["LOOP END", "NOW"] }],
      [ss.queue ?? bankIx, ss.bankAct, ss.bankTgt, ss.queueNow ? 1 : 0], null, c);
  } else if (page === "SONG") {
    const row = song.rows[ss.songCur];
    header("SONG", c, `${ss.songOn ? `ROW ${ss.songRow + 1} · ${ss.songRep + 1}/${song.rows[ss.songRow].rep}` : "OFF · ALGO → ON"}`, "SEQ", SEQ_PAGES.length, pi);
    drawSong(VX, VY, VW, VH);
    cells([{ l: "ROW", t: "int", min: 1, max: 16 }, { l: "BANK", t: "list", opts: ["END", ...BANK_NAMES] }, { l: "REPEATS", t: "int", min: 1, max: 16 }, { l: "TRANSPOSE", t: "int", min: -12, max: 12 }],
      [ss.songCur + 1, row.bank + 1, row.rep, row.trn], null, c);
  } else {
    const G = ss.gen, tool = GEN_TOOLS[G.tool], ab = GEN_AB[tool], Ln = seq.len, eu = tool === "EUCLID";
    header("GEN", c, `${tool} · ALGO → APPLY`, "SEQ", SEQ_PAGES.length, pi);
    drawOverview(VX, VY, VW, VH);
    const pa = ab ? (eu ? { l: ab[0], t: "int", min: 1, max: Ln } : { l: ab[0], t: "num" }) : null, pb = ab ? (eu ? { l: ab[1], t: "int", min: 0, max: Math.max(1, Ln - 1) } : { l: ab[1], t: "num" }) : null;
    cells([{ l: "TOOL", t: "list", opts: GEN_TOOLS }, pa, pb, { l: "SEED", t: "int", min: 0, max: 99 }], [G.tool, eu ? 1 + Math.round(G.a * (Ln - 1)) : G.a, eu ? Math.round(G.b * (Ln - 1)) : G.b, G.seed], null, c);
  }
  if ((ss.rec && ss.run || ss.free) && Math.floor(animT * 3) % 2) dot(214, 13, 3, C.warn, false);
  footer();
}
// BANKS: the eight banks as tiles with their notes in miniature; the playing one lit, the queued one blinking
function drawBanks(X, Y, W, H) {
  const c = KC.seq, tw = W / 8;
  rect(X, Y, W, H, C.well, 6);
  for (let b = 0; b < 8; b++) {
    const x = X + b * tw + 2, bk = banks[b], cur = b === bankIx, q = b === ss.queue && Math.floor(animT * 4) % 2;
    rect(x, Y + 4, tw - 4, H - 8, cur ? "#2a2026" : "#141413", 4);
    if (cur || q) frame(x, Y + 4, tw - 4, H - 8, cur ? c : "#fff", 4);
    const all = bk.steps.slice(0, bk.len).flatMap((s, i) => s.notes.map((o) => [i, o.n]));
    if (all.length) { const lo = Math.min(...all.map((a) => a[1])), hi = Math.max(lo + 6, ...all.map((a) => a[1])); for (const [i, nn] of all) rect(x + 2 + i / bk.len * (tw - 8), Y + H - 14 - (nn - lo) / (hi - lo) * (H - 30), 2, 2, rgba(c, cur ? 1 : .55)); }
    if (cur && ss.run && ss.cur >= 0) rect(x + 2 + ss.cur / bk.len * (tw - 8), Y + 8, 1, H - 16, rgba(c, .7));
    T(x + (tw - 4) / 2, Y + H - 4, BANK_NAMES[b], { a: "center", size: 9, c: cur ? c : C.dim });
    T(x + (tw - 4) / 2, Y + 14, bankEmpty(b) ? "·" : String(bk.len), { a: "center", size: 7, c: C.dim });
  }
}
// SONG: the rows as blocks as long as their repeats; the row playing lit, the cursor framed
function drawSong(X, Y, W, H) {
  const c = KC.seq; rect(X, Y, W, H, C.well, 6);
  const rows = song.rows, used = rows.map((r) => (r.bank >= 0 ? r.rep : 1)), tot = used.reduce((a, b) => a + b, 0);
  let x = X + 3; const scale = (W - 6) / tot;
  rows.forEach((r, i) => {
    const w = used[i] * scale, on = ss.songOn && i === ss.songRow;
    rect(x + .5, Y + 14, w - 1, H - 26, r.bank < 0 ? "#0c0c0b" : on ? rgba(c, .75) : rgba(c, .25), 2);
    if (r.bank >= 0 && w > 9) T(x + w / 2, Y + 30, BANK_NAMES[r.bank], { a: "center", size: 9, c: on ? "#111" : C.fg });
    if (r.bank >= 0 && r.trn && w > 14) T(x + w / 2, Y + 42, `${r.trn > 0 ? "+" : ""}${r.trn}`, { a: "center", size: 7, c: C.gold });
    if (i === ss.songCur) frame(x, Y + 12, w, H - 22, "#fff", 2);
    if (on && ss.run) rect(x + (ss.songRep + (ss.cur >= 0 ? ss.cur / seq.len : 0)) / r.rep * w, Y + 12, 1, H - 22, "#fff");
    x += w;
  });
  T(X + 4, Y + 10, `JAM ${ss.jam.length} PASSES · ALGO ← TO SONG`, { size: 7, c: C.dim });
  if (rows.every((r) => r.bank < 0)) T(X + W / 2, Y + H / 2 + 8, "EMPTY · KNOB 2 GIVES A ROW A BANK", { a: "center", size: 8, c: C.dim });
}
function drawSheet() {
  const st = seq.steps[ss.cursor], o = selNote();
  rect(30, 40, 180, 150, "#0d0d0c", 8); frame(30, 40, 180, 150, KC.seq, 8);
  T(40, 56, o ? `NOTE ${noteNameMidi(o.n)} · STEP ${ss.cursor + 1}` : `STEP ${ss.cursor + 1}`, { size: 11, c: KC.seq });
  const vals = [st.slide ? "ON" : "OFF", String(st.nudge || 0), FILLS[st.fill], "K2 ±1", "OCT+", "OCT+"];
  SHEET_ROWS.forEach((r, i) => {
    const y = 64 + i * 18, sel = i === ss.sheet.row;
    if (sel) { rect(36, y, 168, 16, "#25241f", 4); rect(36, y, 2, 16, KC.seq); }
    T(44, y + 12, r, { size: 11, c: sel ? C.fg : C.dim }); T(196, y + 12, vals[i], { a: "right", size: 10, c: sel ? KC.seq : C.dim });
  });
  T(120, 182, "K1 ROW · K2 SET · OCT+ DO · OCT− CLOSE", { a: "center", size: 8, c: C.dim });
}
function drawRecScreen() {
  rect(20, 50, 200, 120, "#0d0d0c", 8); frame(20, 50, 200, 120, C.warn, 8);
  const free = !!ss.free;
  dot(36, 66, 4, C.warn, Math.floor(animT * 3) % 2 === 0);
  T(48, 70, free ? "FREE TAKE" : "REC READY", { size: 14, c: C.warn });
  if (free && ss.free.t0) {
    const raw = audioNow() - ss.free.t0;
    T(30, 96, `${raw.toFixed(1)} S · ${ss.free.events.length} NOTES`, { size: 12 });
    T(30, 116, 'REC ON THE "1" CLOSES THE LOOP', { size: 9, c: C.dim });
  } else {
    T(30, 96, free ? "PLAY FREELY: THE TEMPO FOLLOWS YOU" : "YOUR FIRST NOTE STARTS STEP 1", { size: 9 });
    T(30, 112, free ? "REC ON THE 1 CLOSES THE LOOP" : "PLAY ALSO STARTS IT", { size: 9, c: C.dim });
    T(30, 140, `K1 MODE ${ss.recMode ? "FREE" : "TEMPO"}${hasNotes() ? " (PATTERN HAS NOTES)" : ""}`, { size: 9, c: C.fg });
    T(30, 156, `K2 LENGTH ${ss.recBars} BAR${ss.recBars > 1 ? "S" : ""} · ${glo.p[G_TEMPO]} BPM`, { size: 9, c: C.fg });
    T(30, 166, `K3 QNT ${ss.recQ ? "ON (ON THE GRID)" : "OFF (AS PLAYED)"}`, { size: 8, c: C.dim });
  }
}
// a held layer: the 16 white keys as tiles, the knobs as dials
function layerDials() {
  const L = ss.layer, held = [...ss.heldSteps];
  if (L === "SEQ") {
    if (held.length) { const st = seq.steps[held[0]], o = st.notes[0], ref = currentLockRef(), lk = st.locks[lockKey(ref)];
      return [["NOTE", o ? noteNameMidi(o.n) : "—"], ["LEVEL", o ? LEVELS[o.lvl] : "—"], ["RATCHET", `X${st.ratchet || 1}`], ["NUDGE", String(st.nudge || 0)], [`LOCK ${refLabel(ref)}`.slice(0, 18), lk ? fmt(refParam(ref), lk.v) : "PRESETS"]]; }
    return [["NOTE", chordName(ss.setNotes)], ["DIV", SEQ_DIVS[seq.div]], ["SWING", String(Math.round(seq.swing * 100))], ["LENGTH", String(seq.len)]];
  }
  if (L === "EDIT") return [["SHIFT", "◀ ▶"], ["LENGTH", "½ ×2"], ["TRANSPOSE", "− +"], ["UNDO", "OCT− / +"]];
  if (L === "ARP") return [["RATE", REP_RATES[ss.repRate]], ["", ""], ["", ""], ["", ""]];
  if (L === "GLO") return [["LEVEL", fmt(AMP.pages[1][3], amp.p[7])], ["TEMPO", `${glo.p[G_TEMPO]}`], ["", ""], ["", ""]];
  return [["CHORD", CHORD_TYPES[chd.p[C_TYPE]]], ["SCALE", Object.keys(SCALES)[chd.p[C_SCALE]]], ["QNT", QNT_MODES[chd.p[C_QNT]]], ["VOICING", VOICINGS[chd.p[C_VOICE]]]];
}
function drawLayer() {
  const L = ss.layer, c = LAYER_COL[L]();
  rect(0, 26, 240, 188, "#050505");
  rect(0, 26, 240, 13, c); T(6, 36, `${L} ${{ SEQ: "STEPS", EDIT: "ERASE", ARP: "REPEAT", GLO: "MIX", SEL: "KEY + CHORDS" }[L]}${ss.latched ? " · LOCK" : ""}`, { size: 10, c: "#111" });
  if (L === "SEQ") T(234, 36, `${ss.seqPage * 16 + 1}–${ss.seqPage * 16 + 16}`, { a: "right", size: 9, c: "#111" });
  const deg = scaleDegs();
  for (let w = 0; w < 16; w++) {
    const x = 6 + (w % 4) * 36, y = 42 + Math.floor(w / 4) * 36, k = WHITE[w], nn = 53 + k + 12 * octave;
    let fill = "#141413", label = "", sub = "", lit = 0;
    if (L === "SEQ") {
      const i = ss.seqPage * 16 + w, st = seq.steps[i], fl = Math.max(0, 1 - (animT - ss.flash[i]) * 4);
      label = String(i + 1);
      if (i >= seq.len) fill = "#090909";
      else if (st.notes.length) { lit = .35 + .5 * LVL_ALPHA[Math.max(...st.notes.map((o) => o.lvl))]; sub = stepLabel(st).slice(0, 6); }
      if (fl) lit = Math.max(lit, fl);
      if (ss.heldSteps.has(i)) frame(x - 1, y - 1, 34, 34, "#fff", 5);
      if (Object.keys(st.locks).length || st.nudge) dot(x + 28, y + 4, 1.6, C.gold, false);
      if (st.fill) T(x + 3, y + 10, st.fill === 1 ? "F" : "×", { size: 8, c: C.gold });
      if ((st.ratchet || 1) > 1) for (let r = 0; r < st.ratchet; r++) rect(x + 3 + r * 4, y + 28, 2, 2, C.fg);
      if (i === ss.cur && ss.run) frame(x - 2, y - 2, 36, 36, c, 6);
    } else if (L === "GLO") {
      label = { 8: "FILL", 9: "BAR", 15: "TAP" }[w] || ""; lit = (w === 8 && ss.fillHeld) || (w === 9 && (ss.fillPending || Math.floor(ss.pos16 / 16) === ss.fillBar)) ? 1 : label ? .15 : 0;
    } else {
      label = noteNameMidi(nn);
      if (L === "SEL") lit = (((nn - chd.p[C_KEY]) % 12) + 12) % 12 === 0 ? 1 : deg.includes((((nn - chd.p[C_KEY]) % 12) + 12) % 12) ? .3 : 0;
      if (L === "EDIT") lit = seq.steps.some((s) => s.notes.some((o) => o.n === nn)) ? .45 : 0;
      if (L === "ARP") lit = ss.repeat && ss.repeat.k === k ? .5 + .5 * Math.abs(Math.sin(animT * 12)) : 0;
    }
    if (lit) fill = rgba(c, lit);
    rect(x, y, 32, 32, fill, 5);
    T(x + 16, y + 19, label, { a: "center", size: label.length > 3 ? 8 : 10, c: lit > .5 ? "#111" : C.fg });
    if (sub) T(x + 16, y + 28, sub, { a: "center", size: 6, c: lit > .5 ? "#111" : C.dim });
    if (w % 4 === 0) rect(x + 1, y + 1, 3, 3, C.dim, 1);       // the first key of each row
  }
  layerDials().forEach(([lab, val], i) => {
    const y = 42 + i * 36; if (!lab && !val) return;
    rect(152, y, 82, 32, "#121211", 5);
    T(156, y + 11, lab, { size: 7, c: C.dim }); T(156, y + 26, String(val).slice(0, 11), { size: 11 });
  });
  T(6, 196, { SEQ: "TAP A STEP · HOLD + KNOBS · OCT PAGE", EDIT: "A KEY ERASES THAT NOTE · OCT UNDO/REDO", ARP: "HOLD A KEY: IT REPEATS ON THE GRID", GLO: "KEY 9 FILL · 10 FILL BAR · 16 TAP TEMPO", SEL: "A KEY SETS THE KEY OF THE SONG" }[L], { size: 8, c: C.dim });
  T(6, 208, ss.latched ? "ANY BUTTON LEAVES" : "HOLD + HOME LOCKS THE LAYER", { size: 8, c: C.faint === "#2b2a27" ? "#55534c" : C.dim });
}
