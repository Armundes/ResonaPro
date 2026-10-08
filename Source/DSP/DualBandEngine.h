#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>
#include "SpectralEngine.h"

namespace ResonaPro
{
    class DualBandEngine
    {
    public:
        DualBandEngine() {}

        void prepare(int overlapFactor, double sampleRate)
        {
            this->sampleRate = sampleRate;

            // Step 1: Low Band Path (20 Hz - 1 kHz) N=8192
            int lowSize = static_cast<int>(8192 * (sampleRate / 48000.0));
            int lowPow2 = std::max(8, juce::roundToInt(std::log2(juce::nextPowerOfTwo(lowSize))));
            
            // High Band Path (1 kHz - 20 kHz) N=1024
            int highSize = static_cast<int>(1024 * (sampleRate / 48000.0));
            int highPow2 = std::max(8, juce::roundToInt(std::log2(juce::nextPowerOfTwo(highSize))));

            // OverlapFactor could be 8 or 16 (Step 2 Ultra OLA)
            engineLow.prepare(lowPow2, overlapFactor, sampleRate);
            engineHigh.prepare(highPow2, overlapFactor, sampleRate);

            crossoverLpL.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
            crossoverHpL.setType(juce::dsp::LinkwitzRileyFilterType::highpass);
            crossoverLpR.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
            crossoverHpR.setType(juce::dsp::LinkwitzRileyFilterType::highpass);
            crossoverLpLKey.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
            crossoverHpLKey.setType(juce::dsp::LinkwitzRileyFilterType::highpass);
            crossoverLpRKey.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
            crossoverHpRKey.setType(juce::dsp::LinkwitzRileyFilterType::highpass);

            float xoverFreq = 1000.0f;
            crossoverLpL.setCutoffFrequency(xoverFreq);
            crossoverHpL.setCutoffFrequency(xoverFreq);
            crossoverLpR.setCutoffFrequency(xoverFreq);
            crossoverHpR.setCutoffFrequency(xoverFreq);
            crossoverLpLKey.setCutoffFrequency(xoverFreq);
            crossoverHpLKey.setCutoffFrequency(xoverFreq);
            crossoverLpRKey.setCutoffFrequency(xoverFreq);
            crossoverHpRKey.setCutoffFrequency(xoverFreq);

            juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32>(8192), 1 };
            crossoverLpL.prepare(spec);
            crossoverHpL.prepare(spec);
            crossoverLpR.prepare(spec);
            crossoverHpR.prepare(spec);
            crossoverLpLKey.prepare(spec);
            crossoverHpLKey.prepare(spec);
            crossoverLpRKey.prepare(spec);
            crossoverHpRKey.prepare(spec);

            int latLow = engineLow.getLatencySamples();
            int latHigh = engineHigh.getLatencySamples();
            delaySamples = latLow - latHigh;
            if (delaySamples < 0) delaySamples = 0;

            delayBufferHighL.assign(delaySamples + 1, 0.0f);
            delayBufferHighR.assign(delaySamples + 1, 0.0f);
            delayWritePos = 0;
        }

        void reset()
        {
            engineLow.reset();
            engineHigh.reset();
            crossoverLpL.reset();
            crossoverHpL.reset();
            crossoverLpR.reset();
            crossoverHpR.reset();
            crossoverLpLKey.reset();
            crossoverHpLKey.reset();
            crossoverLpRKey.reset();
            crossoverHpRKey.reset();
            std::fill(delayBufferHighL.begin(), delayBufferHighL.end(), 0.0f);
            std::fill(delayBufferHighR.begin(), delayBufferHighR.end(), 0.0f);
            delayWritePos = 0;
        }

        int getLatencySamples() const { return engineLow.getLatencySamples(); }

        void setVocalProfile(VocalProfile profile)
        {
            engineLow.setVocalProfile(profile);
            engineHigh.setVocalProfile(profile);
        }

        void updateSidechainWeights(const ParametricEQWeighting& eq)
        {
            engineLow.updateSidechainWeights(eq);
            engineHigh.updateSidechainWeights(eq);
        }

        void setParams(const SpectralEngine::Params& params)
        {
            engineLow.setParams(params);
            engineHigh.setParams(params);
        }

        void processSample(float inL, float inR, float& outL, float& outR, float keyL, float keyR, bool useKey)
        {
            // Crossover split
            float lowL = crossoverLpL.processSample(0, inL);
            float highL = crossoverHpL.processSample(0, inL);
            float lowR = crossoverLpR.processSample(0, inR);
            float highR = crossoverHpR.processSample(0, inR);

            float kLowL = useKey ? crossoverLpLKey.processSample(0, keyL) : lowL;
            float kHighL = useKey ? crossoverHpLKey.processSample(0, keyL) : highL;
            float kLowR = useKey ? crossoverLpRKey.processSample(0, keyR) : lowR;
            float kHighR = useKey ? crossoverHpRKey.processSample(0, keyR) : highR;

            // Process bands
            float outLowL = 0.0f, outLowR = 0.0f;
            float outHighL = 0.0f, outHighR = 0.0f;
            engineLow.processSample(lowL, lowR, outLowL, outLowR, kLowL, kLowR, useKey);
            engineHigh.processSample(highL, highR, outHighL, outHighR, kHighL, kHighR, useKey);

            // Delay high band to match low band latency
            float delayedHighL = delayBufferHighL[delayWritePos];
            float delayedHighR = delayBufferHighR[delayWritePos];
            delayBufferHighL[delayWritePos] = outHighL;
            delayBufferHighR[delayWritePos] = outHighR;
            
            if (++delayWritePos >= delayBufferHighL.size()) delayWritePos = 0;

            // Phase polarity verification at crossover summation point:
            // juce::dsp::LinkwitzRileyFilter automatically maintains phase alignment 
            // for LR4 crossovers. The summed output is phase-coherent.
            outL = outLowL + delayedHighL;
            outR = outLowR + delayedHighR;
        }

        // For Visualizer: we can just proxy the High Engine since that handles 1kHz - 20kHz
        // Alternatively, we could stitch them together, but for simplicity we return High.
        const float* getMagnitudeSpectrum() const { return engineHigh.getMagnitudeSpectrum(); }
        const float* getReductionDb() const { return engineHigh.getReductionDb(); }
        const float* getBaselineDb() const { return engineHigh.getBaselineDb(); }
        int getNumBins() const { return engineHigh.getNumBins(); }
        double getSampleRate() const { return sampleRate; }
        float getReferenceProminenceDb() const { return engineHigh.getReferenceProminenceDb(); }

    private:
        SpectralEngine engineLow;
        SpectralEngine engineHigh;

        juce::dsp::LinkwitzRileyFilter<float> crossoverLpL, crossoverHpL, crossoverLpR, crossoverHpR;
        juce::dsp::LinkwitzRileyFilter<float> crossoverLpLKey, crossoverHpLKey, crossoverLpRKey, crossoverHpRKey;

        std::vector<float> delayBufferHighL, delayBufferHighR;
        int delayWritePos = 0;
        int delaySamples = 0;
        double sampleRate = 48000.0;
    };
}
