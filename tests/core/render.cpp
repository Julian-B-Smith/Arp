// render.cpp - the listening renders (ROADMAP T-007 M3 gate: the human's listening sign-off).
//
// Drives the SHIPPED core (target `arp_core`, no ARP_PLANTS) the way horde would: prepare once at 44.1 kHz, fixed
// blocks of 256 samples, one settings snapshot per block, the transport and tempo as data, every input event at an
// integer sample offset. Nothing here reads a clock or draws a random number, so two runs write identical bytes.
// For each scenario it writes <id>.wav (a small synth voiced from the core's OUTPUT events), <id>.mid and one
// manifest.json that lab/listen.html reads. The synth is this file's, never the core's: the core
// emits note events only, and the voicing below exists so a human can judge them by ear.
//
//   arp_render <output-dir>
//
// Exit 0 only if every scenario kept the core's contracts: nothing allocated inside process() / reset() (the A1
// hook of alloc_hook.cpp is linked in and armed around each call), every NoteOn got exactly one NoteOff, no counter
// that means "something was refused or lost" moved.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "alloc_hook.h"
#include "arp/core.h"

using namespace arp;

namespace {

constexpr int kSr = 44100;
constexpr int kBlock = 256;
constexpr int kTailSamples = kSr * 6 / 10;   // room for the last release to ring out
constexpr double kPi = 3.14159265358979323846;
constexpr int kMidiDivision = 1920;          // MIDI ticks per quarter note
constexpr std::uint16_t kCMajor = 0xAB5;     // root-relative mask: degrees 0 2 4 5 7 9 11
constexpr std::uint16_t kMinor = 0x5AD;      // root-relative mask: degrees 0 2 3 5 7 8 10

// ---------------------------------------------------------------- scenario description

struct NoteSpec { std::int64_t on, off; int key, vel; std::int64_t id; };   // a key the player holds
struct PedalSpec { std::int64_t at; bool down; };
struct Change { std::int64_t at; std::function<void(Settings&)> fn; };
struct ScaleChange { std::int64_t at; Scale scale; };
struct Mark { std::int64_t at; std::string text, key, value; };

struct Scenario {
  std::string id, title, listen;
  double bpm = 120;
  int beatsPerBar = 4;
  bool playing = true;      // transport running (false: the free clock, SPEC 2.4)
  bool direct = false;      // also play what the player holds, as a quiet sine (horde's `direct` tee; Arp never emits it)
  bool metronome = false;   // a quiet click on every beat, the bar line accented
  double lengthBeats = 0;   // the played length; the audio adds a short tail
  Settings s0;
  Scale scale0{0, kCMajor};  // horde's global scale, C major unless a scenario says otherwise
  bool hasUser = false;
  UserPattern user;
  std::function<void(Settings&, double)> perBlock;   // (settings, seconds at the block start): a smooth sweep
  std::vector<std::string> extra;   // plain-text facts that are not Arp settings (e.g. horde's scale)
  std::vector<NoteSpec> notes;
  std::vector<PedalSpec> pedals;
  std::vector<Change> changes;
  std::vector<ScaleChange> scaleChanges;
  std::vector<Mark> marks;
  std::int64_t nextId = 1;

  std::int64_t at(double beat) const { return std::llround(beat * 60.0 / bpm * kSr); }
  double beats(double seconds) const { return seconds * bpm / 60.0; }

