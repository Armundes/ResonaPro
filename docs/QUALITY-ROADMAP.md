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
