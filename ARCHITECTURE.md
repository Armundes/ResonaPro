# ResonaPro DSP & Software Architecture Guide 📐

> **Note (v2):** this document describes the **original v1 engine**. Several of
> the algorithms it documents were replaced in v2 because they were measurably
> incorrect or misleading — in particular the centre-inclusive linear-domain
> baseline, the fixed-threshold detector, the unused `PitchTracker` /
> `TransientSplitter` pair, and the "oversampling" scheme (which reduced rather
> than increased usable frequency resolution, and was never zero-latency).
>
> The authoritative description of the current engine is
> [docs/RESEARCH-AND-DESIGN.md](docs/RESEARCH-AND-DESIGN.md); the list of defects
> and fixes is in [docs/UPGRADE-REPORT.md](docs/UPGRADE-REPORT.md).

This document provides a deep technical breakdown of the mathematics, algorithms, and JUCE integration inside **ResonaPro** for developers, AI coding assistants (like Manus), and DSP engineers.

---

## 1. High-Level Signal Flow

```
[ Incoming Audio Block (xL, xR) ]
               │
      [ Polyphase Oversampler (1x, 2x, 4x, 8x) ]
               │
    ┌──────────┴──────────┐
    ▼                     ▼
[ Transient Stream ]   [ Sustain Stream ]  <-- Real-time SMS Transient Splitter
    │                     │
[ Circular Delay ]        ▼
 (N samples)           [ WOLA STFT Analysis (Apple vDSP) ]
    │                     │
    │                  [ ISO 226 Human Ear Sensitivity Weighting ]
    │                     │
    │                  [ 8-Band Parametric Sidechain EQ Weighting ]
    │                     │
    │                  [ Spectral Resonance Peak Detector ]
    │                     │
    │                  [ Dynamic Per-Bin Attack/Release Suppressor ]
    │                     │
    │                  [ Complex Spectrum Attenuation ]
    │                     │
    │                  [ Inverse STFT Synthesis (Square-Root Hann COLA) ]
    │                     │
    └──────────┬──────────┘
               ▼
   [ Time-Aligned Recombination ]
               │
   [ Dry/Wet Mix & Delta Solo Switch ]
               │
   [ Polyphase Downsampler ]
               │
   [ Output Audio Block (yL, yR) ]
```

---

## 2. DSP Subsystems

### A. WOLA STFT Engine (`DSP/SpectralEngine.h`)
* **Window Function:** Sqrt-Hann window $w[n] = \sin\left(\frac{\pi(n + 0.5)}{N}\right)$ for both analysis and synthesis.
* **Hop Interval:** $R = N / 4$ ($75\%$ overlap).
* **COLA Identity Condition:**
  $$\sum_{m} w_a[n - mR] \cdot w_s[n - mR] = \sum_{m} \sin^2\left(\frac{\pi(n + 0.5 - mR)}{N}\right) = \frac{N}{2R} = 2.0$$
* **Synthesis Normalization:** With Apple `vDSP_fft_zrip` round-trip gain of $2N$, the exact scaling factor is $\frac{1}{4N}$.
* **Result:** Round-trip transfer function when `depth = 0.0` has **$0.000\%$ harmonic distortion and $< 10^{-15}$ reconstruction error**.

### B. Transient / Sustain Deconstruction (`DSP/TransientSplitter.h`)
* Uses dual fast/slow leaky peak envelope detectors:
  * **Fast Envelope:** $\tau_{\text{attack}} = 1.5\text{ ms}, \tau_{\text{release}} = 10\text{ ms}$.
  * **Slow Envelope:** $\tau_{\text{attack}} = 25\text{ ms}, \tau_{\text{release}} = 80\text{ ms}$.
* **Transient Index:**
  $$\gamma(t) = \text{clamp}\left(\frac{\max(0, E_{\text{fast}}(t) - E_{\text{slow}}(t))}{E_{\text{fast}}(t) + \epsilon} \cdot 1.8, 0.0, 1.0\right)$$
* **Separation:**
  $$x_{\text{transient}}(t) = x(t) \cdot \gamma(t), \quad x_{\text{sustain}}(t) = x(t) \cdot (1 - \gamma(t))$$
* **Plosive Preservation:** $x_{\text{transient}}(t)$ is delayed by $N = 2048$ samples and recombined with the de-resonated sustain, preventing plosive dulling.