  // A chord (or one key) pressed in the order given, released together. The presses sit a few samples BEFORE the beat
  // and two samples apart, so every key is held when the grid step on that beat is evaluated (restart `chord` anchors on
  // the first grid step at or after the press) and the press order is exactly the order of the list.
  void hold(double on, double off, std::initializer_list<int> keys, int vel = 100) {
    int i = 0;
    for (int k : keys) {
      const std::int64_t t = std::max<std::int64_t>(0, at(on) - 16 + 2 * i);
      notes.push_back({t, std::max(at(off), t + 1), k, vel, nextId++});
      ++i;
    }
  }
  void pedal(double beat, bool down) { pedals.push_back({at(beat), down}); }
  void change(double beat, std::function<void(Settings&)> fn) { changes.push_back({at(beat), std::move(fn)}); }
  void scaleAt(double beat, Scale s) { scaleChanges.push_back({at(beat), s}); }
  void label(double beat, const std::string& text) { marks.push_back({at(beat), text, "", ""}); }
  void order(double beat, Order o) {
    change(beat, [o](Settings& s) { s.order = o; });
    marks.push_back({at(beat), "", "order", kOrderNames[static_cast<std::size_t>(o)]});
  }
  void pattern(double beat, Pattern p) {
    change(beat, [p](Settings& s) { s.pattern = p; });
    marks.push_back({at(beat), "", "pattern", kPatternNames[static_cast<std::size_t>(p)]});
  }
};

Scenario make(const char* id, const char* title, const char* listen, double bpm = 120) {
  Scenario s;
  s.id = id;
  s.title = title;
  s.listen = listen;
  s.bpm = bpm;
  s.s0.enabled = true;
  return s;
}

// ---------------------------------------------------------------- the listening set

std::vector<Scenario> buildScenarios() {
  std::vector<Scenario> all;

  {  // 1
    Scenario s = make("01-baseline", "Baseline: up over C, E, G, B",
        "A plain rising arpeggio: C, E, G, B, then back to C, in even sixteenth notes with a small gap between notes. "
        "Listen for a steady, even pulse with no stumble where the figure starts over.");
    s.hold(0.5, 16.5, {60, 64, 67, 71});
    s.lengthBeats = 17;
    all.push_back(std::move(s));
  }

  {  // 2a / 2b
    static const char* titles[2] = {"Order tour, part 1 of 2", "Order tour, part 2 of 2"};
    static const char* ids[2] = {"02a-order-tour-1", "02b-order-tour-2"};
    static const char* listens[2] = {
        "The same four held notes (C, E, G, B, pressed in a scrambled order) played one bar at a time in eight orders: "
        "up, down, up-down, up-down with the ends played twice, as played, all at once, random, and shuffle without back-to-back repeats. "
        "Each bar should sound like its name; the label on the roll says which bar is which.",
        "The same four held notes, one bar each, in the other eight orders: converge (outside in), diverge (inside out), "
        "the lowest note between the others, the highest note between the others, the chord and then its notes one by one, "
        "stride (the bass, then the rest as a chord), a chord between single notes, and pairs of neighbouring notes."};
    for (int part = 0; part < 2; ++part) {
      Scenario s = make(ids[part], titles[part], listens[part]);
      s.hold(0.5, 32.5, {67, 60, 71, 64});
      for (int i = 0; i < 8; ++i) s.order(0.5 + 4 * i, static_cast<Order>(part * 8 + i));
      s.lengthBeats = 33.5;
      all.push_back(std::move(s));
    }
  }

  {  // 3a
    Scenario s = make("03a-pattern-tour-1", "Rhythm pattern tour, part 1 of 2",
        "One bar each of six rhythm patterns over a rising arpeggio: plain, off-beat (only half the steps play), gallop (long, short, short), "
        "three-three-two (a syncopated 3 + 3 + 2), a downbeat accent (every fourth note louder) and tied legato (notes held across steps). "
        "The rhythm should change at each bar line while the notes keep climbing.");
    s.hold(0.5, 24.5, {60, 64, 67, 71});
    const Pattern ps[6] = {Pattern::None, Pattern::Offbeat, Pattern::Gallop, Pattern::ThreeThreeTwo, Pattern::AccentOne, Pattern::TiedLegato};
    for (int i = 0; i < 6; ++i) s.pattern(0.5 + 4 * i, ps[i]);
    s.lengthBeats = 25.5;
    all.push_back(std::move(s));
  }

  {  // 3b
    Scenario s = make("03b-pattern-tour-2", "Rhythm pattern tour, part 2 of 2",
        "The other five rhythm patterns over the same arpeggio: octave leap (the second step jumps up an octave, the fourth down), "
        "roll at the end (each cycle ends with a double and a quadruple repeat), sparse chance (many steps drop out, the same ones every time you render), "
        "bar trigger (a click once per bar, with notes) and trigger pulse (a click on every beat and no notes at all).");
    s.hold(0.5, 28.5, {60, 64, 67, 71});
    s.pattern(0.5, Pattern::OctaveLeap);
    s.pattern(4.5, Pattern::RollEnd);
    s.pattern(8.5, Pattern::SparseChance);
    s.pattern(16.5, Pattern::BarTrigger);
    s.pattern(24.5, Pattern::TriggerPulse);
    s.lengthBeats = 29.5;
    all.push_back(std::move(s));
  }

  {  // 4: lab/patterns/broken-chord-stabs.json, transcribed (format arp-user-pattern/1; omitted fields are the bank's step defaults)
    Scenario s = make("04-user-pattern", "Your saved pattern: Broken chord stabs",
        "Your own pattern over a four-chord progression (C major 7, A minor 7, F major 7, G7), two bars per chord. "
        "Listen for single notes picked from the chord (lowest, third, second, fourth), full-chord stabs, and the last stab sounding an octave higher. "
        "Each new chord should start the pattern again from its first note.");
    s.s0.pattern = Pattern::User;
    s.hasUser = true;
    s.user.length = 8;
    s.user.stepSize = 12;
    s.user.stepUnit = StepUnit::Semitones;
    const std::int32_t picks[8] = {1, 3, 2, kPickChord, 1, 4, kPickChord, 2};
    for (int i = 0; i < 8; ++i) s.user.steps[i].pick = picks[i];
    s.user.steps[6].step = 1;
    s.hold(0.5, 8.4, {60, 64, 67, 71});
    s.hold(8.5, 16.4, {57, 60, 64, 67});
    s.hold(16.5, 24.4, {53, 57, 60, 64});
    s.hold(24.5, 32.4, {55, 59, 62, 65});
    s.label(0.5, "C major 7");
    s.label(8.5, "A minor 7");
    s.label(16.5, "F major 7");
    s.label(24.5, "G7");
    s.lengthBeats = 33.5;
    all.push_back(std::move(s));
  }

  {  // 5
    Scenario s = make("05-swing", "Swing: straight, triplet feel, maximum",
        "A rising arpeggio in sixteenths, two bars each at three swing settings: straight, then a triplet feel, then the maximum. "
        "Every second note should lean later and later; the beat itself must not move.");
    s.hold(0.5, 24.5, {60, 64, 67, 71});
    s.change(8.5, [](Settings& x) { x.swing = 667; });
    s.change(16.5, [](Settings& x) { x.swing = 750; });
    s.label(0.5, "straight");
    s.label(8.5, "triplet feel");
    s.label(16.5, "maximum");
    s.lengthBeats = 25.5;
    all.push_back(std::move(s));
  }

  {  // 6
    Scenario s = make("06-transposition", "Transposition steps",
        "First the arpeggio repeats three more times, each pass a whole octave higher, then drops back to the start. "
        "After a pause it repeats two more times, each pass two notes of the C major scale higher (so the shape stays in key), then drops back.");
    s.s0.steps = 3;
    s.s0.stepSize = 12;
    s.hold(0.5, 8.5, {48, 52, 55, 59});
    s.change(9.0, [](Settings& x) {
      x.steps = 2;
      x.stepSize = 2;
      x.stepUnit = StepUnit::Degrees;
      x.quantize = Quantize::Scale;
    });
    s.hold(9.5, 18.5, {48, 52, 55, 59});
    s.label(0.5, "3 steps of +12 semitones");
    s.label(9.5, "2 steps of +2 scale degrees");
    s.extra.push_back("Horde's scale: C major");
    s.lengthBeats = 19.5;
    all.push_back(std::move(s));
  }

  {  // 7
    Scenario s = make("07-scale-quantize", "Scale quantize",
        "A held cluster of six neighbouring notes. First as played; then every note is pulled into A minor (the notes outside the scale snap to a neighbour, so some notes repeat). "
        "After a pause the cluster is played from three different lowest notes in the movable mode, where the minor shape follows the lowest note held, so the sound moves with the chord.");
    s.scale0 = Scale{9, kMinor};
    s.extra.push_back("Horde's scale: A minor");
    s.hold(0.5, 8.4, {60, 61, 62, 63, 64, 65});
    s.change(4.5, [](Settings& x) { x.quantize = Quantize::Scale; });
    s.change(8.9, [](Settings& x) { x.quantize = Quantize::ChordRoot; });
    s.hold(9.5, 13.4, {60, 61, 62, 63, 64, 65});
    s.hold(13.5, 17.4, {62, 63, 64, 65, 66, 67});
    s.hold(17.5, 21.4, {65, 66, 67, 68, 69, 70});
    s.label(0.5, "as played");
    s.label(4.5, "snapped to A minor");
    s.label(9.5, "shape follows the lowest note");
    s.lengthBeats = 22;
    all.push_back(std::move(s));
  }

  {  // 8
    Scenario s = make("08-chord-stage", "Chord stage under the arp",
        "One note at a time is held. The chord stage turns it into a chord that sounds steadily (on the right), and the arpeggio plays the notes of that chord (on the left). "
        "First each note gains a major third and a fifth; later an octave is added as a third added note, so the arpeggio gets one more step.");
    s.s0.chordEnabled = true;
    s.hold(0.5, 4.3, {60});
    s.hold(4.5, 8.3, {65});
    s.hold(8.5, 12.3, {67});
    s.change(12.4, [](Settings& x) { x.chordOn[2] = true; });
    s.hold(12.5, 16.3, {60});
    s.hold(16.5, 20.3, {65});
    s.hold(20.5, 24.3, {67});
    s.label(0.5, "+4 and +7");
    s.label(12.5, "+4, +7 and +12");
    s.lengthBeats = 25;
    all.push_back(std::move(s));
  }

  {  // 9
    Scenario s = make("09-chord-generator", "Chord generator: arp off, chord on",
        "The arpeggio is off; only the chord stage plays, so every note of a short melody comes out as a chord that lasts exactly as long as you hold the key. "
        "The first time the chords are major; the second time the lower added note moves down a semitone and they turn minor.");
    s.s0.arpEnabled = false;
    s.s0.chordEnabled = true;
    const int mel[7] = {60, 64, 67, 65, 64, 62, 60};
    const double len[7] = {0.9, 0.9, 0.9, 0.9, 0.9, 0.9, 1.9};
    const double beat0[7] = {0, 1, 2, 3, 4, 5, 6};
    for (int phrase = 0; phrase < 2; ++phrase)
      for (int i = 0; i < 7; ++i) s.hold(0.5 + 9 * phrase + beat0[i], 0.5 + 9 * phrase + beat0[i] + len[i], {mel[i]});
    s.change(8.9, [](Settings& x) { x.chordShift[0] = 3; });
    s.label(0.5, "major chords");
    s.label(9.5, "minor chords");
    s.lengthBeats = 18;
    all.push_back(std::move(s));
  }

  {  // 10
    Scenario s = make("10-time-mode", "Time mode: a smooth speed sweep",
        "The steps run on a clock of their own (no transport needed) from the moment the keys go down. "
        "The step time glides from 250 ms down to 25 ms and back again; the arpeggio should accelerate smoothly into a buzz and slow down again, with no gap, stumble or burst of late notes.");
    s.playing = false;
    s.s0.rateMode = RateMode::Time;
    s.s0.stepTimeUs = 250000;
    s.hold(0.5, 19.5, {60, 64, 67});
    s.perBlock = [](Settings& x, double t) {
      double f = 0;
      if (t >= 1.0 && t < 5.0) f = (t - 1.0) / 4.0;
      else if (t >= 5.0 && t < 9.0) f = 1.0 - (t - 5.0) / 4.0;
      x.stepTimeUs = static_cast<std::int32_t>(std::llround(250000.0 * std::pow(0.1, f)));
    };
    s.label(s.beats(1.0), "250 ms per step");
    s.label(s.beats(5.0), "25 ms per step");
    s.label(s.beats(9.0), "back to 250 ms");
    s.lengthBeats = 20;
    all.push_back(std::move(s));
  }

  {  // 11
    Scenario s = make("11-repeats", "Repeats",
        "With repeats set to 2, each chord is arpeggiated twice and then the arpeggio stops although the keys are still down; the next chord starts it again. "
        "Afterwards repeats is set to forever and a chord is held, so it plays until you let go.");
    s.s0.rate = Rate::R1_8;
    s.s0.repeats = 2;
    s.hold(0.5, 5.5, {60, 64, 67});
    s.hold(6.5, 11.5, {65, 69, 72});
    s.hold(12.5, 17.5, {67, 71, 74});
    s.change(18.0, [](Settings& x) { x.repeats = 0; });
    s.hold(18.5, 26.5, {60, 64, 67});
    s.label(0.5, "repeats 2: plays twice, then stops");
    s.label(18.5, "repeats forever");
    s.lengthBeats = 27;
    all.push_back(std::move(s));
  }

  {  // 12
    Scenario s = make("12-restart", "Restart: where the pattern begins",
        "A syncopated three-three-two rhythm over a chord pressed late in the bar, with a click on every beat (louder on the bar line) and a quiet tone for your own key press. "
        "First the pattern begins at your press; then it is tied to the bar, so it begins again after every bar line; then it runs free with the song, so it does not care when you pressed.",
        120);
    s.metronome = true;
    s.direct = true;
    s.s0.pattern = Pattern::ThreeThreeTwo;
    s.s0.rate = Rate::R3_16;
    s.hold(1.3, 11.5, {60, 64, 67, 71});
    s.change(12.0, [](Settings& x) { x.restart = Restart::Bar; });
    s.hold(13.3, 23.5, {60, 64, 67, 71});
    s.change(24.0, [](Settings& x) { x.restart = Restart::Free; });
    s.hold(25.3, 35.5, {60, 64, 67, 71});
    s.label(1.3, "restart on new chord");
    s.label(13.3, "restart on bar");
    s.label(25.3, "restart free");
    s.lengthBeats = 36.5;
    all.push_back(std::move(s));
  }

  {  // 13
    Scenario s = make("13-ratchets-ties-chance", "Ratchets, ties and probability",
        "Three rhythm patterns with a feature each. Roll at the end: the last two steps of each cycle are repeated twice and four times. "
        "Tied legato at the default gate: a tied note simply carries on through the next step. "
        "Sparse chance: only a few steps always play and the rest play now and then, the same way every time you render it.");
    s.s0.rate = Rate::R1_8;
    s.hold(0.5, 28.5, {60, 64, 67, 71});
    s.pattern(0.5, Pattern::RollEnd);
    s.pattern(8.5, Pattern::TiedLegato);
    s.pattern(16.5, Pattern::SparseChance);
    s.change(16.5, [](Settings& x) { x.rate = Rate::R1_16; });
    s.lengthBeats = 29.5;
    all.push_back(std::move(s));
  }

  {  // 14
    Scenario s = make("14-triggers", "Triggers (clicks)",
        "Clicks are the arpeggio's trigger outputs, one pitch per lane. First a click once per bar next to the notes, then a click on every beat with no notes. "
        "Then with the restart set to free and no key held at all, the clicks still come on the bar; when you press a chord the notes join in. "
        "At the end each of four steps fires its own lane, so you hear four click pitches in a row.");
    s.hasUser = true;
    s.user.length = 4;
    s.user.stepSize = 12;
    for (int i = 0; i < 4; ++i) s.user.steps[i].trig = i + 1;
    s.hold(0.5, 6.4, {60, 64, 67, 71});
    s.pattern(0.5, Pattern::BarTrigger);
    s.pattern(6.45, Pattern::TriggerPulse);
    s.hold(6.5, 12.4, {60, 64, 67, 71});
    s.change(12.45, [](Settings& x) { x.restart = Restart::Free; });
    s.pattern(12.45, Pattern::BarTrigger);
    s.hold(20.5, 28.4, {60, 64, 67, 71});
    // The restart mode is adopted at the next evaluated step, so it changes while the old chord is still held: the new chord's
    // press must already find restart `chord` in force to re-anchor the pattern (else the first lane would not be lane 1).
    s.change(27.9, [](Settings& x) { x.restart = Restart::Chord; });
    s.pattern(28.45, Pattern::User);
    s.hold(28.5, 34.4, {60, 64, 67, 71});
    s.label(0.5, "click once per bar, with notes");
    s.label(6.5, "click every beat, no notes");
    s.label(12.5, "no keys held: clicks only");
    s.label(20.5, "chord pressed: notes join");
    s.label(28.5, "four lanes, four click pitches");
    s.lengthBeats = 35.5;
    all.push_back(std::move(s));
  }

  {  // 15
    Scenario s = make("15-latch-pedal", "Latch and sustain pedal",
        "A quiet tone marks the keys you are actually holding. Latch in replace mode: tap a chord and the arpeggio keeps going after you let go; tapping a second chord replaces it. "
        "Latch in add mode: each tapped note is added to what is already going, and tapping a note that is already there takes it out again. "
        "Finally the sustain pedal: tap a chord with the pedal down and the arpeggio keeps going until you lift the pedal.");
    s.direct = true;
    s.change(0.2, [](Settings& x) { x.latch = Latch::Replace; });
    s.hold(0.5, 1.0, {60, 64, 67});
    s.hold(6.5, 7.0, {65, 69, 72});
    s.change(11.5, [](Settings& x) { x.latch = Latch::Off; });
    s.change(12.2, [](Settings& x) { x.latch = Latch::Append; });
    s.hold(12.5, 13.0, {60});
    s.hold(16.5, 17.0, {64});
    s.hold(20.5, 21.0, {67});
    s.hold(24.5, 25.0, {64});
    s.change(28.5, [](Settings& x) { x.latch = Latch::Off; });
    s.pedal(29.5, true);
    s.hold(30.0, 30.5, {60, 64, 67});
    s.pedal(37.0, false);
    s.label(0.5, "latch replace");
    s.label(12.5, "latch add");
    s.label(24.5, "same note again: removed");
    s.label(29.5, "sustain pedal down");
    s.label(37.0, "pedal up");
    s.lengthBeats = 38;
    all.push_back(std::move(s));
  }

  {  // 16
    Scenario s = make("16-dense", "Dense: triplet thirty-seconds at 174 bpm",
        "A fast, busy texture: eight held notes, up and down, in triplet thirty-second notes with a roll at the end of each cycle. "
        "It should stay clean and exactly in time, with no dropped notes, no doubled notes and no wobble in the pulse.", 174);
    s.s0.rate = Rate::R1_32T;
    s.s0.order = Order::Bounce;
    s.s0.pattern = Pattern::RollEnd;
    s.hold(0.5, 22.5, {60, 62, 64, 65, 67, 69, 71, 72});
    s.lengthBeats = 23.5;
    all.push_back(std::move(s));
  }

  return all;
}

// ---------------------------------------------------------------- running the core

struct OutNote { std::int64_t on, off; int key, vel; Stream stream; };
struct OutTrig { std::int64_t at; int lane; };

struct Seen {   // the values one setting took during the run, for the manifest's "settings that differ"
  std::vector<std::string> vals;
  bool numeric = true, overflow = false;
  long long mn = 0, mx = 0;
};

struct Result {
  std::vector<OutNote> notes;
  std::vector<OutTrig> trigs;
  std::vector<std::pair<std::string, Seen>> seen;   // in params.json key order
  std::int64_t frames = 0;
  std::int64_t ons = 0, offs = 0, allocs = 0;
  Counters counters{};
  std::vector<float> left, right;
};

using Flat = std::vector<std::pair<std::string, std::string>>;

// Settings as (params.json key, value text) in params.json order; enums by name, as the file stores them.
Flat flat(const Settings& s) {
  Flat f;
  auto b = [&](const std::string& k, bool v) { f.emplace_back(k, v ? "true" : "false"); };
  auto n = [&](const std::string& k, long long v) { f.emplace_back(k, std::to_string(v)); };
  auto e = [&](const std::string& k, const char* v) { f.emplace_back(k, v); };
  b("enabled", s.enabled);
  b("arp_enabled", s.arpEnabled);
  e("order", kOrderNames[static_cast<std::size_t>(s.order)]);
  e("pattern", kPatternNames[static_cast<std::size_t>(s.pattern)]);
  e("rate", kRateNames[static_cast<std::size_t>(s.rate)]);
  e("rate_mode", kRateModeNames[static_cast<std::size_t>(s.rateMode)]);
  n("step_time_us", s.stepTimeUs);
  n("steps", s.steps);
  n("step_size", s.stepSize);
  e("step_unit", kStepUnitNames[static_cast<std::size_t>(s.stepUnit)]);
  n("gate", s.gate);
  n("swing", s.swing);
  e("latch", kLatchNames[static_cast<std::size_t>(s.latch)]);
  e("vel_mode", kVelModeNames[static_cast<std::size_t>(s.velMode)]);
  n("vel_fixed", s.velFixed);
  e("restart", kRestartNames[static_cast<std::size_t>(s.restart)]);
  n("repeats", s.repeats);
  e("quantize", kQuantizeNames[static_cast<std::size_t>(s.quantize)]);
  e("scale_source", kScaleSourceNames[static_cast<std::size_t>(s.scaleSource)]);
  n("scale_root", s.scaleRoot);
  for (int i = 0; i < 12; ++i) b("scale_deg" + std::to_string(i), s.scaleDeg[i]);
  n("seed", s.seed);
  b("chord_enabled", s.chordEnabled);
  e("chord_unit", kStepUnitNames[static_cast<std::size_t>(s.chordUnit)]);
  for (int i = 0; i < 6; ++i) {
    b("chord_on" + std::to_string(i + 1), s.chordOn[i]);
    n("chord_shift" + std::to_string(i + 1), s.chordShift[i]);
  }
  return f;
}

void noteSeen(Result& r, const Settings& s) {
  const Flat f = flat(s);
  if (r.seen.empty())
    for (const auto& kv : f) r.seen.emplace_back(kv.first, Seen{});
  for (std::size_t i = 0; i < f.size(); ++i) {
    Seen& sn = r.seen[i].second;
    const std::string& v = f[i].second;
    if (std::find(sn.vals.begin(), sn.vals.end(), v) != sn.vals.end()) continue;
    char* end = nullptr;
    const long long x = std::strtoll(v.c_str(), &end, 10);
    if (end == v.c_str() || *end != '\0') sn.numeric = false;
    else if (sn.vals.empty()) sn.mn = sn.mx = x;
    else { sn.mn = std::min(sn.mn, x); sn.mx = std::max(sn.mx, x); }
    if (sn.vals.size() < 8) sn.vals.push_back(v); else sn.overflow = true;
  }
}

bool run(const Scenario& sc, Result& r) {
  auto core = std::make_unique<Core>();
  if (!core->prepare(kSr, kBlock)) { std::printf("FAIL  %s: prepare refused\n", sc.id.c_str()); return false; }
  if (sc.hasUser) core->setUserPattern(sc.user);

  struct In { std::int64_t at; int prio; Event e; };   // prio: releases first, then the pedal, then presses
  std::vector<In> ins;
  for (const NoteSpec& n : sc.notes) {
    Event on, off;
    on.type = EventType::NoteOn;
    on.ref.id = n.id;
    on.ref.key = n.key;
    on.key = n.key;
    on.vel = n.vel;
    off.type = EventType::NoteOff;
    off.ref = on.ref;
    ins.push_back({n.on, 2, on});
    ins.push_back({n.off, 0, off});
  }
  for (const PedalSpec& p : sc.pedals) {
    Event e;
    e.type = EventType::Pedal;
    e.down = p.down;
    ins.push_back({p.at, 1, e});
  }
  std::stable_sort(ins.begin(), ins.end(), [](const In& a, const In& b) { return a.at != b.at ? a.at < b.at : a.prio < b.prio; });
  std::vector<Change> changes = sc.changes;
  std::stable_sort(changes.begin(), changes.end(), [](const Change& a, const Change& b) { return a.at < b.at; });
  std::vector<ScaleChange> scales = sc.scaleChanges;
  std::stable_sort(scales.begin(), scales.end(), [](const ScaleChange& a, const ScaleChange& b) { return a.at < b.at; });

  const std::int64_t nBlocks = (sc.at(sc.lengthBeats) + kBlock - 1) / kBlock;
  const std::int64_t incPerSample = std::llround(sc.bpm * 1000.0) * kTpq;   // SPEC 2.2: milli-bpm * TPQ posNum per sample
  Settings cur = sc.s0, lastSeen = sc.s0;
  Scale curScale = sc.scale0;
  noteSeen(r, cur);
  std::vector<Event> inbuf;
  inbuf.reserve(1024);
  std::vector<std::int64_t> idToNote;
  I128 rep{};
  std::size_t ii = 0, ci = 0, si = 0;

  auto take = [&](const EventSpan& out, std::int64_t base) {
    for (const Event& e : out) {
      const std::int64_t t = base + e.offset;
      if (e.type == EventType::NoteOn) {
        const auto id = static_cast<std::size_t>(e.ref.id);
        if (id >= idToNote.size()) idToNote.resize(id + 1, -1);
        idToNote[id] = static_cast<std::int64_t>(r.notes.size());
        r.notes.push_back({t, -1, e.key, e.vel, e.stream});
        ++r.ons;
      } else if (e.type == EventType::NoteOff) {
        const auto id = static_cast<std::size_t>(e.ref.id);
        if (id < idToNote.size() && idToNote[id] >= 0 && r.notes[static_cast<std::size_t>(idToNote[id])].off < 0) {
          r.notes[static_cast<std::size_t>(idToNote[id])].off = t;
          ++r.offs;
        }
      } else if (e.type == EventType::Trigger) {
        r.trigs.push_back({t, e.lane});
      }
    }
  };

  for (std::int64_t b = 0; b < nBlocks; ++b) {
    const std::int64_t start = b * kBlock;
    while (ci < changes.size() && changes[ci].at / kBlock <= b) changes[ci++].fn(cur);   // a change lands at the start of its block
    while (si < scales.size() && scales[si].at / kBlock <= b) curScale = scales[si++].scale;
    if (sc.perBlock) sc.perBlock(cur, static_cast<double>(start) / kSr);
    if (!(cur == lastSeen)) { noteSeen(r, cur); lastSeen = cur; }
    inbuf.clear();
    while (ii < ins.size() && ins[ii].at < start + kBlock) {
      Event e = ins[ii].e;
      e.offset = static_cast<std::int32_t>(ins[ii].at - start);
      inbuf.push_back(e);
      ++ii;
    }
    Block blk;
    blk.n = kBlock;
    blk.transport.playing = sc.playing;
    blk.transport.hasBeats = sc.playing;
    blk.transport.hasRep = true;   // the exact position, so the grid is the host's, never a rounded double
    blk.transport.rep = rep;
    blk.transport.hasTempo = true;
    blk.transport.tempo = sc.bpm;
    blk.transport.timeSigNum = sc.beatsPerBar;
    blk.transport.timeSigDen = 4;
    blk.settings = cur;
    blk.scale = curScale;
    blk.in = std::span<const Event>(inbuf.data(), inbuf.size());
    {
      arp_test::AllocScope guard;   // the real-time contract: nothing may allocate inside process()
      const EventSpan out = core->process(blk);
      r.allocs += guard.count();
      take(out, start);
    }
    rep += I128::mul(kBlock, incPerSample);
  }
  {
    arp_test::AllocScope guard;
    const EventSpan out = core->reset();   // anything still sounding is ended at the end of the played length
    r.allocs += guard.count();
    take(out, nBlocks * kBlock);
  }
  r.counters = core->counters();
  r.frames = nBlocks * kBlock + kTailSamples;

  const Counters& c = r.counters;
  const bool ok = r.allocs == 0 && r.ons == r.offs && c.offsLost == 0 && c.refused == 0 && c.pendingOverflow == 0 &&
                  c.soundingOverflow == 0 && c.eventOverflow == 0 && c.badInput == 0 && c.stepLogDropped == 0 && ii == ins.size();
  std::printf("%s  %-24s %6.2f s  %5lld notes  %4zu triggers  allocs in process(): %lld  unpaired offs: %lld\n", ok ? "PASS" : "FAIL",
              sc.id.c_str(), static_cast<double>(r.frames) / kSr, static_cast<long long>(r.ons), r.trigs.size(),
              static_cast<long long>(r.allocs), static_cast<long long>(r.ons - r.offs));
  if (!ok)
    std::printf("      offsLost %lld refused %lld pendingOverflow %lld soundingOverflow %lld eventOverflow %lld badInput %lld stepLogDropped %lld unconsumedInput %zu\n",
                static_cast<long long>(c.offsLost), static_cast<long long>(c.refused), static_cast<long long>(c.pendingOverflow),
                static_cast<long long>(c.soundingOverflow), static_cast<long long>(c.eventOverflow), static_cast<long long>(c.badInput),
                static_cast<long long>(c.stepLogDropped), ins.size() - ii);
  return ok;
}

// ---------------------------------------------------------------- the synth (this file's, not the core's)

struct Voice {
  double attack, release, gain, pan;   // seconds, seconds, linear, -1 (left) .. +1 (right)
  bool soft;                           // true: a soft saw (the arp); false: a rounder organ-like tone (the chord)
};
constexpr Voice kArp{0.003, 0.035, 0.30, -0.25, true};
constexpr Voice kChordVoice{0.015, 0.150, 0.09, 0.25, false};   // sustained and stacked, so quieter per voice than the short arp notes
constexpr Voice kDirectVoice{0.005, 0.005, 0.10, 0.0, false};   // pure sine, drawn separately below

void pan2(double pan, double& gl, double& gr) {
  const double a = (pan + 1.0) * kPi / 4.0;   // equal power
  gl = std::cos(a);
  gr = std::sin(a);
}

double hz(int key) { return 440.0 * std::pow(2.0, (key - 69) / 12.0); }

void addTone(Result& r, std::int64_t on, std::int64_t off, int key, int vel, const Voice& v, bool sine) {
  const double f = hz(key), w = 2.0 * kPi * f / kSr;
  const int harmonics = std::max(1, std::min(12, static_cast<int>(7000.0 / f)));
  double coef[13] = {};
  for (int h = 1; h <= harmonics; ++h) coef[h] = 1.0 / std::pow(h, 1.3);
  const double atk = std::max(1.0, v.attack * kSr), rel = std::max(1.0, v.release * kSr);
  const double amp = v.gain * vel / 127.0;
  double gl, gr;
  pan2(v.pan, gl, gr);
  const std::int64_t end = std::min<std::int64_t>(static_cast<std::int64_t>(r.left.size()), off + static_cast<std::int64_t>(rel));
  for (std::int64_t i = on; i < end; ++i) {
    const double n = static_cast<double>(i - on), ph = w * n;
    double env = std::min(1.0, n / atk);
    if (i >= off) env *= std::max(0.0, 1.0 - static_cast<double>(i - off) / rel);
    double s;
    if (sine) s = std::sin(ph);
    else if (v.soft) {
      s = 0;
      for (int h = 1; h <= harmonics; ++h) s += coef[h] * std::sin(h * ph);
      s *= 0.45;
    } else {
      s = std::sin(ph) + 0.35 * std::sin(2 * ph) + 0.12 * std::sin(3 * ph);
    }
    const double x = amp * env * s;
    r.left[static_cast<std::size_t>(i)] += static_cast<float>(x * gl);
    r.right[static_cast<std::size_t>(i)] += static_cast<float>(x * gr);
  }
}

// A short decaying click, centred. `freq` picks its pitch.
void addClick(Result& r, std::int64_t at, double freq, double gain, double decay) {
  const std::int64_t len = static_cast<std::int64_t>(0.05 * kSr);
  for (std::int64_t i = 0; i < len && at + i < static_cast<std::int64_t>(r.left.size()); ++i) {
    const double t = static_cast<double>(i) / kSr;
    const double x = gain * std::sin(2.0 * kPi * freq * t) * std::exp(-t / decay);
    r.left[static_cast<std::size_t>(at + i)] += static_cast<float>(x * 0.7071);
    r.right[static_cast<std::size_t>(at + i)] += static_cast<float>(x * 0.7071);
  }
}

void synth(const Scenario& sc, Result& r) {
  r.left.assign(static_cast<std::size_t>(r.frames), 0.f);
  r.right.assign(static_cast<std::size_t>(r.frames), 0.f);
  if (sc.direct)
    for (const NoteSpec& n : sc.notes) addTone(r, n.on, n.off, n.key, n.vel, kDirectVoice, true);
  for (const OutNote& n : r.notes) addTone(r, n.on, n.off < 0 ? n.on + 1 : n.off, n.key, n.vel, n.stream == Stream::Chord ? kChordVoice : kArp, false);
  for (const OutTrig& t : r.trigs) addClick(r, t.at, 600.0 * std::pow(2.0, (t.lane - 1) / 4.0), 0.35, 0.007);
  if (sc.metronome) {
    const std::int64_t total = sc.at(sc.lengthBeats);
    for (int b = 0; sc.at(b) < total; ++b) {
      const bool accent = b % sc.beatsPerBar == 0;
      addClick(r, sc.at(b), accent ? 1500.0 : 1000.0, accent ? 0.22 : 0.12, 0.004);
    }
  }
}

// ---------------------------------------------------------------- files

void put16(std::vector<std::uint8_t>& v, unsigned x) { v.push_back(x & 0xFF); v.push_back((x >> 8) & 0xFF); }
void put32(std::vector<std::uint8_t>& v, unsigned x) { put16(v, x & 0xFFFF); put16(v, x >> 16); }

bool writeFile(const std::filesystem::path& p, const std::vector<std::uint8_t>& bytes) {
  std::ofstream f(p, std::ios::binary);
  f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(f);
}

bool writeWav(const std::filesystem::path& p, const Result& r, double gain) {
  std::vector<std::uint8_t> v;
  const std::uint32_t dataBytes = static_cast<std::uint32_t>(r.frames) * 4;
  v.reserve(44 + dataBytes);
  for (char c : std::string("RIFF")) v.push_back(static_cast<std::uint8_t>(c));
  put32(v, 36 + dataBytes);
  for (char c : std::string("WAVEfmt ")) v.push_back(static_cast<std::uint8_t>(c));
  put32(v, 16);
  put16(v, 1);        // PCM
  put16(v, 2);        // stereo
  put32(v, kSr);
  put32(v, kSr * 4);
  put16(v, 4);
  put16(v, 16);
  for (char c : std::string("data")) v.push_back(static_cast<std::uint8_t>(c));
  put32(v, dataBytes);
  auto q = [&](float x) {
    const double y = std::max(-1.0, std::min(1.0, static_cast<double>(x) * gain));
    return static_cast<unsigned>(static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(y * 32767.0))));
  };
  for (std::int64_t i = 0; i < r.frames; ++i) {
    put16(v, q(r.left[static_cast<std::size_t>(i)]));
    put16(v, q(r.right[static_cast<std::size_t>(i)]));
  }
  return writeFile(p, v);
}

