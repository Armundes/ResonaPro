# Handoff prompt for a new agent or workspace

Paste the block below as the first message when opening ResonaPro in a new
workspace, agent, or IDE session.

---

```
You are picking up an existing, mature audio-plugin project. Read before you
change anything. Do not write code until you have finished the reading list and
run the verification steps below.

## What this project is

ResonaPro is a vocal spectral de-resonator and de-esser. It is a JUCE C++20
audio plugin for macOS, used inside FL Studio. It is a specialized alternative
to soothe2, built for vocals specifically.

Repository root:
/Users/armundescarey/Documents/ResonaPro

Dry vocal test corpus (18 takes, about 7 singers):
/Users/armundescarey/Documents/Dry Vocal For Plugin test/
A converted copy lives at .work/audio/corpus/

## Read these first, in this order

1. AGENTS.md -- standing rules. These are the owner's, not yours. Follow them
   without being asked again.
2. docs/LEARN-UPGRADE-PLAN.md -- the approved plan for the Learn feature.
3. docs/LEARN-SCORE-BASELINE.md -- the measurement harness and what it found.
4. docs/DE-ESSER-ROADMAP.md -- the de-esser plan.
5. docs/SIBILANCE-MISS-DIAGNOSIS.md -- corpus investigation, including a failed
   experiment that was reverted.

Then list docs/ and read whatever relates to your task. Several docs record
what was tried and why it failed. Read those before you retry an idea. That
history is worth more than the things that worked.

## The standing rules you must follow

- Do NOT build .pkg, .dmg, or installer .zip files unless explicitly asked.
  They cost real time and are only needed when a build leaves this machine.
- After any source change that affects audio: build, run both test suites,
  install. Do not stop at a successful build.
- Keep the install current. The owner's DAW scans
  ~/Library/Audio/Plug-Ins/VST3/ResonaPro.vst3
  ~/Library/Audio/Plug-Ins/Components/ResonaPro.component
  If you change audio code and do not install, the owner will test a stale
  build and report a bug that is already fixed.
- Evidence over assertion. Every claim about audio behavior must be measured.
  A change not covered by a check is not verified. When a change fails, report
  the measurement that failed it, including when the change is yours.
- Synthetic test signals prove the signal path is clean. They say nothing about
  whether the plugin sounds right on a real voice. That still needs the owner's
  ears. Say so when it applies.

## Verify your environment before touching code

Run these. If they do not pass, fix the environment first.

    cd /Users/armundescarey/Documents/ResonaPro
    ./.work/scripts/build.sh configure
    ./.work/scripts/build.sh build
    ./.work/scripts/build.sh test        # or build the two CMake test targets
    ./install.sh

Both suites must report 0 failures:
    ResonaProDspTests
    ResonaProTests

If cmake is not on your PATH, this machine has a python cmake shim at
.work/tools/pycmake. Prefix commands with:
    PYTHONPATH=.work/tools/pycmake python3 -m cmake ...

Confirm the installed version, and that it matches the source:
    /usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' \
      "$HOME/Library/Audio/Plug-Ins/VST3/ResonaPro.vst3/Contents/Info.plist"

The version label on the plugin interface shows what is actually loaded. Check
it whenever a bug report does not match the source.

## Current state

- Installed version: 2.13.1
- Both test suites pass with 0 failures.
- The GUI is a cream, off-white design inspired by soothe.
- The core controls include Depth, Detail, Selectivity, Attack, Release, Max
  Cut, Transient Protect, Quality/Response, Stereo Link, ISO 226, Hard,
  Mid/Side, Delta, Solo Cut, Learn/Apply/Undo, three Tilt controls, Sibilance
  De-Ess, external key, and focus bands.
- The three tilts (Detail Tilt, Attack Tilt, Release Tilt) all run -1 to +1 and
  default to 0.0, which is neutral. They shape how the detector and suppressor
  react by frequency. They do not EQ the signal.
- Learn Tier 1 landed in 2.13.1: a real onset/decay state machine with a
  debounce, percentile-based depth and max cut, and persistence-weighted peak
  width. Before this, the attack measurement was dead and always returned its
  8 ms default.

## Known open problems

Read docs/LEARN-SCORE-BASELINE.md for the measurements behind these.

1. Learn timings saturate. The learner reads scope frames at 20 Hz, so one
   frame is 50 ms. The parameters it proposes are millisecond-scale (attack runs
   1-30 ms). The frame rate cannot resolve what the parameters need, so every
   take produces clamped settings. Depth and max cut saturate the same way.
   The likely fix is audio-rate onset/decay measurement inside the engine during
   Learn, not more tuning of the editor-side analyzer.

2. The harmonic rejection planned as Learn Tier 2 is not implemented. The agreed
   approach is motion-weighted down-weighting of harmonics, reusing the
   detector's lag search. Do NOT hard-reject integer multiples of the
   fundamental -- that was considered and rejected.

3. An unresolved disagreement about the de-esser. Tools/LearnScore.cpp measured
   sibilance reduction at 0.00 to 0.69 dB across the corpus, with most takes
   under 0.05 dB, while a single 6 dB shelf control measured 1.77 dB. The owner
   reports the de-esser works. One possible reconciliation: a very short, very
   deep cut could read low in a frame average while still being audible. Check
   this before acting on either view. Do not treat the measurement as settled,
   and do not treat the disagreement as resolved.

## How to work here

- Measure before you claim. Write the measurement into docs/ so it outlives the
  session.
- Revert anything that measures worse than what it replaced. Keep a backup of
  every file before you edit it. Backups live in .work/backups/.
- Do not change the plugin's sound to satisfy a question. Ask whether the owner
  wants an answer or a change when it is not clear.
- Learn must never change Quality/Resolution, Hard mode, Mid/Side, Delta,
  profile weighting, output, stereo link, mix, auto gain, external key, bypass,
  or monitoring and routing decisions. This is enforced by an allowlist and by
  tests. Do not weaken either.
- The owner tests by ear in FL Studio on real vocals. Ask for that when a
  question cannot be settled by measurement.

Start by reading the files and running the verification steps. Then tell the
owner what you found, including anything that contradicts what is written here.
```
