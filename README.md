# ResonaPro

**A vocal-tuned dynamic resonance suppressor, de-esser and tonal balancer.**

ResonaPro listens to a vocal in the frequency domain, works out which narrow
peaks are *resonances* rather than *the voice itself*, and pulls those peaks down
with per-bin dynamics. It is built in C++20 with JUCE and uses Apple's `vDSP`
for the transforms.

Version 2.1 added an optional sidechain key, low-band analysis, a note-motion
cue, and user-reviewed learning. Version 2.2.0 makes the MATCH button measure
perceived loudness (ITU-R BS.1770 K-weighting) instead of raw RMS, and caps the
correction so it can never raise the peak above what the input already had. Read
the [FL Studio quick start](docs/HOW-TO-USE.md), the
[2.1 release notes](docs/V2.1-RELEASE-NOTES.md), and the
[audio-quality report](docs/AUDIO-QUALITY.md), which documents the measurements
behind the transparency and level-matching claims. If you are coming from
v1, read [docs/UPGRADE-REPORT.md](docs/UPGRADE-REPORT.md) first — several
controls changed meaning.

---

## Why it is not "another spectral compressor"

The hard part of this kind of processor is not the reduction; it is the
**detection**. A sung note is a comb of narrow harmonic peaks. Any detector that
simply hunts for "narrow peaks that stand above the local average" will flag
every harmonic and hollow out the voice.

ResonaPro's detector does three things about that:

1. It estimates the **harmonic period** of the input every frame, and widens its
   analysis window so that a harmonic is always compared against its
   *neighbouring harmonics* rather than against the gaps between them.
2. It derives an **adaptive threshold** from the statistics of the frame — how
   peaky this material already is — so only peaks that stand out *beyond the
   material's own character* are treated.
3. It works entirely in **dB prominence relative to a local baseline**, which
   makes the behaviour identical for a whisper and a belt.

The result: measured on a deliberately hostile synthetic vocal, the harmonics
are left at 0.0 dB while a genuine Q = 14 resonance in the same signal is
reduced. See [docs/RESEARCH-AND-DESIGN.md](docs/RESEARCH-AND-DESIGN.md) §5.3.

---

## Controls

### Main

| Control | Range | Default | What it does |
|---|---|---|---|
| **Depth** | 0 – 4 | 1.0 | How much reduction is applied. 0 is a true bypass-grade passthrough. The upper range is deliberately generous. |
| **Detail** (sharpness) | 0.2 – 4 | 1.0 | Analysis bandwidth. Low = wide, musical, dynamic-EQ-like. High = narrow, surgical. |
| **Selectivity** | 0 – 1 | 0.5 | How far a peak must stand above the material's own peakiness before it is touched. |
| **Transient Protect** | 0 – 1 | 0.5 | Raises the threshold on attacks. 0 = process everything (best for de-essing), 1 = leave all attacks alone. This is what keeps plosives and consonants intact. |
| **Max Cut** | 3 – 36 dB | 24 dB | Ceiling on the reduction of any single bin. |
| **Attack / Release** | 0.5–50 ms / 5–500 ms | 8 / 70 ms | Per-bin envelope follower. Frequency dependent: the release is ~2.5× slower in the low end than in the top end. |
| **Vocal Profile** | 4 choices | Lead Vocal | Biases the detector per region. See below. |
| **Stereo Link** | 0 – 1 | 1.0 | 0 = dual mono (each channel decides independently), 1 = one shared decision for both channels. |
| **Mix** | 0 – 100 % | 100 % | Dry/wet. The dry path is delayed to match, so mixing never combs. |
| **Output** | ±24 dB | 0 dB | Output trim. |

### Vocal profiles

| Profile | Regions biased toward reduction | Regions protected |
|---|---|---|
| **Lead Vocal (All-Round)** | 1.8 – 8 kHz | below 200 Hz, above 14 kHz |
| **De-Ess / Sibilance** | 4.5 – 10 kHz | everything below 3.5 kHz, above 10 kHz |
| **Warm Body & De-Mud** | 180 Hz – 1.6 kHz | above 8 kHz |
| **Air & Silk** | 8 kHz and up | below 5 kHz |

### Focus bands (the graph)

Eight draggable nodes. Their gain is an offset to the **detection threshold**,
and the sign is inverted on purpose:

* pull a node **up** → the detector listens harder there → **more** processing;
* pull a node **down** → **protection** for that region.

* double-click empty graph space to add/enable a node
* double-click a node to enable/disable it
* mouse wheel over a node adjusts its Q
* hold Shift while dragging to snap to a 1/6-octave frequency grid and whole dB
* right-click a node to type `frequency gain Q` (for example, `3200 3.0 1.4`)
* **Reset Bands** restores all eight to neutral

### Character

* **EAR GUARD** — a small, fixed ear-sensitive threshold bias. The internal
  `iso226` parameter ID is kept for session compatibility, but this is **not**
  an ISO 226 equal-loudness contour.
