// FM1 Rack firmware-readiness tests. From the repo root, after firmware/desktop/build.sh (and, for exact
// 32-bit memory numbers, engine/build.sh + firmware/desktop/build_wasm32.sh):
//   node firmware/tests/run_tests.mjs
// 1. same engine:        native build of rack.cc = the browser's rack.wasm, every factory preset
// 2. arena is invisible: the FM-1 build with room for everything = the page's FM-1 preview (set_hw)
// 3. FM-1 at 340 KB:     each preset's voices, arena use and unloaded modules, which must match what the
//                        page's preview predicts (page-fit.json, from record_calls.mjs)
// 4. app glue:           hw/fm1_app.cc on desktop/fake_board.cc plays a chord and a MIDI note
import fs from 'fs'; import os from 'os'; import path from 'path'; import { execFileSync } from 'child_process';
const here = new URL('.', import.meta.url).pathname, root = path.resolve(here, '../..');
const bin = (n) => path.join(root, 'firmware/desktop/build', n);
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'fm1-tests-'));
const trace = path.join(here, 'presets.trace'), wasm = path.join(root, 'web/rack.wasm');
const node = (args) => execFileSync(process.execPath, args, { encoding: 'utf8' });
let failed = 0;
const step = (title, ok, detail) => { console.log(`${ok ? 'PASS' : 'FAIL'}  ${title}${detail ? '  ' + detail : ''}`); if (!ok) failed++; };
const tail = (s) => s.trim().split('\n').pop();
// one file per preset, for runs where nothing may carry over between presets
const parts = fs.readFileSync(trace, 'utf8').split(/\n(?=patch )/).filter((p) => p.startsWith('patch '));
const split = parts.map((p, i) => { const f = path.join(tmp, `p${i}.trace`); fs.writeFileSync(f, p.trimEnd() + '\n'); return f; });
const eachPatch = (run) => { const all = {}; split.forEach((f, i) => { const o = path.join(tmp, `r${i}.json`); run(f, o); Object.assign(all, JSON.parse(fs.readFileSync(o, 'utf8'))); }); return all; };

// 1
node([path.join(here, 'replay.mjs'), trace, wasm, path.join(tmp, 'wasm.json')]);
execFileSync(bin('rack_cli'), [trace, '--json', path.join(tmp, 'native.json')]);
let r; try { r = node([path.join(here, 'compare.mjs'), path.join(tmp, 'wasm.json'), path.join(tmp, 'native.json')]); step('1 native engine = browser engine', true, tail(r)); }
catch (e) { step('1 native engine = browser engine', false, tail(e.stdout)); }

// 2
node([path.join(here, 'replay.mjs'), trace, wasm, path.join(tmp, 'wasm_hw.json'), '--hw', '--fresh']);
fs.writeFileSync(path.join(tmp, 'big.json'), JSON.stringify(eachPatch((f, o) => execFileSync(bin('rack_cli_fm1_big'), [f, '--json', o]))));
try { r = node([path.join(here, 'compare.mjs'), path.join(tmp, 'wasm_hw.json'), path.join(tmp, 'big.json')]); step('2 FM-1 build (unlimited arena) = page preview', true, tail(r)); }
catch (e) { step('2 FM-1 build (unlimited arena) = page preview', false, tail(e.stdout)); }

// 3
const w32 = bin('rack_cli_fm1.wasm'), use32 = fs.existsSync(w32);
const run32 = (f, o) => execFileSync(process.execPath, ['--no-warnings', path.join(here, 'run_wasi.mjs'), w32, f, '--json', o], { encoding: 'utf8' });
const fm1 = eachPatch(use32 ? run32 : (f, o) => execFileSync(bin('rack_cli_fm1'), [f, '--json', o]));
const pageFit = fs.existsSync(path.join(here, 'page-fit.json')) ? Object.fromEntries(JSON.parse(fs.readFileSync(path.join(here, 'page-fit.json'), 'utf8')).map((x) => [x.name, x])) : null;
const SL = [[0, 'SRC'], [1, 'SHP'], [4, 'FX1'], [5, 'FX2']];
let mism = 0, bad = 0;
console.log(`\nFM-1 build, ${use32 ? '32-bit (wasm32)' : 'native (64-bit: memory numbers approximate)'}, arena ${Math.round(Object.values(fm1)[0].arena_size / 1024)} KB:`);
for (const [name, x] of Object.entries(fm1)) {
  const dropped = SL.filter(([b]) => x.unloaded >> b & 1).map(([, s]) => s);
  if (x.bad || x.arena > x.arena_size) bad++;
  let note = '';
  if (pageFit && pageFit[name]) { const p = pageFit[name]; const same = p.fit === x.fit && p.dropped.join('+') === dropped.join('+'); if (!same) { mism++; note = `  page says ${p.fit}V${p.dropped.length ? ' ' + p.dropped.join('+') + ' off' : ''}`; } }
  console.log(`  ${name.padEnd(14)} fits ${x.fit} voices  arena ${String(Math.round(x.arena / 1024)).padStart(3)} KB${dropped.length ? `  unloaded ${dropped.join('+')}` : ''}${x.bad ? '  NaN!' : ''}${note}`);
}
step('3 FM-1 build stays inside its arena, no NaN', bad === 0, `${Object.keys(fm1).length} presets`);
if (pageFit) step('3 page preview predicts the FM-1 build', use32 ? mism === 0 : true, use32 ? `${mism} mismatches` : 'skipped: needs the 32-bit build');
// 4
try { const o = execFileSync(bin('fake_board'), [path.join(tmp, 'fake_board.wav')], { encoding: 'utf8' }); step('4 app glue plays on a pretend FM-1 (hw/fm1_app.cc)', true, o.trim().split('\n')[0].replace(/^.*?: /, '')); }
catch (e) { step('4 app glue plays on a pretend FM-1 (hw/fm1_app.cc)', false, String(e.stdout || e.message).trim()); }
fs.rmSync(tmp, { recursive: true, force: true });
console.log(failed ? `\n${failed} failed` : '\nall passed');
process.exit(failed ? 1 : 0);
