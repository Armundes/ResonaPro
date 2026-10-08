#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#if defined(__APPLE__)
 #include <Accelerate/Accelerate.h>
#endif

namespace ResonaPro
{
    enum class ProcessingMode
    {
        Soft = 0,
        Hard
    };

    /** Dynamic suppressor with frequency-dependent binomial smoothing,
        asymmetric attack/release ballistics, and attack/release tilt controls.
    */
    class DynamicSuppressor
    {
    public:
        DynamicSuppressor() = default;

        struct Params
        {
            float depth           = 1.0f;   // 0 .. 4
            float attackMs        = 8.0f;
            float releaseMs       = 70.0f;
            float attackTilt      = 0.0f;   // -1 .. +1 (positive = fast highs, slow lows)
            float releaseTilt     = 0.0f;   // -1 .. +1 (positive = fast high recovery, slow low hold)
            float maxReductionDb  = 24.0f;  // ceiling of the applied reduction
            float cutWidth        = 1.0f;   // 0 = tightest cut, 1 = the full spread
            ProcessingMode mode   = ProcessingMode::Soft;
        };

        void prepare (int numBins, float sampleRate, int hopSize, const std::vector<float>& binFrequencies)
        {
            bins = std::max (2, numBins);
            sr   = sampleRate > 0.0f ? sampleRate : 44100.0f;
            hop  = std::max (1, hopSize);
            hopSeconds = static_cast<float> (hop) / sr;
            binFreqs = binFrequencies;

            gainEnvelope.assign        (static_cast<size_t> (bins), 1.0f);
            targetGain.assign          (static_cast<size_t> (bins), 1.0f);
            smoothedTarget.assign      (static_cast<size_t> (bins), 1.0f);
            rawReductionDb.assign      (static_cast<size_t> (bins), 0.0f);
            smoothedReductionDb.assign (static_cast<size_t> (bins), 0.0f);
            relTiltMulPerBin.assign     (static_cast<size_t> (bins), 1.0f);
            attackCoeff.assign         (static_cast<size_t> (bins), 0.0f);
            releaseCoeff.assign        (static_cast<size_t> (bins), 0.0f);
            freqScale.assign           (static_cast<size_t> (bins), 1.0f);
            kernelRadius.assign        (static_cast<size_t> (bins), 2);
            reductionAge.assign        (static_cast<size_t> (bins), 0);

            for (int k = 0; k < bins; ++k)
            {
                const float f = (k < static_cast<int> (binFreqs.size()))
                                  ? binFreqs[static_cast<size_t> (k)] : 1000.0f;

                // Frequency timing scaling:
                freqScale[static_cast<size_t> (k)] =
                    std::clamp (std::pow (std::max (20.0f, f) / 1000.0f, -0.30f), 0.50f, 2.2f);

                // Frequency-adaptive kernel radius:
                // < 5.5 kHz -> 5-tap (radius 2)
                // >= 5.5 kHz -> 3-tap (radius 1)
                // Using 3-tap in the highs prevents smearing individual resonance notches into a broad high-shelf dip,
                // keeping the vocal open, bright, and silky without any muffling.
                if (f < 5500.0f)
                    kernelRadius[static_cast<size_t> (k)] = 2;
                else
                    kernelRadius[static_cast<size_t> (k)] = 1;
            }

            cachedAttack      = -1.0f;
            cachedRelease     = -1.0f;
            cachedAttackTilt  = -999.0f;
            cachedReleaseTilt = -999.0f;
            updateTiming (8.0f, 70.0f, 0.0f, 0.0f);
        }

