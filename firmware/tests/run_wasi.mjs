// Runs a WASI command module (build/rack_cli_fm1.wasm) with Node: node run_wasi.mjs <module.wasm> <args...>
import { WASI } from 'node:wasi'; import fs from 'fs';
process.removeAllListeners('warning');
const [wasm, ...args] = process.argv.slice(2);
const wasi = new WASI({ version: 'preview1', args: ['rack_cli', ...args], preopens: { '/': '/' } });
const { instance } = await WebAssembly.instantiate(fs.readFileSync(wasm), { wasi_snapshot_preview1: wasi.wasiImport });
wasi.start(instance);