* **HARD** — hard mode: level dependent and much more aggressive (the detector
  reacts to absolute harmonic levels instead of relative ones).
* **MID/SIDE** — process mid and side independently.
* **DELTA** — hear exactly what is being removed. This is the fastest way to
  learn what the plug-in is doing.
* **BYPASS** — latency-compensated bypass.
* **EXT KEY** — detect from an optional host sidechain bus while processing the
  main vocal. If the bus is disabled, it falls back to the vocal.
* **LOW DETAIL** — uses a 4k window for low-band detection in the 1k/2k modes.
  It does not add a second synthesis path or new latency.
* **NOTE MOTION** — a bounded cue that protects harmonics which move with the
  sung note and favours peaks that stay at one frequency.
* **LEARN / APPLY LEARN** — while the editor is open, analyse a played take and
  propose up to eight focus-band settings. Nothing changes until Apply.

### Quality and Response

These replace v1's "oversampling" control, which did not do what its name
implied (see the upgrade report).

| Quality | Transform | Latency @44.1 kHz |
|---|---|---|
| Low Latency | 1024 | ~23 ms |
| Standard | 2048 | ~46 ms |
| High | 4096 | ~93 ms |
| Ultra | 8192 | ~186 ms |

| Response | Overlap | Effect |
|---|---|---|
| Eco | 2× | lowest CPU, slowest gain updates |
| Standard | 4× | balanced |
| Fine | 8× | fastest gain updates, highest CPU |

### Presets

Eleven factory presets, grouped by role in the menu: Init, Lead Vocal (Gentle / Tame Harshness / Forward &
Clean), De-Ess (Broad / Whistling), Warm Body De-Mud, Air & Silk, Plosive Safe
De-Ess, Harsh Bus Glue, Extreme Resonance Hunt.

---

## Building

```bash
# configure (JUCE path can be overridden with -DJUCE_SOURCE_DIR=...)
./.work/scripts/build.sh configure

# build AU + VST3 + Standalone
./.work/scripts/build.sh build

# install into ~/Library/Audio/Plug-Ins  (run from a normal Terminal)
./install.sh
```

Or with CMake directly:

```bash
cmake -S . -B .work/build -DCMAKE_BUILD_TYPE=Release -DRESONAPRO_COPY_PLUGIN=OFF
cmake --build .work/build --config Release -j 8
```

`RESONAPRO_COPY_PLUGIN=ON` makes the build copy the plug-ins into the user's
plug-in folders automatically.

---

## Testing

Two automated suites, both headless:

```bash
# engine level: reconstruction, transparency, detection, stereo, stability
clang++ -std=c++20 -O2 -I Source/DSP .work/scripts/dsp_test.cpp \
        -framework Accelerate -o .work/build/dsp_test && ./.work/build/dsp_test

# full processor: latency, null tests, presets, state, block-size independence
cmake -S . -B .work/build-tests -DCMAKE_BUILD_TYPE=Release \
      -DRESONAPRO_BUILD_TESTS=ON -DRESONAPRO_COPY_PLUGIN=OFF
cmake --build .work/build-tests --target ResonaProTests -j 4
./.work/build-tests/ResonaProTests_artefacts/Release/ResonaProTests
```

Both must report `ALL PASS`. The engine suite verifies, among other things, that
at Depth 0 the output is the input delayed by exactly the reported latency with a
residual of ~1e−15.

---

## Project layout

```
ResonaPro/
├── Source/
│   ├── PluginProcessor.h/cpp        # parameters, audio pipeline, presets, state
│   ├── PluginEditor.h/cpp           # UI layout
│   ├── DSP/
│   │   ├── SpectralEngine.h         # vDSP STFT, latency probe, stereo modes
│   │   ├── ResonanceDetector.h      # harmonic period, baseline, adaptive threshold
│   │   ├── DynamicSuppressor.h      # per-bin attack/release, ceilings, smoothing
│   │   ├── TransientDetector.h      # attack detection for transient protection
│   │   ├── LearnAnalyzer.h          # editor-driven, user-reviewed take suggestions
│   │   └── ParametricEQWeighting.h  # 8 focus bands as real biquad responses
│   └── UI/
│       ├── CustomLookAndFeel.h
│       └── SpectralVisualizer.h
├── Tests/ProcessorTests.cpp         # headless processor harness
├── docs/
│   ├── RESEARCH-AND-DESIGN.md       # findings, sources, full DSP design
│   └── UPGRADE-REPORT.md            # what was broken in v1 and what changed
├── install.sh
└── CMakeLists.txt
```

---

## Hosts

Built as AU, VST3 and Standalone on macOS, with arm64 and x86_64 slices.
Processor tests cover mono/stereo layouts and a stereo optional sidechain bus.
Apple's `auval` validates the installed AU. An automated test cannot stand in
for a session in FL Studio or other hosts; scan the VST3 and test the sidechain
routing in your DAW before relying on it in a release project.
