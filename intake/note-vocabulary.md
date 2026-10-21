# The note vocabulary Arp speaks

> Summarised by the horde lead on 2026-10-21 from horde ROADMAP B92 (2026-09-01), FOUNDATIONS' `response-note-transport`
> (their DECISIONS #115, OQ #31), horde ADR-257 A1 and the rack-slot contract's RS-4.11 (§12.5). Those records stay the
> source; this is a reading aid.

Arp takes notes in and sends notes out. Both sides use the ecosystem's ruled note vocabulary:

1. **Identity is `NoteRef`-compatible, never a bare key.** A note is the host's note id, or (port, channel, key) when the
   host supplies none. Two notes on one key are two identities. horde's own panic dump (key 60 sounding twice, distinct
   note ids, both gated) is the proof that a bare key aliases notes.
2. **Every identity gets exactly one terminal event,** through whichever path it leaves: note-off, steal or expiry. For
   Arp this means every note it emits ends exactly once, including when the pattern stops, the transport stops, the
   arp is switched off mid-note, or a held chord changes.
3. **Pitch on the bus is canonical:** each note's base frequency from the tuning, with bend and glide as separate
   per-note offsets. Each consumer derives its own hearing. Arp emits base pitches; it never bakes a bend into a note.

**Where Arp sits.** In horde it runs ahead of the engine on the instrument's note input. Its output is what the engine's
IF-1 accepts (see `engine-note-interface.md`). In horde's FX device after 1.0 (horde ADR-254) it may run on the MIDI
sidechain's notes, so it must not assume an engine downstream.
