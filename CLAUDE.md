# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

JUCE audio plugin (VST3 / AU / Standalone) named **NiDMI Seq** — a MIDI sequencer that wraps the DSP/logic library `nidmi-sequencer-core`. The plugin is the thin presentation + host-integration layer; the sequencer engine (`SequencerEngine`, `SequencerClockDriver`, `SequencerCommandApi`, `SeqEvent`) lives in the sibling repo.

## Repository layout requirement

CMake uses a **relative** `add_subdirectory` to locate the core. The sibling repo **must** exist at `../nidmi-sequencer-core` relative to this dir:

```
repo/
  nidmi-seq-vst/          ← this repo
  nidmi-sequencer-core/   ← required sibling
```

If missing, configure will fail. Adjust `CMakeLists.txt` only if the user explicitly relocates the core.

## Build commands

All commands run from the `nidmi-seq-vst` repo root.

```bash
# Configure (Release). First run downloads JUCE 8.0.6 via FetchContent — needs network.
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build everything (AU + VST3 + Standalone)
cmake --build build --parallel

# Build just one format
cmake --build build --target NidmiSeq_Standalone --parallel
cmake --build build --target NidmiSeq_VST3 --parallel
cmake --build build --target NidmiSeq_AU --parallel

# Dev convenience target: plugin + AudioPluginHost (when option is ON)
cmake --build build --target nidmi_seq_dev_session --parallel

# Skip AudioPluginHost to speed up configure/build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DNIDMI_SEQ_BUILD_AUDIO_PLUGIN_HOST=OFF
```

Xcode generator requires `--config Release|Debug` on the build step as well.

### Running the core test suite

The core library (`nidmi-sequencer-core`) has a Catch2 test suite (85+ tests covering polyrythm, harmony, macros, chain VM, etc.). Tests are off by default when core is used as a subdirectory of this VST build, but can be built standalone from the core repo:

```bash
cd ../nidmi-sequencer-core
cmake -S . -B build-tests -DNIDMI_SEQ_BUILD_TESTS=ON
cmake --build build-tests --target nidmi_seq_core_tests --parallel
cd build-tests && ctest --output-on-failure
```

The VST project itself has no test suite.

## Artefact locations

`COPY_PLUGIN_AFTER_BUILD TRUE` installs **VST3 + AU only** into `~/Library/Audio/Plug-Ins/…`. The Standalone `.app` is *not* copied — it only lives in the build tree:

```
build/NidmiSeq_artefacts/<Config>/Standalone/NiDMI Seq.app   # Standalone (note the space)
build/NidmiSeq_artefacts/<Config>/VST3/NiDMI Seq.vst3
build/NidmiSeq_artefacts/<Config>/AU/NiDMI Seq.component
build/nidmi_AudioPluginHost/AudioPluginHost_artefacts/<Config>/AudioPluginHost.app
```

When invoking the Standalone, quote the path: `open "build/NidmiSeq_artefacts/Release/Standalone/NiDMI Seq.app"`. macOS may launch a stale copy from another build dir if you rely on Spotlight — always use an absolute/explicit path after rebuilds.

## The three differentiating pillars

NiDMI Seq is deliberately designed around three non-negotiable axes. All design decisions must preserve these:

### 1. Bar-relative homogeneous grid, per row
Not "step = fraction of a quarter note", but "the bar is divided into N equal steps". **Each row has its own N** (`PatternRow.numSteps`, 1–64). All rows realign on the first tick of each bar (V1). Polyrhythm emerges naturally: a row at N=16 and a row at N=11 diverge through the bar and reconverge at the next downbeat.

### 2. First-class harmony with chord progressions
`ChordProgression` (up to 32 slots) on each pattern, each slot = degree (I–VII) + quality (maj/min/dim/aug/7/maj7/m7/m7b5/dim7/minMaj7/sus2/sus4) + extensions (9/11/13 + alterations) + bassOffset + durationSlots. Rows resolve notes through **4 modes** (`RowHarmonyMode`) :
- **A**: scale degree (absolute in mother scale).
- **B1**: scale degree rerouted via current chord's root.
- **B2**: chord-tones (R/3/5/7/9/11/13) of the current chord.
- **Chromatic**: raw MIDI.

