// FM1 Rack: runs the rack engine (rack.wasm) in the audio thread.
class FM1Rack extends AudioWorkletProcessor {
  constructor() {
    super();
    this.x = null; this.queue = []; this.timed = []; this.count = 0; this.watch = -1;
    this.busy = 0; this.span = 0; this.peak = 0; this.late = 0;
    this.port.onmessage = (e) => this.onMessage(e.data);
  }
  async onMessage(m) {
    if (m.type === "load") {
      try {
        const { instance } = await WebAssembly.instantiate(m.wasm, {});
        this.x = instance.exports;
        if (this.x._initialize) this.x._initialize();
        this.x.init();
        for (const q of this.queue) this.apply(q);
        this.queue = [];
        this.port.postMessage({ type: "ready" });
      } catch (err) { this.port.postMessage({ type: "error", message: String(err) }); }
      return;
    }
    if (!this.x) { this.queue.push(m); return; }
    this.apply(m);
  }
  apply(m) {
    if (m.type === "watch") { this.watch = m.slot; return; }
    // calls stamped with an AudioContext time, for the sequencer and strummed chords
    if (m.type === "timed") { this.timed.push(m); this.timed.sort((a, b) => a.at - b.at); return; }
    if (m.type === "cancel") { this.timed = this.timed.filter((t) => t.tag !== m.tag); return; }
    if (m.type !== "calls") return;
    for (const [fn, ...args] of m.calls) if (typeof this.x[fn] === "function") this.x[fn](...args);
  }
  process(_inputs, outputs) {
    const out = outputs[0];
    if (!this.x || !out || !out[0]) return true;
    const n = out[0].length;
    const due = currentTime + n / sampleRate;
    while (this.timed.length && this.timed[0].at < due) this.apply({ type: "calls", calls: this.timed.shift().calls });
    const clk = globalThis.performance ? () => performance.now() : () => Date.now();
    const t0 = clk();
    this.x.render(n);
    const used = clk() - t0, budget = n / sampleRate * 1000;
    this.busy += used; this.span += budget; this.peak = Math.max(this.peak, used / budget * 100);
    if (used > budget) this.late++;
    const mem = this.x.memory.buffer;
    out[0].set(new Float32Array(mem, this.x.out_l(), n));
    if (out[1]) out[1].set(new Float32Array(mem, this.x.out_r(), n));
    if (++this.count % 4 === 0) {
      const mon = new Float32Array(this.x.memory.buffer, this.x.monitor(this.watch), 200).slice();
      let cpu = null;
      if (this.span > 500) { cpu = [this.busy / this.span * 100, this.peak, this.late]; this.busy = 0; this.span = 0; this.peak = 0; }
      this.port.postMessage({ type: "mon", scope: new Float32Array(this.x.memory.buffer, this.x.scope(), 128).slice(), mon, cpu });
    }
    return true;
  }
}
registerProcessor("fm1-rack", FM1Rack);
