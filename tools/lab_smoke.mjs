// lab_smoke.mjs - Arp's lab smoke (ROADMAP Phase A gate; SPEC section 11).
//
// The lab is a listening twin, not a parity target: this does NOT prove the C++
// core. It proves the lab is fit to listen to against the spec's invariants: one
// terminal per note, output identical across block sizes and sample rates, seeded
// determinism, and the spec'd grid, swing, quantize, tie, probability and trigger
// rules. EVERY detector is preceded by its planted fault (charter: "every detector
// has a must-fail planted control before it gates anything"); a plant that does not
// fire is a blind detector and is itself a FAIL.
//
// Harness: the CORE section is sliced out at its banners and built with
// `new Function`, with Math, Date and performance SHADOWED by poisoned stand-ins, so
// a clock read or Math.random in the core throws (the static regex is the second,
// independent layer). A missing banner throws: a silent mis-slice would make every
// check vacuous. Plants are the core's own `lab.plant` switches (named, inert by
// default) turned on for a FRESH core; the lab file is never edited here.
import { readFileSync } from "node:fs";
import { isDeepStrictEqual } from "node:util";

const t0 = process.hrtime.bigint();
const html = readFileSync(new URL("../lab/arp-lab.html", import.meta.url), "utf8");
const A = html.indexOf("/* ================= CORE"), B = html.indexOf("/* ================= UI");
if (A < 0 || B < 0 || B <= A) throw new Error("lab_smoke: CORE/UI banners not found - slice anchor lost");
const core = html.slice(A, B);

let fail = 0;
const check = (ok, msg) => { console.log(`  ${ok ? "PASS" : "FAIL"}  ${msg}`); if (!ok) fail = 1; };
const plant = (fired, msg) => check(fired, `plant fires: ${msg}`);

const EXPORTS = "return { ArpCore, simHost, ARP_PARAMS, ARP_BANK, ARP_PLANTS, defaultSettings, draw, TPQ };";
const poisonedMath = Object.freeze(Object.assign(Object.create(null),
  Object.fromEntries(Object.getOwnPropertyNames(Math).map(k => [k, Math[k]])),
  { random() { throw new Error("Math.random called inside the lab CORE"); } }));
const clock = () => { throw new Error("wall-clock read inside the lab CORE"); };
const build = src => new Function("Math", "Date", "performance", src + "\n;" + EXPORTS)(poisonedMath, clock, { now: clock });
const X = build(core);
const D_OF = sr => BigInt(60 * sr * 1000);

