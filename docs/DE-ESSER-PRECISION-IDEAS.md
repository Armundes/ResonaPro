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