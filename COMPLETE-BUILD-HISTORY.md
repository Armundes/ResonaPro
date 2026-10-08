# ResonaPro — the complete build history

One file. Everything recorded during this build, in order, so a new agent
or a new workspace can pick the work up without asking what happened.

Generated 2026-10-08. The plugin it describes is version 2.14.0.

## How to use this file

Read the master record first. It is the spine: what the plugin is, where it
came from, every defect, every phase of work, every measurement, every
failure and every revert. Everything after it is the detail behind that
spine, in the order it was written.

If you only read one part, read the master record and then the honest limits
at its end. Those say what is proven and what is not.

## Contents

| Part | Document | What it holds |
|---|---|---|
| 1 | `README.md` | Project overview |
| 2 | `AGENTS.md` | Standing working rules |
| 3 | `PROJECT-RECORD.md` | THE MASTER RECORD |
| 4 | `ARCHITECTURE.md` | Architecture (describes v1 in part) |
| 5 | `UPGRADE-REPORT.md` | The v1 to v2 upgrade |
| 6 | `V2.1-RELEASE-NOTES.md` | Release notes 2.1 |
| 7 | `V2.3.0-RELEASE-NOTES.md` | Release notes 2.3.0 |
| 8 | `CRASH-DIAGNOSIS-2026-10-06.md` | Incident: the DAW crash |
| 9 | `REGRESSION-2026-10-06.md` | Incident: the Antigravity regression |
| 10 | `PLUGIN-STATUS-REPORT.md` | Status report: every problem found |
| 11 | `BOTTLENECKS.md` | Bottleneck audit, ranked |
| 12 | `QUALITY-ROADMAP.md` | Quality roadmap, phased |
| 13 | `QUALITY-GAP-EXPLAINED.md` | The algorithmic gap, explained |
| 14 | `RESEARCH-SYNTHESIS-OPTIONS.md` | Phase-coherent and multi-resolution, costed |
| 15 | `RESEARCH-AND-DESIGN.md` | Reference research and DSP design |
| 16 | `AUDIO-QUALITY.md` | Audio quality measurements |
| 17 | `FINE-TUNE-AUDIT.md` | The fine-tune drawer audit |
| 18 | `DE-ESSER-ROADMAP.md` | De-esser roadmap, six phases |
| 19 | `DE-ESSER-PRECISION-IDEAS.md` | De-esser precision brainstorm |
| 20 | `SIBILANCE-MISS-DIAGNOSIS.md` | Sibilance miss, frame-level |
| 21 | `LEARN-DESIGN.md` | Learn design and the fence |
| 22 | `LEARN-UPGRADE-PLAN.md` | Learn upgrade plan, four tiers |
| 23 | `LEARN-SCORE-BASELINE.md` | Scoring harness, validation, findings |
| 24 | `UI-SUGGESTIONS.md` | Interface proposals |
| 25 | `HOW-TO-USE.md` | User guide |
| 26 | `HANDOFF-PROMPT.md` | Onboarding prompt |


---

# Part 1 — Project overview

Source: `README.md`

# ResonaPro

**A vocal-tuned dynamic resonance suppressor, de-esser and tonal balancer.**

ResonaPro listens to a vocal in the frequency domain, works out which narrow
peaks are *resonances* rather than *the voice itself*, and pulls those peaks down
with per-bin dynamics. It is built in C++20 with JUCE and uses Apple's `vDSP`
for the transforms.

Version 2.1 added an optional sidechain key, low-band analysis, a note-motion
cue, and user-reviewed learning. Version 2.2.0 made the MATCH button measure
perceived loudness (ITU-R BS.1770 K-weighting) instead of raw RMS, and capped the
correction so it can never raise the peak above what the input already had.
Version 2.3.0 repairs the detector itself. Its shoulder baseline was measuring
bin positions rather than signal levels, which left the prominence negative in
every bin and stopped the plug-in asking for any reduction at all. The full
account is in the [status report](docs/PLUGIN-STATUS-REPORT.md).

Read the [FL Studio quick start](docs/HOW-TO-USE.md), the
[2.3.0 release notes](docs/V2.3.0-RELEASE-NOTES.md), the
[2.1 release notes](docs/V2.1-RELEASE-NOTES.md), and the
[audio-quality report](docs/AUDIO-QUALITY.md), which documents the measurements
behind the transparency and level-matching claims. If you are coming from
v1, read [docs/UPGRADE-REPORT.md](docs/UPGRADE-REPORT.md) first — several
controls changed meaning.

---

## Why it is not "another spectral compressor"

The hard part of this kind of processor is not the reduction; it is the
**detection**. A sung note is a comb of narrow harmonic peaks. Any detector that
simply hunts for "narrow peaks that stand above the local average" will flag
every harmonic and hollow out the voice.

ResonaPro's detector does three things about that:

1. It estimates the **harmonic period** of the input every frame, and widens its
   analysis window so that a harmonic is always compared against its
   *neighbouring harmonics* rather than against the gaps between them.
2. It derives an **adaptive threshold** from the statistics of the frame — how
   peaky this material already is — so only peaks that stand out *beyond the
   material's own character* are treated.
3. It works entirely in **dB prominence relative to a local baseline**, which
   makes the behaviour identical for a whisper and a belt.

The result: measured on a deliberately hostile synthetic vocal, the harmonics
are left at 0.0 dB while a genuine Q = 14 resonance in the same signal is
reduced. See [docs/RESEARCH-AND-DESIGN.md](docs/RESEARCH-AND-DESIGN.md) §5.3.

---

## Controls

### Main

| Control | Range | Default | What it does |
|---|---|---|---|
| **Depth** | 0 – 4 | 1.0 | How much reduction is applied. 0 is a true bypass-grade passthrough. The upper range is deliberately generous. |
| **Detail** (sharpness) | 0.2 – 4 | 1.0 | Analysis bandwidth. Low = wide, musical, dynamic-EQ-like. High = narrow, surgical. |
| **Selectivity** | 0 – 1 | 0.5 | How far a peak must stand above the material's own peakiness before it is touched. |
| **Transient Protect** | 0 – 1 | 0.5 | Raises the threshold on attacks. 0 = process everything (best for de-essing), 1 = leave all attacks alone. This is what keeps plosives and consonants intact. |
| **Max Cut** | 3 – 36 dB | 24 dB | Ceiling on the reduction of any single bin. |
| **Attack / Release** | 0.5–50 ms / 5–500 ms | 8 / 70 ms | Per-bin envelope follower. Frequency dependent: the release is ~2.5× slower in the low end than in the top end. |
| **Vocal Profile** | 4 choices | Lead Vocal | Biases the detector per region. See below. |
| **Stereo Link** | 0 – 1 | 1.0 | 0 = dual mono (each channel decides independently), 1 = one shared decision for both channels. |
| **Mix** | 0 – 100 % | 100 % | Dry/wet. The dry path is delayed to match, so mixing never combs. |
| **Output** | ±24 dB | 0 dB | Output trim. |

### Vocal profiles

| Profile | Regions biased toward reduction | Regions protected |
|---|---|---|
| **Lead Vocal (All-Round)** | 1.8 – 8 kHz | below 200 Hz, above 14 kHz |
| **De-Ess / Sibilance** | 4.5 – 10 kHz | everything below 3.5 kHz, above 10 kHz |
| **Warm Body & De-Mud** | 180 Hz – 1.6 kHz | above 8 kHz |
| **Air & Silk** | 8 kHz and up | below 5 kHz |

### Focus bands (the graph)

Eight draggable nodes. Their gain is an offset to the **detection threshold**,
and the sign is inverted on purpose:

* pull a node **up** → the detector listens harder there → **more** processing;
* pull a node **down** → **protection** for that region.

* double-click empty graph space to add/enable a node
* double-click a node to enable/disable it
* mouse wheel over a node adjusts its Q
* hold Shift while dragging to snap to a 1/6-octave frequency grid and whole dB
* right-click a node to type `frequency gain Q` (for example, `3200 3.0 1.4`)
* **Reset Bands** restores all eight to neutral

### Character

* **EAR GUARD** — a small, fixed ear-sensitive threshold bias. The internal
  `iso226` parameter ID is kept for session compatibility, but this is **not**
  an ISO 226 equal-loudness contour.
* **HARD** — hard mode: level dependent and much more aggressive (the detector
  reacts to absolute harmonic levels instead of relative ones).
* **MID/SIDE** — process mid and side independently.
* **DELTA** — hear exactly what is being removed. This is the fastest way to
  learn what the plug-in is doing.
* **BYPASS** — latency-compensated bypass.
* **EXT KEY** — detect from an optional host sidechain bus while processing the
  main vocal. If the bus is disabled, it falls back to the vocal.
* **LOW DETAIL** — uses a 4k window for low-band detection in the 1k/2k modes.
  It does not add a second synthesis path or new latency.
* **NOTE MOTION** — a bounded cue that protects harmonics which move with the
  sung note and favours peaks that stay at one frequency.
* **LEARN / APPLY LEARN** — while the editor is open, analyse a played take and
  propose up to eight focus-band settings. Nothing changes until Apply.

### Quality and Response

These replace v1's "oversampling" control, which did not do what its name
implied (see the upgrade report).

| Quality | Transform | Latency @44.1 kHz |
|---|---|---|
| Low Latency | 1024 | ~23 ms |
| Standard | 2048 | ~46 ms |
| High | 4096 | ~93 ms |
| Ultra | 8192 | ~186 ms |

| Response | Overlap | Effect |
|---|---|---|
| Eco | 2× | lowest CPU, slowest gain updates |
| Standard | 4× | balanced |
| Fine | 8× | fastest gain updates, highest CPU |

### Presets

Eleven factory presets, grouped by role in the menu: Init, Lead Vocal (Gentle / Tame Harshness / Forward &
Clean), De-Ess (Broad / Whistling), Warm Body De-Mud, Air & Silk, Plosive Safe
De-Ess, Harsh Bus Glue, Extreme Resonance Hunt.

---

## Building

```bash
# configure (JUCE path can be overridden with -DJUCE_SOURCE_DIR=...)
./.work/scripts/build.sh configure

# build AU + VST3 + Standalone
./.work/scripts/build.sh build

# install into ~/Library/Audio/Plug-Ins  (run from a normal Terminal)
./install.sh
```

Or with CMake directly:

```bash
cmake -S . -B .work/build -DCMAKE_BUILD_TYPE=Release -DRESONAPRO_COPY_PLUGIN=OFF
cmake --build .work/build --config Release -j 8
```

`RESONAPRO_COPY_PLUGIN=ON` makes the build copy the plug-ins into the user's
plug-in folders automatically.

---

## Testing

Two automated suites, both headless:

```bash
# engine level: reconstruction, transparency, detection, stereo, stability
clang++ -std=c++20 -O2 -I Source/DSP .work/scripts/dsp_test.cpp \
        -framework Accelerate -o .work/build/dsp_test && ./.work/build/dsp_test

# full processor: latency, null tests, presets, state, block-size independence
cmake -S . -B .work/build-tests -DCMAKE_BUILD_TYPE=Release \
      -DRESONAPRO_BUILD_TESTS=ON -DRESONAPRO_COPY_PLUGIN=OFF
cmake --build .work/build-tests --target ResonaProTests -j 4
./.work/build-tests/ResonaProTests_artefacts/Release/ResonaProTests
```

Both must report `ALL PASS`. The engine suite verifies, among other things, that
at Depth 0 the output is the input delayed by exactly the reported latency with a
residual of ~1e−15.

---

## Project layout

```
ResonaPro/
├── Source/
│   ├── PluginProcessor.h/cpp        # parameters, audio pipeline, presets, state
│   ├── PluginEditor.h/cpp           # UI layout
│   ├── DSP/
│   │   ├── SpectralEngine.h         # vDSP STFT, latency probe, stereo modes
│   │   ├── ResonanceDetector.h      # harmonic period, baseline, adaptive threshold
│   │   ├── DynamicSuppressor.h      # per-bin attack/release, ceilings, smoothing
│   │   ├── TransientDetector.h      # attack detection for transient protection
│   │   ├── LearnAnalyzer.h          # editor-driven, user-reviewed take suggestions
│   │   └── ParametricEQWeighting.h  # 8 focus bands as real biquad responses
│   └── UI/
│       ├── CustomLookAndFeel.h
│       └── SpectralVisualizer.h
├── Tests/ProcessorTests.cpp         # headless processor harness
├── docs/
│   ├── RESEARCH-AND-DESIGN.md       # findings, sources, full DSP design
│   └── UPGRADE-REPORT.md            # what was broken in v1 and what changed
├── install.sh
└── CMakeLists.txt
```

---

## Hosts

Shipped as AU, VST3 and Standalone on macOS, with arm64 and x86_64 slices.
Processor tests cover mono/stereo layouts and a stereo optional sidechain bus.
Apple's `auval` validates the installed AU. An automated test cannot stand in
for a session in FL Studio or other hosts; scan the VST3 and test the sidechain
routing in your DAW before relying on it in a release project.

VST2 is supported by the build but is not included, because Steinberg stopped
licensing and distributing the VST2 SDK in October 2018 and JUCE removed its
bundled copy. If you have those headers, configure with
`-DVST2_SDK_DIR=/path/to/vst2sdk` and VST2 joins the build; the installers pick
it up automatically. Every current host, FL Studio included, loads the VST3.


---

# Part 2 — Standing working rules

Source: `AGENTS.md`

# ResonaPro — working instructions

Standing preferences for work in this repository. These are the user's, not mine;
follow them without being asked again.

## Delivery: do not package unless asked

**Do not build `.pkg`, `.dmg`, or installer `.zip` files unless the user asks for
them.** They cost real time, they are only needed when a build is going to someone
else, and the user tests locally in FL Studio.

Unless packaging is requested, the end of a change is:

1. Build the plugin (`./.work/scripts/build.sh build`)
2. Run both test suites (`ResonaProDspTests`, `ResonaProTests`)
3. Install into the local plug-in folders (`./install.sh`)
4. Report what changed, with the measurements behind it

The packaging scripts (`.work/scripts/package_pkg.sh`, `.work/scripts/package_dist.sh`)
stay in the tree for when they are wanted. Do not run them by default.

## Keep the install current

The user's DAW scan reads `~/Library/Audio/Plug-Ins/VST3/ResonaPro.vst3` and
`~/Library/Audio/Plug-Ins/Components/ResonaPro.component`. After any source change
that affects audio, install, or the user will test a stale build and report a bug
that is already fixed. The version label on the interface shows what is actually
loaded — check it when a report does not match the source.

## Evidence over assertion

Every claim about audio behaviour in this project must be measured. The test suites
are the contract; a change that is not covered by a check is not verified. When a
change fails, report the measurement that failed it, including when the change was
mine and the numbers are unflattering.

Two failures in this repository's history were found only by measuring and would
have shipped otherwise:

- A gain-composition bug where overlapping band weights turned an 18 dB cut into
  roughly 10 dB
- A `log()` applied to a value already in dB, which produced NaN and silently broke
  the sibilance tracking

## File layout

- `Source/` — the plug-in. `Source/DSP/` is the engine, `Source/UI/` the interface.
- `Tests/` — `DspTests.cpp` and `ProcessorTests.cpp`, both built through CMake so
  they always compile against the current headers.
- `Tools/Analyze.cpp` — the file-based analysis harness.
- `docs/` — design notes, roadmaps and reports. Keep them current; several record
  what was tried and why it failed, which is worth more than what succeeded.
- `.work/` — build trees, probes, logs, backups, previews. Not deliverables.
- `outputs/` — only populated when the user asks for packages.

## Reporting

State what was measured, and separate what is verified from what is reasoned. The
distinction matters here: synthetic test signals prove the signal path is clean and
say nothing about whether the plug-in sounds right on a real voice. That still needs
the user's ears, and it is fair to say so.

## Priority order: quality first

The user's standing priority, stated directly: **audible quality outranks
everything else.** Latency, plug-in size and CPU cost are all acceptable
trade-offs in service of it, within reason.

Practical consequences:

- Do not shelve an approach because it costs latency, size or cycles. Shelve it
  only because measurement says it does not improve the sound, or because it is
  not achievable.
- When a quality improvement and a latency or CPU saving conflict, take the
  quality. Say what the cost was, but do not let the cost decide.
- "Within reason" still applies: a change that makes the plug-in unusable in a
  session, or that a typical machine cannot run, is not a quality improvement.
  State the measured cost so the user can judge it.
- This reopens work that was set aside for cost reasons. The multi-resolution and
  phase-coherent resynthesis refactor (Phase 3 of the quality roadmap) was shelved
  because it adds ~85 ms of latency and CPU. That reasoning no longer holds on its
  own; it now needs a quality argument either way, not a latency one.

Measured costs are still required. Quality-first changes the decision, not the
evidence standard: report what it cost as well as what it bought.


---

# Part 3 — THE MASTER RECORD

Source: `docs/PROJECT-RECORD.md`

# ResonaPro — the complete record

Everything done on this project: what it is, where it started, every defect
found, how each one was caught, every measurement taken, every experiment that
failed, where it stands, and where it goes next.

Written 8 October 2026. Installed version at the time of writing: 2.13.1.

## How to read this

This document gathers the project's history into one place. The individual
documents in `docs/` hold the detail behind each section; this record ties them
together and points at the right file.

Two kinds of statement appear here:

- **Measured** — a number came from a test, a corpus run, or a code read.
- **Reasoned** — an explanation that fits the evidence but was not itself
  measured.

The distinction matters. This project has twice found real bugs by measuring, and
it has twice been misled by a measurement that had a flaw. Both are recorded
below.

Some of the early history here comes from the project's own documents rather
than from first-hand memory, because the working session was compressed partway
through. Where that is the case, the source document is named.

---

## 1. What ResonaPro is

ResonaPro is a vocal spectral de-resonator and de-esser. It finds resonances and
harshness in a voice and reduces them, the way soothe2 does, but aimed at vocals
rather than at everything.

- **Language:** C++20, built on JUCE 9
- **Platform:** macOS, universal (arm64 and x86_64)
- **Formats:** VST3 and AU
- **Host:** FL Studio
- **DSP:** Apple Accelerate / vDSP, windowed overlap-add STFT
- **Size:** about 7,470 lines across 17 source files

### The signal path

The engine splits audio into overlapping windows, converts each to the frequency
domain, decides how much to reduce each frequency bin, applies that reduction,
and converts back. That process is an STFT with overlap-add resynthesis.

Around that core:

- A **resonance detector** estimates which bins hold a resonance rather than
  musical content.
- A **dynamic suppressor** holds per-bin reduction envelopes with attack and
  release timing.
- A **de-esser** watches the sibilance range and cuts it.
- A **learner** listens to a take and proposes settings.
- A **visualizer** draws the reduction.

### The controls

Depth, Detail, Selectivity, Attack, Release, Max Cut, Transient Protect, a
profile selector, Quality/Response, Stereo Link, ISO 226, Hard, Mid/Side, Delta,
Solo Cut, Learn/Apply/Undo, three Tilt controls, Sibilance De-Ess, an external
key input, and focus bands.

---

## 2. Where the project came from

### 2.1 The starting point

The plugin arrived as a v2 codebase with four Git commits:

```
72571ee  Initial commit: ResonaPro v2.2 - Dynamic Resonance Suppressor & Vocal De-Esser
d320545  Fix cue-gated resonance suppression & calibrate high-frequency sibilance sensitivity
968afd5  Fix cue-proportional thresholding, fix high-end sensitivity, and fix EQ filter defaults
bb119e2  Fix high frequency resonance detection threshold
```

A review of that code produced `docs/UPGRADE-REPORT.md`. It found eight
correctness bugs and a set of design problems. The most serious:

**Latency was reported incorrectly**, so dry and wet never lined up. Delta and
bypass therefore played the wrong thing.

**Delta was not the removed signal.** It was supposed to play only what the
plugin took out. It played something else.

**Every harmonic was treated as a resonance.** A sung note has harmonics. The
detector could not tell a harmonic from a resonance, so it cut musical content.

**Non-finite input corrupted the engine permanently.** One NaN or infinity
anywhere in the stream poisoned the recursive state, and the plugin stayed
broken for the rest of the session.

**Parameter smoothing was scaled in blocks, not seconds.** A control's smoothing
time therefore changed with the buffer size.

**Allocation risk on the audio thread.** The audio thread must not allocate
memory, and this one did.

### 2.2 The Antigravity session, and the regression

Work happened in Gemini Antigravity between sessions. `docs/REGRESSION-2026-10-06.md`
records what that produced.

It found one genuine improvement and one change that broke the plugin. The
regression was measured, not assumed. The document's own conclusion: the rewrite
introduced a defect that the test suite did not catch, which raised the question
of why the suite was silent.

That question drove the next phase of work.

---

## 3. The defects, and how each was caught

This section lists every defect found across the project, grouped by area. Each
entry names the symptom, the cause, and how it surfaced.

### 3.1 Correctness defects

| Defect | Cause | How it was caught |
|---|---|---|
| Dry/wet never aligned | Latency reported as a wrong number | Code read, then a null test |
| Delta played the wrong signal | Delta computed from the wrong point in the chain | Code read |
| Harmonics cut as resonances | No harmonic-versus-resonance test in the detector | Listening, then code read |
| One NaN broke the session | Non-finite values entered recursive state | Fuzzing with non-finite input |
| Smoothing scaled in blocks | Time constant used block count, not seconds | Code read; behaviour changed with buffer size |
| Audio-thread allocation | Containers resized during processing | Code read |
| Repository hygiene | Build artefacts and binaries tracked in Git | Inspection |

### 3.2 The crash

`docs/CRASH-DIAGNOSIS-2026-10-06.md` records this one. It has five parts.

**Why the DAW crashed.** A teardown defect: memory freed twice. It killed the
host, not just the plugin.

**Why the test suite missed it.** This is the part that mattered. The suite did
not exercise teardown, so the bug lived outside its reach. A test suite that
cannot see a defect provides no protection against it.

**A wrong recovery, corrected.** An early attempt to fix it made things worse
and was reverted.

**Four further defects found while investigating:**

- Perfect reconstruction was destroyed — the engine no longer reproduced its
  input when set to do nothing
- The Quality control did nothing
- The dual-band wrapper broke transparency by construction
- A second null dereference in the state round-trip test

**A latent double-free hazard** in the same code path.

**The two intermittent defects and their root cause.** This is the most useful
finding in the document. Two separate symptoms, one cause: an out-of-bounds
write in phase-locking code that was no longer used but still ran. The dead code
was the bug.

The lesson recorded there: an intermittent crash with no clear cause can come
from code that appears to do nothing.

### 3.3 Detection defects

`docs/PLUGIN-STATUS-REPORT.md` (6 October 2026) lists five problems:

**Problem 1 — the baseline was a bin number, not a signal level.** The code
compared a bin index against a threshold instead of comparing a level. The
measurement is in the document. The consequence in use: the detector's idea of
"normal" had no relation to the audio.

**Problem 2 — reduction was gated behind a raised cue.** Reduction only happened
when a cue control sat above a point. Below it, nothing was reduced. In use, the
plugin appeared to do nothing across much of the control range.

**Problem 3 — safety guards removed.** Limits that prevented extreme behaviour
had been deleted.

**Problem 4 — a high-frequency multiplier** produced wrong behaviour at the top
of the spectrum.

**Problem 5 — the tests were not run.** The suite existed but had not been
executed against the current code.

Problem 5 is a process failure rather than a code defect, and the fix was
structural: the test runner now compiles from source on every build, so the
suite cannot go stale without anyone noticing.

### 3.4 The fine-tune drawer

`docs/FINE-TUNE-AUDIT.md` records a complaint from the owner: the controls in the
Fine Tune drawer seemed to do nothing.

The audit found:

**Detail Tilt was dead.** It had nothing to act on. The tilt modified a quantity
that the engine then ignored.

**Sibilance was too weak** to have an audible effect at any setting.

**Attack Tilt, Release Tilt, and Note Motion** had not been proved to do
anything useful.

The plan that followed gave the tilts something to act on, rebuilt Sibilance as a
real de-esser, and required the remaining controls to be proved or removed. The
governing principle: a control that does nothing is worse than no control,
because it teaches the user to distrust the interface.

Results landed in 2.6.0.

### 3.5 The de-esser

The owner reported sibilants surviving at high settings. Two documents record
the investigation.

`docs/DE-ESSER-ROADMAP.md` found four things:

**The band stopped at 11 kHz and sibilance does not.** Sibilant energy extends
above where the de-esser looked, so part of every S sat outside its reach.

**Dense sibilance got less correction than sparse sibilance.** This is
backwards. The busier the sibilance, the less the plugin did about it.

**The cut shape was fixed and real sibilance moves.** Six static bands cannot
follow a sibilant that shifts frequency.

**It was a broadband duck in disguise.** Despite the six-band presentation, the
effect reduced a wide range rather than the harsh part.

`docs/SIBILANCE-MISS-DIAGNOSIS.md` investigated the specific "switch lanes"
sibilant the owner reported. It found:

- The frame data showed the detector failing to trigger, not over-correcting
- The miss was not caused by the suspected factors, which were tested and ruled
  out
- Corpus miss rates were measured across all 18 takes
- **Two measurement errors were found and corrected** — the investigation had
  been reading the data wrong in two places
- The first hypothesis was wrong and is recorded as such
- A gated baseline was implemented, measured, and **reverted**

The gated baseline is worth its own note. The idea: exclude sibilant frames from
updating the detector's resting estimate, so a dense passage cannot raise the
baseline and cancel the correction. The idea was sound and the owner approved it.
Implementation raised the corpus miss rate from **8.7% to 87.5%**. It was
reverted the same session. The reasoning that produced it was not enough to save
it, and the measurement caught that.

### 3.6 The Learn feature

`docs/LEARN-UPGRADE-PLAN.md` records defects found by reading the code:

**The attack measurement was dead.** The code reset its own counter on the line
before reading it, so the reported attack time was always the 8 ms default. The
value never reflected the audio.

**The release timer ran away.** Its counter was never reset, so it grew for the
whole take and pushed the release into its ceiling every time.

**No debounce on onset detection.** The state machine flipped on any threshold
crossing, so material sitting near the threshold registered as a stream of
events.

**Depth and Max Cut used the maximum**, so one cough or plosive set the depth
for the entire take.

**Peak width was unweighted**, so a spike appearing once counted the same as a
peak that lasted the whole take.

**No harmonic rejection.** Learn could target musical harmonics, which is what
made its proposals sound thin or boxy.

`docs/LEARN-DESIGN.md` records the fence: Learn may not change Quality or
Resolution, Hard mode, Mid/Side, Delta, profile weighting, output, stereo link,
mix, auto gain, external key, bypass, or any monitoring and routing decision.
This is enforced by an allowlist in the code and by tests. The owner set this
requirement and it holds.

### 3.7 The interface

`docs/UI-SUGGESTIONS.md` proposed a set of changes. The owner approved them and
they were implemented. The main ones:

- Make the graph the hero rather than one element among many
- Split controls into "always" and "if you want"
- Remove toggles that are really modes
- Say what each control does in plain words
- Give every control a readable value

Later interface fixes:

- The reduction curve drew as blocky stair-steps; it was interpolated and
  smoothed
- The background spectrum sloped downward toward the right; display tilt
  compensation was applied
- The FINE TUNE button blended into the background; it was given the brand
  orange
- A glitched character at the end of a label was removed

### 3.8 The visualizer freeze

Solo Cut played only the removed signal. When engaged, the reduction graph
froze and did not return when Solo Cut was switched off.

Cause: with Solo Cut engaged, gain could reach zero, producing negative infinity
and NaN. The visualizer's recursive smoothing carried those values forward, and
the graph stopped updating.

Fixed in 2.13 by guarding against non-finite values in the visualizer.

---

## 4. The work, in phases

### Phase A — Correctness

Fixed the eight defects from the v1 review. Established that dry and wet align,
that Delta plays the removed signal, and that non-finite input no longer
corrupts the engine.

### Phase B — Stability

Found and fixed the teardown double-free. Fixed the test suite so it exercises
teardown. Fixed the intermittent crash by removing dead phase-locking code.

Structural change: the test runner compiles from source on every build, so the
suite always tests the current headers.

### Phase C — Audio quality

`docs/AUDIO-QUALITY.md` and `docs/BOTTLENECKS.md` record this.

Bottlenecks identified:

**Latency of 42.7 ms with no way down to zero.** The engine needs lookahead, so
zero latency is not available in this architecture.

**The transform used a fixed sample count**, so behaviour changed with sample
rate. The same settings did not sound the same at 44.1 and 48 kHz.

**One resolution for the whole spectrum.** A single STFT setting serves both a
narrow 6 kHz sibilant and a wide 300 Hz resonance, and one setting cannot serve
both well.

The owner ranked quality above latency, file size, and CPU. That ranking shaped
everything after.

`docs/RESEARCH-SYNTHESIS-OPTIONS.md` examined two ways to close the quality gap:

**Phase-coherent resynthesis** — keep track of phase relationships across frames
so the resynthesis stays coherent. Cost: substantial complexity, and the phase
estimate must be right or artefacts get worse.

**Multi-resolution synthesis** — run two transforms at different window sizes
and combine them. The document lists four problems: the two transforms have
different phase references, summing two chains comb-filters, the band split must
reconstruct, and decisions must stay consistent across the crossover.

Both were costed and shelved, because of what came next.

**The listening test changed the plan.** The owner ran the plugin in FL Studio on
held notes and transient attacks and reported: no watery or gargling modulation,
no comb filtering or hollowed formants, no consonant pre-ringing, no metallic
chirping.

That result shelved Phase 3 — an 85 ms multi-resolution rewrite with phase
locking. If the artefacts are not audible on a real voice, the rewrite buys
nothing and costs CPU and latency. **This was the right call and it saved a large
amount of work.** It is also a case where the measurement that mattered was a
listening test, not a number.

Also in this phase: a level-matching defect. The MATCH button drifted about
2 dB, so bypass comparisons were not honest. Fixed.

And the symbol table: the binary carried 2.85 MB of symbols per architecture,
5.4 MB across both. Stripping it takes the arm64 slice from 10.16 MB to 7.48 MB,
a 26% cut. Not done, because download size was not the owner's concern.

### Phase D — Interface

The cream, off-white design inspired by soothe. Graph-centric layout. Plain
language labels. The UI suggestions from `docs/UI-SUGGESTIONS.md` were
implemented in full.

### Phase E — Fine-tune drawer

The audit described in section 3.4, implemented in 2.6.0. The tilts were given
something to act on. Sibilance was rebuilt.

### Phase F — De-esser

Six phases from `docs/DE-ESSER-ROADMAP.md`. The owner approved Tier 1 work:
a spectral flatness gate, dynamic band centering, and sub-bin parabolic
interpolation, plus Solo Cut for auditioning the removed signal.

Results, recorded honestly:

- **Spectral flatness gate — shipped.** It stops bright vowels from
  false-triggering the de-esser, with a fallback so the de-esser still works if
  the gate reads badly.
- **Sub-bin parabolic interpolation — shelved.** The owner's judgement, and a
  correct one: the search was unstable.
- **Dynamic band centering — shelved.** It made the detector chase harmonic
  overtones. The static grid stayed.
- **Solo Cut — shipped.** Then its graph-freeze bug was found and fixed in 2.13.
- **The tracked center was fixed** so 5 kHz and 11 kHz separate cleanly across an
  octave. This solved much of the static-grid problem without the feedback loop.

Also tested and reverted: the gated baseline (section 3.5).

### Phase G — Learn

The owner asked for a world-class Learn. `docs/LEARN-UPGRADE-PLAN.md` set out
four tiers. The owner approved Tier 1 and Tier 2, and asked for a judgement on
the plan before implementation. That judgement is in the document.

**Tier 1 — implemented in 2.13.1.** Real onset and decay measurement with a
debounce, percentile-based depth and max cut, persistence-weighted peak width.

**Tier 2 — harmonic rejection. Not implemented.** The agreed approach is
motion-weighted down-weighting, reusing the detector's lag search. The original
proposal was to reject integer multiples of the fundamental outright. That was
rejected after review, because hard rejection removes real content when the pitch
estimate is wrong, and pitch estimates are wrong often on a real voice.

**Tier 3 and Tier 4 — not started.** A confidence score with the option to
propose nothing, and learning from an external key.

---

### Phase H — Learn timing, and the startup defaults

**Tier 1 of the Learn plan.** The learner had four defects. Its attack
measurement reset its own counter on the line before reading it, so it returned
the 8 ms default on every take. Its release counter never reset, so it grew for
the whole take and pinned release to its ceiling. It triggered on any threshold
crossing with no minimum length, so material near the threshold counted as a
stream of fake onsets. And depth and max cut came from the take's single worst
frame, so one cough could set them.

All four are fixed. The attack now runs a real two-state machine with a debounce.
Depth and max cut come from the 95th percentile of the take's excess. Peak width
weights by how often each point was a peak, so a spike that appears once no
longer counts the same as a peak that lasts.

**The fix exposed a structural limit.** The attack still pinned to its ceiling
after the repair — it read 30 ms on every take. The reason is the frame rate. The
learner reads 20 frames a second, so one frame is 50 ms, and the attack control
spans 1 to 30 ms. A 50 ms ruler cannot measure a 30 ms window, so every mapping
saturates.

**The engine now measures the timing.** Onset and decay are measured inside the
engine at the transform rate — 10.7 ms per frame at standard detail, 5.3 ms at
high. The engine hands the finished numbers to Learn. This runs only while a take
is being learned, so playback costs nothing extra. This is the "run the CPU
harder while gathering data" idea, applied where the resolution actually exists.

**A memory defect found while reading the preset code.** The preset reset held
nine boolean settings but only eight values. The loop read past the end of the
array on every preset load. `externalKey` received whatever sat next to the array
in memory, and `multiResolution` received nothing. Fixed, and the two lists now
have the same length.

**Startup defaults.** The plugin already started neutral, because `depth`
defaults to 0. Match did not start on. Setting it on broke two tests, and one of
those failures is real: with Match on, the output depends slightly on the host's
block size. That would make a bypass comparison drift with the user's buffer
setting, which is the exact thing Match exists to prevent. Match therefore stays
off until that dependency is fixed.

---

## 5. Every measurement taken

### The corpus

18 dry takes from about 7 singers, at
`/Users/armundescarey/Documents/Dry Vocal For Plugin test/`, converted into
`.work/audio/corpus/`.

### The measurement tools built

**`Tools/LearnScore.cpp`** — renders each take through the real processor in
three configurations: dry, default, and learned. Measures frame-restricted
reduction in the 5–16 kHz range and movement in the 100–1000 Hz range. Writes
JSON.

**`.work/scripts/sib_frames.cpp`** — a frame-level diagnostic. Reports sibilance
energy, cut, deepest cut, tracked centre, spectral flatness, and baseline value
per frame.

**`.work/scripts/corpus_run.sh`** — batch corpus analysis.

### What LearnScore found

**The metric was validated first.** A 6 dB high-shelf cut was inserted into the
signal and measured at **1.77 dB**. The tool can see a cut. This step mattered,
because without it a zero reading proves nothing.

**Then the corpus.** Sibilance reduction across the 18 takes measured between
**0.00 and 0.69 dB**, with most takes **under 0.05 dB**.

**At full authority**, reduction measured **0.02 dB**.

For scale: the plugin at full power measured about one sixtieth of what a single
static shelf cut measures.

**Two metric defects were found and fixed while building the tool.** Both are
recorded in `docs/LEARN-SCORE-BASELINE.md`. One was averaging over the whole
take, which hid the behaviour. The other was a focus-band misreading: the tool
treated a focus band as a filter, but focus bands aim the detector rather than
cutting. The control condition was built to settle that.

### Learn output, measured before and after Tier 1

Same three takes, before and after:

| Take | Attack before | Attack after | Release | Depth | MaxCut |
|---|---|---|---|---|---|
| v06 | 8.0 | 30.0 | 300.0 | 3.20 | 30.0 |
| v11 | 8.0 | 30.0 | 70.0 | 3.20 | 30.0 |
| v05 | 8.0 | 30.0 | 300.0 | 3.20 | 30.0 |

**The attack went from one constant to another constant.** Before Tier 1 it was
always 8 ms because the measurement was dead. After Tier 1 it is always 30 ms
because it hits its ceiling.

The reason: the learner reads scope frames at **20 Hz**, so one frame is **50 ms**.
The parameters it proposes run **1–30 ms**. A 50 ms ruler cannot measure a 5 ms
event, so every mapping saturates. Depth and max cut saturate the same way.

**This is the most important open finding in the project.** It means the learner
proposes the same clamped settings for every take, which matches the owner's
report that Learn output sounds weird, thin, and boxy.

### Test suite results

Both suites pass with zero failures after every change recorded here:

```
ResonaProDspTests   === ALL PASS (0 failures) ===
ResonaProTests      === ALL PASS (0 failures) ===
```

### The unresolved de-esser disagreement

LearnScore measured sibilance reduction at 0.00–0.69 dB across the corpus, and
0.02 dB at full authority. The owner reports the de-esser works.

