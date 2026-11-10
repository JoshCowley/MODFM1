#!/usr/bin/env python3
"""The FM-1's factory presets as engine calls, without the page (Nov 2026).

The page (web/presets.json, from web/presets_src.py) and the FM-1 used to share one set, recorded from the page by
tests/record_calls.mjs. The FM-1 now has its own: the page's presets, changed or added to by presets_fm1.py (FUNC,
which the page doesn't have, LFO targets, voices that fit its CPU), turned into engine calls here the way the page's
loadEntry + syncEngine (web/src/1-data.js, 6-engine.js) would, from the same module definitions (make_ui.load()).

  python3 fm1_presets.py --check   every page preset gives the calls the page recorded (presets-calls.json)
  python3 fm1_presets.py           writes ../../tests/presets-fm1-calls.json, which make_presets.py reads
"""
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
WEB = HERE.parent.parent.parent / "web"
TESTS = HERE.parent.parent / "tests"
OUT = TESTS / "presets-fm1-calls.json"

MID = {"PLAITS": 1, "BRAIDS": 2, "ELEMENTS": 3, "RINGS": 10, "WARPS": 11, "FILTER": 12, "PEAKS": 20, "TIDES": 21,
       "MARBLES": 22, "GRIDS": 23, "FUNC": 24, "CLOUDS": 30, "DELAY": 31, "REVERB": 32, "CHORUS": 33}
SLOTS = [("SRC", "SRC"), ("SHP", "SHP"), ("MODA", "MOD A"), ("MODB", "MOD B"), ("FX1", "FX 1"), ("FX2", "FX 2")]
BUILT_SRC = ["—", "M1", "M2", "M3", "M4", "VELO", "KEY TRK", "RANDOM", "AMP ENV"]
EXTRA_SRC = ["LFO 1", "LFO 2", "LFO 3", "MOD ENV", "SEQ 1", "SEQ 2", "SEQ 3", "SEQ 4", "WHEEL", "PRESS"]
A_MODE, A_VINTAGE = 11, 13
MAXV = 6


def defs():
    import make_ui
    from func_def import FUNC
    d = make_ui.load()
    d["MODULES"]["FUNC"] = FUNC
    return d


def flat(m):
    return [p for pg in m["pages"] for p in pg]


def meta(p):
    t = p["t"]
    return [0, len(p["opts"]) - 1, len(p["opts"]) - 1] if t == "list" else [-1, 1, 2] if t == "bip" else \
        [p["min"], p["max"], p["max"] - p["min"]] if t == "int" else [0, 1, 1]


def apply_over(m, vals, over, who):
    fl = flat(m)
    for label, v in (over or {}).items():
        j = next((i for i, p in enumerate(fl) if p["l"] == label), -1)
        if j < 0:
            raise SystemExit(f"fm1_presets: {who}: no parameter {label}")
        if fl[j]["t"] == "list":
            if str(v) not in fl[j]["opts"]:
                raise SystemExit(f"fm1_presets: {who}: {label} has no option {v}")
            vals[j] = fl[j]["opts"].index(str(v))
        else:
            vals[j] = v


