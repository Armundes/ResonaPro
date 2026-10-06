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