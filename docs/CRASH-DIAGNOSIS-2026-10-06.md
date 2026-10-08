# ResonaPro: Crash Diagnosis and Corrective Work

**Date:** 6 October 2026
**Symptom:** FL Studio crashes every time the plugin is loaded.
**Status:** All defects found and fixed. Version 2.3.1.

Two intermittent defects (a teardown abort and a 2 dB level-match drift) turned
out to share a single root cause, found on 6 October 2026. See section 6.

---

## 1. Why the DAW crashed (fixed)

The installed plugin dereferenced a null pointer during `prepareToPlay()`, the
first thing a host calls when you insert a plugin. A crash there is a crash on
sight, which is exactly what was reported.

```
SpectralEngine::applyBarkSmoothing(...)   <-- fault, address 0x0
SpectralEngine::processSTFTFrame()
SpectralEngine::processSample(...)
SpectralEngine::processBlock(...)
SpectralEngine::measureLatency()
SpectralEngine::prepare(...)
DualBandEngine::prepare(...)
ResonaProAudioProcessor::buildEngines(...)
ResonaProAudioProcessor::prepareToPlay(...)
main
```

`applyBarkSmoothing()` groups the reduction curve into 25 critical bands and
needs to know which band each bin belongs to. It read that from a member called
`binBark`. In the whole file, `binBark` appeared three times: read at line 694,
read at line 721, **declared at line 804**. Nothing ever filled it, so it was an
empty vector with a null data pointer, and every read was a null dereference.

```
EXC_BAD_ACCESS (SIGSEGV) — KERN_INVALID_ADDRESS at 0x0000000000000000
```

`measureLatency()` pushes a synthetic impulse through the real processing path,
so the engine's own latency measurement was what killed it.

**Fix:** `binBark` is now filled during `prepare()`, immediately after
`binFreq[]` and before `measureLatency()`, using Traunmüller's Bark formula
(0 to ~24.6 over 20 Hz to 20 kHz, matching the 25 bands).

## 2. Why the test suite did not catch it (the process failure)

The test suite **did not compile** against the changed source. The detector's
`detect()` had gained a parameter and `prepare()` had changed arity; the four
call sites in `Tests/ProcessorTests.cpp` were never updated.

The *test* build failed; the *plugin* build succeeded. The plugin was built,
packaged and installed, and the test binary that reported `ALL PASS` was a stale
executable from an earlier build that never recompiled.

> The test suite reported success because it never ran.

## 3. A wrong recovery, corrected

The first recovery step was to reinstall the last packaged build, version 2.2.0,
as "last known good". **That was wrong.** Version 2.2.0 carries the
shoulder-sampling defect described in its own release notes: the helper collects
bin *indices* where the caller expects *levels*, so prominence is negative in
every bin and the detector discards everything. 2.2.0 loads but processes
nothing — every control except Output and Mix is inert.

The correct target was the working tree, which is the only state that actually
reduces. The work below fixes that tree forward.

## 4. Further defects found and fixed

### 4.1 Perfect reconstruction was destroyed

The window comment said the analysis and synthesis windows were both the square
root of a Hann window, whose product is Hann and therefore satisfies
constant-overlap-add. The code had been changed to use a Blackman-Harris window
both ways. Blackman-Harris does not satisfy COLA, so the round trip was no
longer unity gain.

| | before | after |
|---|---|---|
| residual at depth 0 | 2.34 | **0.000000** |

**Fix:** restored sqrt-Hann both ways and the derived `hop / N²` synthesis scale.

### 4.2 The Quality control did nothing

`DualBandEngine::prepare(int overlapFactor, double sampleRate)` received only
the overlap. The processor called `prepare(kOverlap[r], activeRate)` and never
passed the quality index, so every quality setting ran the same transform and
reported the same latency.

| | before | after |
|---|---|---|
| 2k setting reports | 8192 samples | **2048 samples** |

**Fix:** `prepare(kFftPow2[q], kOverlap[r], activeRate)`.

### 4.3 The dual-band wrapper broke transparency by construction

`DualBandEngine` split the signal with a Linkwitz-Riley crossover, sent each band
through its own STFT, and summed the results. A second synthesis path behind a
crossover cannot sum back to the input. `SpectralEngine`'s own comment already
said the correct design analyses low frequencies in a single chain with no phase
split.

**Fix:** the processor now uses `SpectralEngine` directly. Multi-resolution
low-band *detection* still happens inside the engine, where it costs detection
quality but no latency.

| | before | after |
|---|---|---|
| memory after `prepareToPlay` at 48 kHz | 268 MB | **100 MB** |
| at 192 kHz | 485 MB | reduced proportionally |

### 4.4 A second null dereference in the state round-trip test

The test called `setParam (a, "multiResolution", ...)` and
`getRawParameterValue ("multiResolution")->load()`. The `multiResolution`
parameter no longer exists, so `getRawParameterValue` returned null and `->load()`
dereferenced it. **Fix:** removed both references.

### 4.5 Latent double-free hazard

`SpectralEngine` owns raw vDSP setup handles but the compiler still generated
copy and move operations that would duplicate those pointers, so two engines
would each destroy the same handle. **Fix:** copy and move are now deleted.

## 5. Where the test suite stands

After the fixes, every functional check passes:

