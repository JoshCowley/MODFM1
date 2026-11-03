// ================================================================ 5c. after Clipper: undo / compare, morph, randomizer, library,
// arpeggiator with a Turing machine, modulation sequencer lanes, voice modes, tuning, MIDI out, CPU readout

// ---------------------------------------------------------------- undo and compare: every edit becomes a step once it rests
const hist = { list: [], i: -1, orig: null, timer: 0, pending: "", applying: false, cmp: 0, cmpEdit: null, edited: false };
const bodyOf = (s) => JSON.stringify({ ...s, name: 0, label: 0 });
function snapNow(label) { return { ...snapshot(patchName), label }; }
function histReset(label) { clearTimeout(hist.timer); const s = snapNow(label); hist.list = [s]; hist.i = 0; hist.orig = s; hist.cmp = 0; hist.cmpEdit = null; hist.edited = false; }
function noteEdit(label) {
  if (hist.applying) return;
  if (hist.cmp) { hist.cmp = 0; hist.cmpEdit = null; }
  hist.pending = label; clearTimeout(hist.timer); hist.timer = setTimeout(histCommit, 650);
}
function histCommit() {
  const s = snapNow(hist.pending);
  if (hist.list[hist.i] && bodyOf(hist.list[hist.i]) === bodyOf(s)) return;
  hist.list = hist.list.slice(0, hist.i + 1); hist.list.push(s);
  if (hist.list.length > 17) hist.list.shift();
  hist.i = hist.list.length - 1;
  hist.edited = !!hist.orig && bodyOf(s) !== bodyOf(hist.orig); dirty = true;
}
function applySnap(s) { hist.applying = true; restoreSnap(s); hist.applying = false; draw(); }
function histGo(k) {
  clearTimeout(hist.timer); if (hist.pending && hist.i === hist.list.length - 1) { histCommit(); hist.pending = ""; }
  const j = clamp(hist.i + k, 0, hist.list.length - 1);
  if (j === hist.i) { say(k < 0 ? "NOTHING TO UNDO" : "NOTHING TO REDO", "UNDO"); return; }
  hist.i = j; applySnap(hist.list[j]);
  hist.edited = bodyOf(hist.list[j]) !== bodyOf(hist.orig);
  say(`${j - (hist.list.length - 1) || "NOW"} ${hist.list[j].label}`.trim(), "UNDO");
}
function setCompare(on) {
  if (on === hist.cmp) return;
  if (on) { hist.cmpEdit = snapNow("EDIT"); applySnap(hist.orig); }
  else if (hist.cmpEdit) applySnap(hist.cmpEdit);
  hist.cmp = on; say(on ? "COMPARE: AS LOADED" : "COMPARE: YOUR EDITS", "UNDO");
}

// ---------------------------------------------------------------- morph between two versions of the patch
const morph = { a: null, b: null, x: 0, via: 0, lastVia: -1 };
const lerp = (a, b, x) => a + (b - a) * x;
function lerpParams(def, pa, pb, x) {
  const flat = def ? def.pages.flat() : [];
  return pa.map((v, i) => { const p = flat[i]; if (!p || pb[i] === undefined) return v; return p.t === "list" || p.t === "int" ? (x < .5 ? v : pb[i]) : lerp(v, pb[i], x); });
}
function morphTo(x) {
  const A = morph.a, B = morph.b; if (!A || !B) return;
  morph.x = x; hist.applying = true;
  for (const s of SLOTS) {
    const a = A.slots[s.id], b = B.slots[s.id], use = x < .5 ? a : b;
    if (a.mod === b.mod) { if (rack[s.id].mod !== a.mod) rack[s.id] = { mod: a.mod, p: [...a.p] }; rack[s.id].p = lerpParams(MODULES[a.mod], a.p, b.p, x); }
    else if (rack[s.id].mod !== use.mod) { const f = fitOf(s.id, use.mod); if (f.ok) rack[s.id] = { mod: use.mod, p: [...use.p] }; }
  }
  amp.p = lerpParams(AMP, A.amp, B.amp, x); lfo.p = lerpParams(LFOS, A.lfo, B.lfo, x);
  matrix = A.matrix.map((r, i) => { const q = B.matrix[i]; return r.src === q.src && r.dst === q.dst && r.via === q.via ? { ...r, amt: lerp(r.amt, q.amt, x) } : { ...(x < .5 ? r : q) }; });
  if (!morph.via) macros = A.macros.map((v, i) => lerp(v, B.macros[i], x));
  hist.applying = false; noteEdit("MORPH"); draw();
}

