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
        float sibilanceSmooth   = 0.5f;  // 0.0 .. 1.0 (dedicated musical S/T consonant smoother)
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

        void prepare (int numBins, float sampleRate)
        {
            bins     = std::max (2, numBins);
            sr       = sampleRate > 0.0f ? sampleRate : 44100.0f;
            binWidth = (sr * 0.5f) / static_cast<float> (bins - 1);

            binFreq.resize (static_cast<size_t> (bins));
            profileOffsetDb.resize (static_cast<size_t> (bins));
            iso226Db.resize (static_cast<size_t> (bins));
            radialBins.resize (static_cast<size_t> (bins));
            prefix.resize (static_cast<size_t> (bins) + 1, 0.0);
            prominenceDb.resize (static_cast<size_t> (bins), 0.0f);
            baselineDb.resize (static_cast<size_t> (bins), -120.0f);
            magDb.resize (static_cast<size_t> (bins), -120.0f);
            previousMagDb.assign (static_cast<size_t> (bins), -240.0f);
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

        void setFullScaleReference (float fullScale) noexcept
        {
            fullScaleReference = fullScale > 0.0f ? fullScale : 1.0f;
        }

        void resetHistory() noexcept
        {
            periodSmooth = 0.0f;
            previousPeriod = 0.0f;
            historyValid = false;
            std::fill (previousMagDb.begin(), previousMagDb.end(), -240.0f);
            lastReferencePromDb = 0.0f;
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
                     const float* weightsDb,
                     float* out,
                     const DetectorParams& p)
        {
            if (magnitude == nullptr || out == nullptr || bins <= 0) return;

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

            // Fast local magnitude smoothing (radius 2 bins):
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
                const float effSharpness = std::clamp (p.sharpness * tiltMul, 0.20f, 5.0f);

                // Glasberg & Moore ERB bandwidth:
                const float erbHz = (24.7f * (4.37f * (f * 0.001f) + 1.0f)) * (1.0f / effSharpness);
                const int erbBins = std::clamp (static_cast<int> (erbHz / binWidth), 3, maxRadius);

                radialBins[static_cast<size_t> (k)] = std::clamp (std::max (erbBins, minRadiusBins), 3, maxRadius);
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
                appendSamples (shoulderScratch, std::max (0, lFrom), std::min (bins - 1, lTo));
                appendSamples (shoulderScratch, rFrom, std::min (bins - 1, rTo));

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

                baselineDb[static_cast<size_t> (k)] =
                    base > 1.0e-12 ? static_cast<float> (20.0 * std::log10 (base)) : -240.0f;

                const float mag = magnitude[k];
                float prom;
                if (mag <= 1.0e-12f || base <= 1.0e-12)
                    prom = 0.0f;
                else
                    prom = std::clamp (static_cast<float> (20.0 * std::log10 (mag / base)), -60.0f, 60.0f);

                prominenceDb[static_cast<size_t> (k)] = prom;
                maxProm = std::max (maxProm, prom);
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

            // ---- 6. Cue-Gated, Cue-Proportional Dynamic Resonance Processing -----
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
                const float rawCueWeight = (weightsDb != nullptr) ? weightsDb[static_cast<size_t> (k)] : 0.0f;
                const bool isSibilanceBand = (fHz >= 4000.0f && fHz <= 12000.0f);
                
                // The dedicated Sibilance smoother creates its own "virtual cue" in the high end.
                // This ensures sibilance reduction works even if standard EQ cues are flat.
                float effectiveCue = std::max(0.0f, rawCueWeight);
                if (isSibilanceBand && p.sibilanceSmooth > 0.01f)
                {
                    effectiveCue = std::max(effectiveCue, p.sibilanceSmooth * 6.0f); // Up to +6dB equivalent cue
                }

                // RULE 1: STRICT CUE GATING
                // If the user has not moved a cue up (and no sibilance smoothing is active),
                // resonance reduction is EXACTLY zero.
                if (effectiveCue <= 0.05f)
                {
                    out[k] = 0.0f;
                    continue;
                }

                // Frequency-localized nominal threshold:
                float nominalThresholdDb;
                if (fHz < 1000.0f)
                    nominalThresholdDb = 5.0f + p.selectivity * 3.5f;
                else if (fHz < 3000.0f)
                    nominalThresholdDb = 3.0f + p.selectivity * 2.5f;
                else if (fHz < 10000.0f)
                    nominalThresholdDb = 1.2f + p.selectivity * 1.5f;
                else
                    nominalThresholdDb = 0.8f + p.selectivity * 1.0f;

                float thr = nominalThresholdDb + hardOffsetDb;
                thr += profileOffsetDb[static_cast<size_t> (k)];
                if (p.useIso226)
                    thr += iso226Db[static_cast<size_t> (k)];

                if (isSibilanceBand && p.sibilanceSmooth > 0.05f)
                {
                    thr -= (4.0f * p.sibilanceSmooth);
                }

                // Lower the threshold slightly based on the cue to grab more resonance, 
                // but cap it so we don't start suppressing the raw noise floor.
                thr -= std::min(effectiveCue * 0.5f, 5.0f);

                float rawExcess = prominenceDb[static_cast<size_t> (k)] - thr;
                if (rawExcess <= 0.0f)
                {
                    out[k] = 0.0f;
                    continue;
                }

                // RULE 2: STRICT PROPORTIONAL SCALING
                // This is the magic. The amount of reduction scales DIRECTLY with the cue size.
                // A tiny 0.1 dB filter tail only produces a 0.016x multiplier (effectively 0).
                // A +6 dB cue gives a 1.0x multiplier. A +12 dB cue gives 2.0x.
                float cueScale = effectiveCue / 6.0f;

                // RULE 3: HIGH-FREQUENCY SENSITIVITY FIX
                // The user noted the high-end was not catching resonance. We boost the cue scale
                // massively in the upper register to ensure immediate, surgical bite.
                if (fHz >= 2500.0f)
                {
                    cueScale *= 2.5f;
                }

                cueScale = std::clamp(cueScale, 0.0f, 6.0f);
                float excess = rawExcess * cueScale;

                // Transient protection (lows/mids only)
                if (fHz < 3000.0f)
                {
                    float transientPenalty = std::clamp (p.transientActivity, 0.0f, 1.0f)
                                           * std::clamp (p.transientGuard, 0.0f, 1.0f) * 5.0f;
                    if (transientPenalty > 0.0f)
                    {
                        excess = std::max(0.0f, excess - transientPenalty);
                    }
                }

                if (p.hardMode && excess > 0.0f)
                {
                    const float levelDb = 20.0f * std::log10 (std::max (1.0e-9f, mag / refMag));
                    const float levelWeight = std::clamp ((levelDb + 50.0f) / 20.0f, 0.15f, 1.0f);
                    excess *= levelWeight;
                }

                // Note Motion protection (low registers only):
                if (p.motionProtect > 0.0f && historyValid && previousPeriod >= 3.0f
                    && periodSmooth >= 3.0f && fHz < 1500.0f)
                {
                    const float oldBin = k * previousPeriod / periodSmooth;
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
            historyValid = true;
            (void) maxProm;
        }

        const float* getBaselineDb()  const noexcept { return baselineDb.data(); }
        const float* getProminenceDb() const noexcept { return prominenceDb.data(); }
        float getReferenceProminenceDb() const noexcept { return lastReferencePromDb; }

    private:
        static void appendSamples (std::vector<float>& dest, int from, int to) noexcept
        {
            if (from > to) return;
            const int count = to - from + 1;
            if (count <= 16)
            {
                for (int i = from; i <= to; ++i)
                    dest.push_back (static_cast<float> (i));
                return;
            }
            constexpr int TargetSamples = 16;
            const float step = static_cast<float> (count - 1) / static_cast<float> (TargetSamples - 1);
            for (int s = 0; s < TargetSamples; ++s)
            {
                const int idx = from + static_cast<int> (std::lround (static_cast<float> (s) * step));
                dest.push_back (static_cast<float> (idx));
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
        std::vector<double> prefix;
        std::vector<float>  prominenceDb;
        std::vector<float>  baselineDb;
        std::vector<float>  magDb;
        std::vector<float>  previousMagDb;
        std::vector<float>  magSmooth;
        std::vector<float>  shoulderScratch;

        float periodSmooth = 0.0f;
        float previousPeriod = 0.0f;
        bool historyValid = false;
        std::vector<int>    activeBins;
        std::vector<float>  peakScratch;
        std::vector<int>    peakPositions;

        float lastReferencePromDb = 0.0f;
        float fullScaleReference = 1.0f;
    };
}
