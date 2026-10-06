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