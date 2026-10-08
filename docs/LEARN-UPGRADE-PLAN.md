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