struct MidiEv { std::int64_t tick; int prio; std::vector<std::uint8_t> bytes; };   // prio: offs before ons at one tick

void vlq(std::vector<std::uint8_t>& v, std::uint32_t x) {
  std::uint8_t buf[5];
  int n = 0;
  buf[n++] = x & 0x7F;
  while ((x >>= 7) != 0) buf[n++] = static_cast<std::uint8_t>((x & 0x7F) | 0x80);
  while (n > 0) v.push_back(buf[--n]);
}

void putTrack(std::vector<std::uint8_t>& file, std::vector<MidiEv> evs, std::int64_t endTick) {
  std::stable_sort(evs.begin(), evs.end(), [](const MidiEv& a, const MidiEv& b) { return a.tick != b.tick ? a.tick < b.tick : a.prio < b.prio; });
  std::vector<std::uint8_t> body;
  std::int64_t last = 0;
  for (const MidiEv& e : evs) {
    vlq(body, static_cast<std::uint32_t>(e.tick - last));
    last = e.tick;
    body.insert(body.end(), e.bytes.begin(), e.bytes.end());
  }
  vlq(body, static_cast<std::uint32_t>(std::max<std::int64_t>(0, endTick - last)));
  body.push_back(0xFF); body.push_back(0x2F); body.push_back(0x00);
  for (char c : std::string("MTrk")) file.push_back(static_cast<std::uint8_t>(c));
  for (int s = 24; s >= 0; s -= 8) file.push_back(static_cast<std::uint8_t>((body.size() >> s) & 0xFF));
  file.insert(file.end(), body.begin(), body.end());
}

