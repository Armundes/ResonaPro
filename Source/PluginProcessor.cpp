#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

namespace ResonaPro
{
    namespace
    {
        constexpr int   kFftPow2[4] = { 10, 11, 12, 13 };
        constexpr int   kOverlap[3] = { 2, 4, 8 };

        const float kDefaultEqFreq[8] = { 80.0f, 250.0f, 500.0f, 1200.0f, 3200.0f, 6000.0f, 9000.0f, 14000.0f };
        const float kDefaultEqQ[8]    = { 0.707f, 1.0f, 1.2f, 1.2f, 1.4f, 1.5f, 1.6f, 0.707f };
        const int   kDefaultEqType[8] = { 3, 0, 0, 0, 0, 0, 0, 4 };
    }

    //==============================================================================
    ResonaProAudioProcessor::ResonaProAudioProcessor()
        : AudioProcessor (BusesProperties()
                              .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                              .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)
                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "Parameters", createParameterLayout())
    {
        for (int i = 0; i < ScopeSize; ++i)
        {
            scopeMagnitudeDb[i].store (-100.0f);
            scopeReductionDb[i].store (0.0f);
            scopeBaselineDb[i].store (-100.0f);
        }

        cacheParameterPointers();

        mixSmoothed.reset (currentSampleRate, 0.02);
        outGainSmoothed.reset (currentSampleRate, 0.02);
    }

    ResonaProAudioProcessor::~ResonaProAudioProcessor()
    {
        cancelPendingUpdate();
    }

    //==============================================================================
    void ResonaProAudioProcessor::cacheParameterPointers()
    {
        pDepth       = apvts.getRawParameterValue ("depth");
        pSharpness   = apvts.getRawParameterValue ("sharpness");
        pSelectivity = apvts.getRawParameterValue ("selectivity");
        pAttack      = apvts.getRawParameterValue ("attack");
        pRelease     = apvts.getRawParameterValue ("release");
        pMaxReduction= apvts.getRawParameterValue ("maxReduction");
        pTransient   = apvts.getRawParameterValue ("transientPreserve");
        pProfile     = apvts.getRawParameterValue ("vocalProfile");
        pQuality     = apvts.getRawParameterValue ("quality");
        pResponse    = apvts.getRawParameterValue ("response");
        pStereoLink  = apvts.getRawParameterValue ("stereoLink");
        pIso226      = apvts.getRawParameterValue ("iso226");
        pModeHard    = apvts.getRawParameterValue ("modeHard");
        pMidSide     = apvts.getRawParameterValue ("midSide");
        pDelta       = apvts.getRawParameterValue ("deltaListen");
        pBypass      = apvts.getRawParameterValue ("bypass");
        pMix         = apvts.getRawParameterValue ("mix");
        pOutGain     = apvts.getRawParameterValue ("outGain");
        pAutoGain    = apvts.getRawParameterValue ("autoGain");
        pDeltaBand   = apvts.getRawParameterValue ("deltaBand");
        pExternalKey = apvts.getRawParameterValue ("externalKey");
        pMultiRes    = apvts.getRawParameterValue ("multiResolution");
        pMotion      = apvts.getRawParameterValue ("motionProtect");
        pAttackTilt  = apvts.getRawParameterValue ("attackTilt");
        pReleaseTilt = apvts.getRawParameterValue ("releaseTilt");
        pDetailTilt  = apvts.getRawParameterValue ("detailTilt");
        pSibilanceSmooth = apvts.getRawParameterValue ("sibilanceSmooth");

        for (int b = 0; b < 8; ++b)
        {
            const auto s = juce::String (b + 1);
            pEqEnable[b] = apvts.getRawParameterValue ("eq_enable_" + s);
            pEqType[b]   = apvts.getRawParameterValue ("eq_type_" + s);
            pEqFreq[b]   = apvts.getRawParameterValue ("eq_freq_" + s);
            pEqGain[b]   = apvts.getRawParameterValue ("eq_gain_" + s);
            pEqQ[b]      = apvts.getRawParameterValue ("eq_q_" + s);
        }
    }

    //==============================================================================
    juce::AudioProcessorValueTreeState::ParameterLayout ResonaProAudioProcessor::createParameterLayout()
    {
        std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

        // --- main controls -----------------------------------------------------
        // Depth has a wide range on purpose; 1.0 is a gentle, musical starting
        // point and the upper half is there for creative use.
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "depth", "Depth", juce::NormalisableRange<float> (0.0f, 4.0f, 0.01f, 0.75f), 0.0f));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "sharpness", "Detail", juce::NormalisableRange<float> (0.2f, 4.0f, 0.01f, 0.6f), 1.0f));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "selectivity", "Selectivity", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "attack", "Attack", juce::NormalisableRange<float> (0.5f, 50.0f, 0.1f, 0.5f), 8.0f, " ms"));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "release", "Release", juce::NormalisableRange<float> (5.0f, 500.0f, 0.1f, 0.5f), 70.0f, " ms"));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "maxReduction", "Max Cut", juce::NormalisableRange<float> (3.0f, 36.0f, 0.1f), 24.0f, " dB"));

        // Transient protect raises the detection threshold on attacks, so
        // consonants and plosives survive. 0 = process everything (best for
        // de-essing), 1 = leave all attacks alone.
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "transientPreserve", "Transient Protect", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));

        juce::StringArray profiles;
        for (int i = 0; i < 4; ++i) profiles.add (getVocalProfileName (static_cast<VocalProfile> (i)));
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            "vocalProfile", "Vocal Profile", profiles, 0));

        // --- quality / response ------------------------------------------------
        // These replace the old "oversampling" switch. Resampling the signal into
        // a fixed 2048-point transform (what the plug-in used to do) *reduced*
        // the usable frequency resolution by the oversampling factor - at 8x only
        // about 128 bins covered the whole audio band. Changing the transform
        // length instead gives a real resolution improvement, and changing the
        // overlap gives the faster gain updates that made oversampling feel
        // "smoother" in the first place.
        juce::StringArray qualityChoices { "Low Latency (1k)", "Standard (2k)", "High (4k)", "Ultra (8k)" };
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            "quality", "Quality", qualityChoices, 1));

        juce::StringArray responseChoices { "Eco (2x overlap)", "Standard (4x)", "Fine (8x)" };
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            "response", "Response", responseChoices, 1));

        // --- stereo / character ------------------------------------------------
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "stereoLink", "Stereo Link", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));

        params.push_back (std::make_unique<juce::AudioParameterBool> (
            "iso226", "Ear Guard (approximate)", true));

        params.push_back (std::make_unique<juce::AudioParameterBool> (
            "modeHard", "Hard Mode", false));

        params.push_back (std::make_unique<juce::AudioParameterBool> (
            "midSide", "Mid/Side", false));

        params.push_back (std::make_unique<juce::AudioParameterBool> (
            "deltaListen", "Delta", false));

        params.push_back (std::make_unique<juce::AudioParameterBool> (
            "bypass", "Bypass", false));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "mix", "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, " %"));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "outGain", "Output", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, " dB"));

        // Level match: trims the output so it sits at the same average level as
        // the input. Removing resonances costs level, and a quieter signal almost
        // always sounds better in a comparison, so without this the user is
        // choosing on loudness rather than on quality.
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            "autoGain", "Match Level", false));

        // Delta restricted to one focus band, so the graph can be used to target
        // a specific region instead of monitoring everything at once.
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            "deltaBand", "Delta Band",
            juce::StringArray { "All", "1", "2", "3", "4", "5", "6", "7", "8" }, 0));

        params.push_back (std::make_unique<juce::AudioParameterBool> (
            "externalKey", "External Sidechain", false));
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            "multiResolution", "Low Band Detail", false));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "motionProtect", "Note Motion", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "attackTilt", "Attack Tilt", juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "releaseTilt", "Release Tilt", juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "detailTilt", "Detail Tilt", juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "sibilanceSmooth", "Sibilance De-Ess", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));

        // --- eight focus bands -------------------------------------------------
        const juce::StringArray kFilterTypeChoices {
            "Bell",
            "Low Cut (High Pass)",
            "High Cut (Low Pass)",
            "Low Shelf",
            "High Shelf",
            "Band Pass"
        };

        for (int b = 0; b < 8; ++b)
        {
            const auto s = juce::String (b + 1);
            params.push_back (std::make_unique<juce::AudioParameterBool> (
                "eq_enable_" + s, "Band " + s + " Enable", true));
            params.push_back (std::make_unique<juce::AudioParameterChoice> (
                "eq_type_" + s, "Band " + s + " Type", kFilterTypeChoices, kDefaultEqType[b]));
            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                "eq_freq_" + s, "Band " + s + " Freq",
                juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.3f), kDefaultEqFreq[b], " Hz"));
            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                "eq_gain_" + s, "Band " + s + " Sensitivity",
                juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, " dB"));
            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                "eq_q_" + s, "Band " + s + " Q",
                juce::NormalisableRange<float> (0.1f, 8.0f, 0.05f, 0.5f), kDefaultEqQ[b]));
        }

        return { params.begin(), params.end() };
    }

    //==============================================================================
    void ResonaProAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        juce::ignoreUnused (samplesPerBlock);
        currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

        buildEngines (currentSampleRate);

        // Dry delay ring: long enough for the largest supported transform so the
        // dry path always stays sample aligned with the spectral path.
        int maxLatency = 0;
        for (int q = 0; q < NumQualities; ++q)
            for (int r = 0; r < NumResponses; ++r)
                if (engines[q][r] != nullptr)
                    maxLatency = std::max (maxLatency, engines[q][r]->getLatencySamples());

        dryDelayLength = maxLatency + 8;
        dryDelayL.assign (static_cast<size_t> (dryDelayLength), 0.0f);
        dryDelayR.assign (static_cast<size_t> (dryDelayLength), 0.0f);
        dryWritePos = 0;

        // Mix and output gain are ramped per sample; depth is ramped with an
        // explicit time based step (see processBlock) so that the ramp takes the
        // same amount of *time* whatever block size the host uses.
        mixSmoothed.reset (currentSampleRate, 0.02);
        outGainSmoothed.reset (currentSampleRate, 0.02);

        const float initialDepth = pDepth != nullptr ? pDepth->load() : 1.0f;
        depthCurrent = initialDepth;
        mixSmoothed.setCurrentAndTargetValue (pMix != nullptr ? pMix->load() * 0.01f : 1.0f);
        outGainSmoothed.setCurrentAndTargetValue (
            juce::Decibels::decibelsToGain (pOutGain != nullptr ? pOutGain->load() : 0.0f));

        agDryEnv = agWetEnv = 0.0f;
        agGain   = 1.0f;
        agWetPeak = 0.0f;
        agDryPeak = 0.0f;
        kDryL.design (currentSampleRate); kDryR.design (currentSampleRate);
        kWetL.design (currentSampleRate); kWetR.design (currentSampleRate);
        kDryL.reset(); kDryR.reset(); kWetL.reset(); kWetR.reset();
        bpX1 = bpX2 = bpY1 = bpY2 = 0.0f;
        bpX1r = bpX2r = bpY1r = bpY2r = 0.0f;
        bpActiveBand = -1;
        bpLastFreq = bpLastQ = -1.0f;

        updateLatencyAndDisplay();
    }

    void ResonaProAudioProcessor::buildEngines (double sampleRate)
    {
        for (int q = 0; q < NumQualities; ++q)
        {
            for (int r = 0; r < NumResponses; ++r)
            {
                if (engines[q][r] == nullptr)
                    engines[q][r] = std::make_unique<SpectralEngine>();

                engines[q][r]->prepare (kFftPow2[q], kOverlap[r], sampleRate);
            }
        }
    }

    void ResonaProAudioProcessor::releaseResources()
    {
        // Engines keep their allocations; just clear running state.
        for (int q = 0; q < NumQualities; ++q)
            for (int r = 0; r < NumResponses; ++r)
                if (engines[q][r] != nullptr)
                    engines[q][r]->reset();

        std::fill (dryDelayL.begin(), dryDelayL.end(), 0.0f);
        std::fill (dryDelayR.begin(), dryDelayR.end(), 0.0f);
        dryWritePos = 0;
    }

    bool ResonaProAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto out = layouts.getMainOutputChannelSet();
        if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
            return false;
        if (layouts.getMainInputChannelSet() != out || layouts.inputBuses.size() < 2)
            return false;
        const auto key = layouts.inputBuses[1];
        return key.isDisabled() || key == juce::AudioChannelSet::mono()
               || key == juce::AudioChannelSet::stereo();
    }

    int ResonaProAudioProcessor::getEngineLatencySamples() const noexcept
    {
        const auto& e = engines[static_cast<size_t> (juce::jlimit (0, NumQualities - 1, activeQuality))]
                                 [static_cast<size_t> (juce::jlimit (0, NumResponses - 1, activeResponse))];
        return e != nullptr ? e->getLatencySamples() : 0;
    }

    double ResonaProAudioProcessor::getTailLengthSeconds() const
    {
        return static_cast<double> (getEngineLatencySamples() + 8192) / currentSampleRate;
    }

    void ResonaProAudioProcessor::updateLatencyAndDisplay()
    {
        const int latency = getEngineLatencySamples();
        if (getLatencySamples() != latency)
        {
            setLatencySamples (latency);
            updateHostDisplay();
        }
    }

    void ResonaProAudioProcessor::handleAsyncUpdate()
    {
        updateLatencyAndDisplay();
    }

    //==============================================================================
    void ResonaProAudioProcessor::updateDeltaBandFilter (int bandIndex)
    {
        if (bandIndex < 0 || bandIndex > 7)
        {
            if (bpActiveBand < 0) return;
            bpActiveBand = -1;
            // No filter: the whole removed signal is monitored.
            bpB0 = 1.0f; bpB1 = 0.0f; bpB2 = 0.0f; bpA1 = 0.0f; bpA2 = 0.0f;
            return;
        }

        // RBJ band-pass at the focus band's centre frequency. The monitor width
        // follows the band's own Q so that what you hear is what is being shaped.
        const auto& band = sidechainEQ.getBand (bandIndex);
        if (bandIndex == bpActiveBand && bpLastFreq == band.frequency && bpLastQ == band.q)
            return;
        bpActiveBand = bandIndex;
        bpLastFreq = band.frequency;
        bpLastQ = band.q;
        const float f0 = juce::jlimit (20.0f, static_cast<float> (currentSampleRate * 0.45), band.frequency);
        const float q  = juce::jlimit (0.3f, 4.0f, band.q * 0.6f);

        const float w0    = 2.0f * juce::MathConstants<float>::pi * f0
                            / static_cast<float> (currentSampleRate);
        const float cosw  = std::cos (w0);
        const float alpha = std::sin (w0) / (2.0f * q);
        const float a0    = 1.0f + alpha;

        bpB0 =  alpha / a0;
        bpB1 =  0.0f;
        bpB2 = -alpha / a0;
        bpA1 = (-2.0f * cosw) / a0;
        bpA2 = (1.0f - alpha) / a0;
    }

    void ResonaProAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        juce::ScopedNoDenormals noDenormals;

        const int numIn  = getMainBusNumInputChannels();
        const int numOut = getMainBusNumOutputChannels();
        const int n      = buffer.getNumSamples();

        // JUCE's processBlock buffer may also contain an optional sidechain bus.
        // Never clear or overwrite it; it may alias memory owned by the host.
        const auto keyBus = getBusBuffer (buffer, true, 1);
        const bool useKey = pExternalKey != nullptr && pExternalKey->load() > 0.5f
                            && keyBus.getNumChannels() > 0;
        const float* keyL = useKey ? keyBus.getReadPointer (0) : nullptr;
        const float* keyR = useKey ? keyBus.getReadPointer (keyBus.getNumChannels() > 1 ? 1 : 0) : nullptr;

        if (numOut > numIn && numOut <= buffer.getNumChannels())
            for (int i = numIn; i < numOut; ++i)
                buffer.clear (i, 0, n);

        if (n == 0 || numIn == 0)
            return;

        // ---- parameter snapshot -------------------------------------------------
        const float depthTarget   = pDepth->load();
        const float sharpness     = pSharpness->load();
        const float selectivity   = pSelectivity->load();
        const float attackMs      = pAttack->load();
        const float releaseMs     = pRelease->load();
        const float maxReduction  = pMaxReduction->load();
        const float transientProt = pTransient->load();
        const int   profileIdx    = static_cast<int> (pProfile->load());
        const int   qualityIdx    = juce::jlimit (0, NumQualities - 1, static_cast<int> (pQuality->load()));
        const int   responseIdx   = juce::jlimit (0, NumResponses - 1, static_cast<int> (pResponse->load()));
        const float stereoLink    = pStereoLink->load();
        const bool  iso226        = pIso226->load() > 0.5f;
        const bool  hardMode      = pModeHard->load() > 0.5f;
        const bool  midSide       = pMidSide->load() > 0.5f;
        const bool  deltaListen   = pDelta->load() > 0.5f;
        const bool  bypassed      = pBypass->load() > 0.5f;
        const float mixTarget     = pMix->load() * 0.01f;
        const float outGainDb     = pOutGain->load();
        const bool  autoGainOn    = pAutoGain != nullptr && pAutoGain->load() > 0.5f;

        // Choice 0 is "All"; 1..8 select a focus band. Stored as a zero-based
        // band index, with -1 meaning no band filter.
        const int deltaBandIdx = pDeltaBand != nullptr
                                   ? static_cast<int> (pDeltaBand->load()) - 1
                                   : -1;

        // The level correction is applied from the previous block's measurement,
        // which costs one block of latency on a slow trim and nothing else.
        const float agApply = autoGainOn ? agGain : 1.0f;
        double sumDrySq = 0.0, sumOutSq = 0.0;

        mixSmoothed.setTargetValue (mixTarget);
        outGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (outGainDb));

        // Depth ramp: 20 ms regardless of block size.
        {
            const float step = juce::jmax (1.0e-6f,
                                           static_cast<float> (n) /
                                           static_cast<float> (0.02 * currentSampleRate));
            depthCurrent += juce::jlimit (-step, step, depthTarget - depthCurrent);
        }

        // ---- engine selection (no allocation: all variants are pre-prepared) ----
        if (qualityIdx != activeQuality || responseIdx != activeResponse)
        {
            activeQuality  = qualityIdx;
            activeResponse = responseIdx;
            engines[static_cast<size_t> (activeQuality)][static_cast<size_t> (activeResponse)]->reset();
            triggerAsyncUpdate();          // report the new latency from the message thread
        }

        auto& engine = *engines[static_cast<size_t> (activeQuality)][static_cast<size_t> (activeResponse)];

        engine.setVocalProfile (static_cast<VocalProfile> (profileIdx));

        for (int b = 0; b < 8; ++b)
        {
            auto& band = sidechainEQ.getBand (b);
            band.enabled   = pEqEnable[b]->load() > 0.5f;
            band.type      = static_cast<FilterType> (static_cast<int> (pEqType[b] ? pEqType[b]->load() : 0.0f));
            band.frequency = pEqFreq[b]->load();
            band.gainDb    = pEqGain[b]->load();
            band.q         = pEqQ[b]->load();
        }
        updateDeltaBandFilter (deltaBandIdx);
        engine.updateSidechainWeights (sidechainEQ);

        SpectralEngine::Params ep;
        ep.depth          = depthCurrent;
        ep.sharpness      = sharpness;
        ep.selectivity    = selectivity;
        ep.attackMs       = attackMs;
        ep.releaseMs      = releaseMs;
        ep.maxReductionDb = maxReduction;
        ep.stereoLink     = stereoLink;
        ep.transientGuard = transientProt;
        ep.mode           = hardMode ? ProcessingMode::Hard : ProcessingMode::Soft;
        ep.midSide        = midSide;
        ep.useIso226      = iso226;
        ep.multiResolution = pMultiRes != nullptr && pMultiRes->load() > 0.5f;
        ep.motionProtect   = pMotion != nullptr ? pMotion->load() : 0.0f;
        ep.attackTilt      = pAttackTilt != nullptr ? pAttackTilt->load() : 0.0f;
        ep.releaseTilt     = pReleaseTilt != nullptr ? pReleaseTilt->load() : 0.0f;
        ep.detailTilt      = pDetailTilt != nullptr ? pDetailTilt->load() : 0.0f;
        ep.sibilanceSmooth = pSibilanceSmooth != nullptr ? pSibilanceSmooth->load() : 0.5f;
        engine.setParams (ep);

        const int latency = engine.getLatencySamples();

        float* left  = buffer.getWritePointer (0);
        float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

        // ---- per-sample pipeline (allocation free) ------------------------------
        for (int s = 0; s < n; ++s)
        {
            // Sanitize before writing the dry delay: sanitizing inside the FFT
            // alone still leaves the dry ring (and level-match RMS) poisoned.
            const float xL = std::isfinite (left[s]) ? left[s] : 0.0f;
            const float xR = right != nullptr
                               ? (std::isfinite (right[s]) ? right[s] : 0.0f) : xL;

            float wetL = 0.0f, wetR = 0.0f;
            engine.processSample (xL, xR, wetL, wetR,
                                  useKey ? keyL[s] : xL, useKey ? keyR[s] : xR, useKey);

            // Dry path, delayed by exactly the spectral latency so that dry/wet
            // blending and delta monitoring stay phase accurate.
            const int readIdx = dryWritePos - latency;
            const int wrapped = readIdx < 0 ? readIdx + dryDelayLength : readIdx;
            const float dryL = dryDelayL[static_cast<size_t> (wrapped)];
            const float dryR = dryDelayR[static_cast<size_t> (wrapped)];
            dryDelayL[static_cast<size_t> (dryWritePos)] = xL;
            dryDelayR[static_cast<size_t> (dryWritePos)] = xR;
            if (++dryWritePos >= dryDelayLength)
                dryWritePos = 0;

            const float m = mixSmoothed.getNextValue();
            const float outGain = outGainSmoothed.getNextValue();

            float yL, yR;
            if (bypassed)
            {
                yL = dryL;
                yR = dryR;
            }
            else if (deltaListen)
            {
                // Exactly the energy the processor removed.
                yL = dryL - wetL;
                yR = dryR - wetR;

                // Optionally restricted to one focus band, so the graph can be
                // used to target a region instead of monitoring everything.
                if (deltaBandIdx >= 0)
                {
                    auto biquad = [this] (float x, float x1, float x2, float y1, float y2, float& outY1, float& outY2)
                    {
                        const float y = bpB0 * x + bpB1 * x1 + bpB2 * x2 - bpA1 * y1 - bpA2 * y2;
                        outY1 = y;
                        outY2 = y1;
                        return y;
                    };

                    yL = biquad (yL, bpX1, bpX2, bpY1, bpY2, bpY1, bpY2);
                    yR = biquad (yR, bpX1r, bpX2r, bpY1r, bpY2r, bpY1r, bpY2r);

                    bpX2 = bpX1; bpX1 = dryL - wetL;
                    bpX2r = bpX1r; bpX1r = dryR - wetR;
                }
            }
            else
            {
                yL = dryL + m * (wetL - dryL);
                yR = dryR + m * (wetR - dryR);
            }

            if (numOut == 1)
            {
                left[s] = yL * outGain * agApply;
            }
            else
            {
                left[s]  = yL * outGain * agApply;
                if (right != nullptr) right[s] = yR * outGain * agApply;
            }

            // Loudness is accumulated through the K-weighting filter. The peak is
            // taken from the unfiltered wet signal, because the peak is what decides
            // whether a correction boost would clip.
            const float kdL = kDryL.process (dryL);
            const float kwL = kWetL.process (yL);
            if (numOut == 1)
            {
                sumDrySq += double (kdL) * kdL;
                sumOutSq += double (kwL) * kwL;
            }
            else
            {
                const float kdR = kDryR.process (dryR);
                const float kwR = kWetR.process (yR);
                sumDrySq += 0.5 * (double (kdL) * kdL + double (kdR) * kdR);
                sumOutSq += 0.5 * (double (kwL) * kwL + double (kwR) * kwR);
            }

            const float wetAbs = (numOut == 1) ? std::abs (yL)
                                               : std::max (std::abs (yL), std::abs (yR));
            if (wetAbs > agWetPeak) agWetPeak = wetAbs;

            const float dryAbs = (numOut == 1) ? std::abs (dryL)
                                               : std::max (std::abs (dryL), std::abs (dryR));
            if (dryAbs > agDryPeak) agDryPeak = dryAbs;

            if (! std::isfinite (left[s])) left[s] = 0.0f;
            if (right != nullptr && ! std::isfinite (right[s])) right[s] = 0.0f;
        }

        // ---- level matching -----------------------------------------------------
        // Compare the perceived loudness of the dry signal with the loudness of what
        // is leaving the plug-in, and trim towards equality. Removing resonances
        // costs level, and a quieter signal nearly always sounds better in a
        // comparison, so without this the user ends up choosing on loudness.
        {
            const float blockCoeff = std::exp (-static_cast<float> (n)
                                               / static_cast<float> (0.35 * currentSampleRate));
            const float dryLoud = static_cast<float> (std::sqrt (sumDrySq / std::max (1, n)));
            const float wetLoud = static_cast<float> (std::sqrt (sumOutSq / std::max (1, n)));

            agDryEnv = blockCoeff * agDryEnv + (1.0f - blockCoeff) * dryLoud;
            agWetEnv = blockCoeff * agWetEnv + (1.0f - blockCoeff) * wetLoud;

            // Peak hold with a slow release, so the guard below tracks the current
            // passage rather than the loudest moment of the whole take.
            const float peakCoeff = std::exp (-static_cast<float> (n)
                                              / static_cast<float> (0.40 * currentSampleRate));
            agWetPeak = peakCoeff * agWetPeak;
            agDryPeak = peakCoeff * agDryPeak;
            if (! std::isfinite (agWetPeak))
                agWetPeak = 0.0f;
            if (! std::isfinite (agDryPeak))
                agDryPeak = 0.0f;

            // Matching is meaningless while bypassed, and actively wrong while
            // monitoring delta: the output there is the removed content, which is
            // meant to be quiet, and matching it to the dry level would misrepresent
            // how much was taken out.
            const bool agActive = autoGainOn && ! bypassed && ! deltaListen
                                && agWetEnv > 1.0e-5f && agDryEnv > 1.0e-5f;

            float wanted = agActive ? juce::jlimit (0.25f, 4.0f, agDryEnv / agWetEnv) : 1.0f;

            // Never let the correction add clipping the input did not already have.
            // The output peak may rise as far as the louder of the -0.5 dBFS ceiling
            // and whatever the input itself peaked at, and no further. A quiet take
            // is therefore compensated in full, while a hot one is never pushed past
            // the level the user was already working at. Only the boost is capped,
            // so the trim can always pull down and the gain never falls below unity
            // on this account.
            constexpr float ceiling = 0.944f;   // -0.5 dBFS
            if (wanted > 1.0f && agWetPeak > 1.0e-6f)
            {
                const float allowed = (agDryPeak >= ceiling) ? agDryPeak : ceiling;
                wanted = std::min (wanted, allowed / agWetPeak);
            }

            const float gainCoeff = std::exp (-static_cast<float> (n)
                                              / static_cast<float> (0.25 * currentSampleRate));
            agGain = gainCoeff * agGain + (1.0f - gainCoeff) * wanted;
            if (agDryPeak >= ceiling && agWetPeak > 1.0e-6f)
                agGain = std::min (agGain, agDryPeak / agWetPeak);
            if (! std::isfinite (agGain))
                agGain = 1.0f;
        }

        // ---- visualiser ---------------------------------------------------------
        const float* mag  = engine.getMagnitudeSpectrum();
        const float* red  = engine.getReductionDb();
        const float* base = engine.getBaselineDb();
        const int bins = engine.getNumBins();
        const double sr = engine.getSampleRate();

        for (int i = 0; i < ScopeSize; ++i)
        {
            // Log frequency axis from 20 Hz to 20 kHz, matching the frequency ruler.
            const double norm = static_cast<double> (i) / static_cast<double> (ScopeSize - 1);
            const double f    = 20.0 * std::pow (1000.0, norm);
            const double bin  = f / (sr * 0.5) * static_cast<double> (bins - 1);

            const int b0 = juce::jlimit (0, bins - 1, static_cast<int> (bin));
            const int b1 = juce::jmin (bins - 1, b0 + 1);
            const float t = static_cast<float> (bin - static_cast<double> (b0));

            const float m = mag[b0]  * (1.0f - t) + mag[b1]  * t;
            const float r = red[b0]  * (1.0f - t) + red[b1]  * t;
            const float bl = base[b0] * (1.0f - t) + base[b1] * t;

            scopeMagnitudeDb[i].store (juce::Decibels::gainToDecibels (juce::jmax (1.0e-7f, m), -140.0f));
            scopeReductionDb[i].store (r);
            scopeBaselineDb[i].store (bl);
        }
        scopeReferencePromDb.store (engine.getReferenceProminenceDb());
        scopeFrameSequence.fetch_add (1, std::memory_order_release);
    }

    //==============================================================================
    void ResonaProAudioProcessor::getVisualizerData (std::array<float, ScopeSize>& magnitudeDbOut,
                                                     std::array<float, ScopeSize>& reductionDbOut,
                                                     std::array<float, ScopeSize>& baselineDbOut,
                                                     float& referenceProminenceDbOut,
                                                     uint64_t* frameSequenceOut)
    {
        for (int i = 0; i < ScopeSize; ++i)
        {
            magnitudeDbOut[i] = scopeMagnitudeDb[i].load();
            reductionDbOut[i] = scopeReductionDb[i].load();
            baselineDbOut[i]  = scopeBaselineDb[i].load();
        }
        referenceProminenceDbOut = scopeReferencePromDb.load();
        if (frameSequenceOut != nullptr)
            *frameSequenceOut = scopeFrameSequence.load (std::memory_order_acquire);
    }

    //==============================================================================
    void ResonaProAudioProcessor::setParameterValue (const juce::String& paramID, float value)
    {
        if (auto* param = apvts.getParameter (paramID))
        {
            const auto& range = param->getNormalisableRange();
            param->setValueNotifyingHost (range.convertTo0to1 (value));
        }
    }

    juce::StringArray ResonaProAudioProcessor::getPresetNames()
    {
        return { "Init (Neutral)",
                 "Lead Vocal - Pop Silky & Open",
                 "Lead Vocal - Modern Trap / Rap Presence",
                 "Lead Vocal - R&B Velvet Silk",
                 "Lead Vocal - Forward & Intimate",
                 "De-Box & De-Mud (Room Acoustic Fix)",
                 "Singer's Formant Clarity",
                 "De-Ess - Smooth Sibilance",
                 "De-Ess - Whistling & Piercing Peak",
                 "Plosive & Breath Safe Vocal",
                 "Acoustic Guitar Harshness Tamer",
                 "Podcast & Broadcast Warmth",
                 "Background Vocals Wide Space",
                 "Mix Bus Silk & Air Glue",
                 "Extreme Resonance Hunt" };
    }

    int ResonaProAudioProcessor::getNumPresets() { return getPresetNames().size(); }

    void ResonaProAudioProcessor::loadPreset (int presetIndex)
    {
        auto reset = [this]
        {
            const char* ids[] = { "depth", "sharpness", "selectivity", "attack", "release",
                                  "maxReduction", "transientPreserve", "vocalProfile",
                                  "stereoLink", "mix", "outGain", "quality", "response" };
            const float vals[] = { 1.0f, 1.0f, 0.5f, 8.0f, 70.0f, 24.0f, 0.5f, 0.0f,
                                   1.0f, 100.0f, 0.0f, 1.0f, 1.0f };
            for (size_t i = 0; i < sizeof (ids) / sizeof (ids[0]); ++i)
                setParameterValue (ids[i], vals[i]);

            const char* bools[] = { "iso226", "modeHard", "midSide", "deltaListen", "bypass",
                                    "autoGain", "externalKey", "multiResolution" };
            const float boolVals[] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                       0.0f, 0.0f, 0.0f };
            for (size_t i = 0; i < sizeof (bools) / sizeof (bools[0]); ++i)
                setParameterValue (bools[i], boolVals[i]);

            setParameterValue ("motionProtect", 0.0f);
            setParameterValue ("attackTilt", 0.0f);
            setParameterValue ("releaseTilt", 0.0f);
            setParameterValue ("detailTilt", 0.0f);
            setParameterValue ("sibilanceSmooth", 0.5f);
            setParameterValue ("deltaBand", 0.0f);

            for (int b = 1; b <= 8; ++b)
            {
                const auto s = juce::String (b);
                setParameterValue ("eq_enable_" + s, 1.0f);
                setParameterValue ("eq_type_" + s, static_cast<float> (kDefaultEqType[b - 1]));
                setParameterValue ("eq_freq_" + s, kDefaultEqFreq[b - 1]);
                setParameterValue ("eq_gain_" + s, 0.0f);
                setParameterValue ("eq_q_" + s, kDefaultEqQ[b - 1]);
            }
        };

        reset();

        auto set = [this] (const char* id, float v) { setParameterValue (id, v); };

        switch (presetIndex)
        {
            case 0:   // Init
                set ("depth", 0.0f);
                break;

            case 1:   // Pop Silky & Open
                set ("vocalProfile", 3.0f); // AirAndSilk
                set ("depth", 1.2f); set ("sharpness", 1.1f); set ("selectivity", 0.48f);
                set ("attack", 6.0f); set ("release", 65.0f); set ("transientPreserve", 0.60f);
                set ("eq_freq_5", 3500.0f); set ("eq_gain_5", 2.5f); set ("eq_q_5", 1.4f);
                break;

            case 2:   // Modern Trap / Rap Presence
                set ("depth", 1.8f); set ("sharpness", 1.6f); set ("selectivity", 0.40f);
                set ("attack", 4.0f); set ("release", 50.0f); set ("transientPreserve", 0.70f);
                set ("eq_freq_4", 2400.0f); set ("eq_gain_4", 3.0f); set ("eq_q_4", 1.5f);
                set ("eq_freq_6", 6500.0f); set ("eq_gain_6", 3.5f); set ("eq_q_6", 2.0f);
                break;

            case 3:   // R&B Velvet Silk
                set ("vocalProfile", 3.0f); // AirAndSilk
                set ("depth", 1.5f); set ("sharpness", 1.0f); set ("selectivity", 0.50f);
                set ("attack", 8.0f); set ("release", 80.0f); set ("transientPreserve", 0.65f);
                set ("eq_freq_3", 1200.0f); set ("eq_gain_3", 2.0f); set ("eq_q_3", 1.2f);
                break;

            case 4:   // Forward & Intimate
                set ("depth", 1.4f); set ("sharpness", 1.8f); set ("selectivity", 0.55f);
                set ("attack", 6.0f); set ("release", 90.0f); set ("transientPreserve", 0.75f);
                set ("eq_gain_1", -12.0f); set ("eq_gain_2", -6.0f); // keep full low-end body
                break;

            case 5:   // De-Box & De-Mud (Room Acoustic Fix)
                set ("vocalProfile", 2.0f); // WarmBodyClarity
                set ("depth", 2.2f); set ("sharpness", 0.9f); set ("selectivity", 0.38f);
                set ("attack", 10.0f); set ("release", 120.0f); set ("transientPreserve", 0.50f);
                set ("eq_freq_2", 350.0f); set ("eq_gain_2", 5.0f); set ("eq_q_2", 1.8f);
                set ("eq_freq_3", 650.0f); set ("eq_gain_3", 3.5f); set ("eq_q_3", 1.6f);
                break;

            case 6:   // Singer's Formant Clarity
                set ("depth", 1.6f); set ("sharpness", 1.5f); set ("selectivity", 0.45f);
                set ("attack", 7.0f); set ("release", 75.0f); set ("transientPreserve", 0.60f);
                set ("eq_freq_4", 2800.0f); set ("eq_gain_4", 3.5f); set ("eq_q_4", 2.2f);
                break;

            case 7:   // De-Ess - Smooth Sibilance
                set ("vocalProfile", 1.0f); // DeEss
                set ("depth", 1.8f); set ("sharpness", 1.0f); set ("selectivity", 0.35f);
                set ("attack", 2.0f); set ("release", 35.0f); set ("transientPreserve", 0.20f);
                set ("attackTilt", 0.60f); set ("releaseTilt", 0.50f); set ("sibilanceSmooth", 0.85f);
                set ("maxReduction", 18.0f);
                set ("eq_enable_6", 1.0f); set ("eq_type_6", 4.0f); set ("eq_freq_6", 6200.0f); set ("eq_gain_6", 6.0f); set ("eq_q_6", 0.71f);
                break;

            case 8:   // De-Ess - Whistling & Piercing Peak
                set ("vocalProfile", 1.0f); // DeEss
                set ("depth", 2.5f); set ("sharpness", 2.8f); set ("selectivity", 0.55f);
                set ("attack", 2.0f); set ("release", 50.0f); set ("transientPreserve", 0.30f);
                set ("eq_freq_6", 7200.0f); set ("eq_gain_6", 6.0f); set ("eq_q_6", 3.0f);
                set ("eq_freq_7", 9800.0f); set ("eq_gain_7", 5.0f); set ("eq_q_7", 2.5f);
                break;

            case 9:   // Plosive & Breath Safe Vocal
                set ("vocalProfile", 0.0f);
                set ("depth", 1.6f); set ("sharpness", 1.2f); set ("selectivity", 0.45f);
                set ("transientPreserve", 0.90f); set ("attack", 8.0f); set ("release", 60.0f);
                break;

            case 10:  // Acoustic Guitar Harshness Tamer
                set ("depth", 1.8f); set ("sharpness", 1.4f); set ("selectivity", 0.42f);
                set ("attack", 5.0f); set ("release", 70.0f); set ("transientPreserve", 0.55f);
                set ("eq_freq_4", 2200.0f); set ("eq_gain_4", 4.0f); set ("eq_q_4", 2.0f);
                set ("eq_freq_5", 4200.0f); set ("eq_gain_5", 3.0f); set ("eq_q_5", 1.8f);
                break;

            case 11:  // Podcast & Broadcast Warmth
                set ("vocalProfile", 2.0f);
                set ("depth", 1.6f); set ("sharpness", 0.85f); set ("selectivity", 0.40f);
                set ("attack", 10.0f); set ("release", 110.0f); set ("transientPreserve", 0.65f);
                set ("autoGain", 1.0f);
                break;

            case 12:  // Background Vocals Wide Space
                set ("midSide", 1.0f); set ("stereoLink", 0.15f); // Mid-focused suppression, leaves Side reverb wide
                set ("depth", 2.0f); set ("sharpness", 1.2f); set ("selectivity", 0.45f);
                set ("attack", 8.0f); set ("release", 85.0f); set ("transientPreserve", 0.50f);
                break;

            case 13:  // Mix Bus Silk & Air Glue
                set ("depth", 0.9f); set ("sharpness", 0.50f); set ("selectivity", 0.50f);
                set ("attack", 12.0f); set ("release", 140.0f); set ("transientPreserve", 0.60f);
                set ("quality", 2.0f); set ("maxReduction", 8.0f);
                break;

            case 14:  // Extreme Resonance Hunt
                set ("depth", 3.0f); set ("sharpness", 3.0f); set ("selectivity", 0.65f);
                set ("attack", 2.0f); set ("release", 40.0f);
                set ("transientPreserve", 0.2f); set ("maxReduction", 30.0f);
                break;

            default:
                break;
        }
    }

    //==============================================================================
    juce::AudioProcessorEditor* ResonaProAudioProcessor::createEditor()
    {
        return new ResonaProAudioProcessorEditor (*this);
    }

    void ResonaProAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        auto state = apvts.copyState();
        state.setProperty ("resonaProVersion", 2, nullptr);
        if (auto xml = state.createXml())
            copyXmlToBinary (*xml, destData);
    }

    void ResonaProAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        auto xmlState = getXmlFromBinary (data, sizeInBytes);
        if (xmlState != nullptr && xmlState->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ResonaPro::ResonaProAudioProcessor();
}