        /** Recomputes the envelope coefficients. Allocation free, safe for audio thread. */
        void updateTiming (float attackMs, float releaseMs, float attackTilt = 0.0f, float releaseTilt = 0.0f)
        {
            if (std::abs (attackMs - cachedAttack) < 0.01f
                && std::abs (releaseMs - cachedRelease) < 0.01f
                && std::abs (attackTilt - cachedAttackTilt) < 0.01f
                && std::abs (releaseTilt - cachedReleaseTilt) < 0.01f)
                return;

            cachedAttack      = attackMs;
            cachedRelease     = releaseMs;
            cachedAttackTilt  = attackTilt;
            cachedReleaseTilt = releaseTilt;

            const float minAttackSec  = std::max (0.0003f, 0.15f * hopSeconds);
            const float minReleaseSec = std::max (0.0030f, 0.60f * hopSeconds);

            const float atkTilt = std::clamp (attackTilt, -1.0f, 1.0f);
            const float relTilt = std::clamp (releaseTilt, -1.0f, 1.0f);

            for (int k = 0; k < bins; ++k)
            {
                const float f = (k < static_cast<int> (binFreqs.size()))
                                  ? binFreqs[static_cast<size_t> (k)] : 1000.0f;
                const float ratio = std::clamp (std::max (20.0f, f) / 1000.0f, 0.02f, 20.0f);

                const float baseScale = freqScale[static_cast<size_t> (k)];

                // Attack tilt: positive = faster highs (<1ms on sibilance), slower lows; negative = faster lows
                const float atkTiltMul = std::pow (ratio, -0.75f * atkTilt);
                const float effAttackSec  = std::max (minAttackSec,  0.001f * attackMs  * baseScale * atkTiltMul);

                // Release tilt: positive = fast high recovery (preserves air), longer low hold; negative = fast low recovery
                const float relTiltMul = std::pow (ratio, -0.65f * relTilt);
                const float effReleaseSec = std::max (minReleaseSec, 0.001f * releaseMs * baseScale * relTiltMul);

                relTiltMulPerBin[static_cast<size_t> (k)] = relTiltMul;
                attackCoeff[static_cast<size_t> (k)]  = std::exp (-hopSeconds / effAttackSec);
                releaseCoeff[static_cast<size_t> (k)] = std::exp (-hopSeconds / effReleaseSec);
            }
        }

        void reset()
        {
            std::fill (gainEnvelope.begin(),        gainEnvelope.end(),        1.0f);
            std::fill (targetGain.begin(),          targetGain.end(),          1.0f);
            std::fill (smoothedTarget.begin(),      smoothedTarget.end(),      1.0f);
            std::fill (rawReductionDb.begin(),      rawReductionDb.end(),      0.0f);
            std::fill (smoothedReductionDb.begin(), smoothedReductionDb.end(), 0.0f);
            std::fill (reductionAge.begin(),        reductionAge.end(),        0);
        }