### 3. Hardware-first ergonomics
**Target hardware — FROZEN** (`nidmi-seq-hardware/docs/BOM.md`): colour TFT
**ILI9488 4,0″ 480×320 SPI, non-touch**, **5 push encoders** (EC11), **8 PB86
buttons**, 27 capacitive keys (16 white + 11 black) + ribbon, 3× ESP32-S3.

**Implemented today in the VST**: **5 push encoders** — `navEncoder_` (cursor),
`valueEncoder_`, `veloEncoder_`, `zoomEncoder_`, `masterEncoder_` (BPM, push
BPM/Pat) — keys + Shift. The count matches the target; the **roles do not**. The
vision assigns one fixed attribute per encoder (Cursor · Pitch · Velo · Gate ·
Master, identical in every view); today's roles are contextual and there is no
dedicated Gate encoder (gate hides behind a push toggle on Velo).

Do not conflate the two. The ergonomic reference is
**`VISION_ERGO_HARMONIE.md`** — designated source of truth by the hardware repo;
its §8 holds the vision/implemented table. `CAHIER_DES_CHARGES_V1.md` §10.1–10.2
predates it (2026-05) and is **superseded on the control surface and on the
encoder grammar**.

> Revision 2026-05 **supersedes** the original OLED 256×64 mono / 2-encoder
> spec. The old mono "2 rows × 4 columns / 8 params" layout survives only as a
> special case (GLOBAL page), not as the general display constraint. Do not
> quote the OLED figures as a limit — they are obsolete. The screen only
> *displays* state; all editing goes through keys and encoders.

## Architecture

### Layering

```
JUCE host  ──►  NidmiSeqAudioProcessor  ──►  SequencerEngine (core)
                        ▲    │                   ▲
                        │    │ drains SeqEvents  │
                UI / APVTS   ▼                   │
           NidmiSeqAudioProcessorEditor          │
                        │                        │
                        │ commands (lock-free)   │
                        └──► CommandFifo ────────┘ drained on audio thread
```

The processor owns the engine and is the *only* code that mutates it. The editor and any other non-audio path must go through `CommandFifo` / `VstSequencerController::postCommand`.

### Key components (Source/)

- **`PluginProcessor`** — `juce::AudioProcessor` subclass. Owns `SequencerEngine`, `SequencerClockDriver`, `CommandFifo`, `HostTimeMapper`, `MidiClockTransport`, `VstSequencerController`, and the `AudioProcessorValueTreeState` (APVTS). Its `processBlock` is the hub: it resolves the active time source, drains commands, syncs APVTS → engine, ticks the clock, and emits generated notes into the outgoing `MidiBuffer`.

- **`VstSequencerController`** — thin facade the UI (or state loader) calls to enqueue `SequencerCommand`s or apply a loaded `ValueTree` pattern. Never touches the engine directly.

- **`CommandFifo`** — lock-free SPSC queue (JUCE `AbstractFifo`, capacity 128) carrying `SequencerCommand`s from non-audio threads to the audio thread. Drained once per `processBlock`.

- **`HostTimeMapper`** — when the plugin runs inside a DAW and `followHost` is on, snapshots the host `AudioPlayHead` (PPQ, play state, BPM) and converts to the engine's microsecond time base.

- **`MidiClockTransport`** — alternative time source: interprets incoming MIDI Start/Continue/Stop + Timing Clock (24 ppqn), estimates BPM from clock intervals via EMA, and yields transport events aligned to sample positions. Used when `useMidiClock` is enabled (typically in Standalone).

- **`PatternValueTree`** — serialization bridge between `SequencerEngine` state and `juce::ValueTree` (used in `getStateInformation` / `setStateInformation`). Plugin state XML has two children: the APVTS `"PARAMETERS"` tree and the `PatternValueTree` tree.