// ---------------------------------------------------------------- randomizer by section
const RAND_WHAT = ["SRC", "SHP", "AMP", "MOD", "FX", "MATRIX", "ALL"];
const rnd = { what: 0, amt: .35, t: -9, changed: [] };
function randomize() {
  const what = RAND_WHAT[rnd.what], amt = rnd.amt, keep = /LEVEL|FREEZE|MODE$|VCA|INT EXC|QUALITY|OCTAVE|TUNE|SYNC|1SHOT|NOTE TRG|PITCH TRK|GAIN|ENV SRC/;
  const jiggle = (def, arr) => def.pages.flat().forEach((p, i) => {
    if (keep.test(p.l) && !(def === AMP && p.l === "MODE" && false)) return;
    if (p.t === "list") { if (Math.random() < amt * .6) arr[i] = Math.floor(Math.random() * p.opts.length); }
    else if (p.t === "int") { if (Math.random() < amt * .5) arr[i] = Math.round(clamp(arr[i] + (Math.random() - .5) * amt * (p.max - p.min), p.min, p.max)); }
    else if (p.t === "bip") arr[i] = clamp(arr[i] + (Math.random() * 2 - 1) * amt, -1, 1);
    else arr[i] = clamp(arr[i] + (Math.random() * 2 - 1) * amt, 0, 1);
  });
  const slotIds = { SRC: ["SRC"], SHP: ["SHP"], MOD: ["MODA", "MODB"], FX: ["FX1", "FX2"], ALL: ["SRC", "SHP", "MODA", "MODB", "FX1", "FX2"] }[what] || [];
  for (const id of slotIds) { const m = MODULES[rack[id].mod]; if (m) jiggle(m, rack[id].p); }
  if (what === "AMP" || what === "ALL") jiggle({ pages: AMP.pages.slice(0, 3) }, amp.p);
  if (what === "MOD" || what === "ALL") jiggle(LFOS, lfo.p);
  if (what === "MATRIX" || what === "ALL") for (const r of matrix) if (r.src !== "—" && r.dst !== "—") r.amt = clamp(Math.round((r.amt + (Math.random() * 2 - 1) * amt) * 100) / 100, -1, 1);
  rnd.t = animT; noteEdit(`RANDOM ${what}`); say(`RANDOM ${what} ${Math.round(amt * 100)}`, "RANDOM"); draw();
}

// ---------------------------------------------------------------- library: favourites and a filter for PRESETS
const LIB_FILTERS = ["ALL", "FAVS", "USER", "BASS", "PADS", "LEAD", "CHORD", "FX", "PLAITS", "BRAIDS", "ELEMENTS"];
const LIB_CATS = { BASS: "BS ", PADS: "PD ", LEAD: "LD ", CHORD: "CH ", FX: "FX " };   // the categories are the name prefixes
const lib = { filter: 0, favs: new Set() };
try { const v = JSON.parse(localStorage.getItem("fm1rack.lib.v1") || "null"); if (v) { lib.filter = v.filter || 0; lib.favs = new Set(v.favs || []); } } catch { /* defaults */ }
function storeLib() { try { localStorage.setItem("fm1rack.lib.v1", JSON.stringify({ filter: lib.filter, favs: [...lib.favs] })); } catch { /* this visit only */ } }
const entryKey = (e) => (e.u !== undefined ? `U${e.u}` : `F:${PRESETS[e.f].name}`);
function entrySrc(e) { return e.u !== undefined ? userPatches[e.u].slots.SRC.mod : PRESETS[e.f].slots.SRC; }
function filterList(list) {
  const f = LIB_FILTERS[lib.filter];
  const out = f === "ALL" ? list : f === "FAVS" ? list.filter((e) => lib.favs.has(entryKey(e))) : f === "USER" ? list.filter((e) => e.u !== undefined) : LIB_CATS[f] ? list.filter((e) => CAT_NAMES[entryCat(e)] === f) : list.filter((e) => entrySrc(e) === f);
  // favourites first
  return [...out.filter((e) => lib.favs.has(entryKey(e))), ...out.filter((e) => !lib.favs.has(entryKey(e)))];
}
function currentEntry() { return patchList(true).find((e) => e.name === patchName) || patchList(true)[0]; }

