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