- **`PluginEditor` + `HardwareStyleComponents`** — hardware-style UI mirroring the
  target panel: colour screen (`PatternScreen`, pages PAT / ROLL / HARM / AUTO /
  GLOB / SONG), transport buttons, **4 push encoders today (target: 5)**, and a
  16-white + 11-black keys pad with Shift. *Not* the obsolete OLED / 2-encoder
  layout — see pillar 3.
  Screen painting is split per page (`ScreenPatternView`, `ScreenRollView`,
  `ScreenHarmonyView`, `ScreenAutoGlobalView`); geometry helpers shared by painting
  **and** hit-testing live in `HardwareStyleInternal.h` (`computePrLayout`,
  `computeHarmLayout`, `DurationBand`). Most settings live in APVTS and are
  automatable.

- **`DeviceProfile`** — synth profiles loaded at runtime from
  `~/Documents/NiDMI/Profiles/*.json`: CC map, labels, panel geometry, MIDI-learn
  remapping. `Profiles/FORMAT.md` documents the schema; `Profiles/` ships the
  Kobol Expander and Waldorf M.

### Core engine model — bar-relative sampling

The engine uses **`engine.tick(nowUs)`** (not step-by-step advance). Algorithm:

```
barElapsed = nowUs - barStartUs
if barElapsed >= barDurationUs:
    barStartUs += barDurationUs
    reset all row states
    advance progression, emit PatternLooped
for each row r:
    step = (barElapsed * row.numSteps) / barDurationUs
    if step != rowState[r].lastStepIdx:
        triggerRowStep(r, step, nowUs)
```

