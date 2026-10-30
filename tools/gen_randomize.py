#!/usr/bin/env python3
"""gen_randomize.py - generate core/include/arp/randomize_data.h from spec/randomize.json (SPEC 15, D-044, D-048).

    python3 tools/gen_randomize.py            write the header
    python3 tools/gen_randomize.py --check    exit 1 if the header on disk differs from what the JSON generates, if the JSON
                                              disagrees with spec/params.json or with its own stated laws; then prove the
                                              checks on planted faults (RZ15)

The randomize table is DATA, like the bank (gen_bank.py): spec/randomize.json is normative and the header is generated, never
hand-edited, so the C++ needs no parser. The three integer tables (sigma, exp, the step-time ladder) are committed in the JSON
as integers: the C++ reads them and never computes a power, a logarithm or an exponential, which is what makes randomize
identical on every platform. This tool recomputes each table from its stated law in Python floats only to CHECK the JSON
(RZ15 "the sigma table regenerates bit for bit"); the JSON, not this recomputation, is the truth the core uses.
Stdlib only.
"""
import copy
import json
import math
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
RND_JSON = REPO / "spec" / "randomize.json"
PARAMS_JSON = REPO / "spec" / "params.json"
BANK_JSON = REPO / "spec" / "bank" / "starter.json"
HEADER = REPO / "core" / "include" / "arp" / "randomize_data.h"


def fnv1a(s):
    """FNV-1a 32-bit, the key id of SPEC 15 (the C++ computes the same at compile time; RZ15 compares)."""
    h = 0x811C9DC5
    for ch in s.encode():
        h = ((h ^ ch) * 0x01000193) & 0xFFFFFFFF
    return h


def problems(rnd, params, bank):
    """Every disagreement between randomize.json and params.json / the bank / its own laws, as strings (empty = consistent)."""
    out = []
    pkeys = [p["key"] for p in params["params"]]
    rkeys = [k["key"] for k in rnd["keys"]]
    if rkeys != pkeys:
        out.append("keys differ from params.json (same 50, same order required)")
    byp = {p["key"]: p for p in params["params"]}
    bank_ids = [p["id"] for p in bank["patterns"]] + ["user"]
    rate_names = [r["name"] for r in params["rate"]]
    for k in rnd["keys"]:
        p = byp.get(k["key"])
        if p is None:
            continue
        if k["class"] not in ("structure", "value", "never"):
            out.append("%s: bad class %r" % (k["key"], k["class"]))
        if k["class"] == "never" and (k.get("exempt") or k.get("group")):
            out.append("%s: a never-draw key cannot be exempt or grouped" % k["key"])
        if "band" in k:
            lo, hi = k["band"]
            if p.get("type") in ("bool", "enum") or not (p["min"] <= lo <= hi <= p["max"]):
                out.append("%s: band %r is outside its legal range" % (k["key"], k["band"]))
        if "taste" in k:
            legal = p.get("values") or (bank_ids if k["key"] == "pattern" else ["off", "on"] if p["type"] == "bool" else None)
            if legal is None or sorted(k["taste"]) != sorted(legal):
                out.append("%s: taste names differ from the setting's values" % k["key"])
            if any((not isinstance(w, int)) or w < 0 for w in k["taste"].values()):
                out.append("%s: a taste weight is not a non-negative integer" % k["key"])
    exempt = {k["key"] for k in rnd["keys"] if k.get("exempt")}
    if len(exempt) != 15:
        out.append("expected 15 exempt keys, found %d" % len(exempt))
    for g, members in rnd["groups"].items():
        for m in members:
            if next((k for k in rnd["keys"] if k["key"] == m), {}).get("group") != g:
                out.append("group %s: member %s does not name it" % (g, m))
    for name, t in rnd["intervals"].items():
        lo, hi = (-48, 48) if name == "transposition" else (-24, 24)
        for unit, rows in t.items():
            for v, w in rows:
                if not (lo <= v <= hi) or v == 0 or w <= 0:
                    out.append("intervals.%s.%s: %r is outside %d..%d, zero, or unweighted" % (name, unit, [v, w], lo, hi))
    for fam, f in rnd["rate_families"].items():
        if any(m not in rate_names for m in f["members"]) or any(b not in f["members"] for b in f["band"]):
            out.append("rate family %s names a division params.json lacks" % fam)
    fams = sum((f["members"] for f in rnd["rate_families"].values()), [])
    if sorted(fams) != sorted(rate_names):
        out.append("rate families do not partition params.json's 22 divisions")
    if sorted(rnd["fifths"]) != list(range(12)):
        out.append("fifths is not a permutation of 0..11")
    scale_names = [n for n, _ in params["ui_scale_names"]["scales"]]
    if sorted(rnd["named_scale_taste"]) != sorted(scale_names):
        out.append("named_scale_taste differs from params.json ui_scale_names")
    want_seed = [p["id"] for p in bank["patterns"] if any(s.get("prob", 100) < 100 for s in p["steps"])] + ["user"]
    if rnd["seed_reading_patterns"] != want_seed:
        out.append("seed_reading_patterns differ from the bank's patterns with a prob < 100 step (plus user)")
    t = rnd["tables"]
    sig = [0] + [round(4 * (c / 1000) * 50 ** (c / 1000 - 1) * 65536) for c in range(1, 1001)]
    if t["sigma_q16"]["values"] != sig:
        out.append("tables.sigma_q16 does not regenerate bit for bit from its law")
    if t["exp_neg_q16"]["values"] != [round(math.exp(-x / 256) * 65536) for x in range(4096)]:
        out.append("tables.exp_neg_q16 does not regenerate bit for bit from its law")
    lad = t["step_time_ladder_us"]["values"]
    if lad != [round(10000 * 2 ** (i / 12)) for i in range(92)] + [2000000] or not all(10000 <= v <= 2000000 for v in lad):
        out.append("tables.step_time_ladder_us does not regenerate from its law, or leaves 10000..2000000")
    ids = [fnv1a(n) for n in pkeys + list(rnd["groups"]) + ["scale_mask"]]
    if len(set(ids)) != len(ids):
        out.append("key ids collide")
    return out