def calls_for(pr, D, tail):
    """the engine calls the page would send for preset pr (a presets.json entry), in the page's order. tail: the
    calls that are the same for every preset (tempo, master, EQ, MSEQ slews, tuning), as recorded."""
    M, AMP, LFOS = D["MODULES"], D["AMP"], D["LFOS"]
    rack = {}
    for sid, _ in SLOTS:
        name = pr["slots"].get(sid)
        if name:
            v = [p["def"] for p in flat(M[name])]
            apply_over(M[name], v, pr.get("p", {}).get(sid), f"{pr['name']} {sid}")
            rack[sid] = (name, v)
        else:
            rack[sid] = (None, [])
    amp = [p["def"] for p in flat(AMP)]
    apply_over(AMP, amp, pr.get("amp"), pr["name"] + " AMP")
    lfo = [p["def"] for p in flat(LFOS)]
    apply_over(LFOS, lfo, pr.get("lfo"), pr["name"] + " LFO")

    def src_ix(name):
        if name in BUILT_SRC:
            return BUILT_SRC.index(name)
        if name in EXTRA_SRC:
            return 13 + EXTRA_SRC.index(name)
        for k, sid in enumerate(["MODA", "MODB"]):
            mod = rack[sid][0]
            if mod:
                for j, o in enumerate(M[mod].get("outs", [])):
                    if f"{sid[-1]} {o}" == name:
                        return 9 + k * 2 + j
        if name not in (None, "", "—"):
            raise SystemExit(f"fm1_presets: {pr['name']}: no source {name}")
        return 0

    def dst_ix(name):
        if name == "PITCH":
            return [-1, 0]
        if name == "PAN":
            return [-1, 1]
        for pre, m, slot in (("LFO.", LFOS, 8), ("AMP.", AMP, 6)):
            if name.startswith(pre):
                j = next((i for i, p in enumerate(flat(m)) if pre + p["l"] == name), -1)
                return [slot, j] if j >= 0 else None
        for si, (sid, sname) in enumerate(SLOTS):
            mod = rack[sid][0]
            if mod:
                j = next((i for i, p in enumerate(flat(M[mod])) if f"{sname}.{p['l']}" == name), -1)
                if j >= 0:
                    return [si, j]
        return None

    calls = []
    for si, (sid, _) in enumerate(SLOTS):
        name, v = rack[sid]
        calls.append(["set_module", si, MID[name] if name else 0])
        if name:
            for pi, p in enumerate(flat(M[name])):
                calls.append(["set_meta", si, pi, *meta(p)])
                calls.append(["set_param", si, pi, v[pi]])
    for pi, p in enumerate(flat(AMP)):
        calls.append(["set_meta", 6, pi, *meta(p)])
        calls.append(["set_param", 6, pi, amp[pi]])
    calls.append(tail["tempo"])
    for pi, p in enumerate(flat(LFOS)):
        calls.append(["set_meta", 8, pi, *meta(p)])
        calls.append(["set_param", 8, pi, lfo[pi]])
    rows = pr.get("matrix", [])
    for i in range(12):
        r = rows[i] if i < len(rows) else None
        src, dst, amt, via = (r[0], r[1], r[2], r[3] if len(r) > 3 else "—") if r else ("—", "—", 0, "—")
        d = None if dst == "—" else dst_ix(dst)
        if dst != "—" and d is None:
            raise SystemExit(f"fm1_presets: {pr['name']}: no destination {dst}")
        calls.append(["set_route", i, src_ix(src) if d else 0, d[0] if d else 0, d[1] if d else 0, amt, src_ix(via)])
    tg = pr.get("lfo_tgt", [])
    for j in range(6):
        t = tg[j] if j < len(tg) else None
        d = dst_ix(t[0]) if t else None
        if t and d is None:
            raise SystemExit(f"fm1_presets: {pr['name']}: no LFO target {t[0]}")
        calls.append(["set_route", 12 + j, src_ix(f"LFO {j // 2 + 1}") if d else 0, d[0] if d else 0, d[1] if d else 0,
                      t[1] if t else 0, 0])
    for i, v in enumerate(pr.get("macros", [0.5] * 4)):
        calls.append(["set_macro", i, v])
    calls += tail["pre"]
    calls.append(["set_legato", 0 if amp[A_MODE] == 1 else 1])
    calls.append(["set_vintage", amp[A_VINTAGE]])
    calls += tail["post"]
    return {"name": pr["name"], "calls": calls, "voices": 1 if amp[A_MODE] in (1, 2) else MAXV, "mode": amp[A_MODE]}


def tail_of(rec):
    """the calls every recorded preset has the same (taken from INIT's recording)"""
    c = next(p for p in rec if p["name"] == "INIT")["calls"]
    tempo = next(x for x in c if x[0] == "set_param" and x[1] == 7)
    pre = [x for x in c if x[0] in ("set_master", "set_master_fx")]
    post = [x for x in c if x[0] in ("set_ext_slew", "set_tune", "set_hw")]
    return {"tempo": tempo, "pre": pre, "post": post}


def norm(calls):
    """final state, keyed as make_presets.py keys it, floats rounded (the page's are JavaScript doubles)"""
    from make_presets import final_state
    return {k: [round(x, 6) if isinstance(x, float) else x for x in c] for k, c in final_state(calls).items()}


def check():
    rec = json.loads((TESTS / "presets-calls.json").read_text())
    src = {p["name"]: p for p in json.loads((WEB / "presets.json").read_text())}
    D, tail, bad = defs(), tail_of(rec), 0
    for r in rec:
        mine = calls_for(src[r["name"]], D, tail)
        a, b = norm(r["calls"]), norm(mine["calls"])
        diff = [k for k in set(a) | set(b) if a.get(k) != b.get(k)]
        if diff or mine["mode"] != r["mode"] or mine["voices"] != r["voices"]:
            bad += 1
            print(r["name"], "differs:", [(k, a.get(k), b.get(k)) for k in diff[:4]], mine["mode"], r["mode"], r["voices"])
    print(f"fm1_presets --check: {len(rec) - bad} of {len(rec)} presets give the page's calls")
    return 1 if bad else 0


def main():
    if "--check" in sys.argv:
        return check()
    from presets_fm1 import fm1_presets
    rec = json.loads((TESTS / "presets-calls.json").read_text())
    D, tail = defs(), tail_of(rec)
    out = []
    for pr in fm1_presets(json.loads((WEB / "presets.json").read_text())):
        c = calls_for(pr, D, tail)
        c["fm1"] = {k: pr[k] for k in ("fm1_voices", "osc") if k in pr}
        out.append(c)
    OUT.write_text(json.dumps(out))
    print(f"{OUT.name}: {len(out)} presets")


if __name__ == "__main__":
    sys.exit(main())
