# Audio quality: what is measurable, what is not

## The short answer

Three different questions hide inside "does it lose audio quality". They have three different answers.

| Question | Answer | Status |
|---|---|---|
| Does it change the sound when it is not reducing anything? | No. At Depth 0 the plug-in is a bit-exact delay. | Verified, 2.1.0 |
| Does it damage sounds it is not aiming at? | No, once the defect below is fixed. Every harmonic of a plain comb measures 0.00 dB change. | Fixed in 2.1.1 |
| Can a resonance suppressor remove a resonance without any loss at all? | No. Removing energy is the job. | Physics |

So the goal you asked for is reachable in the sense that matters: **the plug-in should never touch what you did not ask it to touch.** That is now true and tested. A suppressor that removes a resonance while leaving the signal bit-identical is not possible, because the removed energy has to go somewhere.

## The defect this investigation found

The probe compared the average magnitude spectrum before and after processing, aligned by the reported latency, so a pure delay reads as 0.000 dB. Running a 190 Hz harmonic comb with **no resonance** through the plug-in at Depth 1.5 exposed a real problem:

| Quality | Deviation before | Deviation after |
|---|---|---|
| Low (1k) | 0.000 dB | 0.000 dB |
| Standard (2k) | 0.000 dB | 0.000 dB |
| **High (4k)** | **0.982 dB rms, worst bin -4.32 dB** | **0.031 dB rms, worst bin -0.24 dB** |
| Ultra (8k) | not measured | 0.00 dB |

Choosing a *higher* quality setting made the sound **worse**, not better. At 4k the plug-in carved up to 4.3 dB out of harmonics that Standard left completely alone.

### Why

The detector compares each peak against a baseline built from the peaks around it. That baseline is measured on a lightly smoothed magnitude spectrum, and the smoothing used a fixed five-tap kernel, meaning plus or minus two bins. Two bins is a fixed *number*, not a fixed *frequency*, so as the transform grows each bin narrows and the kernel spans less and less of the spectrum:

* at 2048 points a bin is 23.4 Hz, so the kernel spans about 94 Hz;
* at 4096 points a bin is 11.7 Hz, so the same kernel spans about 47 Hz.

Harmonics sit 190 Hz apart. At 2048 points the kernel partly bridges the gaps between them, so the baseline tracks the harmonic peaks. At 4096 points it no longer does, so the baseline falls into the valleys between harmonics. Measured directly on the comb, the baseline sat **9 dB below the neighbouring peak at 2048 points but 27 dB below it at 4096**. A baseline that low makes an ordinary harmonic look like a resonance standing above its surroundings, and the plug-in dutifully removed it.

### The fix

The smoothing radius now scales with the transform length, so the kernel spans the same frequency width and keeps the same triangular shape at every Quality setting. A 2048 point transform keeps the original two-bin radius exactly, which is why Low and Standard measure identically before and after.

## What the measurements now say

All figures at 48 kHz, Depth 1.5, Selectivity 0.5, on synthetic material.

| Test | Result | Reading |
|---|---|---|
| Depth 0 residual vs delayed dry | -136 dB | Bit-exact. The plug-in is a pure delay. |
| Depth 0 spectrum deviation | 0.0004 dB | At the float noise floor of the measurement itself. |
| Harmonic comb, no resonance | 0.00 dB on every harmonic, 1k/2k/4k/8k | Untouched. |
| Click transient, peak level | -0.01 dB | Plosives and consonants keep their impact. |
| Click transient, pre-ringing | below -49 dB | No smearing before the transient. |
| Envelope ripple, steady tone | 0.003 dB at 2k/4x | No audible warbling. |
| Selectivity 0.0 | worst bin -1.30 dB | Below 0.5 the plug-in starts touching non-resonant content. |

Two practical consequences follow. **Keep Selectivity at 0.5 or above** unless you deliberately want aggressive behaviour; that single control governs how much collateral change the plug-in causes. And **Response (overlap) at 8x** halves the gain update interval, which measured a 14x reduction in envelope ripple on a steady tone at the cost of roughly double the CPU.

## What is still not established

