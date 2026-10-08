#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <limits>
#if defined(__APPLE__)
 #include <Accelerate/Accelerate.h>
#endif

namespace ResonaPro
{
    enum class VocalProfile
    {
        AllRoundLead = 0,
        DeEssSibilance,
        WarmBodyClarity,
        AirAndSilk
    };

    inline const char* getVocalProfileName (VocalProfile p) noexcept
    {
        switch (p)
        {
            case VocalProfile::AllRoundLead:    return "Lead Vocal (All-Round)";
            case VocalProfile::DeEssSibilance:  return "De-Ess / Sibilance";
            case VocalProfile::WarmBodyClarity: return "Warm Body & De-Mud";
            case VocalProfile::AirAndSilk:      return "Air & Silk Polish";
        }
        return "";
    }

    struct DetectorParams
    {
        float sharpness         = 1.0f;  // 0.2 .. 4.0
        float detailTilt        = 0.0f;  // -1.0 .. +1.0 (positive = surgical highs, broad lows)
        float selectivity       = 0.5f;  // 0.0 .. 1.0
        float transientGuard    = 0.5f;  // 0.0 .. 1.0
        float transientActivity = 0.0f;  // 0.0 .. 1.0
        float depth             = 1.0f;  // Drives sensitivity & suppression depth
        float motionProtect     = 0.0f;  // 0=off, 1=distinguish moving harmonics
        bool  hardMode          = false;
        bool  useIso226         = true;
    };

    class ResonanceDetector
    {
    public:
        ResonanceDetector() = default;

        void prepare (int numBins, float sampleRate, int hopSamples)
        {
            bins     = std::max (2, numBins);
            sr       = sampleRate > 0.0f ? sampleRate : 44100.0f;
            binWidth = (sr * 0.5f) / static_cast<float> (bins - 1);

            binFreq.resize (static_cast<size_t> (bins));
            profileOffsetDb.resize (static_cast<size_t> (bins));
            iso226Db.resize (static_cast<size_t> (bins));
            radialBins.resize (static_cast<size_t> (bins));
            periodHistory.fill (0.0f);
            periodHistoryIdx   = 0;
            periodHistoryCount = 0;
            periodThen         = 0.0f;
            prefix.resize (static_cast<size_t> (bins) + 1, 0.0);
            prominenceDb.resize (static_cast<size_t> (bins), 0.0f);
            baselineDb.resize (static_cast<size_t> (bins), -120.0f);
            magDb.resize (static_cast<size_t> (bins), -120.0f);
            previousMagDb.assign (static_cast<size_t> (bins), -240.0f);
            
            float frameMs = 1000.0f * (hopSamples > 0 ? hopSamples : 512) / sr;
            temporalFrames = std::clamp(static_cast<int>(200.0f / std::max(1.0f, frameMs)), 1, 64);
            prominenceHistory.assign(static_cast<size_t>(bins * temporalFrames), 0.0f);
            historyWriteIdx = 0;
            temporalScratch.reserve(static_cast<size_t>(temporalFrames));
            
            magHistory.assign(static_cast<size_t>(bins * hpsFrames), 0.0f);
            magWriteIdx = 0;
            hpsScratch.reserve(static_cast<size_t>(hpsFrames));
            vScratch.reserve(32);
            percussiveFlags.assign(static_cast<size_t>(bins), false);

            resetHistory();
            activeBins.reserve (static_cast<size_t> (bins));
            magSmooth.assign (static_cast<size_t> (bins), 0.0f);
            shoulderScratch.reserve (256);
            peakScratch.reserve (static_cast<size_t> (bins));
            peakPositions.reserve (static_cast<size_t> (bins));

            for (int k = 0; k < bins; ++k)
                binFreq[static_cast<size_t> (k)] = binFrequency (k);

            updateIso226Curve();
            updateProfileWeights (VocalProfile::AllRoundLead);
        }

        void setVocalProfile (VocalProfile profile) { updateProfileWeights (profile); }


