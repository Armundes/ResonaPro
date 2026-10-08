#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "DSP/SpectralEngine.h"
#include "DSP/TrackingEngine.h"
#include "DSP/ParametricEQWeighting.h"

namespace ResonaPro
{
    class ResonaProAudioProcessor : public juce::AudioProcessor,
                                    public juce::AudioProcessorValueTreeState::Listener,
                                    private juce::AsyncUpdater
    {
    public:
        ResonaProAudioProcessor();
        ~ResonaProAudioProcessor() override;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override { return true; }

        const juce::String getName() const override { return "ResonaPro"; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        bool isMidiEffect() const override { return false; }
        double getTailLengthSeconds() const override;

        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock& destData) override;
        void setStateInformation (const void* data, int sizeInBytes) override;

        /** Phase 8: profile the take's sibilance. Call from the message thread;
            the audio thread only accumulates. Nothing is applied until the user
            accepts the result.
        */
        /** The fence around LEARN. Returns true only for the parameters a take
            analysis is allowed to propose. Everything else -- resolution, hard
            mode, mid/side, delta, the vocal weighting, output, stereo link, mix
            -- is a session decision and is refused. Tested in Tests/.
        */
        static bool isLearnEditable (const juce::String& paramId);
        static const juce::StringArray& learnEditableIds();

        void startSibilanceLearn() noexcept;
        SibilanceDeEsser::Profile finishSibilanceLearn() noexcept;
        // Onset and decay of the problems in the take, measured by the engine at
        // the transform rate. Valid after finishSibilanceLearn.
        float getEngineLearnOnsetMs() const noexcept;
        float getEngineLearnRingMs()   const noexcept;

        juce::AudioProcessorValueTreeState apvts;

        //==============================================================================
        /** Number of log-frequency display points handed to the editor. */
        static constexpr int ScopeSize = 288;

        /** Fills the three display curves: input magnitude (dB), applied gain
            reduction (dB, negative) and the detector's baseline (dB).
            All three are resampled onto a log frequency axis from 20 Hz to 20 kHz
            so that what you see lines up with the frequency ruler.
        */
        void getVisualizerData (std::array<float, ScopeSize>& magnitudeDbOut,
                                std::array<float, ScopeSize>& reductionDbOut,
                                std::array<float, ScopeSize>& baselineDbOut,
                                std::array<float, ScopeSize>& windowHzOut,
                                float& referenceProminenceDbOut,
                                uint64_t* frameSequenceOut = nullptr);

        ParametricEQWeighting& getSidechainEQ() noexcept { return sidechainEQ; }

        /** Latency the host should compensate, in samples at the base rate. */
        int getEngineLatencySamples() const noexcept;

        //==============================================================================
        /** Factory presets (vocal oriented). */
        static juce::StringArray getPresetNames();
        static int getNumPresets();
        void loadPreset (int presetIndex);

        /** Applies one parameter value without touching anything else. */
        void setParameterValue (const juce::String& paramID, float value);

    private:
        void parameterChanged (const juce::String& parameterID, float newValue) override;
        void handleAsyncUpdate() override;
        void cacheParameterPointers();
        void updateLatencyAndDisplay();

        juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

        static constexpr int NumQualities = 4;   // 1024 / 2048 / 4096 / 8192
        static constexpr int NumResponses = 3;   // 2x / 4x / 8x overlap

        void buildEngines (double sampleRate);

        ParametricEQWeighting sidechainEQ;

        /** One fully prepared engine per (quality, response) combination, so that
            changing either control on the fly never allocates on the audio thread.

            One SpectralEngine per combination, not a dual-band wrapper. Analysis
            and synthesis happen in a single WOLA path: a second synthesis path
            behind a crossover cannot sum back to the input, so depth 0 stopped
            being bit-transparent and the reported latency became the longest
            path's rather than the selected transform's. Multi-resolution low-band
            *analysis* still happens, inside the engine, where it costs detection
            quality but no latency.
        */
        std::array<std::array<std::array<std::unique_ptr<SpectralEngine>, 3>, NumResponses>, NumQualities> engines;
        std::array<std::array<std::array<std::unique_ptr<TrackingEngine>, 3>, NumResponses>, NumQualities> trackingEngines;
        
        std::atomic<bool> isTrackingMode { false };
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixCrossfade;
        int activeOversampling = 0;
        std::unique_ptr<juce::dsp::Oversampling<float>> oversamplers[2];
        int activeQuality  = 1;
        int activeResponse = 1;

        /** Dry delay ring, long enough for the largest supported transform. */
        std::vector<float> dryDelayL, dryDelayR;
        int dryDelayLength = 0;
        int dryWritePos = 0;

        double currentSampleRate = 44100.0;

        /** Depth is ramped with an explicit time based step so the ramp duration
            is identical for any host block size. Mix and output gain are ramped
            per sample by SmoothedValue.
        */
        float depthCurrent = 1.0f;
        juce::SmoothedValue<float> mixSmoothed, outGainSmoothed;

        // Raw parameter pointers, cached so the audio thread never has to do
        // string lookups or hash map access while processing.
        std::atomic<float>* pDepth          = nullptr;
        std::atomic<float>* pSharpness      = nullptr;
        std::atomic<float>* pSelectivity    = nullptr;
        std::atomic<float>* pAttack         = nullptr;
        std::atomic<float>* pRelease        = nullptr;
        std::atomic<float>* pMaxReduction   = nullptr;
        std::atomic<float>* pTransient      = nullptr;
        std::atomic<float>* pProfile        = nullptr;
        std::atomic<float>* pQuality        = nullptr;
        std::atomic<float>* pResponse       = nullptr;
        std::atomic<float>* pStereoLink     = nullptr;
        std::atomic<float>* pIso226         = nullptr;
        std::atomic<float>* pModeHard       = nullptr;
        std::atomic<float>* pMidSide        = nullptr;
        std::atomic<float>* pDelta          = nullptr;
        std::atomic<float>* pSoloDeEss      = nullptr;
        std::atomic<float>* pBypass         = nullptr;
        std::atomic<float>* pMix            = nullptr;
        std::atomic<float>* pOutGain        = nullptr;
        std::atomic<float>* pAutoGain       = nullptr;
        std::atomic<float>* pDeltaBand      = nullptr;
        std::atomic<float>* pExternalKey    = nullptr;
        std::atomic<float>* pOversampling = nullptr;
        std::atomic<float>* pMotion         = nullptr;
        std::atomic<float>* pAttackTilt     = nullptr;
        std::atomic<float>* pReleaseTilt    = nullptr;
        std::atomic<float>* pDetailTilt     = nullptr;
        std::atomic<float>* pSibilanceSmooth= nullptr;
        std::atomic<float>* pSibilanceLow   = nullptr;
        std::atomic<float>* pSibilanceHigh  = nullptr;
        std::atomic<float>* pTracking = nullptr;

        // ---- level matching -----------------------------------------------------
        // The correction compares perceived loudness, not raw RMS. A resonance
        // suppressor removes energy at chosen frequencies and the ear does not
        // weight those frequencies equally: 3 dB off 3 kHz changes loudness far
        // more than 3 dB off 60 Hz. Matching on plain RMS therefore mis-sets the
        // correction, and the user ends up choosing on loudness anyway.
        //
        // K-weighting (ITU-R BS.1770) is the standard answer and is what every
        // loudness meter uses. Both stages are designed at the running sample rate,
        // so the response is correct at 44.1 kHz as well as 48 kHz. Any constant
        // gain inside the filter cancels, because dry and wet pass through the
        // identical filter and only their ratio is used.
        struct Biquad
        {
            double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
            double z1 = 0.0, z2 = 0.0;

            void reset() noexcept { z1 = z2 = 0.0; }

            float process (float x) noexcept
            {
                const double y = b0 * x + z1;
                z1 = b1 * x - a1 * y + z2;
                z2 = b2 * x - a2 * y;
                return static_cast<float> (y);
            }

            void setHighShelf (double fs, double f0, double q, double gainDb) noexcept
            {
                const double A  = std::pow (10.0, gainDb / 40.0);
                const double w0 = 2.0 * juce::MathConstants<double>::pi * f0 / fs;
                const double cw = std::cos (w0), sw = std::sin (w0);
                const double al = sw / (2.0 * q);
                const double sa = 2.0 * std::sqrt (A) * al;
                const double a0 = (A + 1.0) - (A - 1.0) * cw + sa;
                b0 =  A * ((A + 1.0) + (A - 1.0) * cw + sa) / a0;
                b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw) / a0;
                b2 =  A * ((A + 1.0) + (A - 1.0) * cw - sa) / a0;
                a1 =  2.0 * ((A - 1.0) - (A + 1.0) * cw) / a0;
                a2 =        ((A + 1.0) - (A - 1.0) * cw - sa) / a0;
            }

            void setHighPass (double fs, double f0, double q) noexcept
            {
                const double w0 = 2.0 * juce::MathConstants<double>::pi * f0 / fs;
                const double cw = std::cos (w0), sw = std::sin (w0);
                const double al = sw / (2.0 * q);
                const double a0 = 1.0 + al;
                b0 =  (1.0 + cw) / 2.0 / a0;
                b1 = -(1.0 + cw) / a0;
                b2 =  (1.0 + cw) / 2.0 / a0;
                a1 = -2.0 * cw / a0;
                a2 =  (1.0 - al) / a0;
            }
        };

