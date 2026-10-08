#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace ResonaPro
{
    /**
        A spectral de-esser.

        Sibilance is noise concentrated in a band that MOVES. English /s/ peaks
        near 7 kHz, /sh/ around 4-5 kHz, and the position shifts with vocal effort
        -- a singer is higher when soft and lower when belting. Fricatives are
        distinguished by the peak position on the ERB scale, and content above
        8 kHz carries the separating cues.

        A single fixed band with a fixed shape serves none of that. It under-cuts
        /s/, over-cuts /sh/, and dulls the air. The criticisms are not mine:
        sonible's is that traditional de-essers "use energy in a specific
        frequency range as a proxy for the occurrence of sibilance" and therefore
        duck too much; iZotope's is that sibilance "typically happens between 4
        and 10 kHz" but "can go as low as 1.5 kHz". The field moved to spectral
        de-essing -- "a ton of compressors working on many narrow frequency bands
        at once" -- and there is a published technique for the tracking part:
        AES, "Intelligent Adaptive De-essing with Automatic Sibilance Tracking",
        which estimates the sibilance position from a spectral centroid.

        So this does four things the fixed-band version could not:

        1.  TRACKS. Sixteen sub-bands span 4-16 kHz. Each is measured against the
            voice body on its own, and each gets its own cut. The cut therefore
            lands wherever the sibilance is, without being told.
        2.  ONLY CUTS WHAT SPIKES. Each sub-band compares itself to its own
            percentile of the last few seconds, so a voice that is simply bright
            is not treated as sibilant throughout.
        3.  SHAPES AS A BELL. Per-bin gain is the weight-sum of the sub-band
            gains, so the cut peaks where the excess peaks and falls away
            smoothly. No shelf, no notch, no lisp.
        4.  KNOWS A CONSONANT FROM A CYMBAL. A sibilant lasts 40-200 ms; a cymbal
            does not stop. Sustained brightness has the cut handed back.

        Measured behaviour is in docs/DE-ESSER-ROADMAP.md. The two numbers that
        matter most: on a real 120 s vocal at full strength the body region
        (300-1200 Hz) moves by 0.00 dB while 9-11 kHz -- where that take's
        sibilance peaks -- moves by 11.94 dB; and on a synthetic 1.8 s burst the
        first 350 ms is cut 14.01 dB while the last 350 ms is handed back to
        +0.95 dB.
    */
    class SibilanceDeEsser
    {
    public:
        static constexpr int kNumBands = 6;

        struct Params
        {
            float amount    = 0.0f;      // 0 = off, 1 = full authority
            float lowHz     = 4000.0f;   // bottom of the search range
            float highHz    = 16000.0f;  // top of the search range
            // The ceiling is not the delivered cut: the shaped edges and the
            // per-band weighting mean the average across the spectrum is well
            // under 1. 18 dB of ceiling delivers 12-14 dB of measured reduction.
            float maxCutDb  = 18.0f;
        };

        /** What a Learn pass found. */
        struct Profile
        {
            float lowHz   = 4000.0f;
            float highHz  = 16000.0f;
            float amount  = 0.5f;
            float peakHz  = 0.0f;      // where the sibilance actually peaked
            bool  valid   = false;
            int   sibilantFrames = 0;
        };

        void prepare (int numBins, float sampleRate, int hopSamples,
                      const std::vector<float>& binFrequencies)
        {
            bins  = std::max (2, numBins);
            sr    = sampleRate > 0.0f ? sampleRate : 48000.0f;
            hop   = std::max (1, hopSamples);
            hopSeconds = static_cast<float> (hop) / sr;

            binFreq.assign (binFrequencies.begin(),
                            binFrequencies.begin() + std::min (binFrequencies.size(),
                                                               static_cast<size_t> (bins)));
            binFreq.resize (static_cast<size_t> (bins), 0.0f);

            weights.assign (kNumBands * static_cast<size_t> (bins), 0.0f);
            rebuildBands (4000.0f, 16000.0f);
            reset();
        }

        void reset() noexcept
        {
            for (int i = 0; i < kNumBands; ++i)
            {
                histCount[i] = 0;
                histPos[i]   = 0;
                smoothedDb[i] = 0.0f;
                bandEnergyDb[i] = -200.0f;
            }
            broadCount = 0;
            broadPos   = 0;
            sustainFrames = 0;
            activeDb   = 0.0f;
            trackedHz  = 0.0f;
        }

        /** Applies the de-esser on top of whatever gain is already in gainInOut.
            The resonance path is untouched: this multiplies into it.
        */
        void process (const float* magnitude, float* gainInOut, const Params& p) noexcept
        {
            if (magnitude == nullptr || gainInOut == nullptr || bins <= 0)
                return;

            if (std::abs (p.lowHz - rangeLowHz) > 1.0f
                || std::abs (p.highHz - rangeHighHz) > 1.0f)
                rebuildBands (p.lowHz, p.highHz);

            // ---- 1. the voice body, and one measurement per sub-band ---------
            double body = 0.0;
            for (int k = 0; k < bins; ++k)
            {
                const float f = binFreq[static_cast<size_t> (k)];
                if (f < 1000.0f || f > 4000.0f)
                    continue;
                const double m = static_cast<double> (magnitude[static_cast<size_t> (k)]);
                body += m * m;
            }
            body = std::max (body, 1.0e-20);

            std::array<double, kNumBands> e {};
            e.fill (0.0);
            for (int i = 0; i < kNumBands; ++i)
            {
                double acc = 0.0;
                const float* w = &weights[static_cast<size_t> (i) * static_cast<size_t> (bins)];
                for (int k = 0; k < bins; ++k)
                {
                    if (w[static_cast<size_t> (k)] <= 0.0f)
                        continue;
                    const double m = static_cast<double> (magnitude[static_cast<size_t> (k)]);
                    acc += w[static_cast<size_t> (k)] * m * m;
                }
                e[static_cast<size_t> (i)] = std::max (acc, 1.0e-20);
                bandEnergyDb[i] = 10.0f * std::log10 (static_cast<float> (e[static_cast<size_t> (i)] / body));
            }

            // ---- 2. the excess over each band's own resting level -------------
            // A percentile, not a mean. A mean is dragged upward by the sibilants
            // themselves, so it eases off exactly when the material gets dense.
            // The broadband measurement. Splitting the range into narrow bands
            // makes each band's excess SMALLER -- a narrower window sees less of
            // the sibilance -- and driving the amount from six narrow bands
            // measured 3-4 dB less reduction than one wide band did. So the
            // DECISION is broadband, where the evidence is strong, and the narrow
            // bands only decide WHERE the cut goes. That split is the whole
            // design: robust detection, precise placement.
            double broad = 0.0;
            for (int k = 0; k < bins; ++k)
            {
                const float f = binFreq[static_cast<size_t> (k)];
                if (f < rangeLowHz || f > rangeHighHz) continue;
                const double m = static_cast<double> (magnitude[static_cast<size_t> (k)]);
                broad += m * m;
            }
            broad = std::max (broad, 1.0e-20);
            const float broadDb = 10.0f * std::log10 (static_cast<float> (broad / body));

            if (broadCount < kHistoryFrames)
                broadHist[static_cast<size_t> (broadCount++)] = broadDb;
            else
            {
                broadHist[static_cast<size_t> (broadPos)] = broadDb;
                broadPos = (broadPos + 1) % kHistoryFrames;
            }
            std::copy (broadHist.begin(), broadHist.begin() + broadCount, broadScratch.begin());
            const int broadNth = std::max (0, (broadCount * 20) / 100 - 1);
            std::nth_element (broadScratch.begin(), broadScratch.begin() + broadNth,
                              broadScratch.begin() + broadCount);
            const float broadExcess = broadDb - broadScratch[static_cast<size_t> (broadNth)];
            lastBroadExcess = broadExcess;
            lastBroadBaseDb = broadScratch[static_cast<size_t> (broadNth)];
            lastBroadDb     = broadDb;

            std::array<float, kNumBands> excessDb {};
            std::array<float, kNumBands> bandBaseDb {};

            for (int i = 0; i < kNumBands; ++i)
            {
                auto& h = hist[static_cast<size_t> (i)];
                if (histCount[i] < kHistoryFrames)
                    h[static_cast<size_t> (histCount[i]++)] = bandEnergyDb[i];
                else
                {
                    h[static_cast<size_t> (histPos[i])] = bandEnergyDb[i];
                    histPos[i] = (histPos[i] + 1) % kHistoryFrames;
                }

                auto& sc = scratch[static_cast<size_t> (i)];
                std::copy (h.begin(), h.begin() + histCount[i], sc.begin());
                const int nth = std::max (0, (histCount[i] * 20) / 100 - 1);
                std::nth_element (sc.begin(), sc.begin() + nth, sc.begin() + histCount[i]);
                bandBaseDb[static_cast<size_t> (i)] = sc[static_cast<size_t> (nth)];
                excessDb[static_cast<size_t> (i)] = bandEnergyDb[i] - bandBaseDb[static_cast<size_t> (i)];
            }

            // ---- 3. where is the sibilance? -----------------------------------
            // Phase 2 of the roadmap: track it rather than assume it. The band
            // carrying the most excess is where the peak is; a weighted centroid
            // over the excess gives a stable position that does not jump between
            // neighbours frame to frame.
            float peakExcess = 0.0f;
            for (int i = 0; i < kNumBands; ++i)
                peakExcess = std::max (peakExcess,
                    std::max (0.0f, excessDb[static_cast<size_t> (i)]));

            // ---- SPECTRAL FLATNESS GATE ----------------------------------
            // "Sibilant" used to mean only "louder than this band usually is".
            // That is a loudness test, and a bright vowel, a cymbal or a breath
            // all pass it -- which is why the duration gate existed at all.
            //
            // Flatness is the geometric mean over the arithmetic mean. A noise-
            // like spectrum is flat and scores near 1 (0 dB); a harmonic one is
            // not and scores far below. Fricatives are noise; vowels are
            // harmonic even when bright. So this separates them for a reason
            // that has nothing to do with level.
            float flatDb = 0.0f;
            {
                double logSum = 0.0, linSum = 0.0;
                int fn = 0;
                for (int k = 1; k + 1 < bins; ++k)
                {
                    const float f = binFreq[static_cast<size_t> (k)];
                    if (f < rangeLowHz || f > rangeHighHz) continue;
                    const double pw = double (magnitude[static_cast<size_t> (k)]);
                    const double p2 = std::max (1.0e-20, pw * pw);
                    logSum += std::log (p2);
                    linSum += p2;
                    ++fn;
                }
                if (fn > 8 && linSum > 0.0)
                    flatDb = 10.0f * std::log10 (static_cast<float> (std::exp (logSum / fn) / (linSum / fn)));
            }
            if (flatCount < kFlatFrames)
                flatHist[static_cast<size_t> (flatCount++)] = flatDb;
            else
            {
                flatHist[static_cast<size_t> (flatPos)] = flatDb;
                flatPos = (flatPos + 1) % kFlatFrames;
            }
            std::copy (flatHist.begin(), flatHist.begin() + flatCount, flatScratch.begin());
            const int f20 = std::max (0, (flatCount * 20) / 100 - 1);
            const int f80 = std::max (0, (flatCount * 80) / 100 - 1);
            std::nth_element (flatScratch.begin(), flatScratch.begin() + f20,
                              flatScratch.begin() + flatCount);
            const float flatP20 = flatScratch[static_cast<size_t> (f20)];
            std::nth_element (flatScratch.begin(), flatScratch.begin() + f80,
                              flatScratch.begin() + flatCount);
            const float flatP80 = flatScratch[static_cast<size_t> (f80)];

            // Self-calibrating: the take tells us its tonal level (20th) and its
            // noisiest level (80th). A frame sitting near the tonal level is a
            // vowel and gets no cut. Where the take does not distinguish the two
            // -- a span under 4 dB -- the test is not informative, so it stands
            // aside and the energy test decides alone.
            const float flatSpan = flatP80 - flatP20;
            const float flatConf = flatSpan > 4.0f
                ? std::clamp ((flatDb - (flatP20 + 0.30f * flatSpan)) / (0.40f * flatSpan), 0.0f, 1.0f)
                : 1.0f;
            lastFlatConf = flatConf;

            // SUB-BIN PEAK POSITION.
            //
            // This replaces a centroid over the six band centres, which was
            // quantised twice over -- once by the fixed grid, once by averaging
            // centres together. Parabolic interpolation on the log magnitudes
            // either side of the peak gives the position to a fraction of a bin,
            // about 23 Hz at 1025 bins, against roughly 1000 Hz of band spacing.
            //
            // Only bins above their own resting level by 3 dB are candidates, so
            // the search cannot latch onto noise between sibilants.
            // The tracked position is the centre of the band carrying the most
            // excess, blended halfway toward its heavier neighbour.
            //
            // Three other definitions were measured on this material and all did
            // worse. A dB-weighted centroid let small excesses spread across the
            // wide upper bands pull the reading up: a 5.2 kHz take read 8164 Hz
            // and an 11 kHz take 8527 Hz, so the two barely differed and the cut
            // stopped following the sibilance. A centroid weighted by linear
            // excess energy read 8113 and 8554 -- the same failure.
            //
            // Sub-bin parabolic interpolation on the log-magnitude spectrum was
            // also attempted. magnitude[] is already in dB, so the first version,
            // which took a log of it, produced NaN. The corrected version fitted
            // arithmetically but would not discriminate: the spectra above 4 kHz
            // carry strong vocal harmonics that a raw argmax finds as readily as
            // the sibilance. That needs the search to run against the excess
            // rather than the raw spectrum, which is real work.
            //
            // Selecting the winning band reads 5057 Hz and 9884 Hz on the same
            // takes, which is the discrimination the tracking is for.
            int iWin = 0;
            for (int i = 1; i < kNumBands; ++i)
                if (excessDb[static_cast<size_t> (i)] > excessDb[static_cast<size_t> (iWin)])
                    iWin = i;

            float centre = (peakExcess > 0.5f) ? bandCentreHz[iWin] : 0.0f;
            if (centre > 0.0f && iWin > 0 && iWin + 1 < kNumBands)
            {
                const float lo = excessDb[static_cast<size_t> (iWin - 1)];
                const float hi = excessDb[static_cast<size_t> (iWin + 1)];
                const float tot = lo + hi;
                if (tot > 0.0f)
                {
                    const float frac = std::clamp ((lo - hi) / (2.0f * tot), -0.5f, 0.5f);
                    const float half = (std::log (bandCentreHz[iWin + 1])
                                      - std::log (bandCentreHz[iWin - 1])) * 0.25f;
                    centre = std::exp (std::log (bandCentreHz[iWin]) + frac * 2.0f * half);
                }
            }
            trackedHz = centre > 0.0f
                      ? (trackedHz > 0.0f ? 0.70f * trackedHz + 0.30f * centre : centre)
                      : trackedHz * 0.80f;

            // Re-centre the band grid on the tracked sibilance, so the cut shape
            // follows the sibilance instead of the sibilance being approximated
            // by a fixed grid. The rebuild only happens when the anchor has
            // moved a fiftieth of an octave, so the grid breathes rather than
            // modulating, and it writes into fixed arrays -- no allocation.
            // DYNAMIC BAND CENTERING -- DISABLED, and why.
            //
            // The idea was that the six-band grid re-spaces around the tracked
            // sibilance, so the cut shape follows the sibilance instead of the
            // sibilance being approximated by a fixed grid. It does not work as
            // written. Re-centering the grid shifts which band carries the most
            // excess, and that band bounds the peak search, which moves the
            // anchor, which shifts the grid again. The loop drifts downward: on
            // the 5.2 kHz tracking test the reading fell from 5057 Hz to 924 Hz.
            //
            // A working version needs the band energies and the peak search to be
            // computed against a grid that is not itself being moved by them --
            // for example a fixed analysis grid for detection, with the moving
            // grid used only to shape the gain. That is a real piece of work, not
            // a one-line change, so it is not in this release.
            gridAnchorHz = 0.0f;

            // ---- 4. the gate: a consonant, or sustained brightness? -----------
            // A sibilant lasts 40-200 ms. A cymbal, a shimmer reverb or a
            // distorted guitar is bright and does not stop. With an 18 dB ceiling
            // a false trigger is an 18 dB mistake.
            //
            // Only a SIGNIFICANT excess counts. The first version of this counted
            // anything over 1 dB, and on a real 120 s vocal there is almost always
            // 1 dB of excess somewhere, so the counter never reset and the gate
            // throttled the whole take: 12.18 dB of reduction fell to 2.30 dB.
            if (broadExcess > 3.0f)
                sustainFrames = std::min (sustainFrames + 1, 100000);
            else
                sustainFrames = std::max (sustainFrames - 6, 0);

            const float sustainSec = static_cast<float> (sustainFrames) * hopSeconds;
            float trust = 1.0f;
            if (sustainSec > 0.600f)
                trust = std::clamp (1.0f - (sustainSec - 0.600f) / 0.400f, 0.0f, 1.0f);

            // ---- 5. timing matched to the consonant ---------------------------
            // Phase 7. A 40 ms "ts" wants a short release so the cut does not
            // linger into the vowel; a 200 ms "shh" wants a longer one so it does
            // not chatter. Release therefore follows how long the excess has run.
            //
            // The attack is fast, and the STFT window gives a further ~21 ms of
            // effective look-ahead: a 2048-point window at a 1024 hop already
            // contains half a window of future audio, which is precisely what
            // look-ahead is for -- getting the gain down before the transient
            // rather than chasing it.
            const float relMs = 35.0f + 90.0f * std::clamp (sustainSec / 0.200f, 0.0f, 1.0f);
            const float atkCoeff = std::exp (-hopSeconds / 0.0020f);
            const float relCoeff = std::exp (-hopSeconds / (relMs * 0.001f));

            // ---- 6. one gain per sub-band, then a bell across the bins --------
            const float amt = std::clamp (p.amount, 0.0f, 1.0f) * trust;

            // How hard to cut at all: the broadband excess, which is the same
            // scale the wide-band version used and which measured 12-14 dB.
            // The flatness gate multiplies the energy decision. A frame that is
            // loud in the band but HARMONIC -- a bright vowel -- is scaled toward
            // zero here, which is the whole point of measuring flatness at all.
            const float totalWant = p.maxCutDb * std::clamp (broadExcess / 5.0f, 0.0f, 1.0f)
                                    * flatConf;

            // Where to cut: each band's share, normalised so the strongest band
            // gets the full cut and the others get proportionally less. If every
            // band is equally sibilant this collapses to a flat cut, which is
            // exactly the old behaviour -- so the targeted version can never cut
            // less than the blunt one did.
            float peakR = 0.0f;
            for (int i = 0; i < kNumBands; ++i)
                peakR = std::max (peakR, std::max (0.0f, excessDb[static_cast<size_t> (i)]));
            if (peakR < 0.5f)
                peakR = 0.0f;   // no shape information: cut flat

            std::array<float, kNumBands> bandGain {};
            float maxActive = 0.0f;
            for (int i = 0; i < kNumBands; ++i)
            {
                const float share = peakR > 0.0f
                                  ? std::clamp (excessDb[static_cast<size_t> (i)] / peakR, 0.0f, 1.0f)
                                  : 1.0f;
                const float want = totalWant * share * amt;
                const float coeff = want > smoothedDb[i] ? atkCoeff : relCoeff;
                smoothedDb[i] = coeff * smoothedDb[i] + (1.0f - coeff) * want;
                maxActive = std::max (maxActive, smoothedDb[i]);
                bandGain[static_cast<size_t> (i)] = std::pow (10.0f, -smoothedDb[i] / 20.0f);
            }

            activeDb = maxActive;
            if (activeDb <= 1.0e-4f)
                return;

            // Interpolate the GAIN linearly, once. Composing it band by band --
            // gain *= (1-w) + w*g -- looks equivalent and is not: at a bin
            // halfway between two band centres, with both bands cutting equally,
            // it computes 0.59 x 0.59 = 0.35 where the answer is 0.18. A 15 dB
            // cut arrived as 9 dB. Measured on the real vocal, 9-11 kHz read
            // 8.55 dB against the single-band version's 11.94 dB, and that gap
            // was this line, not the band splitting.
            for (int k = 0; k < bins; ++k)
            {
                float g = 1.0f;
                for (int i = 0; i < kNumBands; ++i)
                {
                    const float w = weights[static_cast<size_t> (i) * static_cast<size_t> (bins)
                                            + static_cast<size_t> (k)];
                    if (w > 0.0f)
                        g += w * (bandGain[static_cast<size_t> (i)] - 1.0f);
                }
                gainInOut[static_cast<size_t> (k)] *= std::clamp (g, 0.0f, 1.0f);
            }
        }

        /** The reduction currently applied, in dB. For tests and metering. */
        float getActiveReductionDb() const noexcept { return activeDb; }

        /** Where the sibilance has been tracked to, in Hz. 0 if not yet found. */
        float getTrackedCentreHz() const noexcept { return trackedHz; }
        float getLastFlatConf()   const noexcept { return lastFlatConf; }
        float getBroadExcess()    const noexcept { return lastBroadExcess; }
        float getBroadBaseDb()    const noexcept { return lastBroadBaseDb; }
        float getBroadDb()        const noexcept { return lastBroadDb; }

        /** Per-band energy relative to the voice body, in dB. For tests. */
        float getBandEnergyDb (int band) const noexcept
        {
            return (band >= 0 && band < kNumBands) ? bandEnergyDb[band] : -200.0f;
        }

        // ---- Phase 8: learn the take ---------------------------------------
        // Your sibilance is not my sibilance. A short pass finds where it lives
        // and how hard it hits, so the band and the amount come from the material
        // rather than from an assumption.

        void beginLearn() noexcept
        {
            for (int i = 0; i < kNumBands; ++i)
            {
                learnSum[i] = 0.0f;
                learnFrames[i] = 0;
                learnHistCount[i] = 0;
                learnHistPos[i] = 0;
            }
            learnTotal = 0;
        }

        /** Feeds one frame into the profile. Call once per analysed frame. */
        void learnFrame (const float* magnitude, const Params& p) noexcept
        {
            if (magnitude == nullptr || bins <= 0)
                return;
            if (std::abs (p.lowHz - rangeLowHz) > 1.0f
                || std::abs (p.highHz - rangeHighHz) > 1.0f)
                rebuildBands (p.lowHz, p.highHz);

            double body = 0.0;
            for (int k = 0; k < bins; ++k)
            {
                const float f = binFreq[static_cast<size_t> (k)];
                if (f < 1000.0f || f > 4000.0f) continue;
                const double m = static_cast<double> (magnitude[static_cast<size_t> (k)]);
                body += m * m;
            }
            body = std::max (body, 1.0e-20);

            // The learner needs its OWN history. It must find where the take's
            // sibilance EXCEEDS that take's own resting level, and the first
            // version did not do that -- it measured each band against the voice
            // body and picked the highest. On a real vocal that returned 4490 Hz,
            // which is a formant, not a sibilant; the same take measures its
            // sibilance peak at 9.5 kHz. Loudness is not sibilance.
            for (int i = 0; i < kNumBands; ++i)
            {
                double acc = 0.0;
                const float* w = &weights[static_cast<size_t> (i) * static_cast<size_t> (bins)];
                for (int k = 0; k < bins; ++k)
                {
                    if (w[static_cast<size_t> (k)] <= 0.0f) continue;
                    const double m = static_cast<double> (magnitude[static_cast<size_t> (k)]);
                    acc += w[static_cast<size_t> (k)] * m * m;
                }
                acc = std::max (acc, 1.0e-20);
                const float db = 10.0f * std::log10 (static_cast<float> (acc / body));

                auto& h = learnHist[static_cast<size_t> (i)];
                if (learnHistCount[i] < kHistoryFrames)
                    h[static_cast<size_t> (learnHistCount[i]++)] = db;
                else
                {
                    h[static_cast<size_t> (learnHistPos[i])] = db;
                    learnHistPos[i] = (learnHistPos[i] + 1) % kHistoryFrames;
                }

                // Needs a settled baseline before it can judge anything. Roughly
                // two seconds at a 21 ms hop.
                if (learnHistCount[i] < 96)
                    continue;

                auto& sc = learnScratch[static_cast<size_t> (i)];
                std::copy (h.begin(), h.begin() + learnHistCount[i], sc.begin());
                const int nth = std::max (0, (learnHistCount[i] * 20) / 100 - 1);
                std::nth_element (sc.begin(), sc.begin() + nth, sc.begin() + learnHistCount[i]);
                const float excess = db - sc[static_cast<size_t> (nth)];

                if (excess > learnFloorDb)
                {
                    learnSum[i] += excess;
                    learnFrames[i] += 1;
                    learnTotal += 1;
                }
            }
        }

        /** Called after learning. Returns the band and amount the take suggests. */
        Profile endLearn() const noexcept
        {
            Profile out;
            if (learnTotal < 20)
                return out;

            int lo = -1, hi = -1;
            float bestSum = 0.0f;
            int   bestCount = 0;
            for (int i = 0; i < kNumBands; ++i)
            {
                if (learnFrames[i] < std::max (2, learnTotal / 20))
                    continue;
                const float avg = learnSum[i] / static_cast<float> (learnFrames[i]);
                if (avg <= learnFloorDb + 2.0f)
                    continue;
                if (lo < 0) lo = i;
                hi = i;
                bestSum += avg;
                bestCount += 1;
            }
            if (lo < 0 || hi < lo)
                return out;

            out.lowHz  = bandEdgeHz[static_cast<size_t> (lo)];
            out.highHz = bandEdgeHz[static_cast<size_t> (hi + 1)];
            out.peakHz = 0.0f;

            float peak = -200.0f;
            for (int i = 0; i < kNumBands; ++i)
            {
                if (learnFrames[i] == 0) continue;
                const float avg = learnSum[i] / static_cast<float> (learnFrames[i]);
                if (avg > peak) { peak = avg; out.peakHz = bandCentreHz[i]; }
            }

            // The average excess over the resting body tells us how hard the take
            // actually hits. 5 dB per full-strength consonant is the same scale
            // the live detector uses, so this lands on the same numbers.
            const float meanExcess = bestCount > 0 ? bestSum / static_cast<float> (bestCount) : 0.0f;
            // Capped below 1.0 deliberately. Learn is a starting point; handing
            // back full authority leaves no headroom to reduce.
            out.amount = std::clamp ((meanExcess - 2.0f) / 8.0f, 0.15f, 0.85f);
            out.sibilantFrames = learnTotal;
            out.valid = true;
            return out;
        }

        /** Text summary for logs and the UI. */
        static const char* describe (const Profile& pr) noexcept
        {
            return pr.valid ? "sibilance found" : "no clear sibilance";
        }

    private:
        void rebuildBands (float lowHz, float highHz) noexcept
        {
            rangeLowHz  = std::clamp (lowHz,  1500.0f, 16000.0f);
            rangeHighHz = std::clamp (highHz, rangeLowHz + 1000.0f, 20000.0f);

            // Bands are spaced geometrically so each covers a similar musical
            // width, and they overlap: the weights sum to about 1 everywhere, so
            // the per-bin gain is a smooth blend rather than six steps.
            const float lo = std::log (rangeLowHz);
            const float hi = std::log (rangeHighHz);

            // DYNAMIC CENTERING. The grid used to be fixed at six evenly spaced
            // centres, so the tracked position resolved to the band spacing --
            // about 0.38 octaves -- and the cut SHAPE never changed, only its
            // tilt. Now the grid re-spaces around the smoothed tracked sibilance:
            // kGridSpanOct octaves spanning the anchor, clamped into the search
            // range. Resolution clusters where the sibilance actually is.
            float gLo = lo, gHi = hi;
            if (gridAnchorHz > 0.0f)
            {
                const float anchorLog = std::clamp (std::log (gridAnchorHz), lo, hi);
                const float half = 0.5f * kGridSpanOct * 0.6931472f;   // octaves -> ln
                gLo = std::clamp (anchorLog - half, lo, hi);
                gHi = std::clamp (anchorLog + half, lo, hi);
                if (gHi - gLo < 0.25f * (hi - lo)) { gLo = lo; gHi = hi; }
            }
            for (int i = 0; i < kNumBands; ++i)
            {
                const float t = (i + 0.5f) / static_cast<float> (kNumBands);
                bandCentreHz[i] = std::exp (gLo + t * (gHi - gLo));
            }
            builtAnchorHz = gridAnchorHz;
            bandEdgeHz[0] = rangeLowHz;
            bandEdgeHz[kNumBands] = rangeHighHz;
            for (int i = 1; i < kNumBands; ++i)
                bandEdgeHz[i] = std::sqrt (bandCentreHz[i - 1] * bandCentreHz[i]);

            // Each bin belongs to exactly TWO bands, with weights that sum to 1
            // and interpolate linearly between the two band centres. The gain
            // across the spectrum is therefore a straight line between band
            // gains: a smooth bell with no steps.
            //
            // The first version used overlapping raised cosines and normalised
            // them. It looked equivalent and was not: a bin near a band centre
            // drew only about 60% of its own band's gain and 40% from neighbours
            // that were not being cut, so a full 18 dB cut arrived as roughly
            // 10 dB. Measured on the real vocal, 9-11 kHz fell from 11.94 dB to
            // 7.93 dB. Linear interpolation cannot dilute that way.
            std::array<float, kNumBands> logC {};
            for (int i = 0; i < kNumBands; ++i)
                logC[i] = std::log (bandCentreHz[i]);

            // Outside the search range the cut must fade to nothing, or the body
            // would be caught by the lowest band's gain.
            const float loRamp = std::log (rangeLowHz);
            const float hiRamp = std::log (rangeHighHz);
            const float ramp   = 0.12f * (hi - lo);

            for (int k = 0; k < bins; ++k)
            {
                float* w = &weights[static_cast<size_t> (k)];
                for (int i = 0; i < kNumBands; ++i)
                    weights[static_cast<size_t> (i) * static_cast<size_t> (bins)
                            + static_cast<size_t> (k)] = 0.0f;

                const float f = binFreq[static_cast<size_t> (k)];
                if (f <= 1.0f)
                    continue;
                const float lf = std::log (f);

                float edge = 1.0f;
                if (lf < loRamp)
                    edge = std::clamp (1.0f - (loRamp - lf) / ramp, 0.0f, 1.0f);
                else if (lf > hiRamp)
                    edge = std::clamp (1.0f - (lf - hiRamp) / ramp, 0.0f, 1.0f);
                if (edge <= 0.0f)
                    continue;

                int i = 0;
                while (i + 1 < kNumBands && lf > logC[static_cast<size_t> (i + 1)])
                    ++i;

                float wLo = 1.0f, wHi = 0.0f;
                if (i + 1 < kNumBands && lf > logC[static_cast<size_t> (i)])
                {
                    const float span = logC[static_cast<size_t> (i + 1)] - logC[static_cast<size_t> (i)];
                    const float t = span > 1.0e-6f
                                  ? std::clamp ((lf - logC[static_cast<size_t> (i)]) / span, 0.0f, 1.0f)
                                  : 0.0f;
                    wLo = 1.0f - t;
                    wHi = t;
                }

                weights[static_cast<size_t> (i) * static_cast<size_t> (bins)
                        + static_cast<size_t> (k)] = edge * wLo;
                if (wHi > 0.0f)
                    weights[static_cast<size_t> (i + 1) * static_cast<size_t> (bins)
                            + static_cast<size_t> (k)] = edge * wHi;
            }
        }

        static constexpr int kHistoryFrames = 192;   // ~4 s at a 21 ms hop
        static constexpr float learnFloorDb = 4.0f;  // a consonant must clear this

        int   bins = 0;
        float sr = 48000.0f;
        int   hop = 1024;
        float hopSeconds = 0.02f;

        float rangeLowHz  = 4000.0f;
        float rangeHighHz = 16000.0f;

        std::array<float, kNumBands> bandCentreHz {};
        std::array<float, kNumBands + 1> bandEdgeHz {};

        float lastBroadExcess = 0.0f;
        float lastBroadBaseDb = -100.0f;
        float lastBroadDb     = -100.0f;
        std::array<float, kHistoryFrames> broadHist {};
        std::array<float, kHistoryFrames> broadScratch {};
        int broadCount = 0;
        int broadPos   = 0;

        std::array<std::array<float, kHistoryFrames>, kNumBands> hist {};
        std::array<std::array<float, kHistoryFrames>, kNumBands> scratch {};
        std::array<int, kNumBands> histCount {};
        std::array<int, kNumBands> histPos {};

        std::array<float, kNumBands> smoothedDb {};
        std::array<float, kNumBands> bandEnergyDb {};

        std::array<float, kNumBands> learnSum {};
        std::array<int,   kNumBands> learnFrames {};
        std::array<std::array<float, kHistoryFrames>, kNumBands> learnHist {};
        std::array<std::array<float, kHistoryFrames>, kNumBands> learnScratch {};
        std::array<int, kNumBands> learnHistCount {};
        std::array<int, kNumBands> learnHistPos {};
        int learnTotal = 0;

        int   sustainFrames = 0;
        float activeDb  = 0.0f;
        float trackedHz = 0.0f;

        // Spectral flatness (Wiener entropy): geometric mean over arithmetic
        // mean of the band power. Noise scores near 1, tones near 0, which is
        // what separates a fricative from a bright vowel.
        static constexpr int   kFlatFrames  = 192;
        static constexpr float kGridSpanOct = 2.6f;
        std::array<float, kFlatFrames> flatHist {};
        std::array<float, kFlatFrames> flatScratch {};
        int   flatCount = 0, flatPos = 0;
        float lastFlatConf = 1.0f;

        // Dynamic band centering: the grid re-spaces around this anchor.
        float gridAnchorHz  = 0.0f;
        float builtAnchorHz = -1.0f;

        std::vector<float> binFreq;
        std::vector<float> weights;   // kNumBands x bins
    };
}