        void resetHistory() noexcept
        {
            periodSmooth = 0.0f;
            previousPeriod = 0.0f;
            historyValid = false;
            std::fill (previousMagDb.begin(), previousMagDb.end(), -240.0f);
            lastReferencePromDb = 0.0f;
            std::fill (prominenceHistory.begin(), prominenceHistory.end(), 0.0f);
            historyWriteIdx = 0;
            std::fill(magHistory.begin(), magHistory.end(), 0.0f);
            magWriteIdx = 0;
        }

        float binFrequency (int bin) const noexcept
        {
            return std::max (20.0f, static_cast<float> (bin) * binWidth);
        }

        /** Detects resonance excess per bin.

            @param magnitude  Linear magnitude spectrum [bins].
            @param weightsDb  Per-bin focus band cue weights in dB (from ParametricEQWeighting).
            @param out        Output per-bin resonance excess in dB [bins].
            @param p          Detection parameters.
        */
        void detect (const float* magnitude,
                     const float* trueEnvelope,
                     const float* weightsDb,
                     float* out,
                     const DetectorParams& p)
        {
            if (magnitude == nullptr || out == nullptr || bins <= 0) return;

            // ---- 0. Harmonic-Percussive Separation (HPS) --------------------------
            int magOffset = magWriteIdx * bins;
            for (int k = 0; k < bins; ++k)
                magHistory[static_cast<size_t>(magOffset + k)] = magnitude[k];
            magWriteIdx = (magWriteIdx + 1) % hpsFrames;

            if (p.transientGuard > 0.01f)
            {
                const int vRadius = std::max(3, bins / 64);
                const float guardThreshold = 1.0f + (1.0f - std::clamp(p.transientGuard, 0.0f, 1.0f));
                for (int k = 0; k < bins; ++k)
                {
                    hpsScratch.clear();
                    for (int t = 0; t < hpsFrames; ++t)
                        hpsScratch.push_back(magHistory[static_cast<size_t>(t * bins + k)]);
                    std::nth_element(hpsScratch.begin(), hpsScratch.begin() + hpsFrames / 2, hpsScratch.end());
                    float hMed = hpsScratch[hpsFrames / 2];

                    vScratch.clear();
                    int startBin = std::max(0, k - vRadius);
                    int endBin = std::min(bins - 1, k + vRadius);
                    for (int j = startBin; j <= endBin; ++j)
                        vScratch.push_back(magnitude[j]);
                    std::nth_element(vScratch.begin(), vScratch.begin() + vScratch.size() / 2, vScratch.end());
                    float vMed = vScratch[vScratch.size() / 2];

                    percussiveFlags[static_cast<size_t>(k)] = (vMed > hMed * guardThreshold);
                }
            }
            else
            {
                std::fill(percussiveFlags.begin(), percussiveFlags.end(), false);
            }

            // ---- 1. Magnitude prefix sums and peak envelope -----------------------
            double framePeak = 0.0;
            for (int k = 0; k < bins; ++k)
                if (magnitude[k] > framePeak) framePeak = magnitude[k];

            if (framePeak < 1.0e-7)
            {
                std::fill (out, out + bins, 0.0f);
                std::fill (prominenceDb.begin(), prominenceDb.end(), 0.0f);
                return;
            }

            const float refMag = static_cast<float> (framePeak);
            const float detailTilt = std::clamp (p.detailTilt, -1.0f, 1.0f);

            // The DETAIL dial now runs 0 .. 10. It used to be a raw multiplier
            // whose effect stopped at 1.0, which left three quarters of the
            // travel doing nothing. The dial is spread geometrically instead, so
            // every position changes the analysis window: 0 is the widest window
            // the detector uses, 10 the narrowest.
            const float dialT = std::clamp (p.sharpness / 10.0f, 0.0f, 1.0f);
            const float dialSharpness = 0.20f * std::pow (20.0f, dialT);

            // The window is also allowed to keep narrowing toward the top of the
            // dial, but only down to half the old floor. The floor exists so the
            // span always reaches past the feature being measured; below that the
            // window would read a peak's own flanks as the baseline.
            const float floorScale = 1.0f - 0.5f * dialT;

            prefix[0] = 0.0;
            for (int k = 0; k < bins; ++k)
            {
                prefix[static_cast<size_t> (k) + 1] =
                    prefix[static_cast<size_t> (k)] + static_cast<double> (magnitude[k]);

                magDb[static_cast<size_t> (k)] = magnitude[k] > 1.0e-12f
                                                   ? static_cast<float> (20.0 * std::log10 (magnitude[k]))
                                                   : -240.0f;
            }

            const float activityGate = static_cast<float> (framePeak) * 1.0e-3f;  // -60 dB
            const float audibleGate  = static_cast<float> (framePeak) * 1.5e-3f;  // -56 dB

            // Local magnitude smoothing, with the radius tracking the transform
            // length so behaviour does not change with the resolution setting.
            //
            // Measured: a wider kernel looks like the obvious way to average out
            // noise, but it averages out formant structure just as readily. At
            // radius 6 the voice was cut only 0.61 times as hard as white noise;
            // at radius 2 it is cut 1.64 times as hard. Broadband rejection
            // belongs in the threshold, not in the kernel.
            const int fftLength  = 2 * (bins - 1);
            const int smoothRadius = std::clamp (
                static_cast<int> (std::lround (2.0 * static_cast<double> (fftLength) / 2048.0)),
                2, 6);

            magSmooth[0] = magnitude[0];
            magSmooth[static_cast<size_t> (bins - 1)] = magnitude[bins - 1];
            for (int k = 1; k < bins - 1; ++k)
            {
                const int a = std::max (0, k - smoothRadius);
                const int b = std::min (bins - 1, k + smoothRadius);
                float sum = 0.0f, wsum = 0.0f;
                for (int i = a; i <= b; ++i)
                {
                    const float w = static_cast<float> (smoothRadius + 1)
                                  - std::abs (static_cast<float> (i - k));
                    if (w > 0.0f) { sum += w * magnitude[i]; wsum += w; }
                }
                magSmooth[static_cast<size_t> (k)] = wsum > 0.0f ? sum / wsum : magnitude[k];
            }

            // ---- 2. Harmonic period tracking (for low fundamental separation) ----
            activeBins.clear();
            for (int k = 1; k < bins - 1; ++k)
                if (magnitude[k] >= activityGate)
                    activeBins.push_back (k);

            int minRadiusBins = 0;
            const int maxLag = std::max (4, bins / 16);
            if (activeBins.size() >= 16)
            {
                float bestScore = std::numeric_limits<float>::max();
                int   bestLag   = 0;
                for (int lag = 3; lag <= maxLag; ++lag)
                {
                    float sum = 0.0f;
                    int   cnt = 0;
                    for (int k : activeBins)
                    {
                        const int j = k + lag;
                        if (j > bins - 1) break;
                        if (magnitude[j] < activityGate) continue;
                        sum += std::abs (magDb[static_cast<size_t> (k)] - magDb[static_cast<size_t> (j)]);
                        ++cnt;
                    }
                    if (cnt < 8) continue;
                    const float score = sum / static_cast<float> (cnt);
                    if (score < bestScore) { bestScore = score; bestLag = lag; }
                }

                if (bestLag > 0 && bestScore < 12.0f)
                    minRadiusBins = std::min (static_cast<int> (1.2f * static_cast<float> (bestLag)),
                                              bins / 16);
            }

            if (minRadiusBins > 0)
            {
                if (periodSmooth <= 0.0f)
                    periodSmooth = static_cast<float> (minRadiusBins);
                else
                    periodSmooth += 0.25f * (static_cast<float> (minRadiusBins) - periodSmooth);

                minRadiusBins = static_cast<int> (periodSmooth + 0.5f);
            }
            else if (periodSmooth > 0.5f)
            {
                periodSmooth *= 0.75f;
                minRadiusBins = static_cast<int> (periodSmooth + 0.5f);
            }
            else
            {
                periodSmooth = 0.0f;
                minRadiusBins = 0;
            }

            // ---- 3. ERB Psychoacoustic Auditory Baseline Radii --------------------
            const int maxRadius = std::max (4, bins / 14);
            minRadiusBins = std::clamp (minRadiusBins, 0, maxRadius);

            for (int k = 0; k < bins; ++k)
            {
                const float f = binFreq[static_cast<size_t> (k)];
                const float ratio = std::clamp (std::max (20.0f, f) / 1000.0f, 0.02f, 20.0f);
                const float tiltMul = std::pow (ratio, 0.50f * detailTilt);
                const float effSharpness = std::clamp (dialSharpness * tiltMul, 0.20f, 5.0f);

                // Glasberg & Moore ERB bandwidth:
                const float erbHz = (24.7f * (4.37f * (f * 0.001f) + 1.0f)) * (1.0f / effSharpness);
                const int erbBins = std::clamp (static_cast<int> (erbHz / binWidth), 3, maxRadius);

                // The span has to reach past the feature it is measuring. A window
                // narrower than the bump reads the bump's own flanks as the
                // baseline, which hides wide resonances while leaving single-bin
                // noise spikes standing. An ERB alone is too narrow below 1 kHz.
                //
                // The floor also moves with the tilt. It used to be fixed, and
                // because it is applied as a maximum it overrode the tilted ERB
                // and left Detail Tilt with no effect at all -- the control was
                // wired end to end and could not change a single bin.
                const float floorMul = 1.0f / std::max (0.25f, tiltMul);
                const int wideFloor = std::max (4, static_cast<int> ((bins / 48)
                                                                     * floorScale * floorMul));
                radialBins[static_cast<size_t> (k)] = std::clamp (
                    std::max (std::max (erbBins, minRadiusBins), wideFloor), 3, maxRadius);
            }

            // ---- 4. Prominence per bin -------------------------------------------
            float maxProm = 0.0f;
            for (int k = 0; k < bins; ++k)
            {
                const int rad   = radialBins[static_cast<size_t> (k)];
                const int guard = std::max (2, rad / 3);
                const float fHz = binFreq[static_cast<size_t> (k)];

                const int lFrom = k - rad;
                const int lTo   = k - guard - 1;
                const int rFrom = k + guard + 1;
                const int rTo   = k + rad;

                shoulderScratch.clear();
                appendSamples (shoulderScratch, magSmooth, lFrom, lTo);
                appendSamples (shoulderScratch, magSmooth, rFrom, rTo);

                float base;
                if (shoulderScratch.size() >= 3)
                {
                    // 60th percentile for highs (>= 2.5 kHz) to track true baseline floor beside sibilants
                    // 75th percentile for lows to track harmonic troughs
                    const float pct = (fHz >= 2500.0f) ? 0.60f : 0.75f;
                    const size_t idx = static_cast<size_t> (pct * static_cast<float> (shoulderScratch.size() - 1));
                    std::nth_element (shoulderScratch.begin(),
                                      shoulderScratch.begin() + static_cast<long> (idx),
                                      shoulderScratch.end());
                    base = shoulderScratch[idx];
                }
                else if (! shoulderScratch.empty())
                {
                    base = *std::max_element (shoulderScratch.begin(), shoulderScratch.end());
                }
                else
                {
                    base = magnitude[k];
                }
                if (base <= 0.0f) base = magnitude[k];

                if (trueEnvelope != nullptr)
                {
                    base = std::max(base, trueEnvelope[k] * 0.95f);
                }

                baselineDb[static_cast<size_t> (k)] =
                    base > 1.0e-12 ? static_cast<float> (20.0 * std::log10 (base)) : -240.0f;

                // Compare like with like. The baseline is a percentile of the
                // smoothed shoulders, so the numerator has to be the smoothed
                // magnitude too. Taking the raw magnitude here pitted a single
                // noisy bin against a smoothed floor, which reports more
                // prominence on white noise than on a real resonance and is why
                // the detector muffled breath and fricatives.
                const float mag = magSmooth[static_cast<size_t> (k)];
                float prom;
                if (mag <= 1.0e-12f || base <= 1.0e-12)
                    prom = 0.0f;
                else
                    prom = std::clamp (static_cast<float> (20.0 * std::log10 (mag / base)), -60.0f, 60.0f);

                prominenceDb[static_cast<size_t> (k)] = prom;
            }

            int writeOffset = historyWriteIdx * bins;
            for (int k = 0; k < bins; ++k)
            {
                prominenceHistory[static_cast<size_t>(writeOffset + k)] = prominenceDb[static_cast<size_t>(k)];
            }
            historyWriteIdx = (historyWriteIdx + 1) % temporalFrames;

            // Apply Rolling Median to prominence
            for (int k = 0; k < bins; ++k)
            {
                temporalScratch.clear();
                for (int t = 0; t < temporalFrames; ++t)
                {
                    temporalScratch.push_back(prominenceHistory[static_cast<size_t>(t * bins + k)]);
                }
                const size_t mid = temporalScratch.size() / 2;
                std::nth_element(temporalScratch.begin(), temporalScratch.begin() + static_cast<long>(mid), temporalScratch.end());
                float medianProm = temporalScratch[mid];
                prominenceDb[static_cast<size_t>(k)] = medianProm;
                maxProm = std::max(maxProm, medianProm);
            }


            // ---- 5. Peak Scratch & Reference Monitoring --------------------------
            peakScratch.clear();
            for (int k = 1; k < bins - 1; ++k)
            {
                const float m = magnitude[k];
                if (m >= activityGate && m > magnitude[k - 1] && m > magnitude[k + 1])
                    peakScratch.push_back (prominenceDb[static_cast<size_t> (k)]);
            }

            float referenceProm = 0.0f;
            if (! peakScratch.empty())
            {
                const size_t mid = peakScratch.size() / 2;
                std::nth_element (peakScratch.begin(), peakScratch.begin() + static_cast<long> (mid),
                                  peakScratch.end());
                referenceProm = std::clamp (peakScratch[mid], 0.5f, 20.0f);
            }

            // ---- 6. Per-bin threshold and cue emphasis ---------------------------
            const float hardOffsetDb = p.hardMode ? (2.0f - 1.5f * std::clamp (p.depth, 0.0f, 4.0f)) : 0.0f;

            for (int k = 0; k < bins; ++k)
            {
                const float mag = magnitude[k];
                if (mag < audibleGate)
                {
                    out[k] = 0.0f;
                    continue;
                }

                const float fHz = binFreq[static_cast<size_t> (k)];
                const bool hasCueWeighting = (weightsDb != nullptr);
                const float rawCueWeight = hasCueWeighting ? weightsDb[static_cast<size_t> (k)] : 0.0f;

                // Focus bands bias the detector. They do not gate it.
                //
                // A neutral band (0 dB) means "look here as normal", which is what
                // a user expects from a plugin with no focus set. This used to be a
                // hard gate: a band at or below +0.05 dB produced exactly zero
                // reduction, so the plugin was transparent on its default settings
                // and only acted where a focus band had been raised. Neutral now
                // gives the baseline, a raised band lowers the threshold and
                // deepens the cut, and a cut band raises the threshold and eases
                // it. Cutting a band to exclude it still works; it is now the
                // user's explicit choice rather than the default.
                float effectiveCue = rawCueWeight;

                float nominalThresholdDb;
                if (fHz < 1000.0f)
                    nominalThresholdDb = 5.0f + p.selectivity * 3.5f;
                else if (fHz < 3000.0f)
                    nominalThresholdDb = 4.5f + p.selectivity * 3.0f;
                else if (fHz < 10000.0f)
                    nominalThresholdDb = 4.0f + p.selectivity * 2.5f;
                else
                    nominalThresholdDb = 4.0f + p.selectivity * 2.0f;

                float thr = nominalThresholdDb + hardOffsetDb;
                thr += profileOffsetDb[static_cast<size_t> (k)];
                if (p.useIso226)
                    thr += iso226Db[static_cast<size_t> (k)];



                // CUE THRESHOLD DROP:
                // Lower threshold proportionally so that user cues catch resonances aggressively.
                if (hasCueWeighting)
                {
                    thr -= (effectiveCue * 0.85f);
                }

                float rawExcess = prominenceDb[static_cast<size_t> (k)] - thr;
                if (rawExcess <= 0.0f)
                {
                    out[k] = 0.0f;
                    continue;
                }

                // Reduction scales from the neutral baseline of 1.0.
                //   neutral band   0 dB  -> 1.0x, the designed behaviour
                //   raised band   +6 dB  -> 2.0x
                //   raised band  +18 dB  -> 4.0x, the ceiling
                //   cut band      -6 dB  -> 0.0x, excluded
                const float cueScale = std::clamp (1.0f + effectiveCue / 6.0f, 0.0f, 4.0f);
                float excess = rawExcess * cueScale;

                // Transient protection: completely exclude percussive bins
                if (percussiveFlags[static_cast<size_t>(k)])
                {
                    excess = 0.0f;
                }
                else
                {
                    const float transGuard = std::clamp (p.transientActivity, 0.0f, 1.0f)
                                            * std::clamp (p.transientGuard, 0.0f, 1.0f);
                    if (transGuard > 0.0f)
                    {
                        float guardFactor = 0.99f * transGuard;

                        excess *= std::clamp (1.0f - guardFactor, 0.01f, 1.0f);
                    }
                }

                if (p.hardMode && excess > 0.0f)
                {
                    const float levelDb = 20.0f * std::log10 (std::max (1.0e-9f, mag / refMag));
                    const float levelWeight = std::clamp ((levelDb + 50.0f) / 20.0f, 0.15f, 1.0f);
                    excess *= levelWeight;
                }

                // Note Motion protection (low registers only):
                if (p.motionProtect > 0.0f && historyValid && periodThen >= 3.0f
                    && periodSmooth >= 3.0f && fHz < 1500.0f)
                {
                    // Where this bin's content sat when the note was at its old
                    // pitch. This used to compare against the previous frame's
                    // smoothed period, which is almost the same number, so the
                    // offset was always under a bin and the control never fired.
                    // A voice moves over tens of frames, not one.
                    const float oldBin = k * periodThen / periodSmooth;
                    if (std::abs (oldBin - k) >= 1.0f && oldBin > 1.0f && oldBin < bins - 2)
                    {
                        const int j = static_cast<int> (oldBin);
                        const float moving = std::max ({previousMagDb[static_cast<size_t> (j - 1)],
                                                        previousMagDb[static_cast<size_t> (j)],
                                                        previousMagDb[static_cast<size_t> (j + 1)]});
                        const float fixed = previousMagDb[static_cast<size_t> (k)];
                        const float evidence = std::clamp ((fixed - moving) / 9.0f, -1.0f, 1.0f);
                        excess *= 1.0f + std::clamp (p.motionProtect, 0.0f, 1.0f) * 0.25f * evidence;
                    }
                }

                out[k] = std::clamp (excess, 0.0f, 48.0f);
            }

            out[0] = 0.0f;
            out[bins - 1] = 0.0f;

            lastReferencePromDb = referenceProm;
            std::copy (magDb.begin(), magDb.end(), previousMagDb.begin());
            previousPeriod = periodSmooth;

            // Push this frame's period and read the one from a full history back.
            periodHistory[static_cast<size_t> (periodHistoryIdx)] = periodSmooth;
            periodHistoryIdx = (periodHistoryIdx + 1) % periodHistoryLength;
            if (periodHistoryCount < periodHistoryLength)
                ++periodHistoryCount;
            periodThen = periodHistory[static_cast<size_t> (periodHistoryIdx)];

            historyValid = true;
            (void) maxProm;
        }