// ---------------------------------------------------------------- TOOLS page (HOME pressed three times)
const TOOL_PAGES = ["UNDO", "MORPH", "RANDOM", "LIBRARY"];
function toolParams() {
  if (subpage === 0) return [[{ l: "HISTORY", t: "list", opts: hist.list.map((_, i) => String(i - (hist.list.length - 1) || "NOW")) }, hist.i], [{ l: "COMPARE", t: "list", opts: ["EDIT", "ORIG"] }, hist.cmp], [null], [null]];
  if (subpage === 1) return [[{ l: "SET A", t: "list", opts: ["—", "STORED"] }, morph.a ? 1 : 0], [{ l: "SET B", t: "list", opts: ["—", "STORED"] }, morph.b ? 1 : 0], [{ l: "MORPH", t: "num" }, morph.x], [{ l: "VIA", t: "list", opts: ["KNOB", "M1", "M2", "M3", "M4"] }, morph.via]];
  if (subpage === 2) return [[{ l: "WHAT", t: "list", opts: RAND_WHAT }, rnd.what], [{ l: "AMOUNT", t: "num" }, rnd.amt], [{ l: "ROLL", t: "list", opts: ["TURN"] }, 0], [{ l: "BACK", t: "list", opts: ["TURN"] }, 0]];
  const e = currentEntry();
  return [[{ l: "FILTER", t: "list", opts: LIB_FILTERS }, lib.filter], [{ l: "FAVOURITE", t: "list", opts: ["OFF", "ON"] }, e && lib.favs.has(entryKey(e)) ? 1 : 0], [null], [null]];
}
function toolEncoder(role, k) {
  if (role === "select") { subpage = clamp(subpage + Math.sign(k), 0, TOOL_PAGES.length - 1); return; }
  if (role === "algo") return;
  const d = Math.sign(k);
  if (subpage === 0) { if (role === 0) histGo(d); if (role === 1) setCompare(d > 0 ? 1 : 0); }
  else if (subpage === 1) {
    if (role === 0 && d > 0) { morph.a = snapshot("A"); say("MORPH A STORED", "MORPH"); }
    if (role === 1 && d > 0) { morph.b = snapshot("B"); say("MORPH B STORED", "MORPH"); }
    if (role === 2) { if (!morph.a || !morph.b) { say("STORE A AND B FIRST", "MORPH"); return; } morphTo(clamp(morph.x + k * .01, 0, 1)); }
    if (role === 3) { morph.via = clamp(morph.via + d, 0, 4); morph.lastVia = -1; say(morph.via ? `MORPH FOLLOWS M${morph.via}` : "MORPH ON KNOB 3", "MORPH"); }
  } else if (subpage === 2) {
    if (role === 0) rnd.what = clamp(rnd.what + d, 0, RAND_WHAT.length - 1);
    if (role === 1) rnd.amt = clamp(rnd.amt + k * .01, .02, 1);
    if (role === 2 && d > 0) randomize();
    if (role === 3 && d < 0) histGo(-1);
  } else {
    if (role === 0) { lib.filter = clamp(lib.filter + d, 0, LIB_FILTERS.length - 1); storeLib(); say(`PRESETS: ${LIB_FILTERS[lib.filter]} · ${patchList().length}`, "HOME"); }
    if (role === 1) { const e = currentEntry(); if (!e) return; const key = entryKey(e); if (d > 0) lib.favs.add(key); else lib.favs.delete(key); storeLib(); say(d > 0 ? "FAVOURITE ★" : "NOT A FAVOURITE", "HOME"); }
  }
}
function drawTools() {
  const c = KC.tool;
  header("TOOLS", c, TOOL_PAGES[subpage], "TOOLS", TOOL_PAGES.length, subpage);
  well(VX, VY, VW, VH);
  clipTo(VX, VY, VW, VH, () => [visUndo, visMorph, visRandom, visLibrary][subpage](VX, VY, VW, VH, c));
  const tp = toolParams();
  cells(tp.map((x) => x[0]), tp.map((x) => x[1] ?? 0), null, c);
  footer();
}
function visUndo(X, Y, W, H, c) {
  const n = hist.list.length;
  for (let k = 0; k < 4; k++) {
    const j = hist.i - 1 + k; if (j < 0 || j >= n) continue;
    const y = Y + 12 + k * 14, cur = j === hist.i;
    if (cur) rect(X + 4, y - 10, W - 8, 13, rgba(c, .18), 3);
    T(X + 10, y, String(j - (n - 1) || "NOW"), { size: 9, c: cur ? c : C.dim });
    T(X + 40, y, (hist.list[j].label || "").slice(0, 26), { size: 10, c: cur ? C.fg : C.dim });
  }
  // the timeline: one dot per step, the current one glowing
  for (let j = 0; j < n; j++) dot(X + W - 8 - (n - 1 - j) * 7, Y + H - 6, j === hist.i ? 2.2 : 1.3, j === hist.i ? c : C.dim, j === hist.i);
  if (hist.cmp) { rect(X + W - 54, Y + 4, 50, 13, C.gold, 3); T(X + W - 29, Y + 14, "ORIG", { a: "center", size: 10, c: "#111" }); }
  else if (hist.edited) T(X + W - 6, Y + 14, "EDITED", { a: "right", size: 9, c: C.gold });
}
function visMorph(X, Y, W, H, c) {
  const A = morph.a, B = morph.b;
  rect(X + 6, Y + 8, 26, 20, A ? rgba(c, .7) : "#1b1b19", 4); T(X + 19, Y + 22, "A", { a: "center", size: 12, c: A ? "#111" : C.dim });
  rect(X + W - 32, Y + 8, 26, 20, B ? rgba(c, .7) : "#1b1b19", 4); T(X + W - 19, Y + 22, "B", { a: "center", size: 12, c: B ? "#111" : C.dim });
  const x0 = X + 40, x1 = X + W - 40, px = x0 + (x1 - x0) * morph.x;
  rect(x0, Y + 17, x1 - x0, 2, C.faint, 1); rect(x0, Y + 17, px - x0, 2, c, 1); dot(px, Y + 18, 3, c);
  if (A && B) {     // how far apart A and B are, parameter by parameter
    const diffs = [];
    for (const s of SLOTS) if (A.slots[s.id].mod === B.slots[s.id].mod) A.slots[s.id].p.forEach((v, i) => diffs.push(Math.abs(v - B.slots[s.id].p[i])));
    A.amp.forEach((v, i) => diffs.push(Math.abs(v - B.amp[i])));
    const nb = Math.min(diffs.length, 54), bw = (W - 12) / nb;
    for (let i = 0; i < nb; i++) { const h = Math.min(1, diffs[i]) * 24, wob = 1 + .15 * Math.sin(animT * 3 + i); rect(X + 6 + i * bw, Y + H - 4 - h * wob, Math.max(1, bw - 1), h * wob, rgba(c, .25 + .6 * Math.min(1, diffs[i] * 2)), 1); }
  } else T(X + W / 2, Y + H - 12, "TURN K1 AND K2 TO STORE A AND B", { a: "center", size: 9, c: C.dim });
}
function visRandom(X, Y, W, H, c) {
  const age = animT - rnd.t, spin = Math.max(0, 1 - age * 1.5);
  g.save(); g.translate(X + 30, Y + H / 2); g.rotate(spin * 6); rect(-13, -13, 26, 26, rgba(c, .8), 5);
  const pips = [[-6, -6], [6, 6], [0, 0], [6, -6], [-6, 6]].slice(0, 1 + (Math.floor(animT * (spin ? 12 : 0)) % 5));
  for (const [x, y] of pips) dot(x, y, 2, "#111", false);
  g.restore();
  T(X + 62, Y + 22, `RANDOM ${RAND_WHAT[rnd.what]}`, { size: 13 });
  T(X + 62, Y + 38, `BY ${Math.round(rnd.amt * 100)} · K3 ROLLS · K4 BACK`, { size: 9, c: C.dim });
  for (let i = 0; i < 20; i++) { const h = 4 + 10 * Math.abs(Math.sin(i * 1.7 + rnd.t * 9)) * (spin ? 1 : .6); rect(X + 62 + i * 8, Y + H - 4 - h, 5, h, rgba(c, .2 + spin * .6), 1); }
}
function visLibrary(X, Y, W, H, c) {
  const list = patchList(), cur = list.findIndex((e) => e.name === patchName);
  const top = clamp(cur - 1, 0, Math.max(0, list.length - 4));
  for (let k = 0; k < 4 && top + k < list.length; k++) {
    const e = list[top + k], y = Y + 13 + k * 14, sel = top + k === cur;
    if (sel) rect(X + 4, y - 10, W - 8, 13, rgba(c, .18), 3);
    if (lib.favs.has(entryKey(e))) T(X + 10, y, "★", { size: 10, c: C.gold });
    T(X + 24, y, e.name.slice(0, 22), { size: 10, c: sel ? C.fg : C.dim });
  }
  T(X + W - 6, Y + 13, `${LIB_FILTERS[lib.filter]} ${list.length}`, { a: "right", size: 9, c });
}