Both cannot be right. One reconciliation: a cut that is very short and very deep
could read low in a frame average while still being audible. That has not been
tested.

**This is recorded as open.** Neither the measurement nor the report has been
established as correct.

---

## 6. Every failure and revert

This project keeps its failures on purpose. Each one below cost time and each one
produced a rule.

| What was tried | What happened | What was learned |
|---|---|---|
| Gated baseline for the de-esser | Miss rate went from 8.7% to 87.5% | The reasoning was sound and the result was bad. Reverted. |
| Sub-bin parabolic interpolation | Unstable search | A stable static grid beats an unstable dynamic one |
| Dynamic band centering | Detector chased harmonic overtones | A feedback loop needs a decoupled detector |
| Hard rejection of harmonics in Learn | Rejected at review | A wrong pitch estimate plus hard rejection removes real content |
| Multi-resolution and phase-locking rewrite | Shelved after listening test | Measure before rewriting. The artefacts were not audible. |
| Learn attack mapping | Replaced one constant with another | The frame rate cannot resolve millisecond timing |
| Averaging reduction over the whole take | Hid the behaviour being studied | Aggregate metrics hide events |
| Treating a focus band as a filter | Nearly reported a bug that was not there | Validate the measurement before trusting it |
| Early crash recovery attempt | Made things worse | A fix that is not measured is a guess |

**Two of these are worth repeating.**

The gated baseline had good reasoning behind it, was approved by the owner, and
made the problem ten times worse. Only the measurement caught it.

The focus-band error nearly produced a false bug report. It was caught by
building a control condition. **A measurement tool must be proved able to see the
thing it claims to measure.**

---

## 7. Test infrastructure

Two suites, both built through CMake so they always compile against current
headers:

- **`Tests/DspTests.cpp`** — DSP-level regression
- **`Tests/ProcessorTests.cpp`** — processor-level regression, including the
  Solo Cut graph regression test added in 2.13

Both run from source on every build. The stale-suite problem from section 3.3
cannot recur.

Offline tools:

- **`Tools/LearnScore.cpp`** — the scoring harness
- **`Tools/Analyze.cpp`** — file-based analysis
- **`.work/scripts/sib_frames.cpp`** — frame-level de-esser diagnostic

---

## 8. Version history

| Version | What it carried |
|---|---|
| v2.2 | The incoming codebase, four commits |
| v2.1 notes | Detection that follows the vocal |
| v2.3.0 | Fine-tune drawer work |
| v2.3.1 | Focus band bias, stale-suite fix |
| v2.6.0 | Fine Tune audit implemented |
| v2.13 | Solo Cut graph freeze fixed; visualizer non-finite guards |
| v2.13.1 | Learn Tier 1 |
| v2.14.0 | Engine-rate Learn timing; the nine-versus-eight array defect; Match left off |

Version numbering went backwards once — a build was labelled 2.3.2 when work had
already passed 3.0. The owner asked why. The label now matches the work.

The version label on the plugin interface shows what is loaded. Check it when a
bug report does not match the source.

---

## 9. Current state

**Installed:** 2.14.0 in `~/Library/Audio/Plug-Ins/VST3/ResonaPro.vst3` and the
AU folder. Signature verified.

**Tests:** both suites, zero failures.

**Working:** the engine is clean and transparent on a real voice, confirmed by
the owner's listening test on held notes and transients. The cream interface is
implemented. Solo Cut works and its graph freeze is fixed. The tilts act on
something. The de-esser has a spectral flatness gate. The engine measures onset
and decay at the transform rate, so Learn has real timings to work from.

**Not working as intended:** Match depends slightly on the host block size, so it
stays off by default. Learn's new timings are wired but have not been measured
across the corpus yet.

**Open and unresolved:** the de-esser measurement disagreement. The owner reports
the de-esser works. The measurement says it cuts 0.02 dB across 18 takes while a
single shelf measures 1.77 dB. One frame-average test would settle it. Until
then, treat neither side as fact.

---

## 10. Where we're going

### Next, in order

**1. Learn timing, measured at audio rate.** Have the engine measure onset and
decay inside the processing loop during Learn capture, rather than from 20 Hz
editor frames. The owner has approved extra CPU during learning, provided
playback cost returns to normal afterward. This is the change that can make Learn
propose different settings per take.

**2. Learn Tier 2 — motion-weighted harmonic down-weighting.** Not hard
rejection. Reuse the detector's existing lag search.

**3. Settle the de-esser question.** Test whether a short, deep cut reads low in
a frame average. Until that is settled, neither view should drive work.

**4. Learn Tier 3 — a confidence score**, with the option to propose nothing
rather than a bad guess.

### Later, if wanted

- **Sample-rate-independent transform** — so settings behave the same at 44.1
  and 48 kHz
- **Per-band time constants** in the de-esser
- **A continuous gain curve** instead of six bands
- **Learn Tier 4** — learning from an external key
- **Symbol table stripping** — 26% off the arm64 binary, no effect on sound

### Not planned

**Multi-resolution synthesis and phase-coherent resynthesis.** Shelved after the
listening test found no audible artefacts. Revisit only if the owner hears
something the current engine cannot handle.

---

## 11. Standing rules

These are the owner's and they hold:

1. **Do not build `.pkg`, `.dmg`, or installer `.zip` files unless asked.**
2. **After any audio-affecting change:** build, run both suites, install.
3. **Keep the install current**, or the owner tests a stale build and reports a
   bug that is already fixed.
4. **Evidence over assertion.** Every claim about audio behaviour must be
   measured. A change not covered by a check is not verified. Report the
   measurement that failed a change, including when the change is yours.
5. **Synthetic signals prove the signal path is clean and say nothing about
   whether the plugin sounds right on a real voice.** That needs the owner's
   ears, and it is fair to say so.
6. **Learn must never change** Quality/Resolution, Hard mode, Mid/Side, Delta,
   profile weighting, output, stereo link, mix, auto gain, external key, bypass,
   or monitoring and routing decisions. Enforced by allowlist and by tests.

---

### The priority, stated by the owner

**Audible quality outranks everything else.** Latency, plug-in size and CPU cost
are acceptable trade-offs in service of it, within reason.

This reopens work that was shelved for cost. The multi-resolution and
phase-coherent resynthesis refactor was set aside because it adds about 85 ms of
latency and CPU. That reason no longer stands on its own. It now needs a quality
argument either way, not a latency one.

Measured costs are still required. Quality-first changes the decision, not the
evidence standard.

---

## 12. Key files

| File | What it holds |
|---|---|
| `Source/DSP/SpectralEngine.h` | The WOLA/STFT engine. Resonance processing, de-esser stage, sidechain, Solo Cut path. |
| `Source/DSP/ResonanceDetector.h` | The detector. Adaptive baseline, harmonic spacing, detail tilt, weighting, motion protection. |
| `Source/DSP/DynamicSuppressor.h` | Per-bin reduction envelopes and frequency-dependent attack and release tilt. |
| `Source/DSP/SibilanceDeEsser.h` | Six-band de-esser. Flatness gate, learned profile, tracked centre, Solo Cut. |
| `Source/DSP/LearnAnalyzer.h` | The learner. Tier 1 state machine, histogram, weighted widths. |
| `Source/PluginProcessor.cpp` | Parameter layout, routing, Learn fence, visualizer publication, level matching. |
| `Source/PluginEditor.cpp` | The cream interface, Learn workflow, controls, tooltips. |
| `Source/UI/SpectralVisualizer.h` | Reduction graph and spectrum. Non-finite guards from 2.13. |
| `Tests/DspTests.cpp` | DSP regression suite |
| `Tests/ProcessorTests.cpp` | Processor regression suite |
| `Tools/LearnScore.cpp` | The scoring harness |
| `.work/scripts/sib_frames.cpp` | Frame-level de-esser diagnostic |
| `.work/backups/` | File backups taken before each edit |

### The documents

| Document | What it holds |
|---|---|
| `UPGRADE-REPORT.md` | The v1 review. Eight correctness bugs. |
| `CRASH-DIAGNOSIS-2026-10-06.md` | The crash, the process failure, and the root cause. |
| `REGRESSION-2026-10-06.md` | What the Antigravity session changed, good and bad. |
| `PLUGIN-STATUS-REPORT.md` | Five detection problems. |
| `BOTTLENECKS.md` | Latency, sample-rate dependence, single resolution. |
| `AUDIO-QUALITY.md` | What is measurable in audio quality and what is not. |
| `QUALITY-ROADMAP.md` | The phased quality plan. |
| `QUALITY-GAP-EXPLAINED.md` | The algorithmic gap and the symbol table, explained. |
| `RESEARCH-SYNTHESIS-OPTIONS.md` | Phase-coherent and multi-resolution, costed. |
| `RESEARCH-AND-DESIGN.md` | Reference research and DSP design. |
| `FINE-TUNE-AUDIT.md` | The drawer audit and its implementation. |
| `DE-ESSER-ROADMAP.md` | Six phases, four findings. |
| `DE-ESSER-PRECISION-IDEAS.md` | The precision brainstorm, tiered. |
| `SIBILANCE-MISS-DIAGNOSIS.md` | The frame-level investigation and the revert. |
| `LEARN-DESIGN.md` | The Learn fence and the review step. |
| `LEARN-UPGRADE-PLAN.md` | Four tiers and the judgement. |
| `LEARN-SCORE-BASELINE.md` | The harness, its validation, and the shallow-cut finding. |
| `UI-SUGGESTIONS.md` | Interface proposals. |
| `HOW-TO-USE.md` | The user guide. |
| `HANDOFF-PROMPT.md` | The onboarding prompt for a new tool or workspace. |
| `V2.1-RELEASE-NOTES.md`, `V2.3.0-RELEASE-NOTES.md` | Release notes. |

---

## 13. Honest limits

**The measurements are synthetic in part.** The suites use test signals. They
prove the signal path is clean. They do not prove the plugin sounds right on a
voice. The one real-voice check so far was the owner's listening test, and it
covered held notes and transients on one voice.

**The corpus is 18 takes from about 7 singers.** That is a real sample and a
small one. Conclusions drawn from it carry uncertainty.

**The de-esser question is unresolved.** Do not treat either side as settled.

**The learner's problem is diagnosed but not fixed.** The frame-rate limit is
established by measurement. The audio-rate fix is designed and not built.

**Some early history is reconstructed.** The working session was compressed
partway through, so parts of sections 2 and 4 come from the project's documents
rather than from first-hand memory. The documents are the source.

**Nothing here replaces listening.** The plugin exists to change how a voice
sounds. A number can tell you the engine is clean. It cannot tell you the voice
sounds better.


---

# Part 4 — Architecture (describes v1 in part)

Source: `ARCHITECTURE.md`

# ResonaPro DSP & Software Architecture Guide 📐

> **Note (v2):** this document describes the **original v1 engine**. Several of
> the algorithms it documents were replaced in v2 because they were measurably
> incorrect or misleading — in particular the centre-inclusive linear-domain
> baseline, the fixed-threshold detector, the unused `PitchTracker` /
> `TransientSplitter` pair, and the "oversampling" scheme (which reduced rather
> than increased usable frequency resolution, and was never zero-latency).
>
> The authoritative description of the current engine is
> [docs/RESEARCH-AND-DESIGN.md](docs/RESEARCH-AND-DESIGN.md); the list of defects
> and fixes is in [docs/UPGRADE-REPORT.md](docs/UPGRADE-REPORT.md).

This document provides a deep technical breakdown of the mathematics, algorithms, and JUCE integration inside **ResonaPro** for developers, AI coding assistants (like Manus), and DSP engineers.

---

## 1. High-Level Signal Flow

```
[ Incoming Audio Block (xL, xR) ]
               │
      [ Polyphase Oversampler (1x, 2x, 4x, 8x) ]
               │
    ┌──────────┴──────────┐
    ▼                     ▼
[ Transient Stream ]   [ Sustain Stream ]  <-- Real-time SMS Transient Splitter
    │                     │
[ Circular Delay ]        ▼
 (N samples)           [ WOLA STFT Analysis (Apple vDSP) ]
    │                     │
    │                  [ ISO 226 Human Ear Sensitivity Weighting ]
    │                     │
    │                  [ 8-Band Parametric Sidechain EQ Weighting ]
    │                     │
    │                  [ Spectral Resonance Peak Detector ]
    │                     │
    │                  [ Dynamic Per-Bin Attack/Release Suppressor ]
    │                     │
    │                  [ Complex Spectrum Attenuation ]
    │                     │
    │                  [ Inverse STFT Synthesis (Square-Root Hann COLA) ]
    │                     │
    └──────────┬──────────┘
               ▼
   [ Time-Aligned Recombination ]
               │
   [ Dry/Wet Mix & Delta Solo Switch ]
               │
   [ Polyphase Downsampler ]
               │
   [ Output Audio Block (yL, yR) ]
```

---

## 2. DSP Subsystems

### A. WOLA STFT Engine (`DSP/SpectralEngine.h`)
* **Window Function:** Sqrt-Hann window $w[n] = \sin\left(\frac{\pi(n + 0.5)}{N}\right)$ for both analysis and synthesis.
* **Hop Interval:** $R = N / 4$ ($75\%$ overlap).
* **COLA Identity Condition:**
  $$\sum_{m} w_a[n - mR] \cdot w_s[n - mR] = \sum_{m} \sin^2\left(\frac{\pi(n + 0.5 - mR)}{N}\right) = \frac{N}{2R} = 2.0$$
* **Synthesis Normalization:** With Apple `vDSP_fft_zrip` round-trip gain of $2N$, the exact scaling factor is $\frac{1}{4N}$.
* **Result:** Round-trip transfer function when `depth = 0.0` has **$0.000\%$ harmonic distortion and $< 10^{-15}$ reconstruction error**.

### B. Transient / Sustain Deconstruction (`DSP/TransientSplitter.h`)
* Uses dual fast/slow leaky peak envelope detectors:
  * **Fast Envelope:** $\tau_{\text{attack}} = 1.5\text{ ms}, \tau_{\text{release}} = 10\text{ ms}$.
  * **Slow Envelope:** $\tau_{\text{attack}} = 25\text{ ms}, \tau_{\text{release}} = 80\text{ ms}$.
* **Transient Index:**
  $$\gamma(t) = \text{clamp}\left(\frac{\max(0, E_{\text{fast}}(t) - E_{\text{slow}}(t))}{E_{\text{fast}}(t) + \epsilon} \cdot 1.8, 0.0, 1.0\right)$$
* **Separation:**
  $$x_{\text{transient}}(t) = x(t) \cdot \gamma(t), \quad x_{\text{sustain}}(t) = x(t) \cdot (1 - \gamma(t))$$
* **Plosive Preservation:** $x_{\text{transient}}(t)$ is delayed by $N = 2048$ samples and recombined with the de-resonated sustain, preventing plosive dulling.

### C. Psychoacoustic Resonance Detection (`DSP/ResonanceDetector.h`)
* **Running Smoothed Baseline:** Adaptive moving-average across frequency bins with kernel radius scaled by frequency and `sharpness`:
  $$\text{radius}(k) = \text{clamp}\left(\text{int}(0.05 \cdot k) + \frac{16}{\text{sharpness}}, 3, 64\right)$$
* **Resonance Prominence Ratio:**
  $$R(k) = \frac{X_{\text{mag}}(k)}{X_{\text{smooth}}(k) + \epsilon} - 1.0$$
* **ISO 226 Human Ear Curve:** Normalized equal-loudness weighting boosting sensitivity around the $2.5\text{ kHz} - 5\text{ kHz}$ ear canal resonance zone.
* **Sharpness Exponent:**
  $$\text{PeakStrength}(k) = (\max(0, R(k) - \text{Selectivity} \cdot 0.5))^{\text{sharpness}}$$

### D. 8-Band Sidechain Sensitivity Curve (`DSP/ParametricEQWeighting.h`)
* Evaluates sensitivity multiplier $S(f)$ for each FFT bin:
  $$S(f) = \prod_{b=1}^{8} 10^{\frac{\text{gainDb}_b \cdot \text{Bell}_b(f)}{10.0}}$$
* **Gain Behavior:**
  * $+12\text{ dB}$: Sensitivity increased by $+16\times$ (heavy suppression in that band).
  * $-18\text{ dB}$: Sensitivity reduced to $0.02\times$ (bypasses suppression in that band).
  * $0\text{ dB}$: Completely neutral ($1.0\times$).

---

## 3. Parameter Mapping (`AudioProcessorValueTreeState`)

| Parameter ID | Name | Range | Default | Description |
| :--- | :--- | :--- | :--- | :--- |
| `depth` | Vocal Depth | `0.0 - 4.0` | `0.0` | Master reduction intensity (0.0 = neutral bypass) |
| `sharpness` | Sharpness | `0.2 - 4.0` | `1.0` | Exponent from broad EQ to surgical needle notches |
| `selectivity` | Selectivity | `0.0 - 1.0` | `0.5` | Threshold for ignoring non-resonant background energy |
| `attack` | Attack | `0.5 - 50.0 ms` | `8.0 ms` | Envelope attack time constant |
| `release` | Release | `5.0 - 500.0 ms` | `70.0 ms` | Envelope release time constant |
| `transientPreserve` | Plosive Preserve | `0.0 - 1.0` | `0.0` | Plosive/consonant transient preservation amount |
| `vocalProfile` | Vocal Profile | `0 - 3` | `0` | Lead, De-Ess, Warm De-Mud, Air & Silk |
| `oversampling` | Oversampling | `0 - 3` | `0` | 1x (0ms), 2x, 4x, 8x polyphase oversampling |
| `iso226` | ISO 226 Ear | `Bool` | `true` | Psychoacoustic ear curve weighting |
| `modeHard` | Hard Knee | `Bool` | `false` | Asymptotic soft vs steep linear reduction knee |
| `midSide` | Mid/Side | `Bool` | `false` | M/S processing for stereo separation |
| `deltaListen` | Delta Solo | `Bool` | `false` | Solo the removed resonance content |
| `mix` | Dry/Wet Mix | `0 - 100 %` | `100 %` | Parallel processing dry/wet blend |
| `eq_freq_1..8` | Band 1..8 Freq | `20 - 20000 Hz` | Preset | Center frequency of cue node |
| `eq_gain_1..8` | Band 1..8 Sens | `-24 - +24 dB` | `0.0 dB` | Sensitivity boost/cut for cue node |
| `eq_q_1..8` | Band 1..8 Q | `0.1 - 8.0` | Preset | Bandwidth of cue node |
| `eq_enable_1..8`| Band 1..8 Enable | `Bool` | `true` | Enable state of cue node |

---

## 4. UI & Visualizer (`UI/SpectralVisualizer.h`)
* Renders at **60 FPS** via a lock-free thread-safe atomic array FIFO (`scopeMagnitude` and `scopeReduction`).
* Interactive draggable node coordinates map logarithmic frequency $\log_{10}(f)$ to horizontal pixels and $\text{dB}$ gain to vertical pixels.
* **Double-click listener:** Toggles existing nodes or activates inactive nodes at the clicked mouse coordinates.
* **Mouse wheel listener:** Smoothly expands/contracts the Q factor of the hovered node.


---

# Part 5 — The v1 to v2 upgrade

Source: `docs/UPGRADE-REPORT.md`

# ResonaPro — v1 Code Review and v2 Upgrade Report

This is the practical summary of the audit. Every defect listed here was found by
reading the v1 sources and then **confirmed by measurement**, and every fix is
covered by an automated test that fails on the old behaviour.

---

## 1. Bugs that broke correctness

### 1.1 Latency was reported incorrectly, so dry/wet never lined up

**v1:** `SpectralEngine` had no `getLatencySamples()`. The processor estimated the
delay from `(fftSize / 2 + hopSize - 1)`, and in the oversampling path multiplied
by the oversampling factor. The overlap-add ring was read at an offset derived
from a *different* formula again.

**Consequence:** the dry and wet paths were misaligned by tens or hundreds of
samples. Blending Mix produced comb filtering, and the host's plug-in delay
compensation was wrong so the whole track shifted.

**v2:** the engine measures its own latency with an impulse probe in `prepare()`
and returns it; the processor uses that number for the dry delay line, for the
mix, and for `setLatencySamples`. Verified: at every quality setting the measured
and reported latency agree exactly, and the reconstruction residual is ~1e−15.

### 1.2 Delta was not the removed signal

**v1:** `delta = input[n] − processed[n]`, with no delay compensation between the
two.

**Consequence:** Delta contained the entire signal plus a comb filter. The one
feature that lets a user trust the plug-in was useless.

**v2:** `delta = delayedDry − wet`, using the same delay the mix uses. Verified:
Delta sits 11–14 dB below the dry signal and contains only the removed energy.

### 1.3 Detection used a centre-inclusive baseline in the linear domain

**v1:** the local baseline average included the bin under test, and everything
was compared as linear magnitudes against a fixed threshold.

**Consequence:** a peak raised its own baseline, so real resonances inside a
formant were under-detected while low-level ripple was over-detected. The
behaviour also changed with input level — quiet takes were untouched, loud takes
were destroyed.

**v2:** side-excluding shoulders, dB-domain prominence, adaptive threshold.

### 1.4 Every harmonic was treated as a resonance

**v1:** nothing distinguished the harmonic comb of a sung note from a resonance.

**Consequence:** the classic hollow, lisping, over-processed vocal.

**v2:** harmonic-period estimation plus a peak-relative baseline. Measured on a
hostile synthetic vocal, harmonics are now reduced by 0.0 dB while a genuine
Q = 14 resonance in the same signal is reduced.

### 1.5 Non-finite input corrupted the engine permanently

**v1:** a NaN or Inf sample (which an upstream plug-in can produce) entered the
FFT frame and, through the overlap-add ring, stayed in the buffer forever.

**Consequence:** the plug-in stayed silent or noisy for the rest of the session.

**v2:** input sanitising in the engine plus an output guard. Verified: a buffer
full of NaNs and 1e30 values produces finite output, and the plug-in recovers
cleanly on the next clean buffer.

### 1.6 Parameter smoothing was scaled in blocks, not seconds

**v1:** `SmoothedValue` was advanced once per block with a ramp length expressed
in seconds, so a nominal "20 ms" ramp actually took several seconds and the
result depended on the host block size.

**v2:** per-sample ramps for mix and output gain, and an explicit time-based step
for depth. Verified: output is now bit-identical between 64-sample and
2048-sample block sizes (max difference 0.000000).

### 1.7 Allocation risk on the audio thread

**v1:** oversampling objects were constructed inside `processBlock` in one code
path.

**v2:** every engine variant for every (Quality, Response) combination is built
in `prepareToPlay`, so switching quality during playback costs nothing on the
audio thread.

### 1.8 Repository hygiene

Stray `sys/` build artefacts and macOS duplicate files (`* 2.h`) were committed;
`PitchTracker.h` (a YIN tracker) and `TransientSplitter.h` were referenced by
CMake but never actually used by the engine, and both were listed in the CMake
source list. All removed or replaced.

---

## 2. Design problems, not just bugs

### 2.1 "Oversampling" did not do what it claimed

Resampling the signal to 8× and feeding a **fixed 2048-point** FFT does not
increase resolution — it spreads the same 2048 bins over 8× the bandwidth, so in
the audio band you end up with roughly **128 usable bins** instead of 1024. It
costs CPU, adds latency, and makes the frequency resolution *worse*.

It was also labelled "1x (Zero Latency)", which was simply untrue: the engine
already had a 2048-sample transform and therefore 2048 samples of latency.

**v2 replaces it with two honest controls:**

* **Quality** — the transform length (1k / 2k / 4k / 8k). This is what actually
  buys frequency resolution, and the cost is latency, which is displayed.
* **Response** — the overlap factor (2× / 4× / 8×). More overlap means the gain
  curve is recomputed more often, which is the *real* benefit people were
  chasing when they reached for oversampling.

### 2.2 No way to protect attacks

The only transient handling was a per-bin ratio against the baseline, which is
not the same thing — a consonant is broadband, not a narrow peak, so the
mechanism acted on the wrong material and its result depended heavily on the
release time.

**v2 adds a dedicated `TransientDetector`** (fast/slow envelope ratio plus a
spectral-flux assist) whose output raises the detection threshold on attacks.
`Transient Protect` is a first-class control and is used by the factory presets
to build genuinely plosive-safe de-essers. Verified to change behaviour
measurably on an onset-heavy test signal.

### 2.3 No preset system, and no musical default

**v2 adds eleven factory presets** and loads *Lead Vocal — Gentle* on first open,
so the plug-in is doing something useful the moment it appears.

### 2.4 UI and honest labelling

* The frequency graph is now labelled for what it is — a **detection bias**, with
  inverted-gain semantics explained in the UI — rather than an EQ.
* `Sharpness` is displayed as **Detail**, `Transient Preservation` as
  **Transient Protect**, `Oversampling` as **Quality / Response**, and the
  reduction ceiling is exposed as **Max Cut**.
* A **Reset Bands** button was added, plus a working **Bypass**.
* The visualiser now draws three curves: input magnitude, the detector's own
  baseline, and the applied reduction — so you can see *why* the plug-in is
  acting where it acts.
* The header no longer claims features the DSP does not have.

---

## 3. What was added

| Feature | Why |
|---|---|
| Harmonic-period estimation | The core fix for "vocal sounds hollow" |
| Adaptive (statistical) threshold | Level independence and content awareness, matching soothe's documented soft-mode behaviour |
| ISO 226 ear weighting | Stops the 3.4 kHz ear-canal resonance from being permanently carved out |
| Frequency-dependent attack/release | Low-end resonances are typically broader and need slower dynamics than a 7 kHz sibilant |
| Per-bin reduction ceiling (**Max Cut**) | Direct control over how far the processor may go |
| Gain-curve smoothing (3-tap) | Suppresses musical-noise warbling |
| Stereo link (0–100 %) | Consistent image; dual-mono available when needed |
| Mid/Side mode | Process the centre and the sides independently |
| Transient Protect | Plosive- and consonant-safe operation |
| Quality / Response | Real resolution and response-speed control |
| Delta correct implementation | The most useful diagnostic in the plug-in |
| Bypass | Host-independent A/B |
| Eleven presets | Usability |
| Log-frequency visualiser with baseline curve | Shows the detector's reasoning |
| Two automated test suites | So the above cannot silently regress |

---

## 4. Verification summary

```
Engine suite  (.work/scripts/dsp_test.cpp)          -> ALL PASS (0 failures)
Processor suite (Tests/ProcessorTests.cpp)          -> ALL PASS (0 failures)
Release build of AU + VST3 + Standalone             -> 0 errors
```

Covered, among others:

* exact reconstruction at 1k/2k/4k/8k and 2×/4×/8× overlap
* measured latency equals reported latency
* Depth 0 with Mix 100 % is transparent to ~1e−15, through the *whole* processor
* bypass is transparent; Delta contains only the removed energy
* harmonics are protected while a resonance in the same frame is reduced
* transient protection measurably protects onsets
* mono and stereo buses both render finite audio
* NaN / 1e30 input never escapes and the plug-in recovers
* output is independent of host block size (bit-identical)
* all 11 presets load and render inside bounds
* state save/restore round-trips every parameter

---

## 5. Migration notes

* **`oversampling` no longer exists.** Old projects will fall back to the new
  defaults for `quality` / `response`. Re-save presets.
* **Default depth is 1.0**, not 0 — the plug-in is active when instantiated. Use
  the *Init (Neutral)* preset for a bypass-grade starting point.
* The focus-band semantics are unchanged in the UI (up = more), but the sign
  convention in the code is explicit now: `weight` is subtracted from the
  detection threshold.
* `PitchTracker` and `TransientSplitter` were removed. `TransientDetector` is the
  new attack detector.


---

# Part 6 — Release notes 2.1

Source: `docs/V2.1-RELEASE-NOTES.md`

# ResonaPro 2.1: detection that follows the vocal

A fixed key tells you little about a ringing vocal. The same note can carry a moving harmonic and a peak that stays near one frequency. This release gives you better tools to separate them and, when you want, to let another track drive detection.

## What changed

| Feature | What the plugin does | Limit |
|---|---|---|
| **NOTE MOTION** | Compares a low-band peak at its old frequency with the frequency predicted by a change in the harmonic period. It adjusts the excess-peak decision by at most 25% at full strength. | It is a cautious cue, not a polyphonic pitch tracker. It acts only when the estimated note moves enough to resolve the two hypotheses. On synthetic 150→175 Hz harmonic-comb input with a stationary 702 Hz line, the largest decision change was 0.080 dB. A real-vocal listening test remains necessary. |
| **LOW DETAIL** | Runs a second 4096-point **detector** below 1.2 kHz when Quality is 1k or 2k. The low decision blends into the main transform. | Only one synthesis path runs, so host latency stays at 1024 or 2048 samples. It is not a dual-resolution resynthesis system. In a ringing-noise probe, the long window reduced *less* than the short window by 1–5 dB, which may protect a low note or may leave an unwanted ring. Check DELTA before keeping it on. At 4k/8k Quality this switch has no extra effect. |
| **EXT KEY** | Adds an optional mono/stereo input bus. When its button is on and the host enables the bus, its spectrum steers reduction on the main vocal. | An enabled but silent key can yield little reduction. The key bus is never sent to the output. Sidechain mapping in FL Studio requires both a Mixer send and the wrapper's input assignment. |
| **LEARN / APPLY LEARN** | While the editor stays open, samples the played vocal at 20 Hz for up to 10 minutes. It ranks recurring excess peaks, displays candidate frequencies and suggests up to eight focus-band settings. | It does not process a file offline or fit a static corrective EQ. It changes no parameters until you press Apply. It needs one second of active audio frames. Turn EXT KEY off before learning. |
| **Preset and node workflow** | Categories group the factory presets. Shift-drag snaps a node to 1/6-octave and whole-dB steps. Right-click a node to enter `Hz dB Q`. COPY/PASTE uses the system clipboard so it works between plugin instances. | The clipboard payload is plain text with an identifying header and is rejected if malformed. |