def header(rnd, params, bank):
    """The C++ header text."""
    byk = {k["key"]: k for k in rnd["keys"]}
    pvals = {p["key"]: p.get("values") for p in params["params"]}
    bank_ids = [p["id"] for p in bank["patterns"]] + ["user"]
    rate_names = [r["name"] for r in params["rate"]]

    def taste(key, names):
        return "{" + ", ".join(str(byk[key]["taste"][n]) for n in names) + "}"

    def arr(vals):
        return ", ".join(str(v) for v in vals)

    d = rnd["draw"]
    L = [
        "// GENERATED by tools/gen_randomize.py from spec/randomize.json - DO NOT EDIT BY HAND.",
        "// SPEC 15 (D-044, D-048). tools/gen_randomize.py --check regenerates it in memory and compares (RZ15).",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "namespace arp::rnd::data {",
        "",
        "// The draw's constants (SPEC 15.2).",
        "inline constexpr std::uint32_t kSalt = 0x%08Xu, kClick = 0x%08Xu, kCorner = 0x%08Xu, kKey = 0x%08Xu, kIdx = 0x%08Xu;"
        % (d["salt"], d["k_click"], d["k_corner"], d["k_key"], d["k_idx"]),
        "",
        "// Classes per ParamId (params.json order): 0 structure, 1 value, 2 never. Group: 0 none, 1 scale, 2 transposition, 3 chord.",
    ]
    # Every setting's name and legal range, in params.json order (enums and bools as indices): the key ids hash the names, and
    # validSettings() checks the ranges (SPEC 15: no settings validator existed before randomize).
    names, lo, hi = [], [], []
    for pp in params["params"]:
        names.append(pp["key"])
        t = pp["type"]
        if t == "bool":
            a, z = 0, 1
        elif t == "enum":
            n = len(pp["values"]) if "values" in pp else len(bank_ids) if pp["key"] == "pattern" else len(rate_names)
            a, z = 0, n - 1
        elif t == "uint32":
            a, z = 0, 4294967295
        else:
            a, z = pp["min"], pp["max"]
        lo.append(a); hi.append(z)
    L.append("inline constexpr const char* kParamNames[%d] = {%s};" % (len(names), ", ".join('"%s"' % n for n in names)))
    L.append("inline constexpr std::int64_t kParamMin[%d] = {%s};" % (len(lo), arr(lo)))
    L.append("inline constexpr std::int64_t kParamMax[%d] = {%s};" % (len(hi), arr("%dLL" % v for v in hi)))
    L.append("")
    cls = {"structure": 0, "value": 1, "never": 2}
    gid = {None: 0, "scale": 1, "transposition": 2, "chord": 3}
    keys = [k["key"] for k in rnd["keys"]]
    L.append("inline constexpr std::uint8_t kClass[%d] = {%s};" % (len(keys), arr(cls[byk[k]["class"]] for k in keys)))
    L.append("inline constexpr std::uint8_t kGroup[%d] = {%s};" % (len(keys), arr(gid[byk[k].get("group")] for k in keys)))
    L.append("inline constexpr bool kExempt[%d] = {%s};" % (len(keys), ", ".join("true" if byk[k]["exempt"] else "false" for k in keys)))
    L.append("")
    L.append("// Taste weights, in each enum's value order (pattern: the bank's ids, then user; bools: off, on).")
    L.append("inline constexpr std::uint32_t kTasteOrder[] = %s;" % taste("order", pvals["order"]))
    L.append("inline constexpr std::uint32_t kTastePattern[] = %s;" % taste("pattern", bank_ids))
    L.append("inline constexpr std::uint32_t kTasteRestart[] = %s;" % taste("restart", pvals["restart"]))
    L.append("inline constexpr std::uint32_t kTasteVelMode[] = %s;" % taste("vel_mode", pvals["vel_mode"]))
    L.append("inline constexpr std::uint32_t kTasteQuantize[] = %s;" % taste("quantize", pvals["quantize"]))
    L.append("inline constexpr std::uint32_t kTasteStepUnit[] = %s;" % taste("step_unit", pvals["step_unit"]))
    L.append("inline constexpr std::uint32_t kTasteChordUnit[] = %s;" % taste("chord_unit", pvals["chord_unit"]))
    L.append("inline constexpr std::uint32_t kTasteChordEnabled[] = %s;" % taste("chord_enabled", ["off", "on"]))
    L.append("inline constexpr std::uint32_t kTasteChordOn[] = %s;" % taste("chord_on1", ["off", "on"]))
    for i in range(2, 7):
        if byk["chord_on%d" % i]["taste"] != byk["chord_on1"]["taste"]:
            raise ValueError("chord_on%d's taste differs from chord_on1's; the header carries one table" % i)
    L.append("")
    L.append("// Bands {lo, hi} in the setting's own units (SPEC 15, the safe bands).")
    for k, name in (("gate", "Gate"), ("swing", "Swing"), ("step_time_us", "StepTime"), ("vel_fixed", "VelFixed"),
                    ("vel_random", "VelRandom"), ("gate_random", "GateRandom"), ("random_curve", "RandomCurve"),
                    ("steps", "Steps"), ("repeats", "Repeats")):
        lo, hi = byk[k]["band"]
        L.append("inline constexpr std::int32_t kBand%s[2] = {%d, %d};" % (name, lo, hi))
    L.append("inline constexpr std::int32_t kSpanSemitones = %d, kSpanDegrees = %d;" % (rnd["span_bound"]["semitones"], rnd["span_bound"]["degrees"]))
    L.append("")
    L.append("// Interval tables {value, weight}.")
    for name, cname in (("transposition", "Step"), ("chord", "Chord")):
        for unit, uname in (("semitones", "Semis"), ("degrees", "Degs")):
            rows = rnd["intervals"][name][unit]
            L.append("inline constexpr std::int32_t k%s%s[%d][2] = {%s};" % (cname, uname, len(rows), ", ".join("{%d, %d}" % (v, w) for v, w in rows)))
    L.append("")
    L.append("// Rate families: members as arp::Rate indices in length order, and the band as member positions {lo, hi}.")
    for fam, cname in (("straight", "Straight"), ("dotted", "Dotted"), ("triplet", "Triplet")):
        f = rnd["rate_families"][fam]
        L.append("inline constexpr std::uint8_t kRate%s[%d] = {%s};" % (cname, len(f["members"]), arr(rate_names.index(m) for m in f["members"])))
        L.append("inline constexpr std::int32_t kRate%sBand[2] = {%d, %d};" % (cname, f["members"].index(f["band"][0]), f["members"].index(f["band"][1])))
    L.append("")
    L.append("inline constexpr std::int32_t kFifths[12] = {%s};" % arr(rnd["fifths"]))
    scales = params["ui_scale_names"]["scales"]
    L.append("// The named scales (params.json ui_scale_names) as root-relative 12-bit masks (bit i = degree i), and their taste.")
    L.append("inline constexpr std::uint16_t kScaleMask[%d] = {%s};" % (len(scales), arr(sum(1 << i for i in degs) for _, degs in scales)))
    L.append("inline constexpr std::uint32_t kScaleTaste[%d] = {%s};" % (len(scales), arr(rnd["named_scale_taste"][n] for n, _ in scales)))
    L.append("// Patterns (arp::Pattern indices) with a step whose prob < 100, plus user: they read the seed (SPEC 15, seed's live-when).")
    L.append("inline constexpr std::uint8_t kSeedPatterns[%d] = {%s};" % (len(rnd["seed_reading_patterns"]), arr(bank_ids.index(p) for p in rnd["seed_reading_patterns"])))
    L.append("")
    t = rnd["tables"]
    L.append("inline constexpr std::uint32_t kSigmaQ16[1001] = {%s};" % arr(t["sigma_q16"]["values"]))
    L.append("inline constexpr std::uint32_t kExpNegQ16[4096] = {%s};" % arr(t["exp_neg_q16"]["values"]))
    lad = t["step_time_ladder_us"]["values"]
    L.append("inline constexpr std::int32_t kStepTimeLadder[%d] = {%s};" % (len(lad), arr(lad)))
    L.append("")
    L.append("}  // namespace arp::rnd::data")
    return "\n".join(L) + "\n"