// ---------------------------------------------------------------- arpeggiator (ARP pressed twice), with a Turing machine
const ARP_MODES = ["OFF", "UP", "DOWN", "UPDN", "DNUP", "RANDOM", "ORDER", "CHORD", "REPEAT", "UP+8", "CONV", "DIVG", "PINKY", "THUMB", "TURING"];
const ARP_RATES = ["1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"], ARP_BEATS = [1, .5, 1 / 3, .25, 1 / 6, .125];
const ARPDEF = { kind: "arp", pages: [[L("MODE", ARP_MODES), L("RATE", ARP_RATES, 3), I("OCTAVE", 1, 4, 1), n("GATE", .5)], [L("HOLD", ["OFF", "ON"]), n("MUTATE", .1), I("LOOP", 1, 16, 8), n("SWING", 0)]] };
const arpS = { p: blankParams(ARPDEF) };
try { const v = JSON.parse(localStorage.getItem("fm1rack.arp.v1") || "null"); if (v && v.length === arpS.p.length) arpS.p = v; } catch { /* defaults */ }
function storeArp() { try { localStorage.setItem("fm1rack.arp.v1", JSON.stringify(arpS.p)); } catch { /* this visit only */ } }
const arp = { pool: [], vel: .75, nextT: 0, idx: 0, reg: 0xb5ad, cur: [], flash: -9, running: false, step: 0 };
const arpOn = () => arpS.p[0] > 0;
// the notes the arp plays: held and latched keys (as chords), held MIDI notes, in the order they arrived
function arpUpdate(pool, vel) {
  if (vel) arp.vel = vel;
  if (!pool.length && arpS.p[4] === 1) { arp.latchFree = true; return; }      // LATCH: keep the last notes
  if (arpS.p[4] === 1 && arp.latchFree && pool.length) { arp.latchFree = false; arp.idx = 0; }
  if (pool.length && !arp.pool.length) { arp.idx = 0; arp.nextT = 0; }
  arp.pool = pool;
}
function arpSequence() {
  const pool = arp.pool, mode = ARP_MODES[arpS.p[0]], oct = arpS.p[2];
  const asc = []; const sorted = [...pool].sort((a, b) => a - b);
  for (let o = 0; o < oct; o++) for (const nt of sorted) asc.push(nt + 12 * o);
  const ord = []; for (let o = 0; o < oct; o++) for (const nt of pool) ord.push(nt + 12 * o);
  const lo = asc[0], hi = asc[asc.length - 1];
  switch (mode) {
    case "DOWN": return [...asc].reverse();
    case "UPDN": return asc.length > 2 ? asc.concat(asc.slice(1, -1).reverse()) : asc;
    case "DNUP": { const d = [...asc].reverse(); return d.length > 2 ? d.concat(d.slice(1, -1).reverse()) : d; }
    case "ORDER": return ord;
    case "REPEAT": return asc.flatMap((x) => [x, x]);
    case "UP+8": return asc.flatMap((x) => [x, x + 12]);
    case "CONV": { const o = []; let a = 0, b = asc.length - 1; while (a <= b) { o.push(asc[a++]); if (a <= b) o.push(asc[b--]); } return o; }
    case "DIVG": { const o = []; let m = Math.floor((asc.length - 1) / 2), k = 0; while (o.length < asc.length) { const j = m + (k % 2 ? -Math.ceil(k / 2) : Math.ceil(k / 2)); if (j >= 0 && j < asc.length) o.push(asc[j]); k++; } return o; }
    case "PINKY": return asc.slice(0, -1).flatMap((x) => [x, hi]).concat(asc.length === 1 ? [hi] : []);
    case "THUMB": return asc.slice(1).flatMap((x) => [lo, x]).concat(asc.length === 1 ? [lo] : []);
    default: return asc;
  }
}
function arpPick() {
  const mode = ARP_MODES[arpS.p[0]];
  if (mode === "CHORD") return [...arp.pool];
  const sq = arpSequence(); if (!sq.length) return [];
  if (mode === "RANDOM") return [sq[Math.floor(Math.random() * sq.length)]];
  if (mode === "TURING") {      // a looping shift register; MUTATE is how often a bit flips
    const L = arpS.p[6], mask = (1 << L) - 1;
    let bit = (arp.reg >> (L - 1)) & 1; if (Math.random() < arpS.p[5] * .5) bit ^= 1;
    arp.reg = ((arp.reg << 1) | bit) & mask;
    const asc = [...sq].sort((a, b) => a - b);
    return [asc[Math.min(asc.length - 1, Math.floor((arp.reg / Math.max(1, mask)) * asc.length))]];
  }
  return [sq[arp.idx++ % sq.length]];
}
function arpTick() {
  if (!audio.on || !arpOn() || !arp.pool.length) { arp.running = false; return; }
  const now = audioNow(), beat = 60 / glo.p[G_TEMPO], d = ARP_BEATS[arpS.p[1]] * beat;
  if (!arp.running || arp.nextT < now - .05) { arp.nextT = now + .01; arp.running = true; arp.step = 0; }
  while (arp.nextT < now + .1) {
    const t = arp.nextT + (arp.step % 2 ? arpS.p[7] * .5 * d : 0), notes = expandUni(arpPick());
    if (notes.length) {
      engineAt(t, notes.map((x) => ["note_on", x, arp.vel]), "arp");
      engineAt(t + Math.max(.02, arpS.p[3] * d * .95), notes.map((x) => ["note_off", x]), "arp");
      noteOut(notes, arp.vel, t, t + Math.max(.02, arpS.p[3] * d * .95));
      const wait = Math.max(0, (t - now) * 1000);
      setTimeout(() => { arp.cur = notes; arp.flash = animT; notes.forEach((x) => onNote(x, arp.vel)); seqLight(notes); }, wait);
    }
    arp.nextT += d; arp.step++;
  }
}
setInterval(arpTick, 25);
function visArp(X, Y, W, H, c) {
  const mode = ARP_MODES[arpS.p[0]];
  if (mode === "OFF") { T(X + W / 2, Y + H / 2 + 4, "ARP OFF · TURN K1", { a: "center", size: 12, c: C.dim }); return; }
  const sq = arp.pool.length ? arpSequence() : [];
  const fl = Math.max(0, 1 - (animT - arp.flash) * 5);
  if (mode === "TURING") {         // the register's bits, lit
    const L = arpS.p[6];
    for (let b = 0; b < 16; b++) { const on = b < L && ((arp.reg >> (L - 1 - b)) & 1); rect(X + 8 + b * 13.5, Y + 6, 10, 10, b >= L ? "#0e0e0d" : on ? rgba(c, .5 + .5 * fl) : "#1d1d1b", 2); }
  }
  const lo = Math.min(...(sq.length ? sq : [60])), hi = Math.max(...(sq.length ? sq : [72])), span = Math.max(12, hi - lo);
  const n = Math.min(sq.length, 24), bw = (W - 16) / Math.max(1, n), top = mode === "TURING" ? Y + 22 : Y + 8;
  for (let i = 0; i < n; i++) {
    const h = 6 + ((sq[i] - lo) / span) * (Y + H - 6 - top - 6), on = arp.cur.includes(sq[i]) && fl > 0;
    rect(X + 8 + i * bw, Y + H - 6 - h, Math.max(2, bw - 2), h, on ? rgba(c, .6 + .4 * fl) : rgba(c, .22), 2);
  }
  if (!sq.length) T(X + W / 2, Y + H / 2 + 8, `${mode} · HOLD SOME KEYS`, { a: "center", size: 11, c: C.dim });
}

