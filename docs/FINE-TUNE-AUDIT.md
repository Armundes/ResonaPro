# FINE TUNE drawer — audit and plan

Measured on 2026-10-07 against build 2.5.0. Every figure below comes from the
file-based analyser (`Tools/Analyze.cpp`) on two synthetic takes: a formant voice
and a sibilant take with gated 5–11 kHz consonants.

## The complaint

> "I'm not really hearing an effect when I turn the sibilance up in the fine-tune
> tab."

Confirmed. The whole drawer is close to inert.

## What each control measures

Level change from the control's minimum to its maximum, depth 2, Standard (2k)
resolution, 4x overlap, auto-gain off.

| Control | Parameter | Min | Max | Change | Verdict |
|---|---|---|---|---|---|
| Sibilance De-Ess | `sibilanceSmooth` | −0.87 dB | −0.99 dB | **0.12 dB** | too weak to hear |
| Attack Tilt | `attackTilt` | −0.92 dB | −0.90 dB | **0.02 dB** | not shown by this test |
| Release Tilt | `releaseTilt` | −0.92 dB | −0.92 dB | **0.00 dB** | not shown by this test |
| Detail Tilt | `detailTilt` | −0.92 dB | −0.92 dB | **0.00 dB** | genuinely dead |
| Note Motion | `motionProtect` | −0.92 dB | −0.92 dB | **0.00 dB** | not shown by this test |

Two of these need a different test before they can be judged, and the table alone
would be unfair to them. The distinction matters:

- **Steady-state level cannot show a ballistics control.** Attack Tilt and Release
  Tilt change *how fast* the gain moves, not how far it ends up. On a two-second
  steady take the gain settles to the same value either way. They need a gated or
  percussive signal and a time-resolved measurement.
- **Note Motion needs pitch movement.** It is gated behind
  `motionProtect > 0 && historyValid && previousPeriod >= 3`. A fixed-pitch take
  never satisfies it. It needs a take that glides between notes.

So the honest reading is: **one control is dead, one is far too weak, and two are
untested rather than proven dead.**

## Why Detail Tilt is dead

`ResonanceDetector.h` computes the analysis window like this:

```cpp
const float tiltMul = std::pow (ratio, 0.50f * detailTilt);
const float effSharpness = std::clamp (dialSharpness * tiltMul, 0.20f, 5.0f);
const float erbHz = (24.7f * (4.37f * (f * 0.001f) + 1.0f)) * (1.0f / effSharpness);
const int erbBins = std::clamp (static_cast<int> (erbHz / binWidth), 3, maxRadius);
const int wideFloor = std::max (4, static_cast<int> ((bins / 48) * floorScale));
const int radialBins = std::clamp (std::max (std::max (erbBins, minRadiusBins), wideFloor), 3, maxRadius);
```

The tilt is applied to `effSharpness`, which shrinks `erbBins`. But `wideFloor`
then takes the maximum, and at Standard (2k) that floor is about 16 bins. The ERB
at 4 kHz is already about 17 bins before tilting, so the tilt can only pull it
down to the floor and no further. `radialBins` never changes. The control is
wired all the way through and has no effect.

This is the same root cause as the original DETAIL problem: a floor that was
added to protect the detector from reading a peak's own flanks as its baseline
now swallows every control that tries to narrow the window.

## Why Sibilance is too weak

`ResonanceDetector.h` gates it to 4–12 kHz and gives it two levers:

```cpp
if (isSibilanceBand && p.sibilanceSmooth > 0.01f)
    effectiveCue = std::max (effectiveCue, p.sibilanceSmooth * 3.0f);

if (isSibilanceBand && p.sibilanceSmooth > 0.05f)
    thr -= (1.5f * p.sibilanceSmooth);
```

A threshold drop of **1.5 dB maximum** and a cue ceiling of 3.0. Compared with
the main controls — Depth moves reduction by several dB, How Picky moves the
threshold by 3.5 dB — the sibilance lever is roughly a third the authority of
anything else on the panel. It is not broken. It is underpowered by design, and
the design was never checked against a sibilant signal.

## What the control is supposed to be

Sibilance is not a resonance. It is broadband noise concentrated in 4–12 kHz,
present only during S and T consonants. A resonance suppressor handles it badly
because the shape is wrong: the plugin cuts narrow peaks, sibilance is a broad
hump that comes and goes.

That is why the de-esser needs its own behaviour, not a small nudge to the
resonance detector:

1. **Detection by broadband energy, not by peaks.** Measure the 4–12 kHz band's
   level against the 1–4 kHz band's level. Sibilance is when the high band jumps
   relative to the body. That is how a de-esser works and it is why it fires on
   consonants and not on vowels.
2. **A time constant matched to consonants.** S and T last 40–120 ms. The
   detector's frames are 46 ms at Standard resolution. The control needs its own
   envelope, not the resonance ballistics.
3. **A wide, shallow cut.** Sibilance needs a broad shelf or a low-Q dip across
   5–11 kHz, not a narrow notch. Cutting it with notches is what makes de-essers
   sound lispy.
4. **A range control.** Which frequencies count as sibilance depends on the
   voice. A fixed 4–12 kHz will be wrong for some singers.

## The plan

Ordered by value per unit of risk. Each step is measurable with the existing
analyser, and each has a test that can fail.

### Step 1 — Give the tilts somewhere to act (fixes Detail Tilt)

