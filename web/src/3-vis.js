// ================================================================ 3. module visualisers (the moving band on each slot page)
const VX = 6, VY = 30, VW = 228, VH = 62;
const pulses = [];                 // note-ons seen by the UI: { t, note, vel }
const HIST = 114;
const modHist = [[], [], [], []], lfoHist = [[], [], []];
const vs = {};                     // per-visualiser animation state
const envMax = () => Math.max(0, ...Array.from(mon.subarray(16, 22)));
const TAU = Math.PI * 2;
function onNote(note, vel) { pulses.push({ t: animT, note, vel }); if (pulses.length > 24) pulses.shift(); }
function recentPulses(since) { return pulses.filter((p) => animT - p.t < since); }
function pushHist() {
  for (let i = 0; i < 4; i++) { modHist[i].push(mon[40 + i]); if (modHist[i].length > HIST) modHist[i].shift(); }
  for (let i = 0; i < 3; i++) { lfoHist[i].push(mon[44 + i]); if (lfoHist[i].length > HIST) lfoHist[i].shift(); }
}
function label(x, y, s, c) { T(x, y, s, { size: 9, c: c || C.dim }); }
function trace(hist, X, Y, W, H, c, bip = true, scale = 1) {
  const pts = hist.map((v, i) => [X + W - (hist.length - 1 - i) * (W / (HIST - 1)), Y + H / 2 - (bip ? v : v * 2 - 1) * scale * (H / 2 - 4)]);
  glow(pts, c);
  if (pts.length) dot(...pts[pts.length - 1], 2.2, c);
}
function centreLine(X, Y, W, H) { g.fillStyle = "#1d1d1b"; g.fillRect(X + 4, Y + H / 2, W - 8, 1); }