// ---- 1. Static and dynamic bans -------------------------------------------------
const strip = s => s.replace(/\/\*[\s\S]*?\*\//g, "").replace(/\/\/.*$/gm, "");
const banned = s => /Math\.random|\bDate\b|\bperformance\b|\bsetTimeout\b|\bAudioContext\b/.test(strip(s));
check(!banned(core), "static: CORE holds no Math.random / Date / performance / setTimeout / AudioContext");
plant(banned(core + "\nconst t = performance.now();") && banned(core + "\nsetTimeout(f, 1);"), "static ban detects a planted performance read and a planted setTimeout");
{
  const anchor = "prepare(sampleRate, maxBlock) {";
  if (core.split(anchor).length !== 2) throw new Error("lab_smoke: plant anchor not unique");
  let threw = false;
  try { build(core.replace(anchor, anchor + " Math.random();")).ArpCore.prototype.prepare.call({ caps: {}, _resetState() { } }, 48000, 64); } catch { threw = true; }
  plant(threw, "poisoned Math throws on a planted Math.random in the core");
}

// ---- 2. Embedded params and bank equal the spec files -------------------------------
{
  const specP = JSON.parse(readFileSync(new URL("../spec/params.json", import.meta.url), "utf8"));
  const specB = JSON.parse(readFileSync(new URL("../spec/bank/starter.json", import.meta.url), "utf8"));
  const m = JSON.parse(JSON.stringify(specP)); m.params[5].default += 1;
  plant(!isDeepStrictEqual(m, X.ARP_PARAMS), "embedded-copy check detects a planted default change");
  check(isDeepStrictEqual(specP, X.ARP_PARAMS), "embedded ARP_PARAMS deep-equals spec/params.json");
  check(isDeepStrictEqual(specB, X.ARP_BANK), "embedded ARP_BANK deep-equals spec/bank/starter.json");
}

// ---- helpers: cores, the exact driver, normalised event streams ------------------
function mkCore(plants = [], opts) {
  const c = new X.ArpCore(opts);
  for (const p of plants) { if (!(p in c.lab.plant)) throw new Error("unknown plant " + p); c.lab.plant[p] = true; }
  return c;
}
const refStr = r => r.port + ":" + r.channel + ":" + r.key + ":" + r.id;
const norm = (e, base, epoch) => ({ a: base + e.s, t: e.t, key: e.key !== undefined ? e.key : e.ref && e.ref.key, vel: e.vel, lane: e.lane, id: e.ref ? refStr(e.ref) : undefined, stream: e.stream, tick: e.tick, epoch, pass: e.ev });   // stream: SPEC §1 / S1 — dropping it here made S1's plant pass vacuously
const streamOf = evs => evs.map(e => `${e.a} ${e.t} ${e.key} ${e.vel} ${e.lane} ${e.id} ${e.stream}`).join("\n");
const tickStreamOf = evs => evs.map(e => `${e.t} ${e.key} ${e.vel} ${e.lane} ${e.id} ${e.tick}`).join("\n");

// Drives a core block by block. Blocks are cut at every host edge, settings change and
// reset, so the SAME musical session can be replayed at any block size (SPEC G1).
function run(o) {
  const sr = o.sr || 8000, block = o.block || 64, total = o.total;
  const c = o.core || mkCore();
  if (o.userPattern) c.setUserPattern(o.userPattern);   // BEFORE prepare on purpose: SPEC 4.6 says the user pattern survives it
  c.prepare(sr, o.maxBlock || 4096);
  const host = o.host || X.simHost({ sr, bpm: o.bpm || 120, startBeats: 0, playing: true });
  const cfg = (o.cfg || []).slice().sort((a, b) => a.at - b.at), ins = (o.ins || []).slice().sort((a, b) => a.at - b.at);
  const resets = o.resets || [], edits = (o.userEdits || []).slice().sort((a, b) => a.at - b.at);
  const cuts = [...new Set([...host.edges(0, total), ...cfg.map(x => x.at), ...edits.map(x => x.at), ...resets].filter(x => x > 0 && x < total))].sort((a, b) => a - b);
  const settingsAt = pos => Object.assign(X.defaultSettings(), { enabled: true }, o.base || {}, ...cfg.filter(x => x.at <= pos).map(x => x.set));
  const evs = [], steps = []; let epoch = 0, pos = 0, ci = 0;
  while (pos < total) {
    if (resets.includes(pos)) { for (const e of c.reset()) evs.push(norm(e, pos, epoch)); epoch++; }
    for (const ed of edits) if (ed.at === pos) c.setUserPattern(ed.p);   // between blocks, in list order (two at one sample: the later one must win)
    while (ci < cuts.length && cuts[ci] <= pos) ci++;
    const n = Math.min(block, (ci < cuts.length ? cuts[ci] : total) - pos);
    const inb = ins.filter(e => e.at >= pos && e.at < pos + n).map(e => { const r = Object.assign({}, e, { s: e.at - pos }); delete r.at; return r; });
    let tr = host.at(pos);
    if (o.jitter) tr = Object.assign({}, tr, { rep: tr.rep + BigInt(o.jitter(pos)) * host.D });
    const out = c.process({ n, transport: tr, settings: settingsAt(pos), scale: o.scale || { root: 0, mask: 0 }, in: inb });
    if (out.length > c.capEv) throw new Error("block exceeded the event cap");
    for (const e of out) evs.push(norm(e, pos, epoch));
    for (const s of c.lastSteps) steps.push(Object.assign({ a: pos + s.s }, s));
    pos += n;
  }
  if (o.finalReset !== false) for (const e of c.reset()) evs.push(norm(e, total, epoch));
  return { evs, steps, core: c, host };
}
const ons = r => r.evs.filter(e => e.t === "on");

// N1: exactly one NoteOff per NoteOn (same ref tuple), at least one sample later.
function n1(evs) {
  const bad = [], on = new Map(), off = new Map();
  for (const e of evs) {
    const k = e.epoch + ":" + e.id;
    if (e.t === "on") { if (on.has(k)) bad.push("duplicate on " + k); on.set(k, e.a); }
    else if (e.t === "off") off.set(k, (off.get(k) || []).concat(e.a));
  }
  for (const [k, a] of on) {
    const o = off.get(k);
    if (!o) bad.push("on without off " + k); else if (o.length > 1) bad.push("double off " + k); else if (o[0] < a + 1) bad.push("off " + (o[0] - a) + " samples after its on " + k);
  }
  for (const k of off.keys()) if (!on.has(k)) bad.push("off without on " + k);
  return bad;
}

// ---- 3. N1: scripted sessions that exercise every row T1-T16 of SPEC 7 -----------------
const K = (at, ref, key, extra) => Object.assign({ at, t: "on", ref, key, vel: 100 }, extra);
const O = (at, ref) => ({ at, t: "off", ref });
const NDOWN = { root: 0, mask: 0xAB5 };
const inScale = (key, root, mask) => ((mask >> (((key - root) % 12) + 12) % 12) & 1) === 1;
const SESS = {
  // T1 gate end, T2 same-key retrigger (gate 150 on one key), T3 held-set change, latch replace/append, pedal, T8 discrete, T9 continuous, T12 tie
  held: () => ({ total: 44000, base: { order: "up", gate: 150 },
    cfg: [{ at: 20000, set: { latch: "replace", pattern: "tied_legato" } }, { at: 27000, set: { latch: "append", pattern: "none" } }, { at: 34000, set: { latch: "off" } }, { at: 36000, set: { gate: 200, swing: 667, vel_fixed: 50 } }],
    ins: [K(100, "A", 60), K(5100, "B", 64), O(9100, "A"), { at: 12000, t: "pedal", down: true }, K(12500, "C", 67), O(14500, "B"), O(15500, "C"), { at: 18000, t: "pedal", down: false },
      K(21000, "D", 60), O(23000, "D"), K(25000, "E", 64), O(26000, "E"),
      K(28000, "F", 60), O(29000, "F"), K(30000, "G", 64), O(31000, "G"), K(32000, "H", 60), O(33000, "H"), K(35000, "X", 62), K(36500, "I", 60)] }),
  // T4 / T5 via the enabled setting, plus pass-through while disabled
  enable: () => ({ total: 30000, base: { gate: 150 },
    cfg: [{ at: 8000, set: { enabled: false } }, { at: 12000, set: { enabled: true } }, { at: 20000, set: { enabled: false } }, { at: 24000, set: { enabled: true } }],
    ins: [K(100, "a", 60), K(100, "b", 64), { at: 5000, t: "bend", value: 100 }, { at: 10000, t: "bend", value: 200 }] }),
  // T6 start/stop/jump/loop wrap/clock switch, T14 free-clock re-base with a note sounding, H7b
  transport: () => {
    const host = X.simHost({ sr: 8000, bpm: 120, startBeats: 0, playing: true });
    host.set(12000, { playing: false }); host.set(20000, { playing: true, seekBeats: 0 }); host.set(28000, { seekBeats: 7 }); host.set(34000, { seekBeats: 1 }); host.set(38000, { bpm: 133.7 });
    return { total: 48000, host, base: { gate: 150 }, ins: [K(100, "a", 60), K(100, "b", 64), O(14000, "a"), O(14000, "b"), K(14200, "c", 67)] };
  },
  // T7 panic (forwarded after the offs)
  panic: () => ({ total: 24000, base: { gate: 150 }, ins: [K(100, "a", 60), K(100, "b", 64), K(100, "c", 67), { at: 15000, t: "panic" }, K(17000, "d", 60)] }),
  // T8 / T15 every discrete setting changes mid-note
  discrete: () => ({ total: 46000, base: { gate: 200 }, scale: NDOWN,
    cfg: [{ at: 8000, set: { order: "down" } }, { at: 12000, set: { rate: "1/8" } }, { at: 16000, set: { pattern: "roll_end" } }, { at: 20000, set: { steps: 2 } }, { at: 21000, set: { step_size: 7 } }, { at: 24000, set: { restart: "bar" } },
      { at: 28000, set: { restart: "free" } }, { at: 32000, set: { quantize: "scale" } }, { at: 34000, set: { step_unit: "degrees" } }, { at: 36000, set: { vel_mode: "fixed", seed: 7 } }, { at: 40000, set: { restart: "chord", order: "shuffle" } }],
    ins: [K(100, "a", 61), K(100, "b", 64), K(100, "c", 66)] }),
  // SPEC 4.2 / 4.6 / 4.7: the four chord + note styles, a user pattern edited mid-run, converge, shuffle, transposition steps in both units, with quantize ties in play
  styles: () => ({ total: 44000, scale: NDOWN, userPattern: UP1, base: { order: "chord_walk", pattern: "user", gate: 120, steps: 2, step_size: 7 },
    cfg: [{ at: 8000, set: { order: "stride" } }, { at: 12000, set: { quantize: "scale" } }, { at: 16000, set: { step_unit: "degrees", step_size: 2 } }, { at: 14000, set: { order: "chord_between" } }, { at: 20000, set: { order: "pairs" } },
      { at: 26000, set: { order: "converge" } }, { at: 32000, set: { order: "shuffle", pattern: "tied_legato" } }, { at: 38000, set: { order: "chord_walk", pattern: "user" } }],
    userEdits: [{ at: 17003, p: { length: 5, step_size: 2, step_unit: "degrees", steps: [{ pick: "high" }, { tie: true }, { pick: "chord", ratchet: 3 }, {}, { pick: 2, trig: 1 }] } }],
    ins: [K(100, "a", 60), K(100, "b", 66), K(900, "c", 70), O(15000, "b"), K(16200, "d", 63), O(30000, "a"), O(30000, "c"), O(30000, "d"), K(33000, "e", 61), K(33400, "f", 69)] }),
  // SPEC 2.6: time mode. The host jumps and stops under it (ignored: no T6), the step time changes (a re-grid), rate_mode switches to sync and back
  // (each a T6 clock switch), restart bar counts from tick 0, and a user pattern with a non-12 step size (degrees) plays.
  time: () => {
    const host = X.simHost({ sr: 8000, bpm: 120, startBeats: 0, playing: true });
    host.set(9000, { seekBeats: 3 }); host.set(14000, { playing: false }); host.set(18000, { playing: true, seekBeats: 0, bpm: 90 });
    return { total: 40000, host, scale: NDOWN, userPattern: UP1, base: { rate_mode: "time", step_time_us: 50000, gate: 120, pattern: "gallop", steps: 1, step_size: 5 },
      cfg: [{ at: 12000, set: { step_time_us: 30000 } }, { at: 20000, set: { rate_mode: "sync" } }, { at: 24000, set: { rate_mode: "time", pattern: "user", quantize: "scale" } },
        { at: 30000, set: { restart: "bar", step_unit: "degrees", step_size: 2 } }, { at: 35000, set: { restart: "free", step_time_us: 80000 } }],
      ins: [K(100, "a", 60), K(100, "b", 66), O(16000, "a"), K(22000, "c", 62), O(26000, "b"), O(26000, "c"), K(27000, "d", 64), K(31000, "e", 70)] };
  },
  // T10 reset() mid-run (ids restart), then a new gesture
  reset: () => ({ total: 30000, base: { gate: 150 }, resets: [20000], ins: [K(100, "a", 60), K(100, "b", 64), K(21000, "c", 67)] }),
  // T11 the sounding table is full (a small table keeps the session short)
  overflow: () => ({ total: 12000, core: () => mkCore([], { caps: { sounding: 3 } }), base: { order: "chord", gate: 200 }, ins: [60, 62, 64, 65, 67, 69].map((k, i) => K(100, "k" + i, k)) }),
  // T16 keys outside 0-127, and a note shorter than one sample (SPEC 1.1 deferral): 999 bpm, 1/64, gate 1 %
  short: () => ({ total: 8000, bpm: 999, base: { rate: "1/64", gate: 1, steps: 2, order: "up" }, ins: [K(100, "a", 60), K(100, "b", 125)] }),
};
// A user pattern that uses every field: picks, a tie, a ratchet, a trigger, a rest, a probability and a step offset (a non-12 step size: +7 semitones).
const UP1 = { length: 8, step_size: 7, steps: [{ pick: "order" }, { pick: 3, ratchet: 2, vel: 130 }, { tie: true }, { pick: "low", gate: 50 }, { pick: "high", trig: 2 },
  { pick: "chord", prob: 80 }, { on: false }, { pick: "order", step: 1 }] };
const ROWS = {};
const addRows = c => { for (const [k, v] of Object.entries(c.rows)) ROWS[k] = (ROWS[k] || 0) + v; };
const runSess = (name, block, plants) => {
  const s = SESS[name]();
  return run(Object.assign({ block }, s, { core: s.core ? s.core() : mkCore(plants || []) }));
};
{
  // Plants first: each must make the N1 checker read red on a session where the clean core is green.
  plant(n1(runSess("held", 64, ["dropRelease"]).evs).length > 0, "N1 detects a dropped release");
  plant(n1(runSess("held", 64, ["doubleRelease"]).evs).length > 0, "N1 detects a doubled release");
  plant(n1(runSess("short", 64, ["sameSampleOff"]).evs).length > 0, "N1 detects a 1-tick note whose off lands at its own on sample");
  let bad = [], total = 0;
  for (const name of Object.keys(SESS)) for (const block of [64, 7]) {
    const r = runSess(name, block); total += r.evs.length;
    for (const b of n1(r.evs)) bad.push(`${name}@${block}: ${b}`);
    if (block === 64) addRows(r.core);
  }
  check(bad.length === 0, `N1: one terminal per note, >= 1 sample later, over ${Object.keys(SESS).length} sessions x 2 block sizes (${total} events)` + (bad.length ? " - " + bad.slice(0, 3).join("; ") : ""));
  // pass-through: non-note input events come out unchanged at their offsets, even while disabled
  const passOf = r => r.evs.filter(e => e.t === "pass").map(e => e.a + ":" + e.pass.value);
  const pe = passOf(runSess("enable", 64));
  check(pe.join() === "5000:100,10000:200", "pass-through: pitch-bend events arrive unchanged, also while disabled (" + pe.join() + ")");
  plant(pe.slice(1).join() !== "5000:100,10000:200", "pass-through comparison detects a dropped event");
  const pr = runSess("panic", 64).evs.filter(e => e.a === 15000 && (e.t === "off" || e.t === "panic")).map(e => e.t);
  check(pr.includes("panic") && pr.lastIndexOf("off") < pr.indexOf("panic"), "panic: releases first, then Panic is forwarded");
}

// ---- 4. G1: identical absolute samples at block sizes 1, 7, 64, 512, 4096 ---------------
function g1Session(plants, block) {
  const host = X.simHost({ sr: 8000, bpm: 120, startBeats: 0, playing: true });
  host.set(9500, { bpm: 133.7 }); host.set(15003, { bpm: 90.5 }); host.set(22001, { playing: false }); host.set(26007, { playing: true, seekBeats: 3 }); host.set(33000, { seekBeats: 0.5 });
  return run({ core: mkCore(plants), block, host, total: 52000, scale: NDOWN, userPattern: UP1, base: { order: "random", pattern: "roll_end", swing: 620, gate: 130, seed: 42, rate: "1/32" },
    // the tail adds the four chord + note styles, a user pattern (edited mid-step), converge, transposition steps in both units and quantize with real ties (F# between F and G)
    userEdits: [{ at: 40007, p: { length: 3, step_size: 5, step_unit: "degrees", steps: [{ pick: "chord" }, { pick: 2, ratchet: 2 }, { pick: "low", step: -1 }] } }],
    cfg: [{ at: 9001, set: { order: "shuffle", pattern: "sparse_chance" } }, { at: 18007, set: { rate: "1/16", latch: "replace" } }, { at: 30011, set: { order: "bounce", pattern: "tied_legato", rate: "1/32" } },
      { at: 34003, set: { order: "stride", pattern: "user", steps: 2, step_size: 7, quantize: "scale" } }, { at: 38501, set: { order: "converge", pattern: "none", step_unit: "degrees", step_size: 2 } }, { at: 44001, set: { order: "chord_walk", pattern: "user" } },
      { at: 46001, set: { order: "chord_between" } }, { at: 48003, set: { order: "pairs" } },
      { at: 49001, set: { rate_mode: "time", step_time_us: 40000 } }],
    ins: [K(37, "a", 60), K(41, "b", 64), K(3011, "c", 67), O(12003, "a"), { at: 13000, t: "pedal", down: true }, O(14001, "b"), K(17777, "d", 72), O(19000, "c"), { at: 24000, t: "pedal", down: false }, K(25100, "e", 55), K(36000, "f", 59), { at: 38000, t: "panic" }, K(38500, "g", 62), K(41000, "h", 66), O(45000, "h"), K(47000, "i", 69)] });
}
function g1Diverges(plants) {
  let ref = null;
  for (const b of [4096, 512, 64, 7, 1]) {
    let s;
    try { s = streamOf(g1Session(plants, b).evs); } catch (e) { return "threw: " + e.message; }
    if (ref === null) ref = s; else if (s !== ref) return `block ${b} differs`;
  }
  return null;
}
{
  plant(g1Diverges(["clampOffset"]) !== null, "G1 detects offsets clamped into the block: " + g1Diverges(["clampOffset"]));
  plant(g1Diverges(["floatPosition"]) !== null, "G1 detects a position accumulated in float: " + g1Diverges(["floatPosition"]));
  const r = g1Diverges([]);
  const g1 = g1Session([], 64), n = g1.evs.length;
  const qd = g1.evs.filter(e => e.t === "on" && e.a >= 36000);   // steps evaluated well after quantize switched on
  check(r === null && n > 100 && qd.length > 20 && qd.every(e => inScale(e.key, 0, 0xAB5)), `G1: ${n} events, absolute samples identical at block sizes 4096/512/64/7/1 (swing, ratchets, prob, tempo change, stop/start, jumps, latch, pedal, panic, quantize ties, user pattern, the new styles, transposition steps in semitones and degrees, step offsets with a non-12 step size in both units, time mode)` + (r ? " - " + r : ""));
}

// ---- 5. G2: ons land at cdiv of their exact tick position --------------------------------
{
  const sess = plants => run({ core: mkCore(plants), block: 512, total: 60000, bpm: 133.7, base: { gate: 50 }, ins: [K(0, "a", 60)] });
  const exp = () => { const D = D_OF(8000), inc = 133700n * 15360n, out = []; for (let k = 0; k < 24; k++) { const x = BigInt(k * 3840) * D; out.push(Number((x + inc - 1n) / inc)); } return out; };
  const bad = plants => { const got = ons(sess(plants)).map(e => e.a).slice(0, 24), e = exp(); return got.length !== 24 || got.some((v, i) => v !== e[i]); };
  plant(bad(["roundNotCeil"]), "G2 detects round() in place of ceil in the offset");
  check(!bad([]), "G2: the first 24 ons at 133.7 bpm sit at cdiv(tick * D, inc) exactly");
}

// ---- 6. G3: swing parity follows kAbs, not k --------------------------------------------------
{
  const sess = plants => run({ core: mkCore(plants), block: 64, total: 30000, base: { swing: 667, gate: 50 }, ins: [K(2500, "a", 60)] });
  const exp = () => { const D = D_OF(8000), inc = 120000n * 15360n, dl = Math.floor(3840 * (2 * 667 - 1000) / 1000), out = []; for (let k = 3; k < 12; k++) { const x = BigInt(k * 3840 + (k % 2 ? dl : 0)) * D; out.push(Number((x + inc - 1n) / inc)); } return out; };
  const bad = plants => { const got = ons(sess(plants)).map(e => e.a).slice(0, 9), e = exp(); return got.length < 9 || got.some((v, i) => v !== e[i]); };
  plant(bad(["swingParityK"]), "G3 detects swing parity taken from k under restart=chord (first step on an odd kAbs)");
  check(!bad([]), "G3: with the chord anchored on odd kAbs 3, the swung steps are still the odd kAbs (swing 667 permille)");
}

// ---- 7. G4: the tick of every event is identical at 44.1 / 48 / 96 / 192 kHz ----------------
{
  const sess = (plants, sr) => run({ core: mkCore(plants), sr, block: 1024, total: sr * 3, base: { pattern: "roll_end", swing: 600, gate: 80, order: "up" },
    ins: [K(0, "a", 60), K(0, "b", 64), K(0, "c", 67), O(sr * 2, "b")] });
  const diverges = plants => { const ref = tickStreamOf(sess(plants, 48000).evs); return [44100, 96000, 192000].some(sr => tickStreamOf(sess(plants, sr).evs) !== ref); };
  plant(diverges(["srTickMath"]), "G4 detects a sample-rate-dependent value read into tick math");
  const ref = sess([], 48000).evs;
  check(!diverges([]) && ref.length > 60, `G4: ${ref.length} events carry identical ticks at 44.1, 48, 96 and 192 kHz`);
}

// ---- 8. G5: under +-1/32-beat host jitter every grid step is evaluated exactly once ---------
{
  const lcg = s => (Math.imul(s, 1664525) + 1013904223) >>> 0;
  const jitter = () => { let s = 12345; return pos => { s = lcg(s ^ pos); return (s % 961) - 480; }; };   // +-480 ticks = +-1/32 beat
  const sess = plants => run({ core: mkCore(plants), block: 64, total: 40000, jitter: jitter(), base: { gate: 80 }, ins: [K(0, "a", 60)] });
  const once = r => { const ks = r.steps.map(s => s.kAbs); return ks.length >= 30 && ks.every((k, i) => i === 0 || k === ks[i - 1] + 1); };
  plant(!once(sess(["noOnceGuard"])), "G5 detects a dropped once-guard (a step evaluated twice or skipped under jitter)");
  const r = sess([]);
  check(once(r) && n1(r.evs).length === 0, `G5: ${r.steps.length} grid steps evaluated exactly once, consecutively, under jittered re-base; N1 holds`);
}

// ---- 9. D1: same seed -> identical stream; different seed changes random / shuffle / prob ----
{
  const keys = [60, 62, 64, 67, 71].map((k, i) => K(0, "k" + i, k));
  const sess = (plants, seed, extra) => streamOf(run({ core: mkCore(plants), block: 256, total: 40000, base: Object.assign({ seed }, extra), ins: keys }).evs);
  const cases = { random: { order: "random" }, shuffle: { order: "shuffle" }, prob: { pattern: "sparse_chance" } };
  plant(Object.values(cases).some(c => sess(["timeInDraw"], 1, c) !== sess(["timeInDraw"], 1, c)), "D1 detects a time-varying value in the draw (same seed, different stream)");
  const same = Object.entries(cases).filter(([, c]) => sess([], 1, c) !== sess([], 1, c)).map(([n]) => n);
  const diff = Object.entries(cases).filter(([, c]) => sess([], 1, c) === sess([], 2, c)).map(([n]) => n);
  check(same.length === 0 && diff.length === 0, "D1: same seed -> identical stream; seed 1 vs 2 differs for random, shuffle and probability" + (same.length ? " - unstable: " + same : "") + (diff.length ? " - seed-blind: " + diff : ""));
}

// ---- 10. Q1-Q4: quantize in horde's scale schema (SPEC §6, D-010) ----------------------------
// The mask is ROOT-RELATIVE (degree i = i semitones above the root), as horde's quantise() reads it.
const MAJOR_DEGS = { scale_deg0: true, scale_deg1: false, scale_deg2: true, scale_deg3: false, scale_deg4: true, scale_deg5: true,
                     scale_deg6: false, scale_deg7: true, scale_deg8: false, scale_deg9: true, scale_deg10: false, scale_deg11: true };
{ // Q1: every quantized key is in the (global) scale and in 0-127
  const keys = [61, 63, 66, 70, 125, 127].map((k, i) => K(0, "k" + i, k));
  const sess = (plants, order, pattern, unit) => run({ core: mkCore(plants), block: 256, total: 40000, scale: NDOWN, base: { quantize: "scale", steps: 2, step_size: 5, step_unit: unit || "semitones", order, pattern: pattern || "none" }, ins: keys });
  const bad = plants => ["up", "chord", "random", "bounce"].some(o => ons(sess(plants, o)).concat(ons(sess(plants, o, "octave_leap")), ons(sess(plants, o, "none", "degrees"))).some(e => e.key < 0 || e.key > 127 || !inScale(e.key, 0, 0xAB5)));
  // the EFFECTIVE scale (SPEC §11 Q1, r2.6): under chord_root it is the global shape anchored on the lowest held key (C# here), whose own pitch class is always admitted
  const badEff = plants => ons(run({ core: mkCore(plants), block: 256, total: 20000, scale: NDOWN, base: { quantize: "chord_root", order: "chord", steps: 1 }, ins: [61, 64, 66].map((k, i) => K(0, "k" + i, k)) }))
    .some(e => e.key < 0 || e.key > 127 || !inScale(e.key, 1, 0xAB5 | 1));
  plant(bad(["skipQuantizeChord"]) && badEff(["skipQuantizeChord"]), "Q1 detects quantize skipped on chord steps (global and chord_root scales)");
  check(!bad([]) && !badEff([]), "Q1: every quantized key is in the effective scale (global C major; chord_root anchored on the lowest held key) and in 0-127 (chord, random, bounce, octave offsets)");
}
{ // Q2: a tie goes toward the PREVIOUS quantized output (horde's rule); the lower key when there is none
  // Held C#4 (61, a tie between 60 and 62) and A4 (69), order up: the first 61 has no previous -> 60;
  // every later 61 follows a 69, and 62 is nearer 69 than 60 is -> 62.
  const seq = plants => ons(run({ core: mkCore(plants), block: 128, total: 30000, scale: NDOWN, base: { quantize: "scale", order: "up" }, ins: [K(0, "a", 61), K(0, "b", 69)] })).map(e => e.key);
  const bad = plants => { const q = seq(plants); const c = q.filter((k, i) => i % 2 === 0); return q.length < 6 || c[0] !== 60 || c.slice(1).some(k => k !== 62); };
  plant(bad(["tieAlwaysDown"]), "Q2 detects ties always resolved downward");
  check(!bad([]), `Q2: ties resolve toward the previous output: ${seq([]).slice(0, 6).join(" ")} (first 61 -> 60 with nothing before it; later 61 -> 62 after 69)`);
}
{ // Q3: scale source = own uses Arp's scale_root + scale_deg* (E-flat major) over horde's global C major
  const sess = plants => run({ core: mkCore(plants), block: 256, total: 30000, scale: NDOWN, base: Object.assign({ quantize: "scale", scale_source: "own", scale_root: 3, steps: 1, order: "up" }, MAJOR_DEGS), ins: [64, 67, 71, 72].map((k, i) => K(0, "k" + i, k)) });
  const bad = plants => { const o = ons(sess(plants)); return o.length < 8 || o.some(e => !inScale(e.key, 3, 0xAB5)); };
  plant(bad(["ownIgnored"]), "Q3 detects the own scale ignored (the global scale used instead)");
  check(!bad([]), "Q3: with scale source = own, every key is in Arp's own E-flat major, not horde's global C major");
}
{ // Q4: chord_root anchors the scale SHAPE on the lowest held key (horde's movable-do); the root setting is ignored
  const sess = plants => run({ core: mkCore(plants), block: 256, total: 30000, scale: NDOWN, base: Object.assign({ quantize: "chord_root", scale_source: "own", scale_root: 0, steps: 1, order: "up" }, MAJOR_DEGS), ins: [62, 65, 69].map((k, i) => K(0, "k" + i, k)) });
  const bad = plants => { const o = ons(sess(plants)); return o.length < 8 || o.some(e => !inScale(e.key, 2, 0xAB5)); };
  plant(bad(["chordRootAsRoot"]), "Q4 detects chord_root anchored on the root setting instead of the lowest held key");
  check(!bad([]), "Q4: chord_root over D F A puts every key in D major (the shape anchored on D, root setting C ignored)");
}

// ---- 11. R1: rests, misses, ties and trigger-only steps do not advance m ----------------------
{
  const bank = { step_defaults: X.ARP_BANK.step_defaults, patterns: [
    ...X.ARP_BANK.patterns,
    { id: "p50", name: "p50", length: 1, steps: [{ prob: 50 }] },
    { id: "trigmix", name: "trigmix", length: 2, steps: [{ on: false, trig: 2 }, {}] }] };
  const keys = [60, 64, 67].map((k, i) => K(0, "k" + i, k));
  const seq = (plants, pattern) => ons(run({ core: mkCore(plants, { bank }), block: 512, total: 60000, base: { pattern }, ins: keys })).map(e => e.key);
  const ok = (plants, pattern) => { const s = seq(plants, pattern); return s.length >= 6 && s.every((k, i) => k === [60, 64, 67][i % 3]); };
  const pats = ["offbeat", "tied_legato", "p50", "trigmix", "gallop"];
  plant(!pats.every(p => ok(["advanceOnRest"], p)), "R1 detects m advanced on rests");
  check(pats.every(p => ok([], p)), "R1: the up-cycle stays contiguous across rests (offbeat, gallop), ties (tied_legato), probability misses (p50) and trigger-only steps (trigmix)");
}

{ // R1 (extended): an explicit `pick` plays its note WITHOUT consuming the order stream (SPEC 4.3)
  const bank = { step_defaults: X.ARP_BANK.step_defaults, patterns: [...X.ARP_BANK.patterns, { id: "pk", name: "pk", length: 4, steps: [{}, { pick: 1 }, {}, { pick: "high" }] }] };
  const keys = [60, 64, 67].map((k, i) => K(0, "k" + i, k));
  const got = plants => ons(run({ core: mkCore(plants, { bank }), block: 512, total: 60000, base: { pattern: "pk" }, ins: keys })).map(e => e.key);
  // reference: the order stream (up over 60 64 67) advances only on the two `order` steps of the four
  const want = n => { const o = []; let i = 0; for (let s = 0; o.length < n; s++) { if (s % 4 === 0 || s % 4 === 2) o.push([60, 64, 67][i++ % 3]); else o.push(s % 4 === 1 ? 60 : 67); } return o; };
  const bad = plants => { const g = got(plants); return g.length < 16 || g.slice(0, 16).join() !== want(16).join(); };
  plant(bad(["pickAdvancesM"]), "R1 detects m advanced on explicit picks");
  check(!bad([]), "R1 (extended): explicit-pick steps leave m alone: " + got([]).slice(0, 8).join(" ") + " ... (order stream 60 64 67 resumes where it left off)");
}

// ---- 11b. U1: the user pattern (SPEC 4.6, T17) ------------------------------------------------------------
{
  const K3 = [100, 101, 102].map((at, i) => K(at, "k" + i, [60, 64, 67][i]));
  const byTick = r => { const m = new Map(); for (const e of ons(r)) m.set(e.tick / 3840, (m.get(e.tick / 3840) || []).concat(e.key).sort((a, b) => a - b)); return m; };
  const U = [{ pick: 1 }, { pick: 3 }, { pick: 5 }, { pick: "low" }, { pick: "high" }, { pick: "chord" }, { on: false }, { pick: "order" }];
  // (a) picks and rhythm. The chord anchors at kAbs 1 (the first step after the presses), step j = (kAbs - 1) mod 8.
  // Reference: 1 -> E[0], 3 -> E[2], 5 -> E[mod(4,3)] = E[1], low, high, chord, rest, then the up stream (60 64 67 ...) on the order steps.
  const refA = () => { const m = new Map(); let up = 0; for (let k = 1; k < 40; k++) { const j = (k - 1) % 8; const keys = j === 7 ? [[60, 64, 67][up++ % 3]] : [[60], [67], [64], [60], [67], [60, 64, 67], []][j]; if (keys.length) m.set(k, keys); } return m; };
  const sameMap = (a, b, upTo) => { for (let k = 0; k < upTo; k++) if ((a.get(k) || []).join() !== (b.get(k) || []).join()) return false; return true; };
  const badA = plants => { const r = run({ core: mkCore(plants), block: 64, total: 40 * 1000, userPattern: { length: 8, steps: U }, base: { pattern: "user", gate: 50 }, ins: K3 }); return !sameMap(byTick(r), refA(), 38) || n1(r.evs).length > 0; };
  plant(badA(["ignorePick"]), "U1 detects pick ignored (every step read as order)");
  plant(badA(["prepareResetsUser"]), "U1 detects a user pattern wiped by prepare (set before prepare, SPEC 4.6 r2 H3)");
  check(!badA([]), "U1: the user pattern plays picks 1, 3, 5 (cyclic), low, high, chord, a rest and an order step, in that rhythm, with the pattern set BEFORE prepare");
  // (b) edits between blocks take effect at the next grid step: A = default plain `order` (up stream), then B = all `high` from sample 4300
  //     (step 5 is the first evaluated after it); two calls at one sample leave the LATER one (all pick 1) from step 9.
  const all = (n, pick) => ({ length: n, steps: Array.from({ length: n }, () => ({ pick })) });
  const edits = [{ at: 4300, p: all(4, "high") }, { at: 8100, p: all(2, "high") }, { at: 8100, p: all(2, 1) }];
  const badB = plants => {
    const r = run({ core: mkCore(plants), block: 4096, total: 14000, userEdits: edits, base: { pattern: "user", gate: 50 }, ins: K3 }), m = byTick(r);
    const want = { 1: 60, 2: 64, 3: 67, 4: 60, 5: 67, 6: 67, 7: 67, 8: 67, 9: 60, 10: 60, 11: 60, 12: 60, 13: 60 };
    return Object.entries(want).some(([k, v]) => (m.get(Number(k)) || []).join() !== String(v));
  };
  plant(badB(["userFirstWins"]), "U1 detects two writes between steps resolved to the FIRST (latest must win)");
  check(!badB([]), "U1: setUserPattern between blocks applies from the next grid step; of two writes at one sample the later wins");
  // (c) an edit landing DURING a step's evaluation must not tear that step: A = [note, tie, note, note] (the note's off is extended through the tie by look-ahead);
  //     B has no tie. The first note must keep A's tied off (6912 ticks at gate 80), T17.
  class Inj extends X.ArpCore { _play(...a) { if (!this._inj && a[1] === 0) { this._inj = true; this.setUserPattern({ length: 4, steps: [{}, {}, {}, {}] }); } return super._play(...a); } }
  const A4 = { length: 4, steps: [{}, { tie: true }, {}, {}] };
  const sessC = plants => { const c = new Inj(); for (const p of plants) c.lab.plant[p] = true; return run({ core: c, block: 64, total: 12000, userPattern: A4, base: { pattern: "user", gate: 80 }, ins: K3 }); };
  const badC = plants => { const r = sessC(plants), o = ons(r)[0], off = r.evs.find(e => e.t === "off" && e.id === o.id); return off.tick - o.tick !== 6912 || n1(r.evs).length > 0 || ons(r).filter(e => e.tick === o.tick + 3840).length === 0; };
  plant(badC(["swapMidStep"]), "U1 detects an edit applied mid-step (the step's tie look-ahead tears)");
  check(!badC([]), "U1/T17: an edit landing mid-evaluation leaves that step whole (tied off 6912 ticks kept) and the next step uses the new pattern; N1 holds");
  // (d) validation: clamps every field, unknown pick -> order, length 1-16; getUserPattern returns the writer's latest copy
  const clampOk = want => { const c = mkCore(); c.setUserPattern({ length: 99, step_size: -99, step_unit: "bogus", steps: [{ vel: 999, gate: 0, step: 9, prob: -4, ratchet: 9, trig: 12, pick: "bogus" }, { pick: "9" }, { pick: "7" }] });
    const g = c.getUserPattern(); g.steps[0].vel += want; return g.length === 16 && g.steps.length === 16 && g.steps[0].vel === 200 && g.steps[0].gate === 1 && g.steps[0].step === 8 && g.step_size === -48 && g.step_unit === "semitones" && g.steps[0].prob === 0 && g.steps[0].ratchet === 4 && g.steps[0].trig === 8 && g.steps[0].pick === "order" && g.steps[1].pick === "order" && g.steps[2].pick === 7; };
  plant(!clampOk(1), "U1 validation check can go red (a perturbed expectation fails)");
  check(clampOk(0), "U1: setUserPattern clamps length 1-16, the pattern's step size / unit and every field (step -8..8), reads an unknown pick as order; getUserPattern returns the stored copy");
}

// ---- 11d. N2: shuffle never repeats a key back to back, and plays every key once per cycle (SPEC 4.2, D-012a) --------
{
  const sess = (plants, e, seed) => ons(run({ core: mkCore(plants), sr: 8000, bpm: 999, block: 4096, total: 250 * 1100, base: { order: "shuffle", rate: "1/64", gate: 20, seed }, ins: [60, 62, 64, 67, 71].slice(0, e).map((k, i) => K(0, "k" + i, k)) })).map(x => x.key);
  const bad = plants => { for (const e of [2, 3, 5]) for (const seed of [1, 2, 3, 4, 5, 6]) {
    const seq = sess(plants, e, seed), keys = [60, 62, 64, 67, 71].slice(0, e);
    if (seq.length < 200 * e) return true;
    for (let i = 1; i < seq.length; i++) if (seq[i] === seq[i - 1]) return true;
    for (let c = 0; c + e <= seq.length; c += e) if (seq.slice(c, c + e).sort((x, y) => x - y).join() !== keys.join()) return true;
  } return false; };
  plant(bad(["shuffleBoundaryRepeat"]), "N2 detects a back-to-back repeat across a cycle boundary (boundary swap skipped)");
  check(!bad([]), "N2: over >= 200 cycles x 6 seeds, shuffle (e = 2, 3, 5) never repeats a key back to back and plays each key once per cycle");
}

// ---- 11e. Q5: tie direction, lastQ reset at release-all, collisions (SPEC 6, r2.6) ---------------------------------
{
  // (a) a tie with lastQ ABOVE it resolves UP. Chord over C#4 (61) and A4 (69): ascending, 61 first; step 0 has no lastQ -> 60, then every step's 69
  //     leaves lastQ = 69 so 61 -> 62 (Q2's up-cycle checks the same through a single-note order).
  const chordSeq = plants => { const r = run({ core: mkCore(plants), block: 128, total: 30000, scale: NDOWN, base: { quantize: "scale", order: "chord" }, ins: [K(0, "a", 61), K(0, "b", 69)] }), m = new Map();
    for (const e of ons(r)) m.set(e.tick, (m.get(e.tick) || []).concat(e.key).sort((x, y) => x - y)); return [...m.values()].map(v => v.join()); };
  const badA = plants => { const q = chordSeq(plants); return q.length < 5 || q[0] !== "60,69" || q.slice(1).some(v => v !== "62,69"); };
  plant(badA(["tieAlwaysDown"]), "Q5 detects ties always resolved downward (lastQ above the tie)");
  check(!badA([]), `Q5: with lastQ above the tie it resolves UP: ${chordSeq([]).slice(0, 4).join(" | ")}`);
  // (b) lastQ resets at release-all: 69 sounds, Panic, then C# alone: with nothing remembered the tie goes DOWN (60); a remembered 69 would pull it up to 62.
  const afterPanic = plants => { const r = run({ core: mkCore(plants), block: 128, total: 14000, scale: NDOWN, base: { quantize: "scale" }, ins: [K(100, "a", 69), { at: 6000, t: "panic" }, K(9000, "b", 61)] });
    return ons(r).filter(e => e.a > 9000)[0].key; };
  plant(afterPanic(["lastQNoReset"]) !== 60, "Q5 detects lastQ never reset at release-all (the tie after Panic follows the old 69)");
  check(afterPanic([]) === 60, "Q5: lastQ resets at release-all (T7): the first tie after Panic resolves down to 60");
  // (c) a collision: C and C# in C major both land on C (the tie follows lastQ = 60); emitted once, with the EARLIEST source's velocity (C, 40)
  const coll = plants => { const r = run({ core: mkCore(plants), block: 128, total: 20000, scale: NDOWN, base: { quantize: "scale", order: "chord" }, ins: [K(0, "a", 60, { vel: 40 }), K(0, "b", 61, { vel: 90 })] }); return ons(r).map(e => `${e.key}@${e.vel}`); };
  const badC = plants => { const c = coll(plants); return c.length < 5 || c.some(v => v !== "60@40"); };
  plant(badC(["collisionKeepsLast"]), "Q5 detects a collision resolved to the LAST source's velocity");
  check(!badC([]), "Q5: two chord keys quantizing to one key emit it once, with the earliest source's velocity (60@40)");
}

// ---- 12. P1: measured probability within +-2 % over 10,000 steps ----------------------------
{
  const bank = { step_defaults: X.ARP_BANK.step_defaults, patterns: [{ id: "none", length: 1, steps: [{}] }, ...[0, 30, 50, 100].map(p => ({ id: "p" + p, name: "p" + p, length: 1, steps: [{ prob: p }] }))] };
  const measure = (plants, p) => {
    const r = run({ core: mkCore(plants, { bank }), block: 4096, total: 10000 * 1000, base: { pattern: "p" + p, gate: 20 }, ins: [K(0, "a", 60)], finalReset: false });
    return { steps: r.steps.length, hit: ons(r).length };
  };
  // A +-2 % band cannot see a 1 % off-by-one on its own (stderr ~0.5 %), so the endpoints are exact too: prob 0 never fires, 100 always.
  const bad = plants => [0, 30, 50, 100].some(p => { const m = measure(plants, p); return m.steps !== 10000 || Math.abs(100 * m.hit / m.steps - p) > 2 || (p === 0 && m.hit !== 0) || (p === 100 && m.hit !== m.steps); });
  plant(bad(["probLE"]), "P1 detects <= in place of < (prob 0 fires)");
  const m30 = measure([], 30);
  check(!bad([]), `P1: over ${m30.steps} steps the measured rate is within 2 % of prob 0/30/50/100 (30 -> ${(100 * m30.hit / m30.steps).toFixed(2)} %); endpoints exact`);
}

// ---- 13. T1: triggers fire once per firing step, at the first on, never per ratchet ----------
{
  const bank = { step_defaults: X.ARP_BANK.step_defaults, patterns: [{ id: "none", length: 1, steps: [{}] }, { id: "tr", name: "tr", length: 1, steps: [{ trig: 3, ratchet: 4, prob: 60 }] }] };
  const sess = plants => run({ core: mkCore(plants, { bank }), block: 128, total: 60000, base: { pattern: "tr", gate: 50 }, ins: [K(0, "a", 60)] });
  const bad = plants => {
    const r = sess(plants), D = D_OF(8000), inc = 120000n * 15360n;
    const want = r.steps.filter(s => s.fires).map(s => Number((BigInt(s.kAbs * 3840) * D + inc - 1n) / inc));
    const got = r.evs.filter(e => e.t === "trig").map(e => e.a);
    return want.length < 5 || want.join() !== got.join() || r.evs.some(e => e.t === "trig" && e.lane !== 3);
  };
  plant(bad(["trigEveryRatchet"]), "T1 detects a trigger fired on every ratchet");
  const r = sess([]);
  check(!bad([]), `T1: ${r.evs.filter(e => e.t === "trig").length} triggers, exactly one per firing step, at the step's first on, none on the extra ratchets`);
}

// ---- 13b. S1: every emitted NoteOn is on the `arp` stream (SPEC §1, D-009, horde ADR-270) -------
// The plant tags most ons and drops the tag on a few, so the detector must check EVERY on, not one.
{
  const sess = plants => run({ core: mkCore(plants), block: 64, total: 60000, base: { order: "bounce", steps: 2, step_size: 7, pattern: "roll_end" }, ins: [K(0, "a", 60), K(0, "b", 64), K(0, "c", 67)] });
  const bad = plants => { const ons = sess(plants).evs.filter(e => e.t === "on"); return ons.length < 20 || ons.some(e => e.stream !== "arp"); };
  plant(bad(["untaggedOn"]), "S1 detects a NoteOn emitted without the arp stream tag");
  check(!bad([]), `S1: all ${sess([]).evs.filter(e => e.t === "on").length} NoteOns carry stream "arp" (bounce, 2 transposition steps, ratchets)`);
}

// ---- 14. K1: ties engage at gate 80 by look-ahead ----------------------------------------------
{
  const sess = (plants, pattern) => run({ core: mkCore(plants), block: 256, total: 40000, base: { pattern, gate: 80 }, ins: [K(0, "a", 60)] });
  const durs = (plants, pattern) => { const r = sess(plants, pattern), off = new Map(r.evs.filter(e => e.t === "off").map(e => [e.id, e.tick])); return ons(r).map(e => [e.tick, off.get(e.id) - e.tick]); };
  const want = [[0, 6912], [7680, 6912], [15360, 3072], [19200, 10752]];   // SPEC 5.2: off = on(k+t) + fdiv(span*gate*steps[j+t].gate, 10000)
  const bad = plants => { const d = durs(plants, "tied_legato"); return want.some((w, i) => d[i] === undefined || d[i][0] !== w[0] || d[i][1] !== w[1]) || d[4][0] !== 30720; };
  plant(bad(["tieAtTieStep"]), "K1 detects ties evaluated at the tie step (tied_legato collapses to plain notes at gate 80)");
  const plain = durs([], "none");
  check(!bad([]) && plain.every(d => d[1] === 3072), "K1: tied_legato at gate 80 yields held notes 6912/6912/3072/10752 ticks, tie steps emit nothing; plain stays 3072");
}

// ---- 15. C1: at the event cap no NoteOn is emitted without its NoteOff ----------------------------
{
  const keys = Array.from({ length: 32 }, (_, i) => K(0, "k" + i, 36 + i));
  const sess = plants => run({ core: mkCore(plants, { caps: { events: 400 } }), sr: 8000, bpm: 999, maxBlock: 128, block: 128, total: 128 * 40, base: { order: "chord", rate: "1/64", pattern: "roll_end", gate: 200 }, ins: keys });
  const bad = plants => { try { return n1(sess(plants).evs).length > 0; } catch (e) { return true; } };
  plant(bad(["noOffReserve"]), "C1 detects a dropped off-reserve (ons emitted whose offs no longer fit)");
  const r = sess([]); addRows(r.core);
  check(!bad([]) && r.core.counters.refused > 0 && r.core.rows.T13 > 0, `C1: ${r.core.counters.refused} NoteOns refused at the cap (T13), every emitted on has its off, no block above the cap`);
}

// ---- 15b. N1 row coverage: every row T1-T16 really acted in the scripted sessions (a row that never fired proves nothing)
{
  const miss = Object.keys(ROWS).filter(k => !ROWS[k]);
  check(miss.length === 0 && Object.keys(ROWS).length === 16, "N1 coverage: " + Object.entries(ROWS).map(([k, v]) => k + "=" + v).join(" ") + (miss.length ? " - NEVER ACTED: " + miss.join(",") : ""));
}

// ---- 16. Order styles: period and first cycle match SPEC 4.2 for e = 1, 2, 3, 5 ---------------------
{
  // Expected sequences are built by CONSTRUCTION (two-pointer, interleave, reverse), not from the spec's closed forms.
  const seqs = e => {
    const up = [...Array(e).keys()], rev = up.slice().reverse();
    const conv = []; for (let lo = 0, hi = e - 1; lo <= hi;) { conv.push(lo++); if (lo <= hi) conv.push(hi--); }
    return {
      up, down: rev, bounce: e === 1 ? [0] : [...up, ...rev.slice(1, -1)], bounce2: [...up, ...rev],
      converge: conv, diverge: conv.slice().reverse(),
      pedal_low: e === 1 ? [0] : up.slice(1).flatMap(x => [0, x]),
      pedal_high: e === 1 ? [0] : rev.slice(1).flatMap(x => [e - 1, x]),
    };
  };
  const PITCH = { 1: [60], 2: [60, 64], 3: [60, 64, 67], 5: [60, 62, 64, 67, 71] };
  const play = (order, keys, extra) => run({ core: mkCore(), block: 512, total: 40000, base: Object.assign({ order, gate: 50 }, extra), ins: keys.map((k, i) => K(100 + i, "k" + i, k)) });
  const wrong = [];
  for (const e of [1, 2, 3, 5]) {
    const keys = PITCH[e];
    for (const [order, idx] of Object.entries(seqs(e))) {
      const got = ons(play(order, keys)).map(x => x.key), want = idx.map(i => keys[i]);
      const per = want.length;
      if (got.length < 3 * per || !got.slice(0, 3 * per).every((k, i) => k === want[i % per])) wrong.push(`${order}/e${e}`);
    }
    // played: press order, not pitch order
    const pressed = keys.slice().reverse(), got = ons(play("played", pressed)).map(x => x.key);
    if (!got.slice(0, 3 * e).every((k, i) => k === pressed[i % e])) wrong.push(`played/e${e}`);
    // chord: every key at once, period 1; with 1 step of +12 the second pass is the chord an octave up
    const ch = play("chord", keys, { steps: 1 }), byA = new Map(); for (const x of ons(ch)) byA.set(x.a, (byA.get(x.a) || []).concat(x.key));
    const cyc = [...byA.values()].slice(0, 6);
    if (!cyc.every((c, i) => c.join() === keys.map(k => k + 12 * (i % 2)).join())) wrong.push(`chord/e${e}`);
    // random: only held keys; shuffle: every e-block is a permutation
    const rnd = ons(play("random", keys)).map(x => x.key);
    if (rnd.some(k => !keys.includes(k)) || (e > 1 && new Set(rnd).size !== e)) wrong.push(`random/e${e}`);
    const shf = ons(play("shuffle", keys)).map(x => x.key);
    for (let c = 0; c + e <= Math.min(shf.length, 10 * e); c += e) if (shf.slice(c, c + e).slice().sort((x, y) => x - y).join() !== keys.join()) wrong.push(`shuffle/e${e}`);
  }
  check(wrong.length === 0, "order styles: all 12 match SPEC 4.2 (period and first cycle) for e = 1, 2, 3, 5 (E = the held keys, no octave copies)" + (wrong.length ? " - wrong: " + wrong.join(" ") : ""));
}

// ---- 16b. The four chord + note styles (SPEC 4.2, D-011): period and first cycle for e = 1, 2, 3, 5 ------------------
// Expected steps are built by CONSTRUCTION from the spec's prose, not from its closed forms. Each step is a list of keys; H is in pitch order.
// There are no octave copies (r2.7): E is the held keys, and range comes from transposition passes (X1 below).
const baseCycle = (style, H) => {
  const E = H.slice(), up = E, rev = E.slice().reverse();
  if (style === "chord_walk") return [H, ...E.map(k => [k])];                                   // the chord, then each note of E
  if (style === "chord_between") return E.flatMap(k => [H, [k]]);                               // chord, note, chord, note ...
  if (style === "pairs") return E.length === 1 ? [[E[0]]] : E.slice(0, -1).map((k, i) => [k, E[i + 1]]);   // adjacent dyads
  if (style === "stride") return [[H[0]], H.length > 1 ? H.slice(1) : [H[0] + 12]];             // bass, then the rest of the chord (one key held: that key an octave up)
  if (style === "chord") return [H];
  const conv = []; for (let lo = 0, hi = E.length - 1; lo <= hi;) { conv.push(E[lo++]); if (lo <= hi) conv.push(E[hi--]); }
  const e = E.length;
  return { up, down: rev, bounce: e === 1 ? [E[0]] : [...up, ...rev.slice(1, -1)], bounce2: [...up, ...rev], converge: conv, diverge: conv.slice().reverse(),
    pedal_low: e === 1 ? [E[0]] : up.slice(1).flatMap(x => [E[0], x]), pedal_high: e === 1 ? [E[0]] : rev.slice(1).flatMap(x => [E[e - 1], x]) }[style].map(k => [k]);
};
{
  const PITCH = { 1: [60], 2: [60, 64], 3: [60, 64, 67], 5: [60, 62, 64, 67, 71] };
  const play = (style, H) => run({ core: mkCore(), block: 512, total: 60000, base: { order: style, gate: 50 }, ins: H.map((k, i) => K(100 + i, "k" + i, k, { vel: 60 + 7 * i })) });
  const srcVel = (H, k) => 60 + 7 * H.indexOf(k);   // a note takes the velocity of the held key it came from
  const wrong = (mutate) => {
    const bad = [];
    for (const style of ["chord_walk", "stride", "chord_between", "pairs"]) for (const e of [1, 2, 3, 5]) {
      const H = PITCH[e], want = baseCycle(style, H), per = want.length, r = play(style, H), byTick = new Map();
      for (const x of ons(r)) byTick.set(x.tick, (byTick.get(x.tick) || []).concat(x));
      const steps = [...byTick.entries()].sort((a, b) => a[0] - b[0]).map(([, v]) => v.sort((a, b) => a.key - b.key));
      const ref = mutate ? mutate(want) : want;
      // stride with one key: the rest is that key +12, whose source velocity is the held key's
      const ok = steps.length >= 3 * per && steps.slice(0, 3 * per).every((st, i) => st.map(x => x.key).join() === ref[i % per].slice().sort((a, b) => a - b).join() && st.every(x => x.vel === srcVel(H, H.includes(x.key) ? x.key : x.key - 12)));
      if (!ok) bad.push(`${style}/e${e}`);
    }
    return bad;
  };
  plant(wrong(w => w.slice(1).concat(w.slice(0, 1))).length > 0, "the chord + note reference check detects a cycle rotated by one step");
  const w = wrong();
  check(w.length === 0, "order styles: chord_walk, stride (period 2, no octave cycling), chord_between and pairs match SPEC 4.2 (period, first cycle, source velocities) for e = 1, 2, 3, 5" + (w.length ? " - wrong: " + w.join(" ") : ""));
}

// ---- 16c. X1: transposition steps (SPEC 4.7, D-014) ---------------------------------------------------------------
{
  const C3 = [60, 64, 67].map((k, i) => K(0, "k" + i, k));
  const keysOf = (r, n) => ons(r).map(e => e.key).slice(0, n);
  const run1 = (plants, base, extra) => run(Object.assign({ core: mkCore(plants), block: 256, total: 40000, base: Object.assign({ gate: 50 }, base), ins: C3 }, extra));
  // (a) up over C E G, 2 steps of +7 semitones: each pass is the base pass shifted; after the last shifted pass it returns to pass 0
  const wantA = "60,64,67,67,71,74,74,78,81,60,64,67,67,71,74";
  const badA = plants => keysOf(run1(plants, { order: "up", steps: 2, step_size: 7 }), 15).join() !== wantA;
  plant(badA(["transposeMerged"]), "X1 detects the passes merged into one range (r2.6 octaves)");
  check(!badA([]), "X1(a): up over C E G, 2 steps of +7 semitones: C E G | G B D | D F# A | C E G");
  // (b) steps = 0 never shifts, whatever the step size
  const badB = plants => { const k = keysOf(run1(plants, { order: "up", steps: 0, step_size: 7 }), 12); return k.length < 12 || k.some((v, i) => v !== [60, 64, 67][i % 3]); };
  plant(badB(["stepsZeroShifts"]), "X1 detects a shift with steps = 0");
  check(!badB([]), "X1(b): steps = 0 never transposes (step_size +7 ignored)");
  // (c) degrees move in-scale keys: C major, steps 1, +2 degrees: C E G | E G B; negative and scale-off cases ride along
  const cfgC = { quantize: "scale", order: "up", steps: 1, step_unit: "degrees" };
  const degs = (plants, size, extra) => keysOf(run1(plants, Object.assign({}, cfgC, { step_size: size }, extra), { scale: NDOWN }), 6).join();
  const badC = plants => degs(plants, 2) !== "60,64,67,64,67,71" || degs(plants, -1) !== "60,64,67,59,62,65" || degs(plants, 2, { quantize: "off" }) !== "60,64,67,62,66,69";
  plant(badC(["degreesAsSemis"]), "X1 detects degrees counted as semitones while a scale is on");
  check(!badC([]), "X1(c): degrees count in-scale keys: C major +2 -> C E G | E G B, -1 -> C E G | B D F, and with quantize off a degree is a semitone");
  // (d) an explicit pick takes the current pass's shift. Pattern [order, low]: the order stream climbs 60 64 67 then pass 1 (+12) while `low` follows the pass
  const bank = { step_defaults: X.ARP_BANK.step_defaults, patterns: [...X.ARP_BANK.patterns, { id: "pk2", name: "pk2", length: 2, steps: [{}, { pick: "low" }] }] };
  const wantD = "60,60,64,60,67,72,72,72,76,72,79,60,60,60";   // the pass is read from m at the pick: after the 3rd order play of pass 0 m = 3, so the next pick is already pass 1
  const badD = plants => keysOf(run({ core: mkCore(plants, { bank }), block: 256, total: 40000, base: { order: "up", pattern: "pk2", steps: 1, step_size: 12, gate: 50 }, ins: C3 }), 14).join() !== wantD;
  plant(badD(["transposeMerged"]), "X1 detects a picked note that does not follow its pass");
  check(!badD([]), "X1(d): a `low` pick takes the pass current at its step (t from m): 60 60 64 60 67 | 72 72 72 76 72 79 | 60 60 60");
  // (e) every deterministic style: pass t is the base cycle shifted by t*7, over 2 steps and 2 full loops
  const STY = ["up", "down", "bounce", "bounce2", "converge", "diverge", "pedal_low", "pedal_high", "chord", "chord_walk", "stride", "chord_between", "pairs"];
  const badE = [];
  for (const style of STY) for (const H of [[60, 64, 67], [60, 62, 64, 67, 71]]) {
    const cyc = baseCycle(style, H), per = cyc.length, want = [];
    for (let loop = 0; loop < 2; loop++) for (let t = 0; t < 3; t++) for (const st of cyc) want.push(st.map(k => k + 7 * t).sort((a, b) => a - b).join());
    const r = run({ core: mkCore(), block: 512, total: 120000, base: { order: style, steps: 2, step_size: 7, gate: 40 }, ins: H.map((k, i) => K(100 + i, "k" + i, k)) });
    const byTick = new Map(); for (const x of ons(r)) byTick.set(x.tick, (byTick.get(x.tick) || []).concat(x.key));
    const got = [...byTick.entries()].sort((a, b) => a[0] - b[0]).map(([, v]) => v.sort((a, b) => a - b).join());
    if (got.length < want.length || got.slice(0, want.length).join("|") !== want.join("|")) badE.push(`${style}/e${H.length}`);
  }
  check(badE.length === 0, "X1(e): all 13 deterministic styles (e = 3, 5): each pass is the base cycle shifted by t * 7 semitones, 3 passes then back to 0" + (badE.length ? " - wrong: " + badE.join(" ") : ""));
}

// ---- 17. TM1: time mode (SPEC 2.6): step k sounds at cdiv((k*U + swingDelay(k)) * sr, 1e6) samples after the key press --------
const swingDelayUs = (U, swing, k) => (k % 2 === 1 ? Math.floor(U * (2 * swing - 1000) / 1000) : 0);
{
  const U = 125000, PRESS = 1234, NSTEP = 8;
  const sessTM = (plants, o = {}) => {
    const sr = o.sr || 8000, host = X.simHost({ sr, bpm: o.bpm || 120, startBeats: 0, playing: true });
    if (o.wild) { host.set(Math.round(sr * 0.2), { seekBeats: 7 }); host.set(Math.round(sr * 0.35), { playing: false }); host.set(Math.round(sr * 0.5), { playing: true, seekBeats: 0.3, bpm: 77 }); }
    return run({ core: mkCore(plants), sr, host, block: o.block || 512, total: Math.round(sr * 1.4), base: { rate_mode: "time", step_time_us: U, gate: 30, swing: o.swing || 500 }, ins: [K(Math.round(PRESS * sr / 8000), "a", 60)] });
  };
  // the on time of step k is its grid tick PLUS the swing delay of an odd step, then cdiv to samples
  const wantAt = (sr, k, swing) => Math.round(PRESS * sr / 8000) + Number((BigInt(k * U + swingDelayUs(U, swing, k)) * BigInt(sr) + 999999n) / 1000000n);
  const exact = (r, sr, swing = 500) => { const o = ons(r); return o.length >= NSTEP && o.slice(0, NSTEP).every((e, i) => e.a === wantAt(sr, i, swing) && e.tick === i * U + swingDelayUs(U, swing, i)); };
  const badTM = plants => [[60, 500], [174, 500], [120, 667], [174, 600]].some(([bpm, swing]) => !exact(sessTM(plants, { bpm, wild: true, swing }), 8000, swing));
  plant(badTM(["timeUsesTempo"]), "TM1 detects the step derived from the host tempo (60 and 174 bpm)");
  plant(badTM(["timeAnchorsTransport"]), "TM1 detects the grid anchored at the transport instead of the key press");
  const bs = [4096, 512, 64, 7, 1].map(block => streamOf(sessTM([], { block, wild: true, swing: 667 }).evs));
  const srOk = [44100, 48000, 96000, 192000].every(sr => { const r = sessTM([], { sr, wild: true, block: 4096, swing: 667 }); return exact(r, sr, 667) && n1(r.evs).length === 0; });
  check(!badTM([]) && bs.every(x => x === bs[0]) && srOk,
    `TM1: step k sounds at cdiv((k * 125000 us + swing delay) * sr, 1e6) after the press at 60/174 bpm and swing 500/600/667, through host seeks and a stop/start, identical at block sizes 4096/512/64/7/1, and at 44.1/48/96/192 kHz the ticks are the same microseconds`);
}

// ---- 17b. TM2: a step_time_us change mid-gesture is a piecewise grid (SPEC 2.6, r3 HIGH-1) -------------------------------------
// sr 8000: 125000 us = 1000 samples, 250000 us = 2000, 2 s = 16000, 10 ms = 80. The key is pressed at sample 0 so step k of the first grid is at k * U.
{
  const timeRun = (plants, base, cfg, total) => run({ core: mkCore(plants), block: 64, total, base: Object.assign({ rate_mode: "time", gate: 20 }, base), cfg, ins: [K(0, "a", 60)] });
  const sm = r => ons(r).map(e => e.a);
  // (a) 125 -> 250 ms at sample 3500 (steps at 0 1000 2000 3000 so far): the next step is exactly 250 ms after the last one (5000), then every 2000
  const wantA = [0, 1000, 2000, 3000, 5000, 7000, 9000].join();
  const badA = plants => sm(timeRun(plants, { step_time_us: 125000 }, [{ at: 3500, set: { step_time_us: 250000 } }], 10000)).slice(0, 7).join() !== wantA;
  // (b) 2 s -> 10 ms one second after a step (step at 16000, change at 24000): the next step fires AT the change point, then every 80 samples; never a burst
  const badB = plants => { const a = sm(timeRun(plants, { step_time_us: 2000000 }, [{ at: 24000, set: { step_time_us: 10000 } }], 25000)), i = a.indexOf(24000);
    const tail = a.slice(i); return i < 0 || tail.some((v, j) => v !== 24000 + 80 * j) || a.slice(0, i).join() !== "0,16000"; };
  // (c) a slow continuous sweep up and back down: no gap longer than max(L_old, L_new), no burst, N1 holds
  const sweep = []; { let us = 125000, up = true; for (let at = 1300; at < 30000; at += 1300) { us = up ? us + 25000 : us - 25000; if (us >= 250000) up = false; if (us <= 125000) up = true; sweep.push({ at, set: { step_time_us: us } }); } }
  const badC = plants => {
    const r = timeRun(plants, { step_time_us: 125000 }, sweep, 32000), a = sm(r), usAt = x => { let v = 125000; for (const c of sweep) if (c.at <= x) v = c.set.step_time_us; return v; };
    for (let i = 1; i < a.length; i++) {
      const vals = [usAt(a[i - 1]), usAt(a[i])]; for (const c of sweep) if (c.at > a[i - 1] && c.at <= a[i]) vals.push(c.set.step_time_us);
      const gap = a[i] - a[i - 1], hi = Math.ceil(Math.max(...vals) * 8000 / 1e6) + 1, lo = Math.floor(Math.min(...vals) * 8000 / 1e6) - 1;
      if (gap > hi || gap < lo) return true;
    }
    return a.length < 20 || n1(r.evs).length > 0;
  };
  for (const pl of ["regridAbsolute", "regridKAbs"]) plant(badA([pl]) && badB([pl]), `TM2 detects ${pl} (the old absolute re-grid / kAbs * L_new): (a) and (b) both go red`);
  plant(badC(["regridKAbs"]), "TM2 detects a sweep that bursts or gaps under tick = kAbs * L_new");
  check(!badA([]) && !badB([]) && !badC([]), "TM2: 125 -> 250 ms mid-gesture gives exactly one 250 ms gap (0 1000 2000 3000 5000 7000 9000); 2 s -> 10 ms fires at the change point then every 10 ms with no burst; a slow sweep never gaps past max(L_old, L_new) and never bursts; N1 holds");
}

// ---- 17c. TM3: restart is inert in time mode; entering time mode with keys held; inactive settings do nothing (SPEC 2.6) -------
{
  const keys = [60, 64, 67];
  const seq = (plants, restart, extra = {}) => run({ core: mkCore(plants), block: 128, total: 20000, base: Object.assign({ rate_mode: "time", step_time_us: 125000, restart, gate: 30 }, extra),
    ins: [...keys.map((k, i) => K(1234, "k" + i, k)), ...keys.map((k, i) => O(6500, "k" + i)), ...keys.map((k, i) => K(9000, "j" + i, k))] });
  const keysOf = (r) => ons(r).map(e => e.a + ":" + e.key).join(" ");
  const refStream = keysOf(seq([], "chord"));
  // pattern anchors at EACH gesture start: both gestures play 60 64 67 from the top
  const cyc = r => { const o = ons(r); const g2 = o.filter(e => e.a >= 9000).map(e => e.key); return o.slice(0, 3).map(e => e.key).join() === "60,64,67" && g2.slice(0, 3).join() === "60,64,67"; };
  const badTM3 = plants => ["bar", "free"].some(rs => keysOf(seq(plants, rs)) !== refStream) || !cyc(seq(plants, "bar"));
  plant(badTM3(["timeBarsCount"]), "TM3 detects bars of 61,440 us counted in time mode");
  check(!badTM3([]) && refStream.length > 20, "TM3: in time mode restart bar and free play exactly the chord stream (the pattern anchors at each gesture start, 60 64 67 from the top both times)");
  // entering time mode with H non-empty: the time clock starts AT the switch offset; sync notes before it are released there (T19)
  const enter = plants => run({ core: mkCore(plants), block: 128, total: 14000, base: { rate_mode: "sync", step_time_us: 125000, gate: 30 }, cfg: [{ at: 5001, set: { rate_mode: "time" } }], ins: [K(100, "a", 60)] });
  const badEnter = plants => { const r = enter(plants), t = ons(r).filter(e => e.a >= 5001).map(e => e.a); return t.length < 5 || t.some((v, i) => v !== 5001 + Number((BigInt(i) * 125000n * 8000n + 999999n) / 1000000n)) || n1(r.evs).length > 0; };
  plant(badEnter(["timeEntryNoStart"]), "TM3 detects a time clock that does not start when time mode is entered with keys held");
  check(!badEnter([]), "TM3: entering time mode with a key held starts the time clock at that offset (5001, 6001, ...), release-all first (T19); N1 holds");
  // the inactive setting does nothing: step_time_us in sync, rate in time (no re-anchor, the stream is unchanged)
  const inact = (plants, mode, cfg) => streamOf(run({ core: mkCore(plants), block: 128, total: 14000, base: { rate_mode: mode, step_time_us: 125000, rate: "1/16", gate: 30 }, cfg, ins: keys.map((k, i) => K(100, "k" + i, k)) }).evs);
  const badInact = plants => inact(plants, "sync", [{ at: 3500, set: { step_time_us: 200000 } }]) !== inact(plants, "sync", []) || inact(plants, "time", [{ at: 3500, set: { rate: "1/8" } }]) !== inact(plants, "time", []);
  plant(badInact(["inactiveReanchors"]), "TM3 detects a morph of the inactive rate setting that re-anchors");
  check(!badInact([]), "TM3: step_time_us is ignored in sync and rate in time mode: changing the inactive one leaves the stream byte-identical");
}

// ---- 18. ST1: the step offset is step x the pattern's step size, in the pattern's unit (SPEC 4.3, 4.7, D-016) ---------------
{
  const keysOf = (r, n) => ons(r).map(e => e.key).slice(0, n);
  const up = (plants, base, pat, keys, scale) => run({ core: mkCore(plants), block: 256, total: 30000, scale: scale || { root: 0, mask: 0 }, userPattern: pat, base: Object.assign({ order: "up", gate: 40 }, base), ins: keys.map((k, i) => K(0, "k" + i, k)) });
  // (a) octave_leap at the default step size (+12 semitones) is the old octave: 60 72 60 48
  const wantA = "60,72,60,48,60,72,60,48", bankA = plants => keysOf(up(plants, { pattern: "octave_leap" }, null, [60]), 8).join() !== wantA || keysOf(up(plants, { pattern: "octave_leap" }, null, [60, 64]), 8).join() !== "60,76,60,52,60,76,60,52";
  // (b) size +7 semitones, step 2 -> +14 (and a negative step with a negative size goes up)
  const pB = { length: 3, step_size: 7, steps: [{ step: 2 }, {}, { step: -1 }] };
  const badB = plants => keysOf(up(plants, { pattern: "user" }, pB, [60]), 6).join() !== "74,60,53,74,60,53";
  // (c) degrees: +2 degrees in C major, step 1 on C -> E; step -1 on E (-2 degrees) -> C
  const pC = { length: 2, step_size: 2, step_unit: "degrees", steps: [{ step: 1 }, { step: -1 }] };
  const badC = plants => keysOf(up(plants, { pattern: "user", quantize: "scale" }, pC, [60], NDOWN), 4).join() !== "64,57,64,57" || keysOf(up(plants, { pattern: "user", quantize: "off" }, pC, [60]), 4).join() !== "62,58,62,58";
  // (d) mixed: a semitone transposition (steps 1, +1 semitone) and a degree step offset (+2 degrees) sum BY KIND and follow 4.7's order:
  //     key + S, drop, quantize, move G in-scale keys. Pass 0: 60 -> E 64. Pass 1: 61 -> quantize (the tie goes toward lastQ 64) 62 -> D E F = 65.
  const pD = { length: 1, step_size: 2, step_unit: "degrees", steps: [{ step: 1 }] };
  const badD = plants => keysOf(up(plants, { pattern: "user", quantize: "scale", steps: 1, step_size: 1, step_unit: "semitones" }, pD, [60], NDOWN), 6).join() !== "64,65,64,65,64,65";
  // (e) the critic's example: chord {61, 63}, C major, +2 degrees, no prior lastQ. Per key, 61 -> 60 (a tie, lower) -> +2 degrees = 64, lastQ 64; then
  //     63 is a tie again and goes toward lastQ 64 -> 64 -> +2 degrees = 67. Stage by stage it would be {64, 65}.
  const pE = { length: 1, step_size: 2, step_unit: "degrees", steps: [{ pick: "chord", step: 1 }] };
  const firstChord = plants => { const r = up(plants, { pattern: "user", quantize: "scale", order: "chord" }, pE, [61, 63], NDOWN); const t = ons(r)[0].tick; return ons(r).filter(e => e.tick === t).map(e => e.key).sort((a, b) => a - b).join(); };
  const badE = plants => firstChord(plants) !== "64,67";
  // (f) the step offset reads the PATTERN's size and unit, never the global transposition ones: pattern +7 semitones, global +12 degrees, steps 0
  const pF = { length: 1, step_size: 7, step_unit: "semitones", steps: [{ step: 1 }] };
  const badF = plants => keysOf(up(plants, { pattern: "user", quantize: "scale", steps: 0, step_size: 12, step_unit: "degrees" }, pF, [60], NDOWN), 4).join() !== "67,67,67,67";
  plant(badE(["offsetsPerStage"]), "ST1 detects the offset stages run stage-by-stage across keys instead of per key ({64, 65})");
  plant(badF(["stepOffsetReadsGlobal"]), "ST1 detects a step offset that reads the global transposition size/unit (tested with values that differ from the pattern's)");
  const bad = plants => bankA(plants) || badB(plants) || badC(plants) || badD(plants) || badE(plants) || badF(plants);
  plant(badB(["stepSizeIgnored"]) && badC(["stepSizeIgnored"]) && badD(["stepSizeIgnored"]), "ST1 detects every step size treated as 12 semitones (b, c and d each go red)");
  check(!bad([]), "ST1: octave_leap at +12 is 60 72 60 48; size +7 with step 2 is +14; +2 degrees with step 1 on C in C major is E (and -1 goes down two degrees; quantize off, two semitones); mixed units sum by kind in 4.7's order; the six stages run per key ({61, 63} + 2 degrees = {64, 67}); the step offset reads the pattern's size/unit, not the global one");
}

// Coverage: a session that never reaches the style it names proves nothing, so every window must hold notes.
{
  const w = (evs, a, b) => evs.filter(e => e.t === "on" && e.a >= a && e.a < b).length;
  const wins = [[runSess("styles", 64).evs, [[8000, 14000], [14000, 20000], [20000, 26000], [26000, 32000], [32000, 38000], [38000, 44000]], "styles"],
    [runSess("time", 64).evs, [[0, 9000], [9000, 14000], [14000, 18000], [18000, 20000], [20000, 24000], [24000, 30000], [30000, 35000], [35000, 40000]], "time"],
    [g1Session([], 64).evs, [[34003, 38501], [38501, 44001], [44001, 46001], [46001, 48003], [48003, 49001], [49001, 52000]], "G1 tail"]];
  const empty = wins.flatMap(([evs, ws, n]) => ws.filter(([a, b]) => !w(evs, a, b)).map(([a, b]) => `${n}@${a}-${b}`));
  check(empty.length === 0, "coverage: the N1 styles session and the G1 tail play notes in every chord_walk/stride/chord_between/pairs/converge/shuffle/time-mode window (both transposition units)" + (empty.length ? " - EMPTY: " + empty.join(" ") : ""));
}
const ms = Number((process.hrtime.bigint() - t0) / 1000000n);
console.log(fail ? `lab_smoke: FAIL (${ms} ms)` : `lab_smoke: OK (${ms} ms)`);
process.exit(fail);