| Area | Result |
|---|---|
| Latency (2k = 2048, 8k = 8192, host follows) | PASS |
| Depth 0 bit-transparent | PASS — residual 0.000000 |
| Bypass latency-aligned, delta, mono bus | PASS |
| Flat cues produce no reduction | PASS |
| Raised cue produces a gentle change | PASS |
| 15 presets load and render | PASS |
| State saves and restores | PASS |
| NaN and huge input never escape | PASS |
| Delta restricted to one band | PASS |
| Output independent of host block size | PASS |

## 6. The two intermittent defects, and their single root cause (FIXED)

Two problems looked unrelated and both looked non-deterministic, which is why
they survived so long. They were the same bug.

### 6.3 The root cause: an out-of-bounds write in dead phase-locking code

`SpectralEngine::lockPhases()` ran on **every frame, unconditionally**, from two
call sites. Three things were wrong with it.

**First, it wrote past the end of a heap buffer.**

```cpp
int rightBound = (p == peaks.size() - 1) ? numBins : (peak + peaks[p+1]) / 2;
for (int k = leftBound; k < rightBound; ++k) {
    re[static_cast<size_t>(k)] = ...;
    im[static_cast<size_t>(k)] = ...;
}
```

`rightBound` reaches `numBins`, but the split-complex buffers hold `halfSize`:

```
halfSize = fftSize / 2        (line 111)
numBins  = halfSize + 1       (line 112)
realL.resize (halfSize)       (line 152)
```

So the loop wrote `re[halfSize]` — **one float, four bytes, past the end of a
`std::vector<float>`** — twice per frame, thousands of times per second. It
read one float past the end too, because `vDSP_zvphas` and `vDSP_zvabs` were
given `numBins` as their length.

That is heap corruption. It explains every symptom:

| Symptom | Why |
|---|---|
| Random crash points, random signals (SIGSEGV, SIGBUS, SIGTRAP) | Whatever heap block sat after the buffer, and when |
| `pointer being freed was not allocated` in `vDSP_destroy_fftsetup` | Adjacent block or allocator metadata overwritten |
| The 2 dB `Match` drift | The same overwrite landing in the gain state |

**Second, it did nothing.** `float rotation = 0.0f;` was never assigned. Every
bin was rebuilt from its own magnitude and phase, so the function was a no-op
that added float round-trip error.

**Third, it allocated on the audio thread.** Two `std::vector<float>` and one
`std::vector<int>` per call, four calls per frame.

**Fix.** The calls and the function are removed. A no-op that corrupts the heap,
allocates in the audio callback and perturbs phase has no reason to exist. Real
identity phase locking (Laroche and Dolson 1999) needs `rotation =
phases[peak] - phases[k]` and writes clamped to the buffer size;
`RESEARCH-SYNTHESIS-OPTIONS.md` covers it if listening ever demands it.

**Measured result.**

| | before | after |
|---|---|---|
| Runs clean (24 runs, guard pages) | 2 crashes + 3 failures in 16 runs | **24 / 24 clean** |
| `Match` loudness error | -2.07 dB (fail) | **-0.03 dB (pass)** |


### The original notes, kept for the record

#### The teardown abort, as first observed

Roughly 2 runs in 12 abort with `SIGABRT` during destruction. A crash report
names the site:

```
___BUG_IN_CLIENT_OF_LIBMALLOC_POINTER_BEING_FREED_WAS_NOT_ALLOCATED
libvDSP.dylib
vDSP_destroy_fftsetup
ResonaPro::SpectralEngine::~SpectralEngine()
std::array<std::array<std::unique_ptr<SpectralEngine>, ...>, ...>
ResonaPro::ResonaProAudioProcessor::~ResonaProAudioProcessor()
main
```

An FFT setup handle is destroyed twice, or a stale handle is destroyed after its
memory was reused. Ruled out so far:

* vDSP does not return the same handle for identical requests (measured).
* `freeFFT()` nulls its pointer; the low-band handle is nulled at both destroy
  sites.
* Deleting copy and move did not change the rate.
* A handle live-set tracker in `SpectralEngine` (temporary, prints only on an
  actual double destroy) has not fired, which suggests the invalid free is not
  reaching either tracked member.

A live-set tracker is still compiled in and will print `[FFT] DOUBLE DESTROY` if
it catches the case.

#### The level-matching drift, as first observed

Some runs report:

```
[FAIL] matching restores perceived loudness   -- -2.07 dB
[FAIL] matching also keeps the plain RMS close -- -1.99 dB
```

The auto-gain envelope does not always converge to the matched level, so `Match`
sometimes leaves the output 2 dB quieter than the input. This is a real audio
defect, not a test artefact, and it is the more user-visible of the two.

## 7. Files

| File | Purpose |
|---|---|
| `.work/scripts/MemProbe.cpp` | Staged load probe: memory per stage, every engine combination, oversampling, live switching |
| `.work/build-probe` | Build tree for the probe (`-DRESONAPRO_BUILD_MEMPROBE=ON`) |
| `.work/backups/broken-230-crashing-20261006-131745/` | The crashing build, kept |
| `.work/backups/pre-230-20261006-084952/` | The 2.2.0 build (loads, processes nothing) |

## 8. The process fix that matters more than any single defect

Every defect in sections 1, 4.1, 4.2 and 4.4 was invisible to a build that
succeeds and a test binary that reports green. The gate that should have caught
all of them is one rule:

> The tests must build and pass against the exact source being shipped.

A release that ships while its own test target fails to compile is not gated at
all. This belongs in Phase 0 of the roadmap, ahead of any audio change.
