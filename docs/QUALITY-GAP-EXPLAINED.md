# The three points, explained

You asked me to go deeper on three things I said in passing: where ResonaPro
differs from soothe2, the symbol table, and what the measurements do not cover.
This note takes them one at a time, in plain language.

---

## Part 1: the algorithmic gap

### Start with what an STFT is

A resonance suppressor cannot work on the raw waveform. It needs to see which
frequencies are present, and how loud each one is. The standard tool for that is
the Short-Time Fourier Transform, or STFT.

Here is the whole idea:

1. Cut the audio into short slices, called frames.
2. Overlap the slices, so no sound sits at the edge of only one frame.
3. Run a Fourier transform on each frame. This reports, for that instant, how
   much energy sits at each frequency.
4. Each frequency slot is called a bin. A frame gives you a number per bin.

That number is a **complex** value, which means it carries two separate pieces of
information: a **magnitude** (how loud that frequency is) and a **phase** (where
in its cycle that frequency sits at that instant).

5. Change the magnitudes however you want.
6. Run an inverse transform on each frame to turn it back into audio.
7. Add the frames back together, overlapping, so they rebuild the original.

Steps 1 to 4 are analysis. Steps 6 and 7 are synthesis. The whole thing is called
a vocoder, and it is how almost every spectral plugin works.

### What ResonaPro does

ResonaPro changes **magnitude only**. It reads the phase from the analysis and
writes the same phase back into the synthesis. It never touches it.

That choice matters more than it sounds. The phase the analysis measured is not a
guess. It is the actual phase of the actual signal. So when the plugin has no
reason to change a frequency, the frame comes back out exactly as it went in, and
the overlap-add rebuilds the original waveform.

This is why the measurements read the way they do:

| Test | Result |
|---|---|
| Depth 0, output against the delayed input | −136 dB — bit-exact |
| Non-resonant harmonic comb, every harmonic | 0.00 dB change |
| Transient pre-ringing | below −49 dB |

A plugin that reconstructs phase cannot make that claim. It would be replacing a
measured value with an estimate, and the estimate would show up in those numbers.

### So where does the remaining artefact come from?

Not from phase. From **gain modulation**.

Suppose a sustained note sits in one bin. Frame 1 gets a gain of −2 dB. Frame 2,
a millisecond later, gets −4 dB. Each frame, reconstructed on its own, is fine.
But frames overlap, and the overlap-add of two differently scaled copies of the
same tone does not equal that tone scaled by some gain. It comes out slightly
amplitude-modulated. Amplitude modulation creates sidebands, heard as a faint
warble on a held note.

Measured, this is small: **0.003 dB of envelope ripple** on a steady tone, and
pre-ringing below −49 dB on a click. That is the entire remaining artefact
budget. Everything else about the signal path measures clean.

### What phase-locked resynthesis would do

The classic method is identity phase locking, published by Laroche and Dolson in
1999. The idea: find the spectral peaks, then force the bins around each peak to
keep the same phase relationships the analysis found. This restores what the
literature calls vertical phase coherence.

It was invented to fix a specific problem: when you time-stretch or pitch-shift
audio, the phase relationships fall apart, and the result sounds "phasey" or
metallic. Laroche and Dolson measured a large improvement in their metric, from
about 6.5 dB to 37 dB of phase-coherence error.

That is a real fix for a real problem. It is just not our problem.

Three costs would come with it:

**One extra frame of latency.** The modern variant needs the next frame to
estimate how phase changes over time. At the default settings that adds about
10.7 ms on top of the 42.7 ms the engine already reports.

**More CPU.** Phase integration walks the time-frequency plane per bin. It costs
far more than the current multiply.

**It trades a correct value for an estimate.** This is the decisive point. Our
phase is measured and exact. Phase reconstruction estimates it. On a processor
that only scales magnitude, throwing away a correct phase to install an estimate
can make the result worse.

Phase reconstruction earns its cost when phase *must* change, as in time
stretching. Here it must not change at all.

### What multi-resolution synthesis would do

This one is worth separating into two halves, because one half already ships.

**Multi-resolution detection is in the plugin.** A 4096-point detector
contributes to the decision below 1.2 kHz while synthesis stays at one
resolution. That is the Low Band Detail switch. The plugin already looks harder
at the low end with a longer window.

**Multi-resolution synthesis is the other half, and it is the hard one.** The
idea: process the low band with a long window and the high band with a short one,
then recombine. Five obstacles stand in the way.

**1. The two transforms have different phase references.** A bin at 400 Hz in a
2048-point transform and a bin at 400 Hz in a 4096-point transform do not carry
comparable phase. The windows differ, so each phase is a differently weighted
average of the signal around that moment. You cannot mix them without first
reconciling the references.

**2. Summing two chains creates comb filtering.** Run two complete
analysis-modify-synthesis chains and add the outputs, and it does not work. A
4096-point window has more group delay than a 2048-point one, so the same event
emerges from the two chains at two different times. Add them and you get
cancellation notches that sweep with frequency.

**3. The band split has to reconstruct.** You would split the signal into bands,
process each at its own resolution, then recombine. The split and the
recombination must sum back to flat. That is achievable, but the two paths have
different latencies, so the short path needs delaying to match the long one. That
delay becomes the plugin's new latency floor.

