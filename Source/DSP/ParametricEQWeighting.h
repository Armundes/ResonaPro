#pragma once

#include <vector>
#include <cmath>
#include <algorithm>

namespace ResonaPro
{
    enum class FilterType
    {
        Bell = 0,
        LowCut,     // High Pass: suppresses resonances above cutoff
        HighCut,    // Low Pass: suppresses resonances below cutoff
        LowShelf,
        HighShelf,
        BandPass
    };

    inline const char* getFilterTypeName (FilterType t) noexcept
    {
        switch (t)
        {
            case FilterType::Bell:      return "Bell";
            case FilterType::LowCut:    return "Low Cut (High Pass)";
            case FilterType::HighCut:   return "High Cut (Low Pass)";
            case FilterType::LowShelf:  return "Low Shelf";
            case FilterType::HighShelf: return "High Shelf";
            case FilterType::BandPass:  return "Band Pass";
        }
        return "Bell";
    }

    /** One 'focus band' node.

        The gain is NOT applied to the audio; it biases how sensitive the
        resonance detector is in that region. When gainDb = 0.0 dB (neutral),
        no reduction occurs. Raising a cue tells the detector to focus
        suppression specifically inside this curve.
    */
    struct FilterBand
    {
        bool       enabled   = true;
        FilterType type      = FilterType::Bell;
        float      frequency = 1000.0f;   // Hz
        float      gainDb    = 0.0f;      // -24 .. +24 dB of sensitivity
        float      q         = 1.0f;      // 0.1 .. 8.0

        /** Returns the sensitivity offset in dB at the given frequency.
            Positive = more sensitive (more reduction), 0 = neutral (no reduction).
        */
        float getWeightDbAt (float targetFreq) const noexcept
        {
            if (! enabled || std::abs (gainDb) < 0.05f)
                return 0.0f;

            const float f       = std::max (10.0f, targetFreq);
            const float centre  = std::max (10.0f, frequency);
            const float octDiff = std::log2 (f / centre);
            const float qVal    = std::clamp (this->q, 0.1f, 8.0f);

            switch (type)
            {
                case FilterType::Bell:
                {
                    // -3 dB bandwidth of an RBJ peaking filter, in octaves.
                    const float bwOct = std::max (0.15f, (2.0f / 0.6931472f) * std::asinh (1.0f / (2.0f * qVal)));
                    const float norm  = octDiff / (0.5f * bwOct);
                    return gainDb / (1.0f + norm * norm);
                }

                case FilterType::LowCut: // High Pass: suppresses resonances above cutoff frequency
                {
                    const float slope = std::clamp (qVal * 3.0f, 1.5f, 12.0f);
                    return gainDb / (1.0f + std::pow (std::max (0.0f, -octDiff) * 2.0f, slope));
                }

                case FilterType::HighCut: // Low Pass: suppresses resonances below cutoff frequency
                {
                    const float slope = std::clamp (qVal * 3.0f, 1.5f, 12.0f);
                    return gainDb / (1.0f + std::pow (std::max (0.0f, octDiff) * 2.0f, slope));
                }

                case FilterType::LowShelf:
                {
                    const float slope = std::clamp (qVal * 2.0f, 1.0f, 8.0f);
                    return gainDb / (1.0f + std::pow (std::max (0.0f, octDiff) * 2.0f, slope));
                }

                case FilterType::HighShelf:
                {
                    const float slope = std::clamp (qVal * 2.0f, 1.0f, 8.0f);
                    return gainDb / (1.0f + std::pow (std::max (0.0f, -octDiff) * 2.0f, slope));
                }

                case FilterType::BandPass:
                {
                    const float bwOct = std::max (0.15f, (2.0f / 0.6931472f) * std::asinh (1.0f / (2.0f * qVal)));
                    const float norm  = octDiff / (0.5f * bwOct);
                    const float slope = std::clamp (qVal * 2.0f, 2.0f, 8.0f);
                    return gainDb / (1.0f + std::pow (std::abs (norm), slope));
                }
            }
            return 0.0f;
        }
    };

    /** Eight focus bands that shape the detector's sensitivity across frequency. */
    class ParametricEQWeighting
    {
    public:
        static constexpr int NumBands = 8;

        ParametricEQWeighting()
        {
            // All bands start at 0.0 dB => completely neutral weighting (zero reduction everywhere).
            bands[0] = { true, FilterType::LowCut,     80.0f,   0.0f, 0.707f };
            bands[1] = { true, FilterType::Bell,      250.0f,   0.0f, 1.0f   };
            bands[2] = { true, FilterType::Bell,      500.0f,   0.0f, 1.2f   };
            bands[3] = { true, FilterType::Bell,     1200.0f,   0.0f, 1.2f   };
            bands[4] = { true, FilterType::Bell,     3200.0f,   0.0f, 1.4f   };
            bands[5] = { true, FilterType::Bell,     6000.0f,   0.0f, 1.5f   };
            bands[6] = { true, FilterType::Bell,     9000.0f,   0.0f, 1.6f   };
            bands[7] = { true, FilterType::HighCut,  16000.0f,   0.0f, 0.707f };
        }

        void setBand (int index, FilterType type, float freq, float gainDb, float q, bool enabled = true)
        {
            if (index >= 0 && index < NumBands)
                bands[index] = { enabled, type, freq, gainDb, q };
        }

        FilterBand& getBand (int index) noexcept { return bands[std::clamp (index, 0, NumBands - 1)]; }
        const FilterBand& getBand (int index) const noexcept { return bands[std::clamp (index, 0, NumBands - 1)]; }

        /** Sums all enabled bands into a per-bin sensitivity offset in dB. */
        void computeWeightingDb (float* weightsDb, int numBins, float sampleRate) const noexcept
        {
            if (weightsDb == nullptr || numBins <= 0) return;

            const float binWidth = (sampleRate * 0.5f) / static_cast<float> (numBins - 1);
            for (int k = 0; k < numBins; ++k)
            {
                const float freq = std::max (20.0f, static_cast<float> (k) * binWidth);
                float sum = 0.0f;
                for (int b = 0; b < NumBands; ++b)
                    sum += bands[b].getWeightDbAt (freq);

                weightsDb[k] = std::clamp (sum, -36.0f, 36.0f);
            }
        }

    private:
        FilterBand bands[NumBands];
    };
}