        const float* getBaselineDb()  const noexcept { return baselineDb.data(); }
        const float* getProminenceDb() const noexcept { return prominenceDb.data(); }
        float getReferenceProminenceDb() const noexcept { return lastReferencePromDb; }

    private:
        /** Gathers up to 16 evenly spaced samples from src over [from, to].

            The values are what the caller needs. An earlier version pushed the bin
            *index* instead, which turned the shoulder baseline into a number in the
            hundreds and the prominence into a level-versus-position ratio. That made
            prominence negative in every bin, so the detector never asked for any
            reduction at all and the plug-in passed audio through untouched.
        */
        static void appendSamples (std::vector<float>& dest, const std::vector<float>& src,
                                   int from, int to) noexcept
        {
            const int last = static_cast<int> (src.size()) - 1;
            from = std::max (0, from);
            to   = std::min (last, to);
            if (from > to) return;
            const int count = to - from + 1;
            if (count <= 16)
            {
                for (int i = from; i <= to; ++i)
                    dest.push_back (src[static_cast<size_t> (i)]);
                return;
            }
            constexpr int TargetSamples = 16;
            const float step = static_cast<float> (count - 1) / static_cast<float> (TargetSamples - 1);
            for (int s = 0; s < TargetSamples; ++s)
            {
                const int idx = from + static_cast<int> (std::lround (static_cast<float> (s) * step));
                dest.push_back (src[static_cast<size_t> (std::clamp (idx, 0, last))]);
            }
        }