const VIS = {
  PLAITS(X, Y, W, H, P, c) {
    const model = Math.round(P(0)), har = P(1), tim = P(2), mor = P(3);
    const e = envMax(), amp = 0.22 + 0.78 * e, cy = Y + H / 2, t = animT;
    centreLine(X, Y, W, H);
    if (model === 17 || model === 18 || model === 16) {          // noise, particles, swarm: a field of dots
      const st = vs.plaitsDots || (vs.plaitsDots = Array.from({ length: 60 }, () => ({ x: Math.random(), y: Math.random(), s: Math.random() })));
      for (const d of st) {
        d.x = (d.x + (0.002 + d.s * 0.01) * (1 + tim * 3)) % 1;
        const yy = cy + Math.sin(d.y * TAU + t * (1 + har * 4) + d.s * 9) * (H / 2 - 6) * amp;
        g.fillStyle = rgba(c, .25 + .75 * d.s * amp); g.fillRect(X + d.x * W, yy, 1.5 + mor * 2, 1.5 + mor * 2);
      }
    } else if (model >= 21) {                                       // drums: bursts on each note
      const ps = recentPulses(1.2);
      for (const p of ps) {
        const age = animT - p.t, k = Math.exp(-age * (3 + (1 - P(5)) * 6));
        const pts = [];
        for (let x = 0; x <= W; x += 2) { const u = x / W; pts.push([X + x, cy - Math.sin(u * TAU * (2 + har * 6) * (1 + (1 - u) * mor * 3)) * Math.exp(-u * 3) * k * (H / 2 - 4) * (p.vel || .8)]); }
        glow(pts, c);
      }
      if (!ps.length) glow([[X + 4, cy], [X + W - 4, cy]], rgba(c, .4));
    } else {
      const cycles = 1.5 + har * 3.5, pts = [];
      for (let x = 0; x <= W; x += 2) {
        const ph = (x / W) * cycles * TAU - t * 3;
        let y = Math.sin(ph) + har * .55 * Math.sin(2 * ph + t) + har * har * .35 * Math.sin(3 * ph - t * .5);
        y = Math.sin(y * (1 + tim * 2.6)) / Math.sin(Math.min(1.57, 1 + tim * 2.6));
        const saw = ((ph / TAU) % 1 + 1) % 1 * 2 - 1;
        y = y * (1 - mor * .55) + saw * mor * .55;
        pts.push([X + x, cy - y * amp * (H / 2 - 5)]);
      }
      glow(pts, c);
    }
    label(X + 6, Y + 11, PLAITS_MODELS[model] || "");
    if (Math.round(P(4)) === 1) { label(X + W - 30, Y + 11, "LPG"); dot(X + W - 8, Y + 8, 2, e > .05 ? c : C.faint, e > .05); }
  },
  BRAIDS(X, Y, W, H, P, c) {
    const tim = P(1), col = P(2), drift = P(3);
    const bits = [2, 3, 4, 6, 8, 12, 16][Math.round(P(4))] || 16, step = [12, 6, 3, 2, 1, 1][Math.round(P(5))] || 1;
    const e = envMax(), amp = .25 + .75 * e, cy = Y + H / 2, t = animT, levels = 2 ** Math.min(bits, 6);
    centreLine(X, Y, W, H);
    const tints = [c, "#ffd9a0", "#ff8f5a"];
    for (let s = 0; s < 3; s++) {
      const pts = []; let held = 0;
      for (let x = 0; x <= W; x += 2) {
        if ((x / 2) % step === 0) {
          const ph = (x / W) * TAU * (1.5 + tim * 2) - t * (2.2 + s * drift * 2) + s * (0.6 + col * 1.5);
          held = Math.sin(ph) * (0.45 + 0.55 * tim) * Math.cos(ph * 0.5 + s);
          if (bits <= 6) held = Math.round(held * levels / 2) / (levels / 2);
        }
        pts.push([X + x, cy - held * amp * (H / 2 - 5)]);
      }
      glow(pts, tints[s], s ? 1 : 1.4);
    }
    label(X + 6, Y + 11, `${BRAIDS_MODELS[Math.round(P(0))]}  ${bits}BIT`);
  },
  ELEMENTS(X, Y, W, H, P, c) {
    const bow = P(0), blow = P(1), strike = P(2), geo = P(4), bright = P(5), damp = P(6), pos = P(7);
    const e = envMax(), cy = Y + H / 2 + 4, x0 = X + 14, x1 = X + W - 10, Ls = x1 - x0, t = animT;
    const mode = 1 + Math.round(geo * 4), w = 7 + bright * 9;
    const pts = [];
    for (let x = 0; x <= Ls; x += 2) {
      const u = x / Ls;
      const y = Math.sin(Math.PI * mode * u) * Math.sin(t * w) * (0.08 + 0.92 * e) * (H / 2 - 8) + Math.sin(Math.PI * (mode + 1) * u) * Math.sin(t * w * 1.7) * (1 - damp) * e * 4;
      pts.push([x0 + x, cy - y]);
    }
    rect(x0 - 2, cy - 6, 2, 12, C.dim); rect(x1, cy - 6, 2, 12, C.dim);
    glow(pts, c);
    const bx = x0 + pos * Ls;
    if (bow > .05) { const by = Math.sin(t * 2.5) * 8 * bow; rect(bx - 1, cy - 20 + by, 3, 18, rgba("#f2efe6", .3 + .7 * bow), 1); }
    for (const p of recentPulses(0.6)) { const a = animT - p.t; if (strike > .05) dot(bx + 10, cy - 18 + Math.min(a * 120, 14) - Math.max(0, a - .12) * 60, 3, rgba(C.gold, (1 - a / .6) * strike)); }
    if (blow > .05) for (let i = 0; i < 6; i++) { const ph = (t * .6 + i / 6) % 1; dot(X + 2 + ph * 12, cy - 12 + Math.sin(i * 3 + t) * 4, 1, rgba("#9fd8ff", blow * (1 - ph)), false); }
    label(X + 6, Y + 11, "BOW", bow > .05 ? c : C.faint); label(X + 30, Y + 11, "BLOW", blow > .05 ? c : C.faint); label(X + 60, Y + 11, "STRIKE", strike > .05 ? c : C.faint);
  },
  RINGS(X, Y, W, H, P, c) {
    const model = Math.round(P(0)), struct = P(1), bright = P(2), damp = P(3), pos = P(4);
    const st = vs.rings || (vs.rings = { ripples: [], seen: 0 });
    for (const p of pulses) if (p.t > st.seen) { st.ripples.push({ t0: p.t, x: X + W / 2 + (pos - .5) * W * .5 + (p.note % 7 - 3) * 6, v: p.vel || .8 }); st.seen = p.t; }
    const life = 0.6 + damp * 4, rings = 1 + Math.round(struct * 4), sp = 4 + struct * 6;
    st.ripples = st.ripples.filter((r) => animT - r.t0 < life);
    clipTo(X, Y, W, H, () => {
      g.strokeStyle = rgba(c, .1); g.lineWidth = 1;
      for (let k = 1; k <= rings + 1; k++) { g.beginPath(); g.ellipse(X + W / 2 + (pos - .5) * W * .5, Y + H / 2, k * (sp + 4), k * (sp + 4) * .42, 0, 0, TAU); g.stroke(); }
      for (const r of st.ripples) {
        const age = animT - r.t0, a = (1 - age / life) * r.v;
        for (let k = 0; k < rings; k++) {
          const rad = age * (24 + bright * 40) - k * sp;
          if (rad <= 0) continue;
          g.strokeStyle = rgba(k ? "#bff5ee" : c, a * (1 - k / (rings + 1))); g.lineWidth = 1.2;
          g.beginPath(); g.ellipse(r.x, Y + H / 2, rad, rad * .42, 0, 0, TAU); g.stroke();
        }
      }
      const e = envMax();
      if (e > .03) { g.strokeStyle = rgba(c, e * .5); g.beginPath(); g.ellipse(X + W / 2 + (pos - .5) * W * .5, Y + H / 2, 6 + Math.sin(animT * 20) * 1.5, 3, 0, 0, TAU); g.stroke(); }
    });
    label(X + 6, Y + 11, ["MODAL", "SYMPATHETIC", "STRING", "FM VOICE", "SYMP QUANT", "STRING+VERB"][model] || "");
  },
  WARPS(X, Y, W, H, P, c) {
    const algo = P(0), tim = P(1), carrier = Math.round(P(2)), modIn = Math.round(P(3));
    const e = .3 + .7 * envMax(), t = animT, cy = Y + H / 2;
    const car = (ph) => carrier === 0 ? Math.sin(ph) : carrier === 1 ? (2 / Math.PI) * Math.asin(Math.sin(ph)) : (((ph / TAU) % 1 + 1) % 1) * 2 - 1;
    const pc = [], pm = [], pr = [];
    for (let x = 0; x <= W; x += 2) {
      const u = x / W, a = car(u * TAU * 4 - t * 4), m = modIn === 2 ? (Math.sin(u * 91 + t * 37) * Math.sin(u * 53 - t * 23)) : Math.sin(u * TAU * (1.5 + tim * 2) - t * 1.3);
      let r;
      if (algo < .25) r = a * (1 - algo * 4) + m * algo * 4;
      else if (algo < .5) r = Math.sin((a + m) * (1 + (algo - .25) * 12));
      else if (algo < .75) r = a * m;
      else r = a * Math.abs(m) * (1 + tim);
      pc.push([X + x, Y + 12 - a * 5]); pm.push([X + x, Y + H - 10 - m * 5]); pr.push([X + x, cy - Math.max(-1, Math.min(1, r)) * e * (H / 2 - 16)]);
    }
    glow(pc, "#3f6f6a", .8); glow(pm, "#3f6f6a", .8); glow(pr, c);
    label(X + W - 4 - textW(["XFADE", "FOLD", "RING MOD", "VOCODE"][Math.min(3, Math.floor(algo * 4))], 9), Y + 11, ["XFADE", "FOLD", "RING MOD", "VOCODE"][Math.min(3, Math.floor(algo * 4))]);
    label(X + 6, Y + 11, ["OSC", "USB IN", "NOISE"][modIn] + " IN");
  },
  FILTER(X, Y, W, H, P, c) {
    const type = Math.round(P(0)), cut = P(1), reso = P(2), drive = P(3), envAmt = P(4);
    const fc = Math.min(20000, Math.max(20, 440 * 2 ** ((cut * 120 - 30 + envAmt * envMax() * 60 - 9) / 12))), Q = .5 + reso * reso * 25;
    const fx = (x) => 20 * 1000 ** (x / W);
    const mag = (f) => { const r = f / fc, d = Math.sqrt((1 - r * r) ** 2 + (r / Q) ** 2); return type === 0 ? 1 / d : type === 1 ? (r / Q) / d * Math.min(Q, 3) : (r * r) / d; };
    const yOf = (m) => { const db = Math.max(-36, Math.min(18, 20 * Math.log10(m + 1e-6))); return Y + 14 + (18 - db) / 54 * (H - 18); };
    // drifting spectrum under the curve
    const st = vs.filt || (vs.filt = Array.from({ length: 28 }, () => Math.random()));
    const lvl = .3 + .7 * Math.min(1, (mon[48] || 0) * 2 + envMax() * .5);
    for (let i = 0; i < st.length; i++) {
      st[i] += (Math.random() - st[i]) * .12;
      const x = X + 4 + i * (W - 8) / st.length, m = Math.min(1.5, mag(fx(x - X)));
      const h = st[i] * m * lvl * (H - 20) * .7;
      rect(x, Y + H - 4 - h, (W - 8) / st.length - 2, h, rgba(c, .18), 1);
    }
    const pts = []; for (let x = 0; x <= W; x += 2) pts.push([X + x, yOf(mag(fx(x)))]);
    g.fillStyle = rgba(c, .08); g.beginPath(); g.moveTo(X, Y + H); pts.forEach(([x, y]) => g.lineTo(x, y)); g.lineTo(X + W, Y + H); g.fill();
    glow(pts, c, 1.4 + drive);
    const cx = X + Math.log(fc / 20) / Math.log(1000) * W;
    if (cx > X && cx < X + W) { g.fillStyle = rgba(C.gold, .5); for (let y = Y + 4; y < Y + H - 2; y += 4) g.fillRect(cx, y, 1, 2); }
    label(X + 6, Y + 11, `${["LP", "BP", "HP"][type]} ${fc >= 1000 ? (fc / 1000).toFixed(1) + "K" : Math.round(fc)}HZ`);
  },
  PEAKS(X, Y, W, H, P, c, k) {
    const func = Math.round(P(0));
    centreLine(X, Y, W, H);
    trace(modHist[k * 2], X, Y + 6, W, H - 8, c, func !== 0, func === 0 ? .9 : .9);
    label(X + 6, Y + 11, ["ENVELOPE", "LFO", "TAP LFO"][func] + ` · ${GATES[Math.round(P(3))]}`);
  },
  TIDES(X, Y, W, H, P, c, k) {
    const mode = Math.round(P(0)), shape = P(2), slope = P(3);
    centreLine(X, Y, W, H);
    // one cycle of the slope shape, left
    const sx = X + 6, sw = 40, sy = Y + 16, sh = H - 22, rise = .5 + slope * .45, pts = [];
    for (let i = 0; i <= 20; i++) { const u = i / 20; let v = u < rise ? u / rise : 1 - (u - rise) / (1 - rise); v = v ** (0.4 + shape * 2.2); pts.push([sx + u * sw, sy + sh - v * sh]); }
    glow(pts, rgba(c, .6), 1);
    trace(modHist[k * 2], X + 52, Y + 6, W - 56, H - 8, c, true);
    label(X + 6, Y + 11, ["AD", "LOOP", "AR"][mode]);
  },
  MARBLES(X, Y, W, H, P, c, k) {
    const st = vs["marb" + k] || (vs["marb" + k] = { balls: [], prev: 0 });
    const tv = mon[40 + k * 2 + 1], xv = mon[40 + k * 2];
    if (tv > .95 && st.prev <= .95) st.balls.push({ x: X + W - 8, y: Y + H - 8 - Math.max(0, Math.min(2, xv)) / 2 * (H - 18), r: 2.5 + P(1) * 2.5, hue: Math.random() });
    st.prev = tv;
    const speed = 22 + P(0) * 40;
    for (const bl of st.balls) bl.x -= speed / 30;
    st.balls = st.balls.filter((bl) => bl.x > X - 6);
    clipTo(X, Y, W, H, () => {
      const pts = st.balls.map((bl) => [bl.x, bl.y]);
      if (pts.length > 1) glow(pts, rgba(c, .35), .8);
      for (const bl of st.balls) { dot(bl.x, bl.y, bl.r, bl.hue > .5 ? c : "#e3d4ff"); g.fillStyle = "rgba(255,255,255,.7)"; g.fillRect(bl.x - bl.r * .4, bl.y - bl.r * .5, 1, 1); }
    });
    if (P(4) > .55) { const lx = X + W - 8 - (Math.round(P(5)) + 1) * 14; g.fillStyle = rgba(C.gold, .5); g.fillRect(lx, Y + H - 4, X + W - 8 - lx, 1); label(lx, Y + H - 7, "DEJA VU", C.gold); }
    label(X + 6, Y + 11, `X ${xv.toFixed(2)}V`);
  },
  GRIDS(X, Y, W, H, P, c, k) {
    const fill = P(2), out = Math.round(P(3)), step = Math.floor(mon[47]) & 31;
    const live = watchSlot === SLOTS.findIndex((s) => s.id === (k ? "MODB" : "MODA"));
    const cw = (W - 26) / 32, rows = ["BD", "SD", "HH"];
    for (let r = 0; r < 3; r++) {
      const y = Y + 14 + r * 16;
      label(X + 4, y + 4, rows[r], out === 3 || out === r ? c : C.faint);
      for (let s = 0; s < 32; s++) {
        const lv = live ? mon[64 + r * 32 + s] : 0, x = X + 22 + s * cw + cw / 2;
        const hit = lv > 1 - fill && (out === 3 || out === r);
        const now = s === step;
        if (hit) dot(x, y, now ? 3.2 : 1.2 + lv * 1.5, now ? "#ffffff" : c, now);
        else { g.fillStyle = now ? C.dim : "#2a2a27"; g.fillRect(x - .5, y - .5, 1 + lv * 1.5, 1 + lv * 1.5); }
      }
    }
    g.fillStyle = rgba("#ffffff", .08); g.fillRect(X + 22 + step * cw, Y + 6, cw, H - 10);
  },
  CLOUDS(X, Y, W, H, P, c) {
    const pos = P(0), size = P(1), pitch = P(2), dens = P(3), tex = P(4), blend = P(5), spread = P(6), freeze = Math.round(P(10)) === 1;
    const st = vs.clouds || (vs.clouds = { grains: [], acc: 0 });
    const col = freeze ? "#d8f3ff" : c, rate = freeze ? 0 : (4 + dens * 60) * (.45 + envMax() * .55 + (mon[48] || 0));
    st.acc += rate / 30;
    while (st.acc > 1) { st.acc--; st.grains.push({ t0: animT, x: X + 10 + (pos * .8 + (Math.random() - .5) * spread * .4) * (W - 30), y: Y + 12 + Math.random() * (H - 20), len: 4 + size * 26, life: .3 + size * 1.3, vy: -pitch * 14 }); }
    st.grains = st.grains.filter((gr) => freeze || animT - gr.t0 < gr.life).slice(-80);
    clipTo(X, Y, W, H, () => {
      for (const gr of st.grains) {
        const age = freeze ? gr.life / 2 : animT - gr.t0, env = Math.sin(Math.PI * Math.min(1, age / gr.life));
        const y = gr.y + gr.vy * age, a = env * (.25 + tex * .75) * (.4 + blend * .6);
        g.fillStyle = rgba(col, a); g.beginPath(); g.roundRect(gr.x, y - 1.5, gr.len * (.4 + env * .6), 3, 1.5); g.fill();
      }
    });
    label(X + 6, Y + 11, ["GRANULAR", "STRETCH", "LOOP DELAY", "SPECTRAL"][Math.round(P(9))] + (freeze ? " · FROZEN" : ""));
  },
  DELAY(X, Y, W, H, P, c) {
    const time = [.25, .5, .75, 1, 2][Math.round(P(0))], fb = P(1), tone = P(2), mix = P(3);
    const period = time * 60 / glo.p[G_TEMPO], spacing = Math.min(70, 14 + time * 34), fade = period * 1.2 + .2;
    for (let i = 0; i < 9; i++) { const x = X + 10 + i * spacing; if (x > X + W - 6) break; const h = (H - 18) * fb ** i; rect(x, Y + H - 4 - h, 3, h, rgba(c, .14), 1); }
    // a beat marker sweeping across the taps
    const sweep = (animT / period) % 1; rect(X + 10 + sweep * spacing, Y + H - 3, 3, 2, rgba(c, .6));
    for (const p of recentPulses(10)) {
      const age = animT - p.t;
      for (let i = 0; i < 9; i++) {
        const at = age - i * period; if (at < 0 || at > fade) continue;
        const x = X + 10 + i * spacing; if (x > X + W - 6) continue;
        const a = 1 - at / fade, h = (H - 18) * fb ** i;
        rect(x - 1, Y + H - 4 - h, 5, h, rgba(c, (.35 + tone * .65) * a), 2);
        dot(x + 1.5, i % 2 ? Y + 16 : Y + 22, 2, rgba(i % 2 ? "#a0c4ff" : c, a));
      }
    }
    label(X + 6, Y + 11, `${["1/16", "1/8", "1/8D", "1/4", "1/2"][Math.round(P(0))]} · MIX ${Math.round(mix * 100)}`);
  },
  REVERB(X, Y, W, H, P, c) {
    const size = P(0), damp = P(1), diff = P(2), mix = P(3);
    const st = vs.verb || (vs.verb = { d: [] });
    const spawn = (6 + mix * 30) * (.25 + envMax() + (mon[48] || 0));
    for (let i = 0; i < spawn / 6; i++) { const a = Math.random() * TAU, sp = 10 + size * 40; st.d.push({ x: X + W / 2, y: Y + H / 2, vx: Math.cos(a) * sp, vy: Math.sin(a) * sp * .5, t0: animT }); }
    const life = .8 + size * 3;
    st.d = st.d.filter((d) => animT - d.t0 < life).slice(-220);
    frame(X + 4, Y + 4, W - 8, H - 8, "#1f2a3a", 4);
    for (const d of st.d) {
      d.x += d.vx / 30; d.y += d.vy / 30;
      if (d.x < X + 6 || d.x > X + W - 6) { d.vx *= -1; d.vx *= (1 - damp * .3); }
      if (d.y < Y + 6 || d.y > Y + H - 6) { d.vy *= -1; d.vy *= (1 - damp * .3); }
      const a = 1 - (animT - d.t0) / life;
      g.fillStyle = rgba(damp > .6 ? "#a6b8d6" : c, a * (.4 + diff * .6)); g.fillRect(d.x, d.y, 1.5, 1.5);
    }
    label(X + 10, Y + 15, `SIZE ${Math.round(size * 100)}`);
  },
  CHORUS(X, Y, W, H, P, c) {
    const rate = P(0), depth = P(1), width = P(2), mix = P(3), t = animT, cy = Y + H / 2;
    const cols = [c, "#a0c4ff", "#6affd8"];
    for (let v = 0; v < 3; v++) {
      const pts = [], off = Math.sin(t * (.6 + rate * 4) + v * 2.1) * depth * 1.6, side = (v - 1) * width * 6;
      for (let x = 0; x <= W; x += 2) pts.push([X + x, cy + side - Math.sin(x / W * TAU * 3 + off - t) * (H / 2 - 12) * (v ? .4 + mix * .6 : 1)]);
      glow(pts, cols[v], v ? .8 : 1.2);
    }
    label(X + 6, Y + 11, `RATE ${Math.round(rate * 100)} · WIDTH ${Math.round(width * 100)}`);
  },
};
// bands for the fixed pages
function visAdsr(X, Y, W, H, c) {
  const [a, d, s, r] = [0, 1, 2, 3].map((i) => amp.p[i]);
  const wa = 6 + a * 50, wd = 6 + d * 50, ws = 34, wr = 6 + r * 50, sc = (W - 16) / (wa + wd + ws + wr);
  const x0 = X + 8, yb = Y + H - 6, top = Y + 14;
  const P = [[x0, yb], [x0 + wa * sc, top], [x0 + (wa + wd) * sc, yb - (yb - top) * s], [x0 + (wa + wd + ws) * sc, yb - (yb - top) * s], [x0 + (wa + wd + ws + wr) * sc, yb]];
  g.fillStyle = rgba(c, .1); g.beginPath(); g.moveTo(...P[0]); P.forEach((p) => g.lineTo(...p)); g.fill();
  glow(P, c);
  // each voice as a dot riding the envelope
  for (let i = 0; i < MAXV; i++) {
    const gt = mon[28 + i], e = mon[16 + i]; if (gt <= 0 || e < .005) continue;
    const st = vs["v" + i] || (vs["v" + i] = { prev: 0 });
    let x;
    if (gt === 1 && e >= st.prev && e < .999) x = P[0][0] + (P[1][0] - P[0][0]) * e;
    else if (gt === 1) x = P[1][0] + (P[2][0] - P[1][0]) * Math.min(1, (1 - e) / Math.max(.001, 1 - s));
    else x = P[3][0] + (P[4][0] - P[3][0]) * (1 - e / Math.max(.001, s));
    st.prev = e;
    dot(x, yb - (yb - top) * e, 2.2, i === mon[62] ? "#fff" : c);
  }
  label(X + 6, Y + 11, "A D S R");
}
function visModEnv(X, Y, W, H, c) {
  const a = amp.p[8], d = amp.p[9], loop = amp.p[10] === 1;
  const wa = 6 + a * 70, wd = 6 + d * 70, sc = (W - 40) / (wa + wd), x0 = X + 8, yb = Y + H - 6, top = Y + 14;
  const P = [[x0, yb], [x0 + wa * sc, top], [x0 + (wa + wd) * sc, yb]];
  glow(P, c);
  if (loop) { g.strokeStyle = rgba(c, .5); g.beginPath(); g.arc(P[2][0] + 12, yb - 12, 8, Math.PI * .2, Math.PI * 1.7); g.stroke(); }
  for (let i = 0; i < MAXV; i++) { const e = mon[22 + i]; if (mon[28 + i] <= 0 || e < .01) continue; dot(X + W - 14, yb - (yb - top) * e, 2, c); }
  label(X + 6, Y + 11, loop ? "AD · LOOP" : "AD");
}
function visVoices(X, Y, W, H, c) {
  const nv = budget().voices;
  for (let i = 0; i < MAXV; i++) {
    const x = X + 8 + i * 37, on = i < nv, e = mon[16 + i];
    rect(x, Y + 8, 28, H - 16, on ? "#151514" : "#0c0c0b", 4);
    if (on) { const h = (H - 20) * e; rect(x + 2, Y + H - 10 - h, 24, h, rgba(c, .35 + .65 * e), 3); }
    if (on && e > .01) T(x + 14, Y + 20, noteNameMidi(mon[34 + i]), { size: 9, a: "center" });
    T(x + 14, Y + H - 12, String(i + 1), { size: 8, a: "center", c: on ? C.dim : C.faint });
  }
}
function visLfos(X, Y, W, H) {
  for (let l = 0; l < 3; l++) {
    const y = Y + 2 + l * 20, sel = l === subpage, c = sel ? KC.lfo : "#5a4f73";
    const shape = lfo.p[l * 4];
    // one cycle of the shape as a faint guide
    const pts = []; for (let i = 0; i <= 24; i++) { const u = i / 24; let v; switch (shape) { case 0: v = Math.sin(u * TAU); break; case 1: v = u < .5 ? u * 4 - 1 : 3 - u * 4; break; case 2: v = 1 - u * 2; break; case 3: v = u < .5 ? 1 : -1; break; default: v = Math.sin(u * 17) * Math.cos(u * 5); } pts.push([X + 22 + u * 30, y + 9 - v * 7]); }
    glow(pts, rgba(c, .5), .8);
    T(X + 6, y + 13, String(l + 1), { size: 10, c });
    const h = lfoHist[l].slice(-60);
    const tp = h.map((v, i) => [X + 60 + i * (W - 66) / 59, y + 9 - v * 7]);
    glow(tp, c, sel ? 1.3 : .8);
    if (tp.length) dot(...tp[tp.length - 1], sel ? 2.4 : 1.6, c);
  }
}
function visGlobal(X, Y, W, H, c) {
  const beat = Math.floor(mon[47] / 4) % 4, frac = (mon[47] / 4) % 1;
  for (let i = 0; i < 4; i++) { const on = i === beat; rect(X + 10 + i * 26, Y + 22, 20, 20, on ? rgba(i ? c : C.gold, .25 + .75 * (1 - frac)) : "#1a1a18", 3); }
  T(X + 128, Y + 40, String(glo.p[G_TEMPO]), { size: 22 });
  T(X + 176, Y + 40, midiState.clockLock ? "BPM EXT" : "BPM", { size: 10, c: C.dim });
  const ma = midiState.last && performance.now() - midiState.last < 300;
  icon("MIDI", X + W - 18, Y + 6, ma ? C.ok : midiState.inputs ? C.dim : C.faint);
  label(X + 6, Y + 11, midiState.inputs ? `MIDI · ${midiState.inputs} IN` : midiState.denied ? "MIDI BLOCKED HERE" : "NO MIDI INPUT");
}
function visScale(X, Y, W, H, c) {
  const deg = SCALES[Object.keys(SCALES)[scl.p[2]]], root = scl.p[1], on = scl.p[0] === 1;
  const whites = [0, 2, 4, 5, 7, 9, 11], blacks = [1, 3, 6, 8, 10], kw = (W - 20) / 7, x0 = X + 10, y0 = Y + 8;
  const inScale = (pc) => deg.includes((pc - root + 12) % 12);
  const lastPulse = pulses[pulses.length - 1], lastPc = lastPulse && animT - lastPulse.t < .5 ? lastPulse.note % 12 : -1;
  whites.forEach((pc, i) => {
    const lit = inScale(pc);
    rect(x0 + i * kw + 1, y0, kw - 2, H - 14, lit ? rgba(pc === root ? C.gold : c, on ? .85 : .35) : "#1d1d1b", 3);
    if (pc === lastPc) frame(x0 + i * kw + 1, y0, kw - 2, H - 14, "#fff", 3);
    T(x0 + i * kw + kw / 2, y0 + H - 18, KEYS12[pc], { size: 9, a: "center", c: lit ? "#111" : C.dim });
  });
  blacks.forEach((pc) => {
    const i = [0, 1, 3, 4, 5][blacks.indexOf(pc)], x = x0 + (i + 1) * kw - kw * .3, lit = inScale(pc);
    rect(x, y0, kw * .6, (H - 14) * .55, lit ? rgba(pc === root ? C.gold : c, on ? 1 : .45) : "#0a0a09", 2);
    if (pc === lastPc) frame(x, y0, kw * .6, (H - 14) * .55, "#fff", 2);
  });
}
