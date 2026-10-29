# Prior art — Arp (Phase 0)

**Dated 2026-10-21.** ROADMAP T-001, the Phase 0 gate. Three parallel read-only
research agents (Sonnet, pinned) worked from public sources only: manuals,
official docs and API headers, developer forums, and open-source code that
they read but did not copy. The lead consolidated their reports and checked the
load-bearing numeric claims (two corrections, marked **[corrected]**).
**UNVERIFIED** marks a claim from a snippet, a forum, or memory that nobody
confirmed in a primary source.

**Clean-room.** Order styles are recorded as generic algorithms. Product names
appear only as citations of who offers what. No preset names, UI text or
manual prose are reproduced. The open-source arps (§4) are GPL or ISC and were
read for understanding only; nothing from them enters this repo.

What this changes is recorded in DECISIONS D-005.

---

## 1. Pattern-bank landscape (order styles and step patterns)

### 1.1 Order styles as algorithms
Let N be the held notes sorted by pitch, n = |N|.

| Generic name (ours) | Definition | Offered by (examples) |
|---|---|---|
| ascend / descend | lowest→highest (or reverse), then wrap | nearly all ([Ableton](https://www.ableton.com/en/manual/live-midi-effect-reference/), [Reason RPG-8](https://docs.reasonstudios.com/reason13/rpg-8-arpeggiator), [Novation Peak](https://www.fullcompass.com/common/files/29522-NovationPeakUserManual.pdf), [Roland SH-4d](https://static.roland.com/manuals/sh-4d_v200/en-US/8662682786207755.html), [NI Komplete Kontrol](https://docs.native-instruments.com/online-guides/komplete-kontrol-manual/en/using-scales-and-the-arpeggiator)) |
| bounce, ends once | up then down; top and bottom played once per cycle, so the **period is 2n−2** | RPG-8, Peak, Omnisphere |
| bounce, ends twice | the turnaround notes repeat, so the **period is 2n** | Peak, Omnisphere; Arturia KeyStep (UNVERIFIED) |
| bounce, descending-first | either bounce, starting downward | Logic ([Apple](https://help.apple.com/logicpro-instruments/mac/10.2.3/en.lproj/lgsi7ada22c1.html)), Omnisphere |
| as played | order of key-down; a new key appends | Ableton, Logic, Peak, RPG-8, Roland, NI |
| chord | all held notes on every step | Ableton, Logic, Peak, NI |
| random, free | uniform pick per step, repeats allowed | Ableton, Peak, RPG-8, Roland |
| random, no repeat | shuffle-bag: no note repeats until all have played | Ableton |
| random, frozen | one random permutation, looped until the held set changes | Ableton |
| converge / diverge | alternate lowest and highest remaining, moving inward (or from the middle outward) | Ableton; Logic ([Sound on Sound](https://www.soundonsound.com/techniques/arp-nouveau)) |
| pedal-tone (thumb/pinky family) | the lowest (or highest) note interleaved between each other note | Ableton (UNVERIFIED: recalled, not read); RPG-8 "insert"-type |
| repeat-each ×2/×4 | ascend with each note repeated | Omnisphere; Arturia (UNVERIFIED) |
| rotate / offset | the finished sequence rotated by k steps | Ableton |
| user index order | slots holding "the k-th lowest held note" | Cubase Arpache ([Steinberg](https://steinberg.help/cubase_pro_plugin_reference/v13/en/_shared/topics/plug_ref/arpache_5/arpache_5_overview_r.html)) |

**Octave range has at least four semantics:**
- appended whole-sequence copies an octave up (Peak, RPG-8, NI);
- a signed range (Roland);
- three distinct orderings across octaves ([Bitwig](https://www.bitwig.com/userguide/latest/note_fx/)):
  - stacked;
  - flattened into one sorted run;
  - the full pattern per octave in turn;
- inversion cycling instead of octaves (Logic).

### 1.2 Step-pattern banks
| Product | Bank | What a step carries |
|---|---|---|
| Novation Peak | 33 fixed rhythms, escalating one idea | rhythm and gate |
| NI Komplete Kontrol | 8 sequences; 16 steps (12 at triplet); a change lands at the next note | velocity, gate |
| Logic (Alchemy-style arp) | 16 stored patterns per arp | on/rest, velocity, tie, pitch offset on ties, length |
| Reason RPG-8 | one 16-step on/off pattern, length 1–16 | on/rest only |
| Bitwig | 17 patterns | velocity scale, pitch offset, humanize |
| Yamaha Montage/MODX | >10,000 phrase arpeggios | full phrase data; velocity and gate offsets; swing; a time multiplier |
| Ableton | **no step bank** (rhythm comes from rate, gate and groove) | — |

**The common model:** the order style produces a note stream, and the step
pattern gates it. **A rest does not consume a note** (RPG-8, Peak). The
pattern's length is independent of the number of held notes. No product read
documents **per-step probability**. Ratchets appear only in Arturia and
Elektron material (UNVERIFIED).

### 1.3 Behaviour knobs, as others define them
- **Latch:** two semantics coexist. "Replace" means a new chord after a full
  release replaces the held set (NI; Logic's Hold). "Append" means keys added
  while latched join the set (Ableton, Peak, RPG-8; Logic's Add). Ableton also
  removes a held note when it is pressed again.
- **Pedal:** RPG-8 treats sustain as Hold, which is our D-000 Open 3 rule.
  Others are undocumented in the sample.
- **Restart:** off, on a new note after silence, or on a beat or bar
  interval (Ableton, Logic, Peak KeySync, NI). This matches our three-value
  setting.
- **Swing:** defined differently by each product:
  - even steps later (Logic);
  - 20–80 % of the step pair (Peak);
  - 0–100 % of a step (NI);
  - ±50 shuffle (Roland).
- **Gate above 100 % (legato overlap):** Ableton yes, NI to 200 %, Peak and
  Roland capped at 100 %.
- **Velocity modes:**
  - as played or fixed;
  - scaled (to 200 %);
  - blended with per-step values;
  - decaying toward a target.

---

## 2. Engineering failure modes and defensive rules

Each failure mode is listed with its cause and the rule Arp adopts (proposed in
D-005; the M1 tests enforce them).

1. **Stuck notes on stop, bypass or removal.**
   - **Cause:** waiting for a transport edge, or trusting the host to send
     note-offs. The JUCE arp demo has no transport handling
     ([source](https://raw.githubusercontent.com/juce-framework/JUCE/master/examples/Plugins/ArpeggiatorPluginDemo.h)).
     Hanging notes on stop are reported in HISE
     ([forum](https://forum.hise.audio/post/64415)), and in Logic with a
     third-party arp ([forum](https://forum.scalermusic.com/t/bug-quirk-when-daw-synced-causing-stuck-notes/22478)).
     Some hosts keep feeding a bypassed plugin
     ([KVR](https://www.kvraudio.com/forum/viewtopic.php?p=7699767)).
   - **Rule:** Arp owns a table of every note it emitted and releases all of
     it on any of these: transport stop or jump, the playing flag lost, a
     choke, deactivate, stop-processing, reset, bypass, or switch-off.
2. **Double or missing note-offs.**
   - **Cause:** identity by bare key, or "already off" state kept in several
     places. CLAP notes the off may arrive with id −1 and tells plugins to
     match on the whole (port, channel, key, id) tuple
     ([events.h](https://raw.githubusercontent.com/free-audio/clap/main/include/clap/events.h));
     VST3 ids are −1 when absent
     ([ivstevents.h](https://raw.githubusercontent.com/steinbergmedia/vst3_pluginterfaces/master/vst/ivstevents.h)).
   - **Rule:** one idempotent `release(NoteRef)` is the only path to a
     terminal. It clears the table entry and emits the exact tuple of the
     note-on. No wildcard offs.
3. **Same-key overlap** (gate ≥ 100 %, or two steps hitting one key).
   - **Cause:** CLAP assumes one voice per (port, channel, key) at the host
     (events.h), so two live notes on one key are ambiguous downstream even
     with distinct ids.
   - **Rule:** emit the old note's off, then the new note's on, at the same
     sample and in that order. Whether hosts preserve the order of
     equal-timestamp events is UNVERIFIED, so never rely on reordering.
     Gate > 100 % on a repeated key becomes a forced terminal at the next
     step (proposed; horde's engine rows VO-3/VO-6 decide what the voice does).
4. **Block-boundary jitter and off-by-one.**
   - **Cause:** the JUCE demo and Helm
     ([arpeggiator.cpp](https://github.com/mtytel/helm/blob/master/mopo/src/arpeggiator.cpp))
     fire at most one step per block and clamp offsets into the block.
   - **Rule:** the block is the half-open window [start, end) on the grid.
     Loop over every boundary in it. Never clamp an offset; assert
     0 ≤ offset < n. A boundary exactly at the block's end belongs to the
     next block.
5. **Drift of song position.**
   - **Cause:** accumulating block lengths into a floating position
     ([JUCE forum](https://forum.juce.com/t/issue-syncing-a-sequencer-with-logic-bpm-reporting/58358)).
     Logic is reported to round tempo and position (UNVERIFIED user reports).
   - **Rule:** no running float state across blocks. Re-derive from the
     host's position at each block start, converted once to integer ticks.
6. **Tempo changes mid-block.**
   - **Cause:** CLAP can send transport events and a per-sample tempo
     increment mid-block, while VST3 and AU give one context per block. LMMS
     had stuck notes under tempo automation because note lengths were fixed
     in frames ([PR #5365](https://github.com/LMMS/lmms/pull/5365)).
   - **Rule:** schedule terminals in grid time, never in frames. In CLAP,
     split the block at transport events.
7. **Loop wrap, jumps and pre-roll.**
   - **Cause:** Cubase pre-rolls through negative positions, with doubled
     notes at the wrap ([JUCE forum](https://forum.juce.com/t/ppq-position-and-actual-transport-start-point/65933)).
     Logic shows negative sample times in pre-roll. CLAP flags pre-roll; AU
     flags discontinuities
     ([AUHostTransportStateFlags](https://developer.apple.com/documentation/audiotoolbox/auhosttransportstateflags.md)).
   - **Rule:** predict each block's start from the last one. A mismatch is a
     jump: release all, then recompute the step index from the absolute grid.
     Split at loop end. Use floor division on negative ticks (never C `%`).
     Pre-roll behaviour is decided explicitly (Phase A).
8. **No transport, or "stopped but processing".**
   - **Cause:** CLAP's transport pointer may be null in a free-running host
     ([process.h](https://raw.githubusercontent.com/free-audio/clap/main/include/clap/process.h)).
     JUCE's standalone playhead may never report playing. LibreArp falls back
     to the wall clock, which breaks determinism.
   - **Rule:** a deterministic free-running clock from a sample counter at a
     documented tempo. Trust the host timeline only when its beats and tempo
     flags are set. Live playing with the transport stopped is a first-class
     case.
9. **Swing on the wrong steps.**
   - **Cause:** LibreArp warps time with a sine rather than displacing a
     step parity.
   - **Rule:** parity comes from the integer step index on the absolute grid,
     never from a counter that resets at note-on. Window membership is tested
     on the swung time, since a delayed step can cross a block edge.
10. **Random that does not repeat.**
    - **Cause:** Helm's random mode uses unseeded global `rand()`.
    - **Rule:** a counter-based draw keyed on (seed, absolute step index,
      lane), with no stateful RNG advanced per call. Scrubbing and block size
      then cannot change the output. This is consistent with our mulberry32
      doctrine if mulberry32 is seeded from that key per draw; the exact
      construction is a Phase A spec item.

---

## 3. Transport data and grid math

**What each API gives:**
- **CLAP:**
  - `song_pos_beats` is int64 fixed point with a factor of 2³¹ per beat
    ([fixedpoint.h](https://raw.githubusercontent.com/free-audio/clap/main/include/clap/fixedpoint.h));
  - tempo plus a per-sample tempo increment;
  - loop points, bar start and number, and time signature;
  - flags for has-tempo, has-beats, playing, loop-active and pre-roll;
  - delivered per block and as mid-block events.
- **VST3:** `ProcessContext` once per block, with musical time as a double in
  quarters, bar position, cycle points, tempo and time signature, each behind
  a valid-flag
  ([ivstprocesscontext.h](https://raw.githubusercontent.com/steinbergmedia/vst3_pluginterfaces/master/vst/ivstprocesscontext.h)).
- **AU:** host musical-context and transport-state blocks per render cycle,
  any of which may be null, with no per-field validity
  ([AUHostMusicalContextBlock](https://developer.apple.com/documentation/audiotoolbox/auhostmusicalcontextblock.md)).
- **Wrapper caveat** (clap-wrapper, read at 806bba0): its VST3 and AUv2
  bridges fill some CLAP transport fields loosely:
  - beat 0.0 is treated as "no position";
  - loop start is never set;
  - the beats flag is taken from bar validity.

  So trust a CLAP beat position only with both the beats and tempo flags set,
  and cross-check it against the seconds timeline.

**Grid recipe** (a synthesis; no definitive public reference was found):
1. At each block start, convert the host position once to int64 ticks at
   **15,360 ticks per quarter**. Every horde division (`intake/tempo-divisions.md`,
   triplets and dotted values included) is then an exact integer number of
   ticks. The lead checked this; 96 ticks per quarter is the smallest grid that
   works.
   - **[corrected]** The research report claimed 15,360 is also divisible
     by 7. It is not (15,360 = 2¹⁰·3·5), so septuplets would not be exact.
   - **[corrected]** It also claimed CLAP's 2³¹ fixed point converts
     exactly. It does not, so the conversion rounds once per block, by a
     documented rule.
2. Step k starts at tick ⌊k·num/den⌋, computed from the absolute k and never
   accumulated.
3. The first step in the window is k₀ = ⌈start·den/num⌉. Iterate while the
   step's tick is before the window's end.
4. The sample offset is computed from the tick difference in one place, by one
   rounding rule.
5. Block-size invariance requires that block i's end tick equal block i+1's
   start tick. The test harness simulates the host position as a function of
   the absolute sample count.

---

## 4. Open-source arps read (learned from, not copied)

- **LibreArp** (GPL-3, [source](https://gitlab.com/LibreArp/LibreArp/-/blob/master/Source/LibreArp.cpp)).
  - Good: an integer-tick grid; the position derived per block; a
    stop-all-on-edge; a playing-notes table.
  - Bad: (channel, key) identity, so retriggers drop; sine swing; a
    wall-clock fallback.
- **JUCE arp demo** (ISC). Tiny and readable, but sample-counted with no
  transport and at most one step per block.
- **Helm** (GPL-3). Off-before-on and in-block gate scheduling, but a
  free-running phase, clamped offsets, unseeded random, and no flush on
  disable.
- **BespokeSynth** (GPL-3, [source](https://github.com/BespokeSynth/BespokeSynth/blob/main/Source/Arpeggiator.cpp)).
  Explicit same-pitch off-then-on and a flush on disable, but one sounding
  note and bare-pitch identity.

---

## 5. Name collision (first pass; feeds horde's B433, not legal advice)

- **"ARP" is a live mark in the exact market.**
  - ARP Instruments (1969–1981, named for Alan R. Pearlman,
    [Wikipedia](https://en.wikipedia.org/wiki/ARP_Instruments)).
  - Korg sells ARP hardware reissues ([ARP 2600 M](https://www.korg.com/arp2600_m/))
    and ARP software in the Korg Collection.
  - Korg Inc. applied for "ARP" word marks in instrument class 15 in 2014.
    That comes from an index snippet; registration status and any further
    classes are UNVERIFIED.
- **Products:** suffixed "Arp" plugins are common (Arp-EG, D-Arp, H-Arp). No
  plugin named exactly "Arp" was found, which is a thin search, not proof.
- **Verdict:**
  - **high** collision risk as a user-facing product name;
  - **low** as a feature label inside horde ("arp" is common descriptive
    usage).
  - Keep "Arp" as the codename and in-UI feature label; never use it as a
    marketed product name.
- **Naming directions with no collisions found in a thin search:**
  - change-ringing terms (method, Plain Bob, Grandsire);
  - weaving (heddle, treadle, selvedge);
  - clockwork (escapement);
  - orrery and epicycle terms.

  Two to avoid:
  - "Loom" (an existing MIDI sequencer);
  - "-Bud" and "Turing" (generative-MIDI lineage).

  "Orrery" is also a sibling project's name in this group.

---

## Open after Phase 0 (UNVERIFIED in public sources)
- Do hosts keep the order of equal-timestamp output events? Do they split
  blocks at loop points? Which transport fields does each DAW actually fill?
  No public matrix exists. A logging probe in each target host is the only
  answer (proposed for M3 admission, D-005).
- Primary manuals not read: Bitwig's pattern list, Cubase's full mode list,
  Arturia, Korg, Roland Juno/JD-Xi, Novation Circuit, Elektron. Read them only
  if one becomes load-bearing.

---

## Re-scan 2026-10-29: the user-visible label (Phase N, horde B433; a zero-cost knockout screen, not legal advice)

**What is screened.** The human's choice (D-043, by poll): inside horde, the
module carries a **plain descriptive label**, "Arp" / "Arpeggiator", like
horde's ECHO ruling, and **no brand name**. So this screen asks whether that
descriptive use is clear. It does not ask whether "Arp" could be a brand.

**Sources and method.**
- USPTO TSDR status pages, read directly on 2026-10-29.
- Web search over trademark aggregators and KVR.
- The EU (EUIPO TMview) and WIPO registers were **not** searched: no working
  route this session. That is the same gap horde's B433 screens recorded.

**Findings (US).**
- **Live, Korg Inc., class 15 (music synthesizers):**
  - ARP standard-character word mark, Reg. 4782829 (serial 86198994),
    registered 2015-07-28, renewed 2025-01-07;
  - an ARP design mark (treble clef with a plug, serial 86199003), registered
    2015-08-25, renewed 2025-01-07.
- **Dead, Korg Inc., class 9 (software for playing, creating and editing
  music):** ARP design mark, serial 87269937, filed 2016-12-15. It was
  abandoned on 2018-07-25 after an inter partes decision of the Trademark
  Trial and Appeal Board, and never registered.
- **Products:** no plugin named exactly "Arp" was found. Suffixed and prefixed
  names are common (H-Arp, Pattern Arp Plus, BlueARP, Arp-EG).
- **"Arpeggiator"** is the ordinary name of the feature across synths and
  DAWs; no trademark claim on it was found.

**Verdict.**
- **As a descriptive label for the arpeggiator section inside horde: LOW.** It
  names what the feature is, as countless instruments do. Korg's live marks
  cover synthesizer hardware (class 15), and its software (class 9)
  application died.
- **As a brand or product name: still HIGH,** unchanged from §5. horde is a
  software synthesizer, close to Korg's goods.

**Practices that keep it descriptive** (recommended to horde with the
readiness notice):
- prefer "Arpeggiator" for the section title and "Arp" only as a short label;
- avoid an all-caps "ARP" styled as a logo or wordmark;
- never pair it with a clef or plug device;
- never use "ARP" as a product, preset-pack or plugin name.

**Not covered:**
- the EU and WIPO registers;
- counsel. horde's B433 keeps counsel as a pre-commercial step.