        void updateIso226Curve()
        {
            for (int k = 0; k < bins; ++k)
            {
                const float f = binFreq[static_cast<size_t> (k)];
                float offset = 0.0f;
                if (f < 100.0f)
                    offset = 12.0f * (1.0f - f / 100.0f);
                else if (f >= 2000.0f && f <= 5000.0f)
                    offset = -3.5f * std::sin (3.14159265358979323846f * (f - 2000.0f) / 3000.0f);
                else if (f > 10000.0f)
                    offset = 2.0f * std::min (1.0f, (f - 10000.0f) / 10000.0f);
                iso226Db[static_cast<size_t> (k)] = offset;
            }
        }

        void updateProfileWeights (VocalProfile profile)
        {
            for (int k = 0; k < bins; ++k)
            {
                const float f = binFreq[static_cast<size_t> (k)];
                float off = 0.0f;

                switch (profile)
                {
                    case VocalProfile::AllRoundLead:
                        off = 0.0f;
                        break;

                    case VocalProfile::DeEssSibilance:
                        if (f < 3500.0f)
                            off = 8.0f;
                        else if (f <= 9000.0f)
                            off = -5.0f;
                        else
                            off = 1.0f;
                        break;

                    case VocalProfile::WarmBodyClarity:
                        if (f >= 150.0f && f <= 800.0f)
                            off = -3.5f;
                        else if (f > 4000.0f)
                            off = 4.0f;
                        break;

                    case VocalProfile::AirAndSilk:
                        if (f < 2000.0f)
                            off = 6.0f;
                        else if (f >= 4000.0f && f <= 11000.0f)
                            off = -3.5f;
                        break;
                }

                profileOffsetDb[static_cast<size_t> (k)] = off;
            }
        }

