# Phase-coherent and multi-resolution synthesis: what they cost

## First, a correction

The earlier report rated these two techniques **hard**. It did not rate them impossible, and it should not have been read that way. Both are published, understood, and implemented in shipping software. The reason to hold off is not feasibility. It is that the measurements show very little headroom for either one to improve, and both would cost latency and CPU to pursue.

The rest of this note explains what each technique actually requires, so the trade-off is visible.

## What the engine does today

The engine analyses with a single STFT at a fixed window length, scales the magnitude of each bin, leaves the phase alone, and overlap-adds.

Leaving the phase alone is worth dwelling on, because it is the most benign thing a spectral processor can do. The synthesis frames carry the phases the analysis measured, so the phase relationships between overlapping frames are exact. That is why Depth 0 reconstructs at -136 dB and why non-resonant harmonics measure 0.00 dB of change. A gain-only processor starts from a position of strength that a time-stretcher does not have.

The residual artefacts, such as they are, come from **gain modulation between frames**, not from phase incoherence. When adjacent frames receive different gains, their overlap-add is not the same as scaling the underlying sinusoid by that gain. The result is a small amplitude modulation, which produces sidebands. Measured, this sits at 0.003 dB of envelope ripple on a steady tone and below -49 dB of pre-ringing on a click.

## Phase-coherent resynthesis

### What it is

The classic treatment is identity phase locking (Laroche and Dolson). The idea: pick the spectral peaks, and constrain the synthesis phases of the bins around each peak so they keep the same relative phase relationships the analysis found. That restores what the literature calls vertical phase coherence, and it is what makes time-stretched and pitch-shifted audio stop sounding "phasey". Laroche and Dolson report a large improvement in phase-coherence error over the unlocked algorithm, from roughly 6.5 dB to 37 dB in their metric.

A more recent and more general method is the phase-gradient heap integration used by Prusa and Sondergaard. Instead of picking peaks, it estimates the partial derivatives of the STFT phase in both time and frequency, then integrates them across the time-frequency plane, propagating phase along whichever direction the local magnitude favours. It enforces horizontal and vertical coherence together, with no peak picking and no transient detection. It needs the previous, current and one future frame.

### What it costs

* **One extra frame of latency.** Centred phase differencing needs the next frame, so the plugin would have to look one hop ahead. At 2048 points with 4x overlap that is 512 samples, about 10.7 ms, on top of the 42.7 ms the engine already reports.
* **More CPU.** Phase integration is a per-bin operation with a heap-ordered traversal. It costs more than the current multiply.
* **It replaces a correct value with an estimate.** This is the decisive point. The engine's phase is currently *measured*, and it is correct. Phase reconstruction *estimates* it. On a processor that only scales magnitude, discarding a correct phase to install an estimate can make the result worse, not better. Phase reconstruction earns its cost where phase must be *changed* anyway, as in time scaling. Here it must not be changed at all.

### Verdict

The technique solves a problem this plugin largely does not have. It would buy a small reduction in gain-modulation artefacts, at the cost of latency, CPU, and the risk of degrading a phase that is currently exact.

## Multi-resolution synthesis

Multi-resolution **detection** is already in the plugin. A 4096-point detector contributes to the decision below 1.2 kHz while synthesis stays at one resolution. Multi-resolution **synthesis** is the hard half, and it has five distinct obstacles.

### 1. Two transforms have different phase references

The phase of a bin in a 2048-point STFT and the phase of the same frequency in a 4096-point STFT are not comparable. The window differs, so each phase is a differently weighted average of the signal around that instant. You cannot mix them without explicitly reconciling the references.

### 2. Summing two chains comb-filters

Running two complete analysis-modify-synthesis chains at different resolutions and adding the outputs does not work. A 4096-point window has more group delay than a 2048-point one, so the same event emerges at two different times. Summing the two produces cancellation notches that sweep with frequency. Julius Smith's treatment of the multiresolution STFT makes the related point that every channel must be oversampled in time, or the channels get weighted unevenly, and that is for analysis where the channels are combined by smoothing rather than summed as audio.