### C. Psychoacoustic Resonance Detection (`DSP/ResonanceDetector.h`)
* **Running Smoothed Baseline:** Adaptive moving-average across frequency bins with kernel radius scaled by frequency and `sharpness`:
  $$\text{radius}(k) = \text{clamp}\left(\text{int}(0.05 \cdot k) + \frac{16}{\text{sharpness}}, 3, 64\right)$$
* **Resonance Prominence Ratio:**
  $$R(k) = \frac{X_{\text{mag}}(k)}{X_{\text{smooth}}(k) + \epsilon} - 1.0$$
* **ISO 226 Human Ear Curve:** Normalized equal-loudness weighting boosting sensitivity around the $2.5\text{ kHz} - 5\text{ kHz}$ ear canal resonance zone.
* **Sharpness Exponent:**
  $$\text{PeakStrength}(k) = (\max(0, R(k) - \text{Selectivity} \cdot 0.5))^{\text{sharpness}}$$

### D. 8-Band Sidechain Sensitivity Curve (`DSP/ParametricEQWeighting.h`)
* Evaluates sensitivity multiplier $S(f)$ for each FFT bin:
  $$S(f) = \prod_{b=1}^{8} 10^{\frac{\text{gainDb}_b \cdot \text{Bell}_b(f)}{10.0}}$$
* **Gain Behavior:**
  * $+12\text{ dB}$: Sensitivity increased by $+16\times$ (heavy suppression in that band).
  * $-18\text{ dB}$: Sensitivity reduced to $0.02\times$ (bypasses suppression in that band).
  * $0\text{ dB}$: Completely neutral ($1.0\times$).

---

## 3. Parameter Mapping (`AudioProcessorValueTreeState`)

| Parameter ID | Name | Range | Default | Description |
| :--- | :--- | :--- | :--- | :--- |
| `depth` | Vocal Depth | `0.0 - 4.0` | `0.0` | Master reduction intensity (0.0 = neutral bypass) |
| `sharpness` | Sharpness | `0.2 - 4.0` | `1.0` | Exponent from broad EQ to surgical needle notches |
| `selectivity` | Selectivity | `0.0 - 1.0` | `0.5` | Threshold for ignoring non-resonant background energy |
| `attack` | Attack | `0.5 - 50.0 ms` | `8.0 ms` | Envelope attack time constant |
| `release` | Release | `5.0 - 500.0 ms` | `70.0 ms` | Envelope release time constant |
| `transientPreserve` | Plosive Preserve | `0.0 - 1.0` | `0.0` | Plosive/consonant transient preservation amount |
| `vocalProfile` | Vocal Profile | `0 - 3` | `0` | Lead, De-Ess, Warm De-Mud, Air & Silk |
| `oversampling` | Oversampling | `0 - 3` | `0` | 1x (0ms), 2x, 4x, 8x polyphase oversampling |
| `iso226` | ISO 226 Ear | `Bool` | `true` | Psychoacoustic ear curve weighting |
| `modeHard` | Hard Knee | `Bool` | `false` | Asymptotic soft vs steep linear reduction knee |
| `midSide` | Mid/Side | `Bool` | `false` | M/S processing for stereo separation |
| `deltaListen` | Delta Solo | `Bool` | `false` | Solo the removed resonance content |
| `mix` | Dry/Wet Mix | `0 - 100 %` | `100 %` | Parallel processing dry/wet blend |
| `eq_freq_1..8` | Band 1..8 Freq | `20 - 20000 Hz` | Preset | Center frequency of cue node |
| `eq_gain_1..8` | Band 1..8 Sens | `-24 - +24 dB` | `0.0 dB` | Sensitivity boost/cut for cue node |
| `eq_q_1..8` | Band 1..8 Q | `0.1 - 8.0` | Preset | Bandwidth of cue node |
| `eq_enable_1..8`| Band 1..8 Enable | `Bool` | `true` | Enable state of cue node |

---

## 4. UI & Visualizer (`UI/SpectralVisualizer.h`)
* Renders at **60 FPS** via a lock-free thread-safe atomic array FIFO (`scopeMagnitude` and `scopeReduction`).
* Interactive draggable node coordinates map logarithmic frequency $\log_{10}(f)$ to horizontal pixels and $\text{dB}$ gain to vertical pixels.
* **Double-click listener:** Toggles existing nodes or activates inactive nodes at the clicked mouse coordinates.
* **Mouse wheel listener:** Smoothly expands/contracts the Q factor of the hovered node.
