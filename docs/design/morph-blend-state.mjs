// Scratch measurement (not a gate): a full horde corner record for Arp, with a blob in JSON text or in a compact form. Usage: node morph-blend-state.mjs <repo>
import { readFileSync } from "node:fs";
const P = JSON.parse(readFileSync(process.argv[2] + "/spec/params.json", "utf8")).params;
const values = {}, basis = {}, absolute = {}, names = {};
for (const p of P) { const k = "arp." + p.key;
  if (p.kind === "continuous") { values[k] = p.default; basis[k] = 0; }
  else if (p.type === "enum" || Array.isArray(p.values) || typeof p.values === "string") names[k] = String(p.default);
  else absolute[k] = typeof p.default === "boolean" ? +p.default : p.default; }
const rec = blob => JSON.stringify({ format: "horde.module-preset/1", module: "arp", module_version: "0".repeat(64), origin_id: "arp.user.0000", name: "Saved position", preset: { scope: "arp", values, basis, absolute }, names, blobs: blob ? { "arp.userPattern": blob } : {} });
const nE = Object.keys(names).length, nA = Object.keys(absolute).length, nV = Object.keys(values).length;
const step = (i, v) => ({ on: (i + v) % 3 > 0, tie: (i + v) % 5 === 4, vel: 60 + ((i * 7 + v * 13) % 140), gate: 20 + ((i * 11 + v * 5) % 170), step: ((i + v) % 17) - 8, prob: 30 + ((i * 3 + v) % 70), ratchet: 1 + ((i + v) % 4), trig: (i + v) % 9, pick: (i + v) % 2 ? "order" : (i + v) % 4 ? "high" : "low" });
const busy = (n, v) => Array.from({ length: n }, (_, i) => step(i, v));
const defaults = { on: true, tie: false, vel: 100, gate: 100, step: 0, prob: 100, ratchet: 1, trig: 0, pick: "order" };
const sparse = s => Object.fromEntries(Object.entries(s).filter(([k, x]) => defaults[k] !== x));
const jsonBlob = (lanes) => JSON.stringify({ format: "arp-user-pattern/2", lanes: lanes.map(l => ({ share: l.share, length: l.steps.length, step_size: 12, step_unit: "semitones", steps: l.steps.map(sparse) })) });
const PICK = ["order", "1", "2", "3", "4", "5", "6", "7", "8", "low", "high", "chord"];
// 40 bits a step: on 1, tie 1, vel 8, gate 8, step 5, prob 7, ratchet 2, trig 4, pick 4.
function packSteps(steps) { const b = []; for (const s of steps) { let v = 0n; const put = (x, n) => { v = (v << BigInt(n)) | BigInt(x); }; put(+s.on, 1); put(+s.tie, 1); put(s.vel, 8); put(s.gate, 8); put(s.step + 8, 5); put(s.prob, 7); put(s.ratchet - 1, 2); put(s.trig, 4); put(PICK.indexOf(s.pick), 4); for (let i = 4; i >= 0; i--) b.push(Number((v >> BigInt(i * 8)) & 255n)); } return Buffer.from(b).toString("base64"); }
const compactBlob = lanes => JSON.stringify({ format: "arp-user-pattern/2", l: lanes.map(l => [l.share, l.steps.length, 12, 0, packSteps(l.steps)]) });
const bankBlob = (ids) => JSON.stringify({ format: "arp-user-pattern/2", l: ids.map((id, i) => [Math.round(1e6 / ids.length), id]) });
const shares = n => Array.from({ length: n }, (_, i) => 1000000 / n | 0);
const mk = ns => ns.map((n, i) => ({ share: shares(ns.length)[i], steps: busy(n, i + 1) }));
console.log(`keys: ${nV} continuous (values + basis), ${nA} in absolute, ${nE} in names; total ${nV + nA + nE}`);
const base = rec(null).length; console.log(`corner record, no blob: ${base} B; left of 2,048 B: ${2048 - base} B  (assumes the budget counts this whole record: a hypothesis)`);
console.log("blob".padEnd(34), "JSON".padStart(7), "compact".padStart(8), "record w/ compact".padStart(18));
for (const [name, ns] of [["1 busy layer, 16", [16]], ["2 busy layers, 15 + 16", [15, 16]], ["4 busy layers, 16 each, distinct", [16, 16, 16, 16]], ["4 busy layers, 16,15,13,11", [16, 15, 13, 11]], ["8 busy layers, 16 each", Array(8).fill(16)], ["16 busy layers, 16 each", Array(16).fill(16)]]) {
  const l = mk(ns), c = compactBlob(l).length; console.log(name.padEnd(34), String(jsonBlob(l).length).padStart(7), String(c).padStart(8), String(rec(compactBlob(l)).length).padStart(18)); }
for (const ids of [["three_three_two", "roll_end"], ["gallop", "roll_end", "tied_legato", "sparse_chance"]]) console.log(`bank only, ${ids.length}:`.padEnd(34), "".padStart(7), String(bankBlob(ids).length).padStart(8), String(rec(bankBlob(ids)).length).padStart(18));
// Widest legal values, since defaults flatter the figure. Which fields the budget counts is horde's answer, so three readings are printed.
const widest = (asFloat) => { const v = {}, ba = {}, ab = {}, na = {};
  for (const p of P) { const k = "arp." + p.key, hi = p.max ?? p.default, lo = p.min ?? 0;
    if (p.kind === "continuous") { const big = Math.max(Math.abs(hi), Math.abs(lo)); v[k] = asFloat ? big - 1 / 3 : big; ba[k] = 0; }
    else if (p.type === "enum" || Array.isArray(p.values) || typeof p.values === "string") { const opts = Array.isArray(p.values) ? p.values.map(String) : ["three_three_two", "sparse_chance", "bar_trigger"]; na[k] = opts.reduce((x, y) => y.length > x.length ? y : x, String(p.default)); }
    else ab[k] = typeof p.default === "boolean" ? 1 : (lo < 0 ? lo : hi); }
  return { v, ba, ab, na }; };
const blob4 = compactBlob(mk([16, 16, 16, 16]));
for (const [name, f] of [["integer continuous values", false], ["float continuous values", true]]) { const { v, ba, ab, na } = widest(f);
  const corner = JSON.stringify({ origin_id: "arp.user.0000", module_version: "0".repeat(64), preset: { scope: "arp", values: v, basis: ba, absolute: ab }, names: na, blobs: { "arp.userPattern": blob4 } });
  const file = JSON.stringify({ format: "horde.module-preset/1", module: "arp", name: "Saved position", origin_id: "arp.user.0000", module_version: "0".repeat(64), preset: { scope: "arp", values: v, basis: ba, absolute: ab }, names: na, blobs: { "arp.userPattern": blob4 } });
  console.log(`widest values, 4 busy layers, ${name}: corner record ${corner.length} B; with file fields ${file.length} B; budget 2,048 B`); }