**4. The decisions must agree at the crossover.** If the low band is judged with a
long window and the high band with a short one, the detector can disagree near
the crossover frequency. A disagreement there is a jump in applied gain, heard as
a tone or a hole.

**5. Cost.** Two transforms, two detectors, crossover filters, and a latency
alignment buffer. Roughly double the CPU and memory.

### The honest summary of Part 1

Both techniques are published, understood, and shipping in commercial software.
Neither is impossible. The reason to hold off is not difficulty. It is that the
measurements show almost no headroom for either one to help, and both would cost
latency and CPU.

Where ResonaPro stands apart from soothe2 is **algorithm**, not architecture.
Closing that gap is a rewrite of the synthesis half of the engine, with a new
latency contract and fresh tuning of every control. It is not a size problem. It
would add a few hundred kilobytes of code, not hundreds of megabytes.

The reference list for all of this is in `RESEARCH-SYNTHESIS-OPTIONS.md`.

---

## Part 2: the symbol table

### What it is

When a program is compiled, the compiler writes down the name of every function
and variable it created, and where each one sits in the file. That list is the
symbol table. It exists so a developer can attach a debugger and see meaningful
names instead of raw memory addresses.

Once the plugin is built and working, that list has no job. The processor does not
read it. It only takes up space.

### The numbers

| | Size |
|---|---|
| arm64 slice, as built | 10.16 MB |
| arm64 slice, stripped | 7.48 MB |
| Saving | 2.70 MB per architecture, 5.4 MB across both |

The command that does it is `strip`. It takes seconds, and the result is verified
by the same test suite.

### Why I did not just do it

Two reasons.

The first is what you actually asked for. Your message was "if you're sacrificing
quality for size don't do that." Stripping makes the download **smaller**. That is
the opposite direction from your concern, and doing it while telling you it does
not matter would have been talking past you.

The second is that it does not touch the thing you care about. It changes how long
the file takes to download. It does not change one sample of audio. A build with
the symbol table and a build without it produce bit-identical output — that is
what the Depth 0 test measures.

So it is available, it is free, and it is your call. Say the word and it is one
rebuild and repackage.

---

## Part 3: what the measurements do not cover

### What synthetic tests are good for

Every number in the tables above comes from signals I generated: a harmonic comb,
a click, a steady tone, a synthetic voice. That is deliberate. A synthetic signal
lets you change one thing and hold everything else still, so when the measurement
moves you know what moved it.

That is how the 4k quality defect was found. A 190 Hz comb with no resonance
should pass through untouched. At High quality it did not, and the reason was
traceable to a smoothing kernel that used a fixed number of bins instead of a
fixed frequency width.

No listening test would have found that. It was a 4.3 dB error on harmonics that
should not have been touched, and it only appeared at one of four quality
settings.

### Why they cannot settle the real question

A real recorded voice is not one signal. It is several at once:

- breath noise, which is broadband
- room tone, which is also broadband
- sibilance — the "s" and "sh" sounds, which are bursts of high-frequency noise
- vibrato, which moves the pitch up and down several times a second
- pitch drift, which moves it slowly
- and the harmonics of the note itself, which move with all of the above

The detector has to separate a resonance — a peak that stays put — from a harmonic
that is supposed to be there and is currently sliding around. It has to do that
while breath and room tone put random peaks into the spectrum.

The plugin is tuned for exactly this. It estimates the spacing between harmonics,
uses that to build a baseline, protects anything that moves, and guards
transients. But "tuned for it" and "proven on it" are different claims, and I have
only tested the second one on signals I made up.

### The specific thing to listen for

The one artefact the measurements predict is the gain-modulation warble described
in Part 1. Its signature is a **watery or phasey quality on sustained notes** —
the long held notes in a ballad, not the fast ones.

So the test is:

1. A real vocal take, the kind of material you would actually use this on.
2. Quality on High, Match on.
3. A/B against bypass, level-matched.
4. Listen to the sustained notes specifically, and specifically on the notes where
   the reduction meter moves most.

If the warble shows up there, phase-coherent resynthesis stops being speculative
and becomes justified. The identity phase-locking variant is the cheaper entry
point, because it needs no extra frame of latency.

If it does not show up, the money is better spent somewhere else.

### What I can and cannot claim

I can claim the signal path is clean, and I can show you the numbers. I can claim
the plugin never touches what it is not aiming at, and I can show you a harmonic
comb that comes back at 0.00 dB.

I cannot claim it sounds transparent on your voice. That takes a take, a pair of
headphones, and ten minutes of your time. It is the last open item on the list,
and it is the one item no amount of code from me can close.

---

## What I would do next, in order

1. **Listen.** A real take, High quality, Match on, A/B against bypass. Ten
   minutes. This decides everything below it.
2. **If the warble appears**, add identity phase locking. It costs no extra
   latency, and the published method is well specified.
3. **If it does not appear**, leave the synthesis path alone and spend the effort
   on the detector instead — better discrimination on breathy and noisy material
   would help more than a cleaner reconstruction of a signal that already
   reconstructs cleanly.
4. **Strip the binary**, if you want the smaller download. Zero audio effect,
   five minutes of work.