The numbers above come from synthetic signals. They prove the plug-in leaves a harmonic comb alone and does not smear transients. They do **not** prove it sounds transparent on a real recorded voice, which contains breath, room tone, sibilance, vibrato and pitch drift at the same time. That needs your ears on a real take.

The remaining known limits:

* **Broadband material.** Noise has no true resonances, but it does have random spectral peaks. A detector that hunts for peaks will find some. This is inherent to the method, not a bug, and it is why the plug-in is tuned for voice.
* **Sub-sample phase.** The engine modifies magnitude and keeps the analysis phase. This is standard practice and measured clean here, but a phase-coherent resynthesis would be the next real step in quality. It is a large project, not a tweak.
* **Very low frequencies.** Below roughly 150 Hz a resonance and a note's own harmonics are hard to separate. The plug-in holds back there by design. The Low Band Detail switch adds a longer analysis window to help, and it changes the decision by 1 to 5 dB depending on the material, which is a genuine change rather than a clear improvement.

## How hard was this, and what would the next step cost

| Goal | Difficulty | Done |
|---|---|---|
| Bit-exact passthrough when idle | Trivial. Already true. | Yes |
| No collateral change at any Quality setting | Moderate. One root cause, one scaling fix, plus a regression test. | Yes, 2.1.1 |
| Less warbling on sustained notes | Easy. Raise the default overlap. Costs CPU. | Available now |
| Phase-coherent resynthesis | Hard. New synthesis path, new latency story, fresh tuning. | No |
| Multi-resolution synthesis, not just detection | Hard. Two transforms with independent phase, then a crossfade that does not comb. | No |
| Proven transparency on real vocals | Needs listening tests, not code. | Not started |

## Level matching (the MATCH button)

### Why it exists

Removing a resonance removes energy, so the processed signal is quieter. Measured on a synthetic voice at Depth 1.5, the reduction costs **1.30 dB of perceived loudness**. A quieter signal nearly always sounds better, so without compensation every A/B comparison is biased toward whichever version is louder, and the user ends up choosing on level rather than on tone.

### What changed in 2.2.0

The original version compared plain RMS. That is the wrong quantity here. A resonance suppressor removes energy at chosen frequencies, and the ear does not weight those frequencies equally: 3 dB off 3 kHz changes loudness far more than 3 dB off 60 Hz. Matching on RMS therefore mis-sets the correction.

The measurement now uses **K-weighting (ITU-R BS.1770)**, the same weighting every loudness meter uses. Both stages are designed at the running sample rate, so the response is correct at 44.1 kHz as well as 48 kHz. The shelf centre is 1501 Hz rather than the 1682 Hz quoted in some references, because for shelving filters the centre frequency and "Q" mean different things in different design formulas, and 1501 Hz is the value that actually lands on the published response. Fitted against the published BS.1770 coefficients, the design matches to **0.0004 dB** from 20 Hz to 20 kHz.

Three further corrections went in with it:

* **The boost is capped.** The output peak may rise as far as the louder of a -0.5 dBFS ceiling and whatever the input itself peaked at, and no further. A quiet take is compensated in full; a hot one is never pushed past the level the user was already working at. Only the boost is capped, so the trim can always pull down.
* **Matching is disabled in BYPASS and DELTA.** In DELTA the output is the removed content, which is meant to be quiet, so matching it to the dry level would misrepresent how much was taken out.
* **Both channels are measured.** The original accumulated the left channel only.

### What it measures

From the automated suite, on a synthetic voice at Depth 1.5:

| Take | Match off | Match on |
|---|---|---|
| Realistic level, -11 dBFS peak | -1.30 dB loudness | **+0.05 dB loudness**, +0.10 dB RMS |
| Hot take, peaks above 0 dBFS | | peak change **+0.02 dB** |

So at a realistic mixing level the compensation is complete to within a twentieth of a decibel, and on a signal that already peaks above full scale the correction cannot make the clipping worse.

### What is still not established

The correction is a slow trim: a 0.35 s envelope and a 0.25 s gain smoother. It follows a verse or a phrase, not individual words, which is what you want for a comparison. It is not a limiter and it is not a compressor, and it will not hold a level steady through a performance that moves by many decibels. Judge it on sustained material, and if a take has very uneven dynamics, set the level by ear with the output gain instead.
