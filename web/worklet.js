// FM1 Rack: runs the rack engine (rack.wasm) in the audio thread.
class FM1Rack extends AudioWorkletProcessor {
  constructor() {
    super();
    this.x = null; this.queue = []; this.count = 0; this.watch = -1;
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
    if (m.type !== "calls") return;
    for (const [fn, ...args] of m.calls) if (typeof this.x[fn] === "function") this.x[fn](...args);
  }
  process(_inputs, outputs) {
    const out = outputs[0];
    if (!this.x || !out || !out[0]) return true;
    const n = out[0].length;
    this.x.render(n);
    const mem = this.x.memory.buffer;
    out[0].set(new Float32Array(mem, this.x.out_l(), n));
    if (out[1]) out[1].set(new Float32Array(mem, this.x.out_r(), n));
    if (++this.count % 4 === 0) {
      const mon = new Float32Array(this.x.memory.buffer, this.x.monitor(this.watch), 160).slice();
      this.port.postMessage({ type: "mon", scope: new Float32Array(this.x.memory.buffer, this.x.scope(), 128).slice(), mon });
    }
    return true;
  }
}
registerProcessor("fm1-rack", FM1Rack);
