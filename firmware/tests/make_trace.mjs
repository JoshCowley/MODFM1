// Turns presets-calls.json (what the 1.9 page sends the engine for each factory preset, recorded by
// record_calls.mjs) into presets.trace: each preset from a clean start, a chord (or, in MONO/LEGATO, a
// two-note line) held for 2.5 s, then 2 s of release. rack_cli and replay.mjs play the same file.
import fs from 'fs';
const here = new URL('.', import.meta.url).pathname;
const presets = JSON.parse(fs.readFileSync(here + 'presets-calls.json', 'utf8'));
const out = ['# FM1 Rack factory presets: engine calls with frame times (48 kHz). Made by make_trace.mjs.'];
for (const p of presets) {
  out.push(`patch ${p.name}`, '0 init');
  for (let s = 0; s < 6; s++) out.push(`0 set_module ${s} 0`);
  for (const c of p.calls) out.push(`0 ${c.join(' ')}`);
  out.push(`0 set_voices ${p.voices}`);
  const mono = p.mode === 1 || p.mode === 2;
  if (mono) out.push('0 note_on 45 0.8', '48000 note_on 52 0.8', '48000 note_off 45', '120000 note_off 52');
  else out.push('0 note_on 57 0.8', '0 note_on 60 0.8', '0 note_on 64 0.8', '120000 note_off 57', '120000 note_off 60', '120000 note_off 64');
  out.push('end 216000');
}
fs.writeFileSync(here + 'presets.trace', out.join('\n') + '\n');
console.log(`${presets.length} presets, ${out.length} lines`);