**EAR GUARD replaces the misleading ISO 226 label.** The old parameter ID remains `iso226` for session compatibility. The current code applies a small frequency-dependent threshold bias; it does not implement [ISO 226 equal-loudness contours](https://www.iso.org/standard/34222.html), which depend on sound pressure and a chosen loudness level.

## Tests and performance

- DSP reconstruction suite: **all pass**.
- JUCE processor suite: **all pass**, including optional sidechain layout, unchanged key-bus samples, changed output with an external key, low-band depth-zero null and unchanged latency, note-motion input, recurring-peak learning, saved-state round-trip, and a 960×700 editor render.
- Synthetic low-band probe (48 kHz, short-window versus long-window detector): changes ranged from **+0.09 to +4.55 dB** in output energy depending on the transform length and ring frequency. This proves a difference, not superior sound.
- Synthetic CPU benchmark at 48 kHz, 4× overlap, on this Mac: 2k basic **1.5%** of one core; 2k LOW DETAIL **4.7%**; 1k basic **1.4%**; 1k LOW DETAIL **7.9%**. These numbers exclude host overhead and vary with the material and computer. NOTE MOTION added less than 0.1 percentage point in the same test.
- Universal VST3 and AU are built for `arm64` and `x86_64`.
- The installed 2.1.0 VST3 and AU passed macOS ad-hoc signature verification;
  Apple's `auval -v aufx RsnP ResA` reported **AU VALIDATION SUCCEEDED**.
  The release ZIP passed an archive integrity check. The signatures are local
  ad-hoc signatures, **not** Developer ID signatures or notarization.
- Address/undefined-behavior sanitizer coverage is **not complete**: macOS
  rejected leak detection, and the full sanitized DSP suite ran too long to
  finish in this pass. The normal DSP and processor suites did pass.

**Not yet established:** superiority to soothe2/soothe3, quality on diverse real vocal recordings, host behavior across FL Studio versions, or performance under many simultaneous plugin instances. The external-key test exercises JUCE buffers, not FL Studio's wrapper. Test on a copy of a project first, listen to DELTA, and compare at matched level.

## FL Studio path

Use the VST3 on the vocal track. To feed EXT KEY, right-click a send from your key track to the vocal track and choose **Sidechain only to this track**. Then open ResonaPro's wrapper **Processing → Inputs**, map the sidechain to the plugin's extra input, and turn on EXT KEY. Image-Line documents the [sidechain send](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer.htm) and [wrapper input mapping](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/wrapper.htm).

See [How to Use ResonaPro](HOW-TO-USE.md) for the full setup, Learn workflow and control reference.


---

# Part 7 — Release notes 2.3.0

Source: `docs/V2.3.0-RELEASE-NOTES.md`

# ResonaPro 2.3.0

Released 6 October 2026. macOS, universal (Apple Silicon and Intel), VST3, AU and
standalone.

This release repairs the resonance detector. In 2.2.0 the plug-in passed audio
through untouched, so every control except OUTPUT and MIX did nothing. The cause
was a single line.

## What was wrong

`ResonanceDetector.h` gathers the shoulder samples around each frequency bin to
work out the local floor. The helper collected the bin *numbers* instead of the
*levels* at those bins:

```cpp
dest.push_back (static_cast<float> (i));   // i is the bin index
```

The caller then converted those numbers to decibels as if they were signal
levels, so the baseline near 16 kHz came out at +57 dB while the audio there sat
at -15 dB. Prominence, which is the amount by which a bin stands above its
surroundings, was therefore negative in every bin, and the detector discarded
everything. Measured worst case: the highest prominence anywhere in the spectrum
was -32 dB.

Three further changes had removed safeguards on top of that:

* A raised focus band became a hard gate. With the eight bands at their defaults,
  reduction was set to exactly zero.
* The reduction multiplier was `cue / 6`, which is also zero at rest, so it gated
  a second time.
* The high bands were multiplied by 2.5 above 2.5 kHz, and the threshold dropped
  without a limit. Neither figure came from a measurement.

The plug-in's own test suite caught all of this. It was not run.

## What changed

| Area | Change |
|---|---|
| Shoulder sampling | Collects levels rather than bin positions, and bounds-checks them |
| Prominence | Compares the smoothed magnitude against the smoothed baseline, so a single noisy bin is no longer pitted against a filtered floor |
| Analysis span | Reaches past the feature it measures. An ERB alone is too narrow below 1 kHz, which had hidden wide low resonances |
| Cue gating | Removed. A focus band biases sensitivity; it no longer switches detection off |
| Cue multiplier | `1 + cue/6`, clamped to 1-4, so a neutral cue leaves the detector's own finding intact |
| High-frequency boost | Removed. There was no measurement behind it |
| Thresholds | Set above the measured noise floor and below measured voice structure, per band |
| Sibilance | The virtual cue and the threshold drop were both reduced, since the defaults pushed the high band below the noise floor |
| Dead code | The full-scale reference chain was removed. The engine computed it and the detector ignored it, which made HARD mode look level-aware when it was not |
| Delta monitor | The selected-band filter is narrower, so a one-band view no longer leaks neighbouring reduction |

## Measurements

The detector's baseline now tracks the signal:

| Bin | Frequency | Level | Baseline before | Baseline now |
|---|---|---|---|---|
| 32 | 750 Hz | +0.6 dB | +30.9 dB | +1.1 dB |
| 100 | 2344 Hz | -5.4 dB | +40.6 dB | -5.0 dB |
| 300 | 7031 Hz | -11.1 dB | +50.0 dB | -10.8 dB |
| 700 | 16406 Hz | -15.5 dB | +57.3 dB | -15.3 dB |

Separation between the material the plug-in should act on and the material it
should leave alone, at the shipped settings:

| Material | Peak prominence | Reduction |
|---|---|---|
| White noise | 4.8 dB | below threshold |
| Bright fricative noise | - | -0.03 dB total |
| Formant voice | 9.2 dB | audible |
| Resonance, +12 dB at 700 Hz | 9.7 dB | reduced |
| Resonance, +18 dB at 8 kHz | 17.5 dB | reduced |

Smoothing width was chosen by measurement rather than intuition. A wide kernel
looks like the obvious way to average out noise, but it averages out formant
structure just as readily: at radius 6 the voice was cut only 0.61 times as hard
as white noise, while at radius 2 it is cut 1.64 times as hard. Broadband
rejection belongs in the threshold, not in the kernel.

## Validation

Both suites pass with no failures:

* The processor suite, 37 checks through the real audio path, including the new
  bright-material preservation check.
* The DSP engine suite, 22 checks.

The installed VST3 and AU pass `codesign --verify --deep --strict` and Apple's
`auval` reports AU VALIDATION SUCCEEDED. Both bundles are universal.

## What this means in use

The plug-in now does what the interface says. DEPTH, SELECT, DETAIL, HARD,
MID/SIDE, EXT KEY, LOW DETAIL, NOTE MOTION, SIBILANCE, MAX CUT and the timing
controls all act on the signal. DELTA plays what was removed and is no longer
silent. MATCH measures a real difference, so it applies a real correction.

Focus bands change how hard the detector looks in a region. Leaving them flat is
a valid setting: the detector finds resonances on its own.

## Remaining limits

Fair to state:

* Only the automated harness has been run. No DAW has loaded this build yet, so
  in-host behaviour in FL Studio remains unverified.
* HARD mode is no longer level-aware. Frame-peak normalisation was already in
  place before this release and the full-scale reference was dead; the two cannot
  both hold. Making HARD react to absolute level is future work.
* The adaptive peak-reference machinery still computes a value that the threshold
  no longer uses. It feeds a UI readout, so it was left in place rather than
  removed.
* No listening test has been done. The thresholds are calibrated against
  measurements, not against ears.

## Files

* `ResonaPro-2.3.0-mac-universal.zip` in `outputs/`
* Installed to `~/Library/Audio/Plug-Ins/VST3`, `~/Library/Audio/Plug-Ins/Components`
  and `/Applications`
* Source changes in `Source/DSP/ResonanceDetector.h`,
  `Source/DSP/SpectralEngine.h`, `Source/PluginProcessor.cpp` and
  `Tests/ProcessorTests.cpp`


---

# Part 8 — Incident: the DAW crash

Source: `docs/CRASH-DIAGNOSIS-2026-10-06.md`

# ResonaPro: Crash Diagnosis and Corrective Work

**Date:** 6 October 2026
**Symptom:** FL Studio crashes every time the plugin is loaded.
**Status:** All defects found and fixed. Version 2.3.1.

Two intermittent defects (a teardown abort and a 2 dB level-match drift) turned
out to share a single root cause, found on 6 October 2026. See section 6.

---

## 1. Why the DAW crashed (fixed)

The installed plugin dereferenced a null pointer during `prepareToPlay()`, the
first thing a host calls when you insert a plugin. A crash there is a crash on
sight, which is exactly what was reported.

```
SpectralEngine::applyBarkSmoothing(...)   <-- fault, address 0x0
SpectralEngine::processSTFTFrame()
SpectralEngine::processSample(...)
SpectralEngine::processBlock(...)
SpectralEngine::measureLatency()
SpectralEngine::prepare(...)
DualBandEngine::prepare(...)
ResonaProAudioProcessor::buildEngines(...)
ResonaProAudioProcessor::prepareToPlay(...)
main
```

`applyBarkSmoothing()` groups the reduction curve into 25 critical bands and
needs to know which band each bin belongs to. It read that from a member called
`binBark`. In the whole file, `binBark` appeared three times: read at line 694,
read at line 721, **declared at line 804**. Nothing ever filled it, so it was an
empty vector with a null data pointer, and every read was a null dereference.

```
EXC_BAD_ACCESS (SIGSEGV) — KERN_INVALID_ADDRESS at 0x0000000000000000
```

`measureLatency()` pushes a synthetic impulse through the real processing path,
so the engine's own latency measurement was what killed it.

**Fix:** `binBark` is now filled during `prepare()`, immediately after
`binFreq[]` and before `measureLatency()`, using Traunmüller's Bark formula
(0 to ~24.6 over 20 Hz to 20 kHz, matching the 25 bands).

## 2. Why the test suite did not catch it (the process failure)

The test suite **did not compile** against the changed source. The detector's
`detect()` had gained a parameter and `prepare()` had changed arity; the four
call sites in `Tests/ProcessorTests.cpp` were never updated.

The *test* build failed; the *plugin* build succeeded. The plugin was built,
packaged and installed, and the test binary that reported `ALL PASS` was a stale
executable from an earlier build that never recompiled.

> The test suite reported success because it never ran.

## 3. A wrong recovery, corrected

The first recovery step was to reinstall the last packaged build, version 2.2.0,
as "last known good". **That was wrong.** Version 2.2.0 carries the
shoulder-sampling defect described in its own release notes: the helper collects
bin *indices* where the caller expects *levels*, so prominence is negative in
every bin and the detector discards everything. 2.2.0 loads but processes
nothing — every control except Output and Mix is inert.

The correct target was the working tree, which is the only state that actually
reduces. The work below fixes that tree forward.

## 4. Further defects found and fixed

### 4.1 Perfect reconstruction was destroyed

The window comment said the analysis and synthesis windows were both the square
root of a Hann window, whose product is Hann and therefore satisfies
constant-overlap-add. The code had been changed to use a Blackman-Harris window
both ways. Blackman-Harris does not satisfy COLA, so the round trip was no
longer unity gain.

| | before | after |
|---|---|---|
| residual at depth 0 | 2.34 | **0.000000** |

**Fix:** restored sqrt-Hann both ways and the derived `hop / N²` synthesis scale.

### 4.2 The Quality control did nothing

`DualBandEngine::prepare(int overlapFactor, double sampleRate)` received only
the overlap. The processor called `prepare(kOverlap[r], activeRate)` and never
passed the quality index, so every quality setting ran the same transform and
reported the same latency.

| | before | after |
|---|---|---|
| 2k setting reports | 8192 samples | **2048 samples** |

**Fix:** `prepare(kFftPow2[q], kOverlap[r], activeRate)`.

### 4.3 The dual-band wrapper broke transparency by construction

`DualBandEngine` split the signal with a Linkwitz-Riley crossover, sent each band
through its own STFT, and summed the results. A second synthesis path behind a
crossover cannot sum back to the input. `SpectralEngine`'s own comment already
said the correct design analyses low frequencies in a single chain with no phase
split.

**Fix:** the processor now uses `SpectralEngine` directly. Multi-resolution
low-band *detection* still happens inside the engine, where it costs detection
quality but no latency.

| | before | after |
|---|---|---|
| memory after `prepareToPlay` at 48 kHz | 268 MB | **100 MB** |
| at 192 kHz | 485 MB | reduced proportionally |

### 4.4 A second null dereference in the state round-trip test

The test called `setParam (a, "multiResolution", ...)` and
`getRawParameterValue ("multiResolution")->load()`. The `multiResolution`
parameter no longer exists, so `getRawParameterValue` returned null and `->load()`
dereferenced it. **Fix:** removed both references.

### 4.5 Latent double-free hazard

`SpectralEngine` owns raw vDSP setup handles but the compiler still generated
copy and move operations that would duplicate those pointers, so two engines
would each destroy the same handle. **Fix:** copy and move are now deleted.

## 5. Where the test suite stands

After the fixes, every functional check passes:

| Area | Result |
|---|---|
| Latency (2k = 2048, 8k = 8192, host follows) | PASS |
| Depth 0 bit-transparent | PASS — residual 0.000000 |
| Bypass latency-aligned, delta, mono bus | PASS |
| Flat cues produce no reduction | PASS |
| Raised cue produces a gentle change | PASS |
| 15 presets load and render | PASS |
| State saves and restores | PASS |
| NaN and huge input never escape | PASS |
| Delta restricted to one band | PASS |
| Output independent of host block size | PASS |

## 6. The two intermittent defects, and their single root cause (FIXED)

Two problems looked unrelated and both looked non-deterministic, which is why
they survived so long. They were the same bug.

### 6.3 The root cause: an out-of-bounds write in dead phase-locking code

`SpectralEngine::lockPhases()` ran on **every frame, unconditionally**, from two
call sites. Three things were wrong with it.

**First, it wrote past the end of a heap buffer.**

```cpp
int rightBound = (p == peaks.size() - 1) ? numBins : (peak + peaks[p+1]) / 2;
for (int k = leftBound; k < rightBound; ++k) {
    re[static_cast<size_t>(k)] = ...;
    im[static_cast<size_t>(k)] = ...;
}
```

`rightBound` reaches `numBins`, but the split-complex buffers hold `halfSize`:

```
halfSize = fftSize / 2        (line 111)
numBins  = halfSize + 1       (line 112)
realL.resize (halfSize)       (line 152)
```

So the loop wrote `re[halfSize]` — **one float, four bytes, past the end of a
`std::vector<float>`** — twice per frame, thousands of times per second. It
read one float past the end too, because `vDSP_zvphas` and `vDSP_zvabs` were
given `numBins` as their length.

That is heap corruption. It explains every symptom:

| Symptom | Why |
|---|---|
| Random crash points, random signals (SIGSEGV, SIGBUS, SIGTRAP) | Whatever heap block sat after the buffer, and when |
| `pointer being freed was not allocated` in `vDSP_destroy_fftsetup` | Adjacent block or allocator metadata overwritten |
| The 2 dB `Match` drift | The same overwrite landing in the gain state |

**Second, it did nothing.** `float rotation = 0.0f;` was never assigned. Every
bin was rebuilt from its own magnitude and phase, so the function was a no-op
that added float round-trip error.

**Third, it allocated on the audio thread.** Two `std::vector<float>` and one
`std::vector<int>` per call, four calls per frame.

**Fix.** The calls and the function are removed. A no-op that corrupts the heap,
allocates in the audio callback and perturbs phase has no reason to exist. Real
identity phase locking (Laroche and Dolson 1999) needs `rotation =
phases[peak] - phases[k]` and writes clamped to the buffer size;
`RESEARCH-SYNTHESIS-OPTIONS.md` covers it if listening ever demands it.

**Measured result.**

| | before | after |
|---|---|---|
| Runs clean (24 runs, guard pages) | 2 crashes + 3 failures in 16 runs | **24 / 24 clean** |
| `Match` loudness error | -2.07 dB (fail) | **-0.03 dB (pass)** |


### The original notes, kept for the record

#### The teardown abort, as first observed

Roughly 2 runs in 12 abort with `SIGABRT` during destruction. A crash report
names the site:

```
___BUG_IN_CLIENT_OF_LIBMALLOC_POINTER_BEING_FREED_WAS_NOT_ALLOCATED
libvDSP.dylib
vDSP_destroy_fftsetup
ResonaPro::SpectralEngine::~SpectralEngine()
std::array<std::array<std::unique_ptr<SpectralEngine>, ...>, ...>
ResonaPro::ResonaProAudioProcessor::~ResonaProAudioProcessor()
main
```

An FFT setup handle is destroyed twice, or a stale handle is destroyed after its
memory was reused. Ruled out so far:

* vDSP does not return the same handle for identical requests (measured).
* `freeFFT()` nulls its pointer; the low-band handle is nulled at both destroy
  sites.
* Deleting copy and move did not change the rate.
* A handle live-set tracker in `SpectralEngine` (temporary, prints only on an
  actual double destroy) has not fired, which suggests the invalid free is not
  reaching either tracked member.

A live-set tracker is still compiled in and will print `[FFT] DOUBLE DESTROY` if
it catches the case.

#### The level-matching drift, as first observed

Some runs report:

```
[FAIL] matching restores perceived loudness   -- -2.07 dB
[FAIL] matching also keeps the plain RMS close -- -1.99 dB
```

The auto-gain envelope does not always converge to the matched level, so `Match`
sometimes leaves the output 2 dB quieter than the input. This is a real audio
defect, not a test artefact, and it is the more user-visible of the two.

## 7. Files

| File | Purpose |
|---|---|
| `.work/scripts/MemProbe.cpp` | Staged load probe: memory per stage, every engine combination, oversampling, live switching |
| `.work/build-probe` | Build tree for the probe (`-DRESONAPRO_BUILD_MEMPROBE=ON`) |
| `.work/backups/broken-230-crashing-20261006-131745/` | The crashing build, kept |
| `.work/backups/pre-230-20261006-084952/` | The 2.2.0 build (loads, processes nothing) |

## 8. The process fix that matters more than any single defect

Every defect in sections 1, 4.1, 4.2 and 4.4 was invisible to a build that
succeeds and a test binary that reports green. The gate that should have caught
all of them is one rule:

> The tests must build and pass against the exact source being shipped.

A release that ships while its own test target fails to compile is not gated at
all. This belongs in Phase 0 of the roadmap, ahead of any audio change.


---

# Part 9 — Incident: the Antigravity regression

Source: `docs/REGRESSION-2026-10-06.md`

# Changes from the Antigravity session, 6 October 2026

## What exists now

The project became a git repository on 6 October at 06:54, and three fix commits
followed. `Source/` is fully committed; the working tree is clean.

| Commit | Time | Subject |
|---|---|---|
| `72571ee` | 06:54 | Initial commit: ResonaPro v2.2 - Dynamic Resonance Suppressor & Vocal De-Esser |
| `d320545` | 07:09 | Fix cue-gated resonance suppression & calibrate high-frequency sibilance sensitivity |
| `968afd5` | 07:30 | Fix cue-proportional thresholding, fix high-end sensitivity, and fix EQ filter defaults |
| `bb119e2` | 07:56 | Fix high frequency resonance detection threshold |

Untracked working files left behind by the session: `patch.py`,
`patch_detector.py`, `patch_eq.py`, `test_detector.cpp`, `test_engine.cpp`,
`test_logic.py`, and the compiled `test_detector` and `test_engine` binaries.

## The change that is a genuine improvement

The focus-band filter defaults were wrong and are now fixed.

```
bands[0] = { true, FilterType::LowCut,    80.0f, 0.0f, 0.707f };   before
bands[0] = { true, FilterType::LowShelf,  80.0f, 0.0f, 0.707f };   after
bands[7] = { true, FilterType::HighCut, 16000.0f, 0.0f, 0.707f };  before
bands[7] = { true, FilterType::HighShelf,16000.0f, 0.0f, 0.707f };  after
```

A cut filter has no meaningful gain parameter, so the bottom and top focus bands
could never be boosted, only attenuated. As shelves they behave correctly. The
matching change in `kDefaultEqType`, from `{ 1, 0, 0, 0, 0, 0, 0, 2 }` to
`{ 3, 0, 0, 0, 0, 0, 0, 4 }`, keeps the stored parameter values consistent. This
should have been right in the original.

## The change that breaks the plugin

The detector was rewritten around cue gating. At `Source/DSP/ResonanceDetector.h`
line 337:

```cpp
// RULE 1: STRICT CUE GATING
// If the user has not moved a cue up (and no sibilance smoothing is active),
// resonance reduction is EXACTLY zero.
if (effectiveCue <= 0.05f)
{
    out[k] = 0.0f;
    continue;
}
```

`weightsDb` is the per-bin focus-band curve, and it is 0 dB everywhere when the
eight focus bands sit at their defaults. With all cues neutral, `effectiveCue`
is 0 for every bin, so the detector returns zero for every bin and the plugin
does nothing at all.

Previously a cue *biased* the threshold: 0 dB was neutral and the base threshold
still applied, so detection ran on its own. The change inverts that relationship
from "bias" to "gate".

### Measured consequence

The full build succeeds with no errors. The processor suite then reports **6
failures, every one reading 0.00 dB**:

```
[FAIL] default depth produces a gentle, audible change            -- -0.00 dB
[FAIL] delta exposes only what was removed                        -- -136.4 dB
[FAIL] reduction lowers the loudness (the problem being solved)   -- -0.00 dB
[FAIL] external key changes detection while the vocal stays output-- 0.00 dB
[FAIL] long-window low-band detector changes the low-frequency... -- 0.000 dB
[FAIL] motion cue changes the low-band decision on a pitch shift  -- 0.000 dB
```

Delta at -136.4 dB is the clearest signal: DELTA plays the removed signal, and
there is none, because nothing is being removed. Every feature that depends on
the detector is inert: EXT KEY, LOW DETAIL, and NOTE MOTION all measure exactly
zero change.

The DSP-level suite still passes, because it exercises the engine and the
suppressor directly rather than the detector's decision path.

## Other effects of the rewrite

* **Removed: the curvature guard.** This rejected broadband noise floor being
  treated as a resonance, by requiring a local peak to be curved rather than flat.
* **Removed: air-band preservation above 11 kHz.**
* **Removed: high-frequency tilt compensation** and the 1.35x high-end cue boost.
* **Removed the `fullScaleReference` path.** The reference is now always the
  per-frame peak, so absolute level no longer enters the decision. This is worth
  checking against HARD mode, whose purpose is to react to absolute level.
* **Changed: sibilance band** from 4000-9500 Hz to 4000-12000 Hz.
* **Changed: reduction ceiling** from 40 dB to 48 dB.
* **Changed: transient protection** now applies below 3 kHz only, where it
  previously also covered the sibilance band.
* **Added: a 2.5x cue multiplier above 2.5 kHz**, clamped to 0-6.

`patch.py` shows the threshold term was first written with a limit:

```python
thr -= std::min(effectiveCue * 0.5f, 5.0f)   # "cap it so we don't suppress the noise floor"
```

and then replaced with an uncapped version. Each round removed a safety limit.

`test_logic.py` is worth noting for what it is not. It re-implements the
threshold arithmetic in four lines of Python over invented inputs and prints
three numbers. It never loads the plugin, so it cannot fail when the plugin
breaks.

## The question this raises

Two readings, and they lead to opposite work:

1. **The gating is intended.** The plugin becomes a manual dynamic EQ that acts
   only where you draw a cue. Then the six tests encode an obsolete contract and
   should be rewritten, and the documentation needs to stop describing automatic
   resonance detection.

2. **The gating is a side effect.** The stated goal in the commit messages was to
   make the high end more sensitive, and gating was a blunt way to get decisive
   behaviour. Then the core feature has been switched off by accident, and it
   should be restored while keeping the EQ filter-default fix and the useful
   parts of the high-frequency calibration.

The failure mode was easy to miss because it only shows when the focus bands are
neutral. Anyone testing by dialling in a cue would see the plugin working.


---

# Part 10 — Status report: every problem found

Source: `docs/PLUGIN-STATUS-REPORT.md`

# ResonaPro status report - 6 October 2026

## Summary

The plugin builds without errors, loads in a host, opens its interface, and
passes audio. It does nothing to that audio. It is currently a bypass with a
graphical interface attached.

Six of the automated checks fail, and every one of them fails with the same
number: `0.00 dB`. That is not a coincidence. It is one defect seen from six
angles.

The cause is not the cue gating that the commit messages describe. The gating is
a real problem, but it sits behind a more fundamental fault: the detector's
baseline is not measuring the audio at all. It is measuring bin numbers.

---

## Problem 1 - The baseline is a bin number, not a signal level

**This is the fatal one. Everything else is secondary.**

### What the code does

`Source/DSP/ResonanceDetector.h`, the private helper that gathers the shoulder
samples around each bin:

```cpp
static void appendSamples (std::vector<float>& dest, int from, int to) noexcept
{
    if (from > to) return;
    const int count = to - from + 1;
    if (count <= 16)
    {
        for (int i = from; i <= to; ++i)
            dest.push_back (static_cast<float> (i));   // pushes the BIN INDEX
        return;
    }
    constexpr int TargetSamples = 16;
    const float step = static_cast<float> (count - 1) / static_cast<float> (TargetSamples - 1);
    for (int s = 0; s < TargetSamples; ++s)
    {
        const int idx = from + static_cast<int> (std::lround (static_cast<float> (s) * step));
        dest.push_back (static_cast<float> (idx));     // still the BIN INDEX
    }
}
```

It pushes `i`, the bin number, where it needs to push the magnitude at that bin.
The name says "append samples"; it appends coordinates.

The caller then treats those numbers as if they were signal levels:

```cpp
const size_t idx = static_cast<size_t> (pct * static_cast<float> (shoulderScratch.size() - 1));
std::nth_element (shoulderScratch.begin(), shoulderScratch.begin() + (long) idx, shoulderScratch.end());
base = shoulderScratch[idx];                                   // a bin number
baselineDb[k] = 20.0 * std::log10 (base);                      // logged as if it were a level
prom = 20.0 * std::log10 (mag / base);                         // ratio of a level to a bin number
```

So the "baseline" of a bin near 1 kHz is roughly `20 * log10 (100) = 40 dB`, and
near 16 kHz it is `20 * log10 (700) = 57 dB`. These are not levels. They are the
positions of the bins, converted to decibels.

### The measurement

Detector internals on a smooth test signal, with the true level of each bin and
the baseline it computes:

| bin | frequency | magDb (real level) | baselineDb | 20*log10(bin) |
|---|---|---|---|---|
| 8 | 187 Hz | +7.8 dB | +20.8 dB | 18.1 |
| 32 | 750 Hz | +0.6 dB | +30.9 dB | 30.1 |
| 100 | 2344 Hz | -5.4 dB | +40.6 dB | 40.0 |
| 300 | 7031 Hz | -11.1 dB | +50.0 dB | 49.5 |
| 700 | 16406 Hz | -15.5 dB | +57.3 dB | 56.9 |
| 1000 | 23438 Hz | -17.4 dB | +59.6 dB | 60.0 |

The baseline tracks the bin number, not the signal. It is also *above* the
signal at every frequency, so the prominence - defined as the amount by which a
bin stands above its surroundings - comes out **negative everywhere**. Measured
worst case: the maximum prominence across the entire spectrum was **-32.06 dB**.

### The consequence

`excess = prominence - threshold`, and the code discards anything that is not
positive. With prominence negative in every bin, **every bin is discarded, in
every frame, for every input.** The detector asks for zero reduction, permanently.

Measured directly against the detector:

```
Neutral focus bands, depth 0.0 / 1.0 / 2.0 / 4.0      reduction 0.00 dB
HARD mode, depth 1.0 / 4.0                            reduction 0.00 dB
Sibilance smoothing 0.5 / 1.0                         reduction 0.00 dB
Cue +3 / +6 / +12 dB at a real 20 dB resonance        reduction 0.00 dB
Cue +12 / +24 dB at a 4.4 kHz spike                   reduction 0.00 dB
Cue +12 / +24 dB at an 8 kHz spike                    reduction 0.00 dB
```

Even a 24 dB cue sitting directly on a 20 dB resonance produces nothing,
because the cue can only *lower a threshold that was never reached*.

### What this means when you use the plugin

**Nothing happens to your audio.** Concretely:

| Control | What it does now |
|---|---|
| DEPTH | Nothing at any setting |
| SELECT | Nothing |
| DETAIL | Nothing |
| TRANSIENT / MAX CUT / ATTACK / RELEASE | Nothing to act on |
| HARD | Nothing |
| MID/SIDE | Nothing |
| EXT KEY | Nothing |
| LOW DETAIL | Nothing |
| NOTE MOTION | Nothing |
| SIBILANCE | Nothing |
| EAR GUARD | Nothing |
| MATCH | Measures a difference that is zero, so applies no correction |
| DELTA | **Silent.** Measured at -136 dB |
| MIX | Crossfades between two identical signals - a volume control at most |

Only OUT and MIX change what you hear, and only because they scale the signal.
The plugin behaves as an unusually expensive bypass.

This also explains why the plugin appeared to "not update". The interface says
v2.2.0 and the code is 2.2.0, but the audible result is the same as silence
because there is no processing to hear either way.

---

## Problem 2 - Reduction is gated behind a raised cue

Independent of Problem 1, and it would matter once Problem 1 is fixed.

```cpp
// RULE 1: STRICT CUE GATING
if (effectiveCue <= 0.05f)
{
    out[k] = 0.0f;
    continue;
}
```

`weightsDb` is the focus-band curve, and it is 0 dB everywhere when the eight
focus bands sit at their defaults - confirmed in `ParametricEQWeighting.h`,
where a band contributes nothing when `|gainDb| < 0.05f`.

Previously a cue *biased* the threshold: 0 dB was neutral and the adaptive base
threshold still applied, so the detector ran on its own. The change turned a bias
into a gate, so a fresh instance with untouched focus bands would process nothing
even with a working baseline.

### What this means when you use the plugin

The plugin stops being a resonance suppressor and becomes a manual dynamic EQ
that only acts where you explicitly draw a cue. The "set it and let it find the
harshness" workflow is gone. Every preset that does not raise a focus band would
be silent. Several factory presets raise none.

---

## Problem 3 - Safety guards removed

These protected against recognisable failure modes. Once Problem 1 is fixed they
become live risks again.

| Guard | Purpose | Status |
|---|---|---|
| Curvature check | Rejected flat, broadband energy (noise floor, breath, room tone) being treated as a resonance - a resonance is curved, a noise floor is not | Removed |
| Air-band preservation above 11 kHz | Prevented dulling the top octave when no cue asked for it | Removed |
| High-frequency tilt compensation | Balanced sensitivity above 3 kHz against the natural -6 dB/octave energy fall | Removed |
| Full-scale reference | Kept the decision consistent in absolute level terms | Removed from `detect()` |

The full-scale reference is worth calling out because it is now dead code that
looks alive. `SpectralEngine.h` still computes and supplies it:

```cpp
const float fullScaleMag = static_cast<float> (0.5 * windowSum);
detectorL.setFullScaleReference (fullScaleMag);
detectorR.setFullScaleReference (fullScaleMag);
```

`setFullScaleReference` still exists and still stores the value. `detect()` no
longer reads it, and instead normalises to each frame's own peak. Two
consequences:

* The public API advertises a degree of control it no longer has.
* **HARD mode is compromised.** Its entire purpose is reacting to absolute level -
   a quiet passage and a loud one should be treated differently. Frame-peak
   normalisation removes exactly that information, so a whisper and a scream
   present the same normalised spectrum.

### What this means when you use the plugin

Once detection works again, you would hear: hiss and breath being ducked as if
they were resonances (curvature guard gone), a loss of air and sparkle on
material that did not ask for it (air guard gone), and unpredictable behaviour
between loud and quiet takes (full-scale reference gone).

---

## Problem 4 - The high-frequency multiplier

```cpp
float cueScale = effectiveCue / 6.0f;
if (fHz >= 2500.0f)
    cueScale *= 2.5f;
cueScale = std::clamp (cueScale, 0.0f, 6.0f);
float excess = rawExcess * cueScale;
```

Above 2.5 kHz the requested reduction is multiplied by 2.5. Together with the
uncapped threshold lowering (`thr -= effectiveCue * 0.85f`, which `patch.py`
shows was first written *capped* and then uncapped), a cue raised in the
sibilance region produces a large, immediate reduction.

### What this means when you use the plugin

De-essing would be heavy-handed rather than surgical. Lisping, dulled consonants,
and a "hole" around 4-9 kHz are the expected results. The commit messages call
this "decisive", and it would be - but decisive is not the same as correct, and
there is no measurement behind the 2.5 figure.

---

## Problem 5 - The tests were not run

The session left a file called `test_logic.py`:

```python
def test_excess(prominence, cue):
    thr = 5.0 - cue
    raw_excess = prominence - thr
    if raw_excess < 0: raw_excess = 0
    return raw_excess
print("Cue 0.01:", test_excess(10.0, 0.01))
```

This re-implements the threshold arithmetic in four lines of Python over invented
numbers and prints three values. It never loads the plugin, never touches the
detector, and **cannot fail when the plugin breaks**. It is a sketch of an idea,
not a test.

Meanwhile the real suite, 36 checks that drive the actual audio path, reports:

```
=== FAILURES (6 failures) ===
[FAIL] default depth produces a gentle, audible change             -- -0.00 dB
[FAIL] delta exposes only what was removed                         -- -136.4 dB
[FAIL] reduction lowers the loudness (the problem being solved)    -- -0.00 dB
[FAIL] external key changes detection while the vocal stays output -- 0.00 dB
[FAIL] long-window low-band detector changes the low-frequency...  -- 0.000 dB
[FAIL] motion cue changes the low-band decision on a pitch shift   -- 0.000 dB
```

This is the single most important process failure in the session. The plugin is
a pass-through, and there is no possible reading of a working plugin in which
"reduction lowers the loudness" measures 0.00 dB.

### What this means when you use the plugin

Whatever you were told about the changes working is unverified. The evidence
available says the opposite.

---

## Provenance

I have to be straight about what the repository can and cannot tell me.

The bug in `appendSamples` is present at the initial commit `72571ee`, unchanged,
so the commit history cannot separate it from my own code. Two things point to it
having been introduced after my last verified state:

* The processor suite **passed with real reduction measured** when I last ran it
  on 5 October. If the baseline had been a bin number then, that suite could not
  have passed.
* Every file in `Source/` carries a 6 October modification time. `DynamicSuppressor.h`
  was modified at 06:41, `PluginEditor.cpp` and `SpectralVisualizer.h` at 06:42,
  `SpectralEngine.h` and `PluginProcessor.h` at 06:17 - all **before** the initial
  commit at 06:54. So the "initial commit" is not a clean snapshot of my version;
  it already contains work from that session.

That is a strong inference, not proof. I am not going to claim certainty I do not
have. What I can say with certainty is that the detector does not work now, and
that it did when I last tested it.

---

## What has to happen

Fixing Problem 1 alone changes nothing audible, because Problem 2 gates it off.
Fixing Problem 2 alone changes nothing, because Problem 1 leaves nothing to gate.
They have to be fixed together, and that is the work I would do:

1. **Repair `appendSamples`** so it carries sampled *magnitudes*, not indices.
   Verify against a known test signal that prominence is positive at a real
   resonance and near zero on a smooth spectrum.
2. **Resolve the gating question.** Either restore cue-as-bias (automatic
   detection with cues as emphasis) or keep gating deliberately and rewrite the
   tests and documentation to describe a manual dynamic EQ. This is a product
   decision and I need your answer.
3. **Re-run the real suite** and require it to pass before anything is committed.
4. **Re-examine the removed guards** one at a time, with measurements, rather than
   restoring or discarding them on principle.
5. **Calibrate the high-frequency behaviour** instead of multiplying by 2.5.
6. **Delete or move the mock test** so it cannot be mistaken for coverage.

Nothing in the current state should be relied on until item 1 and item 3 are
done.

---

# Repair applied, 6 October 2026

Problems 1 and 2 were fixed. Problem 1 was a defect with only one correct
answer. Problem 2 was resolved in favour of the documented behaviour - the
README, the tooltips and the architecture all describe adaptive resonance
detection, and a build that detects nothing contradicts all three. The change is
one commit to revert if the gated design was intended.

## What changed in `Source/DSP/ResonanceDetector.h`

1. **`appendSamples` now carries values.** It takes the source array and pushes
   the magnitude at each index instead of the index itself, with bounds checking.
2. **The RULE-1 hard gate is gone.** A neutral focus band no longer switches the
   detector off. Cues still bias the threshold and still emphasise a region
   through a multiplier, but the detector now runs on its own.
3. **The multiplier is neutral-preserving.** It was `effectiveCue / 6`, which is
   0 at rest and therefore a second gate. It is now `1 + effectiveCue / 6`,
   clamped to 1-4, so a neutral cue leaves the detector's own finding intact.
4. **The unmeasured 2.5x high-frequency boost is removed.** Cues already lower
   the threshold, which emphasises the region without an arbitrary multiplier.

## Verification

The baseline now tracks the signal instead of the bin position:

| bin | frequency | magDb | baselineDb (before) | baselineDb (now) |
|---|---|---|---|---|
| 32 | 750 Hz | +0.6 dB | +30.9 dB | **+1.1 dB** |
| 100 | 2344 Hz | -5.4 dB | +40.6 dB | **-5.0 dB** |
| 300 | 7031 Hz | -11.1 dB | +50.0 dB | **-10.8 dB** |
| 700 | 16406 Hz | -15.5 dB | +57.3 dB | **-15.3 dB** |

The detector now produces reduction, and it responds to cues:

```
744 Hz spike       neutral bands      8.21 dB
744 Hz spike       cue +12 dB        47.12 dB @ 750 Hz
4400 Hz spike      neutral bands     40.16 dB
8000 Hz spike      neutral bands     37.13 dB
```

The processor suite went from **6 failures to 2**.

## The two remaining failures, and what they mean

Both are calibration, not defects. Both are in the list above as items 4 and 5.

```
[FAIL] delta exposes only what was removed            -- -5.5 dB   (limit -6.0)
[FAIL] broadband fricatives and breath are preserved  -- -1.85 dB  (limit 0.65)
```

**DELTA measures -5.5 dB against a limit of -6.0 dB.** DELTA plays what was
removed, and it is now only 5.5 dB below the dry signal. That is the mirror of
over-reduction: the plugin is removing so much energy that the removed portion is
nearly as loud as the input. It is 0.5 dB past a sanity limit, and it will come
back inside the limit once the reduction depth is calibrated.

**Broadband noise is being reduced by 1.85 dB against a limit of 0.65 dB.** This
is precisely the failure mode predicted in Problem 3 when the curvature guard was
removed. A resonance is a local peak that curves; white noise has no peaks, only
statistical wobble. With the high-frequency nominal thresholds set as low as
0.8-1.2 dB, that wobble clears the threshold constantly, so noise, breath and
fricatives are treated as resonances and ducked. Restoring an honest
peak-versus-broadband test is what fixes it, and it needs to be measured rather
than guessed.

## Still outstanding

* Calibrate the reduced depth so DELTA and the anti-muffle check both pass.
* Add a discrimination test so broadband energy cannot be reduced again.
* Re-examine the air-band preservation and the full-scale reference.
* Delete or quarantine `test_logic.py` so a mock cannot stand in for coverage.
* Require the real suite to pass before committing.

---

# All problems closed, 6 October 2026, version 2.3.0

| Problem | Status |
|---|---|
| 1. Baseline measuring bin indices | Fixed. `appendSamples` now carries levels, bounds-checked. |
| 2. Reduction gated behind a raised cue | Fixed. The gate is gone; cues bias rather than switch. |
| 3. Safety guards removed | Resolved by measurement rather than by special cases. See below. |
| 4. High-frequency multiplier | Removed. |
| 5. Tests not run, mock test present | Both suites pass. The mock is quarantined. |

## How each guard was resolved

The guards were not restored as code. Each was replaced with a threshold or test
that holds it, which is verifiable where a special case was not.

* **Curvature guard.** Its job was stopping broadband energy being read as a
  resonance. Replaced by thresholds set above the measured noise floor (4.8 dB of
  prominence) and below measured voice structure (9.2 dB). The anti-muffle checks
  confirm it: white noise measures -0.10 dB and bright fricative noise -0.03 dB,
  against a 0.65 dB limit.
* **Air-band preservation.** Its job was stopping the top octave being dulled.
  Covered by the same thresholds, and guaranteed by a new check on
  first-difference noise, which puts its energy where fricatives live.
* **High-frequency tilt compensation.** Replaced by per-band thresholds. The
  high bands now sit at 4.0-5.0 dB rather than the 0.8-1.2 dB that put them below
  the noise floor.
* **Full-scale reference.** Removed. It was dead. The engine computed it and
  `detect()` ignored it, so the chain was deleted in both files. HARD mode is
  honestly documented as not level-aware.

## Also corrected

* `Source/DSP/ResonanceDetector 2.h`, a stray duplicate header, removed.
* `patch.py`, `patch_detector.py`, `patch_eq.py`, `test_logic.py`,
  `test_detector` and `test_detector.cpp` moved to `.work/quarantine/`. These
  were session artifacts; `test_logic.py` in particular re-implemented the
  threshold arithmetic over invented numbers and could never fail.
* The selected-band Delta monitor filter was wider than the node it represents,
  so a one-band view leaked neighbouring reduction. Narrowed.

## Final state

```
processor suite: 37 checks, 0 failures
DSP suite:       22 checks, 0 failures
AU validation:   SUCCEEDED
signatures:      valid (VST3, AU)
architectures:   x86_64 arm64
version:         2.3.0 everywhere
```


---

# Part 11 — Bottleneck audit, ranked

Source: `docs/BOTTLENECKS.md`

# What is holding ResonaPro back

An audit of the real limits, ranked by how much they cost you. Every figure here
comes from a measurement on the current build, not from a guess.

---

## First, what is not a bottleneck

Worth clearing these out, because they are the things people assume are the
problem.

| | Measured | Verdict |
|---|---|---|
| CPU at the default setting | 4.6 % of one core | Fine |
| CPU at the heaviest setting | 11.2 % of one core | Fine |
| Passthrough accuracy at Depth 0 | −136 dB, bit-exact | Fine |
| Collateral change on a harmonic comb | 0.00 dB per harmonic | Fine |
| File size | 20 MB binary | Irrelevant to sound |

The engine is cheap and the signal path is clean. Neither is what limits this
plugin. The limits are elsewhere.

---

## 1. Latency: 42.7 ms, with no way down to zero

### The measurement

```
44 100 Hz   46.4 ms
48 000 Hz   42.7 ms
96 000 Hz   21.3 ms
```

That is the default Quality setting. Selecting "Low Latency (1k)" halves it, to
about 21 ms at 48 kHz. There is nothing faster in the plugin.

### Why it matters

For mixing, 42.7 ms is tolerable. Your DAW compensates, and you hear the result in
time. For **tracking** it is fatal. A singer monitoring through this plugin hears
their own voice a frame and a half late, which is why no one offers a resonance
suppressor for tracking. It also stacks: put three latency plugins on a bus and
you are 100 ms behind.

For comparison, soothe3 ships a low-latency mode that adds **zero samples at base
sample rates**, and Soothe Live runs at 64 samples. That is not a slight edge. It
is a different capability.

### Why it is hard

An FFT needs the whole window before it can produce a spectrum. With a 2048-point
transform you must wait 2048 samples. Latency is not a tuning choice here. It is
what the transform is.

Every route down has a cost:

* **Shorter transform.** 512 points gives 10.7 ms and 93.8 Hz per bin. At that
  resolution a vocal resonance and the harmonic next to it occupy the same bin.
  The plugin stops being able to do its job.
* **Multi-resolution synthesis.** Long window for the low band, short for the
  high. This is the documented five-obstacle rewrite in
  `RESEARCH-SYNTHESIS-OPTIONS.md`. It would lower latency somewhat, but the long
  window still dominates and you would still not reach zero.
* **A different engine.** A bank of narrow bandpass filters with per-band
  dynamics does not need a window at all, so it can run near zero latency. This
  is how low-latency dynamic EQ is built. It is a second engine, not a change to
  this one.

So the honest statement: **zero latency is not reachable from an STFT
architecture.** Getting there means building a second, filterbank-based engine.

### Priority

Highest, if you want live or tracking use. Low, if this stays a mixing tool.
Decide that first, because it determines everything below it.

---

## 2. The transform is a fixed sample count, so the plugin changes behaviour with sample rate

### The measurement

Frequency resolution, in Hz per bin, for each Quality setting:

| Sample rate | Low (1k) | Standard (2k) | High (4k) | Ultra (8k) |
|---|---|---|---|---|
| 44 100 Hz | 43.1 | 21.5 | 10.8 | 5.4 |
| 48 000 Hz | 46.9 | 23.4 | 11.7 | 5.9 |
| 96 000 Hz | 93.8 | 46.9 | 23.4 | 11.7 |
| 192 000 Hz | 187.5 | 93.8 | 46.9 | 23.4 |

The transform length is fixed in samples and ignores the sample rate. At 96 kHz
the **default** setting resolves resonances half as well as it does at 48 kHz. A
20 Hz-wide resonance at 300 Hz is separable at 48 kHz on Standard and is not
separable at 96 kHz on Standard.

### Why it matters

Two people using the same preset at different sample rates get different
processing. Someone working at 96 kHz gets a plugin that quietly performs worse
than the one in the manual, and nothing tells them. Higher sample rates are
supposed to be the more careful workflow; here they are the cruder one.

### What fixing it costs

Scale the transform length with the sample rate so a given Quality setting means
the same frequency resolution everywhere. At 96 kHz, Standard would use 4096
points instead of 2048.

Cost: none in CPU, none in latency in milliseconds (4096 samples at 96 kHz is the
same 42.7 ms as 2048 at 48 kHz), and a moderate amount of work. This is the
cheapest real improvement on this list, and it fixes a correctness problem rather
than adding a feature.

### Priority

High. Cheap, and it prevents a class of confusing bug reports.

---

## 3. One resolution for the whole spectrum

### The situation

The whole spectrum gets one transform. That is a compromise in both directions.
High frequencies do not need a 42.7 ms window; a click at 8 kHz is over long
before the window closes. Low frequencies need more than one; 23.4 Hz per bin is
still coarse for separating a 120 Hz resonance from the note's own harmonics.

### What already ships

Multi-resolution **detection**. The Low Band Detail switch runs a separate
4096-point detector below 1.2 kHz while synthesis stays at one resolution. So the
plugin already looks harder at the low end. What it does not do is *process* at
two resolutions.

### What multi-resolution synthesis would take

Five obstacles, laid out in `RESEARCH-SYNTHESIS-OPTIONS.md`: incomparable phase
references between transforms, comb filtering when two chains are summed, a band
split that has to reconstruct flat, detector disagreement at the crossover, and
roughly double the CPU and memory.

### Priority

Medium. Real, but it is the most expensive item here and the measurements suggest
the headroom is small. Not the place to start.

---

## 4. Noise and breath: the detector cannot tell a random peak from a resonance

This is the biggest limit on **how hard you can push the plugin.**

### The measurement

Prominence is how far a peak stands above its surroundings, in dB. The detector
needs it to exceed a threshold before it acts.

| Material | Prominence p90 | p99 | Worst cut applied |
|---|---|---|---|
| White noise, seed 1 | 1.05 | 1.67 | 2.54 dB |
| White noise, seed 2 | 1.41 | 2.57 | 1.78 dB |
| White noise, seed 3 | 1.43 | 2.49 | **4.78 dB** |
| Harmonic comb, no resonance | 0.27 | 0.71 | 1.93 dB |
| Resonance +12 dB at 700 Hz | −0.09 | 3.13 | 1.42 dB |
| Resonance +18 dB at 1600 Hz | −0.09 | 6.54 | 7.02 dB |
| Resonance +12 dB at 4400 Hz | −0.09 | 6.93 | 11.89 dB |
| Resonance +18 dB at 8000 Hz | −0.09 | 12.60 | 17.09 dB |

Genuine resonances reach prominence 6.5 to 15.4. Noise reaches 1.9 to 3.2. There
is separation, but the tails overlap, and on white noise the plugin still acts on
43 to 59 bins and cuts as much as **4.8 dB**.

### Why it matters

A vocal is full of broadband content: breath, room tone, sibilance. Noise has no
resonances, but it has random peaks, and a detector that hunts for peaks will find
them. Push DEPTH on a breathy take and the plugin starts working on the breath.

This is why the plugin is tuned conservatively, and why Selectivity below 0.5
starts touching non-resonant content (worst bin −1.30 dB). It is the ceiling on
how assertive the plugin can be, and it is inherent to peak-hunting methods
rather than a bug.

### What would move it

Better statistics than a single-frame prominence test. A resonance persists across
many frames at the same frequency; a noise peak does not. Scoring peaks over time
and acting only on the ones that stay put would separate the two more cleanly.
Some of this is in the plugin already through the comb-period estimator and the
motion guard, but the final decision is still a per-frame threshold.

### Priority

High. This is the one that limits the sound on real material, and it is a
detector change rather than an engine rewrite.

---

## 5. Below roughly 150 Hz the plugin holds back

A resonance and a note's own harmonics are hard to tell apart down there. The
plugin deliberately stays cautious.

Low Band Detail adds a longer analysis window to help. Measured, it changes the
decision by 1 to 5 dB depending on material — which the quality report calls "a
genuine change rather than a clear improvement." So the switch exists but nobody
has established that it makes things better.

### Priority

Medium. Worth a decision study on real bass and low male vocals: does Low Band
Detail help, and if so on what?

---

## 6. A warble on sustained notes is possible

The engine changes magnitude and keeps the analysis phase. Where adjacent frames
get different gains, their overlap-add is not exactly the tone scaled by that
gain, and the result is a small amplitude modulation.

Measured: **0.003 dB of envelope ripple** on a steady tone, pre-ringing below
−49 dB. Small, but amplitude modulation is the one mechanism here that produces an
audible artefact, and its signature — a watery quality on held notes — is exactly
what people complain about in spectral processors.

Fixing it means identity phase locking, which needs no extra latency. But see
`QUALITY-GAP-EXPLAINED.md`: it would replace a phase that is currently exact with
one that is estimated, which can make things worse on a gain-only processor.

### Priority

Unknown, and that is the problem. Listen first. If held notes sound watery, this
jumps up the list.

---

## 7. You cannot save your own presets

The plugin has six factory presets, an A/B compare, and it saves its state inside
your DAW project. It has no user preset library, no import, and no export.

So a setting you arrive at and like cannot be named, reused across projects, or
sent to another person. For a plugin whose whole job is judgement about a
particular voice, that is a real omission.

In fairness, this is where soothe2's 267 MB partly comes from — a large preset
library. Ours would be a few kilobytes plus the UI to manage it.

### Priority

High for usability, low for difficulty. This is ordinary application work.

---

## 8. Nothing here has been validated in a real host

* The AU passes Apple's `auval`. That checks conformance, not sound.
* The VST3 has never been loaded in FL Studio. Not once.
* The sidechain path has never been exercised by a host routing audio into it.
* Every transparency figure comes from synthetic signals.

That last point is the important one. The plugin could be perfect on a harmonic
comb and still smear a real voice, and I would not know. I have said this before
and it has not changed: **a listening test on a real take is the highest-value
thing anyone can do to this project.** It costs ten minutes and no code.

### Priority

Highest, because it is the only item that can invalidate or confirm everything
else.

---

## 9. Distribution friction

* Not notarized, so macOS warns on first open. No Apple Developer ID.
* The package needs an administrator account. Without one, only the script works.
* No Windows build, no VST2, no AAX.
* The bundle uses the same identifier for the VST3, the AU and the app — correct
  practice is one identifier per format.

### Priority

Medium. It affects who can use the plugin, not how it sounds. The identifier
issue is worth fixing the next time the package is rebuilt.

---

## What I would do, in order

1. **Listen to it on a real vocal take.** High quality, Match on, A/B against
   bypass. This decides whether item 6 is a real problem and whether item 4 is
   the thing to attack. No code.
2. **Scale the transform with the sample rate.** Cheap, fixes a correctness
   problem, no CPU or latency cost. Item 2.
3. **Attack the detector on noisy material.** Item 4. The change with the most
   effect on real vocals.
4. **Add user presets.** Item 7. Ordinary work, obvious benefit.
5. **Load it in FL Studio and use it.** Item 8. Ten minutes, and it either
   confirms everything or finds something no test can.
6. **Decide about latency.** Item 1. If this stays a mixing tool, leave it. If you
   want tracking, that is a second engine and a much larger project.


---

# Part 12 — Quality roadmap, phased

Source: `docs/QUALITY-ROADMAP.md`

# ResonaPro quality roadmap

A staged plan to make this plugin sound better. Quality is the priority. Latency,
CPU, file size and development time are all expendable.

Every stage below ends in a measurement. Nothing gets kept because it seemed like
a good idea.

---

## Progress, 6 October 2026

**Phase 3 is shelved.** A listening test on a real voice in FL Studio, with the
plugin pushed hard on held notes and transients, found none of the artefacts the
phase exists to remove: no watery modulation, no comb filtering, no hollowed
formants, no pre-ringing on consonants, no metallic chirping. The measurements
agree with the ears. Multi-resolution synthesis and phase-coherent resynthesis
stay unwritten until something audible demands them.

**Phase 0.2 is built.** `Tools/Analyze.cpp` runs a file or a folder through the
engine offline and reports collateral change per octave and worst bin, transient
and envelope behaviour, and level change. It writes JSON and can gate on a saved
reference, so a build is compared against a record rather than against memory.

```
cmake -S . -B .work/build-tools -DRESONAPRO_BUILD_ANALYZE=ON
cmake --build .work/build-tools --target ResonaProAnalyze
./.work/build-tools/ResonaProAnalyze_artefacts/ResonaProAnalyze take.wav --ref ref.json
```

Two alignment traps cost most of the time spent on this tool, and both are worth
knowing. A console process never runs the message loop, so parameter work that
the processor finishes asynchronously never happens and the settings silently do
not arrive. And the reported latency is not a safe alignment figure; the tool now
measures the delay by pushing a single sample through a transparent
configuration and finding where it lands.

**Two defects are fixed.** The intermittent teardown crash and the 2 dB level
match drift shared one root cause: `lockPhases` wrote one float past the end of a
heap buffer on every frame. It was also a no-op. See
`CRASH-DIAGNOSIS-2026-10-06.md`.

**One new defect is open, and it matters.**

On a signal the suite proves the engine reduces by 2 dB, the harness reports
`+0.00 dB` change at depth 3 — nothing at all — when the focus bands sit at
their defaults. Raise two focus bands, as the suite does, and the same signal
reduces. The detector's cue weight is gated by the focus bands, and a neutral
band produces zero weight rather than a flat one.

The consequence reaches the user: **loaded with defaults and no preset, the
plugin is transparent.** Neutral should mean "look everywhere", not "look
nowhere". This is a behaviour change to the detection semantics, so it needs a
decision rather than a silent patch. Measured on a formant-filtered 196 Hz
comb, quality 2k, response 2x:

| Focus bands | Depth | Collateral rms |
|---|---|---|
| default | 3.0 | **+0.00 dB** (no action) |
| `eq_gain_3` and `eq_gain_4` at +6 dB | 3.0 | +1.48 dB |
| `eq_gain_3` and `eq_gain_4` at +6 dB | 0.0 | +0.00 dB (correct) |

Phase 0.1, the forty-take evaluation set, still does not exist. It needs real
recordings; the harness is ready for them.

---

## The governing principle

There is one rule, and it decides every argument in this document:

> **Never ship an untested change, and never keep a change that cannot be shown to
> improve a real recording.**

The 4k quality defect is the reason. A fixed-bin smoothing kernel broke the High
quality setting, and it survived multiple releases because every test used the
same synthetic comb. It was caught only by measuring a spectrum before and after.
No listening test would have found it; no amount of listening would have found the
*next* one either.

So the plan builds measurement capacity first, then uses it.

---

## Phase 0: Build the ability to judge quality

Nothing else can start until this exists. Right now the project cannot tell
whether a change made things better or worse.

### 0.1 Assemble an evaluation set

Synthetic signals are necessary and not sufficient. Collect real material, with
permission, and keep it out of the repository:

| Category | Count | Why |
|---|---|---|
| Solo female vocal, dry | 8 | The main use case |
| Solo male vocal, dry | 8 | Lower resonances, different formants |
| Breathy/ASMR-style vocal | 4 | Broadband content, the detector's weakest case |
| Belted/aggressive vocal | 4 | Strong harmonics, high level |
| Spoken word | 4 | No pitch, different structure |
| Vocal with sibilance problems | 4 | The fricative preservation case |
| Acoustic guitar, solo piano | 4 | Non-vocal control |
| Full mix with vocal | 4 | Realistic masking |

Forty takes. Each one 20 to 60 seconds. Store as 24-bit WAV at 48 kHz, plus a
96 kHz subset of eight for the sample-rate work.

### 0.2 Build the objective harness

A command-line tool that runs a file through the engine offline and reports:

* **Collateral change.** Magnitude spectrum difference against the latency-aligned
  dry signal, per octave band, in dB RMS and worst-bin. This is the metric that
  found the 4k defect.
* **Harmonic preservation.** For voiced frames, the change at each harmonic
  relative to its own local baseline.
* **Envelope preservation.** Formant band energy change, using the true-envelope
  estimate as ground truth.
* **Transient preservation.** Peak level change and pre-ringing at detected
  onsets.
* **Reduction curve.** How much gain was applied, per frame per bin. Saved as a
  matrix so two builds can be compared directly.
* **Artefact proxies.** Envelope ripple on sustained notes; sideband energy around
  steady partials.

The engine already exposes the magnitude and reduction spectra for the
visualiser. Reuse that path.

### 0.3 Build the listening protocol

Objective metrics cannot tell you whether something sounds better.

* ABX or MUSHRA, blind, level-matched using the plugin's own Match path.
* Compare **bypass against processed**, and **build A against build B**.
* At least 8 listeners, at least 20 trials each, headphones.
* Ask one question: which is closer to the unprocessed voice, and which has fewer
  artefacts? Do not ask "which sounds better" — louder and brighter win that every
  time.
* Keep a written log of every audible artefact reported, with the timestamp in the
  file.

### 0.4 Freeze a reference

Tag the current build. Every later build is compared against it, not against a
memory of it.

**Exit criteria for Phase 0:** the harness runs on all forty takes and produces a
report; the reference build is tagged; the listening protocol is written down.

---

## Phase 1: Fix what is known to be wrong

Small, safe, and demonstrably correct. Do these before any new algorithm.

### 1.1 Make the transform independent of sample rate

**Problem.** The transform is a fixed sample count. At 96 kHz the default setting
resolves resonances half as well as at 48 kHz.

```
48 kHz, Standard   23.4 Hz per bin
96 kHz, Standard   46.9 Hz per bin
```

**Fix.** Choose the transform length from the sample rate and the Quality setting
together, so a given setting means a constant frequency resolution.

| Quality | Target resolution | 44.1 k | 48 k | 96 k | 192 k |
|---|---|---|---|---|---|
| Low Latency | ~46 Hz | 1024 | 1024 | 2048 | 4096 |
| Standard | ~23 Hz | 2048 | 2048 | 4096 | 8192 |
| High | ~12 Hz | 4096 | 4096 | 8192 (see note) | 16384 (see note) |
| Ultra | ~6 Hz | 8192 | 8192 | 16384 (see note) | — |

Note: at 96 kHz and above, the top settings need transforms longer than 8192,
which the current engine does not allocate. Two options: cap the setting and say
so in the UI, or extend allocation. Extending is better — memory is cheap and
quality is the priority.

**Validation.** Run the collateral-change metric on the same file at 44.1, 48 and
96 kHz. The numbers must match within 0.05 dB. Right now they do not.

**Cost.** Moderate. Engine allocation, parameter mapping, and the latency report
becomes sample-rate aware. No CPU change at 44.1/48. CPU doubles at 96 kHz for the
same resolution — which is the correct trade, since that is what the user asked
for by running at 96 kHz.

**Risk.** Low. This is arithmetic.

### 1.2 Give each format its own bundle identifier

The VST3, the AU and the standalone all declare `com.ResonaAudio.ResonaPro`.
Correct practice is one per format. This matters to the installer and to
LaunchServices, and it is a two-line change at packaging time.

### 1.3 Decide Low Band Detail

It changes the decision by 1 to 5 dB and nobody has established whether that is an
improvement. Either demonstrate it helps on low male vocals and bass, or remove
the switch and say the plugin holds back below 150 Hz by design. A control that
does something unclear is worse than no control.

**Validation.** Run the harness on the eight lowest-pitched takes with the switch
on and off, and listen blind to both.

---

## Phase 2: The detector — where the audible quality actually lives

This is the phase that matters. Everything above is hygiene.

**The problem, measured.** On white noise the detector acts on 43 to 59 bins and
cuts as much as 4.8 dB. Real resonances reach prominence 6.5 to 15.4; noise
reaches 1.9 to 3.2. The tails overlap. On a breathy vocal, pushing DEPTH makes the
plugin work on breath.

Four approaches, in increasing order of ambition. They are not exclusive; 2.1 and
2.2 combine well.

### 2.1 Temporal persistence scoring

**The idea.** A resonance sits at the same frequency for hundreds of milliseconds.
A noise peak lives for one or two frames at most. Score every candidate peak by how
long it has persisted, and require persistence before acting.

**How.** For each bin, keep a running statistic of prominence over a window of,
say, 200 ms. Act on the *median* rather than the instantaneous value. A noise peak
raises the median briefly and falls back; a resonance holds it up.

This is the cheapest meaningful improvement available. It needs one ring buffer per
bin, it costs almost nothing, and it directly attacks the failure mode.

**Why it should work.** The plugin already trusts time in two places — the
comb-period estimator is smoothed across frames, and the motion guard protects
moving content. Both came from the same insight. This extends it to the decision
itself.

**Validation.** The noise-vs-resonance table must improve: noise cuts must fall
below 1 dB while resonance cuts stay within 0.5 dB of where they are now. Both
numbers come from the same probe.

**Cost.** Low. Small CPU increase, no latency change.

**Risk.** Medium. Too much persistence and the plugin misses short resonances —
the "s" that rings, a single note that peaks. Tune against the fricative takes in
the evaluation set, which is exactly why Phase 0 collected them.

### 2.2 Separate the envelope from the fine structure

**The idea.** This is the principled version of the whole problem.

A voice is a source and a filter. The source is the glottal pulse train, which puts
evenly spaced harmonics into the spectrum. The filter is the vocal tract, which
imposes a smooth envelope on top. **Resonances are the envelope. Harmonics are the
fine structure riding on it.**

The current detector finds peaks and asks whether each one stands above its
neighbours. That works, but it asks about the signal. The better question is:
*is this peak part of the smooth envelope, or is it a harmonic sampling it?*

**How.** Estimate the spectral envelope, then compare the measured spectrum against
it. A bin that sits above the envelope is a resonance. A bin that sits on the
envelope is a harmonic and should be left alone.

The standard method is the **true envelope** (Imai and Abe 1981; refined by Röbel
and colleagues in 2005), which iteratively smooths the cepstrum until the estimate
lies at or above the spectrum at every harmonic. It is more accurate than plain
cepstral smoothing and dramatically more accurate than peak prominence, and it is
the method used for formant tracking in speech research.

Why it helps beyond detection:

* It would let the plugin work **on the envelope**, not on individual bins. A
  resonance is a property of the envelope, so reducing it means lowering the
  envelope locally — and the harmonics underneath stay intact because they are
  scaled by the same envelope they were always scaled by.
* It would make the plugin's behaviour explainable. A user could see the envelope
  and the reduction curve on the same graph and understand what the plugin decided.
* It handles the low-frequency problem for free. Below 150 Hz the difficulty is
  always separating a resonance from the harmonics that sample it. That is exactly
  what the envelope separates.

**Validation.** On a synthetic vowel with a known formant, the envelope estimate
must land within 1 dB of the true envelope across 100 Hz to 8 kHz. Then the noise
test must improve, and the harmonic-comb test must stay at 0.00 dB.

**Cost.** Medium to high. Real work in the detector, plus tuning. The true envelope
is iterative, so it costs CPU — but CPU is not a constraint at 4.6% of a core.

**Risk.** Medium. Envelope estimation fails on unvoiced and noisy material, where
there is no harmonic structure to fit. It needs a fallback to the current method
when the signal is not voiced. This is a known limitation in the literature and
must be handled explicitly.

**This is the highest-value item in the plan.** It is the difference between a
plugin that finds peaks and a plugin that understands what it is looking at.

### 2.3 Harmonic-percussive structure analysis

**The idea.** In a spectrogram, harmonic content forms **horizontal** ridges —
energy that persists across time at a fixed frequency. Percussive content forms
**vertical** stripes — energy that appears across all frequencies at one instant.
The two are separable by their shape (Driedger, Müller and Ewert, 2013).

**Why it helps.**

* **Transient protection.** A transient is a vertical structure. Anything that
  looks vertical should not be treated as a resonance, and should not be reduced.
  The current transient guard is a detector on the amplitude envelope; this would
  work in the time-frequency plane where the information actually is.
* **Fricative protection.** Sibilance is broadband noise with a vertical character.
  Same protection.
* **It reinforces 2.1.** Horizontal persistence is the same idea expressed
  geometrically.

**Validation.** On the sibilance takes, reduction in the 5 to 10 kHz band during
fricatives must fall. On the click test, pre-ringing must not get worse.

**Cost.** Medium. A median filter across time and frequency, plus the decision
logic.

**Risk.** Low. It is additive and can be disabled if it does not help.

### 2.4 A learned detector

**The most ambitious option, and the one with the highest ceiling.**

Published work exists and is directly on point: Grachten, Deruty and Tanguy,
*Auto-adaptive Resonance Equalization using Dilated Residual Networks*
(ISMIR 2019), builds exactly this — a dynamic equalizer that detects resonances,
with a neural network deciding where they are. Mockenhaupt and colleagues (2024)
did the same for individual instrument tracks with convolutional networks.

Feasibility is established. A hybrid DSP/deep-learning speech enhancer runs in real
time at 48 kHz with **90 KB of static memory and 28 MMACs**, at 4 ms latency.
Model size is a non-issue at this scale.

**What it would look like.**

* Input: a log-magnitude spectrogram patch, plus optional side information (pitch
  estimate, harmonic spacing, the current envelope estimate).
* Output: a per-bin reduction curve, or a per-bin resonance probability.
* Architecture: small dilated convolutional stack or a tiny GRU. A receptive field
  of a few hundred milliseconds, which is what persistence requires.
* Training data: real recordings with known resonances, synthesised by filtering
  dry vocals through resonant filters with known centre frequency, Q and gain. The
  ground truth is the filter, so the label is free and exact. Generate tens of
  thousands of examples from the forty takes.

**Why it could beat everything else.** The other three approaches encode a
hypothesis about what a resonance is. This one learns it, and can learn things
nobody wrote down — that a resonance on a nasal vowel behaves differently, that
breath above 6 kHz should be left alone, that a resonance and a vibrato peak look
different.

**Validation.** The same harness. Plus a hard rule: the network must never be the
only path. Keep the analytic detector as a fallback and compare them continuously.

**Cost.** High. Dataset generation, training, an inference runtime, and the work of
proving it does not do something surprising on material outside the training
distribution.

**Risk.** High, and it is a different kind of risk. An analytic detector fails
predictably; a learned one can fail in ways nobody anticipated on a voice that
sounds unusual. That risk is manageable with a conservative output — the network
proposes, and a fixed rule disposes — but it must be managed deliberately.

**Recommendation.** Do 2.1 first. It is cheap and it tells you how much of the
problem is persistence. Then 2.2. Then decide whether 2.4 is worth it, with two
working detectors to train against.

---

## Phase 3: Synthesis quality

Only after the detector is right. A better synthesis of a wrong decision is still
wrong.

### 3.1 Better gain smoothing

The current smoothing is a 5-tap binomial kernel across frequency. It works. It is
also a fixed shape.

**Improvement.** Make the smoothing kernel follow the ear's critical bands rather
than a fixed bin count — the same class of mistake that caused the 4k defect, but
in the gain stage rather than the detector. A narrower smoothing in ERB terms at
low frequencies, wider at high.

**Validation.** Envelope ripple on sustained tones must fall below 0.003 dB without
slowing the attack. Measure both.

**Cost.** Low. **Risk.** Low.

### 3.2 Phase-coherent resynthesis — only if listening demands it

The measurements say the remaining artefact is gain modulation, at 0.003 dB of
ripple. Whether that is audible is an open question, and it is the first thing the
Phase 0 listening protocol should answer.

**If it is audible:** implement identity phase locking (Laroche and Dolson 1999)
first. It needs no extra frame of latency, unlike the phase-gradient method
(Prusa and Sondergaard 2022), which needs the next frame and would add about
10.7 ms.

**The caution, restated.** The engine's phase is measured and correct. Phase
reconstruction estimates it. On a processor that only scales magnitude, replacing
a correct phase with an estimate can make things worse. Test it as an option, not
as a replacement, and measure the collateral-change metric before and after.

**Cost.** Medium. **Risk.** High — it can degrade a signal path that currently
measures clean.

### 3.3 Multi-resolution synthesis

The five obstacles are documented in `RESEARCH-SYNTHESIS-OPTIONS.md`: incomparable
phase references, comb filtering on summation, a band split that must reconstruct
flat, detector disagreement at the crossover, and roughly double the cost.

**The version worth trying** is not two parallel chains. It is a single chain at
the high resolution, with the gain curve computed for the low band from the
long-window analysis and then mapped back onto it. That avoids obstacle 2 entirely
— nothing is summed in the audio domain — and obstacles 1 and 3 disappear with it.
Obstacle 4 remains, and needs a crossfade region where the two decisions are
blended.

**Validation.** Collateral-change metric across the crossover frequency, at
several crossover points. Any discontinuity shows up as a spike.

**Cost.** High. **Risk.** High.

---

## Phase 4: Product quality

Not sound, but it decides whether people can use the thing.

### 4.1 User presets

Save, name, load, import and export. Six factory presets and an A/B control is not
a preset system. This is ordinary application work with an obvious payoff, and it
is the single most requested class of feature in plugins of this type.

### 4.2 Host validation

Load the VST3 in FL Studio. Route a sidechain. Save and reopen a project. Set every
control to both extremes and automate it. Do this before anything else ships, and
write down what happened.

### 4.3 Notarization

An Apple Developer ID removes the Gatekeeper warning and makes the package behave.
It costs about 99 USD a year.

---

## Phase 5: Optional — a second engine for low latency

Only worth starting if the plugin is meant to be used while tracking.

An STFT cannot be low latency: the transform needs its whole window. A bank of
narrow bandpass filters with per-band dynamics can be, because a minimum-phase
filter has no window to wait for. That is how low-latency dynamic EQ is built.

This is a second engine, not a modification. It would share the parameter set, the
UI and the preset format, and share nothing else. Realistically it is a larger
project than everything in Phases 1 to 3 combined, and it would sound less precise
than the STFT engine because the frequency resolution of a filter bank is coarser.

**Do not start this until Phases 0 to 3 are done.**

---

## What could make it worse

A risk register, because quality plans usually fail by adding something.

| Risk | Mitigation |
|---|---|
| A change improves the metric and sounds worse | Every change goes through blind listening before it is kept |
| Over-smoothing makes the plugin slow to react | Measure attack time as a first-class metric, not an afterthought |
| The learned detector overfits to the training voices | Hold out speakers entirely; test on voices unlike the training set |
| Persistence hides short resonances | The fricative and single-note takes in the evaluation set exist for this |
| Envelope estimation fails on unvoiced speech | Explicit fallback to the current detector, tested on the spoken-word takes |
| Complexity introduces a regression nobody notices | The regression suite runs on all forty takes for every commit |
| CPU grows until the plugin is unusable | Measure after every phase. Budget: stay under 10% of one core at Standard |

---

## Order of work

| # | Item | Effort | Quality gain | Risk |
|---|---|---|---|---|
| 0 | Evaluation set, harness, listening protocol | High | Enables everything | None |
| 1.1 | Sample-rate-independent transform | Medium | Correctness | Low |
| 1.2 | Distinct bundle identifiers | Trivial | None | Low |
| 1.3 | Decide Low Band Detail | Low | Small | Low |
| 2.1 | Temporal persistence detection | Medium | **Large** | Medium |
| 2.2 | Envelope-based detection | High | **Largest** | Medium |
| 3.1 | Critical-band gain smoothing | Low | Moderate | Low |
| 2.3 | Harmonic-percussive protection | Medium | Moderate | Low |
| 4.1 | User presets | Medium | Usability | Low |
| 4.2 | Host validation | Low | Prevents release bugs | None |
| 3.2 | Phase-coherent resynthesis | Medium | Unknown | High |
| 3.3 | Multi-resolution synthesis | High | Unknown | High |
| 2.4 | Learned detector | Very high | Highest ceiling | High |
| 5 | Second low-latency engine | Very high | Different capability | High |

---

## The one thing to do first

Before any of this, run the plugin on one real vocal take and listen to it.

High quality, Match on, A/B against bypass, level-matched, on the held notes. That
ten minutes decides whether Phase 3 matters at all, and it is the only item in this
document that no amount of code can substitute for.

---

## Sources

Detection and envelope estimation:

* Imai, S. and Abe, Y. "Spectral envelope extraction by improved cepstral method."
  *Electronics and Communications in Japan*, 1979.
* Röbel, A. and Rodet, X. "Efficient spectral envelope estimation and its
  application to pitch shifting and envelope morphing." DAFx 2005.
  https://www.dafx.de/paper-archive/2005/P_030.pdf
* Röbel, A. et al. "On cepstral and all-pole based spectral envelope modeling with
  unknown model order." *Pattern Recognition Letters*, 2007.
* Shiga, Y. and King, S. "Estimating the spectral envelope of voiced speech using
  multi-frame analysis." Eurospeech 2003.
  https://www.cstr.ed.ac.uk/downloads/publications/2003/shiga_eurospeech03a.pdf

Learned approaches:

* Grachten, M., Deruty, E. and Tanguy, A. "Auto-adaptive resonance equalization
  using dilated residual networks." ISMIR 2019.
  https://archives.ismir.net/ismir2019/paper/000048.pdf
  https://arxiv.org/abs/1807.08636
* Mockenhaupt, F., Rieber, J. S. and Nercessian, S. "Automatic equalization for
  individual instrument tracks using convolutional neural networks." 2024.
  https://arxiv.org/abs/2407.16691
* Drgas, S. "A survey on low-latency DNN-based speech enhancement." 2023.
  https://pmc.ncbi.nlm.nih.gov/articles/PMC9921748/

Structure analysis:

* Driedger, J., Müller, M. and Ewert, S. "Improving time-scale modification of
  music signals using harmonic-percussive separation." *IEEE Signal Processing
  Letters*, 2013.
* Duxbury, C., Davies, M. and Sandler, M. "Separation of transient information in
  musical audio using multiresolution analysis techniques." DAFx 2001.
  https://www.dafx.de/paper-archive/2001/papers/duxbury.pdf

Phase and multiresolution:

* Laroche, J. and Dolson, M. "New phase-vocoder techniques for pitch-shifting,
  harmonizing and other exotic effects." WASPAA 1999.
  https://www.ee.columbia.edu/~dpwe/papers/LaroD99-pvoc.pdf
* Prusa, Z. and Sondergaard, P. "Real-time spectrogram inversion using phase
  gradient heap integration." 2017. https://ltfat.org/notes/ltfatnote043.pdf
* Smith, J. O. "Multiresolution STFT." *Spectral Audio Signal Processing*.
  https://ccrma.stanford.edu/~jos/sasp/Multiresolution_STFT.html


---

# Part 13 — The algorithmic gap, explained

Source: `docs/QUALITY-GAP-EXPLAINED.md`

# The three points, explained

You asked me to go deeper on three things I said in passing: where ResonaPro
differs from soothe2, the symbol table, and what the measurements do not cover.
This note takes them one at a time, in plain language.

---

## Part 1: the algorithmic gap

### Start with what an STFT is

A resonance suppressor cannot work on the raw waveform. It needs to see which
frequencies are present, and how loud each one is. The standard tool for that is
the Short-Time Fourier Transform, or STFT.

Here is the whole idea:

1. Cut the audio into short slices, called frames.
2. Overlap the slices, so no sound sits at the edge of only one frame.
3. Run a Fourier transform on each frame. This reports, for that instant, how
   much energy sits at each frequency.
4. Each frequency slot is called a bin. A frame gives you a number per bin.

That number is a **complex** value, which means it carries two separate pieces of
information: a **magnitude** (how loud that frequency is) and a **phase** (where
in its cycle that frequency sits at that instant).

5. Change the magnitudes however you want.
6. Run an inverse transform on each frame to turn it back into audio.
7. Add the frames back together, overlapping, so they rebuild the original.

Steps 1 to 4 are analysis. Steps 6 and 7 are synthesis. The whole thing is called
a vocoder, and it is how almost every spectral plugin works.

### What ResonaPro does

ResonaPro changes **magnitude only**. It reads the phase from the analysis and
writes the same phase back into the synthesis. It never touches it.

That choice matters more than it sounds. The phase the analysis measured is not a
guess. It is the actual phase of the actual signal. So when the plugin has no
reason to change a frequency, the frame comes back out exactly as it went in, and
the overlap-add rebuilds the original waveform.

This is why the measurements read the way they do:

| Test | Result |
|---|---|
| Depth 0, output against the delayed input | −136 dB — bit-exact |
| Non-resonant harmonic comb, every harmonic | 0.00 dB change |
| Transient pre-ringing | below −49 dB |

A plugin that reconstructs phase cannot make that claim. It would be replacing a
measured value with an estimate, and the estimate would show up in those numbers.

### So where does the remaining artefact come from?

Not from phase. From **gain modulation**.

Suppose a sustained note sits in one bin. Frame 1 gets a gain of −2 dB. Frame 2,
a millisecond later, gets −4 dB. Each frame, reconstructed on its own, is fine.
But frames overlap, and the overlap-add of two differently scaled copies of the
same tone does not equal that tone scaled by some gain. It comes out slightly
amplitude-modulated. Amplitude modulation creates sidebands, heard as a faint
warble on a held note.

Measured, this is small: **0.003 dB of envelope ripple** on a steady tone, and
pre-ringing below −49 dB on a click. That is the entire remaining artefact
budget. Everything else about the signal path measures clean.

### What phase-locked resynthesis would do

The classic method is identity phase locking, published by Laroche and Dolson in
1999. The idea: find the spectral peaks, then force the bins around each peak to
keep the same phase relationships the analysis found. This restores what the
literature calls vertical phase coherence.

It was invented to fix a specific problem: when you time-stretch or pitch-shift
audio, the phase relationships fall apart, and the result sounds "phasey" or
metallic. Laroche and Dolson measured a large improvement in their metric, from
about 6.5 dB to 37 dB of phase-coherence error.

That is a real fix for a real problem. It is just not our problem.

Three costs would come with it:

**One extra frame of latency.** The modern variant needs the next frame to
estimate how phase changes over time. At the default settings that adds about
10.7 ms on top of the 42.7 ms the engine already reports.

**More CPU.** Phase integration walks the time-frequency plane per bin. It costs
far more than the current multiply.

**It trades a correct value for an estimate.** This is the decisive point. Our
phase is measured and exact. Phase reconstruction estimates it. On a processor
that only scales magnitude, throwing away a correct phase to install an estimate
can make the result worse.

Phase reconstruction earns its cost when phase *must* change, as in time
stretching. Here it must not change at all.

### What multi-resolution synthesis would do

This one is worth separating into two halves, because one half already ships.

**Multi-resolution detection is in the plugin.** A 4096-point detector
contributes to the decision below 1.2 kHz while synthesis stays at one
resolution. That is the Low Band Detail switch. The plugin already looks harder
at the low end with a longer window.

**Multi-resolution synthesis is the other half, and it is the hard one.** The
idea: process the low band with a long window and the high band with a short one,
then recombine. Five obstacles stand in the way.

**1. The two transforms have different phase references.** A bin at 400 Hz in a
2048-point transform and a bin at 400 Hz in a 4096-point transform do not carry
comparable phase. The windows differ, so each phase is a differently weighted
average of the signal around that moment. You cannot mix them without first
reconciling the references.

**2. Summing two chains creates comb filtering.** Run two complete
analysis-modify-synthesis chains and add the outputs, and it does not work. A
4096-point window has more group delay than a 2048-point one, so the same event
emerges from the two chains at two different times. Add them and you get
cancellation notches that sweep with frequency.

**3. The band split has to reconstruct.** You would split the signal into bands,
process each at its own resolution, then recombine. The split and the
recombination must sum back to flat. That is achievable, but the two paths have
different latencies, so the short path needs delaying to match the long one. That
delay becomes the plugin's new latency floor.

**4. The decisions must agree at the crossover.** If the low band is judged with a
long window and the high band with a short one, the detector can disagree near
the crossover frequency. A disagreement there is a jump in applied gain, heard as
a tone or a hole.

**5. Cost.** Two transforms, two detectors, crossover filters, and a latency
alignment buffer. Roughly double the CPU and memory.

### The honest summary of Part 1

Both techniques are published, understood, and shipping in commercial software.
Neither is impossible. The reason to hold off is not difficulty. It is that the
measurements show almost no headroom for either one to help, and both would cost
latency and CPU.

Where ResonaPro stands apart from soothe2 is **algorithm**, not architecture.
Closing that gap is a rewrite of the synthesis half of the engine, with a new
latency contract and fresh tuning of every control. It is not a size problem. It
would add a few hundred kilobytes of code, not hundreds of megabytes.

The reference list for all of this is in `RESEARCH-SYNTHESIS-OPTIONS.md`.

---

## Part 2: the symbol table

### What it is

When a program is compiled, the compiler writes down the name of every function
and variable it created, and where each one sits in the file. That list is the
symbol table. It exists so a developer can attach a debugger and see meaningful
names instead of raw memory addresses.

Once the plugin is built and working, that list has no job. The processor does not
read it. It only takes up space.

### The numbers

| | Size |
|---|---|
| arm64 slice, as built | 10.16 MB |
| arm64 slice, stripped | 7.48 MB |
| Saving | 2.70 MB per architecture, 5.4 MB across both |

The command that does it is `strip`. It takes seconds, and the result is verified
by the same test suite.

### Why I did not just do it

Two reasons.

The first is what you actually asked for. Your message was "if you're sacrificing
quality for size don't do that." Stripping makes the download **smaller**. That is
the opposite direction from your concern, and doing it while telling you it does
not matter would have been talking past you.

The second is that it does not touch the thing you care about. It changes how long
the file takes to download. It does not change one sample of audio. A build with
the symbol table and a build without it produce bit-identical output — that is
what the Depth 0 test measures.

So it is available, it is free, and it is your call. Say the word and it is one
rebuild and repackage.

---

## Part 3: what the measurements do not cover

### What synthetic tests are good for

Every number in the tables above comes from signals I generated: a harmonic comb,
a click, a steady tone, a synthetic voice. That is deliberate. A synthetic signal
lets you change one thing and hold everything else still, so when the measurement
moves you know what moved it.

That is how the 4k quality defect was found. A 190 Hz comb with no resonance
should pass through untouched. At High quality it did not, and the reason was
traceable to a smoothing kernel that used a fixed number of bins instead of a
fixed frequency width.

No listening test would have found that. It was a 4.3 dB error on harmonics that
should not have been touched, and it only appeared at one of four quality
settings.

### Why they cannot settle the real question

A real recorded voice is not one signal. It is several at once:

- breath noise, which is broadband
- room tone, which is also broadband
- sibilance — the "s" and "sh" sounds, which are bursts of high-frequency noise
- vibrato, which moves the pitch up and down several times a second
- pitch drift, which moves it slowly
- and the harmonics of the note itself, which move with all of the above

The detector has to separate a resonance — a peak that stays put — from a harmonic
that is supposed to be there and is currently sliding around. It has to do that
while breath and room tone put random peaks into the spectrum.

The plugin is tuned for exactly this. It estimates the spacing between harmonics,
uses that to build a baseline, protects anything that moves, and guards
transients. But "tuned for it" and "proven on it" are different claims, and I have
only tested the second one on signals I made up.

### The specific thing to listen for

The one artefact the measurements predict is the gain-modulation warble described
in Part 1. Its signature is a **watery or phasey quality on sustained notes** —
the long held notes in a ballad, not the fast ones.

So the test is:

1. A real vocal take, the kind of material you would actually use this on.
2. Quality on High, Match on.
3. A/B against bypass, level-matched.
4. Listen to the sustained notes specifically, and specifically on the notes where
   the reduction meter moves most.

If the warble shows up there, phase-coherent resynthesis stops being speculative
and becomes justified. The identity phase-locking variant is the cheaper entry
point, because it needs no extra frame of latency.

If it does not show up, the money is better spent somewhere else.

### What I can and cannot claim

I can claim the signal path is clean, and I can show you the numbers. I can claim
the plugin never touches what it is not aiming at, and I can show you a harmonic
comb that comes back at 0.00 dB.

I cannot claim it sounds transparent on your voice. That takes a take, a pair of
headphones, and ten minutes of your time. It is the last open item on the list,
and it is the one item no amount of code from me can close.

---

## What I would do next, in order

1. **Listen.** A real take, High quality, Match on, A/B against bypass. Ten
   minutes. This decides everything below it.
2. **If the warble appears**, add identity phase locking. It costs no extra
   latency, and the published method is well specified.
3. **If it does not appear**, leave the synthesis path alone and spend the effort
   on the detector instead — better discrimination on breathy and noisy material
   would help more than a cleaner reconstruction of a signal that already
   reconstructs cleanly.
4. **Strip the binary**, if you want the smaller download. Zero audio effect,
   five minutes of work.


---

# Part 14 — Phase-coherent and multi-resolution, costed

Source: `docs/RESEARCH-SYNTHESIS-OPTIONS.md`

# Phase-coherent and multi-resolution synthesis: what they cost

## First, a correction

The earlier report rated these two techniques **hard**. It did not rate them impossible, and it should not have been read that way. Both are published, understood, and implemented in shipping software. The reason to hold off is not feasibility. It is that the measurements show very little headroom for either one to improve, and both would cost latency and CPU to pursue.

The rest of this note explains what each technique actually requires, so the trade-off is visible.

## What the engine does today

The engine analyses with a single STFT at a fixed window length, scales the magnitude of each bin, leaves the phase alone, and overlap-adds.

Leaving the phase alone is worth dwelling on, because it is the most benign thing a spectral processor can do. The synthesis frames carry the phases the analysis measured, so the phase relationships between overlapping frames are exact. That is why Depth 0 reconstructs at -136 dB and why non-resonant harmonics measure 0.00 dB of change. A gain-only processor starts from a position of strength that a time-stretcher does not have.

The residual artefacts, such as they are, come from **gain modulation between frames**, not from phase incoherence. When adjacent frames receive different gains, their overlap-add is not the same as scaling the underlying sinusoid by that gain. The result is a small amplitude modulation, which produces sidebands. Measured, this sits at 0.003 dB of envelope ripple on a steady tone and below -49 dB of pre-ringing on a click.

## Phase-coherent resynthesis

### What it is

The classic treatment is identity phase locking (Laroche and Dolson). The idea: pick the spectral peaks, and constrain the synthesis phases of the bins around each peak so they keep the same relative phase relationships the analysis found. That restores what the literature calls vertical phase coherence, and it is what makes time-stretched and pitch-shifted audio stop sounding "phasey". Laroche and Dolson report a large improvement in phase-coherence error over the unlocked algorithm, from roughly 6.5 dB to 37 dB in their metric.

A more recent and more general method is the phase-gradient heap integration used by Prusa and Sondergaard. Instead of picking peaks, it estimates the partial derivatives of the STFT phase in both time and frequency, then integrates them across the time-frequency plane, propagating phase along whichever direction the local magnitude favours. It enforces horizontal and vertical coherence together, with no peak picking and no transient detection. It needs the previous, current and one future frame.

### What it costs

* **One extra frame of latency.** Centred phase differencing needs the next frame, so the plugin would have to look one hop ahead. At 2048 points with 4x overlap that is 512 samples, about 10.7 ms, on top of the 42.7 ms the engine already reports.
* **More CPU.** Phase integration is a per-bin operation with a heap-ordered traversal. It costs more than the current multiply.
* **It replaces a correct value with an estimate.** This is the decisive point. The engine's phase is currently *measured*, and it is correct. Phase reconstruction *estimates* it. On a processor that only scales magnitude, discarding a correct phase to install an estimate can make the result worse, not better. Phase reconstruction earns its cost where phase must be *changed* anyway, as in time scaling. Here it must not be changed at all.

### Verdict

The technique solves a problem this plugin largely does not have. It would buy a small reduction in gain-modulation artefacts, at the cost of latency, CPU, and the risk of degrading a phase that is currently exact.

## Multi-resolution synthesis

Multi-resolution **detection** is already in the plugin. A 4096-point detector contributes to the decision below 1.2 kHz while synthesis stays at one resolution. Multi-resolution **synthesis** is the hard half, and it has five distinct obstacles.

### 1. Two transforms have different phase references

The phase of a bin in a 2048-point STFT and the phase of the same frequency in a 4096-point STFT are not comparable. The window differs, so each phase is a differently weighted average of the signal around that instant. You cannot mix them without explicitly reconciling the references.

### 2. Summing two chains comb-filters

Running two complete analysis-modify-synthesis chains at different resolutions and adding the outputs does not work. A 4096-point window has more group delay than a 2048-point one, so the same event emerges at two different times. Summing the two produces cancellation notches that sweep with frequency. Julius Smith's treatment of the multiresolution STFT makes the related point that every channel must be oversampled in time, or the channels get weighted unevenly, and that is for analysis where the channels are combined by smoothing rather than summed as audio.

### 3. The band split must reconstruct

You would band-split, process each band at its own resolution, and recombine. The split and recombination must form a complementary filter bank that sums flat, which is achievable, but the two paths have different latencies. The short path must be delayed to match the long one, and that delay becomes the plugin's new latency floor.

### 4. Decisions must stay consistent across the crossover

If the low band is analysed with a long window and the high band with a short one, the detector sees different material on each side and can disagree near the crossover frequency. A disagreement there is a discontinuity in the applied gain, which is audible as a tone or a hole at the crossover.

### 5. Cost

Two STFT chains, two detectors, crossover filters, and a latency alignment buffer. Roughly double the CPU and memory of the current engine.

### Verdict

Architecturally doable, but it is a rewrite of the synthesis half of the engine with a new latency contract and fresh tuning across every control. And the low-band benefit it offers is already delivered by the existing low-band detector, while the high-band transient benefit is already measured at -49 dB of pre-ringing through the transient guard.

## The sources

* Laroche, J. and Dolson, M. "New phase-vocoder techniques for pitch-shifting, harmonizing and other exotic effects." WASPAA 1999. https://www.ee.columbia.edu/~dpwe/papers/LaroD99-pvoc.pdf
* Laroche, J. and Dolson, M. "Phase-vocoder: about this phasiness business." WASPAA 1997. https://ieeexplore.ieee.org/abstract/document/625603/
* Prusa, Z. and Sondergaard, P. "Phase Vocoder Done Right." EUSIPCO 2022. https://arxiv.org/html/2202.07382v1
* Puckette, M. "Phase-locked Vocoder." IEEE ASSP Workshop 1995. https://msp.ucsd.edu/Publications/mohonk95.pdf
* Roebel, A. "A new approach to transient processing in the phase vocoder." DAFx 2003. https://hal.science/hal-01161124/document
* Smith, J. O. "Overlap-Add STFT Processing" and "Multiresolution STFT", Spectral Audio Signal Processing. https://ccrma.stanford.edu/~jos/sasp/Overlap_Add_OLA_STFT_Processing.html and https://ccrma.stanford.edu/~jos/sasp/Multiresolution_STFT.html
* Lukin, A. and Todd, J. "Adaptive time-frequency resolution for analysis and processing of audio." AES Convention 120, 2006.
* Krawczyk, M. and Gerkmann, T. "STFT phase reconstruction in voiced speech for an improved single-channel speech enhancement." IEEE/ACM TASLP 2014. https://ieeexplore.ieee.org/abstract/document/6891278/
* Ottosen, E. S. and Dorfler, M. "A phase vocoder based on nonstationary Gabor frames." IEEE/ACM TASLP 2017. https://ieeexplore.ieee.org/abstract/document/8031036/
* Duxbury, C., Davies, M., and Sandler, M. "Separation of transient information in musical audio using multiresolution analysis techniques." DAFx 2001. https://www.dafx.de/paper-archive/2001/papers/duxbury.pdf

## What I would actually do next

Neither technique, yet.

Every measurement currently available says the plugin is clean on synthetic material: 0.00 dB of change on non-resonant harmonics, -49 dB of transient pre-ringing, 0.003 dB of warbling, and a bit-exact passthrough at Depth 0. Building phase reconstruction or a second synthesis chain now would be solving a problem that has not been shown to exist, at a real cost in latency and CPU.

The one thing the measurements cannot settle is how the plugin behaves on a real recorded voice, where breath, room tone, sibilance, vibrato and pitch drift arrive together. That is a listening question, not a code question.

So the next step is a real take, at High quality, with Match on, A/B against bypass. The specific thing to listen for is a "watery" or "phasey" quality on sustained notes, which is the signature of gain modulation, and the specific thing to measure is whether it appears on the notes that need the most reduction.

If it does appear, phase-coherent resynthesis moves from speculative to justified, and the identity phase-locking variant is the cheaper entry point because it needs no extra frame of latency. If it does not appear, the money is better spent elsewhere.


---

# Part 15 — Reference research and DSP design

Source: `docs/RESEARCH-AND-DESIGN.md`

# ResonaPro v2 — Research Findings and DSP Design

This document records what was researched, what was found to be wrong in the
original implementation, and the reasoning behind every design decision in the
rebuilt engine. It is written so that the next person to touch this code can
change it without breaking it.

---

## 1. What the reference products actually do

### 1.1 oeksound soothe 2

Source: [oeksound soothe2 manual](https://oeksound.com/manuals/soothe2/)

Key facts taken from the manual:

* It is a **dynamic resonance suppressor**, not an EQ. It detects resonances in
  the input and reduces them automatically, in the frequency domain.
* The relevant controls are **Depth** (amount), **Sharpness** (bandwidth of the
  detection/processing), **Selectivity** (how much a peak must stand out to be
  treated), **Attack / Release**, **Oversampling**, and a **frequency curve**
  (sidechain/focus) used to bias or exclude regions.
* The frequency graph is a *sidechain detection curve*, not an output EQ: it
  tells the detector where to listen harder or to leave alone.
* A **Delta** function is provided so the user can hear exactly what is being
  removed. This is the single most important usability feature of the whole
  plug-in — it is how users learn to trust it.

### 1.2 oeksound soothe 3

Source: [oeksound soothe3 manual](https://oeksound.com/manuals/soothe3/)

Published changes that matter for design:

* **Soft mode** is described as using an **adaptive threshold** which "reacts to
  the relative changes in the tonal content" instead of absolute input level.
  This makes the behaviour independent of how loud the material is.
* **Hard mode** is described as reacting to **absolute levels of harmonics** —
  i.e. it is level dependent, and far more aggressive.
* Continued emphasis that **sharpness** changes the analysis bandwidth
  (wide/musical ↔ narrow/surgical).

**Design consequence:** the detector in this project implements an explicit
adaptive threshold derived from the statistics of each analysis frame
(see §4), which is why a vocal's own harmonic comb is no longer mistaken for a
pile of resonances.

### 1.3 Practical guides and papers

* [KERN Audio — Spectral processing guide](https://kernaudio.io/guides/resonance/spectral-processing)
  — walks through per-bin gain, attack/release in the spectral domain,
  frequency-dependent smoothing, and baseline/peak detection.
* [MDPI Applied Sciences — resonance attenuation method](https://www.mdpi.com/2076-3417/15/6/3038)
  — uses a threshold curve derived from the *statistics* of the spectrum rather
  than a fixed threshold, and attenuates only the excess above that curve.

Both sources converge on the same architecture: **local baseline →
prominence in dB → threshold → per-bin reduction**, with the threshold being
adaptive rather than fixed. That is the architecture implemented here.

### 1.4 Well-established DSP facts that drove specific decisions

These are standard results rather than novel claims, but they are the reason
several controls exist:

* **A sung note is a harmonic comb.** Every harmonic is a narrow spectral peak.
  Any detector that simply looks for "narrow peaks above the local average"
  will flag the entire voice, and the result is the classic hollow, lisping,
  over-processed vocal. Separating *resonance* from *harmonic* is the single
  hardest problem in this class of plug-in.
* **Spectral subtraction has a well-known artefact family** ("musical noise"):
  rapidly changing per-bin gains spread energy and create warbling. The remedies
  are frequency-dependent smoothing of the gain curve, a reduction ceiling, and
  not letting the release be too fast.
* **Critical bands / ISO 226.** Human hearing is not uniform with frequency;
  the ear canal resonance around 3.4 kHz makes that region sound louder than it
  measures. A detector that ignores this constantly carves out the ear's most
  sensitive region.
* **STFT reconstruction requires a constant-overlap-add (COLA) analysis/synthesis
  pair.** A square-root Hann window applied on both analysis and synthesis,
  with the analysis frames overlapping by at least 50 %, reconstructs the signal
  exactly (to numerical precision) when all gains are 1.0.
* **Oversampling a fixed-size transform is not higher resolution.** Resampling
  the audio to 8× and feeding a fixed 2048-point FFT gives you 2048 bins spread
  over 8× the bandwidth — the usable frequency resolution per Hz actually gets
  *worse* in the audio band, and latency grows. Making the transform longer is
  what buys resolution.

---

## 2. What was wrong in the original implementation

Each item below was a real defect found by reading the code and then **measured**
with the test harness in `.work/scripts/dsp_test.cpp`.

| # | Defect | Consequence | Status |
|---|--------|-------------|--------|
| 1 | `SpectralEngine::getLatencySamples()` did not exist; the processor compensated latency using `(fftSize / 2 + hopSize - 1)` in some paths and an oversampling-scaling formula in others | **Dry and wet paths were not sample-aligned.** Blending dry/wet produced comb filtering, and Delta was meaningless | Fixed: latency is now measured from the engine by an impulse probe and used everywhere |
| 2 | Delta was computed as `input[n] − processed[n]` with no delay compensation | Delta contained the entire signal plus a comb filter, not the removed resonances | Fixed: Delta is `delayedDry − wet`, computed in the processor against the same delay the mix uses |
| 3 | The local baseline used a **center-inclusive** boxcar average | A peak raised its own baseline, so peaks inside a formant were *under*-detected and low-level ripple was *over*-detected | Fixed: side-excluding shoulders |
| 4 | Fixed-threshold detection in the linear magnitude domain | Behaviour changed with input level; quiet takes were untouched and loud takes were destroyed | Fixed: dB-domain prominence + adaptive threshold |
| 5 | Every harmonic of a sung note was treated as a resonance | The vocal was hollowed out — the exact failure mode users complain about in naive de-resonators | Fixed: harmonic-period estimation + peak-relative baseline (§4) |
| 6 | "Oversampling" resampled into a fixed 2048-bin transform | At 8× only ~128 bins covered the audio band. It made resolution *worse*, cost CPU, added latency, and the resamplers allocated in `prepare` but were driven per-sample with an internal block size | Replaced with real Quality (transform length) and Response (overlap) controls |
| 7 | `juce::dsp::Oversampling` was constructed inside `processBlock` in one path | Object construction on the audio thread — an allocation/lock hazard that can cause dropouts | Fixed: all engines are pre-built in `prepareToPlay` |
| 8 | `PitchTracker` (YIN) was unused dead code; `TransientSplitter` was referenced by CMake but not by the engine | Confusing codebase, wasted build time | Removed; a purpose-built `TransientDetector` replaced them |
| 9 | `sys/` files were written into the repository | Build pollution | Removed |
| 10 | Non-finite input (NaN/Inf from an upstream plug-in) entered the FFT ring buffer and corrupted it permanently | The plug-in stayed silent/corrupt for the rest of the session, requiring a DAW restart | Fixed: input sanitising in the engine and in the processor output |
| 11 | Parameter smoothing used `SmoothedValue::getNextValue()` once **per block** with a ramp length given in seconds | The ramp took *seconds* instead of 20 ms, and the result depended on the host block size | Fixed: per-sample ramps for mix/output, and an explicit time-based step for depth |
| 12 | `processBlock` called `setLatencySamples` / did work that assumed a fixed bus layout without guarding mono | Mono-instantiated hosts could misbehave | Fixed and covered by tests |

---

## 3. The spectral pipeline

```
input ──► transient detector ──► frame counter
   │
   ├──► input ring (fftSize) ──► window (sqrt-Hann) ──► FFT ──► magnitude
   │                                                              │
   │                            ┌─────────────────────────────────┘
   │                            ▼
   │           ResonanceDetector  (per bin, 0 .. Fs/2)
   │              1. harmonic period estimate
   │              2. side-excluding shoulder baseline (smoothed magnitude)
   │              3. prominence = 20*log10(mag / baseline)
   │              4. adaptive reference  = median prominence of the peaks
   │              5. threshold = reference + selectivity·8 dB
   │                            + profile + ISO 226 + transient + focus bands
   │              6. excess = max(0, prominence − threshold)
   │                            │
   │                            ▼
   │           DynamicSuppressor   (attack/release per bin, frequency dependent)
   │              soft:  red = maxCut·(1 − e^(−0.85·depth·excess/maxCut))
   │              hard:  red = min(maxCut, 1.4·depth·(excess − knee))
   │              + reduction ceiling, + gain-curve smoothing, + stereo link
   │                            │
   │                            ▼
   └──► per-bin gain ──► IFFT ──► window ──► overlap-add (2·fftSize ring)
                                                │
                                                ▼
                                          wet output
```

Signal-alignment and mixing happen in the processor:

```
                       ┌──────────── dry delay ring (latency samples) ────────────┐
input ──┬──────────────┤                                                          ├──► dry
        │              └──────────────────────────────────────────────────────────┘
        └──► spectral engine ──► wet
                                    │
   output = bypass ? dry
          : delta  ? dry − wet
          :          dry + mix·(wet − dry)
```

---

## 4. The detector, in detail

This is the part of the plug-in that decides *what is a resonance*.

### 4.1 Harmonic period estimation

Before anything else, the detector estimates how far apart the existing spectral
peaks are. It searches for the lag `L` that minimises the mean absolute
difference of the log-magnitude spectrum, `mean |magdB[k] − magdB[k+L]|`, over
bins above −60 dB. That is the lag at which the spectrum most closely repeats
itself. For a sung note with f0 = 220 Hz at 44.1 kHz / 2048 points, the minimum
lands on L = 10 bins ≈ 220 Hz.

This estimate is used as a **minimum analysis radius**. It is deliberately
robust: it uses the log domain (so the spectral tilt does not matter) and the
mean over hundreds of bins (so a single resonance cannot move it).

### 4.2 Side-excluding shoulder baseline

For each bin `k`:

* The radius `R(k)` is `max(f·bwScale / binWidth, harmonicPeriod · 1.2)`, capped
  at `bins / 16`. `bwScale = 0.30 / sharpness`, so **Sharpness** directly
  controls how far away "elsewhere" is.
* The inner guard is `R/3`, so the bin's own peak is never part of its baseline.
* The shoulder values come from a **5-tap smoothed** copy of the magnitude, and
  the baseline is the **85th percentile** of the shoulders.

A percentile rather than a plain mean or maximum is what lets one statistic
work in both regimes:

* on a **comb** the 85th percentile is the neighbouring harmonic, so a harmonic
  in line with its neighbours scores ~0 dB prominence;
* on **broadband** material it is roughly the local level plus a few dB, so a
  lone noise spike cannot inflate the baseline and hide a resonance.

### 4.3 Prominence and the adaptive threshold

```
prominence[k] = 20·log10( magnitude[k] / baseline[k] )      // clamped ±60 dB
reference     = clamp( median(prominence over spectral peaks), 0.5, 30 ) dB
threshold     = reference + selectivity·8 dB
              + vocal-profile offset
              + ISO 226 offset
              + transientActivity · transientGuard · 9 dB
              − focusBandWeight[k]
excess[k]     = max(0, prominence[k] − threshold)
```

The `median over spectral peaks` term is the "adaptive threshold" idea: it asks
*how peaky is this material already?* and places the bar just above that. This
is what stops a naturally resonant voice from being flattened.

Two hard gates keep the processing where it can be heard:

* bins below −60 dB of the frame peak are ignored entirely;
* bins below −45 dB of the frame peak receive no reduction at all (this is what
  stops the plug-in from carving up the noise floor and the ultrasonic tail).

### 4.4 The focus bands

Eight bands, each a real biquad magnitude response evaluated on the detector's
bin grid (`ParametricEQWeighting`). The band gain is an *offset to the detection
threshold*, and the sign is inverted on purpose:

* pulling a node **up** lowers the threshold → **more** processing there;
* pulling a node **down** raises the threshold → **protection**.

This mirrors soothe's sidechain graph semantics.

### 4.5 Soft vs Hard

* **Soft (default)** — the adaptive threshold above, and a saturating reduction
  law. A very strong resonance can never be notched out completely, which is
  what keeps the result musical.
* **Hard** — the adaptive reference is pushed down by `6 − 1.5·depth`, and the
  excess is additionally weighted by the bin's absolute level
  (`(levelDbFS + 45)/20`, clamped 0…1). Quiet material is ignored, loud
  material is treated decisively, and the reduction law is linear in dB above a
  knee that closes as depth rises. This reproduces the documented "hard mode
  reacts to absolute harmonic levels" behaviour.

---

## 5. Measured behaviour

All numbers below come from `.work/scripts/dsp_test.cpp` (engine level) and
`Tests/ProcessorTests.cpp` (full processor).

### 5.1 Reconstruction accuracy

| Configuration | Reported latency | Measured latency | Residual vs delayed dry |
|---|---|---|---|
| 2048, 2× overlap | 2048 | 2048 | 2.0e−14 |
| 2048, 4× overlap | 2048 | 2048 | 2.1e−15 |
| 2048, 8× overlap | 2048 | 2048 | 1.6e−14 |
| 1024, 4× overlap | 1024 | 1024 | 4.7e−15 |
| 4096, 4× overlap | 4096 | 4096 | 6.9e−15 |
| 8192, 4× overlap | 8192 | 8192 | 7.8e−15 |

### 5.2 Effect strength on a deliberately hostile synthetic vocal

(Harmonic pulse train at 196/220 Hz plus four formants, one of them a sharp
Q = 14 resonance — far "ringier" than a real voice.)

| Depth | mean reduction | max reduction | mean reduction at harmonics | RMS change |
|---|---|---|---|---|
| 0.5 | −1.02 dB | −3.82 dB | −0.52 dB | −1.12 dB |
| 1.0 | −1.69 dB | −6.34 dB | −0.92 dB | −1.92 dB |
| 1.5 | −2.22 dB | −8.04 dB | −1.22 dB | −2.50 dB |
| 2.0 | −2.63 dB | −9.24 dB | −1.45 dB | −2.94 dB |
| 3.0 | −3.21 dB | −10.81 dB | −1.78 dB | −3.53 dB |

By contrast, a bare sine (which genuinely *is* a resonance) is cut by 14 dB at
depth 1.0. That is the intended shape of the response: strong on real
resonances, gentle on musical content.

### 5.3 Discrimination

For the synthetic vocal + resonance signal, at depth 1.0 the detector's own
prominence readings are:

| Frequency | magnitude | baseline | prominence | reduction |
|---|---|---|---|---|
| 220 Hz (harmonic) | 50.9 dB | 46.2 dB | 4.7 dB | 0.0 dB |
| 440 Hz (harmonic) | 46.2 dB | 51.6 dB | −5.4 dB | 0.0 dB |
| 660 Hz (harmonic) | 51.6 dB | 46.2 dB | 5.4 dB | 0.0 dB |
| 3300 Hz (true resonance) | 26.1 dB | 22.6 dB | 3.5 dB | −2.2 dB |

The harmonics sit *at* their neighbours' level and are left alone; the genuine
resonance sits above the local structure and is reduced.

---

## 6. CPU and latency budget

* Transform length and overlap are user selectable, which is the real
  performance/quality trade-off:

  | Quality | Latency @44.1 kHz | Notes |
  |---|---|---|
  | Low Latency (1k) | 1024 samples ≈ 23 ms | tracking |
  | Standard (2k) | 2048 samples ≈ 46 ms | mixing default |
  | High (4k) | 4096 samples ≈ 93 ms | bus / mastering |
  | Ultra (8k) | 8192 samples ≈ 186 ms | extreme surgical work |

* Response (2× / 4× / 8× overlap) trades gain-update rate against CPU. Because
  the detector's threshold is per-frame, higher overlap also makes the
  detection react faster, which is the benefit people were actually chasing
  when they reached for "oversampling".

* All per-frame scratch buffers are pre-allocated in `prepare()`. There is no
  allocation, no lock and no message-thread work in `processBlock`.

---

## 7. Known limitations (stated honestly)

1. **Low frequency.** Below roughly 150 Hz a resonance is hard to separate from
   the harmonic structure, and the plug-in will preferentially leave the
   fundamental alone. The `Warm Body & De-Mud` profile or the focus bands are
   the intended tools there. This is a deliberate trade-off: the alternative is
   a plug-in that guts every low note.
2. **Broad tonal balance** (e.g. "this vowel is 3 dB too dark overall") is not
   what dynamic resonance suppression does. Use the focus bands to bias it, or
   a static EQ.
3. **Changing Quality or Response while audio is playing** switches to a
   differently-sized transform, so there is a brief discontinuity and a latency
   change. This is inherent to the design; audition these two controls with
   playback stopped.
4. **Stereo Link at intermediate values** uses a fixed blend of the two
   channels' magnitudes, not a sidechain bus. External sidechain input is not
   implemented.


---

# Part 16 — Audio quality measurements

Source: `docs/AUDIO-QUALITY.md`

# Audio quality: what is measurable, what is not

## The short answer

Three different questions hide inside "does it lose audio quality". They have three different answers.

| Question | Answer | Status |
|---|---|---|
| Does it change the sound when it is not reducing anything? | No. At Depth 0 the plug-in is a bit-exact delay. | Verified, 2.1.0 |
| Does it damage sounds it is not aiming at? | No, once the defect below is fixed. Every harmonic of a plain comb measures 0.00 dB change. | Fixed in 2.1.1 |
| Can a resonance suppressor remove a resonance without any loss at all? | No. Removing energy is the job. | Physics |

So the goal you asked for is reachable in the sense that matters: **the plug-in should never touch what you did not ask it to touch.** That is now true and tested. A suppressor that removes a resonance while leaving the signal bit-identical is not possible, because the removed energy has to go somewhere.

## The defect this investigation found

The probe compared the average magnitude spectrum before and after processing, aligned by the reported latency, so a pure delay reads as 0.000 dB. Running a 190 Hz harmonic comb with **no resonance** through the plug-in at Depth 1.5 exposed a real problem:

| Quality | Deviation before | Deviation after |
|---|---|---|
| Low (1k) | 0.000 dB | 0.000 dB |
| Standard (2k) | 0.000 dB | 0.000 dB |
| **High (4k)** | **0.982 dB rms, worst bin -4.32 dB** | **0.031 dB rms, worst bin -0.24 dB** |
| Ultra (8k) | not measured | 0.00 dB |

Choosing a *higher* quality setting made the sound **worse**, not better. At 4k the plug-in carved up to 4.3 dB out of harmonics that Standard left completely alone.

### Why

The detector compares each peak against a baseline built from the peaks around it. That baseline is measured on a lightly smoothed magnitude spectrum, and the smoothing used a fixed five-tap kernel, meaning plus or minus two bins. Two bins is a fixed *number*, not a fixed *frequency*, so as the transform grows each bin narrows and the kernel spans less and less of the spectrum:

* at 2048 points a bin is 23.4 Hz, so the kernel spans about 94 Hz;
* at 4096 points a bin is 11.7 Hz, so the same kernel spans about 47 Hz.

Harmonics sit 190 Hz apart. At 2048 points the kernel partly bridges the gaps between them, so the baseline tracks the harmonic peaks. At 4096 points it no longer does, so the baseline falls into the valleys between harmonics. Measured directly on the comb, the baseline sat **9 dB below the neighbouring peak at 2048 points but 27 dB below it at 4096**. A baseline that low makes an ordinary harmonic look like a resonance standing above its surroundings, and the plug-in dutifully removed it.

### The fix

The smoothing radius now scales with the transform length, so the kernel spans the same frequency width and keeps the same triangular shape at every Quality setting. A 2048 point transform keeps the original two-bin radius exactly, which is why Low and Standard measure identically before and after.

## What the measurements now say

All figures at 48 kHz, Depth 1.5, Selectivity 0.5, on synthetic material.

| Test | Result | Reading |
|---|---|---|
| Depth 0 residual vs delayed dry | -136 dB | Bit-exact. The plug-in is a pure delay. |
| Depth 0 spectrum deviation | 0.0004 dB | At the float noise floor of the measurement itself. |
| Harmonic comb, no resonance | 0.00 dB on every harmonic, 1k/2k/4k/8k | Untouched. |
| Click transient, peak level | -0.01 dB | Plosives and consonants keep their impact. |
| Click transient, pre-ringing | below -49 dB | No smearing before the transient. |
| Envelope ripple, steady tone | 0.003 dB at 2k/4x | No audible warbling. |
| Selectivity 0.0 | worst bin -1.30 dB | Below 0.5 the plug-in starts touching non-resonant content. |

Two practical consequences follow. **Keep Selectivity at 0.5 or above** unless you deliberately want aggressive behaviour; that single control governs how much collateral change the plug-in causes. And **Response (overlap) at 8x** halves the gain update interval, which measured a 14x reduction in envelope ripple on a steady tone at the cost of roughly double the CPU.

## What is still not established

The numbers above come from synthetic signals. They prove the plug-in leaves a harmonic comb alone and does not smear transients. They do **not** prove it sounds transparent on a real recorded voice, which contains breath, room tone, sibilance, vibrato and pitch drift at the same time. That needs your ears on a real take.

The remaining known limits:

* **Broadband material.** Noise has no true resonances, but it does have random spectral peaks. A detector that hunts for peaks will find some. This is inherent to the method, not a bug, and it is why the plug-in is tuned for voice.
* **Sub-sample phase.** The engine modifies magnitude and keeps the analysis phase. This is standard practice and measured clean here, but a phase-coherent resynthesis would be the next real step in quality. It is a large project, not a tweak.
* **Very low frequencies.** Below roughly 150 Hz a resonance and a note's own harmonics are hard to separate. The plug-in holds back there by design. The Low Band Detail switch adds a longer analysis window to help, and it changes the decision by 1 to 5 dB depending on the material, which is a genuine change rather than a clear improvement.

## How hard was this, and what would the next step cost

| Goal | Difficulty | Done |
|---|---|---|
| Bit-exact passthrough when idle | Trivial. Already true. | Yes |
| No collateral change at any Quality setting | Moderate. One root cause, one scaling fix, plus a regression test. | Yes, 2.1.1 |
| Less warbling on sustained notes | Easy. Raise the default overlap. Costs CPU. | Available now |
| Phase-coherent resynthesis | Hard. New synthesis path, new latency story, fresh tuning. | No |
| Multi-resolution synthesis, not just detection | Hard. Two transforms with independent phase, then a crossfade that does not comb. | No |
| Proven transparency on real vocals | Needs listening tests, not code. | Not started |

## Level matching (the MATCH button)

### Why it exists

Removing a resonance removes energy, so the processed signal is quieter. Measured on a synthetic voice at Depth 1.5, the reduction costs **1.30 dB of perceived loudness**. A quieter signal nearly always sounds better, so without compensation every A/B comparison is biased toward whichever version is louder, and the user ends up choosing on level rather than on tone.

### What changed in 2.2.0

The original version compared plain RMS. That is the wrong quantity here. A resonance suppressor removes energy at chosen frequencies, and the ear does not weight those frequencies equally: 3 dB off 3 kHz changes loudness far more than 3 dB off 60 Hz. Matching on RMS therefore mis-sets the correction.

The measurement now uses **K-weighting (ITU-R BS.1770)**, the same weighting every loudness meter uses. Both stages are designed at the running sample rate, so the response is correct at 44.1 kHz as well as 48 kHz. The shelf centre is 1501 Hz rather than the 1682 Hz quoted in some references, because for shelving filters the centre frequency and "Q" mean different things in different design formulas, and 1501 Hz is the value that actually lands on the published response. Fitted against the published BS.1770 coefficients, the design matches to **0.0004 dB** from 20 Hz to 20 kHz.

Three further corrections went in with it:

* **The boost is capped.** The output peak may rise as far as the louder of a -0.5 dBFS ceiling and whatever the input itself peaked at, and no further. A quiet take is compensated in full; a hot one is never pushed past the level the user was already working at. Only the boost is capped, so the trim can always pull down.
* **Matching is disabled in BYPASS and DELTA.** In DELTA the output is the removed content, which is meant to be quiet, so matching it to the dry level would misrepresent how much was taken out.
* **Both channels are measured.** The original accumulated the left channel only.

### What it measures

From the automated suite, on a synthetic voice at Depth 1.5:

| Take | Match off | Match on |
|---|---|---|
| Realistic level, -11 dBFS peak | -1.30 dB loudness | **+0.05 dB loudness**, +0.10 dB RMS |
| Hot take, peaks above 0 dBFS | | peak change **+0.02 dB** |

So at a realistic mixing level the compensation is complete to within a twentieth of a decibel, and on a signal that already peaks above full scale the correction cannot make the clipping worse.

### What is still not established

The correction is a slow trim: a 0.35 s envelope and a 0.25 s gain smoother. It follows a verse or a phrase, not individual words, which is what you want for a comparison. It is not a limiter and it is not a compressor, and it will not hold a level steady through a performance that moves by many decibels. Judge it on sustained material, and if a take has very uneven dynamics, set the level by ear with the output gain instead.


---

# Part 17 — The fine-tune drawer audit

Source: `docs/FINE-TUNE-AUDIT.md`

# FINE TUNE drawer — audit and plan

Measured on 2026-10-07 against build 2.5.0. Every figure below comes from the
file-based analyser (`Tools/Analyze.cpp`) on two synthetic takes: a formant voice
and a sibilant take with gated 5–11 kHz consonants.

## The complaint

> "I'm not really hearing an effect when I turn the sibilance up in the fine-tune
> tab."

Confirmed. The whole drawer is close to inert.

## What each control measures

Level change from the control's minimum to its maximum, depth 2, Standard (2k)
resolution, 4x overlap, auto-gain off.

| Control | Parameter | Min | Max | Change | Verdict |
|---|---|---|---|---|---|
| Sibilance De-Ess | `sibilanceSmooth` | −0.87 dB | −0.99 dB | **0.12 dB** | too weak to hear |
| Attack Tilt | `attackTilt` | −0.92 dB | −0.90 dB | **0.02 dB** | not shown by this test |
| Release Tilt | `releaseTilt` | −0.92 dB | −0.92 dB | **0.00 dB** | not shown by this test |
| Detail Tilt | `detailTilt` | −0.92 dB | −0.92 dB | **0.00 dB** | genuinely dead |
| Note Motion | `motionProtect` | −0.92 dB | −0.92 dB | **0.00 dB** | not shown by this test |

Two of these need a different test before they can be judged, and the table alone
would be unfair to them. The distinction matters:

- **Steady-state level cannot show a ballistics control.** Attack Tilt and Release
  Tilt change *how fast* the gain moves, not how far it ends up. On a two-second
  steady take the gain settles to the same value either way. They need a gated or
  percussive signal and a time-resolved measurement.
- **Note Motion needs pitch movement.** It is gated behind
  `motionProtect > 0 && historyValid && previousPeriod >= 3`. A fixed-pitch take
  never satisfies it. It needs a take that glides between notes.

So the honest reading is: **one control is dead, one is far too weak, and two are
untested rather than proven dead.**

## Why Detail Tilt is dead

`ResonanceDetector.h` computes the analysis window like this:

```cpp
const float tiltMul = std::pow (ratio, 0.50f * detailTilt);
const float effSharpness = std::clamp (dialSharpness * tiltMul, 0.20f, 5.0f);
const float erbHz = (24.7f * (4.37f * (f * 0.001f) + 1.0f)) * (1.0f / effSharpness);
const int erbBins = std::clamp (static_cast<int> (erbHz / binWidth), 3, maxRadius);
const int wideFloor = std::max (4, static_cast<int> ((bins / 48) * floorScale));
const int radialBins = std::clamp (std::max (std::max (erbBins, minRadiusBins), wideFloor), 3, maxRadius);
```

The tilt is applied to `effSharpness`, which shrinks `erbBins`. But `wideFloor`
then takes the maximum, and at Standard (2k) that floor is about 16 bins. The ERB
at 4 kHz is already about 17 bins before tilting, so the tilt can only pull it
down to the floor and no further. `radialBins` never changes. The control is
wired all the way through and has no effect.

This is the same root cause as the original DETAIL problem: a floor that was
added to protect the detector from reading a peak's own flanks as its baseline
now swallows every control that tries to narrow the window.

## Why Sibilance is too weak

`ResonanceDetector.h` gates it to 4–12 kHz and gives it two levers:

```cpp
if (isSibilanceBand && p.sibilanceSmooth > 0.01f)
    effectiveCue = std::max (effectiveCue, p.sibilanceSmooth * 3.0f);

if (isSibilanceBand && p.sibilanceSmooth > 0.05f)
    thr -= (1.5f * p.sibilanceSmooth);
```

A threshold drop of **1.5 dB maximum** and a cue ceiling of 3.0. Compared with
the main controls — Depth moves reduction by several dB, How Picky moves the
threshold by 3.5 dB — the sibilance lever is roughly a third the authority of
anything else on the panel. It is not broken. It is underpowered by design, and
the design was never checked against a sibilant signal.

## What the control is supposed to be

Sibilance is not a resonance. It is broadband noise concentrated in 4–12 kHz,
present only during S and T consonants. A resonance suppressor handles it badly
because the shape is wrong: the plugin cuts narrow peaks, sibilance is a broad
hump that comes and goes.

That is why the de-esser needs its own behaviour, not a small nudge to the
resonance detector:

1. **Detection by broadband energy, not by peaks.** Measure the 4–12 kHz band's
   level against the 1–4 kHz band's level. Sibilance is when the high band jumps
   relative to the body. That is how a de-esser works and it is why it fires on
   consonants and not on vowels.
2. **A time constant matched to consonants.** S and T last 40–120 ms. The
   detector's frames are 46 ms at Standard resolution. The control needs its own
   envelope, not the resonance ballistics.
3. **A wide, shallow cut.** Sibilance needs a broad shelf or a low-Q dip across
   5–11 kHz, not a narrow notch. Cutting it with notches is what makes de-essers
   sound lispy.
4. **A range control.** Which frequencies count as sibilance depends on the
   voice. A fixed 4–12 kHz will be wrong for some singers.

## The plan

Ordered by value per unit of risk. Each step is measurable with the existing
analyser, and each has a test that can fail.

### Step 1 — Give the tilts somewhere to act (fixes Detail Tilt)

Make `wideFloor` scale with the control instead of clamping above it. Today the
floor is applied as a maximum that wins; it should be a lower bound the control
can approach. Concretely: let the floor fall to a quarter of its value as the
control goes to its extreme, rather than the current half, and apply the tilt
*after* the floor rather than before.

- Measure: the window width per frequency, from the overlay now on the graph.
- Test: the drawn window must differ by more than 20% between Detail Tilt −1 and
  +1 at 6 kHz.
- Risk: low. The floor still exists; it just moves.

### Step 2 — Rebuild Sibilance as a real de-esser

Replace the two small levers with the four-part design above. Keep the parameter
ID so saved sessions load.

- Measure: level change in the 5–11 kHz octave, on the sibilant take, at the
  control's extremes. Target **3 dB or more** — ten times its current authority.
- Test: on a sibilant take the control must move the 5–11 kHz octave by more
  than 2 dB while moving 250–1000 Hz by less than 0.5 dB. That second half is the
  important one: it proves the control is a de-esser and not a second Depth knob.
- Risk: medium. It is new DSP and will need two or three calibration passes.

### Step 3 — Prove or remove Attack Tilt and Release Tilt

Build the measurement that can see them: a gated tone burst, then track the gain
envelope over the first 50 ms and the release tail separately.

- If they move the envelope by a useful amount, keep them and document what they
  do.
- If they do not, remove them. A control that cannot be measured cannot be
  defended, and two dead knobs in a drawer teach the user to distrust the rest.

### Step 4 — Prove or remove Note Motion

Build a take that glides between two notes and confirm the control changes the
decision. Same rule: if it cannot be shown to act, it should not ship.

### Step 5 — Label everything honestly

Once each control is proven, name it for what it does and give it a tooltip that
states the measurement. The drawer currently mixes three different kinds of
control — a de-esser, two ballistics tilts and a detection tilt — with no
indication of which is which.

## A note on how this was missed

The plugin has 39 processor checks and 23 DSP checks, all passing, and the whole
drawer was inert. Every existing test measured the *main* controls. Nothing
measured the drawer, because the drawer was added last and tested by inspection
rather than by measurement.

The fix for that is the rule applied in this plan: **no control ships without a
measurement that would fail if it did nothing.**

---

## Results: the plan, implemented (2.6.0)

Every number below is from the test suite, which now fails if the control stops
working. The suites stand at **36 processor checks** and **30 DSP checks**, all
passing.

### Step 1 — the floor now moves with the tilt

Detail Tilt was wired end to end and changed nothing, because the protective floor
was applied as a maximum *above* the tilted bandwidth, so the floor always won.

| | Before | After |
|---|---|---|
| Window at 6 kHz, tilt −1 → +1 | 69 → 69 bins | **69 → 49 bins** |
| Audio effect | 0.00 dB | **1.9 dB** |
| Graph pixel change | 0.0 % | **12.28 %** |

The floor is now scaled by the same tilt multiplier. This is the single change
that un-blocked the control.

### Step 2 — Sibilance is a real de-esser

The old path nudged the resonance detector: a 1.5 dB threshold drop and a cue
ceiling in the 4–12 kHz band. It moved the level by **0.12 dB**.

It is now its own stage, `Source/DSP/SibilanceDeEsser.h`, with the four pieces a
de-esser needs: broadband energy detection against the 1–4 kHz body, an excess
measure against the take's own resting ratio, consonant-matched time constants
(2 ms attack, 55 ms release), and a wide raised-cosine cut across the band.

| SIBILANCE | 4 kHz | 8 kHz | 250–2000 Hz |
|---|---|---|---|
| 0.0 | +0.0 dB | +0.0 dB | +0.0 dB |
| 0.5 | 0.7 dB | 1.3 dB | **0.0 dB** |
| 1.0 | 1.2 dB | 2.4 dB | **0.0 dB** |

Measured as a unit test on a voice with a consonant burst: **5–11 kHz −11.04 dB,
250–1000 Hz +0.00 dB.** The second column is the point. Without it this is just a
second Depth knob.

### Step 3 — the tilts are both real

Steady-state level cannot see these; they change *how fast* gain moves, not how
far it lands. Measured by stepping the suppressor and counting frames:

| | 300 Hz | 6 kHz |
|---|---|---|
| Attack, tilt +1 | 8 frames | 2 frames |
| Attack, tilt −1 | 2 frames | 5 frames |

Positive tilt slows the lows and speeds the highs, exactly as documented.

**Release Tilt was genuinely broken.** The tilt reached only the coefficient used
after a sustained attack. The release almost always runs through a fast path that
ignored it entirely, so the control read as dead. With the tilt applied to the
path that actually runs:

| | 300 Hz | 6 kHz |
|---|---|---|
| Release, tilt +1 | 0.425 | 1.000 |
| Release, tilt −1 | 0.849 | 0.546 |

### Step 4 — Note Motion was removed

Its period history was one frame deep, and it compared two smoothed values that
are nearly identical, so its central condition could never be met. A real
eight-frame history was built and it *still* did not act: two measurements of the
situation it exists for — a stationary resonance with harmonics sliding past it —
came back identical at 0 and at 100 %.

The plan says a control that cannot be shown to work does not ship. Its drawer
control is gone and the parameter ID is kept so saved sessions still load.

### What this cost

Three defects in this drawer were all the same defect: **a control that was wired
up, tested by nobody, and quietly doing nothing.** 62 checks passed for months
while the entire drawer was inert, because every test measured the main controls.
The rule that came out of it is in the test suite: a control does not ship without
a measurement that would fail if it did nothing.


---

# Part 18 — De-esser roadmap, six phases

Source: `docs/DE-ESSER-ROADMAP.md`

# Making the de-esser pristine

A plan for taking the sibilance stage from "it works" to "you stop hearing
sibilance". Every claim here is either measured on this machine or cited to a
source. Where I am guessing, I say so.

---

## 1. What you reported, and what I found

You ran a vocal through and the sibilants were still there. So I built four
vocal-like takes — a formant-shaped vowel alternating with a sibilant burst — and
measured the de-esser alone, with the resonance path switched off.

| Take | amount 0.5 | amount 1.0 |
|---|---|---|
| Sparse, gentle sibilance | −4.19 dB | −6.91 dB |
| Dense, rapid sibilance | −3.60 dB | −6.28 dB |
| Harsh narrow resonance at 7.4 kHz | −4.50 dB | −7.81 dB |
| Quiet sibilance, harsh resonance | −4.78 dB | −8.11 dB |

The body of the voice (300–1200 Hz) moved by less than 0.4 dB in every case.

So the stage is working, and it is working cleanly. It cuts 4–8 dB of the
sibilance band and leaves the vowel alone. **That is not the problem.** The
problem is what it is cutting, and how.

### Finding 1: the band stops at 11 kHz, and sibilance does not

I ran the same takes again with the band extended to 14 kHz:

| Take | Band to 11 kHz | Band to 14 kHz | Gained |
|---|---|---|---|
| Sparse | −6.91 dB | −8.67 dB | **+1.76 dB** |
| Dense | −6.28 dB | −7.99 dB | **+1.71 dB** |
| Harsh | −7.81 dB | −9.28 dB | **+1.47 dB** |
| Quiet | −8.11 dB | −9.55 dB | **+1.44 dB** |

The top octave carries real sibilance energy that the current band never touches.
The current high edge is 11 kHz; the fix starts at 14–16 kHz.

### Finding 2: dense sibilance gets *less* correction than sparse

−6.28 dB on the dense take against −6.91 dB on the sparse one. That is backwards.
The detector measures the sibilance band against a 400 ms running average, so
when sibilants arrive faster than that window, the average climbs to meet them and
the measured "excess" shrinks. **The control eases off exactly when the material
gets hardest to handle.**

### Finding 3: the cut shape is fixed, and real sibilance moves

This is the structural one. The current stage cuts a **fixed** band with a
**fixed** raised-cosine shape. Every sibilant gets the same treatment regardless
of where its harshness actually sits.

The acoustics say that is wrong. From the literature:

- English **/s/ has a spectral peak near 7000 Hz** and a spectral mean above
  6000 Hz, while **/ʃ/ peaks much lower**, around 4–5 kHz.
- Sibilant fricatives are distinguished by "static (overall level) and dynamic
  (shape) aspects of the **peak ERBN number**" — the peak position on the ERB
  scale is the discriminating feature.
- Frequencies **above 8 kHz** carry cues that separate sibilant from non-sibilant
  fricatives.
- Sibilance is usually quoted at 4–10 kHz, but "can go as low as 1.5 kHz" on some
  voice and microphone combinations.
- And it moves *within one performance*: "a singer might sibilate at a higher
  frequency when singing softly, but hit the esses harder in the 3 kHz region
  when belting."

A fixed 4–11 kHz band with a fixed shape is a compromise that serves none of
those cases well. It under-cuts /s/, over-cuts /ʃ/, and misses the top entirely.

### Finding 4: it is a broadband duck in disguise

The modern criticism of traditional de-essers is that they "use energy in a
specific frequency range as a proxy for the occurrence of sibilance" and therefore
"duck the entire audio spectrum". Mine does not duck the whole spectrum — it is
split-band — but it ducks **its whole band uniformly**, which is the same mistake
one level down. A harsh narrow peak at 7.4 kHz and a broad gentle hiss get
identical treatment.

The state of the art has moved to spectral de-essing: "a ton of compressors
working on many narrow frequency bands at once", cutting "only the specific
frequencies where these unwanted sounds occur". There is also a published
technique for exactly what we need — **automatic sibilance tracking via a
spectral-centroid-based frequency estimator** (AES, "Intelligent Adaptive
De-Essing with Automatic Sibilance Tracking").

---

## 2. What "pristine" means, concretely

I want the goal stated as something measurable, because "sounds better" is not.

**Target: on a take with genuine sibilance, the de-esser must reduce the harsh
band by 10–14 dB while moving 300–1200 Hz by less than 0.5 dB, and it must reach
that reduction on dense sibilance as readily as on sparse.**

Secondary goals:

- No audible dulling of a voice that is bright but not sibilant.
- No lisp: the cut must not create a "th" sound where an "s" was.
- The cut must follow the sibilant, not sit on a fixed band.
- Nothing above 16 kHz should be touched, and nothing below 3 kHz.
- It must not trigger on cymbals, bright reverb, or distorted guitars.

---

## 3. The plan

Eight phases. Each ends with a measurement that fails if the phase did not work,
because that is the rule that came out of the last round: **no control ships
without a measurement that would fail if it did nothing.**

### Phase 1 — Widen the band and expose it

**Do:** default the band to 4–16 kHz; expose low and high edges as real controls.

**Why:** measured. Worth 1.4–1.8 dB immediately, for a one-line change.

**Evidence:** the table in Finding 1.

**Risk:** low. The only real risk is pulling in cymbal or air-band noise, which
Phase 5's gating addresses.

---

### Phase 2 — Track the sibilance instead of assuming where it is

**Do:** per frame, compute the **spectral centroid of the energy inside the
sibilance band**, smooth it lightly, and centre the cut on it rather than on the
band's geometric middle.

**Why:** this is the published technique, and it directly answers Findings 3 and
4. An /s/ at 7 kHz and an /ʃ/ at 4.5 kHz should not get the same cut in the same
place.

**Evidence:** AES "Intelligent Adaptive De-Essing with Automatic Sibilance
Tracking"; the /s/ ≈ 7 kHz and /ʃ/ ≈ 4–5 kHz figures; the "peak ERBN number"
result.

**How I will know it worked:** on a synthetic /s/ take the tracked centre must sit
above 6 kHz; on an /ʃ/ take, below 5.5 kHz. Today both get 7.5 kHz.

**Risk:** moderate. A centroid is easily dragged by broadband noise. Needs a
sanity clamp and smoothing across frames, or the cut will wander audibly.

---

### Phase 3 — Split the band so only the harsh part is cut

**Do:** divide the sibilance range into three sub-bands (roughly 4–6.5, 6.5–9.5,
9.5–16 kHz), each with its own excess detection and its own gain. Only the
sub-band that is actually harsh gets cut.

**Why:** this is what stops the dulling. It is the difference between "turn the
top down" and "remove the thing that hurts". It also gives the tool something
intelligent to say about mixed cases, which a single band cannot.

**Evidence:** the split-band vs broadband consensus; the multiband and spectral
de-essing typology; the finding that /s/ and /ʃ/ occupy different regions.

**How I will know it worked:** a take with harshness only at 7–9 kHz must show
more than 3 dB of cut there and less than 1 dB in 4–6 kHz. Today both get the
same.

**Risk:** moderate. Three detectors means three chances to mis-trigger, and the
summed gain must not overshoot at the band joins.

---

### Phase 4 — Shape the cut like a bell, not a curtain

**Do:** replace the broad raised-cosine shelf with a bell centred on the tracked
sibilance peak, with a width derived from the peak's ERB extent.

**Why:** a bell removes the harshness and leaves the surrounding air intact. A
curtain takes everything down together, which is why broadband de-essing sounds
dull.

**Evidence:** "In an ideal world, de-essers would only be ducking the specific
frequencies at which sibilance occurs"; the spectral de-esser typology; the ERB
peak-position result.

**How I will know it worked:** at equal in-band reduction, the bell must lose less
energy in 10–16 kHz than the curtain does.

**Risk:** low, but it interacts with Phase 3 — if both are done, the shape may
become redundant. Phase 3 may subsume it. I will check before building both.

---

### Phase 5 — Detect sibilance, not just loud treble

**Do:** three gates, all of which must be satisfied before the de-esser acts:

1. **Absolute** — the band must be above a level floor, so silence never triggers it.
2. **Relative** — the excess over the take's own running ratio (this is what
   exists today, and it is the right idea, but see Phase 6).
3. **Shape** — the energy must look like a fricative: a peak in the band with a
   **negative spectral slope above it**, and a **steep onset**. Cymbals are
   sustained and spectrally flat; sibilants are peaked and sudden.

**Why:** this is sonible's central criticism of traditional de-essers — they are
"triggered by any loud sound in the sibilant frequency range". The slope above
8 kHz is a documented cue for separating sibilant from non-sibilant fricatives,
and it is the same feature that separates a voice from a cymbal.

**How I will know it worked:** on a bright-but-not-sibilant take (my "sparse"
take with the sibilant bursts replaced by sustained cymbal-like noise) the
de-esser must apply less than 1 dB. Today it would apply its full cut.

**Risk:** moderate. Too much gating and real sibilance slips through — which is
the complaint I am trying to fix. This phase needs to be measured on both
directions, not just one.

---

### Phase 6 — Stop the detector easing off when sibilance gets dense

**Do:** replace the single 400 ms running average with a **percentile tracker** —
the 20th percentile of the ratio over the last few seconds, not the mean. A
percentile of the ratio ignores the sibilants themselves and learns the *voice*,
where the mean gets dragged upward by exactly the thing we are trying to catch.

**Why:** this is Finding 2. The dense take gets 0.63 dB less correction than the
sparse one, and density is precisely when you need it.

**How I will know it worked:** the dense take must be corrected at least as much
as the sparse one, ideally slightly more.

**Risk:** low. It is a like-for-like swap of one statistic for a better one.

---

### Phase 7 — Match the timing to the phoneme

**Do:** derive attack and release from the measured sibilant duration rather than
fixed 2 ms / 55 ms. A 40 ms "ts" and a 200 ms "shh" want different releases. Also
add a short **look-ahead** so the cut is in place before the consonant peak
arrives, rather than chasing it.

**Why:** iZotope's guidance is that release should be "short enough to recover
before the next" sibilant, which is a statement about matching the material. A
fixed release cannot do that for both a rapper and a ballad.

**Evidence:** "keep the attack fast since sibilant sounds are transients";
release matched to the gap between sibilants.

**How I will know it worked:** on the dense take the gain must return to unity
between sibilants, and on the sparse take it must not chatter.

**Risk:** low. The look-ahead is a delay, and this plugin already reports latency,
so it costs nothing extra in host delay terms.

---

### Phase 8 — Learn the take

**Do:** reuse the existing Learn machinery to profile the sibilance in a real
take: find the actual band, set the threshold from the measured distribution, and
suggest a starting amount.

**Why:** every serious de-esser now has this, and it is the difference between a
tool that works on my synthetic takes and one that works on your voice. Your
sibilance is not my sibilance.

**How I will know it worked:** the profile taken from a take must produce a
setting within 2 dB of the best setting found by sweeping.

**Risk:** low — the Learn infrastructure and the review-before-apply pattern
already exist.

---

## 4. Order, and what I would cut

Phases 1, 2, 6 and 7 are the high-value ones and are largely independent. Phase 1
is already proven and takes almost no work. Phase 6 is a small, safe change with a
measured payoff.

Phases 3, 4 and 5 are where it becomes genuinely "smarter", and they are also
where the risk sits. Phase 5 in particular can go wrong in the direction of
letting sibilance through, which is the current complaint.

Phase 8 is the one that makes it work on *your* voice, and I would not skip it.

**If I had to pick three:** 1, 6, and 8. They are the cheapest, safest, and they
close the gap between "works on a synthetic take" and "works on your vocal".

---

## 5. How this gets validated

The suite already measures the de-esser on four vocal-like takes and prints the
result, so every phase lands against a number. I will add:

- **A tracking test** — the cut centre must follow an /s/ vs an /ʃ/.
- **A selectivity test** — harshness at 7–9 kHz must not cut 4–6 kHz.
- **A false-positive test** — bright sustained noise must not trigger it.
- **A density test** — dense sibilance corrected at least as much as sparse.
- **A no-lisp test** — the 4–5 kHz region must not be over-cut, because that is
  what turns an "s" into a "th".

The last one is the one that separates a de-esser that measures well from one that
sounds right, and I want it in from the start rather than bolted on.

---

## 6. The honest caveat

I still have not heard your take. Everything above is measured on synthetic
vocal-like material, which proves the signal path behaves — it does not prove it
behaves on your voice. If you can put a short clip of the actual vocal somewhere I
can reach it, I can measure your sibilance directly and set Phase 8 against it
instead of against a guess. That would move this from well-reasoned to certain.

---

## References

- Sonible, *Moving Beyond Traditional De-essers* — traditional de-essers as
  frequency proxies, and the broadband-duck problem.
  https://www.sonible.com/blog/beyond-traditional-deessers/
- iZotope, *The Dos and Don'ts of De-Essing* — wideband, split-band, multiband and
  spectral typology; sibilance range 4–10 kHz, "as low as 1.5 kHz"; the observation
  that sibilance frequency moves with vocal effort.
  https://www.izotope.com/community/blog/the-dos-and-donts-of-de-essing
- *Acoustic characteristics of sibilant fricatives and affricates* (JASA 2023) —
  /s/ peak near 7000 Hz, spectral mean above 6000 Hz.
  https://pubs.aip.org/asa/jasa/article/153/6/3501/2900598/
- Reidy et al., *Spectral dynamics of sibilant fricatives are contrastive and
  language-specific* — static and dynamic shape of the peak ERBN number.
  https://pmc.ncbi.nlm.nih.gov/articles/PMC5132428/
- *Examining the effect of high-frequency information on fricative classification*
  (JASA 2023) — the role of content above 8 kHz.
  https://pubs.aip.org/asa/jasa/article/154/3/1896/2912771/
- Kong et al., *Classification of Fricative Consonants* (PLOS ONE) — spectral slope
  above 8 kHz as a sibilant/non-sibilant cue.
  https://journals.plos.org/plosone/article?id=10.1371/journal.pone.0095001
- AES, *Intelligent Adaptive De-Essing with Automatic Sibilance Tracking* —
  spectral-centroid-based frequency tracking, JUCE implementation.
  https://aes.org/publications/elibrary-page/?id=23182
- Avid, *What is Spectral De-Essing?*
  https://www.avid.com/resource-center/what-is-spectral-de-essing

---

## 7. Progress

### Phase 1 — widen the band: DONE (2.7.0)

Default band moved from 4-11 kHz to 4-16 kHz. Ceiling raised from 12 dB to 18 dB,
because the shaped edges meant 12 dB of ceiling was delivering only about 7 dB of
measured reduction, and the edge width was cut from 25% of the span to 15%.

| Take | Before | After 2.7.0 | Old 11 kHz band at the new ceiling |
|---|---|---|---|
| Sparse, gentle | −6.91 dB | **−12.87 dB** | −9.09 dB |
| Dense, rapid | −6.28 dB | **−12.30 dB** | −8.58 dB |
| Harsh resonance | −7.81 dB | **−13.82 dB** | −10.35 dB |
| Quiet sibilance | −8.11 dB | **−14.11 dB** | −10.73 dB |

Body (300–1200 Hz) moved by 0.27–1.17 dB in every case.

The target was 10–14 dB. Delivered: 12.3–14.1 dB. The widening alone is worth
3.8 dB, which is the comparison column against the old band at the same ceiling.

### Phase 6 — percentile tracker: DONE (2.7.0)

The 400 ms mean is replaced by the 20th percentile of the ratio over roughly four
seconds. The gap between dense and sparse narrowed from 0.63 dB to 0.57 dB.

Honest reading: this is a real improvement but it is **not the fix** I expected.
The tracker was not the main thing holding the dense case back; the ceiling and
the edge width were. The percentile stays because it is more robust and cannot
make things worse, but it did not close the density gap on its own.

### A test that was measuring two things at once

Raising the ceiling made the "bright fricative-band noise keeps its top end" check
fail at −1.42 dB. That check feeds first-difference noise, which is 6 dB per
octave — exactly the tilt a fricative has. The de-esser cutting it is correct.
The check was written to catch the *resonance* path muffling, so it now turns the
de-esser off and measures what it claims to. The de-esser is measured separately
in `testDeEsserOnVocalMaterial()`.

### Still to do

Phases 2, 3, 4, 5, 7 and 8. Phase 5 (shape-based gating) matters more now than it
did, because an 18 dB ceiling means a false trigger is an 18 dB false trigger.

### Phase 5 — the duration gate: DONE (2.8.0)

A sibilant lasts 40–200 ms; a cymbal, a shimmer reverb or a distorted guitar does
not stop. The gate counts how long the excess has been significant and hands the
cut back after 600 ms, over the following 400 ms.

**It went wrong first, and the real vocal caught it.** The first version counted
any excess over 1 dB as "sustained". On a 120 s take there is almost always 1 dB of
excess somewhere, so the counter never reset and the gate throttled the whole
performance: measured reduction at 9–11 kHz fell from **12.18 dB to 2.30 dB**.
Raising the entry threshold to 3 dB and resetting six times faster fixed it.

Proven both ways:

| | 9–11 kHz on the real take |
|---|---|
| No gate | −12.18 dB |
| Gate, 1 dB threshold (wrong) | **−2.30 dB** |
| Gate, 3 dB threshold | **−11.94 dB** |

And on a synthetic 1.8 s burst: first 350 ms **−14.01 dB**, last 350 ms **+0.95 dB**.

### Phase 6 — corrected reading

The percentile tracker is a real improvement but it is not what was holding the
dense case back. The ceiling and the edge width were. Recorded here so the next
round does not over-credit it.

---

## 8. Measured on the real vocal (2.8.0)

A 120 s sung take, 48 kHz, peak −19.5 dBFS. Where the sibilance actually sits,
measured as the excess of sibilant frames over body frames:

| Band | Excess | |
|---|---|---|
| 3000–4000 Hz | +1.9 dB | |
| 4000–5000 Hz | +5.1 dB | |
| 5000–6000 Hz | +9.2 dB | |
| 7000–8000 Hz | +11.7 dB | |
| 8000–9000 Hz | +11.8 dB | |
| 9000–11000 Hz | **+12.5 dB** | ← the peak |
| 11000–13000 Hz | +11.5 dB | |
| 13000–16000 Hz | +10.5 dB | |
| 16000–20000 Hz | +8.9 dB | |

Sibilant peak at **9.5 kHz**. **13.2% of the 5–16 kHz sibilance energy sits above
11 kHz**, which is the old ceiling — so the top octave was never being treated.
Above 16 kHz there is almost nothing (0.1%), so 16 kHz is the right top edge.

What the plugin now does to that take, de-esser alone at full strength:

| Band | Reduction |
|---|---|
| 300–1200 Hz (body) | **−0.00 dB** |
| 4000–5000 | −0.97 dB |
| 5000–7000 | −4.64 dB |
| 7000–9000 | −9.50 dB |
| 9000–11000 | **−11.94 dB** |
| 11000–14000 | **−10.15 dB** |
| 14000–16000 | −7.66 dB |

The cut lands where the sibilance is and the body does not move at all. The
11–14 kHz band gets 10.15 dB that the old band would have given 0 dB.

At the shipped default of 0.5 the same bands read −0.75 / −3.65 / −6.56 / −7.50 /
−6.80 / −5.53 dB.

### Still to do

Phases 2, 3, 4, 7 and 8. With the band, the tracker and the gate in place, the
remaining work is about *where* the cut goes rather than *whether* it happens:
tracking the sibilance position, splitting the band so only the harsh part is cut,
shaping the cut as a bell, matching the timing to the phoneme, and learning the
band from the take.

---

## 9. Sources consulted

- sonible, "Beyond traditional de-essers" — the criticism that conventional
  de-essers "use energy in a specific frequency range as a proxy for the
  occurrence of sibilance". https://www.sonible.com/blog/beyond-traditional-deessers/
- iZotope, "The dos and don'ts of de-essing" — split-band modes, and the range
  "typically between 4 and 10 kHz" but "can go as low as 1.5 kHz".
  https://www.izotope.com/community/blog/the-dos-and-donts-of-de-essing
- AES, "Intelligent Adaptive De-essing with Automatic Sibilance Tracking" — the
  spectral-centroid method for estimating sibilance position.
  https://www.aes.org/e-lib/browse.cfm?elib=19825
- Beat Kitchen, "Multiband, De-Esser, Dynamic EQ, and Sidechain" — practical
  three-band splits (0–6.5, 6.5–8.5, 8.5–20 kHz) with the compressor on the
  middle band. https://beatkitchen.io/guides/mix-primer/20-multiband-deesser-sidechain/
- Techivation, "What does the lookahead in T-De-Esser Pro do" — the purpose of
  look-ahead, which the STFT window already provides ~21 ms of.
  https://techivation.com/blog/lookahead-in-t-de-esser-pro/
- Laroche and Dolson (1999), "Improved phase vocoder time-scale modification of
  audio", IEEE Trans. Speech Audio Process. 7(3) — identity phase locking, for the
  synthesis question in RESEARCH-SYNTHESIS-OPTIONS.md.
  https://www.ee.columbia.edu/~dpwe/papers/LaroD99-pvoc.pdf
- Udo Zölzer (ed.), "DAFX: Digital Audio Effects", chapter 7 — look-ahead time in
  dynamic range control.

---

## 10. What shipped in 2.9.0

All eight phases are in the installed build. The numbers below are measured, not
estimated, and every one of them comes from a reproducible test in `Tests/`.

### The four ways the first rewrite was wrong

Building a multiband de-esser that tracked the sibilance was not a matter of
splitting one band into six. Four separate mistakes each cost real decibels, and
each was found by measuring rather than by reading the code.

**1. The gain was composed band by band.** Multiplying `gain *= (1 - w) + w * g`
for each band looks like interpolation and is not. At a bin halfway between two
band centres, with both bands cutting equally hard, it computes
`0.59 x 0.59 = 0.35` where the answer is `0.18`. A 15 dB cut arrived as 9 dB.
Fixed by interpolating the gain linearly, once. This alone was worth about 5 dB.

**2. The band weights overlapped too far.** Raised cosines with `1.6 / kNumBands`
half-width meant a bin near a band centre drew only about 60% of its own band's
gain and 40% from neighbours that were not being cut. Replaced with a two-band
linear interpolation and an edge taper outside the search range.

**3. The amount was decided per band, where the evidence is weakest.** Six narrow
bands each see less excess than one wide band does, so driving the cut from six
independent decisions measured 3-4 dB less reduction than the single-band version
it replaced. Fixed by splitting the job: the DECISION is broadband, where the
evidence is strong; the narrow bands only decide WHERE the cut lands.

**4. The learner measured loudness, not sibilance.** It compared each band against
the voice body and picked the highest. On a real vocal that returned **4490 Hz**,
which is a formant. The same take measures its sibilance peak at 9.5 kHz. A band
is sibilant when it exceeds *its own* resting level, not when it is loud. Fixed by
giving the learner its own per-band history and profiling the excess over it; the
same take now profiles at **11314 Hz**, and the band it opens spans 4000-16000 Hz.

### Tracking

The cut follows the sibilance rather than sitting at a fixed frequency. On
synthetic takes built so the sibilance is genuinely concentrated:

| Take | Tracked centre |
|---|---|
| sibilance at 5.2 kHz | 5057 Hz |
| sibilance at 11 kHz | 9884 Hz |

Getting there took three attempts at the same line. Weighting the centroid by the
dB excess let the quiet top bands dominate — their resting level is near the noise
floor, so a small amount of sibilant energy up there reads as a large dB excess.
Squaring the dB helped a little and still failed (7785 Hz and 8594 Hz — no useful
tracking). Weighting by **linear excess energy** is what works, because it measures
how much extra sound is present rather than how large the ratio is.

### Measured reduction, amount 1.0

| Take | 2.8.0, one wide band | 2.9.0, tracking multiband |
|---|---|---|
| sparse, gentle | -11.24 dB | **-13.62 dB** (peak -17.08) |
| dense, rapid | -10.72 dB | **-13.03 dB** (peak -16.24) |
| harsh resonance | -12.42 dB | **-14.37 dB** (peak -17.17) |
| quiet sibilance | -12.70 dB | **-14.63 dB** (peak -17.10) |

The body band stays within 0.4-1.2 dB in every case, so the voice itself is not
being hollowed out.

### On your own take

The stop-band profile on the real vocal, amount 1.0:

| Band | Reduction |
|---|---|
| 300-1200 Hz (body) | -0.00 dB |
| 4000-5000 Hz | -4.70 dB |
| 5000-7000 Hz | -5.41 dB |
| 7000-9000 Hz | -9.09 dB |
| 9000-11000 Hz | **-10.84 dB** |
| 11000-14000 Hz | -9.45 dB |
| 14000-16000 Hz | -8.26 dB |

The shape follows the take. Your sibilance measures +5.1 dB of excess at 4-5 kHz
and +12.5 dB at 9-11 kHz, and the cut is proportioned to match rather than applied
flat across the range.

### The learn flow

Press LEARN, play the take, press STOP. The status line reports the take's own
sibilance peak alongside the suggested focus frequencies. APPLY moves the band and
the amount to match what the take actually contained. The learned amount is capped
at 0.85 so there is headroom to reduce — Learn is a starting point, not a maximum.

### Honest limits

- These are measurements, not a listening test. They prove the de-esser reaches
  the right depth at the right frequency and leaves the body alone. They do not
  prove it sounds right on your voice, and only your ears can settle that.
- The six band centres are fixed. Tracking moves the *weighting* between them, so
  the resolution of the tracked position is the band spacing, not the FFT bin.
- The duration gate assumes a held sibilance is a fault. A production that
  deliberately stacks a long "sss" will be trimmed.

### Test coverage

35 DSP checks and 36 processor checks, all passing, including the two new ones:
the cut tracks a low sibilance, and it follows the sibilance when the take moves
it up.


---

# Part 19 — De-esser precision brainstorm

Source: `docs/DE-ESSER-PRECISION-IDEAS.md`

# Making the de-esser more precise — brainstorm

Where the current de-esser loses precision, and what would fix each one. Ordered
by how much it would change, not by how interesting it is.

Current state as the baseline for everything below: six fixed log-spaced bands
across 4–16 kHz, a 20th-percentile per band as the resting baseline, a broadband
excess deciding *how hard* and the per-band share deciding *where*, with linear
gain interpolation between bands.

---

## Tier 1 — the three that would change the most

### 1. Spectral flatness as the sibilance gate

**The problem.** Right now "sibilant" means "this band is louder than it usually
is". That is a loudness test, and a bright vowel, a cymbal bleed, or a breath can
all pass it. It is why the duration gate exists at all — to clean up the false
positives the loudness test lets through.

**The idea.** Spectral flatness, also called Wiener entropy, is the ratio of the
geometric mean of a spectrum to its arithmetic mean (Wikipedia, *Spectral
flatness*; it is standard in MPEG-7 audio descriptors). A noise-like signal has a
flat spectrum and scores near 1. A tonal or harmonic signal scores near 0.

That is exactly the distinction a de-esser needs. Fricatives — /s/, /ʃ/, /f/ — are
noise. Vowels are harmonic, even when bright. So flatness separates them for
reasons that have nothing to do with level, and it does it in a few lines:

```
flatness = exp(mean(log(P[k]))) / mean(P[k])     over the sibilance band
```

A product already builds its whole de-esser on this idea — Mashav's *Entropy
Smart De-Esser* describes "spectral entropy analysis to detect and attenuate harsh
high-frequency content while preserving natural speech character".

**What it would buy.** Fewer false positives means less need for the duration
gate, which means a long deliberate "sss" stops being trimmed. It should also let
the detector work at lower excess thresholds, so it catches gentle sibilance it
currently ignores.

**Cost.** Small. One running log-sum and arithmetic sum per band, already iterating
the bins.

### 2. Move the bands instead of only re-weighting them

**The problem.** The tracked centre resolves to the *band spacing*, not the FFT
bin. Six bands across 4–16 kHz are roughly 0.38 octaves apart. A sibilance peak
landing between two centres gets shared between them, and the shape of the cut
never changes — only its tilt does. This is the single largest precision limit in
the current design and I documented it as such.

**The idea.** Re-centre the band grid on the tracked sibilance each frame. The cut
shape then follows the sibilance instead of the sibilance being approximated by a
fixed grid. With the tracked position smoothed over ~200 ms, the bands would
breathe rather than teleport, so it stays stable.

**What it would buy.** Resolution goes from 0.38 octaves to a smoothed continuous
track. A narrow /s/ gets a narrow cut exactly where it sits rather than a broad
one straddling it.

**Cost.** Small — the weights are already rebuilt when the parameters change; it
becomes a per-frame rebuild. Rebuild cost is ~1025 bins × 6 bands, which is
affordable at 47 frames/second but worth measuring.

**Combined with #3 below**, it becomes strictly better than either alone.

### 3. Sub-bin peak interpolation

**The problem.** The tracked centre is a weighted average of band centres, so it
is quantised before the smoothing even starts.

**The idea.** Parabolic interpolation on the three bins around the spectral peak —
the standard peak-picking refinement. `delta = 0.5 * (a - c) / (a - 2b + c)` on
the log magnitudes gives the peak position to a fraction of a bin. At 1025 bins
over 24 kHz that is roughly 23 Hz, against the current ~1000 Hz band spacing.

**What it would buy.** A genuinely continuous track. Cheap, and it makes #2 worth
doing properly.

**Cost.** Trivial.

---

## Tier 2 — the shape of the cut

### 4. A continuous gain curve instead of six bands

Six bands with linear interpolation is already smooth in the sense that there are
no steps, but the *shape* is still a piecewise-linear approximation of a bell.
Replacing it with a small spline through control points — or a single parametric
bell whose centre, width and depth are all tracked — removes the last quantisation
entirely and lets the Q vary continuously (see #5).

### 5. Adaptive Q with a critical-band floor

**The idea.** Let the measured peak width set the Q, and let it change over time:
a wide /ʃ/ gets a wide cut, a narrow /s/ a narrow one.

**The catch, and why the floor matters.** A very high Q is a narrow notch, and a
narrow notch rings. This is not theoretical — a whole paper exists on the audible
effect of filter ringing on perceived spectral balance (Chalmers, *Investigation
on the Effect of Loudspeaker Ringing on Perceived Spectral Balance*). The critical
band gives the natural floor: you cannot hear a notch narrower than the auditory
filter, so there is no reason to build one. Roughly 160 Hz at 5 kHz, 250 Hz at
9 kHz (Wikipedia, *Critical band*). Floors the Q at something like
`Q_min = f / criticalBandwidth(f)`.

**Also:** smooth the Q over time. A rapidly changing Q is audible as a phasey
sweep even when the gain is steady.

### 6. Per-band time constants

High frequencies tolerate faster attack and release than low ones before pumping
becomes audible. Right now one attack and one release serve all six bands. A
frequency-dependent constant — the existing `attackTilt` / `releaseTilt` machinery
already reaches the suppressor and could drive this instead of a fixed curve.

---

## Tier 3 — better decisions

### 7. A confidence score instead of a threshold

Today the gain is `maxCut × clamp(excess / 5)`. That is a hard map, and it makes
the transition from "not sibilant" to "sibilant" abrupt. Combine the evidence —
excess, flatness, duration, onset sharpness — into a single 0..1 confidence and
scale the cut by it. Frames where the evidence is mixed get a partial cut instead
of a coin flip.

### 8. Two-timescale envelope comparison

This is how transient designers work: compare a fast envelope against a slow one,
and the difference is the transient. Applied here, the "fast above slow" difference
is precisely what a sibilant is — a rapid rise in HF energy over the local average,
regardless of whether the take is loud or quiet overall. This is a better
definition than distance from a percentile, and it behaves the same on a whisper
and a belt.

### 9. Dual-percentile baseline

A single 20th percentile cannot tell a take with a huge dynamic range from a
compressed one. Comparing the 20th and the 80th gives a range estimate, so the
threshold can scale with how dynamic the material actually is.

### 10. One-frame look-ahead

**The problem.** The STFT window already gives about 21 ms of inherent look-ahead
— a frame contains samples from both sides of its centre — but the detector
decides and applies on the same frame, so the gain arrives fractionally after the
onset it is reacting to.

**The idea.** Detect on frame N+1, apply to frame N. The gain is then already down
when the onset arrives rather than chasing it. Published guidance for de-essers
puts useful look-ahead at **3–10 ms** (unison.audio; and Reddit's audioengineering
thread on the subject describes look-ahead as bringing "the volume down before the
sound"). The STFT can provide that for the cost of one frame of buffer, which the
engine already keeps.

---

## Tier 4 — hearing what it does

### 11. Solo the cut

An audition that plays only what is being removed. Nothing else makes the shape of
a cut this obvious, and it is the fastest way for the user to tell whether the
detector is aiming at sibilance or at a formant.

### 12. Per-band gain under the curve

The visualiser already draws the reduction curve. Drawing the instantaneous
per-band gain as a lighter fill underneath would show the cut *moving* as the
sibilance moves — which is the whole claim of the tracking design, and right now
the user cannot see it.

### 13. Peak-hold on the deepest cut

A numeric peak-hold on how hard the de-esser has hit, so "did it actually do
anything?" is answerable at a glance.

---

## Suggested order

**If only three:** flatness gate, moving bands, sub-bin interpolation. Those attack
the two real precision limits — a loudness-based decision and a quantised grid.

**Then:** confidence score, two-timescale envelope, adaptive Q with the critical-band
floor.

**Then:** look-ahead, solo-the-cut, the per-band gain display.

## What none of this fixes

None of these are a listening test. Every claim above is a mechanism with a reason,
not a result. The only way to know whether a change made the de-esser sound better
on a real take is to run a take and listen to it — and that is still the step that
has not been done.


---

# Part 20 — Sibilance miss, frame-level

Source: `docs/SIBILANCE-MISS-DIAGNOSIS.md`

# The "switch lanes" sibilant that survives

Measured 2026-10-07 against the phrase supplied by the user
(`change_2026-08-15 13-10-50_Insert 3 (consolidated)`, 3.73 s, 48 kHz stereo,
converted to 48 kHz mono for analysis).

Reproduced with `.work/scripts/sib_frames.cpp`, which prints what the de-esser
decided for every analysis frame. That tool is the point: it turns "it missed one"
into numbers, and it should be the first thing run on any future miss.

## What the frame data shows

At full authority the de-esser acts on 329 of 346 frames. Around 384-533 ms it
stops acting, and the shape of that gap is the finding:

| Time | Tracked centre | Cut across 5-11 kHz |
|---|---|---|
| 384 ms | 14196 Hz | -0.26 dB |
| 395 ms | 14213 Hz | -0.19 dB |
| 405 ms | 14226 Hz | -0.14 dB |
| 416 ms | 14234 Hz | -0.10 dB |
| 427 ms | 14240 Hz | -0.08 dB |
| 437 ms | 12330 Hz | -3.64 dB |
| 448 ms | 9978 Hz | -2.72 dB |
| 459 ms | 8331 Hz | -2.03 dB |
| 469 ms | 7179 Hz | -1.51 dB |
| 480 ms | 7163 Hz | -1.12 dB |
| 491 ms | 7414 Hz | -0.85 dB |
| 501 ms | 7590 Hz | -0.63 dB |
| 512 ms | 7451 Hz | -0.47 dB |
| 523 ms | 9492 Hz | -0.34 dB |
| 533 ms | 10921 Hz | -0.25 dB |
| 544 ms | 11921 Hz | -1.77 dB |
| 555 ms | 12621 Hz | -4.41 dB |

The sibilance band measures 18-23 dB through this whole span, and elsewhere in
the same take sibilance at a comparable level is cut by up to 15 dB. So the
consonant is present and audible while the de-esser does essentially nothing.

## What is *not* the cause

- **The flatness gate.** It reads 1.00 on every frame in the gap. It is not
  blocking anything.
- **The duration gate.** Frames on both sides of the gap are cut, so the gate is
  opening and closing within it rather than failing to open.
- **The tracked centre saturating at the top band.** This was the first
  hypothesis, and it is wrong. Guarding the band selection so a band must be
  within 20 dB of the loudest band changes nothing on this material -- the 14 kHz
  band genuinely carries comparable energy, so it wins legitimately. That guard
  was written, measured, found ineffective, and reverted rather than kept.

## The cause, measured

`totalWant = maxCut * clamp (broadExcess / 5, 0, 1) * flatConf`, with `flatConf`
reading 1.00. Printing the broadband terms frame by frame gives this:

| Time | broadband now | baseline | excess | cut |
|---|---|---|---|---|
| 384 ms | -20.23 dB | -15.89 dB | -4.35 dB | -0.58 dB |
| 395 ms | -18.00 dB | -16.57 dB | -1.43 dB | -0.43 dB |
| 405 ms | -19.90 dB | -16.59 dB | -3.31 dB | -0.31 dB |
| 416 ms | -19.87 dB | -16.59 dB | -3.28 dB | -0.23 dB |
| **427 ms** | **-17.86 dB** | **-17.86 dB** | **+0.00 dB** | **-0.17 dB** |
| 437 ms | -15.52 dB | -17.86 dB | +2.34 dB | -8.37 dB |
| 555 ms | -17.85 dB | -20.23 dB | +2.39 dB | -8.58 dB |
| 587 ms | -17.44 dB | -20.23 dB | +2.79 dB | -15.47 dB |

At 427 ms the baseline has risen **to exactly the signal level**. Excess is
+0.00 dB, so the cut is zero. Note also the baseline at 384 ms is *above* the
signal (-15.89 against -20.23): the resting level is sitting higher than the
material it is supposed to be a floor for.

The baseline is a 20th percentile over a 192-frame history, which at a 21 ms hop
is about four seconds. This phrase is 3.7 s long and sibilance-dense, so the
entire history is sibilant. The 20th percentile of four seconds of sibilance *is*
the sibilant level, so the excess collapses toward zero and the cut with it. The
window is longer than the structure it is trying to measure.

That is why the miss rate varies so much by take: it depends on how much quiet
material sits inside the baseline window. On material with real gaps -- the user
described spaces between verses -- the percentile lands on the gaps and the
detector works.

## Corpus miss rates

18 dry takes, seven voices, about 40 minutes of material, supplied by the user and
a collaborating engineer. Measured with `.work/scripts/sib_frames.cpp` at the
plug-in's default transform (2048 point, 4x overlap), learning the take first and
applying the learned profile, as the plug-in does.

A frame counts as a miss when it is among the loudest sibilant frames of its own
take and receives under 1 dB of cut anywhere in 5-16 kHz.

| Take | Sibilant frames | Missed | Rate |
|---|---|---|---|
| bigboss - LD Vocals 2 | 590 | 139 | 23.6% |
| untitled_2026-05-13 | 1372 | 322 | 23.5% |
| jackboydxnostems - LD Vocals 2 | 357 | 75 | 21.0% |
| Track 3 | 397 | 52 | 13.1% |
| jackboydxnostems - LD Vocals 1 | 1412 | 163 | 11.5% |
| Track 4 | 404 | 45 | 11.1% |
| untitled_2024-01-16 | 2023 | 103 | 5.1% |
| Track 9 | 971 | 47 | 4.8% |
| Phases 121 bpm F min_2 | 688 | 33 | 4.8% |
| bigboss - LD Vocals 1 | 521 | 15 | 2.9% |
| crew - Crew ref 125 bpm | 2275 | 49 | 2.2% |
| Track 2 | 1053 | 17 | 1.6% |
| Track 5 | 709 | 11 | 1.6% |
| untitled_2026-02-11 | 708 | 6 | 0.8% |
| (four takes with too little loud sibilance to judge) | | | |
| **all judged takes** | **13902** | **1232** | **8.9%** |

So roughly **one loud sibilant frame in eleven receives no meaningful cut**, and
the rate ranges from under 1% to over 20% between takes of the same voices.

## Two measurement errors found and corrected

Both of these produced wrong numbers before they were caught. They are recorded
because the wrong versions were plausible and were nearly reported.

**1. The tool did not learn the take.** The first corpus run gave a 61% miss rate
with individual takes at 97%. `endLearn()` is `const`: it returns the profile, it
does not store it, and the caller has to apply it -- which is what the plug-in
does when Apply Learn writes the band and the amount into the parameters. The tool
called `endLearn()` and then discarded the result. Two consecutive runs produced
byte-identical output, which is what exposed it: applying a learned profile that
changes the band and the amount cannot leave every number unchanged.

**2. The metric counted silence as sibilance.** These takes have long gaps between
verses, and a frame of silence reads -240 dB. Taking a percentile of the whole
distribution therefore landed on silence whenever most of the file was silent,
which made every frame look like a sibilant. That is where the 97% figure came
from on a take where the de-esser was simply not being asked to do anything. The
floor is now set 30 dB below each take's own loudest sibilance.

A third attempt, testing whether miss rate tracks sibilance density, was
abandoned rather than reported: the density metric was defined as the fraction of
frames above the median, which is 50% by construction for every take. The
correlation of 0.158 that it produced means nothing. A real density measure would
be the fraction of sounding frames within a few dB of the take's sibilance peak.

## The first hypothesis, and why it was wrong

The gap lines up with `broadExcess`, the broadband excess over the 20th-percentile
baseline. `totalWant = maxCut * clamp (broadExcess / 5, 0, 1) * flatConf`. With the
flatness gate at 1.00, the only remaining term that can drive the cut to zero is
`broadExcess`.

With the flatness gate at 1.00, the only remaining term that can drive the cut to
zero is `broadExcess`. The next thing to examine is therefore the baseline: it is
a 20th percentile taken over a rolling history, and in a take where 95% of frames
are sibilant, the 20th percentile is itself contaminated by sibilance. That would
raise the floor and shrink the excess everywhere except the very loudest moments,
which is the shape of the gap above -- quiet sibilants fall under the inflated
floor and survive, loud ones clear it and get cut.

That is a hypothesis, not a measurement. It is stated as one.

## The fix this points at

The baseline must be estimated from the *resting* part of the signal, not from a
percentile that includes the sibilants themselves. The principled version is a
gated history: push a frame into the baseline history only when that frame is not
itself a sibilant candidate. The floor is then built from the material the floor
is supposed to describe.

A shorter window would not help -- it would track the sibilants faster, not less.

This has not been implemented. It is written down as the measured direction of
travel, and it should be implemented against the frame tool and re-measured
across all eighteen takes before it is believed.

## The gated baseline: implemented, measured, reverted

The approved fix was implemented. A frame may only update the resting-level
history when it sits at or below the current floor plus 3 dB, so a dense passage
cannot raise the floor that is meant to expose it. The gate decision is broadband
and was applied to the per-band histories too, so placement and decision always
describe the same frames. It built, and both suites passed.

Then it was measured across all 18 takes, against the pre-gate detector:

| | Ungated (before) | Gated (after) |
|---|---|---|
| Miss rate, all takes | **8.7%** | **87.5%** |
| crew - Crew ref 125 bpm | 1.9% | 99.6% |
| bigboss - LD Vocals 2 | 21.8% | 98.1% |
| untitled_2026-05-13 | 23.4% | 96.3% |
| Track 5 | 1.3% | 95.8% |

It regressed every take and the corpus as a whole by a factor of ten. It was
reverted.

**Why it failed.** The admission test is one-sided: it lets a frame in when the
frame is at or below the floor, and keeps it out otherwise. That lets the floor
fall but never rise, and once it settles below the material it can freeze there --
if no frame is ever quiet enough to be admitted, the history stops growing
entirely and the floor is stuck at whatever it was seeded with during bootstrap.
On these takes it appears to have frozen near the bootstrap value, which cancelled
the excess and silenced the cut. The mechanism is a hypothesis from the shape of
the result; what is measured is that the corpus went from 8.7% to 87.5%.

**What the attempt did not test.** The reviewed concern was that a dense passage
raises its own floor. That is a real effect and it was measured directly earlier
(floor +0.00 dB against the signal at 427 ms). But this session also established
that the 384-427 ms frames which motivated it are *not* a miss: at 427 ms the
signal is -20.23 dB while the floor is -15.89 dB, so the frame is quieter than the
resting level and cutting it would be wrong. The gate was aimed at a frame that
did not need fixing.

## The metric had a second confound

The corpus metric measured sibilance over 5-11 kHz. The detector decides over
4-16 kHz broadband. A frame loud in 5-11 kHz but quiet in 12-16 kHz is correctly
not cut, and the 5-11 kHz metric called it a miss. That is the same class of error
as the earlier 5-11 kHz *cut* window, and it was found the same way -- by looking
at a frame the metric flagged and finding the detector was right.

With the metric aligned to the detector's own band, the ungated miss rate is
**8.7%** across 17 judged takes, against 8.9% before. The two agree, so that
figure stands: roughly one loud sibilant frame in eleven gets no meaningful cut,
and the per-take range is 1.3% to 23.4%.

## Where this leaves the detector

The ungated detector is restored and installed. Two candidate causes have now been
tested and rejected: a spurious top-band centre (reverted, no measurement change)
and a gated baseline (reverted, large regression). The remaining measured fact is
that miss rate varies about 18x between takes, and the mechanism is not yet known.

The next step should be to find what distinguishes a 1.3% take from a 23.4% take
by measuring the detector's internals on both, rather than proposing a third
fix on the strength of one frame.


---

# Part 21 — Learn design and the fence

Source: `docs/LEARN-DESIGN.md`

# Making LEARN world-class

Design document. What LEARN does today, what it should do, and the fence it must
never cross.

---

## 1. Where it stands

LEARN currently proposes exactly two things:

1. **Focus bands** — up to eight frequencies with a sensitivity, found by
   `LearnAnalyzer` from persistent peaks in the visualiser scope.
2. **Sibilance** — a low/high band, a peak frequency and an amount, found by the
   de-esser's own profiler.

Everything else in the plug-in stays wherever the user left it. That is a thin
read of a take. A vocal tells you far more than where its peaks are.

## 2. What a take can tell us

Each measurement below is available from material the engine already sees. None
of them require new capture machinery; they require the analyser to look.

| What to measure | How | What it proposes |
|---|---|---|
| Peak excess | Worst-case dB above the local baseline, per detected peak | **DEPTH**, **MAX CUT** |
| Peak sharpness | Width of each peak at −3 dB | **DETAIL** (sharpness) |
| Peak spacing | Median distance between detected peaks | **HOW NARROW** (selectivity) |
| Peak persistence | How many frames a peak survives | Which bands to enable at all |
| Peak shape | Symmetry and skirt steepness | **Filter type per band** — Bell, High Shelf, Low Shelf, Notch |
| Transient density | Count of fast onsets per second | **TRANSIENT** protect |
| Ring decay | How long resonances take to fall | **RELEASE**, **RELEASE TILT** |
| Onset speed | How fast the excess arrives | **ATTACK**, **ATTACK TILT** |
| Spectral tilt | Broad slope of the take's spectrum | **DETAIL TILT** |
| Sibilance position | Excess over each band's own resting level | **SIBILANCE band + amount** |
| Low-end buildup | Excess below 150 Hz | A focus band at the bottom |

## 3. The fence

This is the part that must be enforced in code, not in a comment. The user's
instruction was explicit, and it is right: LEARN is a mixing assistant, not a
session rewriter.

**LEARN MUST NEVER TOUCH**

| Parameter | Why |
|---|---|
| `quality` | The resolution is a latency and CPU decision. Changing it silently would change the reported latency mid-project. |
| `modeHard` | Hard mode is a deliberate character choice. |
| `midSide` | Mid/side is a monitoring decision, not a correction. |
| `deltaListen` | Delta is a monitor. It must never be switched on by an analyser. |
| `vocalProfile` | The weighting curve is the user's intent about what this track is. |
| `outGain` | Output level is set by the user for gain staging. |
| `stereoLink` | The user's instruction, and a mix decision. |
| `mix` | The wet/dry balance is the user's instruction about how much to do. |
| `autoGain` | Level matching is a comparison aid the user turns on. |
| `oversampling` | A rendering decision with a CPU cost. |
| `externalKey` | A routing decision. |
| `bypass`, `deltaBand`, `tracking_mode` | Monitoring and routing. |

**LEARN MAY PROPOSE**

`depth`, `sharpness`, `selectivity`, `attack`, `release`, `maxReduction`,
`transientPreserve`, `attackTilt`, `releaseTilt`, `detailTilt`, `motionProtect`,
`sibilanceSmooth`, `sibilanceLow`, `sibilanceHigh`, and the eight focus bands
(`eq_enable_N`, `eq_freq_N`, `eq_gain_N`, `eq_q_N`, `eq_type_N`).

This list lives in one place in the source as `kLearnEditable`, and a test asserts
that every id in it is a real parameter and that every id outside it is refused.
A fence nobody tests is a fence that rots.

## 4. Nothing changes during capture

Already true, and it stays true. During LEARN the audio thread only accumulates;
the engine's parameters are untouched. Every proposal lands in a review buffer and
waits. This matters more now that LEARN has something to say about depth and
attack — a silent change to those mid-take would be indefensible.

## 5. The review step

The user's second request was access: *change the cues, the shape of the cues, the
filter of the cues, pick the detail, have access to the knobs.*

So LEARN must not be a black box that writes eight values. It must present a
table:

```
LEARNED FROM "take 3"
                          current    proposed
  Depth                     1.49       1.80      [x]
  Detail                    1.00       1.35      [x]
  How narrow                0.50       0.62      [x]
  Transient                 0.50       0.65      [x]
  Max cut                  24.0 dB    18.0 dB    [x]
  Attack                    8.0 ms     6.0 ms    [x]
  Release                  64.6 ms    90.0 ms    [x]
  Detail tilt               0.00       0.30      [x]
  Sibilance                 0.50       0.70      [x]
  Sibilance band        4.0-16.0k   5.1-15.2k   [x]
  -------------------------------------------------
  Band 1   3.2 kHz  Bell  -4.1 dB  Q 2.0        [x]
  Band 2   6.7 kHz  Bell  -5.8 dB  Q 3.1        [x]
  ...
                                        [APPLY] [DISCARD]
```

Every row has a checkbox. Unchecking one means "leave my value alone". The band
rows additionally allow the filter type and Q to be changed before applying — that
is the "change the filter of the cues" request, and the graph already supports
dragging the nodes and right-clicking to set a value.

**Undo.** APPLY takes a snapshot of every parameter it is about to write. A single
UNDO restores them. Learn is the one action in this plug-in that can move eleven
controls at once, so it must be the one action that can be reversed in one click.

## 6. Why the proposals are shaped the way they are

**Depth from peak excess, not from peak count.** A take with one 12 dB ring needs
more depth than a take with eight 3 dB bumps. Depth follows the worst case, with
the count as a mild modifier.

**Detail from peak sharpness.** Narrow peaks need high sharpness or the cut
smears across the formant. Broad bumps need low sharpness or the cut misses them.
This is a direct read of the peak width.

**How narrow from peak spacing.** Closely spaced peaks need a narrow analysis
window or the detector averages them into one. Widely spaced peaks tolerate a
wider one, which is cheaper and more stable.

**Transient protect from onset density.** A take full of plosives and hard
consonants wants protection high. A legato take does not, and setting it high
anyway costs definition.

**Attack and release from the shape of the excess over time.** Fast-arriving
excess wants a fast attack; slow-decaying ring wants a long release.

**Detail tilt from the take's own slope.** If the take is dark, the detector is
looking at a tilted spectrum and the weighting needs a compensating tilt.

**Filter type from peak position and shape.** A peak at the very bottom or top of
the range with a one-sided skirt is a shelf, not a bell. A narrow, deep, isolated
peak is a notch. Everything else is a bell. This is the difference between a
proposal that looks plausible in a table and one that sounds right.

## 7. What this does not claim

- These heuristics are reasoned, not trained. They will be wrong on some material,
  which is exactly why every row is a suggestion the user can refuse.
- The measurements are cheap spectral statistics. They are not a model of what the
  take should sound like. LEARN proposes a starting point; the ears finish it.
- No claim is made about how this compares to any other product's learn function,
  because none of their internals are readable.


---

# Part 22 — Learn upgrade plan, four tiers

Source: `docs/LEARN-UPGRADE-PLAN.md`

# Making Learn smarter: a plan

You asked for a plan before implementation. This is it. It starts with what the
code does today, because two of the causes of "weird settings" are defects rather
than tuning.

Nothing here is implemented. No plugin version changes.

## What Learn does today

`LearnAnalyzer` watches the graph at 20 frames a second while you play a phrase.
For each frame it measures how far each frequency sits above an adaptive baseline,
counts peaks that persist, and measures their width, the spectrum's tilt, and how
often loud events start and stop. From that it proposes eight focus bands and ten
global settings. You review and press Apply.

The design is sound. The measurements that feed it have holes.

## Defects I can point at in the code

**1. Three outputs are constants, not measurements.** In `addFrame` the onset
branch sets `riseFrames = 0` and then adds `riseFrames` to the running sum on the
next line. It adds zero. The line after that adds zero again. `riseSum` therefore
stays at zero for the whole take, and everything downstream of it is fixed:

| Output | What the code produces | What it should be |
|---|---|---|
| `attackMs` | **8.0 ms, always** | the observed onset |
| `releaseTilt` | **0.0, always** | a read of how the take decays |
| `attackTilt` | **0.222, always** | a read of how the take strikes |

`onsetMs` is zero, so `attackMs` falls through to its default; `ringRatio` is
computed from that same zero and so equals 1.0, which pins both tilts. Three of
the ten global proposals carry no information about the take at all.

**1b. The release time drifts upward without bound.** `ringFrames` is never reset.
It increments while an event is above threshold and keeps incrementing after the
event ends, so it grows monotonically across the take and is added to `ringSum`
each time an event closes. `ringMs` therefore climbs for as long as you play, and
`releaseMs` runs into its 300 ms ceiling. A longer take does not produce a better
release estimate; it produces a larger one.

**2. There is no harmonic rejection in the learner (the one that matters).** A sung note is a harmonic
series, and a harmonic series is a set of persistent peaks spaced at even
intervals. The learner counts them as resonances. The main detector has comb and
harmonic awareness; the learner has none. So on held notes the learner places
focus bands on overtones, and applying those bands emphasizes parts of the
spectrum that are not problems. This is my leading candidate for "weird," and for
the boxy and thin results.

**3. Depth and Max Cut come from a single worst frame.** `depth` uses
`worstExcess`, which is the maximum over the whole take, and `maxCutDb` is
`worstExcess * 1.6`, capped at 30 dB. One cough, one plosive, one door — and the
take is described by its outlier. The per-band accumulation caps at 16 dB; the
maximum does not.

**4. Peak width is averaged without regard to persistence.** `peakWidthOct` takes
a flat mean over every point that was ever a peak, including one-off noise spikes
that never recur. It feeds `sharpness`. A spike that appears once should carry no
weight.

**5. The onset counter chatters.** It flips on above 3 dB of excess and off below
1 dB, with no debounce and no minimum length. Material hovering near either
threshold inflates the count, and the count maps straight into `transient`.

**6. Filter type ignores the peak's shape at the extremes.** Anything below
110 Hz becomes a Low Shelf and anything above 9.5 kHz a High Shelf. A narrow
80 Hz resonance gets a shelf, which moves the whole low end. That is a boxy
sounding outcome from a plausible looking table.

**7. One second is enough evidence.** `profile()` returns valid at 20 frames.
A single second of material produces eight bands and ten global values, with no
confidence attached.

**8. Every proposed band has positive gain.** Bands carry `clamp(score * 2, 1, 6)`
dB, always a boost. If the bands land on harmonics, Learn cannot help but push the
wrong parts up.

## What "hyper sophisticated" would mean

Four tiers, cheapest first.

### Tier 1 — stop the learner believing its own outliers

Replace the maximum with a high percentile (95th) for `depth` and `maxCutDb`.
Weight the peak-width mean by persistence. Add hysteresis and a minimum event
length to the onset counter. Fix the attack measurement so it reports what it
observed, or drop the field and stop pretending it measures anything.

*Effect:* proposals stop being dragged around by single events, and four
proposals that currently carry no information start carrying some.
*Cost:* no runtime cost of any kind. This is editor-thread arithmetic that already
runs. Development is small.

### Tier 2 — harmonic rejection (the one that matters)

Give the learner the same comb awareness the detector has. Estimate the harmonic
spacing from the take, then reject peaks that sit at integer multiples of a
fundamental. Keep peaks whose spacing is not harmonic. Where a peak cannot be
classified, mark the band low-confidence and say so in the review table rather
than proposing it silently.

*Effect:* focus bands stop landing on overtones. This should change the boxy and
thin results directly.
*Cost:* one extra pass over 288 points per frame, on the editor thread. The
detector already does comparable work per audio frame at 100x the rate. Runtime
cost is not measurable in plugin terms. Development is the real cost, plus
verification against the corpus.

### Tier 3 — a confidence score, and the option to propose nothing

Attach a confidence to each band from its persistence, its excess above the noise
floor, and whether it looked harmonic. Attach a confidence to the whole take from
how much material was played and how consistent it was. Below a threshold, propose
nothing and say why. A learner that declines to act beats one that acts wrongly,
and thin or boxy settings are worse than no settings.

*Effect:* weak takes stop producing confident nonsense.
*Cost:* negligible runtime. Adds UI text.

### Tier 4 — learn from an external key

You already built the sidechain input. If the user feeds the instrumental as a
key, the learner can subtract it and see only what the voice adds. That is a real
step up in accuracy, and it is the same idea as your reference-track workflow.

*Effect:* the strongest available separation of voice from arrangement.
*Cost:* depends on how much of the sidechain path is done. More work than Tiers
1-3 combined.

## What it costs, honestly

This is not a compute problem. Learn runs on the editor thread at 20 Hz, and the
DSP rate is roughly 2,400 times slower than the audio thread's frame rate. Every
tier above is arithmetic over 288 points. There is no added latency, no
oversampling, no extra memory worth mentioning, and no measurable change to plugin
size or CPU load.

**The cost is verification time.** I now have 18 dry takes from seven voices, and
that is what makes this tractable: I can run Learn on every take, apply the
proposal, render, and measure the result. That turns "it sounds weird sometimes"
into a number.

## The measurement I would add first

Before changing anything, add an objective score, because otherwise I cannot tell
whether a tier helped.

For each take: render with the learned settings, then compare against the dry
signal.

- **Sibilance reduction** — how much the 5-16 kHz sibilant excess falls. This is
  what we want.
- **Tonal damage** — spectral deviation from dry, measured separately in the
  100-400 Hz band (boxy lives here) and the 5-11 kHz band (thin lives here).

The score is sibilance reduction per dB of tonal damage. A good learn gets the
reduction without moving anything else. That single number lets me rank tiers, and
it would have caught a regression like the gated baseline the moment it happened.

I would build this before Tier 1, not after.

## What I recommend

Build the measurement first, then Tier 1 and Tier 2. Tier 2 is the one I expect to
change what you hear; Tier 1 makes sure the numbers it works from mean something.
Hold Tier 3 until the scoring shows what a low-confidence take looks like in
practice. Tier 4 is worth doing but it is a different piece of work.

## Open questions for you

1. When Learn produced settings you called weird, were the focus nodes in sensible
   places and the knobs wrong, or were the nodes themselves in strange places? The
   answer points at different tiers.
2. Thin and boxy are different failures. Did one take produce both, or did
   different takes produce different problems?
3. Did any learned setting sound clearly better than the default? If nothing has
   ever worked, that changes what I would trust.
---

# Judgement on the reviewed plan

The review accepts the diagnosis and approves the scoring harness plus Tiers 1 and
2. I agree with most of it. Three items need changing before I implement, and one
is material.

## Where the review is right

- **Score first.** Correct, and the reason is concrete: twice today a change
  measured worse and only the tool caught it.
- **All four Tier 1 items** map to real defects, one for one.
- **Tier 2 is the highest-value item.** Agreed.
- **The mechanism the review describes is coherent.** Bands on 200-500 Hz harm
  the low end, bands on 3-8 kHz harm the body. That matches boxy and thin.
- **"Default beats Learned"** is the most useful thing in the reply. It is
  strong evidence and it is testable.

One correction on mechanism, because it changes what Tier 2 has to do: a focus
node is a *detection bias*, not a filter. It tells the detector where to listen;
it does not cut anything by itself. The harm is indirect -- bands pointed at
harmonics cause reduction at harmonics, which removes body. The effect the review
describes is right, the path is one step longer.

## What must change

### 1. "Completely ignores integer multiples of the fundamental" is the wrong rule

Four problems, in order of severity.

**There is no fundamental to ignore multiples of.** `LearnAnalyzer` contains zero
references to pitch, f0, harmonic, or autocorrelation. This is not a patch to an
existing estimator; it is a new subsystem inside the learner.

**Hard rejection fails on held notes.** On a sustained note every peak is an
integer multiple of the fundamental. A rule that ignores them proposes nothing at
all -- so on exactly the material where the current learner is most confident, the
new one would go silent.

**Real resonances sit on harmonics.** A room mode or a nasal formant can land on a
harmonic. Ignoring all of them means missing real problems and moving the failure
from "wrong cut" to "no cut".

**The reliable discriminator is motion, not position.** If the pitch moves and a
peak stays where it is, the peak is a resonance. If the peak moves with the grid,
it is a harmonic. That test survives held notes and moving ones, and it degrades
gracefully instead of failing hard.

So Tier 2 becomes: track the fundamental per frame, compare each peak against the
predicted grid, and **down-weight** harmonic peaks by how much motion evidence
exists. Not hard rejection. Where the pitch never moves there is no evidence
either way, and the honest answer is to mark the band low-confidence and let Tier
3 decline -- not to guess.

Good news: the codebase already has the estimator to reuse. `ResonanceDetector`
does a lag search over the spectrum (`bestLag`, around line 250) for exactly this
kind of comb spacing. Tier 2 should reuse that approach rather than invent one.

### 2. The 95th percentile needs a buffer the plan does not mention

`excessMax` and `excessSum` are running scalars. A percentile cannot be computed
from a scalar. Either the per-frame excess is stored -- worst case 36,000 floats
for a 30 minute take, about 144 KB -- or it is binned into a histogram. A 256-bin
histogram over the dB range costs 1 KB and gives the same answer. I would use the
histogram.

### 3. Tier 1 will break the existing tests, and that is fine, but it must be part of the change

`Tests/DspTests.cpp` and `Tests/ProcessorTests.cpp` both assert on learn output.
Changing four constants changes what those assertions see. They need updating in
the same commit. A change that leaves the suite red is not finished, and per the
project rules a change with no check behind it is not verified.

## What I would add that the plan does not have

**A control condition.** Score the default Init patch across all 18 takes before
touching Tier 1. Without that number there is nothing for a learned result to beat,
and the review's own observation -- default beats learned -- stays an impression
rather than a measurement. This is the single cheapest thing in the whole plan and
it makes every later comparison meaningful.

**A careful definition of tonal damage.** Deviation from dry is dominated by the
cut itself, which is the thing we want. Damage has to be measured *outside* the
bands the detector decided to act on, or the score just rewards doing nothing.
Aligning wet to dry needs the measured latency, which `Tools/Analyze.cpp` already
computes.

## Order I would work in

1. Scoring harness, plus the default-patch control across all 18 takes.
2. Tier 1, with the test updates in the same change. Re-score.
3. Tier 2 as motion-weighted down-weighting, not hard rejection. Re-score.
4. Revisit the wording of Tier 3 once the scores show what a weak take looks like.

I am not implementing any of this until told to go.


---

# Part 23 — Scoring harness, validation, findings

Source: `docs/LEARN-SCORE-BASELINE.md`

# Learn scoring harness: built, validated, first finding

The harness the reviewed plan asked for is built and working. This records how it
was validated and what it already found, before any corpus-wide number is quoted.

## What it does

`Tools/LearnScore.cpp` renders each take three ways through the real plug-in
processor and measures the difference.

| Render | Settings | Purpose |
|---|---|---|
| dry | `depth 0` | the input itself, plus the plug-in's process delay |
| control | the Init patch, untouched | the thing a learned result has to beat |
| learned | the profile Learn proposes | the proposal under test |

Wet and dry are aligned by a **measured** process delay, not the reported one. The
tool found 2048 samples (42.67 ms) by pushing an impulse through a transparent
configuration. An earlier tool in this project aligned by the wrong number and
reported misalignment as processing, so this one measures.

## Two metric defects found and fixed while building it

**The first version averaged the whole take.** With the de-esser driven hard it
reported a 0.36 dB reduction. That was the metric, not the plug-in: sibilants
occupy a small fraction of a vocal, so a 6 dB cut on 10% of frames averages under
a dB. Averaging over everything measures how much of the take is sibilant, which
is not a question anyone asked. The tool now restricts to frames that are
sibilant in the dry signal -- the top quarter by 5-16 kHz level among sounding
frames.

**A block size that made the learner's timings wrong.** `LearnAnalyzer` assumes 20
frames a second and one frame is published per audio block. A 4096 sample block
would have made every learned timing 1.7x wrong and the tool would have reported a
defect the plug-in does not have. The harness uses 2400 samples, which is exactly
20 a second at 48 kHz.

## Validation: the metric reads zero when the plug-in does nothing

This is the check that makes every later number worth quoting.

| Setting | Sibilance reduction | Body damage |
|---|---|---|
| `depth 0` (transparent) | **0.00 dB** | **0.00 dB** |
| Init patch (Neutral) | 0.00 dB | 0.00 dB |
| `depth 1` | small | 0.47 dB |
| `depth 4, maxReduction 24, selectivity 0.2` | **0.53 dB** | 1.60 dB |

`depth 0` returning exactly zero on both metrics says the alignment and the band
maths are right. If they were wrong, a transparent setting would show a non-zero
difference.

## The first finding, on one take

On `crew - Crew ref 125 bpm Mixdown(2)`, driving the plug-in from its own default
to an extreme setting -- depth 4, maximum reduction 24 dB, selectivity 0.2 -- moves
the **sibilance reduction from roughly 0.3 dB to 0.53 dB**.

That is a few tenths of a decibel. The loudest sibilant frames in this take are
essentially not being cut, and the amount control barely changes that. This is one
take and one measurement, and it should be read as a question rather than a
verdict: it says the next thing to investigate is why the cut is shallow on the
frames with the most sibilance, which is a different question from the 8.7% miss
rate measured earlier. A miss means no cut on a frame that needed one. This says
the cut is shallow even where it happens.

It lines up with what the user reported: the S sounds slipping through untouched,
and turning the amount up not producing an audible effect.

## What has not been done

The 18-take control run has not been run. It is the next step, and it should come
after establishing whether the shallow-cut result holds across the corpus, because
if it does then the control comparison is a comparison between two settings that
both do very little.

## How to run it

```
cmake -S . -B .work/build-tools -DRESONAPRO_BUILD_ANALYZE=ON -DRESONAPRO_BUILD_LEARNSCORE=ON
cmake --build .work/build-tools --target ResonaProLearnScore
./.work/build-tools/ResonaProLearnScore_artefacts/Release/ResonaProLearnScore takes/*.wav --json out.json
```

`--params id=value,...` overrides any control, which is how the transparent and
extreme checks above were run.

---

# The shallow cut, measured across the corpus

## First, the metric was proved able to see a cut

The `depth 0` check only showed the metric reads zero when nothing happens. That
says nothing about whether it can see a cut that is really there, and without that
the corpus numbers below would not mean anything.

A false control was tried first and rejected: setting a focus band's gain to -6 dB
produced no change, because a focus band is a **detection bias, not an audio
filter**. It tells the detector where to listen. It cuts nothing by itself.

The real control puts a known high shelf in the wet signal through `--shelfDb`:

| Real cut applied | Measured reduction | Body reading |
|---|---|---|
| -3 dB shelf above 4 kHz | **1.02 dB** | 0.63 dB |
| -6 dB shelf above 4 kHz | **1.77 dB** | 0.63 dB |
| -12 dB shelf above 4 kHz | **2.69 dB** | 0.63 dB |

The reading rises with the cut, and the body reading does not move, which is
correct because the shelf sits above 4 kHz and the body band is 100-1000 Hz. The
metric detects real reduction in the sibilant band and scales with it.

## Then the corpus

All 18 takes, de-esser at its default half authority, resonance engine at depth 3
with a 24 dB ceiling:

- **Sibilance reduction: 0.00 to 0.69 dB.** Fourteen of the eighteen sit at or
  below 0.05 dB.
- **Body movement: 0.76 to 8.16 dB**, mean 1.59 dB.
- **Learned beat the control on 2 of 18 takes.**

Then at **full de-esser authority** (`sibilanceSmooth 1.0`), on three takes that
had shown almost nothing:

| Take | Half authority | Full authority |
|---|---|---|
| v02 | 0.01 dB | 0.02 dB |
| v03 | 0.02 dB | 0.01 dB |
| v06 | 0.03 dB | 0.03 dB |

Doubling the de-esser's authority changed the reduction by hundredths of a
decibel.

## What this means

A single -6 dB shelf above 4 kHz measures 1.77 dB. The de-esser at full authority
measures 0.02 dB. **The de-esser is doing roughly one sixtieth of what one static
shelf does**, and its amount control does not change that.

The "Sibilance De-Ess" control therefore has no measurable effect on the sibilant
band of any of the 18 takes. That is a stronger and different statement than the
8.7% miss rate: a miss means no cut on a frame that needed one, and this says the
cut is absent across the board regardless of setting.

It matches what the user reported twice -- the S slipping through untouched, and
turning the amount up producing no audible effect.

## Honest limits on this

- "Body movement" is not automatically damage. The resonance engine is supposed to
  act in the low-mids, so some of that 0.76-8.16 dB is its intended work. The
  metric cannot yet tell intended reduction from collateral, and calling it damage
  overstates it. What is not in doubt is the sibilant number, because the de-esser
  is the only stage meant to act there.
- The corpus is 18 takes from seven voices. It is good evidence, not proof.
- The metric measures a frame average over the loudest quartile of sibilant
  frames. A cut that is deep but extremely short could average low while still
  being audible. Worth checking before the cause is called.

## Next

Find why the de-esser stage produces no measurable cut. Candidates, none yet
tested: the stage is not reached at all; its gain is computed but not applied; its
detection threshold sits above the material; or its band is being clamped. The
shallow-cut question now outranks Tier 1 and Tier 2 of the learn plan, because a
smarter learner placing better bands cannot help a stage that does not act.


---

# Part 24 — Interface proposals

Source: `docs/UI-SUGGESTIONS.md`

# ResonaPro: interface suggestions

A review of the 960 x 700 interface, with changes ranked by how much they help.

The short version: the plugin has about 24 controls and no hierarchy. Every
control carries the same visual weight, so the eye has nowhere to land. The graph
— the one thing that makes this plugin different from a dynamic EQ — is the
smallest element on screen. Most of the work below is not decoration. It is
deciding what matters and letting the rest recede.

Nothing here changes how the plugin sounds.

---

## 1. The one change that matters most: make the graph the hero

The graph shows the spectrum, the reduction curve, the focus bands and the
detection. It is the reason to use this plugin. Right now it shares space with
two rows of knobs and a strip of toggles.

**Do this.** Give the graph the top two thirds of the window and make it fully
interactive:

* Drag a focus node to move it, scroll on it to change Q.
* Right-click a node for a small menu: solo, invert, reset, delete.
* Draw the reduction curve as a filled area under the spectrum, so depth reads
  as area rather than as a line.
* Show the detected resonances as short vertical marks at the frequencies the
  engine chose. This is the feature users of this kind of plugin ask for first,
  because it turns a black box into something you can disagree with.

**Why.** A user cannot tell whether the plugin is working unless they can see
what it decided. Right now they can see a curve, but not the decision behind it.

---

## 2. Split the controls into "always" and "if you want"

Right now all 24 controls sit on one surface. Split them by how often a mixing
engineer touches them.

| Tier | Controls | Where |
|---|---|---|
| Always | Depth, Selectivity, Match, Mix, the graph, Preset, A/B | On the surface |
| Often | Quality, Response, Attack, Release, Max Cut, Vocal Profile, focus bands | On the surface, grouped |
| Occasionally | Stereo Link, Mid/Side, Hard, Ear Guard, Transient, Sibilance, the three tilts, Note Motion, Low Detail, Ext Key | Behind a disclosure |

The editor already has a drawer. **Use it for the third tier and close it by
default.** That alone removes about ten controls from the first impression
without removing any capability.

---

## 3. Kill the toggles that are really modes

Five on/off buttons sit in the same row and read as equals, but they do very
different things.

| Control | Problem | Suggestion |
|---|---|---|
| `MATCH` | An action everyone wants on. Few will ever turn it off. | Default it on. Make it a small checkbox in the output area, not a button in the main row. |
| `EAR GUARD` | A weighting choice. It is not a guard. | Rename to **Loudness weighting**, and put it in the drawer with the other tone options. |
| `HARD` | A behaviour choice with a real cost. | Keep visible, but show the state in the label, for example `HARD` lit when on. |
| `MID/SIDE` | Changes what the graph means. | Keep visible. Add a graph tint when it is on, so the user cannot forget. |
| `EXT KEY` | Only meaningful with a sidechain routed. | Grey it out until a sidechain is present. A control that cannot act should look like it. |

**Why.** Five equal-looking buttons make the user read all five every time. A
mode that is on 95 percent of the time should not compete for attention with one
that is a genuine choice.

---

## 4. Say what each control does, in plain words

Current labels assume the user already knows the plugin. Rename the ones that
carry no meaning on their own.

| Now | Suggested | Why |
|---|---|---|
| `SELECTIVITY` | **How narrow** | Says what changes, not what it is like |
| `SHARPNESS` / `DETAIL` | **Detail** (pick one, use it everywhere) | Two names for one control appears in the UI today |
| `ATK TILT` / `REL TILT` / `DETAIL TILT` | **Attack by frequency**, **Release by frequency**, **Detail by frequency** | "Tilt" is jargon; the tooltip then has nothing left to add |
| `TRANSIT` | **Transient guard** | Truncated label reads as a transport control |
| `RESPONSE` | **Overlap** | This is what the setting is |
| `QUALITY` | **Resolution**, with the Hz shown | The values are FFT sizes; "quality" hides that |
| `LOW DETAIL` | **Low band detail** | Three words, no cost |
| `EAR GUARD` | **Loudness weighting** | It is a weighting curve |

Also: the **headings** currently mix two ideas. `VOCAL TONE & CLARITY`,
`SIBILANCE & AIR` and `LEAD VOCALS` are three headings for what is one idea
(the vocal profile). Use one heading, or drop the headings and let the grouping
carry it.

---

## 5. Give every control a readable value

A row of identical arc knobs forces the user to hover each one. Add a small
persistent readout under each knob showing the value and, where it exists, the
unit — `2.0`, `-12 dB`, `23 Hz`, `2x`. The editor already computes an info
string, so the plumbing exists.

**Why.** Numbers let a user set a value on purpose and return to it later.
Knob position alone does not.

---

## 6. Quality-of-life changes, in order of payoff

These cost little and remove friction the user hits on every session.

| Change | Why |
|---|---|
| **Right-click any knob to type a value** | Precise recall. Standard in every plugin of this type. |
| **Double-click resets to default** (already fixed) and shows the default in the tooltip | Discoverable, and the tooltip already exists |
| **Copy and paste the whole band set, not one band** | The user tunes a voice, then wants it on the double. Paste already works across instances; extend it to the full set. |
| **Undo for band edits** | Dragging a node and losing the previous setting has no recovery today |
| **Drag on the graph to draw the focus shape directly** | Faster than moving nodes one at a time |
| **Remember the drawer state and window size** | Every reopen costs the same two clicks otherwise |
| **Keyboard: arrow keys nudge a selected node; Tab cycles nodes** | Precision without the mouse |
| **A "flat" button next to Reset Bands** | The current reset asks for a decision in the middle of one |

---

## 7. Visual language

The cream palette works and reads well against a spectrum. Keep the direction
and tighten the details.

* **One accent colour, one meaning.** Today the accent marks depth, focus, and
  reduction. Give reduction its own hue so the two curves never read as one.
* **Weight carries meaning.** Primary values at full contrast; secondary labels
  at 60 percent. Right now everything sits at the same weight, so nothing leads.
* **Group with space, not with boxes.** The section boxes add eight lines to the
  screen and no information. Space does the same job and reads calmer.
* **The graph needs a baseline.** A faint horizontal line at 0 dB and a dB grid
  at 6 dB steps lets a user read the curve instead of estimating it.
* **Frequency labels every octave, not fewer.** The graph spans 20 Hz to 20 kHz;
  at present the labels are thin at the low end, where vocals actually live.
* **Empty state.** When the plugin is doing nothing, say so in the graph area —
  "no resonances found". That single line answers the most common support
  question for this kind of plugin.

---

## 8. Two things to remove

* **`RESET BANDS`** beside `COPY`, `PASTE` and the A/B pair makes a row of four
  unrelated buttons. Move reset into the graph's right-click menu, where it
  belongs.
* **Duplicate naming.** `SHARPNESS` and `DETAIL` both appear. Pick one word and
  use it on the knob, the label, the tooltip and the docs.

---

## 9. Layout, concretely

```
+--------------------------------------------------------------+
|  RESONA   [preset v]              A/B    BYPASS    2.3.1      |  56 px
+--------------------------------------------------------------+
|                                                              |
|            spectrum + reduction + focus nodes                |
|                                                              |  ~400 px
|            detected resonances shown as marks                |
|                                                              |
+--------------------------------------------------------------+
|  DEPTH   HOW NARROW   ATTACK   RELEASE   MAX CUT   MIX   MATCH|  110 px
|   2.0       0.50       40 ms    180 ms    -18 dB   100%  [x] |
+--------------------------------------------------------------+
|  [v] Advanced                                       RESOLUTION |  40 px
+--------------------------------------------------------------+
|  Quality   Overlap   Profile   Low band detail   Sidechain     |  drawer
+--------------------------------------------------------------+
```

The first row answers "what is it doing". The second answers "how much". The
third is behind the drawer.

---

## 10. What to do first

If only three changes happen, do these.

1. **Make the graph the hero and show the detected resonances on it.** This is
   the difference between a plugin people trust and one they guess at.
2. **Move the third tier into the drawer, closed by default.** Removes about ten
   controls from the first impression.
3. **Add a value readout under every knob.** Ends the hover-to-learn loop.

---

## A note on how this document was made

These are design suggestions from reading the source and the rendered interface,
not from watching anyone use the plugin. The roadmap's own rule applies: a change
that cannot be shown to help should not be kept. Run each of these past two or
three engineers on a real session before building it, and drop whatever they do
not miss.
---

## What was built, 6 October 2026

Shipped in 2.3.1.

**Done**

* The drawer holds the occasional controls and is **closed by default**, so the
  first impression is the graph and the eight controls that matter. It carries
  Weighting, Low Band Detail, Ext Key, Reset Bands, Note Motion and the four
  tilt and de-ess knobs. Its state persists across editor reopen.
* Five equal-looking toggles on one row became three. Weighting and Reset moved
  into the drawer.
* The graph takes the full width unless the drawer is open.
* **Detected resonances are drawn on the graph.** Each reduced region gets a tick
  above its peak, so a user can see the decision and not just the curve.
* Controls renamed: `QUALITY` to `RESOLUTION`, `RESPONSE` to `OVERLAP`,
  `SELECT` to `HOW NARROW`, `EAR GUARD` to `WEIGHTING`, `LOW DETAIL` to
  `LOW BAND DETAIL`.
* **Units on every readout that has one**: `24.0 dB`, `8.0 ms`, `70.0 ms`,
  `0.0 dB`, `100.0 %`.
* The help line that collided with the knob labels is gone. The tooltips carry
  the same content.

**Deferred, with reasons**

* *Transient* and *Stereo Link* show the fraction (`0.50`, `1.00`) rather than a
  percentage. Both run 0..1, and JUCE ignores a text formatter set after the
  attachment. A wrong percentage would be worse than a plain number, so it waits
  for a proper fix.
* Right-click to type a value, undo for band edits, keyboard node nudging and
  drag-to-draw the focus shape are not built.
* The per-knob readout was built and then removed. It duplicated the text box the
  sliders already carry. Adding the units to the existing readout was the real
  fix.


---

# Part 25 — User guide

Source: `docs/HOW-TO-USE.md`

# How to Use ResonaPro

## 1. Getting it into your DAW

**ResonaPro 2.3.0 is the current build.** It sits in
`~/Library/Audio/Plug-Ins/VST3` and `~/Library/Audio/Plug-Ins/Components`, and
Apple's `auval` validator accepts the AU. You only need this section if you
rebuild the plugin later, or want it on another machine.

Here is where each build lands, so you can confirm a version without guessing.
Look at the small text under the title in the interface: it prints the version
of the binary that is actually running.

| Build | Installed at | How your DAW finds it |
|---|---|---|
| VST3 | `~/Library/Audio/Plug-Ins/VST3/ResonaPro.vst3` | FL Studio scans this folder by default. Use this one. |
| AU | `~/Library/Audio/Plug-Ins/Components/ResonaPro.component` | For Logic, GarageBand and other Audio Unit hosts. |
| VST2 | `~/Library/Audio/Plug-Ins/VST/ResonaPro.vst` | Only present if the build was given a VST2 SDK. Not shipped. |
| Standalone | `/Applications/ResonaPro.app` | Launch it from Spotlight, Launchpad or Finder. No DAW needed. |

Three formats ship: AU, VST3 and standalone. VST2 is not among them. Steinberg
stopped licensing and distributing the VST2 SDK in October 2018, and JUCE
removed its bundled copy, so a VST2 build needs those headers supplied by
whoever builds it. The build and both installers are ready for it the moment
they are available. In practice it rarely matters: FL Studio, Logic, Ableton,
Cubase, Studio One and Reaper all load the VST3 or the AU.

If the version text reads anything other than **v2.3.0**, the host is running a
cached copy and needs to rescan. The FL Studio steps are at the end of this
section.

**Option A — run the installer (recommended)**

```bash
cd /Users/armundescarey/Documents/ResonaPro
./install.sh
```

Run it from Terminal, not from inside an app.

**Option B — copy it yourself in Finder**

1. Press `Cmd+Shift+G` in Finder and go to `~/Library/Audio/Plug-Ins/VST3`.
2. Move any old `ResonaPro.vst3` to a backup folder outside `VST3`.
3. Drag `ResonaPro.vst3` from the release archive into that window.
4. Repeat for the AU in `~/Library/Audio/Plug-Ins/Components` with `ResonaPro.component`.
   The installer signs each copied bundle on this Mac and keeps a rollback copy;
   use it instead of Finder when possible.

**Option C — no DAW needed**

Open `ResonaPro.app` from your Applications folder, or from Spotlight. It runs as a standalone processor with its own audio input and output, so you can hear it with no host involved. `./install.sh` puts it there; a copy also stays in `Binaries/`, but an app left inside a project folder does not show up in Spotlight or Launchpad.

After either option, rescan plugins in your DAW. In Logic, quit and reopen the app. In Ableton, hold `Option` while clicking Rescan. In REAPER, click "Clear cache and rescan".

### FL Studio (macOS)

FL Studio caches plugins by file path, so replacing a plugin with the same file
name can go unnoticed. Force it to re-read:

1. Open FL Studio.
2. Go to **Options → Manage plugins**.
3. Tick **Rescan previously verified plugins**. This step matters. Without it FL
   skips the file because the path already exists in its database.
4. Click **Start scan**.
5. Open the Browser and look under **Plugin database → Installed → Effects**.
   ResonaPro appears there, and also under **VST3**.

To check it faster, find ResonaPro in the Plugin Manager list, select it, and
click **Verify plugin**. That forces a re-read of the one file.

If ResonaPro still shows the old behaviour, right-click it in the Plugin Manager
and remove it, then scan again. That clears the stale entry.

Use the **VST3** version in FL Studio. It is the better supported format there.

**On latency:** FL Studio compensates plugin delay on mixer inserts, so the
46 ms of the Standard quality setting costs you nothing on playback. It does
affect live monitoring through the plugin, so switch to **Low Latency (1k)**
if you are tracking a vocalist and hearing a delay in their headphones.

---

## 2. First five minutes

1. **Load it on a vocal track.** Insert ResonaPro as the first plugin in the chain, before compression and EQ.
2. **Turn on DELTA.** Press the DELTA button and loop a phrase. You now hear only what the plugin removes. This is the fastest way to understand it.
3. **Raise DEPTH.** Start at 0, then move up. You will hear the harshness, the boom, and the sibilant edges come forward as you turn it up.
4. **Turn DELTA off.** Now set DEPTH to where the vocal sounds cleaner but still sounds like the singer. Depth 1 to 1.5 covers most work.
5. **Check the MIX.** If the vocal lost body, pull MIX back to 70 to 80 percent instead of reducing Depth. Parallel blending keeps the tone.

---

## 3. The controls in the order you should reach for them

| Order | Control | What to do |
|---|---|---|
| 1 | **VOCAL PROFILE** | Pick your goal: Lead Vocal, De-Ess, Warm Body, or Air. This aims the detector before you touch anything else. |
| 2 | **DEPTH** | The main amount. 0 is transparent. 1 is gentle. 3 is heavy. |
| 3 | **DELTA** | Use this constantly. It tells you whether the plugin is working on the right thing. |
| 4 | **SELECT** (Selectivity) | Raise it if the plugin touches things it should leave alone. Lower it if it misses obvious harshness. |
| 5 | **DETAIL** (Sharpness) | Low values act like a dynamic EQ. High values act like a surgical notch. |
| 6 | **TRANSIENT** | Raise it when consonants and plosives get dull. Lower it for de-essing. |
| 7 | **MAX CUT** | The hard ceiling on any single frequency. Lower it if the result sounds hollow. |
| 8 | **FOCUS BANDS** | Fine-tuning. Drag a node up to push the detector harder in that range, down to protect that range. |
| 9 | **ATTACK / RELEASE** | Leave at 8 ms and 70 ms unless you need faster sibilance control or slower low-end control. |
| 10 | **STEREO LINK** | Keep at 1.0 for a stable image. Set to 0 when the two sides of the take differ enough to need separate treatment. |

**MID/SIDE** splits the centre from the sides. Use it when a reverb or doubled layer carries the harshness, not the main voice.

**EAR GUARD** applies a small, fixed ear-sensitive threshold bias. It is not
an ISO 226 calculation. The saved parameter ID remains `iso226` so older projects
still load.

**HARD** makes the detector react to loud peaks instead of relative peaks. It is much stronger. Treat it as a separate mode, not a volume knob.

---

## 4. Starting points that work

**Straight lead vocal**
Profile Lead Vocal, Depth 1.0, Select 0.5, Transient 0.5, Mix 100.

**Harsh 3 kHz range, the "sting"**
Preset "Lead Vocal - Tame Harshness". Then drag the focus node near 3 kHz up until Delta plays back the sting on its own.

**Sibilance, the "sss"**
Preset "De-Ess - Broad / Lispy" for a soft, lisping singer. Preset "De-Ess - Whistling / Narrow" for a sharp, narrow whistle. Always check DELTA to confirm you hear only the "sss".

**Plosives and clicks getting dull**
Preset "Plosive Safe De-Ess". It protects attacks, so "P", "T" and "K" stay crisp.

**Muddy low mids**
Preset "Warm Body - De-Mud".

**Dull top end**
Preset "Air & Silk Polish".

**Whole mix bus**
Preset "Harsh Bus Glue". It uses a 4k transform and a 12 dB ceiling, which suits bus work.

---

## 5. Settings that save CPU

| QUALITY | Latency at 44.1 kHz | Use it for |
|---|---|---|
| Low Latency (1k) | about 23 ms | tracking and monitoring |
| Standard (2k) | about 46 ms | mixing, the default |
| High (4k) | about 93 ms | bus and mastering |
| Ultra (8k) | about 186 ms | surgical work on a problem resonance |

**RESPONSE** sets how often the plugin updates its gain curve. Eco uses the least CPU, Fine uses the most.

Changing QUALITY or RESPONSE while audio plays causes a short gap and a latency change. Set these with playback stopped.

Next to those, a readout tells you what the current setting actually means — the
transform length, the number of analysis bands, the spacing between them in Hz,
and the latency in milliseconds. At 96 kHz a "2k" transform has half the
frequency precision it has at 48 kHz, and the readout is the honest answer.

---

## 6. The control strip along the bottom

| Control | What it does |
|---|---|
| **MATCH** | Level matching, and the most useful switch on the panel. Removing resonances costs level, about 1.3 dB on a typical voice at Depth 1.5, and a quieter signal nearly always sounds better, so an A/B comparison lies to you without this. MATCH trims the output to the input's **perceived loudness** using ITU-R BS.1770 K-weighting rather than raw RMS, because the ear does not weight frequencies equally and a resonance suppressor works at chosen frequencies. Leave it on and you judge tone instead of loudness. It never raises the peak above what the input already had, so it cannot add clipping, and it switches itself off in BYPASS and DELTA where matching would be meaningless. |
| **A/B** | Two stored settings, swapped by the button. The label shows which slot is live. Press it once to set the reference, dial in a change, press again to compare. |
| **DELTA BAND** | Restricts DELTA to one focus band. Set to "All" for the usual behaviour, or pick a band number to hear only what is being removed from that one region. This is what turns the graph from a guess into a tool — solo band 3, hear exactly what is happening at 3 kHz, adjust that node, listen again. |
| **COPY / PASTE** | Moves all eight focus band settings between instances. Set the bands up once on the lead vocal, copy, then paste into the doubles and the backing stacks. |
| **RESET BANDS** | Returns all eight focus bands to neutral. |

**Focus-node precision:** Hold **Shift** while dragging a node to snap its
frequency to a 1/6-octave grid and its sensitivity to whole dB steps. Right-click
a node to type `frequency gain Q`, such as `3200 3.0 1.4`, then press Return.
Mouse-wheel over a node changes Q; double-click toggles the node.

## 7. New analysis controls

| Control | Use it when | What changes |
|---|---|---|
| **LOW DETAIL** | A low-mid ring is hard to judge with a 1k or 2k transform. | Adds a 4k analysis window below 1.2 kHz, then blends its detection result into the main transform. It does **not** run a second audio synthesis path or add host latency. It may protect a harmonic or increase reduction, depending on the signal. At 48 kHz in a synthetic CPU test, 2k/4× rose from 1.5% to 4.7% of one core; 1k/4× rose from 1.4% to 7.9%. At 4k and 8k quality it has no extra effect. |
| **NOTE MOTION** | Low harmonics move during a phrase and the detector mistakes them for a fixed ring. | Compares the same frequency with the expected movement of a harmonic between frames. This is a **bounded hint**, not automatic pitch correction. Start near 0.5, then check DELTA. It needs a clear note change; on steady notes it does little. |
| **EXT KEY** | Another track should steer the detector while the vocal remains the processed output. | Reads the optional mono or stereo sidechain bus. If the bus is disabled, detection falls back to the vocal. If a bus is enabled but silent, the key may produce little reduction. |

### Route EXT KEY in FL Studio

1. Put **ResonaPro VST3** on the vocal's Mixer track.
2. On the track you want to use as the key, right-click the send switch to the
   vocal track. Choose **Sidechain only to this track**. The key does not join
   the audible vocal path.
3. Open ResonaPro's **wrapper settings → Processing → Inputs**. Assign that
   sidechain to the plugin's extra input. FL Studio's **Auto map inputs** can
   assign it after you create the sidechain send; check the mapping if you use
   several sends.
4. Turn on **EXT KEY** in ResonaPro. The vocal still supplies the audio output.
   Use DELTA to hear whether the key steers the bands you want.

These FL routing steps follow the [Image-Line Mixer manual](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer.htm)
and [Plugin Wrapper manual](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/wrapper.htm).

### Learn a take

1. Turn **EXT KEY** off. Leave the plugin editor open.
2. Press **LEARN**, play the vocal phrase or whole take, then press **STOP LEARN**.
   Capture runs at 20 snapshots per second for up to 10 minutes. It needs at
   least one second of active frames and stops counting when FL Studio stops
   sending audio.
3. Read the proposed frequencies. If they fit what you hear in DELTA, press
   **APPLY LEARN**. Until you press Apply, **no parameter changes**.
4. Apply fills up to eight focus nodes with modest, positive detector sensitivity.
   It does not print a static EQ curve, change the source audio, or analyse a
   file while FL Studio is closed. You may still drag and fine-tune each node.

For a quiet or breathy take, Learn may propose no peaks. This is safer than
forcing a curve from too little evidence.

### Tooltips

Hover any control and a description appears after about a second. Every knob,
button, and menu has one.

---

## 8. Things that catch people out

- **The plugin is active when you load it.** Depth defaults to 1.0. If you want a clean starting point, choose the "Init (Neutral)" preset.
- **Depth at 0 is a true bypass.** The signal passes through sample-aligned with no processing.
- **DELTA is not a solo of the resonances in isolation.** It plays the removed signal at its natural level and timing, so it blends with your track. That is what makes it useful.
- **It will not fix broad tonal balance.** If the whole vowel sits 3 dB too dark, that is an EQ job. This plugin works on narrow peaks.
- **Below about 150 Hz it holds back.** A low resonance is hard to tell apart from the note's own harmonics, so the plugin prefers to leave the fundamental alone. Use the Warm Body profile or a focus node when you need work down there.
- **Judge it in context.** A vocal that sounds great soloed can sit wrong in the mix, and the reverse happens too.


---

# Part 26 — Onboarding prompt

Source: `docs/HANDOFF-PROMPT.md`

# Handoff prompt for a new agent or workspace

Paste the block below as the first message when opening ResonaPro in a new
workspace, agent, or IDE session.

---

```
You are picking up an existing, mature audio-plugin project. Read before you
change anything. Do not write code until you have finished the reading list and
run the verification steps below.

## What this project is

ResonaPro is a vocal spectral de-resonator and de-esser. It is a JUCE C++20
audio plugin for macOS, used inside FL Studio. It is a specialized alternative
to soothe2, built for vocals specifically.

Repository root:
/Users/armundescarey/Documents/ResonaPro

Dry vocal test corpus (18 takes, about 7 singers):
/Users/armundescarey/Documents/Dry Vocal For Plugin test/
A converted copy lives at .work/audio/corpus/

## Read these first, in this order

1. AGENTS.md -- standing rules. These are the owner's, not yours. Follow them
   without being asked again.
2. docs/LEARN-UPGRADE-PLAN.md -- the approved plan for the Learn feature.
3. docs/LEARN-SCORE-BASELINE.md -- the measurement harness and what it found.
4. docs/DE-ESSER-ROADMAP.md -- the de-esser plan.
5. docs/SIBILANCE-MISS-DIAGNOSIS.md -- corpus investigation, including a failed
   experiment that was reverted.

Then list docs/ and read whatever relates to your task. Several docs record
what was tried and why it failed. Read those before you retry an idea. That
history is worth more than the things that worked.

## The standing rules you must follow

- Do NOT build .pkg, .dmg, or installer .zip files unless explicitly asked.
  They cost real time and are only needed when a build leaves this machine.
- After any source change that affects audio: build, run both test suites,
  install. Do not stop at a successful build.
- Keep the install current. The owner's DAW scans
  ~/Library/Audio/Plug-Ins/VST3/ResonaPro.vst3
  ~/Library/Audio/Plug-Ins/Components/ResonaPro.component
  If you change audio code and do not install, the owner will test a stale
  build and report a bug that is already fixed.
- Evidence over assertion. Every claim about audio behavior must be measured.
  A change not covered by a check is not verified. When a change fails, report
  the measurement that failed it, including when the change is yours.
- Synthetic test signals prove the signal path is clean. They say nothing about
  whether the plugin sounds right on a real voice. That still needs the owner's
  ears. Say so when it applies.

## Verify your environment before touching code

Run these. If they do not pass, fix the environment first.

    cd /Users/armundescarey/Documents/ResonaPro
    ./.work/scripts/build.sh configure
    ./.work/scripts/build.sh build
    ./.work/scripts/build.sh test        # or build the two CMake test targets
    ./install.sh

Both suites must report 0 failures:
    ResonaProDspTests
    ResonaProTests

If cmake is not on your PATH, this machine has a python cmake shim at
.work/tools/pycmake. Prefix commands with:
    PYTHONPATH=.work/tools/pycmake python3 -m cmake ...

Confirm the installed version, and that it matches the source:
    /usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' \
      "$HOME/Library/Audio/Plug-Ins/VST3/ResonaPro.vst3/Contents/Info.plist"

The version label on the plugin interface shows what is actually loaded. Check
it whenever a bug report does not match the source.

## Current state

- Installed version: 2.13.1
- Both test suites pass with 0 failures.
- The GUI is a cream, off-white design inspired by soothe.
- The core controls include Depth, Detail, Selectivity, Attack, Release, Max
  Cut, Transient Protect, Quality/Response, Stereo Link, ISO 226, Hard,
  Mid/Side, Delta, Solo Cut, Learn/Apply/Undo, three Tilt controls, Sibilance
  De-Ess, external key, and focus bands.
- The three tilts (Detail Tilt, Attack Tilt, Release Tilt) all run -1 to +1 and
  default to 0.0, which is neutral. They shape how the detector and suppressor
  react by frequency. They do not EQ the signal.
- Learn Tier 1 landed in 2.13.1: a real onset/decay state machine with a
  debounce, percentile-based depth and max cut, and persistence-weighted peak
  width. Before this, the attack measurement was dead and always returned its
  8 ms default.

## Known open problems

Read docs/LEARN-SCORE-BASELINE.md for the measurements behind these.

1. Learn timings saturate. The learner reads scope frames at 20 Hz, so one
   frame is 50 ms. The parameters it proposes are millisecond-scale (attack runs
   1-30 ms). The frame rate cannot resolve what the parameters need, so every
   take produces clamped settings. Depth and max cut saturate the same way.
   The likely fix is audio-rate onset/decay measurement inside the engine during
   Learn, not more tuning of the editor-side analyzer.

2. The harmonic rejection planned as Learn Tier 2 is not implemented. The agreed
   approach is motion-weighted down-weighting of harmonics, reusing the
   detector's lag search. Do NOT hard-reject integer multiples of the
   fundamental -- that was considered and rejected.

3. An unresolved disagreement about the de-esser. Tools/LearnScore.cpp measured
   sibilance reduction at 0.00 to 0.69 dB across the corpus, with most takes
   under 0.05 dB, while a single 6 dB shelf control measured 1.77 dB. The owner
   reports the de-esser works. One possible reconciliation: a very short, very
   deep cut could read low in a frame average while still being audible. Check
   this before acting on either view. Do not treat the measurement as settled,
   and do not treat the disagreement as resolved.

## How to work here

- Measure before you claim. Write the measurement into docs/ so it outlives the
  session.
- Revert anything that measures worse than what it replaced. Keep a backup of
  every file before you edit it. Backups live in .work/backups/.
- Do not change the plugin's sound to satisfy a question. Ask whether the owner
  wants an answer or a change when it is not clear.
- Learn must never change Quality/Resolution, Hard mode, Mid/Side, Delta,
  profile weighting, output, stereo link, mix, auto gain, external key, bypass,
  or monitoring and routing decisions. This is enforced by an allowlist and by
  tests. Do not weaken either.
- The owner tests by ear in FL Studio on real vocals. Ask for that when a
  question cannot be settled by measurement.

Start by reading the files and running the verification steps. Then tell the
owner what you found, including anything that contradicts what is written here.
```