Consequences:
- Stateless row transitions (only `lastStepIdx` per row).
- No drift — each step position is re-computed from the bar anchor.
- `setRowSteps(r, N)` mid-playback just updates the formula ; realignment happens at next bar.
- **`advanceMainStep` kept as alias for `tick`** (backward compat for callers that don't know about bar-relative).

The `SequencerClockDriver` sub-iterates within a DAW block at ≤0.5 ms resolution to never miss a row transition, regardless of block size.

### Time source selection in `processBlock`

Three mutually exclusive modes (resolved per block):

1. **MIDI clock** (`useMidiClock`): time advances from Timing Clock ticks; transport follows Start/Continue/Stop.
2. **Host transport** (`followHost` && not Standalone && not MIDI clock): time + BPM come from the DAW playhead; Play/Stop edges are forwarded to the engine.
3. **Internal / manual**: time advances from an internal `int64_t` counter accumulated per block; Play/Stop come from manual UI commands.

Switching modes resets transport edges, the internal time accumulator, the clock driver, and MIDI clock state.

### Key core types (StepTypes.h)

- **`PatternRow`** — per-row `numSteps`, `channel`, `kind` (Note/CC), `ccNumber`, `harmonyMode` (A/B1/B2/Chromatic), `muted`, `steps[kMaxSteps]`.
- **`StepData`** — `note`, `velocity`, `gate`, `enabled`, `subPatIdx`, `accent`, `swingEnable`, **`ccLocks[8]`** (P-locks: 8 CC slots per step, emitted before NoteOn).
- **`SubPattern`** — up to 16 sub-steps, own `numSteps`/`duration`, own harmony/timing override. Triggered by a main step; subdivision locked in to the triggering row's N.
- **`ChordSlot` / `ChordProgression`** — up to 32 slots per pattern ; see pillar 2.
- **`ProjectSettings`** — `masterBpm`, `masterRootPc`, `masterScaleId`, `macros[8]`. Shared at project level (V1 single-pattern uses it for master tonality + macros).
- **`Macro`** — value 0..127 + 8 `MacroDestination` (channel/ccNumber/depth/bias). `setMacroValue` cascades: emits CC on every active destination.
- **`ChainVM` / `ChainProgram`** — 12 opcodes (PlayPattern, RepeatBegin/End, Segno, DalSegno(AlCoda), Coda, ToCoda, DaCapo(AlCoda), Fine, End). 64 slots, 8-deep repeat stack. Not yet wired to the engine (V1.5 when multi-pattern lands).

### APVTS parameters (identifiers used in `PluginProcessor.cpp`)

`followHost`, `useHostBpm`, `useMidiClock`, `bpm`, `loop`, `numSteps` (1–64), `numRows` (1–16), `tsNum` (1–16), `tsDen` (choice `{1,2,4,8,16}`), **`masterRoot`** (choice of 12 pitch classes), **`masterScale`** (choice of 12 scale names). Changes are diffed against `lastXxx_` members and converted to `SequencerCommand`s.

## MIDI interop: two time models, by design

Nested tuplets have **no representation in a MIDI file** — the engine samples
`step = (barElapsed × N) / barDuration`, a SMF is a flat list of timestamps.
`MidiExporter` therefore has two modes: `Bake` (flattened, any DAW reads it) and
`Full` (Bake **+ `0xFF 0x7F` Sequencer-Specific Meta** carrying the whole project
as a serialised `ValueTree`). Export is implemented; **import is not** — the
round-trip is half built.

The spec (`CAHIER_DES_CHARGES_V1.md` §9.4, revision 2026-08) keeps a **parallel
MIDI clip player** alongside the tuplet engine, as `RowKind::MidiClip`: same
transport and bar anchor, different "what plays now" function, original timing
preserved instead of quantised.

Its blocker is **layout, not volume** — measured, not assumed: a dense 10-track
MIDI file decodes to ~25 000 channel events ≈ **194 KB**, about the cost of *one*
pattern (205 KB at `kMaxBars` = 8), i.e. 2.4 % of an ESP32-S3 N16R8's 8 MB
PSRAM. The obstacle is that `PatternRow` is a **fixed** 12 301-byte struct with
nowhere to put variable-length data; it needs a separate event pool indexed per
row. Streaming from flash would save ~180 KB out of 8 MB and cost flash latency
on the timing path — only worth it on a board **without** PSRAM.

**This intention was lost once already**: only the "convert on import" half
survived into the spec. Do not quietly drop it again when reasoning about MIDI
interop.

## When adding source files

`CMakeLists.txt` lists sources explicitly in `target_sources(NidmiSeq PRIVATE …)`. Add new `.cpp` files there; headers under `Source/` are picked up via the include directory.

## Gotchas

- Don't remove `C` from `project(... LANGUAGES C CXX)` — JUCE needs it.
- The product name contains a space (`"NiDMI Seq"`). Always quote paths in shell commands.
- macOS may silently open a stale copy of the Standalone app from a previous build directory if invoked without an explicit path; use `open "<absolute path>/NiDMI Seq.app"` after rebuilds.
- The audio thread must never take locks or allocate — go through `CommandFifo` for any engine mutation from the UI.
- `setSteps(N)` (legacy) sets every row to N. For polyrhythmic patterns, use `setRowSteps(row, N)` per row.
- When writing tests that call `tick(nowUs)` multiple times, **nowUs must be monotonic**. Driver resets its accumulator if it detects a time jump backwards.
- Subpatterns run **one per row, up to `kMaxRows` in parallel** (`runningSub_[kMaxRows]`,
  `hasActiveSubPattern(row)`). The old "single global `RunningSubPattern`" limit is gone.
- **`RowKind::CC` is played by the engine** (`triggerRowStep`): the step's `note`
  field carries the 0–127 controller value, and `row.ccInterp` (`Step` / `Linear` /
  `Smooth`) interpolates *between* steps via `emitInterpolatedCC()`. Rate is capped
  per row (10 ms) **and** globally (`setCCRateBudget()`, default 1000 msg/s,
  round-robin across rows) — 16 lanes without the global budget measured 1616 msg/s
  against MIDI DIN's 1041 ceiling. P-locks (`ccLocks[8]`) and macros remain the two
  other CC emission sites.
- **A relative subpattern anchors on the RESOLVED host note.** `triggerRowStep`
  runs `resolveDegreeToMidi` with the row's per-bar harmony mode *before* handing
  the note to `startSubPattern`, so a relative sub follows the modulation. Any UI
  code computing an offset against the host note must resolve it identically —
  `resolvePlayedNote()` in the editor is the single point for that. It used to
  exist in three copies (engine, `buildScreenModel`, `subHostNote()`), and the
  third didn't resolve at all: a pitch clicked in a relative sub was stored with
  the wrong offset and played somewhere else.
- **One object, one renderer — a mode must never fork the drawing path.** The wider
  form of the geometry rule below, and the one that keeps coming back. In the design
  simulation, entering a step made the selected row fall into a *second* branch that
  drew one equal-width cell per step: `span` ignored, covered steps ignored, playhead
  gone — the duration stopped being visible at the exact moment you were editing it.
  The fix is never to re-implement the layout in the second branch (that makes a third
  geometry); it is to delete the branch and let the mode change only what goes *inside*
  the shapes. Grep test: if a rendering branch is selected on a *state* flag rather than
  on the kind of object being drawn, it is the bug.
- **Paint and hit-test must share one geometry.** Bitten twice: the sub-roll
  recomputed `withTrimmedLeft(26.0f)` separately for clicks (now `kPitchGutterW`),
  and the HARMONIE page hit-tested on `bodyArea_` while painting on `gridArea()`
  — so with a measure strip every band was offset and clicking the tonality lane
  selected a chord. Neither showed anything abnormal on screen.
- **Don't encode information "downwards" from the near-black background.**
  `kScreenBg` is (10,12,10): darkening an out-of-scale lane by 34 % black bought
  3.7 of luminance contrast where the chord tint had 24 — the scale was drawn and
  invisible. Scale/chord now ride two independent axes (neutral grey / green).
- **AUTO is THE controller page; ROLL is pitch only.** Two mechanisms sit side by
  side in AUTO's slot band: the leftmost **LANE** cell (`autoSlot == kAutoLaneSlot`,
  i.e. -1) is the row itself as an automation (`RowKind::CC`, own N, interpolation),
  then the 8 **P-lock** slots (per-step values on any row, nothing to interpolate
  between). Rendering CC rows in ROLL as well was tried and reverted — ROLL and AUTO
  are both the detail view of the selected row, so it made AUTO redundant.
  The row's Note/CC type is toggled from AUTO (push Param) — it used to require a
  detour through GLOB.
