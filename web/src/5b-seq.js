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
function notesOn(notes, vel, src = "key") {
  const strum = chd.p[C_STRUM] * .08, now = audioNow(), local = !(src === "key" && glo.p[G_LOCAL] === 1);
  if (notes.length > 1 || CHORD_TYPES[chd.p[C_TYPE]] !== "OFF") lastChord = { notes, t: animT };
  if (src !== "midi") notes.forEach((nt, i) => noteOut([nt], vel, strum ? now + i * strum : undefined, null));
  const voices = [], at = [];
  notes.forEach((nt, i) => {
    const t = now + i * strum;
    for (const x of expandUni([nt])) { voices.push(x); at.push(t); if (local) { if (!strum || !audio.ctx) engine(["note_on", x, vel]); else engineAt(t, [["note_on", x, vel]]); } }
    if (i && strum) setTimeout(() => onNote(nt, vel), i * strum * 1000); else onNote(nt, vel);
  });
  return { notes, voices, at, out: src !== "midi" };
}
// note-offs never overtake a strummed note-on that is still waiting
function notesOff(e) {
  const now = audioNow();
  (e.voices || e.notes).forEach((nt, i) => { if (e.at[i] > now) engineAt(e.at[i] + .003, [["note_off", nt]]); else engine(["note_off", nt]); });
  if (e.out) noteOut(e.notes, 0, null, Math.max(now, ...e.at, 0) + .004);
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
function lockRefFor(slot, i) {
  if (page === "PERFORM") return { w: "macro", i };
  if (page === "AMP" || page === "LFOS") { const def = page === "AMP" ? AMP : LFOS, p = def.pages[subpage][i]; return p ? { w: page, idx: def.pages.slice(0, subpage).flat().length + i } : null; }
  if (slot) { const st = rack[slot.id], m = MODULES[st.mod]; if (!m || !m.pages[subpage][i]) return null; return { w: "slot", slot: slot.id, mod: st.mod, idx: m.pages.slice(0, subpage).flat().length + i }; }
  return null;
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