        struct KWeight
        {
            Biquad shelf, hp;

            void design (double fs) noexcept
            {
                // Prototype parameters fitted so that the design reproduces the
                // published BS.1770-4 coefficients at 48 kHz, and stays correct at
                // any other rate. The shelf centre is 1501 Hz rather than the
                // 1682 Hz quoted in some references: for shelving filters "Q" and
                // the centre frequency mean different things in different design
                // formulas, and 1501 Hz is the value that actually lands on the
                // published response. The fit is within 0.0004 dB across
                // 20 Hz to 20 kHz.
                shelf.setHighShelf (fs, 1501.014451,  0.7073032370, 3.999443854);
                hp.setHighPass    (fs,   38.13547087602444, 0.5003270373238773);
            }
            void reset() noexcept { shelf.reset(); hp.reset(); }
            float process (float x) noexcept { return hp.process (shelf.process (x)); }
        };

        KWeight kDryL, kDryR, kWetL, kWetR;
        float agDryEnv = 0.0f, agWetEnv = 0.0f, agGain = 1.0f;
        float agDryPeak = 0.0f, agWetPeak = 0.0f;

        // ---- delta band filter ---------------------------------------------------
        // A single biquad band-pass applied to the delta signal so that the removed
        // content of one focus band can be monitored on its own. Written by hand
        // rather than with juce::dsp::IIR so that nothing allocates on the audio
        // thread when the band changes.
        void updateDeltaBandFilter (int bandIndex);
        float bpB0 = 1.0f, bpB1 = 0.0f, bpB2 = 0.0f, bpA1 = 0.0f, bpA2 = 0.0f;
        float bpX1 = 0.0f, bpX2 = 0.0f, bpY1 = 0.0f, bpY2 = 0.0f;
        float bpX1r = 0.0f, bpX2r = 0.0f, bpY1r = 0.0f, bpY2r = 0.0f;
        int   bpActiveBand = -1;
        float bpLastFreq = -1.0f, bpLastQ = -1.0f;

        std::atomic<float>* pEqEnable[8] {};
        std::atomic<float>* pEqType[8]   {};
        std::atomic<float>* pEqFreq[8]   {};
        std::atomic<float>* pEqGain[8]   {};
        std::atomic<float>* pEqQ[8]      {};

        std::array<std::atomic<float>, ScopeSize> scopeMagnitudeDb;
        std::array<std::atomic<float>, ScopeSize> scopeReductionDb;
        std::array<std::atomic<float>, ScopeSize> scopeBaselineDb;
        std::array<std::atomic<float>, ScopeSize> scopeWindowHz;
        std::atomic<float> scopeReferencePromDb { 0.0f };
        std::atomic<uint64_t> scopeFrameSequence { 0 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResonaProAudioProcessor)
    };
}
