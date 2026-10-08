# Changes from the Antigravity session, 6 October 2026

## What exists now

The project became a git repository on 6 October at 06:54, and three fix commits
followed. `Source/` is fully committed; the working tree is clean.

| Commit | Time | Subject |
|---|---|---|
| `72571ee` | 06:54 | Initial commit: ResonaPro v2.2 - Dynamic Resonance Suppressor & Vocal De-Esser |
| `d320545` | 07:09 | Fix cue-gated resonance suppression & calibrate high-frequency sibilance sensitivity |
| `968afd5` | 07:30 | Fix cue-proportional thresholding, fix high-end sensitivity, and fix EQ filter defaults |
| `bb119e2` | 07:56 | Fix high frequency resonance detection threshold |

Untracked working files left behind by the session: `patch.py`,
`patch_detector.py`, `patch_eq.py`, `test_detector.cpp`, `test_engine.cpp`,
`test_logic.py`, and the compiled `test_detector` and `test_engine` binaries.

## The change that is a genuine improvement

The focus-band filter defaults were wrong and are now fixed.

```
bands[0] = { true, FilterType::LowCut,    80.0f, 0.0f, 0.707f };   before
bands[0] = { true, FilterType::LowShelf,  80.0f, 0.0f, 0.707f };   after
bands[7] = { true, FilterType::HighCut, 16000.0f, 0.0f, 0.707f };  before
bands[7] = { true, FilterType::HighShelf,16000.0f, 0.0f, 0.707f };  after
```

A cut filter has no meaningful gain parameter, so the bottom and top focus bands
could never be boosted, only attenuated. As shelves they behave correctly. The
matching change in `kDefaultEqType`, from `{ 1, 0, 0, 0, 0, 0, 0, 2 }` to
`{ 3, 0, 0, 0, 0, 0, 0, 4 }`, keeps the stored parameter values consistent. This
should have been right in the original.

## The change that breaks the plugin

The detector was rewritten around cue gating. At `Source/DSP/ResonanceDetector.h`
line 337:

```cpp
// RULE 1: STRICT CUE GATING
// If the user has not moved a cue up (and no sibilance smoothing is active),
// resonance reduction is EXACTLY zero.
if (effectiveCue <= 0.05f)
{
    out[k] = 0.0f;
    continue;
}
```

`weightsDb` is the per-bin focus-band curve, and it is 0 dB everywhere when the
eight focus bands sit at their defaults. With all cues neutral, `effectiveCue`
is 0 for every bin, so the detector returns zero for every bin and the plugin
does nothing at all.

Previously a cue *biased* the threshold: 0 dB was neutral and the base threshold
still applied, so detection ran on its own. The change inverts that relationship
from "bias" to "gate".

### Measured consequence

The full build succeeds with no errors. The processor suite then reports **6
failures, every one reading 0.00 dB**:

```
[FAIL] default depth produces a gentle, audible change            -- -0.00 dB
[FAIL] delta exposes only what was removed                        -- -136.4 dB
[FAIL] reduction lowers the loudness (the problem being solved)   -- -0.00 dB
[FAIL] external key changes detection while the vocal stays output-- 0.00 dB
[FAIL] long-window low-band detector changes the low-frequency... -- 0.000 dB
[FAIL] motion cue changes the low-band decision on a pitch shift  -- 0.000 dB
```

Delta at -136.4 dB is the clearest signal: DELTA plays the removed signal, and
there is none, because nothing is being removed. Every feature that depends on
the detector is inert: EXT KEY, LOW DETAIL, and NOTE MOTION all measure exactly
zero change.

The DSP-level suite still passes, because it exercises the engine and the
suppressor directly rather than the detector's decision path.

## Other effects of the rewrite

* **Removed: the curvature guard.** This rejected broadband noise floor being
  treated as a resonance, by requiring a local peak to be curved rather than flat.
* **Removed: air-band preservation above 11 kHz.**
* **Removed: high-frequency tilt compensation** and the 1.35x high-end cue boost.
* **Removed the `fullScaleReference` path.** The reference is now always the
  per-frame peak, so absolute level no longer enters the decision. This is worth
  checking against HARD mode, whose purpose is to react to absolute level.
* **Changed: sibilance band** from 4000-9500 Hz to 4000-12000 Hz.
* **Changed: reduction ceiling** from 40 dB to 48 dB.
* **Changed: transient protection** now applies below 3 kHz only, where it
  previously also covered the sibilance band.
* **Added: a 2.5x cue multiplier above 2.5 kHz**, clamped to 0-6.

`patch.py` shows the threshold term was first written with a limit:

```python
thr -= std::min(effectiveCue * 0.5f, 5.0f)   # "cap it so we don't suppress the noise floor"
```

and then replaced with an uncapped version. Each round removed a safety limit.

`test_logic.py` is worth noting for what it is not. It re-implements the
threshold arithmetic in four lines of Python over invented inputs and prints
three numbers. It never loads the plugin, so it cannot fail when the plugin
breaks.

## The question this raises

Two readings, and they lead to opposite work:

1. **The gating is intended.** The plugin becomes a manual dynamic EQ that acts
   only where you draw a cue. Then the six tests encode an obsolete contract and
   should be rewritten, and the documentation needs to stop describing automatic
   resonance detection.

2. **The gating is a side effect.** The stated goal in the commit messages was to
   make the high end more sensitive, and gating was a blunt way to get decisive
   behaviour. Then the core feature has been switched off by accident, and it
   should be restored while keeping the EQ filter-default fix and the useful
   parts of the high-frequency calibration.

The failure mode was easy to miss because it only shows when the focus bands are
neutral. Anyone testing by dialling in a cue would see the plugin working.