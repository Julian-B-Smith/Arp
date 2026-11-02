// Scratch model (not a gate) of "a step holds alternatives; blending stacks them; flatten bakes the stack".
// Tests step SELECTION only (which step data plays at each kAbs), not the full core. Usage: node flatten_model.mjs <repo>
import { readFileSync } from "node:fs";
const repo = process.argv[2];
const bank = JSON.parse(readFileSync(repo + "/spec/bank/starter.json", "utf8"));
const html = readFileSync(repo + "/lab/arp-lab.html", "utf8");
const X = new Function(html.slice(html.indexOf("/* ================= CORE"), html.indexOf("/* ================= UI")) + "\n;return { draw };")();
const SCALE = 1000000;
// A step's canonical content, pattern-level step size and unit included, so identical steps merge whichever pattern they came from.
const keyOf = (s, p) => JSON.stringify([s.on ?? true, s.tie ?? false, s.vel ?? 100, s.gate ?? 100, s.step ?? 0, s.prob ?? 100, s.ratchet ?? 1, s.trig ?? 0, s.pick ?? "order", p.step_size ?? 12, p.step_unit ?? "semitones"]);
const fold = k => { let h = 2166136261; for (let i = 0; i < k.length; i++) h = Math.imul(h ^ k.charCodeAt(i), 16777619); return h >>> 0; };
const plain = p => ({ len: p.steps.length, steps: p.steps.map(s => [[keyOf(s, p), SCALE]]) });   // one alternative per step
// The one operator, used live and by flatten: corner weights x each corner's alternatives at k, merged by content, normalised by largest remainder to sum SCALE.
function stack(corners, k) {
  const m = new Map();
  for (const { w, pat } of corners) { if (!w) continue; for (const [key, a] of pat.steps[((k % pat.len) + pat.len) % pat.len]) m.set(key, (m.get(key) || 0) + w * a); }
  const tot = [...m.values()].reduce((x, y) => x + y, 0), rows = [...m.entries()].sort((x, y) => x[0] < y[0] ? -1 : 1);
  const out = rows.map(([key, v]) => [key, Math.floor(v * SCALE / tot), (v * SCALE) % tot]);
  let left = SCALE - out.reduce((x, r) => x + r[1], 0);
  [...out].sort((a, b) => b[2] - a[2] || (a[0] < b[0] ? -1 : 1)).slice(0, left).forEach(r => r[1]++);
  return out.map(r => [r[0], r[1]]);
}
const ETAB = Array.from({ length: 4096 }, (_, i) => Math.round(-Math.log((i + 0.5) / 4096) * 65536));
// The race: one draw per distinct content, keyed by content, lowest E/w wins (cross-multiplied, integers). One alternative makes no draw.
function pick(alts, seed, kAbs, keyed = "content", idx = null) {
  if (alts.length === 1) return alts[0][0];
  let best = null, bE = 0, bW = 1;
  alts.forEach(([key, w], i) => {
    if (!w) return;
    const x = keyed === "content" ? (kAbs ^ fold(key)) >>> 0 : (kAbs + i * 7919) >>> 0;   // the plant keys by list position instead
    const E = ETAB[X.draw(seed, x, 5, 0) >>> 20];
    if (best === null || E * bW < bE * w || (E * bW === bE * w && key < best)) { best = key; bE = E; bW = w; }
  });
  return best;
}
const gcd = (a, b) => b ? gcd(b, a % b) : a, lcm = (a, b) => a / gcd(a, b) * b;
function flatten(corners) {
  const live = corners.filter(c => c.w), L = live.reduce((x, c) => lcm(x, c.pat.len), 1);
  return { len: L, steps: Array.from({ length: L }, (_, k) => stack(live, k)) };
}
const P = Object.fromEntries(bank.patterns.map(p => [p.id, plain(p)]));
const mk = (id, n, steps) => ({ id, ...plain({ steps: Array.from({ length: n }, (_, i) => steps(i)) }) });
const u7 = mk("u7", 7, i => i % 3 ? { vel: 80 } : { ratchet: 2 }), u15 = mk("u15", 15, i => i % 2 ? { on: false } : { step: i % 4 }), u16 = mk("u16", 16, i => ({ gate: 40 + i })), u5 = mk("u5", 5, i => i === 2 ? { tie: true } : {});
const seq = (corners, N, seed, f = "content") => { const o = []; for (let k = 0; k < N; k++) o.push(pick(stack(corners, k), seed, k + 1000, f)); return o; };
const same = (a, b) => a.every((x, i) => x === b[i]);
let fail = 0; const ck = (ok, m) => { console.log((ok ? "PASS " : "FAIL ") + m); if (!ok) fail = 1; };
const N = 20000;
const cases = [["bank 8+8", [[600, P.three_three_two], [400, P.roll_end]]], ["bank 4+16", [[250, P.gallop], [750, P.sparse_chance]]], ["user 7+16", [[500, u7], [500, u16]]], ["user 15+16", [[333, u15], [667, u16]]],
  ["four corners", [[100, P.gallop], [200, P.roll_end], [300, u7], [400, P.tied_legato]]], ["duplicate corners", [[250, P.gallop], [250, P.gallop], [250, P.roll_end], [250, P.roll_end]]]];
