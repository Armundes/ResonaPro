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
