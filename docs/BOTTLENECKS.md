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