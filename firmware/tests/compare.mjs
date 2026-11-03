// Compares two reports (rack_cli --json or replay.mjs): level and loudness envelope per patch.
//   node compare.mjs <a.json> <b.json> [--tol 0.02]
// "env diff" is the largest difference between the two 100 ms loudness envelopes, relative to the louder
// one's peak. Exit code 1 if any patch is over the tolerance or produced NaN.
import fs from 'fs';
const [a, b] = process.argv.slice(2, 4).map((f) => JSON.parse(fs.readFileSync(f, 'utf8')));
const ti = process.argv.indexOf('--tol'), tol = ti > 0 ? +process.argv[ti + 1] : 0.02;
let fail = 0, worst = 0;
for (const name of Object.keys(a)) {
  const x = a[name], y = b[name];
  if (!y) { console.log(`${name}: missing`); fail++; continue; }
  const top = Math.max(...x.env, ...y.env, 1e-6);
  let d = 0; for (let i = 0; i < Math.min(x.env.length, y.env.length); i++) d = Math.max(d, Math.abs(x.env[i] - y.env[i]) / top);
  const db = 20 * Math.log10((y.rms + 1e-9) / (x.rms + 1e-9));
  const bad = x.bad || y.bad || d > tol;
  worst = Math.max(worst, d); if (bad) fail++;
  console.log(`${name.padEnd(14)} level ${db >= 0 ? '+' : ''}${db.toFixed(2)} dB  env diff ${(d * 100).toFixed(2)}%${y.voices !== undefined ? `  voices ${y.voices}` : ''}${bad ? '  <-- ' + (x.bad || y.bad ? 'NaN' : 'differs') : ''}`);
}
console.log(`${Object.keys(a).length} patches, worst env diff ${(worst * 100).toFixed(2)}%, ${fail} over ${tol * 100}%`);
process.exit(fail ? 1 : 0);