// ---------------------------------------------------------------- modulation sequencer: four lanes of 16 steps, matrix sources SEQ 1-4
const MSEQ_RATES = ["1/32", "1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BARS"], MSEQ_BEATS = [.125, .25, .5, 1, 2, 4, 8];
const MSEQ_SHAPES = ["—", "RAMP+", "RAMP-", "TRI", "SINE", "SQR", "RND", "FLAT", "INVERT", "SHIFT"];
function shapeLane(name, v) {
  const f = (i) => i / 15;
  switch (name) {
    case "RAMP+": return v.map((_, i) => f(i) * 2 - 1);
    case "RAMP-": return v.map((_, i) => 1 - f(i) * 2);
    case "TRI": return v.map((_, i) => 1 - Math.abs(i / 7.5 - 1) * 2);
    case "SINE": return v.map((_, i) => Math.sin(i / 16 * TAU));
    case "SQR": return v.map((_, i) => (i % 8 < 4 ? 1 : -1));
    case "RND": return v.map(() => Math.round((Math.random() * 2 - 1) * 8) / 8);
    case "FLAT": return v.map(() => 0);
    case "INVERT": return v.map((x) => -x);
    case "SHIFT": return v.map((_, i) => v[(i + 15) % 16]);
    default: return v;
  }
}
const blankMseq = () => ({ rate: 2, lanes: ["RAMP+", "TRI", "SQR", "RND"].map((s) => ({ v: shapeLane(s, new Array(16).fill(0)), smooth: s === "SQR" ? 0 : .3 })) });
let mseq = blankMseq();
const ms = { lane: 0, cursor: 0, anchor: 0, last: -1, cur: -1, shape: 0 };
function msRealign(t) { ms.anchor = t; ms.last = -1; }
function msTick() {
  if (!audio.on) return;
  const now = audioNow(), d = MSEQ_BEATS[mseq.rate] * 60 / glo.p[G_TEMPO];
  if (!ms.anchor || now - ms.anchor > 3600) msRealign(now);
  const upto = Math.floor((now + .08 - ms.anchor) / d);
  if (ms.last < upto - 8) ms.last = upto - 1;
  for (let s = ms.last + 1; s <= upto; s++) {
    const t = ms.anchor + s * d, st = ((s % 16) + 16) % 16;
    engineAt(t, mseq.lanes.map((l, i) => ["set_ext", i, l.v[st]]), "ms");
    setTimeout(() => { ms.cur = st; dirty = true; }, Math.max(0, (t - now) * 1000));
  }
  ms.last = upto;
}
setInterval(msTick, 25);
function mseqParams() {
  const ln = mseq.lanes[ms.lane];
  return [[{ l: "VALUE", t: "bip" }, ln.v[ms.cursor]], [{ l: "SHAPE", t: "list", opts: MSEQ_SHAPES }, ms.shape], [{ l: "RATE", t: "list", opts: MSEQ_RATES }, mseq.rate], [{ l: "SMOOTH", t: "num" }, ln.smooth]];
}
function mseqEncoder(role, k) {
  const ln = mseq.lanes[ms.lane], d = Math.sign(k);
  if (role === "select") { ms.cursor = clamp(ms.cursor + d, 0, 15); return; }
  if (role === "algo") { ms.lane = clamp(ms.lane + d, 0, 3); ms.shape = 0; return; }
  if (role === 0) ln.v[ms.cursor] = clamp(Math.round((ln.v[ms.cursor] + k * .02) * 100) / 100, -1, 1);
  if (role === 1) { ms.shape = clamp(ms.shape + d, 0, MSEQ_SHAPES.length - 1); if (ms.shape) ln.v = shapeLane(MSEQ_SHAPES[ms.shape], ln.v); }
  if (role === 2) mseq.rate = clamp(mseq.rate + d, 0, MSEQ_RATES.length - 1);
  if (role === 3) ln.smooth = clamp(ln.smooth + k * .01, 0, 1);
  noteEdit(`SEQ ${ms.lane + 1} ${["VALUE", "SHAPE", "RATE", "SMOOTH"][role]}`);
}
function drawMseq() {
  const c = KC.lfo;
  header("MSEQ", c, `LANE ${ms.lane + 1} · ${ms.cursor + 1}`, "LFO");
  well(VX, VY, VW, VH);
  // the lane being edited, drawn big; the other three small underneath
  const big = mseq.lanes[ms.lane], bw = (VW - 8) / 16, mid = VY + 22;
  for (let i = 0; i < 16; i++) {
    const v = big.v[i], x = VX + 4 + i * bw, on = i === ms.cur, h = v * 18;
    rect(x + 1, Math.min(mid, mid - h), bw - 2, Math.max(1, Math.abs(h)), on ? c : rgba(c, .45), 1);
    if (i === ms.cursor && Math.floor(animT * 3) % 2) frame(x, VY + 2, bw, 40, "#fff", 2);
    if (on) rect(x + 1, VY + 42, bw - 2, 2, C.fg);
  }
  rect(VX + 4, mid, VW - 8, 1, C.faint);
  let row = 0;
  for (let L = 0; L < 4; L++) {
    if (L === ms.lane) continue;
    const y = VY + 50 + row * 4;
    T(VX + 4, y + 3, String(L + 1), { size: 6, c: C.dim });
    for (let i = 0; i < 16; i++) rect(VX + 12 + i * 13.4, y, 12, 3, rgba(c, .15 + .6 * Math.abs(mseq.lanes[L].v[i]) * (i === ms.cur ? 1.6 : 1)), 1);
    row++;
  }
  const mp = mseqParams();
  cells(mp.map((x) => x[0]), mp.map((x) => x[1]), null, c);
  footer();
}

