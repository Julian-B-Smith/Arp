// Scratch: bytes of each candidate form, as compact JSON text (horde stores blobs as text). Not a gate.
const defaults = { on: true, tie: false, vel: 100, gate: 100, step: 0, prob: 100, ratchet: 1, trig: 0, pick: "order" };
const sparse = s => Object.fromEntries(Object.entries(s).filter(([k, v]) => defaults[k] !== v));
const fullUser = n => Array.from({ length: n }, (_, i) => ({ on: i % 3 > 0, tie: i % 5 === 4, vel: 60 + i, gate: 30 + i * 3, step: (i % 5) - 2, prob: 50 + i, ratchet: 1 + i % 4, trig: i % 3, pick: i % 2 ? "order" : "high" }));
const busy = n => fullUser(n).map(sparse);                               // every step differs: the worst case
const simple = n => Array.from({ length: n }, (_, i) => sparse(i % 3 ? { vel: 80 } : { ratchet: 2 }));   // a hand-made pattern
const b = o => JSON.stringify(o).length;
const lcmv = (a, c) => { const g = (x, y) => y ? g(y, x % y) : x; return a / g(a, c) * c; };
const fm = { len: 0 };
// form 1: per-step table with a palette of distinct steps: {palette, steps:[[idx,share]...]} (shares in millionths)
function tableForm(lanes) {
  const L = lanes.reduce((x, l) => lcmv(x, l.steps.length), 1), pal = new Map(), rows = [];
  const tot = lanes.reduce((x, l) => x + l.w, 0);
  for (let k = 0; k < L; k++) { const m = new Map(); for (const l of lanes) { const key = JSON.stringify(l.steps[k % l.steps.length]); if (!pal.has(key)) pal.set(key, pal.size); const i = pal.get(key); m.set(i, (m.get(i) || 0) + Math.round(l.w * 1e6 / tot)); } rows.push([...m.entries()].flat()); }
  return { L, bytes: b({ palette: [...pal.keys()].map(JSON.parse), rows }) };
}
// form 2: lanes: bank patterns by id, user patterns by value, each with a share
const laneForm = lanes => b({ lanes: lanes.map(l => l.id ? { id: l.id, w: l.w } : { steps: l.steps, w: l.w }) });
const bank = id => ({ id, steps: Array.from({ length: { three_three_two: 8, roll_end: 8, gallop: 4, tied_legato: 8, sparse_chance: 16, octave_leap: 4 }[id] }, (_, i) => ({ vel: 100 + (id.length * i) % 7 })) });
const cases = [
  ["bank + bank", [{ ...bank("three_three_two"), w: 600000 }, { ...bank("roll_end"), w: 400000 }]],
  ["four bank", [{ ...bank("gallop"), w: 1e5 }, { ...bank("roll_end"), w: 2e5 }, { ...bank("tied_legato"), w: 3e5 }, { ...bank("sparse_chance"), w: 4e5 }]],
  ["simple user 7 + 16", [{ steps: simple(7), w: 5e5 }, { steps: simple(16), w: 5e5 }]],
  ["busy user 15 + 16", [{ steps: busy(15), w: 3e5 }, { steps: busy(16), w: 7e5 }]],
  ["four busy user 16", [1, 2, 3, 4].map(i => ({ steps: busy(16), w: i * 1e5 }))],
  ["busy 16,15,13,11", [16, 15, 13, 11].map((n, i) => ({ steps: busy(n), w: (i + 1) * 1e5 }))],
];
console.log("one busy 16-step user pattern today:", b({ format: "arp-user-pattern/1", steps: busy(16) }), "bytes; all fields explicit:", b({ steps: fullUser(16) }));
console.log("case".padEnd(24), "steps".padStart(7), "table B".padStart(9), "lanes B".padStart(9));
for (const [n, l] of cases) { const t = tableForm(l); console.log(n.padEnd(24), String(t.L).padStart(7), String(t.bytes).padStart(9), String(laneForm(l)).padStart(9)); }
