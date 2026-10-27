#!/usr/bin/env python3
"""core_params_check.py - the drift guard between the C++ Settings struct and spec/params.json.

    python3 tools/core_params_check.py            check; then prove the planted control goes red
    python3 tools/core_params_check.py --plant    run ONLY the planted (perturbed) check; exits 1 = red = the control works
    python3 tools/core_params_check.py --binary PATH   use a specific arp_tests binary

It runs the test binary with --dump-defaults (one `key=value` per setting, plus `values.<key>=` enum
lists, `tpq=` and `rate_ticks.<name>=`) and checks, against spec/params.json and spec/bank/starter.json:
  - the set of setting keys is exactly params.json's 47 (no extra, none missing);
  - every default equals params.json's default (bools true/false, ints, enums by NAME);
  - every enum's value list equals params.json's, in order (rate names and ticks from `rate[]`,
    pattern ids from the starter bank plus `user`);
  - TPQ equals params.json's `tpq`.
The planted control perturbs one parsed default (gate 80 -> 81) and one enum list; the check MUST report
both. A drift guard that cannot fail guards nothing (charter: every detector has a must-fail control).
Exit 0 = green. Stdlib only; reads only this repo's tracked files and the built test binary.
"""
import importlib.util
import json
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
GEN_BANK = REPO / "tools" / "gen_bank.py"
BUILD_SCRIPT = REPO / "tools" / "core_test.sh"
DEFAULT_BINARY = REPO / "build" / "core" / "arp_tests"


