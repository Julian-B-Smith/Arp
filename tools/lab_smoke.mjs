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
  c.prepare(sr, o.maxBlock || 4096);
  const host = o.host || X.simHost({ sr, bpm: o.bpm || 120, startBeats: 0, playing: true });
  const cfg = (o.cfg || []).slice().sort((a, b) => a.at - b.at), ins = (o.ins || []).slice().sort((a, b) => a.at - b.at);
  const resets = o.resets || [];
  const cuts = [...new Set([...host.edges(0, total), ...cfg.map(x => x.at), ...resets].filter(x => x > 0 && x < total))].sort((a, b) => a - b);
  const settingsAt = pos => Object.assign(X.defaultSettings(), { enabled: true }, o.base || {}, ...cfg.filter(x => x.at <= pos).map(x => x.set));
  const evs = [], steps = []; let epoch = 0, pos = 0, ci = 0;
  while (pos < total) {
    if (resets.includes(pos)) { for (const e of c.reset()) evs.push(norm(e, pos, epoch)); epoch++; }
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
    cfg: [{ at: 8000, set: { order: "down" } }, { at: 12000, set: { rate: "1/8" } }, { at: 16000, set: { pattern: "roll_end" } }, { at: 20000, set: { octaves: 2 } }, { at: 24000, set: { restart: "bar" } },
      { at: 28000, set: { restart: "free" } }, { at: 32000, set: { quantize: true } }, { at: 36000, set: { vel_mode: "fixed", seed: 7 } }, { at: 40000, set: { restart: "chord", order: "shuffle" } }],
    ins: [K(100, "a", 61), K(100, "b", 64), K(100, "c", 66)] }),
  // T10 reset() mid-run (ids restart), then a new gesture
  reset: () => ({ total: 30000, base: { gate: 150 }, resets: [20000], ins: [K(100, "a", 60), K(100, "b", 64), K(21000, "c", 67)] }),
  // T11 the sounding table is full (a small table keeps the session short)
  overflow: () => ({ total: 12000, core: () => mkCore([], { caps: { sounding: 3 } }), base: { order: "chord", gate: 200 }, ins: [60, 62, 64, 65, 67, 69].map((k, i) => K(100, "k" + i, k)) }),
  // T16 keys outside 0-127, and a note shorter than one sample (SPEC 1.1 deferral): 999 bpm, 1/64, gate 1 %
  short: () => ({ total: 8000, bpm: 999, base: { rate: "1/64", gate: 1, octaves: 2, order: "up" }, ins: [K(100, "a", 60), K(100, "b", 125)] }),
};
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
  return run({ core: mkCore(plants), block, host, total: 52000, base: { order: "random", pattern: "roll_end", swing: 620, gate: 130, seed: 42, rate: "1/32" },
    cfg: [{ at: 9001, set: { order: "shuffle", pattern: "sparse_chance" } }, { at: 18007, set: { rate: "1/16", latch: "replace" } }, { at: 30011, set: { order: "bounce", pattern: "tied_legato", rate: "1/32" } }],
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
  const n = g1Session([], 64).evs.length;
  check(r === null && n > 100, `G1: ${n} events, absolute samples identical at block sizes 4096/512/64/7/1 (swing, ratchets, prob, tempo change, stop/start, jumps, latch, pedal, panic)` + (r ? " - " + r : ""));
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

// ---- 10. Q1: quantized keys are in the mask and in 0-127; ties go down ----------------------
{
  const keys = [61, 63, 66, 70, 125, 127].map((k, i) => K(0, "k" + i, k));
  const sess = (plants, order, pattern) => run({ core: mkCore(plants), block: 256, total: 40000, scale: NDOWN, base: { quantize: true, octaves: 2, order, pattern: pattern || "none" }, ins: keys });
  const bad = plants => ["up", "chord", "random", "bounce"].some(o => ons(sess(plants, o)).concat(ons(sess(plants, o, "octave_leap"))).some(e => e.key < 0 || e.key > 127 || !((0xAB5 >> (e.key % 12)) & 1)));
  plant(bad(["skipQuantizeChord"]), "Q1 detects quantize skipped on chord steps");
  const tie = (k) => ons(run({ core: mkCore(), block: 256, total: 4000, scale: NDOWN, base: { quantize: true }, ins: [K(0, "a", k)] }))[0].key;
  check(!bad([]) && tie(61) === 60 && tie(66) === 65, "Q1: every quantized key is in the C-major mask and in 0-127 (chord, random, bounce, octave offsets); ties go to the lower key (61->60, 66->65)");
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
  const sess = plants => run({ core: mkCore(plants), block: 64, total: 60000, base: { order: "bounce", octaves: 2, pattern: "roll_end" }, ins: [K(0, "a", 60), K(0, "b", 64), K(0, "c", 67)] });
  const bad = plants => { const ons = sess(plants).evs.filter(e => e.t === "on"); return ons.length < 20 || ons.some(e => e.stream !== "arp"); };
  plant(bad(["untaggedOn"]), "S1 detects a NoteOn emitted without the arp stream tag");
  check(!bad([]), `S1: all ${sess([]).evs.filter(e => e.t === "on").length} NoteOns carry stream "arp" (bounce, 2 octaves, ratchets)`);
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
    // chord: every key at once, +12 per cycle (octaves 2 -> period 2)
    const ch = play("chord", keys, { octaves: 2 }), byA = new Map(); for (const x of ons(ch)) byA.set(x.a, (byA.get(x.a) || []).concat(x.key));
    const cyc = [...byA.values()].slice(0, 6);
    if (!cyc.every((c, i) => c.join() === keys.map(k => k + 12 * (i % 2)).join())) wrong.push(`chord/e${e}`);
    // random: only held keys; shuffle: every e-block is a permutation
    const rnd = ons(play("random", keys)).map(x => x.key);
    if (rnd.some(k => !keys.includes(k)) || (e > 1 && new Set(rnd).size !== e)) wrong.push(`random/e${e}`);
    const shf = ons(play("shuffle", keys)).map(x => x.key);
    for (let c = 0; c + e <= Math.min(shf.length, 10 * e); c += e) if (shf.slice(c, c + e).slice().sort((x, y) => x - y).join() !== keys.join()) wrong.push(`shuffle/e${e}`);
  }
  // octaves 2 with overlapping copies: E is sorted UNIQUE (60,72,84), `played` keeps duplicates (60,72,72,84)
  const u = ons(play("up", [60, 72], { octaves: 2 })).map(x => x.key).slice(0, 6), pl = ons(play("played", [60, 72], { octaves: 2 })).map(x => x.key).slice(0, 8);
  if (u.join() !== "60,72,84,60,72,84") wrong.push("up/unique-E");
  if (pl.join() !== "60,72,72,84,60,72,72,84") wrong.push("played/duplicates");
  check(wrong.length === 0, "order styles: all 12 match SPEC 4.2 (period and first cycle) for e = 1, 2, 3, 5; unique-E and played-duplicates at octaves 2" + (wrong.length ? " - wrong: " + wrong.join(" ") : ""));
}

const ms = Number((process.hrtime.bigint() - t0) / 1000000n);
console.log(fail ? `lab_smoke: FAIL (${ms} ms)` : `lab_smoke: OK (${ms} ms)`);
process.exit(fail);