// ---------------------------------------------------------------- voice modes, tuning
function expandUni(notes) {
  const m = amp.p[A_MODE]; if (m < 3) return notes;
  const d = amp.p[A_DETUNE] * .3, offs = m === 3 ? [-d, d] : [-d, -d / 3, d / 3, d];
  return notes.flatMap((x) => offs.map((o) => Math.round((x + o) * 1000) / 1000));
}
function tuningCalls() {
  const [, table] = TUNINGS[glo.p[G_TUNE]], N = table.length, rootPc = glo.p[G_TROOT];
  const a4 = 12 * Math.log2(glo.p[G_A4] / 440), R = 60 - ((60 - rootPc) % 12 + 12) % 12;
  const calls = [];
  for (let x = 0; x < 128; x++) {
    let tuned;
    if (N === 12) { const pc = (((x - R) % 12) + 12) % 12; tuned = x + (table[pc] - pc * 100) / 100; }
    else { const dd = x - R, o = Math.floor(dd / N), deg = ((dd % N) + N) % N; tuned = R + (o * 1200 + table[deg]) / 100; }
    calls.push(["set_tune", x, tuned - x + a4]);
  }
  return calls;
}

// ---------------------------------------------------------------- MIDI out: keys, sequencer and arp notes, and the macros as CCs
const mout = { outs: [] };
function midiSend(bytes, at) {
  if (!glo.p[G_MOUT] || !mout.outs.length) return;
  const ts = at ? performance.now() + Math.max(0, (at - audioNow()) * 1000) : undefined;
  for (const o of mout.outs) try { o.send(bytes, ts); } catch { /* port gone */ }
}
function noteOut(notes, vel, onAt, offAt) {
  if (!glo.p[G_MOUT]) return;
  const ch = glo.p[G_OUTCH] & 15, seen = new Set();
  for (const x of notes) {
    const nn = clamp(Math.round(x), 0, 127); if (seen.has(nn)) continue; seen.add(nn);
    if (onAt !== null) midiSend([0x90 | ch, nn, clamp(Math.round(vel * 127), 1, 127)], onAt);
    if (offAt !== null && offAt !== undefined) midiSend([0x80 | ch, nn, 0], offAt);
  }
}
function ccOut(cc, v) { if (glo.p[G_MOUT] === 2) midiSend([0xB0 | (glo.p[G_OUTCH] & 15), cc, clamp(Math.round(v * 127), 0, 127)]); }

