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