MidiEv nameEv(const std::string& name) {
  MidiEv e{0, 0, {0xFF, 0x03, static_cast<std::uint8_t>(name.size())}};
  e.bytes.insert(e.bytes.end(), name.begin(), name.end());
  return e;
}

bool writeMidi(const std::filesystem::path& p, const Scenario& sc, const Result& r) {
  auto tickOf = [&](std::int64_t sample) { return std::llround(static_cast<double>(sample) / kSr * sc.bpm / 60.0 * kMidiDivision); };
  const std::int64_t endTick = tickOf(r.frames);
  std::vector<MidiEv> conductor{nameEv(sc.title)};
  const std::uint32_t usPerQuarter = static_cast<std::uint32_t>(std::llround(60e6 / sc.bpm));   // the tempo map: one tempo, the scenario's
  conductor.push_back({0, 0, {0xFF, 0x51, 0x03, static_cast<std::uint8_t>(usPerQuarter >> 16), static_cast<std::uint8_t>(usPerQuarter >> 8), static_cast<std::uint8_t>(usPerQuarter)}});
  conductor.push_back({0, 0, {0xFF, 0x58, 0x04, static_cast<std::uint8_t>(sc.beatsPerBar), 0x02, 0x18, 0x08}});
  std::vector<MidiEv> arpTrack{nameEv("arp")}, chordTrack{nameEv("chord")}, trigTrack{nameEv("triggers")};
  for (const OutNote& n : r.notes) {
    const std::int64_t on = tickOf(n.on), off = std::max(on + 1, tickOf(n.off < 0 ? n.on + 1 : n.off));
    const int ch = n.stream == Stream::Chord ? 1 : 0;
    auto& t = n.stream == Stream::Chord ? chordTrack : arpTrack;
    t.push_back({on, 1, {static_cast<std::uint8_t>(0x90 | ch), static_cast<std::uint8_t>(n.key), static_cast<std::uint8_t>(std::max(1, std::min(127, n.vel)))}});
    t.push_back({off, 0, {static_cast<std::uint8_t>(0x80 | ch), static_cast<std::uint8_t>(n.key), 64}});
  }
  for (const OutTrig& tg : r.trigs) {   // triggers: channel 10, one drum key per lane (36 + lane - 1)
    const std::int64_t on = tickOf(tg.at);
    const auto key = static_cast<std::uint8_t>(36 + tg.lane - 1);
    trigTrack.push_back({on, 1, {0x99, key, 100}});
    trigTrack.push_back({on + kMidiDivision / 16, 0, {0x89, key, 64}});
  }
  std::vector<std::vector<MidiEv>*> tracks{&conductor};
  for (auto* t : {&arpTrack, &chordTrack, &trigTrack})
    if (t->size() > 1) tracks.push_back(t);
  std::vector<std::uint8_t> file;
  for (char c : std::string("MThd")) file.push_back(static_cast<std::uint8_t>(c));
  file.push_back(0); file.push_back(0); file.push_back(0); file.push_back(6);   // header length 6, big-endian
  file.push_back(0); file.push_back(1);                                          // format 1
  file.push_back(0); file.push_back(static_cast<std::uint8_t>(tracks.size()));
  file.push_back(static_cast<std::uint8_t>(kMidiDivision >> 8)); file.push_back(static_cast<std::uint8_t>(kMidiDivision & 0xFF));
  for (auto* t : tracks) putTrack(file, *t, endTick);
  return writeFile(p, file);
}