// ---------------------------------------------------------------- GLOBAL bands for the new pages: tuning, MIDI, system load
const cpuHist = [];
function visTuning(X, Y, W, H, c) {
  const [name, table] = TUNINGS[glo.p[G_TUNE]], N = table.length;
  T(X + 8, Y + 16, name, { size: 14 }); T(X + 8, Y + 30, `${N} NOTES · ROOT ${KEYS12[glo.p[G_TROOT]]} · A4 ${glo.p[G_A4]} HZ`, { size: 9, c: C.dim });
  // each degree against equal temperament
  for (let i = 0; i < N; i++) {
    const x = X + 8 + i * ((W - 16) / N), cents = N === 12 ? table[i] - i * 100 : table[i] - Math.round(table[i] / 100) * 100;
    const h = clamp(cents, -50, 50) / 50 * 12, wob = Math.sin(animT * 2 + i) * .5;
    rect(x, Y + 50 - Math.max(0, h) + wob, Math.max(2, (W - 16) / N - 2), Math.max(1, Math.abs(h)), rgba(c, .7), 1);
  }
  rect(X + 8, Y + 50, W - 16, 1, C.faint);
}
function visMidiOut(X, Y, W, H, c) {
  const on = glo.p[G_MOUT];
  icon("MIDI", X + 8, Y + 8, on ? C.ok : C.dim);
  T(X + 26, Y + 18, on ? `OUT ON CH ${glo.p[G_OUTCH] + 1}${on === 2 ? " · MACRO CCS 21-24" : ""}` : "MIDI OUT OFF", { size: 10 });
  T(X + 26, Y + 32, `${mout.outs.length} OUTPUT${mout.outs.length === 1 ? "" : "S"} · PROG CHANGE ${glo.p[G_PROG] ? "ON" : "OFF"}`, { size: 9, c: C.dim });
  T(X + 26, Y + 46, glo.p[G_LOCAL] ? "LOCAL OFF: KEYS ONLY SEND MIDI" : `BEND ±${glo.p[G_BEND]} · CC11 EXPRESSION ${Math.round(midiState.expr * 100)}`, { size: 9, c: glo.p[G_LOCAL] ? C.warn : C.dim });
}
function visSystem(X, Y, W, H, c) {
  const st = audio.cpu || [0, 0, 0], bg = budget();
  T(X + 8, Y + 14, `CPU ${Math.round(st[0])}%`, { size: 12 }); T(X + 80, Y + 14, `PEAK ${Math.round(st[1])}%`, { size: 10, c: st[1] > 90 ? C.warn : C.dim });
  T(X + 150, Y + 14, `LATE ${st[2]}`, { size: 10, c: st[2] ? C.warn : C.dim });
  T(X + 8, Y + 27, bg.hw ? `BROWSER CPU · FM-1 BUDGET ${bg.cpu}/${bg.B} UNITS` : "BROWSER CPU · FM-1 LIMITS OFF", { size: 8, c: C.dim });
  const n = cpuHist.length;
  const pts = cpuHist.map((v, i) => [X + 6 + i * ((W - 12) / Math.max(1, HIST - 1)), Y + H - 4 - Math.min(1, v / 100) * 26]);
  rect(X + 6, Y + H - 30, W - 12, 26, "#0f0f0e", 2);
  if (n > 1) glow(pts, st[0] > 70 ? C.warn : c);
}