        /** resonanceDb : per-bin excess prominence in dB (from ResonanceDetector)
            gainOut     : per-bin linear gain to apply to the complex spectrum
        */
        void process (const float* resonanceDb, float* gainOut, const Params& p)
        {
            if (gainOut == nullptr) return;

            updateTiming (p.attackMs, p.releaseMs, p.attackTilt, p.releaseTilt);

            const float depth  = std::clamp (p.depth, 0.0f, 4.0f);
            const float maxRed = std::clamp (p.maxReductionDb, 0.5f, 48.0f);
            const bool  active = depth > 0.001f && resonanceDb != nullptr;

            if (! active)
            {
                std::fill (gainEnvelope.begin(), gainEnvelope.end(), 1.0f);
                std::fill (smoothedTarget.begin(), smoothedTarget.end(), 1.0f);
                std::fill (gainOut, gainOut + bins, 1.0f);
                return;
            }

            // 1. Raw target reduction in dB from resonance excess
            for (int k = 0; k < bins; ++k)
            {
                const float excess = resonanceDb[k];
                float reductionDb = 0.0f;

                if (excess > 0.0f)
                {
                    if (p.mode == ProcessingMode::Soft)
                    {
                        reductionDb = maxRed * (1.0f - std::exp (-1.0f * depth * excess / maxRed));
                    }
                    else
                    {
                        reductionDb = std::min (maxRed, 1.6f * depth * excess);
                    }
                }

                rawReductionDb[static_cast<size_t> (k)] = reductionDb;
            }

            // 2. Frequency-adaptive binomial smoothing in the dB domain.
            //    Smooths the dB reduction curve using ring-free binomial kernels.
            //    Using 3-tap (radius 1) in the highs and 5-tap (radius 2) in the mids/lows
            //    eliminates single-bin modulation grain while keeping notches surgical and
            //    preventing high-frequency muffling or dullness.
            for (int k = 0; k < bins; ++k)
            {
                const int r = kernelRadius[static_cast<size_t> (k)];
                float red;

                if (r == 1)
                {
                    // 3-tap binomial (1 2 1) / 4
                    const float m1 = (k >= 1) ? rawReductionDb[static_cast<size_t> (k - 1)] : rawReductionDb[0];
                    const float c0 = rawReductionDb[static_cast<size_t> (k)];
                    const float p1 = (k + 1 < bins) ? rawReductionDb[static_cast<size_t> (k + 1)] : rawReductionDb[static_cast<size_t> (bins - 1)];

                    red = (m1 + 2.0f * c0 + p1) * 0.25f;
                }
                else if (r == 2)
                {
                    // 5-tap binomial (1 4 6 4 1) / 16
                    const float m2 = (k >= 2) ? rawReductionDb[static_cast<size_t> (k - 2)] : rawReductionDb[0];
                    const float m1 = (k >= 1) ? rawReductionDb[static_cast<size_t> (k - 1)] : rawReductionDb[0];
                    const float c0 = rawReductionDb[static_cast<size_t> (k)];
                    const float p1 = (k + 1 < bins) ? rawReductionDb[static_cast<size_t> (k + 1)] : rawReductionDb[static_cast<size_t> (bins - 1)];
                    const float p2 = (k + 2 < bins) ? rawReductionDb[static_cast<size_t> (k + 2)] : rawReductionDb[static_cast<size_t> (bins - 1)];

                    red = (m2 + 4.0f * m1 + 6.0f * c0 + 4.0f * p1 + p2) * (1.0f / 16.0f);
                }
                else
                {
                    // 7-tap binomial (1 6 15 20 15 6 1) / 64
                    const float m3 = (k >= 3) ? rawReductionDb[static_cast<size_t> (k - 3)] : rawReductionDb[0];
                    const float m2 = (k >= 2) ? rawReductionDb[static_cast<size_t> (k - 2)] : rawReductionDb[0];
                    const float m1 = (k >= 1) ? rawReductionDb[static_cast<size_t> (k - 1)] : rawReductionDb[0];
                    const float c0 = rawReductionDb[static_cast<size_t> (k)];
                    const float p1 = (k + 1 < bins) ? rawReductionDb[static_cast<size_t> (k + 1)] : rawReductionDb[static_cast<size_t> (bins - 1)];
                    const float p2 = (k + 2 < bins) ? rawReductionDb[static_cast<size_t> (k + 2)] : rawReductionDb[static_cast<size_t> (bins - 1)];
                    const float p3 = (k + 3 < bins) ? rawReductionDb[static_cast<size_t> (k + 3)] : rawReductionDb[static_cast<size_t> (bins - 1)];

                    // The spread across frequency is what sets how wide a cut is.
                    // This used to be a fixed seven-point kernel, so every cut had
                    // the same width whatever the user asked for. Blending toward
                    // the centre bin lets the control tighten the cut.
                    const float broad = (m3 + 6.0f * m2 + 15.0f * m1 + 20.0f * c0
                                         + 15.0f * p1 + 6.0f * p2 + p3) * (1.0f / 64.0f);
                    const float w = std::clamp (p.cutWidth, 0.0f, 1.0f);
                    red = c0 + (broad - c0) * w;
                }

                targetGain[static_cast<size_t> (k)] = std::pow (10.0f, -red / 20.0f);
            }

            // 3. Dual-Stage Program-Dependent Ballistics with Instant Air Recovery:
            //    - Fast transient syllables / consonants (1-3 hops) release 3.5x faster so trailing
            //      breath and open vocal air are never choked by lingering attenuation.
            //    - Sustained resonant rings release smoothly over the user-selected releaseMs.
            //    - Quick-snap floor: within 0.75 dB of unity, gain snaps cleanly to 1.0 to eliminate
            //      the long dragging 'blanket' tail.
            for (int k = 0; k < bins; ++k)
            {
                const float raw     = targetGain[static_cast<size_t> (k)];
                const float current = gainEnvelope[static_cast<size_t> (k)];

                float effCoeff;
                if (raw < current)
                {
                    // Attack phase: track consecutive reduction duration
                    if (reductionAge[static_cast<size_t> (k)] < 32)
                        reductionAge[static_cast<size_t> (k)] += 1;
                    effCoeff = attackCoeff[static_cast<size_t> (k)];
                }
                else
                {
                    // Release phase: program-dependent auto-release
                    const int age = reductionAge[static_cast<size_t> (k)];
                    if (age < 4)
                    {
                        // Short transient event: fast micro-release for instant air recovery
                        // This path runs whenever the reduction has been steady,
                        // which is the normal case, and it ignored the release
                        // tilt entirely. Release Tilt therefore only ever acted
                        // on the release that follows a fresh attack, and read as
                        // dead everywhere else.
                        const float scale = freqScale[static_cast<size_t> (k)];
                        const float tiltMul = relTiltMulPerBin[static_cast<size_t> (k)];
                        const float fastRelSec = std::max (0.0030f, 0.001f * (cachedRelease / 3.5f)
                                                                 * scale * tiltMul);
                        effCoeff = std::exp (-hopSeconds / fastRelSec);
                    }
                    else
                    {
                        effCoeff = releaseCoeff[static_cast<size_t> (k)];
                    }

                    if (reductionAge[static_cast<size_t> (k)] > 0)
                        reductionAge[static_cast<size_t> (k)] -= 1;
                }

                float next = effCoeff * current + (1.0f - effCoeff) * raw;

                // Quick-snap release floor: eliminates lingering tail within 0.75 dB of unity
                if (raw >= 0.98f && next > 0.92f)
                {
                    next = std::min (1.0f, next + (1.0f - next) * 0.45f);
                    if (next > 0.995f) next = 1.0f;
                }

                // Dynamic slew-rate limiter per hop: max ~6 dB reduction drop per hop
                constexpr float maxStepDownRatio = 0.50f; // ~ -6 dB per hop max
                if (next < current * maxStepDownRatio)
                    next = current * maxStepDownRatio;

                if (! std::isfinite (next))
                    next = 1.0f;

                gainEnvelope[static_cast<size_t> (k)] = next;
                gainOut[k] = next;
            }
        }

        const float* getGainEnvelope() const noexcept { return gainEnvelope.data(); }

    private:
        int   bins = 1025;
        float sr   = 44100.0f;
        int   hop  = 512;
        float hopSeconds   = 0.0116f;
        float cachedAttack      = -1.0f;
        float cachedRelease     = -1.0f;
        float cachedAttackTilt  = -999.0f;
        float cachedReleaseTilt = -999.0f;

        std::vector<float> binFreqs;
        std::vector<float> gainEnvelope;
        std::vector<float> targetGain;
        std::vector<float> smoothedTarget;
        std::vector<float> rawReductionDb;
        std::vector<float> smoothedReductionDb;
        std::vector<float> relTiltMulPerBin;
        std::vector<float> attackCoeff;
        std::vector<float> releaseCoeff;
        std::vector<float> freqScale;
        std::vector<int>   kernelRadius;
        std::vector<int>   reductionAge;
    };
}
