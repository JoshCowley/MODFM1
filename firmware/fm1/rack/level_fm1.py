#!/usr/bin/env python3
"""Sets the FM-1 presets' levels (AMP LEVEL) so they play at about the same loudness through the firmware's own
sound path: builds the host tools, plays every preset's phrase (audition), scales each LEVEL toward the target and
writes presets_fm1_levels.json, which presets_fm1.py applies. A few passes settle it (the voices' gain goes as
LEVEL squared, so a pass moves LEVEL by 10^(dB/40); the effects and the soft clip bend it a little).

  EURORACK=/path/to/eurorack python3 level_fm1.py [passes] [target dB]     (defaults: 3 passes, -19 dB)
"""
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
LEVELS = HERE / "presets_fm1_levels.json"


def measure():
    """name -> (peak, loudness dB) of every preset, as the firmware would play it now"""
    run = lambda c: subprocess.run(c, shell=True, cwd=HERE, check=True, capture_output=True, text=True).stdout
    run("python3 fm1_presets.py && python3 make_presets.py && sh build_host.sh")
    with tempfile.TemporaryDirectory() as d:
        out = run(f"build/host/audition {d}")
    res = {}
    for line in out.splitlines():
        m = re.match(r"(.{16}) (\S+)\s+peak ([\d.]+)\s+loud\s+(-?[\d.]+)", line)
        if m:
            res[m.group(1).strip()] = (float(m.group(3)), float(m.group(4)))
    return res


def main():
    passes = int(sys.argv[1]) if len(sys.argv) > 1 else 3
    target = float(sys.argv[2]) if len(sys.argv) > 2 else -19.0
    sys.path.insert(0, str(HERE))
    from presets_fm1 import fm1_presets
    page = json.loads((HERE.parent.parent.parent / "web" / "presets.json").read_text())
    for k in range(passes):
        now = {p["name"]: p["amp"]["LEVEL"] for p in fm1_presets(page)}
        res = measure()
        lv, worst = {}, 0.0
        for n, (peak, loud) in res.items():
            d = target - loud
            lv[n] = round(max(0.05, min(1.0, now[n] * 10 ** (d / 40))), 3)
            if not (lv[n] == 1.0 and d > 0):          # at full level already: as loud as it goes
                worst = max(worst, abs(d))
        LEVELS.write_text(json.dumps(lv, indent=0, sort_keys=True) + "\n")
        print(f"pass {k + 1}: largest step {worst:.1f} dB")
    res = measure()
    for n, (peak, loud) in sorted(res.items(), key=lambda x: x[1][1]):
        print(f"  {n:16} {loud:6.1f} dB  peak {peak:.3f}")


if __name__ == "__main__":
    os.environ.setdefault("EURORACK", str(HERE.parent.parent.parent.parent / "eurorack"))
    sys.exit(main())