// HARDWARE: what this patch needs on an FM-1, against the assumed budget
function visHardware(X, Y, W, H, c) {
  const bg = budget();
  if (!bg.hw) {
    T(X + 8, Y + 14, "BROWSER: FM-1 LIMITS OFF", { size: 10, c: C.gold });
    T(X + 8, Y + 28, `6 VOICES OF ANYTHING · DELAY 2 S, 32-BIT`, { size: 9, c: C.dim });
    T(X + 8, Y + 42, `AT ${bg.voices} VOICES IT NEEDS ${Math.round(bg.ram)}K RAM`, { size: 9, c: C.dim });
    T(X + 8, Y + 56, "SET TARGET TO FM-1 FOR THE PREVIEW", { size: 8, c: C.dim });
    return;
  }
  const cap = amp.p[A_MODE] === 1 || amp.p[A_MODE] === 2 ? 1 : glo.p[G_VOICES] || MAXV;
  T(X + 8, Y + 14, bg.dropped.length ? (bg.over === "FW" ? `NOT IN FIRMWARE: ${bg.dropped.map((d) => rack[d].mod).join(" + ")}` : `NO RAM: ${bg.dropped.map((d) => rack[d].mod).join(" + ")} UNLOADED`) : bg.over ? "OVER ON CPU: WOULD GLITCH" : `FITS · ${bg.voices} OF ${cap} VOICES`, { size: 10, c: bg.over ? C.warn : bg.limit ? C.gold : C.ok });
  if (bg.limit && !bg.over) T(X + W - 8, Y + 14, `${bg.limit} LIMITS`, { size: 9, a: "right", c: C.gold });
  T(X + 8, Y + 28, `RAM ${Math.round(bg.ram)}/${bg.pool}K · CPU ${bg.cpu}/${bg.B}`, { size: 9 });
  // the biggest memory user
  let big = null;
  for (const s of SLOTS) { const m = MODULES[rack[s.id].mod]; if (!m || bg.dropped.includes(s.id)) continue; const k = m.ram + m.ramV * bg.voices; if (!big || k > big.k) big = { n: rack[s.id].mod, k, v: m.ramV }; }
  if (big) T(X + 8, Y + 42, `MOST RAM: ${big.n} ${big.v ? `${Math.round(big.v)}K × ${bg.voices}V` : `${Math.round(big.k)}K`}`, { size: 9, c: C.dim });
  T(X + 8, Y + 56, "FREE RAM + CPU SPEED ARE ASSUMED", { size: 8, c: C.dim });
}

// FIRMWARE (Lite): what the firmware's flash holds, against the app area in Felucca's memory map
function visFirmware(X, Y, W, H, c) {
  const f = flashUse(), pct = f.used / FW_AREA;
  T(X + 8, Y + 14, `FLASH ${Math.round(f.used)} / ${FW_AREA} KB`, { size: 10, c: f.left < 0 ? C.warn : C.fg });
  rect(X + 8, Y + 19, W - 16, 5, C.faint, 2); rect(X + 8, Y + 19, Math.min(1, pct) * (W - 16), 5, f.left < 0 ? C.warn : f.left < 100 ? C.gold : C.ok, 2);
  T(X + 8, Y + 38, f.left < 0 ? `OVER BY ${Math.round(-f.left)} KB: TAKE A MODULE OUT` : `${Math.round(f.left)} KB LEFT FOR SCREEN, KEYS, SEQ, FONTS`, { size: 9, c: f.left < 0 ? C.warn : f.left < 100 ? C.gold : C.ok });
  T(X + 8, Y + 52, "MEASURED · UI NEEDS NOT KNOWN YET", { size: 8, c: C.dim });
}

// ARP is a fixed page like AMP and GLOBAL
FIXED.ARP = [ARPDEF, arpS];

// MASTER band: the EQ's curve, and how hard the compressor and limiter are working
function visMaster(X, Y, W, H, c) {
  const gains = [glo.p[G_LOW] * 12, glo.p[G_MID] * 10, glo.p[G_HIGH] * 12], pts = [];
  for (let i = 0; i <= 60; i++) {
    const f = 20 * Math.pow(1000, i / 60), lf = Math.log2(f);
    const db = gains[0] / (1 + Math.pow(2, (lf - Math.log2(150)) * 3)) + gains[1] * Math.exp(-Math.pow(lf - Math.log2(1000), 2) * 1.2) + gains[2] / (1 + Math.pow(2, -(lf - Math.log2(5000)) * 3));
    pts.push([X + 6 + i * ((W - 70) / 60), Y + H / 2 - db * 1.8]);
  }
  rect(X + 6, Y + H / 2, W - 70, 1, C.faint); glow(pts, c);
  const comp = mon[160] || 0, lim = mon[161] || 0, mx = X + W - 56;
  for (const [k, lab, v] of [[0, "COMP", comp], [1, "LIMIT", lim]]) {
    const x = mx + k * 26;
    rect(x, Y + 6, 10, H - 22, "#141413", 2);
    const h = clamp(v / 12, 0, 1) * (H - 22); rect(x, Y + 6, 10, h, v > 6 ? C.warn : C.gold, 2);
    T(x + 5, Y + H - 5, lab, { a: "center", size: 7, c: C.dim });
  }
}