### 3. The band split must reconstruct

You would band-split, process each band at its own resolution, and recombine. The split and recombination must form a complementary filter bank that sums flat, which is achievable, but the two paths have different latencies. The short path must be delayed to match the long one, and that delay becomes the plugin's new latency floor.

### 4. Decisions must stay consistent across the crossover

If the low band is analysed with a long window and the high band with a short one, the detector sees different material on each side and can disagree near the crossover frequency. A disagreement there is a discontinuity in the applied gain, which is audible as a tone or a hole at the crossover.

### 5. Cost

Two STFT chains, two detectors, crossover filters, and a latency alignment buffer. Roughly double the CPU and memory of the current engine.

### Verdict

Architecturally doable, but it is a rewrite of the synthesis half of the engine with a new latency contract and fresh tuning across every control. And the low-band benefit it offers is already delivered by the existing low-band detector, while the high-band transient benefit is already measured at -49 dB of pre-ringing through the transient guard.

## The sources

* Laroche, J. and Dolson, M. "New phase-vocoder techniques for pitch-shifting, harmonizing and other exotic effects." WASPAA 1999. https://www.ee.columbia.edu/~dpwe/papers/LaroD99-pvoc.pdf
* Laroche, J. and Dolson, M. "Phase-vocoder: about this phasiness business." WASPAA 1997. https://ieeexplore.ieee.org/abstract/document/625603/
* Prusa, Z. and Sondergaard, P. "Phase Vocoder Done Right." EUSIPCO 2022. https://arxiv.org/html/2202.07382v1
* Puckette, M. "Phase-locked Vocoder." IEEE ASSP Workshop 1995. https://msp.ucsd.edu/Publications/mohonk95.pdf
* Roebel, A. "A new approach to transient processing in the phase vocoder." DAFx 2003. https://hal.science/hal-01161124/document
* Smith, J. O. "Overlap-Add STFT Processing" and "Multiresolution STFT", Spectral Audio Signal Processing. https://ccrma.stanford.edu/~jos/sasp/Overlap_Add_OLA_STFT_Processing.html and https://ccrma.stanford.edu/~jos/sasp/Multiresolution_STFT.html
* Lukin, A. and Todd, J. "Adaptive time-frequency resolution for analysis and processing of audio." AES Convention 120, 2006.
* Krawczyk, M. and Gerkmann, T. "STFT phase reconstruction in voiced speech for an improved single-channel speech enhancement." IEEE/ACM TASLP 2014. https://ieeexplore.ieee.org/abstract/document/6891278/
* Ottosen, E. S. and Dorfler, M. "A phase vocoder based on nonstationary Gabor frames." IEEE/ACM TASLP 2017. https://ieeexplore.ieee.org/abstract/document/8031036/
* Duxbury, C., Davies, M., and Sandler, M. "Separation of transient information in musical audio using multiresolution analysis techniques." DAFx 2001. https://www.dafx.de/paper-archive/2001/papers/duxbury.pdf

## What I would actually do next

Neither technique, yet.

Every measurement currently available says the plugin is clean on synthetic material: 0.00 dB of change on non-resonant harmonics, -49 dB of transient pre-ringing, 0.003 dB of warbling, and a bit-exact passthrough at Depth 0. Building phase reconstruction or a second synthesis chain now would be solving a problem that has not been shown to exist, at a real cost in latency and CPU.

The one thing the measurements cannot settle is how the plugin behaves on a real recorded voice, where breath, room tone, sibilance, vibrato and pitch drift arrive together. That is a listening question, not a code question.

So the next step is a real take, at High quality, with Match on, A/B against bypass. The specific thing to listen for is a "watery" or "phasey" quality on sustained notes, which is the signature of gain modulation, and the specific thing to measure is whether it appears on the notes that need the most reduction.

If it does appear, phase-coherent resynthesis moves from speculative to justified, and the identity phase-locking variant is the cheaper entry point because it needs no extra frame of latency. If it does not appear, the money is better spent elsewhere.
