# Making LEARN world-class

Design document. What LEARN does today, what it should do, and the fence it must
never cross.

---

## 1. Where it stands

LEARN currently proposes exactly two things:

1. **Focus bands** — up to eight frequencies with a sensitivity, found by
   `LearnAnalyzer` from persistent peaks in the visualiser scope.
2. **Sibilance** — a low/high band, a peak frequency and an amount, found by the
   de-esser's own profiler.

Everything else in the plug-in stays wherever the user left it. That is a thin
read of a take. A vocal tells you far more than where its peaks are.

## 2. What a take can tell us

Each measurement below is available from material the engine already sees. None
of them require new capture machinery; they require the analyser to look.

| What to measure | How | What it proposes |
|---|---|---|
| Peak excess | Worst-case dB above the local baseline, per detected peak | **DEPTH**, **MAX CUT** |
| Peak sharpness | Width of each peak at −3 dB | **DETAIL** (sharpness) |
| Peak spacing | Median distance between detected peaks | **HOW NARROW** (selectivity) |
| Peak persistence | How many frames a peak survives | Which bands to enable at all |
| Peak shape | Symmetry and skirt steepness | **Filter type per band** — Bell, High Shelf, Low Shelf, Notch |
| Transient density | Count of fast onsets per second | **TRANSIENT** protect |
| Ring decay | How long resonances take to fall | **RELEASE**, **RELEASE TILT** |
| Onset speed | How fast the excess arrives | **ATTACK**, **ATTACK TILT** |
| Spectral tilt | Broad slope of the take's spectrum | **DETAIL TILT** |
| Sibilance position | Excess over each band's own resting level | **SIBILANCE band + amount** |
| Low-end buildup | Excess below 150 Hz | A focus band at the bottom |

## 3. The fence

This is the part that must be enforced in code, not in a comment. The user's
instruction was explicit, and it is right: LEARN is a mixing assistant, not a
session rewriter.

**LEARN MUST NEVER TOUCH**

| Parameter | Why |
|---|---|
| `quality` | The resolution is a latency and CPU decision. Changing it silently would change the reported latency mid-project. |
| `modeHard` | Hard mode is a deliberate character choice. |
| `midSide` | Mid/side is a monitoring decision, not a correction. |
| `deltaListen` | Delta is a monitor. It must never be switched on by an analyser. |
| `vocalProfile` | The weighting curve is the user's intent about what this track is. |
| `outGain` | Output level is set by the user for gain staging. |
| `stereoLink` | The user's instruction, and a mix decision. |
| `mix` | The wet/dry balance is the user's instruction about how much to do. |
| `autoGain` | Level matching is a comparison aid the user turns on. |
| `oversampling` | A rendering decision with a CPU cost. |
| `externalKey` | A routing decision. |
| `bypass`, `deltaBand`, `tracking_mode` | Monitoring and routing. |

**LEARN MAY PROPOSE**

`depth`, `sharpness`, `selectivity`, `attack`, `release`, `maxReduction`,
`transientPreserve`, `attackTilt`, `releaseTilt`, `detailTilt`, `motionProtect`,
`sibilanceSmooth`, `sibilanceLow`, `sibilanceHigh`, and the eight focus bands
(`eq_enable_N`, `eq_freq_N`, `eq_gain_N`, `eq_q_N`, `eq_type_N`).

This list lives in one place in the source as `kLearnEditable`, and a test asserts
that every id in it is a real parameter and that every id outside it is refused.
A fence nobody tests is a fence that rots.

## 4. Nothing changes during capture

Already true, and it stays true. During LEARN the audio thread only accumulates;
the engine's parameters are untouched. Every proposal lands in a review buffer and
waits. This matters more now that LEARN has something to say about depth and
attack — a silent change to those mid-take would be indefensible.

## 5. The review step

The user's second request was access: *change the cues, the shape of the cues, the
filter of the cues, pick the detail, have access to the knobs.*

So LEARN must not be a black box that writes eight values. It must present a
table:

```
LEARNED FROM "take 3"
                          current    proposed
  Depth                     1.49       1.80      [x]
  Detail                    1.00       1.35      [x]
  How narrow                0.50       0.62      [x]
  Transient                 0.50       0.65      [x]
  Max cut                  24.0 dB    18.0 dB    [x]
  Attack                    8.0 ms     6.0 ms    [x]
  Release                  64.6 ms    90.0 ms    [x]
  Detail tilt               0.00       0.30      [x]
  Sibilance                 0.50       0.70      [x]
  Sibilance band        4.0-16.0k   5.1-15.2k   [x]
  -------------------------------------------------
  Band 1   3.2 kHz  Bell  -4.1 dB  Q 2.0        [x]
  Band 2   6.7 kHz  Bell  -5.8 dB  Q 3.1        [x]
  ...
                                        [APPLY] [DISCARD]
```

Every row has a checkbox. Unchecking one means "leave my value alone". The band
rows additionally allow the filter type and Q to be changed before applying — that
is the "change the filter of the cues" request, and the graph already supports
dragging the nodes and right-clicking to set a value.

**Undo.** APPLY takes a snapshot of every parameter it is about to write. A single
UNDO restores them. Learn is the one action in this plug-in that can move eleven
controls at once, so it must be the one action that can be reversed in one click.

## 6. Why the proposals are shaped the way they are

**Depth from peak excess, not from peak count.** A take with one 12 dB ring needs
more depth than a take with eight 3 dB bumps. Depth follows the worst case, with
the count as a mild modifier.

**Detail from peak sharpness.** Narrow peaks need high sharpness or the cut
smears across the formant. Broad bumps need low sharpness or the cut misses them.
This is a direct read of the peak width.

**How narrow from peak spacing.** Closely spaced peaks need a narrow analysis
window or the detector averages them into one. Widely spaced peaks tolerate a
wider one, which is cheaper and more stable.

**Transient protect from onset density.** A take full of plosives and hard
consonants wants protection high. A legato take does not, and setting it high
anyway costs definition.

**Attack and release from the shape of the excess over time.** Fast-arriving
excess wants a fast attack; slow-decaying ring wants a long release.

**Detail tilt from the take's own slope.** If the take is dark, the detector is
looking at a tilted spectrum and the weighting needs a compensating tilt.

**Filter type from peak position and shape.** A peak at the very bottom or top of
the range with a one-sided skirt is a shelf, not a bell. A narrow, deep, isolated
peak is a notch. Everything else is a bell. This is the difference between a
proposal that looks plausible in a table and one that sounds right.

## 7. What this does not claim

- These heuristics are reasoned, not trained. They will be wrong on some material,
  which is exactly why every row is a suggestion the user can refuse.
- The measurements are cheap spectral statistics. They are not a model of what the
  take should sound like. LEARN proposes a starting point; the ears finish it.
- No claim is made about how this compares to any other product's learn function,
  because none of their internals are readable.
