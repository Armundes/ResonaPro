#pragma once
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>

namespace ResonaPro
{
    /** Summarises a played vocal without touching the audio thread.
        Call addFrame from the editor timer while the user plays a phrase. A peak
        needs to recur in several frames; transient and random noise spikes will
        not survive that test. The result is a suggestion for the eight *focus*
        bands, not a static EQ and not a silent change to the project. The user
        must press Apply to accept it.
    */
    class LearnAnalyzer
    {
    public:
        static constexpr size_t Points = 288;
        struct Candidate { float frequency, sensitivityDb, persistence; };

        void reset() noexcept
        {
            sum.fill (0.0f);
            hits.fill (0);
            frames = 0;
        }
        int frameCount() const noexcept { return frames; }
        static float frequencyAt (size_t i) noexcept
        {
            return 20.0f * std::pow (1000.0f, static_cast<float> (i) / static_cast<float> (Points - 1));
        }
        void addFrame (const std::array<float, Points>& magnitudeDb,
                       const std::array<float, Points>& baselineDb, float referenceDb) noexcept
        {
            if (++frames > 36000) { --frames; return; }  // max ~30 minutes at 20 Hz
            // A local spectral prominence must exceed the adaptive reference.
            // Ignore silent frames and only count local maxima.
            for (size_t i = 3; i + 3 < Points; ++i)
            {
                const float hz = frequencyAt (i);
                if (hz < 150.0f || hz > 12000.0f || magnitudeDb[i] < -75.0f)
                    continue;
                const float excess = magnitudeDb[i] - baselineDb[i] - referenceDb - 2.0f;
                if (excess <= 1.5f) continue;
                bool peak = true;
                for (int d = -3; d <= 3; ++d)
                    if (d != 0 && magnitudeDb[static_cast<size_t> (static_cast<int> (i) + d)] > magnitudeDb[i])
                        peak = false;
                if (! peak) continue;
                hits[i]++;
                sum[i] += std::min (16.0f, excess);
            }
        }
        std::vector<Candidate> suggestions() const
        {
            std::vector<Candidate> pool, selected;
            if (frames < 20) return selected;  // at least one second of evidence
            for (size_t i = 3; i + 3 < Points; ++i)
            {
                const float persistence = static_cast<float> (hits[i]) / static_cast<float> (frames);
                if (persistence < 0.10f) continue;
                const float score = sum[i] / static_cast<float> (frames);
                if (score < 0.45f) continue;
                pool.push_back ({ frequencyAt (i), std::clamp (score * 2.0f, 1.0f, 6.0f), persistence });
            }
            std::sort (pool.begin(), pool.end(), [] (const Candidate& a, const Candidate& b)
            { return a.sensitivityDb * a.persistence > b.sensitivityDb * b.persistence; });
            for (const auto& c : pool)
            {
                bool tooClose = false;
                for (const auto& existing : selected)
                    if (std::abs (std::log2 (c.frequency / existing.frequency)) < 0.22f)
                        tooClose = true;
                if (! tooClose) selected.push_back (c);
                if (selected.size() == 8) break;
            }
            std::sort (selected.begin(), selected.end(), [] (const Candidate& a, const Candidate& b)
            { return a.frequency < b.frequency; });
            return selected;
        }
    private:
        std::array<float, Points> sum {};
        std::array<int, Points> hits {};
        int frames = 0;
    };
}
