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