std::string jstr(const std::string& s) {
  std::string o = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (static_cast<unsigned char>(c) < 0x20) o += ' ';
    else o += c;
  }
  return o + "\"";
}
std::string jnum(double x, int digits = 5) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.*f", digits, x);
  return buf;
}

std::string manifestEntry(const Scenario& sc, const Result& r) {
  const Flat defaults = flat(Settings{});
  auto sec = [&](std::int64_t s) { return jnum(static_cast<double>(s) / kSr); };
  std::string o = "    {\n";
  o += "      \"id\": " + jstr(sc.id) + ",\n";
  o += "      \"title\": " + jstr(sc.title) + ",\n";
  o += "      \"listen\": " + jstr(sc.listen) + ",\n";
  o += "      \"bpm\": " + jnum(sc.bpm, 3) + ",\n";
  o += "      \"beatsPerBar\": " + std::to_string(sc.beatsPerBar) + ",\n";
  o += std::string("      \"timeMode\": ") + (sc.s0.rateMode == RateMode::Time ? "true" : "false") + ",\n";
  o += std::string("      \"transportPlaying\": ") + (sc.playing ? "true" : "false") + ",\n";
  o += std::string("      \"directAudible\": ") + (sc.direct ? "true" : "false") + ",\n";
  o += std::string("      \"metronomeAudible\": ") + (sc.metronome ? "true" : "false") + ",\n";
  o += "      \"duration\": " + sec(r.frames) + ",\n";
  o += "      \"wav\": " + jstr(sc.id + ".wav") + ",\n";
  o += "      \"mid\": " + jstr(sc.id + ".mid") + ",\n";
  // Settings that differ from the default at any time in the run (not `enabled`: the module is on in every scenario).
  o += "      \"settings\": [";
  bool first = true;
  for (std::size_t i = 0; i < r.seen.size(); ++i) {
    const std::string& key = r.seen[i].first;
    const Seen& sn = r.seen[i].second;
    if (key == "enabled") continue;
    if (sn.vals.size() == 1 && sn.vals[0] == defaults[i].second) continue;
    o += first ? "\n        " : ",\n        ";
    first = false;
    o += "{\"key\": " + jstr(key);
    if (sn.overflow && sn.numeric) {
      o += ", \"sweep\": [" + std::to_string(sn.mn) + ", " + std::to_string(sn.mx) + "]";
    } else {
      o += ", \"values\": [";
      for (std::size_t j = 0; j < sn.vals.size(); ++j) o += (j ? ", " : "") + jstr(sn.vals[j]);
      o += "]";
    }
    o += "}";
  }
  o += first ? "],\n" : "\n      ],\n";
  o += "      \"extra\": [";
  for (std::size_t i = 0; i < sc.extra.size(); ++i) o += (i ? ", " : "") + jstr(sc.extra[i]);
  o += "],\n";
  o += "      \"marks\": [";
  for (std::size_t i = 0; i < sc.marks.size(); ++i) {
    const Mark& m = sc.marks[i];
    o += (i ? ",\n        " : "\n        ");
    o += "{\"t\": " + sec(m.at);
    if (!m.text.empty()) o += ", \"text\": " + jstr(m.text);
    if (!m.key.empty()) o += ", \"key\": " + jstr(m.key) + ", \"value\": " + jstr(m.value);
    o += "}";
  }
  o += sc.marks.empty() ? "],\n" : "\n      ],\n";
  // Arrays keep the file small: notes are [on, off, key, velocity, stream] (stream 0 = arp, 1 = chord); the others are described by their name.
  o += "      \"notesFormat\": \"[on seconds, off seconds, key, velocity, stream (0 arp, 1 chord)]\",\n";
  o += "      \"notes\": [";
  for (std::size_t i = 0; i < r.notes.size(); ++i) {
    const OutNote& n = r.notes[i];
    o += (i % 4 == 0 ? (i ? ",\n        " : "\n        ") : ", ");
    o += "[" + sec(n.on) + ", " + sec(n.off < 0 ? n.on + 1 : n.off) + ", " + std::to_string(n.key) + ", " + std::to_string(n.vel) + ", " + (n.stream == Stream::Chord ? "1" : "0") + "]";
  }
  o += r.notes.empty() ? "],\n" : "\n      ],\n";
  o += "      \"triggersFormat\": \"[at seconds, lane]\",\n";
  o += "      \"triggers\": [";
  for (std::size_t i = 0; i < r.trigs.size(); ++i) o += (i ? ", " : "") + std::string("[") + sec(r.trigs[i].at) + ", " + std::to_string(r.trigs[i].lane) + "]";
  o += "],\n";
  o += "      \"heldFormat\": \"[on seconds, off seconds, key, velocity]: the keys the player holds (the input, which Arp never echoes)\",\n";
  o += "      \"held\": [";
  for (std::size_t i = 0; i < sc.notes.size(); ++i) {
    const NoteSpec& n = sc.notes[i];
    o += (i % 4 == 0 ? (i ? ",\n        " : "\n        ") : ", ");
    o += "[" + sec(n.on) + ", " + sec(n.off) + ", " + std::to_string(n.key) + ", " + std::to_string(n.vel) + "]";
  }
  o += sc.notes.empty() ? "]\n" : "\n      ]\n";
  o += "    }";
  return o;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) { std::puts("usage: arp_render <output-dir>"); return 2; }
  if (plantsCompiledIntoLibrary()) { std::puts("FAIL  the library was built with ARP_PLANTS: not the shipped core"); return 1; }
  const std::filesystem::path out = argv[1];
  std::filesystem::create_directories(out);

  const std::vector<Scenario> scenarios = buildScenarios();
  std::vector<Result> results(scenarios.size());
  bool ok = true;
  for (std::size_t i = 0; i < scenarios.size(); ++i) {
    if (!run(scenarios[i], results[i])) ok = false;
    synth(scenarios[i], results[i]);
  }

  // One gain for every file, from the loudest sample anywhere, so the levels of the scenarios stay comparable.
  float peak = 0;
  for (const Result& r : results)
    for (std::size_t i = 0; i < r.left.size(); ++i) peak = std::max({peak, std::fabs(r.left[i]), std::fabs(r.right[i])});
  const double gain = peak > 0 ? 0.89 / static_cast<double>(peak) : 1.0;   // about -1 dBFS

  std::string manifest = "{\n  \"format\": \"arp-listening/1\",\n  \"sampleRate\": " + std::to_string(kSr) + ",\n  \"gain\": " + jnum(gain, 6) + ",\n  \"scenarios\": [\n";
  for (std::size_t i = 0; i < scenarios.size(); ++i) {
    const Scenario& sc = scenarios[i];
    if (!writeWav(out / (sc.id + ".wav"), results[i], gain) || !writeMidi(out / (sc.id + ".mid"), sc, results[i])) {
      std::printf("FAIL  could not write the files of %s into %s\n", sc.id.c_str(), out.string().c_str());
      ok = false;
    }
    manifest += manifestEntry(sc, results[i]) + (i + 1 < scenarios.size() ? ",\n" : "\n");
  }
  manifest += "  ]\n}\n";
  if (!writeFile(out / "manifest.json", std::vector<std::uint8_t>(manifest.begin(), manifest.end()))) {
    std::puts("FAIL  could not write manifest.json");
    ok = false;
  }
  std::printf("%s  %zu scenarios rendered into %s (common gain %.4f)\n", ok ? "PASS" : "FAIL", scenarios.size(), out.string().c_str(), gain);
  return ok ? 0 : 1;
}