- **The interpolation curve drawn in AUTO must match `emitInterpolatedCC`**,
  including the **loop wrap**: the engine looks for the next enabled step with
  `(step + k) % N`, so it interpolates from the last step back to the first across
  the bar line. Stopping the curve at the last step implied a final plateau that
  does not exist.
- `ChainVM` (song mode) **is wired**: `SetSongMode` command, `advanceChainAtPatternLoop()` at the pattern loop edge (or at cadence resolution when the pattern has a chord progression), serialised in `PatternValueTree`, covered by the core tests (219 pass). Toggle lives on the SONG page, key index 4. What is still deferred to V2 is **simultaneous multi-pattern playback** — only one pattern plays at a time.
- Default MIDI channel is **one per row** (row 0 → ch 1, … row 15 → ch 16), set in the `Pattern` constructor. `PatternRow`'s own default is 0, so without it every row spoke on channel 1 — useless for a multitrack sequencer driving several machines.

## Reference documents

- **`SUITE.md`** — where the work stands and what comes next: repo/branch state,
  what is verified vs merely compiled, the ordered backlog, and the practical
  gotchas (relaunch after build, driving the UI, forcing a rebuild). **Read this
  first when resuming.**
- **`CAHIER_DES_CHARGES_V1.md`** — complete V1 specification (design rationale, data model, ergonomic grammar, ESP32-S3 target, roadmap V1/V1.5/V2).