def load():
    return json.load(open(RND_JSON)), json.load(open(PARAMS_JSON)), json.load(open(BANK_JSON))


def main():
    rnd, params, bank = load()
    if "--check" not in sys.argv:
        bad = problems(rnd, params, bank)
        if bad:
            sys.exit("gen_randomize: spec/randomize.json is inconsistent:\n  " + "\n  ".join(bad))
        HEADER.write_text(header(rnd, params, bank))
        print("wrote", HEADER.relative_to(REPO))
        return
    ok = True
    bad = problems(rnd, params, bank)
    for b in bad:
        print("FAIL  randomize.json:", b)
    ok = ok and not bad
    on_disk = HEADER.read_text() if HEADER.exists() else ""
    if on_disk != header(rnd, params, bank):
        print("FAIL  %s is not what %s generates; run: python3 tools/gen_randomize.py" % (HEADER.relative_to(REPO), RND_JSON.relative_to(REPO)))
        ok = False
    else:
        print("PASS  the generated header matches spec/randomize.json")
    if not bad:
        print("PASS  spec/randomize.json agrees with params.json, the bank and its own laws (tables regenerate bit for bit; key ids collision-free)")
    # RZ15's plants: each perturbation must be reported, or the check is blind.
    plants = []
    p1 = copy.deepcopy(rnd); p1["keys"][[k["key"] for k in p1["keys"]].index("order")]["taste"]["up"] += 1
    plants.append(("one taste weight perturbed (order up +1): the header no longer matches", header(p1, params, bank) != on_disk))
    p2 = copy.deepcopy(rnd); p2["keys"][[k["key"] for k in p2["keys"]].index("gate")]["band"] = [0, 100]
    plants.append(("one band perturbed past the legal range (gate 0..100)", bool(problems(p2, params, bank))))
    p3 = copy.deepcopy(rnd); p3["tables"]["sigma_q16"]["values"][350] += 1
    plants.append(("one sigma entry perturbed", bool(problems(p3, params, bank))))
    p4 = copy.deepcopy(rnd); p4["intervals"]["chord"]["semitones"].append([25, 1])
    plants.append(("a chord interval of +25 (outside -24..24)", bool(problems(p4, params, bank))))
    for msg, fired in plants:
        print("  %s  plant fires: %s" % ("PASS" if fired else "FAIL", msg))
        ok = ok and fired
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
