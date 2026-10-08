#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <array>
#include <cmath>

namespace ResonaPro
{
    class TrackingEngine
    {
    public:
        static constexpr int numBands = 32;

        TrackingEngine()
        {
            for (int i = 0; i < numBands; ++i)
            {
                filtersL[i].setType(juce::dsp::StateVariableTPTFilterType::bandpass);
                filtersR[i].setType(juce::dsp::StateVariableTPTFilterType::bandpass);
                
                notchesL[i].setType(juce::dsp::StateVariableTPTFilterType::bandpass);
                notchesR[i].setType(juce::dsp::StateVariableTPTFilterType::bandpass);

                targetGainsL[i] = 1.0f;
                targetGainsR[i] = 1.0f;
                currentGainsL[i] = 1.0f;
                currentGainsR[i] = 1.0f;
            }
        }

        void prepare(double sampleRate)
        {
            this->sampleRate = sampleRate;

            juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32>(8192), 1 };
            
            float minFreq = 100.0f;
            float maxFreq = 10000.0f;

            for (int i = 0; i < numBands; ++i)
            {
                filtersL[i].prepare(spec);
                filtersR[i].prepare(spec);
                notchesL[i].prepare(spec);
                notchesR[i].prepare(spec);

                // Logarithmically space bands
                float freq = minFreq * std::pow(maxFreq / minFreq, static_cast<float>(i) / (numBands - 1));
                
                filtersL[i].setCutoffFrequency(freq);
                filtersR[i].setCutoffFrequency(freq);
                filtersL[i].setResonance(10.0f); // High Q for detection
                filtersR[i].setResonance(10.0f);

                notchesL[i].setCutoffFrequency(freq);
                notchesR[i].setCutoffFrequency(freq);
                notchesL[i].setResonance(5.0f); // Medium Q for cuts
                notchesR[i].setResonance(5.0f);

                envFollowersL[i].reset(sampleRate, 0.005, 0.050); // 5ms attack, 50ms release
                envFollowersR[i].reset(sampleRate, 0.005, 0.050);
                
                baselineEnvL[i].reset(sampleRate, 0.5, 2.0); // Slow follower for baseline
                baselineEnvR[i].reset(sampleRate, 0.5, 2.0);
            }

            coeffSmoothCoeff = static_cast<float>(std::exp(-1.0 / (0.010 * sampleRate))); // 10ms smoothing for coefficients
        }

        void reset()
        {
            for (int i = 0; i < numBands; ++i)
            {
                filtersL[i].reset();
                filtersR[i].reset();
                notchesL[i].reset();
                notchesR[i].reset();
                targetGainsL[i] = 1.0f;
                targetGainsR[i] = 1.0f;
                currentGainsL[i] = 1.0f;
                currentGainsR[i] = 1.0f;
            }
        }

        void processSample(float inL, float inR, float& outL, float& outR)
        {

            for (int i = 0; i < numBands; ++i)
            {
                // Detection
                float bpL = filtersL[i].processSample(0, inL);
                float bpR = filtersR[i].processSample(0, inR);

                float envL = envFollowersL[i].process(std::abs(bpL));
                float envR = envFollowersR[i].process(std::abs(bpR));

                float baseL = baselineEnvL[i].process(std::abs(bpL));
                float baseR = baselineEnvR[i].process(std::abs(bpR));

                // Gain calculation: if fast envelope exceeds slow baseline, we have a resonance
                float ratioL = (baseL > 1e-6f) ? envL / baseL : 1.0f;
                float ratioR = (baseR > 1e-6f) ? envR / baseR : 1.0f;

                float thresh = 2.0f; // 6dB above baseline
                if (ratioL > thresh)
                    targetGainsL[i] = std::max(0.1f, 1.0f - (ratioL - thresh) * 0.1f);
                else
                    targetGainsL[i] = 1.0f;

                if (ratioR > thresh)
                    targetGainsR[i] = std::max(0.1f, 1.0f - (ratioR - thresh) * 0.1f);
                else
                    targetGainsR[i] = 1.0f;

                // Coefficient Smoothing (One-pole lowpass)
                currentGainsL[i] = currentGainsL[i] * coeffSmoothCoeff + targetGainsL[i] * (1.0f - coeffSmoothCoeff);
                currentGainsR[i] = currentGainsR[i] * coeffSmoothCoeff + targetGainsR[i] * (1.0f - coeffSmoothCoeff);

                // Map linear gain back to SVF parameter (it expects amplitude gain for bell filters? Actually SVF doesn't have a direct "gain" parameter for peak filters? Wait! juce::dsp::StateVariableTPTFilter doesn't have bell gain in older juce, but since JUCE 6 it has processSample but it only takes cutoff and Q. Wait, it has no `setGain`? 
                // Ah! IProcessor for SVF is just LP/HP/BP/Notch. TPT SVF doesn't have a bell gain unless it's IIRFilter.
                // We should use juce::dsp::IIR::Filter instead if we want dynamic peak gains, or implement our own SVF.
                // The instructions say: "Instantiate a multi-band dynamic equalizer utilizing juce::dsp::StateVariableTPTFilter... negatively modulate its gain"
                // StateVariableTPTFilter DOES NOT have a gain parameter natively! Wait, "StateVariableTPTFilter" has bandpass and notch. We can parallel mix the bandpass out!
                // out = in + (gain - 1.0) * bandpass.
                // If gain = 1, out = in. If gain = 0, out = in - bandpass = notch! This works perfectly for TPT SVF.
            }

            // Parallel mix for zero latency bell/notch EQ
            float accumL = inL;
            float accumR = inR;

            for (int i = 0; i < numBands; ++i)
            {
                // We run the notch filter path (which is just SVF bandpass)
                // Actually to make a bell filter from an SVF:
                // Bell = in + (gain - 1.0) * BP_normalized.
                // The SVF BP is normalized if it's the bandpass output.
                float bpOutL = notchesL[i].processSample(0, accumL);
                float bpOutR = notchesR[i].processSample(0, accumR);

                accumL += (currentGainsL[i] - 1.0f) * bpOutL;
                accumR += (currentGainsR[i] - 1.0f) * bpOutR;
            }

            outL = accumL;
            outR = accumR;
        }

    private:
        class EnvelopeFollower {
        public:
            void reset(double sr, double attackSec, double releaseSec) {
                attackCoeff = static_cast<float>(std::exp(-1.0 / (attackSec * sr)));
                releaseCoeff = static_cast<float>(std::exp(-1.0 / (releaseSec * sr)));
                envelope = 0.0f;
            }
            float process(float in) {
                if (in > envelope)
                    envelope = attackCoeff * envelope + (1.0f - attackCoeff) * in;
                else
                    envelope = releaseCoeff * envelope + (1.0f - releaseCoeff) * in;
                return envelope;
            }
        private:
            float attackCoeff = 0.0f;
            float releaseCoeff = 0.0f;
            float envelope = 0.0f;
        };

        juce::dsp::StateVariableTPTFilter<float> filtersL[numBands];
        juce::dsp::StateVariableTPTFilter<float> filtersR[numBands];
        
        juce::dsp::StateVariableTPTFilter<float> notchesL[numBands];
        juce::dsp::StateVariableTPTFilter<float> notchesR[numBands];

        EnvelopeFollower envFollowersL[numBands], envFollowersR[numBands];
        EnvelopeFollower baselineEnvL[numBands], baselineEnvR[numBands];

        float targetGainsL[numBands], targetGainsR[numBands];
        float currentGainsL[numBands], currentGainsR[numBands];

        float coeffSmoothCoeff = 0.9f;
        double sampleRate = 48000.0;
    };
}