def dump_defaults(binary: Path) -> dict:
    """The binary's --dump-defaults output as {key: value-string}."""
    if not binary.exists():
        built = subprocess.run(["bash", str(BUILD_SCRIPT), "build"], capture_output=True, text=True)
        if built.returncode != 0 or not binary.exists():
            sys.stderr.write(built.stdout + built.stderr)
            sys.exit("core_params_check: cannot build %s" % binary)
    out = subprocess.run([str(binary), "--dump-defaults"], capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit("core_params_check: %s --dump-defaults exited %d" % (binary, out.returncode))
    dumped = {}
    for line in out.stdout.splitlines():
        if "=" not in line:
            sys.exit("core_params_check: malformed dump line %r" % line)
        key, _, value = line.partition("=")
        if key in dumped:
            sys.exit("core_params_check: duplicate dump key %r" % key)
        dumped[key] = value
    return dumped


def load_gen_bank():
    spec = importlib.util.spec_from_file_location("gen_bank", GEN_BANK)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def dump_bank(binary: Path) -> dict:
    out = subprocess.run([str(binary), "--dump-bank"], capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit("core_params_check: %s --dump-bank exited %d" % (binary, out.returncode))
    return dict(line.partition("=")[::2] for line in out.stdout.splitlines())


def expected_bank(bank: dict) -> dict:
    """The bank the C++ must contain, in the dump's own line format, computed from the JSON by gen_bank's resolver."""
    gb = load_gen_bank()
    want = {}
    for pid, n, size, unit, steps in gb.resolved(bank):
        want["pattern.%s" % pid] = "%d,%d,%s" % (n, size, unit)
        for j, s in enumerate(steps):
            want["pattern.%s.%d" % (pid, j)] = ",".join(str(int(v)) for v in (s["on"], s["tie"], s["vel"], s["gate"], s["step"], s["prob"], s["ratchet"], s["trig"], s["pick"]))
    return want


def check_bank(dumped: dict, bank: dict) -> list:
    """Drift between the COMPILED bank (arp_tests --dump-bank) and spec/bank/starter.json; [] = none."""
    want = expected_bank(bank)
    problems = ["%s: C++ %r, JSON %r" % (k, dumped.get(k), v) for k, v in want.items() if dumped.get(k) != v]
    problems += ["extra in C++: %s" % k for k in sorted(set(dumped) - set(want))]
    return problems


def check_header(bank: dict) -> list:
    """The generated header on disk is what gen_bank.py makes of the JSON."""
    gb = load_gen_bank()
    have = gb.HEADER.read_text() if gb.HEADER.exists() else ""
    return [] if have == gb.generate(bank) else ["core/include/arp/bank_data.h is not what tools/gen_bank.py generates from spec/bank/starter.json"]


def planted_bank(bank: dict) -> dict:
    """A copy of the bank with ONE value changed (a probability): both bank checks must read red on it."""
    bad = json.loads(json.dumps(bank))
    step = next(s for p in bad["patterns"] if p["id"] == "sparse_chance" for s in p["steps"] if "prob" in s)
    step["prob"] += 1
    return bad


def fmt(value) -> str:
    """params.json default -> the string the dump prints."""
    if isinstance(value, bool):
        return "true" if value else "false"
    return str(value)


def check(dumped: dict) -> list:
    """Every drift found, as human-readable strings ([] = no drift)."""
    spec = json.loads((REPO / "spec" / "params.json").read_text())
    bank = json.loads((REPO / "spec" / "bank" / "starter.json").read_text())
    problems = []
    spec_keys = [p["key"] for p in spec["params"]]
    setting_keys = {k for k in dumped if not k.startswith("values.") and not k.startswith("rate_ticks.") and k != "tpq"}
    for k in spec_keys:
        if k not in setting_keys:
            problems.append("missing setting %s (params.json has it)" % k)
    for k in sorted(setting_keys - set(spec_keys)):
        problems.append("extra setting %s (params.json does not have it)" % k)
    rate_names = [r["name"] for r in spec["rate"]]
    for p in spec["params"]:
        k = p["key"]
        if k not in dumped:
            continue
        if dumped[k] != fmt(p["default"]):
            problems.append("default of %s: C++ %r, params.json %r" % (k, dumped[k], fmt(p["default"])))
        if p["type"] == "enum":
            if "values" in p:
                want = p["values"]
            elif k == "rate":
                want = rate_names
            elif k == "pattern":
                want = [x["id"] for x in bank["patterns"]] + ["user"]
            else:
                problems.append("enum %s: no value list to check against" % k)
                continue
            got = dumped.get("values." + k)
            if got != ",".join(want):
                problems.append("values of %s: C++ %r, spec %r" % (k, got, ",".join(want)))
    for r in spec["rate"]:
        key = "rate_ticks." + r["name"]
        if dumped.get(key) != str(r["ticks"]):
            problems.append("%s: C++ %r, params.json %r" % (key, dumped.get(key), str(r["ticks"])))
    extra_rates = {k for k in dumped if k.startswith("rate_ticks.")} - {"rate_ticks." + n for n in rate_names}
    for k in sorted(extra_rates):
        problems.append("extra rate in C++: %s" % k)
    if dumped.get("tpq") != str(spec["tpq"]):
        problems.append("tpq: C++ %r, params.json %r" % (dumped.get("tpq"), str(spec["tpq"])))
    return problems


def planted(dumped: dict) -> dict:
    """The must-fail control: one default and one enum list perturbed."""
    bad = dict(dumped)
    bad["gate"] = str(int(bad["gate"]) + 1)
    bad["values.latch"] = bad["values.latch"] + ",extra"
    return bad


def check_face(params: dict) -> list:
    """horde builds Arp's face from spec/params.json (D-034 Q-H9, D-035): every setting needs a key, a display name,
    a morph class, a default and its range, a unit or display format where it is a continuous value, and a display
    label for every enum value. Missing any of these is a face horde cannot draw, so it is a failure here."""
    problems, labels = [], set()
    for x in params["params"]:
        k = x.get("key", "?")
        if not x.get("label"):
            problems.append("setting %s has no display name (label)" % k)
        elif x["label"] in labels:
            problems.append("display name %r is used twice" % x["label"])
        labels.add(x.get("label"))
        if x.get("kind") not in ("discrete", "continuous"):
            problems.append("setting %s has no morph class (kind)" % k)
        if "default" not in x:
            problems.append("setting %s has no default" % k)
        if x.get("type") in ("int", "percent", "permille") and not ("min" in x and "max" in x):
            problems.append("setting %s has no range" % k)
        if x.get("kind") == "continuous" and not (x.get("unit") or x.get("display")):
            problems.append("continuous setting %s has no unit or display format" % k)
        if x.get("type") == "enum" and "values" in x and set(x.get("value_labels", {})) != set(x["values"]):
            problems.append("enum %s does not label every value" % k)
    return problems


def planted_face(params: dict) -> dict:
    bad = json.loads(json.dumps(params))
    del bad["params"][10]["label"]                                 # gate loses its display name
    del bad["params"][2]["value_labels"]["pedal_high"]             # one order style loses its label
    return bad


def main(argv) -> int:
    binary = DEFAULT_BINARY
    if "--binary" in argv:
        binary = Path(argv[argv.index("--binary") + 1])
    dumped = dump_defaults(binary)
    bank = json.loads((REPO / "spec" / "bank" / "starter.json").read_text())
    params = json.loads((REPO / "spec" / "params.json").read_text())
    if "--plant" in argv:
        problems = check(planted(dumped)) + check_bank(dump_bank(binary), planted_bank(bank)) + check_header(planted_bank(bank)) \
            + check_face(planted_face(params))
        for p in problems:
            print("  RED   (planted) " + p)
        print("planted control: %s" % ("RED, as it must be" if problems else "STILL GREEN - the drift guard is blind"))
        return 1 if problems else 0   # exit 1 = red = the control works; 0 would mean the guard cannot fail
    problems = check(dumped)
    for p in problems:
        print("  FAIL  " + p)
    n_settings = len([k for k in dumped if not k.startswith(("values.", "rate_ticks.")) and k != "tpq"])
    print("%s  core defaults == spec/params.json (%d settings, %d enum lists, %d rate ticks)" % (
        "FAIL" if problems else "PASS", n_settings, len([k for k in dumped if k.startswith("values.")]), len([k for k in dumped if k.startswith("rate_ticks.")])))
    fired = check(planted(dumped))
    wants = {"default of gate", "values of latch"}
    seen = {w for w in wants if any(f.startswith(w) for f in fired)}
    print("%s  plant fires: a perturbed default and a perturbed enum list are both reported (%s)" % (
        "PASS" if seen == wants else "FAIL", "; ".join(fired) if fired else "nothing reported"))
    # the pattern bank: embedded from spec/bank/starter.json by tools/gen_bank.py (SPEC 4.3, D-004)
    compiled = dump_bank(binary)
    bank_problems = check_bank(compiled, bank) + check_header(bank)
    for p in bank_problems:
        print("  FAIL  " + p)
    print("%s  embedded bank == spec/bank/starter.json (%d patterns, %d steps), and bank_data.h is the generated file" % (
        "FAIL" if bank_problems else "PASS", len(bank["patterns"]), sum(p["length"] for p in bank["patterns"])))
    pb = planted_bank(bank)
    red_dump, red_header = check_bank(compiled, pb), check_header(pb)
    bank_fired = bool(red_dump) and bool(red_header)
    print("%s  plant fires: one changed probability in the JSON is reported by the compiled-bank check (%s) and by the generated-header check" % (
        "PASS" if bank_fired else "FAIL", red_dump[0] if red_dump else "nothing reported"))
    # the face horde draws (D-034 Q-H9, D-035): names, units, formats and value labels, from params.json alone
    face_problems = check_face(params)
    for p in face_problems:
        print("  FAIL  " + p)
    print("%s  every setting carries what horde's face needs (%d settings: display name, morph class, default, range, unit or format, enum value labels)" % (
        "FAIL" if face_problems else "PASS", len(params["params"])))
    face_fired = check_face(planted_face(params))
    face_seen = any("gate" in f for f in face_fired) and any("order" in f for f in face_fired)
    print("%s  plant fires: a setting without a display name and an enum with an unlabelled value are both reported (%s)" % (
        "PASS" if face_seen else "FAIL", "; ".join(face_fired) if face_fired else "nothing reported"))
    return 0 if (not problems and seen == wants and not bank_problems and bank_fired and not face_problems and face_seen) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