        int   bins     = 1025;
        float sr       = 44100.0f;
        float binWidth = 21.5332f;

        std::vector<float>  binFreq;
        std::vector<float>  profileOffsetDb;
        std::vector<float>  iso226Db;
        std::vector<int>    radialBins;

        // The period from several frames back, so Note Motion can see a note
        // actually move rather than comparing adjacent frames.
        static constexpr int periodHistoryLength = 8;
        std::array<float, periodHistoryLength> periodHistory { };
        int periodHistoryIdx   = 0;
        int periodHistoryCount = 0;
        float periodThen       = 0.0f;
    public:
        /** The analysis span, in bins, that the detector used per frequency on the
            last frame. Exposed so the interface can draw the window the DETAIL
            control is setting. Without a picture of it the knob has nothing
            visible to move, which is exactly how it came to be reported as dead.
        */
        const std::vector<int>& getRadialBins() const noexcept { return radialBins; }
    private:
        std::vector<double> prefix;
        std::vector<float>  prominenceDb;
        std::vector<float>  baselineDb;
        std::vector<float>  magDb;
        std::vector<float>  previousMagDb;
        std::vector<float>  magSmooth;
        std::vector<float>  shoulderScratch;

        int temporalFrames = 1;
        std::vector<float> prominenceHistory;
        int historyWriteIdx = 0;
        std::vector<float> temporalScratch;

        int hpsFrames = 5;
        std::vector<float> magHistory;
        int magWriteIdx = 0;
        std::vector<float> hpsScratch;
        std::vector<float> vScratch;
        std::vector<bool> percussiveFlags;

        float periodSmooth = 0.0f;
        float previousPeriod = 0.0f;
        bool historyValid = false;
        std::vector<int>    activeBins;
        std::vector<float>  peakScratch;
        std::vector<int>    peakPositions;

        float lastReferencePromDb = 0.0f;
    };
}