for (const [name, c] of cases) {
  const corners = c.map(([w, pat]) => ({ w, pat })), live = seq(corners, N, 7), flat = flatten(corners);
  ck(same(live, seq([{ w: 1000, pat: flat }], N, 7)), `${name}: flattened (${flat.len} steps, up to ${Math.max(...flat.steps.map(s => s.length))} alternatives) plays the same as live, ${N} steps`);
}
{ // Plants. The old plant keyed by position in the SORTED, merged list, so both sides saw the same list and it could not fire.
  // Plant A: D-052's own design, a draw per CORNER with no merging, live; the flat has only merged alternatives. Plant B: a merge that is skipped in the flat's list order.
  const raw = [{ w: 100, pat: P.gallop }, { w: 200, pat: P.roll_end }, { w: 300, pat: u7 }, { w: 400, pat: P.tied_legato }];
  const cornerKeyed = (cs, k, seed) => { let best = null, bE = 0, bW = 1; cs.forEach((c, i) => { if (!c.w) return; const [[key]] = c.pat.steps[k % c.pat.len]; const E = ETAB[X.draw(seed, (k + 1000 + i * 7919) >>> 0, 5, 0) >>> 20]; if (best === null || E * bW < bE * c.w) { best = key; bE = E; bW = c.w; } }); return best; };
  const flat = flatten(raw), flatSeq = seq([{ w: 1000, pat: flat }], N, 7), a = []; for (let k = 0; k < N; k++) a.push(cornerKeyed(raw, k, 7));
  let d = 0; a.forEach((x, i) => { if (x !== flatSeq[i]) d++; });
  ck(d > 0, `plant A: draws keyed by corner (D-052 as written) differ from the flat on ${d} of ${N} steps: the comparison detects it, and D-052's keying cannot save exactly`);
  const swapped = { len: flat.len, steps: flat.steps.map(s => s.map(([key, w]) => [key + "x", w])) };   // same shares, different identities
  const b = seq([{ w: 1000, pat: swapped }], N, 7); let e = 0; b.forEach((x, i) => { if (x.slice(0, -1) !== flatSeq[i]) e++; });
  ck(e > 0, `plant B: a flat whose contents are keyed differently differs on ${e} of ${N} steps`);
}
{ // same process, nested: flatten, then blend the flat with a third, then flatten again
  const F = flatten([{ w: 500, pat: P.three_three_two }, { w: 500, pat: P.roll_end }]);
  const c2 = [{ w: 700, pat: F }, { w: 300, pat: u5 }], F2 = flatten(c2);
  ck(same(seq(c2, N, 3), seq([{ w: 1000, pat: F2 }], N, 3)), `nested: a flat blended with a third pattern, flattened again, plays the same (${F2.len} steps)`);
  const c3 = [{ w: 350, pat: P.three_three_two }, { w: 350, pat: P.roll_end }, { w: 300, pat: u5 }];
  const a = seq(c2, N, 3), b = seq(c3, N, 3); let d = 0; a.forEach((x, i) => { if (x !== b[i]) d++; });
  ck(d === 0, `associative: the flat at 70 % with u5 equals the three sources at 35/35/30 live (${d} of ${N} differ)`);
}
{ // one pattern, whatever the corners: no draw
  const c = [{ w: 250, pat: P.gallop }, { w: 250, pat: P.gallop }, { w: 500, pat: P.gallop }];
  ck([...Array(64).keys()].every(k => stack(c, k).length === 1), "same pattern on every corner: one alternative per step, no draw");
}
{ // shares and monotonicity (four corners)
  const c = [{ w: 100, pat: P.gallop }, { w: 200, pat: P.roll_end }, { w: 300, pat: P.octave_leap }, { w: 400, pat: P.tied_legato }], M = 200000;
  let worst = 0; for (let j = 0; j < 8; j++) { const alts = stack(c, j), n = new Map(); for (let k = j; k < M * 8; k += 8) { const r = pick(alts, 11, k); n.set(r, (n.get(r) || 0) + 1); } for (const [key, w] of alts) worst = Math.max(worst, Math.abs((n.get(key) || 0) / M - w / SCALE)); }
  ck(worst < 0.004, `shares follow the weights: worst error ${worst.toFixed(4)} over ${M} draws per position`);
  let moved = 0, bad = 0; const before = c.map(x => ({ ...x })), after = c.map((x, i) => ({ ...x, w: i === 1 ? x.w + 300 : x.w }));
  for (let k = 0; k < 100000; k++) { const a = pick(stack(before, k), 11, k), b = pick(stack(after, k), 11, k); if (a !== b) { moved++; if (!stack([{ w: 1, pat: P.roll_end }], k).some(([key]) => key === b)) bad++; } }
  ck(moved > 0 && bad === 0, `raising one corner moves steps only to that corner's step (${moved} moved, ${bad} elsewhere)`);
}
{ // Layers, the stored form (revision 3): shares rounded ONCE at layer level, per-step shares summed raw. Live IS flatten, so equality is by construction; test it, nested, with awkward weights.
  const same2 = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);
  const round = rows => { const tot = rows.reduce((x, r) => x + r.v, 0), out = rows.map(r => ({ ...r, share: Math.floor(r.v * SCALE / tot), rem: (r.v * SCALE) % tot })); let left = SCALE - out.reduce((x, r) => x + r.share, 0); [...out].sort((a, b) => b.rem - a.rem || (a.id < b.id ? -1 : 1)).slice(0, left).forEach(r => r.share++); return out.map(({ id, pat, share }) => ({ id, pat, share })).filter(r => r.share > 0); };
  // A setting is { w, layers }; a plain pattern is one layer at SCALE. Identical layers merge.
  const flat = settings => { const m = new Map(); for (const { w, layers } of settings) { if (!w) continue; for (const l of layers) { const e = m.get(l.id) || { id: l.id, pat: l.pat, v: 0 }; e.v += w * l.share; m.set(l.id, e); } } return round([...m.values()].sort((a, b) => a.id < b.id ? -1 : 1)); };
  const altsOf = (layers, k) => { const m = new Map(); for (const l of layers) { const [[key]] = l.pat.steps[k % l.pat.len]; m.set(key, (m.get(key) || 0) + l.share); } return [...m.entries()].sort((a, b) => a[0] < b[0] ? -1 : 1); };
  const play = (layers, n, seed) => { const o = []; for (let k = 0; k < n; k++) o.push(pick(altsOf(layers, k), seed, k + 1000)); return o; };
  const L = (id, pat) => [{ id, pat, share: SCALE }];
  const cs = [["awkward 2", [[333, L("332", P.three_three_two)], [667, L("roll", P.roll_end)]]], ["awkward 4, lengths 7,16,15,5", [[137, L("u7", u7)], [251, L("u16", u16)], [389, L("u15", u15)], [223, L("u5", u5)]]], ["glided-looking", [[291, L("gallop", P.gallop)], [709, L("tied", P.tied_legato)]]]];
  for (const [name, c] of cs) { const set = c.map(([w, layers]) => ({ w, layers })), f = flat(set), again = flat([{ w: 977, layers: f }]);
    ck(same2(play(f, N, 7), play(again, N, 7)) && JSON.stringify(f.map(x => [x.id, x.share])) === JSON.stringify(again.map(x => [x.id, x.share])), `layers, ${name}: saving a saved blend changes nothing (idempotent)`); }
  const A = flat([{ w: 137, layers: L("332", P.three_three_two) }, { w: 863, layers: L("roll", P.roll_end) }]), B = flat([{ w: 419, layers: A }, { w: 581, layers: L("u7", u7) }]), C = flat([{ w: 1, layers: B }]);
  ck(same2(play(B, N, 3), play(C, N, 3)), "layers, nested: a blend of a saved blend saves exactly");
  ck(B.length === 3 && B.reduce((x, l) => x + l.share, 0) === SCALE, "layers, nested: three layers, shares sum to exactly one million");
  const plant = [...A.map(l => ({ ...l, share: l.share + 1 }))]; let d = 0; { const a = play(A, 200000, 3), b = play(plant, 200000, 3); a.forEach((x, i) => { if (x !== b[i]) d++; }); }
  const plantKeyed = play(A.map(l => ({ ...l, pat: { ...l.pat, steps: l.pat.steps.map(s => s.map(([key, w]) => [key + "!", w])) } })), N, 3).map(x => x.slice(0, -1)); let e = 0; { const a = play(A, N, 3); a.forEach((x, i) => { if (x !== plantKeyed[i]) e++; }); }
  ck(e > 0, `layers, plant: content keyed differently differs on ${e} of ${N} steps`);
  console.log(`INFO a 1-in-a-million share error changed ${d} of 200000 steps here: rare, not impossible (the critic estimates about 1 in 10^7), so exactness still comes from rounding once, not from this being small`);
}
{ // sizes: every pair of lengths a user or the bank can hold
  let maxPair = 0, ex; for (let a = 1; a <= 16; a++) for (let b = 1; b <= 16; b++) { const l = lcm(a, b); if (l > maxPair) { maxPair = l; ex = [a, b]; } }
  const lens = bank.patterns.map(p => p.steps.length); const bankL = lens.reduce(lcm, 1);
  console.log(`INFO worst pair of lengths 1..16: lcm ${maxPair} (${ex}); all bank patterns together: lcm ${bankL}; three lengths 16,15,7: ${lcm(lcm(16, 15), 7)}`);
}
process.exit(fail);
