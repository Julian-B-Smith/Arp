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
import json
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
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


def main(argv) -> int:
    binary = DEFAULT_BINARY
    if "--binary" in argv:
        binary = Path(argv[argv.index("--binary") + 1])
    dumped = dump_defaults(binary)
    if "--plant" in argv:
        problems = check(planted(dumped))
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
    return 0 if (not problems and seen == wants) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