Make `wideFloor` scale with the control instead of clamping above it. Today the
floor is applied as a maximum that wins; it should be a lower bound the control
can approach. Concretely: let the floor fall to a quarter of its value as the
control goes to its extreme, rather than the current half, and apply the tilt
*after* the floor rather than before.

- Measure: the window width per frequency, from the overlay now on the graph.
- Test: the drawn window must differ by more than 20% between Detail Tilt −1 and
  +1 at 6 kHz.
- Risk: low. The floor still exists; it just moves.

### Step 2 — Rebuild Sibilance as a real de-esser

Replace the two small levers with the four-part design above. Keep the parameter
ID so saved sessions load.

- Measure: level change in the 5–11 kHz octave, on the sibilant take, at the
  control's extremes. Target **3 dB or more** — ten times its current authority.
- Test: on a sibilant take the control must move the 5–11 kHz octave by more
  than 2 dB while moving 250–1000 Hz by less than 0.5 dB. That second half is the
  important one: it proves the control is a de-esser and not a second Depth knob.
- Risk: medium. It is new DSP and will need two or three calibration passes.

### Step 3 — Prove or remove Attack Tilt and Release Tilt

Build the measurement that can see them: a gated tone burst, then track the gain
envelope over the first 50 ms and the release tail separately.

- If they move the envelope by a useful amount, keep them and document what they
  do.
- If they do not, remove them. A control that cannot be measured cannot be
  defended, and two dead knobs in a drawer teach the user to distrust the rest.

### Step 4 — Prove or remove Note Motion

Build a take that glides between two notes and confirm the control changes the
decision. Same rule: if it cannot be shown to act, it should not ship.

### Step 5 — Label everything honestly

Once each control is proven, name it for what it does and give it a tooltip that
states the measurement. The drawer currently mixes three different kinds of
control — a de-esser, two ballistics tilts and a detection tilt — with no
indication of which is which.

## A note on how this was missed

The plugin has 39 processor checks and 23 DSP checks, all passing, and the whole
drawer was inert. Every existing test measured the *main* controls. Nothing
measured the drawer, because the drawer was added last and tested by inspection
rather than by measurement.

The fix for that is the rule applied in this plan: **no control ships without a
measurement that would fail if it did nothing.**

---

## Results: the plan, implemented (2.6.0)

Every number below is from the test suite, which now fails if the control stops
working. The suites stand at **36 processor checks** and **30 DSP checks**, all
passing.

### Step 1 — the floor now moves with the tilt

Detail Tilt was wired end to end and changed nothing, because the protective floor
was applied as a maximum *above* the tilted bandwidth, so the floor always won.

| | Before | After |
|---|---|---|
| Window at 6 kHz, tilt −1 → +1 | 69 → 69 bins | **69 → 49 bins** |
| Audio effect | 0.00 dB | **1.9 dB** |
| Graph pixel change | 0.0 % | **12.28 %** |

The floor is now scaled by the same tilt multiplier. This is the single change
that un-blocked the control.

### Step 2 — Sibilance is a real de-esser

The old path nudged the resonance detector: a 1.5 dB threshold drop and a cue
ceiling in the 4–12 kHz band. It moved the level by **0.12 dB**.

It is now its own stage, `Source/DSP/SibilanceDeEsser.h`, with the four pieces a
de-esser needs: broadband energy detection against the 1–4 kHz body, an excess
measure against the take's own resting ratio, consonant-matched time constants
(2 ms attack, 55 ms release), and a wide raised-cosine cut across the band.

| SIBILANCE | 4 kHz | 8 kHz | 250–2000 Hz |
|---|---|---|---|
| 0.0 | +0.0 dB | +0.0 dB | +0.0 dB |
| 0.5 | 0.7 dB | 1.3 dB | **0.0 dB** |
| 1.0 | 1.2 dB | 2.4 dB | **0.0 dB** |

Measured as a unit test on a voice with a consonant burst: **5–11 kHz −11.04 dB,
250–1000 Hz +0.00 dB.** The second column is the point. Without it this is just a
second Depth knob.

### Step 3 — the tilts are both real

Steady-state level cannot see these; they change *how fast* gain moves, not how
far it lands. Measured by stepping the suppressor and counting frames:

| | 300 Hz | 6 kHz |
|---|---|---|
| Attack, tilt +1 | 8 frames | 2 frames |
| Attack, tilt −1 | 2 frames | 5 frames |

Positive tilt slows the lows and speeds the highs, exactly as documented.

**Release Tilt was genuinely broken.** The tilt reached only the coefficient used
after a sustained attack. The release almost always runs through a fast path that
ignored it entirely, so the control read as dead. With the tilt applied to the
path that actually runs:

| | 300 Hz | 6 kHz |
|---|---|---|
| Release, tilt +1 | 0.425 | 1.000 |
| Release, tilt −1 | 0.849 | 0.546 |

### Step 4 — Note Motion was removed

Its period history was one frame deep, and it compared two smoothed values that
are nearly identical, so its central condition could never be met. A real
eight-frame history was built and it *still* did not act: two measurements of the
situation it exists for — a stationary resonance with harmonics sliding past it —
came back identical at 0 and at 100 %.

The plan says a control that cannot be shown to work does not ship. Its drawer
control is gone and the parameter ID is kept so saved sessions still load.

### What this cost

Three defects in this drawer were all the same defect: **a control that was wired
up, tested by nobody, and quietly doing nothing.** 62 checks passed for months
while the entire drawer was inert, because every test measured the main controls.
The rule that came out of it is in the test suite: a control does not ship without
a measurement that would fail if it did nothing.
