// ================================================================ 4. pages
function liveFor(slotIdx, count) {
  // the engine reports modulated values for the slot the screen is watching
  if (!audio.on || watchSlot !== slotIdx) return null;
  return Array.from(mon.subarray(0, count));
}
function drawSlot(slot) {
  const si = SLOTS.indexOf(slot), st = rack[slot.id], m = MODULES[st.mod], c = KC[slot.kind];
  if (!m) {
    header(slot.name, c, "EMPTY", "EMPTY");
    well(VX, VY, VW, VH);
    const bob = Math.sin(animT * 2) * 3;
    icon("EMPTY", 114, 48 + bob, C.dim);
    T(120, 130, "EMPTY SLOT", { a: "center", size: 16, c: C.dim });
    T(120, 156, "TURN ALGORITHM", { a: "center", size: 12, c: C.dim });
    T(120, 174, "TO LOAD A MODULE", { a: "center", size: 12, c: C.dim });
    footer(); return;
  }
  subpage = Math.min(subpage, m.pages.length - 1);
  header(slot.name, c, st.mod, st.mod, m.pages.length, subpage);
  const live = liveFor(si, 16);
  const Pv = (i) => (live ? live[i] : st.p[i]);
  well(VX, VY, VW, VH);
  clipTo(VX, VY, VW, VH, () => VIS[st.mod](VX, VY, VW, VH, Pv, c, slot.id === "MODB" ? 1 : 0));
  const off = m.pages.slice(0, subpage).flat().length, params = m.pages[subpage];
  cells(params, params.map((_, i) => st.p[off + i]), live && params.map((_, i) => live[off + i]), c);
  footer();
}
function drawFixed(def, st, tag, titles, iconName, slotIdx, band) {
  subpage = Math.min(subpage, def.pages.length - 1);
  const c = KC[def.kind];
  header(tag, c, titles[subpage], iconName, def.pages.length, subpage);
  well(VX, VY, VW, VH);
  clipTo(VX, VY, VW, VH, () => band(VX, VY, VW, VH, c));
  const off = def.pages.slice(0, subpage).flat().length, params = def.pages[subpage];
  const live = slotIdx >= 0 ? liveFor(slotIdx, 16) : null;
  cells(params, params.map((_, i) => st.p[off + i]), live && params.map((_, i) => live[off + i]), c);
  footer();
}
function drawPerform() {
  header("HOME", C.fg, patchName, "HOME");
  // output scope
  well(6, 30, 228, 34);
  const sc = audio.scope;
  let start = 0; for (let i = 1; i < sc.length - 72; i++) if (sc[i - 1] < 0 && sc[i] >= 0) { start = i; break; }
  const pts = []; for (let i = 0; i < 72; i++) { const v = sc[start + i] || 0; pts.push([8 + i * (224 / 71), 47 - Math.max(-1, Math.min(1, v * 2.4)) * 14]); }
  glow(pts, audio.on ? C.ok : "#3a3a36");
  if (!audio.on) T(120, 51, "OFF", { a: "center", size: 10, c: C.dim });
  // macros
  const pos = [[62, 96], [178, 96], [62, 152], [178, 152]];
  const dsts = destinations();
  for (let i = 0; i < 4; i++) {
    const [x, y] = pos[i], v = macros[i], tgt = matrix.filter((r) => r.src === `M${i + 1}` && dsts.includes(r.dst));
    const sel = learn && learnMacro === i, col = tgt.length ? C.gold : C.dim;
    const st = vs["mac" + i] || (vs["mac" + i] = { v, t: -9 });
    if (Math.abs(st.v - v) > .001) { st.v = v; st.t = animT; }
    const flash = Math.max(0, 1 - (animT - st.t) * 3);
    g.lineCap = "round"; g.lineWidth = 5;
    g.strokeStyle = C.faint; g.beginPath(); g.arc(x, y, 18, Math.PI * .75, Math.PI * 2.25); g.stroke();
    g.strokeStyle = rgba(col, .18 + flash * .3); g.lineWidth = 11; g.beginPath(); g.arc(x, y, 18, Math.PI * .75, Math.PI * (.75 + 1.5 * v)); g.stroke();
    g.strokeStyle = col; g.lineWidth = 5; g.beginPath(); g.arc(x, y, 18, Math.PI * .75, Math.PI * (.75 + 1.5 * v)); g.stroke();
    const a = Math.PI * (.75 + 1.5 * v); dot(x + Math.cos(a) * 18, y + Math.sin(a) * 18, 2, "#fff");
    T(x, y + 5, String(Math.round(v * 100)), { a: "center", size: 13 });
    T(x - 50, y - 16, `M${i + 1}`, { size: 10, c: sel ? C.gold : C.dim });
    if (sel && Math.floor(animT * 4) % 2) frame(x - 54, y - 26, 108, 54, C.gold, 6);
    const lab = tgt.length ? tgt[0].dst + (tgt.length > 1 ? ` +${tgt.length - 1}` : "") : "UNASSIGNED";
    T(x, y + 32, lab.length > 15 ? lab.slice(0, 14) + "…" : lab, { a: "center", size: 9, c: tgt.length ? C.fg : C.dim });
  }
  // modulation activity: MOD A, MOD B, LFO 1-3
  const acts = [["A", mon[40], KC.mod], ["A2", mon[41], KC.mod], ["B", mon[42], KC.mod], ["B2", mon[43], KC.mod], ["L1", mon[44], KC.lfo], ["L2", mon[45], KC.lfo], ["L3", mon[46], KC.lfo]];
  acts.forEach(([nm, v, c], i) => {
    const x = 8 + i * 32;
    T(x, 206, nm, { size: 8, c: C.dim });
    rect(x + 12, 200, 16, 6, C.faint, 2);
    const w = Math.max(-1, Math.min(1, v || 0)) * 8;
    rect(x + 20 + Math.min(0, w), 200, Math.abs(w), 6, c, 2);
  });
  footer();
}
function drawRack() {
  header("RACK", C.fg, patchName, "RACK");
  const tile = (x, y, w, slotId, lab, icn, col, sub) => {
    const name = slotId ? rack[slotId].mod : lab;
    rect(x, y, w, 36, name ? "#131312" : "#0b0b0a", 6); frame(x, y, w, 36, name ? rgba(col, .8) : "#262624", 6);
    icon(name ? (ICONS[name] ? name : icn) : "EMPTY", x + 5, y + 5, name ? col : C.faint);
    T(x + 20, y + 14, sub, { size: 8, c: col });
    T(x + 5, y + 30, name || "--", { size: 10, c: name ? C.fg : C.dim });
  };
  const flow = (pts, n, speed, col, lvl) => {        // dots travelling along a path
    let total = 0; const seg = []; for (let i = 1; i < pts.length; i++) { const d = Math.hypot(pts[i][0] - pts[i - 1][0], pts[i][1] - pts[i - 1][1]); seg.push(d); total += d; }
    g.strokeStyle = "#2a2a27"; g.lineWidth = 1; g.beginPath(); pts.forEach(([x, y], i) => (i ? g.lineTo(x, y) : g.moveTo(x, y))); g.stroke();
    for (let k = 0; k < n; k++) {
      let d = ((animT * speed + k * total / n) % total);
      for (let i = 0; i < seg.length; i++) { if (d <= seg[i]) { const u = d / seg[i]; dot(pts[i][0] + (pts[i + 1][0] - pts[i][0]) * u, pts[i][1] + (pts[i + 1][1] - pts[i][1]) * u, 1.5, rgba(col, .25 + .75 * lvl)); break; } d -= seg[i]; }
    }
  };
  const lvl = Math.min(1, envMax() + (mon[48] || 0));
  T(8, 40, "PER VOICE", { size: 8, c: C.dim });
  flow([[78, 62], [86, 62]], 1, 8, KC.src, lvl); flow([[156, 62], [164, 62]], 1, 8, KC.shp, lvl);
  flow([[198, 80], [198, 92], [42, 92], [42, 104]], 6, 60, KC.amp, lvl);
  flow([[78, 122], [86, 122]], 1, 8, KC.fx, lvl); flow([[156, 122], [164, 122]], 1, 8, KC.fx, lvl);
  tile(6, 44, 72, "SRC", null, "PLAITS", KC.src, "SRC");
  tile(86, 44, 70, "SHP", null, "FILTER", KC.shp, "SHP");
  tile(164, 44, 70, null, "ADSR", "AMP", KC.amp, `AMP · ${budget().voices}V`);
  T(8, 100, "SHARED", { size: 8, c: C.dim });
  tile(6, 104, 72, "FX1", null, "CLOUDS", KC.fx, "FX 1");
  tile(86, 104, 70, "FX2", null, "CLOUDS", KC.fx, "FX 2");
  // output with a pulsing speaker
  rect(164, 104, 70, 36, "#131312", 6); frame(164, 104, 70, 36, "#3a3a37", 6);
  icon("OUT", 169, 109, C.fg); T(184, 118, "OUT", { size: 8, c: C.dim });
  for (let k = 0; k < 3; k++) { const r = 4 + k * 5 + (animT * 10 % 5); g.strokeStyle = rgba(C.ok, lvl * (1 - k / 3) * .8); g.beginPath(); g.arc(214, 122, r, -.7, .7); g.stroke(); }
  // modulation row, dashed lines reaching upward
  T(8, 160, "MODULATION", { size: 8, c: C.dim });
  tile(6, 164, 72, "MODA", null, "PEAKS", KC.mod, "MOD A");
  tile(86, 164, 70, "MODB", null, "PEAKS", KC.mod, "MOD B");
  const nr = matrix.filter((r) => r.src !== "—" && r.dst !== "—").length;
  rect(164, 164, 70, 36, "#131312", 6); frame(164, 164, 70, 36, rgba(KC.mx, .7), 6);
  icon("MATRIX", 169, 169, KC.mx); T(184, 178, "MATRIX", { size: 8, c: KC.mx }); T(169, 194, `${nr} ROUTES`, { size: 10 });
  g.save(); g.setLineDash([2, 3]); g.lineDashOffset = -animT * 20; g.strokeStyle = rgba(KC.mod, .6);
  g.beginPath(); g.moveTo(42, 164); g.lineTo(42, 150); g.lineTo(234, 150); g.stroke(); g.restore();
  footer();
}
function drawMatrix() {
  header("MATRIX", KC.mx, `ROW ${mxRow + 1}/12`, "MATRIX");
  T(8, 40, "SOURCE", { size: 9, c: C.dim }); T(66, 40, "DESTINATION", { size: 9, c: C.dim }); T(176, 40, "AMT", { size: 9, c: C.dim, a: "right" }); T(204, 40, "VIA", { size: 9, c: C.dim });
  const top = Math.max(0, Math.min(mxRow - 2, 12 - 6)), dst = destinations();
  for (let k = 0; k < 6; k++) {
    const i = top + k, r = matrix[i], y = 46 + k * 27, sel = i === mxRow;
    const dormant = r.dst !== "—" && !dst.includes(r.dst), active = r.src !== "—" && !dormant && r.dst !== "—";
    if (sel) { rect(4, y, 232, 25, "#24231e", 5); rect(4, y, 2, 25, KC.mx); }
    const c = active ? C.fg : C.dim;
    T(9, y + 16, r.src.slice(0, 8), { size: 10, c });
    if (active) { const v = mon[50 + i] || 0; const x = 56 + ((animT * 30 * (0.3 + Math.abs(v))) % 8); dot(x, y + 12, 1, rgba(KC.mx, .4 + Math.abs(v) * .6), false); }
    T(66, y + 16, (r.dst.length > 13 ? r.dst.slice(0, 12) + "…" : r.dst), { size: 10, c: dormant ? C.warn : c });
    T(176, y + 16, (r.amt >= 0 ? "+" : "") + Math.round(r.amt * 100), { size: 10, a: "right", c });
    // live amount meter
    const v = active ? Math.max(-1, Math.min(1, mon[50 + i] || 0)) : 0;
    rect(180, y + 10, 20, 4, C.faint, 2); rect(190 + Math.min(0, v * 10), y + 10, Math.abs(v * 10), 4, KC.mx, 2);
    T(204, y + 16, r.via === "—" ? "—" : r.via.slice(0, 5), { size: 10, c: C.dim });
  }
  T(8, 210, "K1 SRC · K2 DEST · K3 AMT · K4 VIA", { size: 9, c: C.dim });
  footer();
}
function drawSave() {
  const st = saveState;
  header("SAVE", C.fg, "NAME THIS PATCH", "SAVE");
  const bob = Math.sin(animT * 3) * 2;
  icon("SAVE", 10, 36 + bob, C.gold, 2);
  const tgt = userPatches[st.slot];
  T(40, 48, `TO U${String(st.slot + 1).padStart(2, "0")}`, { size: 14, c: C.gold });
  T(40, 60, tgt ? `REPLACES ${tgt.name}`.slice(0, 26) : "EMPTY SLOT", { size: 9, c: tgt ? C.warn : C.dim });
  for (let i = 0; i < 10; i++) {
    const x = 11 + i * 22, on = i === st.cursor;
    rect(x, 72, 19, 34, on ? "#2a2924" : "#141412", 3);
    T(x + 9.5, 98, st.name[i] === " " ? "·" : st.name[i], { a: "center", size: 20, c: on ? C.gold : C.fg });
    if (on) rect(x, 108, 19, 2, Math.floor(animT * 3) % 2 ? C.gold : "#7a6a3a");
  }
  // the 16 user slots
  for (let i = 0; i < USER_SLOTS; i++) {
    const x = 12 + (i % 8) * 28, y = 120 + Math.floor(i / 8) * 22, used = !!userPatches[i], sel = i === st.slot;
    rect(x, y, 24, 18, used ? "#2b2a26" : "#121211", 3);
    if (sel) frame(x - 1, y - 1, 26, 20, C.gold, 4);
    T(x + 12, y + 13, String(i + 1).padStart(2, "0"), { size: 9, a: "center", c: used ? C.fg : C.dim });
  }
  T(8, 180, "SELECT MOVE · K1/ALGO LETTER · K2 SLOT", { size: 9, c: C.dim });
  T(8, 198, "SAVE TO STORE · HOME TO CANCEL", { size: 10 });
  footer();
}
function drawPicker() {
  const s = SLOTS.find((x) => x.id === picker.slot), c = KC[s.kind];
  const open = Math.min(1, (performance.now() - picker.t) / 120);
  g.globalAlpha = open;
  rect(10, 28 + (1 - open) * 8, 220, 184, "#0d0d0c", 8); frame(10, 28 + (1 - open) * 8, 220, 184, c, 8);
  T(20, 46, `LOAD INTO ${s.name}`, { size: 11, c });
  s.choices.forEach((ch, i) => {
    const y = 54 + i * 25, sel = i === picker.ix;
    if (sel) { rect(16, y, 208, 23, "#25241f", 5); rect(16, y, 2, 23, c); }
    const f = fitOf(s.id, ch);
    icon(ch || "EMPTY", 24, y + 6, f.ok ? (sel ? c : C.dim) : C.faint);
    T(42, y + 16, ch || "EMPTY", { size: 12, c: f.ok ? C.fg : C.dim });
    T(218, y + 16, f.ok ? `${f.bg.voices}V` : `✕ ${f.why}`, { size: 10, a: "right", c: f.ok ? C.ok : C.warn });
  });
  const ch = s.choices[picker.ix];
  T(120, 186, ch ? MODULES[ch].blurb : "SLOT LEFT EMPTY", { a: "center", size: 10, c: C.fg });
  T(120, 202, `PRESS ${s.name} TO LOAD`, { a: "center", size: 9, c: C.dim });
  g.globalAlpha = 1;
}
function drawBoot(age) {
  g.fillStyle = "#000"; g.fillRect(0, 0, 240, 240);
  const names = ["PLAITS", "BRAIDS", "ELEMENTS", "RINGS", "WARPS", "FILTER", "PEAKS", "TIDES", "MARBLES", "GRIDS", "CLOUDS", "DELAY", "REVERB", "CHORUS"];
  const cols = [KC.src, KC.src, KC.src, KC.shp, KC.shp, KC.shp, KC.mod, KC.mod, KC.mod, KC.mod, KC.fx, KC.fx, KC.fx, KC.fx];
  names.forEach((nm, i) => {
    const appear = i * .05, a = Math.max(0, Math.min(1, (age - appear) * 5));
    const x = 18 + (i % 7) * 30, y = 150 + Math.floor(i / 7) * 26 + (1 - a) * 10;
    g.globalAlpha = a; icon(nm, x, y, cols[i], 2); g.globalAlpha = 1;
  });
  const reveal = Math.min(1, age / .6);
  g.save(); g.beginPath(); g.rect(0, 0, 240 * reveal, 240); g.clip();
  T(120, 92, "FM1 RACK", { a: "center", size: 30 });
  T(120, 114, "EURORACK MODULES · 1.0", { a: "center", size: 11, c: C.dim });
  g.restore();
  g.fillStyle = rgba("#ffffff", .07); g.fillRect(0, (age * 400) % 240, 240, 2);
}
let bootAt = -1;
function render() {
  g.fillStyle = C.bg; g.fillRect(0, 0, 240, 240);
  if (bootAt >= 0 && performance.now() - bootAt < 1500) { drawBoot((performance.now() - bootAt) / 1000); return; }
  const slot = SLOTS.find((s) => s.id === page);
  if (slot) drawSlot(slot);
  else if (page === "AMP") drawFixed(AMP, amp, "AMP", ["ADSR", "VOICE", "MOD ENV"], "AMP", 6, [visAdsr, visVoices, visModEnv][Math.min(subpage, 2)]);
  else if (page === "LFOS") drawFixed(LFOS, lfo, "LFO", ["LFO 1", "LFO 2", "LFO 3"], "LFO", 8, visLfos);
  else if (page === "SAVE") drawSave();
  else if (page === "GLOBAL") drawFixed(GLOBAL, glo, "GLO", ["SETTINGS", "PLAYING"], "GLOBAL", -1, visGlobal);
  else if (page === "SCALE") drawFixed(SCALE, scl, "SCALE", [scl.p[0] ? "LOCKED" : "OFF"], "SCALE", -1, visScale);
  else if (page === "RACK") drawRack();
  else if (page === "MATRIX") drawMatrix();
  else drawPerform();
  if (learn) {
    rect(0, 26, 240, 15, C.gold); icon("LEARN", 4, 28, "#111");
    T(124, 37, `LEARN M${learnMacro + 1} · TURN ANY KNOB`, { a: "center", size: 10, c: "#111" });
    if (Math.floor(animT * 3) % 2) dot(232, 33, 2.5, C.warn, false);
  }
  if (picker) drawPicker();
  drawToast();
}
