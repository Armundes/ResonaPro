#pragma once

#include <cmath>
#include <algorithm>

namespace ResonaPro
{
    /** Fast/slow envelope transient detector.

        Replaces the previous "transient splitter". Splitting the audio into a
        transient and a sustain stream and recombining them after a spectral pass
        is extremely fragile: any mismatch between the two paths (delay, window
        latency, gain) destroys the signal, and the recombination itself is
        non-linear. It is also unnecessary — protecting attacks is a *detection*
        problem, not an audio-routing problem.

        Instead this class reports how percussive the current sample is, and the
        resonance detector raises its threshold accordingly. The audio path stays
        a single, provably transparent spectral pass.
    */
    class TransientDetector
    {
    public:
        TransientDetector() = default;

        void prepare (double sampleRate)
        {
            sr = static_cast<float> (sampleRate > 0.0 ? sampleRate : 44100.0);

            // Fast follower: 1.5 ms attack / 10 ms release
            fastAttackCoeff  = 1.0f - std::exp (-1.0f / (0.0015f * sr));
            fastReleaseCoeff = 1.0f - std::exp (-1.0f / (0.0100f * sr));

            // Slow follower: 25 ms attack / 80 ms release
            slowAttackCoeff  = 1.0f - std::exp (-1.0f / (0.0250f * sr));
            slowReleaseCoeff = 1.0f - std::exp (-1.0f / (0.0800f * sr));

            reset();
        }

        void reset()
        {
            fastEnv[0] = fastEnv[1] = 0.0f;
            slowEnv[0] = slowEnv[1] = 0.0f;
            activity = 0.0f;
        }

        /** Feeds one stereo sample pair; updates the instantaneous activity. */
        inline void pushSample (float inL, float inR) noexcept
        {
            const float a = std::abs (inL);
            const float b = std::abs (inR);

            updateChannel (0, a);
            updateChannel (1, b);

            const float diffA = std::max (0.0f, fastEnv[0] - slowEnv[0]);
            const float diffB = std::max (0.0f, fastEnv[1] - slowEnv[1]);

            const float coeffA = std::clamp ((diffA / (fastEnv[0] + 1.0e-6f)) * 1.8f, 0.0f, 1.0f);
            const float coeffB = std::clamp ((diffB / (fastEnv[1] + 1.0e-6f)) * 1.8f, 0.0f, 1.0f);

            activity = std::max (coeffA, coeffB);
        }

        inline float getActivity() const noexcept { return activity; }

    private:
        inline void updateChannel (int ch, float x) noexcept
        {
            fastEnv[ch] = (x > fastEnv[ch]) ? fastEnv[ch] + fastAttackCoeff  * (x - fastEnv[ch])
                                            : fastEnv[ch] + fastReleaseCoeff * (x - fastEnv[ch]);
            slowEnv[ch] = (x > slowEnv[ch]) ? slowEnv[ch] + slowAttackCoeff  * (x - slowEnv[ch])
                                            : slowEnv[ch] + slowReleaseCoeff * (x - slowEnv[ch]);
        }

        float sr = 44100.0f;
        float fastAttackCoeff = 0.0f, fastReleaseCoeff = 0.0f;
        float slowAttackCoeff = 0.0f, slowReleaseCoeff = 0.0f;
        float fastEnv[2] { 0.0f, 0.0f };
        float slowEnv[2] { 0.0f, 0.0f };
        float activity = 0.0f;
    };
}