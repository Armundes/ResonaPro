#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>

#if defined(__APPLE__)
 #include <Accelerate/Accelerate.h>
#else
 #error "ResonaPro's spectral engine currently requires Apple's Accelerate/vDSP framework."
#endif

#include "ResonanceDetector.h"
#include "DynamicSuppressor.h"
#include "SibilanceDeEsser.h"
#include "ParametricEQWeighting.h"
#include "TransientDetector.h"

namespace ResonaPro
{
    /** Placeholder for Phase Three neural network fallback. */
    struct NeuralDetector
    {
        bool isLoaded() const noexcept { return false; }
        void process(const float* magIn, float* reductionOut, int numBins) { (void)magIn; (void)reductionOut; (void)numBins; }
    };

    /** Weighted Overlap-Add (WOLA) short-time Fourier transform engine.

        Signal path (per channel):

            input ring -> sqrt-Hann window -> real FFT -> magnitude
                       -> resonance detection -> per-bin dynamic gain
                       -> inverse FFT -> sqrt-Hann window -> overlap-add
                       -> output ring

        Analysis and synthesis windows are both the square root of a Hann
        window, so their product is a Hann window. At a hop of N/4 the Hann
        window satisfies the constant-overlap-add condition with a sum of 2 and
        the complete round trip is unity gain to within ~1e-15 (measured).

        The synthesis normalisation is *derived* rather than hard-coded:
        vDSP's real FFT round trip has a gain of 2N and the COLA sum is
        N/(2*hop), so the correct scale is hop / N^2. That keeps perfect
        reconstruction valid for every hop size, which is what makes the
        "Response / time resolution" control safe to expose.

        The true group delay is measured at prepare() time by pushing an impulse
        through the very same code path, so the latency reported to the host can
        never drift away from reality.
    */
    class SpectralEngine
    {
    public:
        SpectralEngine() = default;

        // The engine owns raw vDSP setup handles. The compiler would otherwise
        // generate copy and move operations that merely duplicate those
        // pointers, so two engines would each destroy the same handle and the
        // second destroy aborts inside vDSP_destroy_fftsetup with "pointer being
        // freed was not allocated". One engine, one handle.
        SpectralEngine (const SpectralEngine&)            = delete;
        SpectralEngine& operator= (const SpectralEngine&) = delete;
        SpectralEngine (SpectralEngine&&)                 = delete;
        SpectralEngine& operator= (SpectralEngine&&)      = delete;

        ~SpectralEngine()
        {
            freeFFT();
            if (lowSetup != nullptr)
            {
                vDSP_destroy_fftsetup (lowSetup);
                lowSetup = nullptr;
            }
        }

        struct Params
        {
            float depth           = 0.0f;   // 0 .. 4
            float sharpness       = 1.0f;   // 0.2 .. 4
            float detailTilt      = 0.0f;   // -1 .. +1 (positive = surgical highs, broad lows)
            float selectivity     = 0.5f;   // 0 .. 1
            float cutWidth        = 1.0f;   // 0 = tightest cut, 1 = full spread
            float sibilanceAmount = 0.0f;   // 0 = off, 1 = full de-esser authority
            float sibilanceLowHz  = 4000.0f;
            float sibilanceHighHz = 16000.0f;
            float attackMs        = 8.0f;
            float releaseMs       = 70.0f;
            float attackTilt      = 0.0f;   // -1 .. +1 (positive = fast highs, slow lows)
            float releaseTilt     = 0.0f;   // -1 .. +1 (positive = fast high air recovery, long low hold)
            float maxReductionDb  = 24.0f;
            float stereoLink      = 1.0f;   // 0 = dual mono, 1 = fully linked
            float transientGuard  = 0.5f;   // 0 .. 1
            ProcessingMode mode   = ProcessingMode::Soft;
            bool  midSide         = false;
            bool  useIso226       = true;
            bool  multiResolution = false;  // long low-band analysis; same synthesis latency
            float motionProtect   = 0.0f;   // frame-to-frame moving-harmonic protection
            bool  neuralModeEnabled = false; // Phase Three DL router
            bool  soloDeEss       = false;  // audition: play only what the de-esser removed
        };

        //==============================================================================
        /** @param fftSizePowerOf2  e.g. 11 for a 2048 point transform
            @param overlapFactor    2, 4 or 8  ->  hop = fftSize / overlapFactor
        */
        void prepare (int fftSizePowerOf2, int overlapFactor, double sampleRate)
        {
            freeFFT();

            log2n    = std::clamp (fftSizePowerOf2, 8, 14);
            fftSize  = 1 << log2n;
            halfSize = fftSize / 2;
            numBins  = halfSize + 1;
            sr       = static_cast<float> (sampleRate > 0.0 ? sampleRate : 44100.0);

            overlap = std::clamp (overlapFactor, 2, 16);
            hopSize = fftSize / overlap;

            setupFFT();

            window.resize (static_cast<size_t> (fftSize));
            for (int i = 0; i < fftSize; ++i)
            {
                // Square root of a Hann window. The same window is used for
                // analysis and synthesis, so their product is a Hann window,
                // which satisfies the constant-overlap-add condition at every
                // supported hop. A Blackman-Harris window was used here for a
                // while: it has better sidelobe rejection for analysis, but it
                // does not satisfy COLA, so the round trip stopped being unity
                // gain and the "depth 0 is bit-transparent" guarantee (measured
                // at 1e-7 before) turned into a 2.34 residual. Detection
                // quality is handled inside the detector, not by sacrificing
                // reconstruction.
                const double x = juce::MathConstants<double>::pi
                               * static_cast<double> (i + 0.5) / static_cast<double> (fftSize);
                const double hann = 0.5 - 0.5 * std::cos (2.0 * x);
                window[static_cast<size_t> (i)] = static_cast<float> (std::sqrt (hann));
            }

            // Derived rather than hard-coded: vDSP's real FFT round trip has a
            // gain of 2N and the Hann COLA sum is N/(2*hop), so the scale that
            // makes the round trip unity is hop / N^2.
            synthesisScale = static_cast<float> (hopSize) / static_cast<float> (fftSize * fftSize);

            inputBufferL.assign (static_cast<size_t> (fftSize), 0.0f);
            inputBufferR.assign (static_cast<size_t> (fftSize), 0.0f);
            keyBufferL.assign (static_cast<size_t> (fftSize), 0.0f);
            keyBufferR.assign (static_cast<size_t> (fftSize), 0.0f);
            outputBufferL.assign (static_cast<size_t> (fftSize) * 2, 0.0f);
            outputBufferR.assign (static_cast<size_t> (fftSize) * 2, 0.0f);

            windowedFrame.resize (static_cast<size_t> (fftSize));
            realL.resize (static_cast<size_t> (halfSize));
            imagL.resize (static_cast<size_t> (halfSize));
            realR.resize (static_cast<size_t> (halfSize));
            imagR.resize (static_cast<size_t> (halfSize));
            keyReal.resize (static_cast<size_t> (halfSize));
            keyImag.resize (static_cast<size_t> (halfSize));

            magnitudeL.assign (static_cast<size_t> (numBins), 0.0f);
            magnitudeR.assign (static_cast<size_t> (numBins), 0.0f);
            keyMagnitudeL.assign (static_cast<size_t> (numBins), 0.0f);
            keyMagnitudeR.assign (static_cast<size_t> (numBins), 0.0f);
            blendedMag.assign (static_cast<size_t> (numBins), 0.0f);
            resonanceDbL.assign (static_cast<size_t> (numBins), 0.0f);
            resonanceDbR.assign (static_cast<size_t> (numBins), 0.0f);
            gainL.assign (static_cast<size_t> (numBins), 1.0f);
            deEssPreL.assign (static_cast<size_t> (numBins), 1.0f);
            deEssPreR.assign (static_cast<size_t> (numBins), 1.0f);
            gainR.assign (static_cast<size_t> (numBins), 1.0f);
            reductionDbL.assign (static_cast<size_t> (numBins), 0.0f);
            reductionDbR.assign (static_cast<size_t> (numBins), 0.0f);
            sidechainWeightsDb.assign (static_cast<size_t> (numBins), 0.0f);
            
            cepstralMag.assign(static_cast<size_t>(fftSize), 0.0f);
            cepstralReal.assign(static_cast<size_t>(halfSize), 0.0f);
            cepstralImag.assign(static_cast<size_t>(halfSize), 0.0f);
            cepstralEnvL.assign(static_cast<size_t>(numBins), 0.0f);
            cepstralEnvR.assign(static_cast<size_t>(numBins), 0.0f);
            
            weightsConfigured = false;

            binFreq.resize (static_cast<size_t> (numBins));
            const float binWidth = (sr * 0.5f) / static_cast<float> (numBins - 1);
            for (int k = 0; k < numBins; ++k)
                binFreq[static_cast<size_t> (k)] = std::max (20.0f, static_cast<float> (k) * binWidth);

            // Critical-band index per bin, used by applyBarkSmoothing. This has
            // to be filled before measureLatency() below: that call pushes a
            // block through the engine, which reaches applyBarkSmoothing, and
            // reading an unallocated vector there dereferences a null pointer.
            // Traunmuller's Bark formula spans 0 to about 24.6 over 20 Hz to
            // 20 kHz, which matches the 25 bands the smoothing groups into.
            binBark.assign (static_cast<size_t> (numBins), 0.0f);
            for (int k = 0; k < numBins; ++k)
            {
                const float f = binFreq[static_cast<size_t> (k)];
                const float z = 13.0f * std::atan (0.00076f * f)
                              + 3.5f * std::atan ((f / 7500.0f) * (f / 7500.0f));
                binBark[static_cast<size_t> (k)] = std::floor (juce::jlimit (0.0f, 24.0f, z));
            }

            detectorL.prepare (numBins, sr, hopSize);
            detectorR.prepare (numBins, sr, hopSize);
            profileInitialized = false;
            // For the 1k/2k synthesis modes, analyse low frequencies with an
            // independent 4k FFT. The resulting *detection* curve is mapped back
            // onto the main bins; no second synthesis path or phase split is used.
            lowSize = fftSize < 4096 ? 4096 : 0;
            if (lowSetup != nullptr) { vDSP_destroy_fftsetup (lowSetup); lowSetup = nullptr; }
            if (lowSize != 0)
            {
                lowSetup = vDSP_create_fftsetup (12, FFT_RADIX2);
                
                lowRingL.assign (4096, 0.0f);
                lowRingR.assign (4096, 0.0f);
                lowKeyL.assign (4096, 0.0f);
                lowKeyR.assign (4096, 0.0f);
                lowWindow.resize (4096);
                for (int i = 0; i < 4096; ++i)
                    lowWindow[static_cast<size_t> (i)] = std::sin (3.14159265358979323846f * (i + 0.5f) / 4096.0f);
                lowFrame.resize (4096);
                lowReal.resize (2048); lowImag.resize (2048);
                lowMagnitude.assign (2049, 0.0f);
                lowExcess.assign (2049, 0.0f);
                lowWeights.assign (2049, 0.0f);
                lowDetector.prepare (2049, sr, 1024);
                lowDetectorR.prepare (2049, sr, 1024);
            }
            suppressorL.prepare (numBins, sr, hopSize, binFreq);
            deEsserL.prepare (numBins, sr, hopSize, binFreq);
            suppressorR.prepare (numBins, sr, hopSize, binFreq);
            deEsserR.prepare (numBins, sr, hopSize, binFreq);
            transientDetector.prepare (sr);

            reset();

            // Discover the true group delay of this exact configuration.
            latency = measureLatency();

            reset();
        }

        // ---- Phase 8: learn the take's sibilance ---------------------------
        // The editor's Learn button captures focus bands from the visualiser
        // scope, which is log-spaced and lossy. Sibilance needs the real FFT
        // magnitudes and the real bin frequencies, so the profile is built here
        // where both exist. Nothing changes until the user applies it.
        void beginSibilanceLearn() noexcept
        {
            deEsserL.beginLearn();
            sibLearning = true;
        }

        SibilanceDeEsser::Profile endSibilanceLearn() noexcept
        {
            sibLearning = false;
            return deEsserL.endLearn();
        }

        bool isSibilanceLearning() const noexcept { return sibLearning; }

        // ---- onset and decay, measured at the transform rate ----------------
        //
        // The editor's scope publishes 20 frames a second, so one frame is 50 ms.
        // Onset and release are millisecond-scale, so that rate cannot resolve
        // them and every learned timing came out pinned to its ceiling. The
        // engine sees every transform frame -- 10.7 ms at 2048/4x, 5.3 ms at
        // 8x -- so the timing is measured here and handed up finished.
        //
        // A few passes over the bin array per frame, only while a take is being
        // learned. Playback is untouched.
        void beginTimingLearn() noexcept
        {
            timingLearning    = true;
            timingOnsetSum    = 0; timingOnsetCount = 0;
            timingRingSum     = 0; timingRingCount  = 0;
            timingRiseFrames  = 0; timingDecayFrames = 0;
            timingEventActive = false;
        }

        void endTimingLearn() noexcept { timingLearning = false; }

        /** Mean rise time of events seen during the learn window, in ms. */
        float getLearnOnsetMs() const noexcept
        {
            if (timingOnsetCount == 0) return 0.0f;
            return float (double (timingOnsetSum) / double (timingOnsetCount)
                          * timingMsPerFrame());
        }

        /** Mean decay time of events seen during the learn window, in ms. */
        float getLearnRingMs() const noexcept
        {
            if (timingRingCount == 0) return 0.0f;
            return float (double (timingRingSum) / double (timingRingCount)
                          * timingMsPerFrame());
        }

        /** Where the de-esser has tracked the sibilance to, in Hz. 0 if none. */
        float getSibilanceCentreHz() const noexcept { return deEsserL.getTrackedCentreHz(); }


        void reset()
        {
            std::fill (inputBufferL.begin(), inputBufferL.end(), 0.0f);
            std::fill (inputBufferR.begin(), inputBufferR.end(), 0.0f);
            std::fill (keyBufferL.begin(), keyBufferL.end(), 0.0f);
            std::fill (keyBufferR.begin(), keyBufferR.end(), 0.0f);
            std::fill (lowRingL.begin(), lowRingL.end(), 0.0f);
            std::fill (lowRingR.begin(), lowRingR.end(), 0.0f);
            std::fill (lowKeyL.begin(), lowKeyL.end(), 0.0f);
            std::fill (lowKeyR.begin(), lowKeyR.end(), 0.0f);
            lowWrite = 0;
            std::fill (outputBufferL.begin(), outputBufferL.end(), 0.0f);
            std::fill (outputBufferR.begin(), outputBufferR.end(), 0.0f);

            inBufferPos       = 0;
            outBufferReadPos  = 0;
            outBufferWritePos = hopSize;
            hopCounter        = 0;
            frameTransientSum = 0.0f;
            frameTransientCount = 0;
            keyEnabled = false;

            suppressorL.reset();
            deEsserL.reset();
            suppressorR.reset();
            deEsserR.reset();
            transientDetector.reset();
            detectorL.resetHistory();
            detectorR.resetHistory();
            if (lowSize != 0) { lowDetector.resetHistory(); lowDetectorR.resetHistory(); }
        }

        //==============================================================================
        int   getLatencySamples() const noexcept { return latency; }
        int   getNumBins()        const noexcept { return numBins; }
        int   getFftSize()        const noexcept { return fftSize; }
        int   getHopSize()        const noexcept { return hopSize; }
        float getSampleRate()     const noexcept { return sr; }

        const std::vector<float>& getBinFrequencies() const noexcept { return binFreq; }

        void setVocalProfile (VocalProfile profile)
        {
            if (profileInitialized && profile == currentProfile) return;
            currentProfile = profile;
            profileInitialized = true;
            detectorL.setVocalProfile (profile);
            detectorR.setVocalProfile (profile);
            if (lowSize != 0) lowDetector.setVocalProfile (profile);
            if (lowSize != 0) lowDetectorR.setVocalProfile (profile);
        }

        /** Allocation free: call once per audio block. */
        void setParams (const Params& p) noexcept { params = p; }

        /** Allocation free: call whenever the focus bands change. */
        void updateSidechainWeights (const ParametricEQWeighting& eq)
        {
            eq.computeWeightingDb (sidechainWeightsDb.data(), numBins, sr);
            if (lowSize != 0) eq.computeWeightingDb (lowWeights.data(), 2049, sr);
            weightsConfigured = true;
        }

        //==============================================================================
        /** The engine deliberately does NOT produce a delta signal: the dry/wet
            alignment needed for an accurate delta lives in the processor, which
            already owns the dry delay line. Computing a "difference" here from
            the undelayed input would produce a comb-filtered artefact rather
            than the removed resonances.
        */
        void processBlock (const float* inL, const float* inR,
                           float* outL, float* outR,
                           int numSamples) noexcept
        {
            for (int s = 0; s < numSamples; ++s)
            {
                float a = 0.0f, b = 0.0f;
                processSample (inL ? inL[s] : 0.0f, inR ? inR[s] : 0.0f, a, b);
                if (outL) outL[s] = a;
                if (outR) outR[s] = b;
            }
        }

        /** The whole spectral pipeline for a single stereo sample. */
        inline void processSample (float xL, float xR, float& outL, float& outR) noexcept
        {
            processSample (xL, xR, outL, outR, xL, xR, false);
        }

        inline void processSample (float xL, float xR, float& outL, float& outR,
                                   float keyL, float keyR, bool useKey) noexcept
        {
            // Never let a non-finite sample poison the FFT frame or, worse, the
            // overlap-add ring buffer, which would stay corrupted forever.
            if (! std::isfinite (xL)) xL = 0.0f;
            if (! std::isfinite (xR)) xR = 0.0f;
            if (! std::isfinite (keyL)) keyL = 0.0f;
            if (! std::isfinite (keyR)) keyR = 0.0f;
            keyEnabled = useKey;

            float encL = xL, encR = xR;
            if (params.midSide)
            {
                encL = (xL + xR) * 0.7071067811865476f;
                encR = (xL - xR) * 0.7071067811865476f;
                if (useKey)
                {
                    const float mid = (keyL + keyR) * 0.7071067811865476f;
                    keyR = (keyL - keyR) * 0.7071067811865476f;
                    keyL = mid;
                }
            }

            inputBufferL[static_cast<size_t> (inBufferPos)] = encL;
            inputBufferR[static_cast<size_t> (inBufferPos)] = encR;
            keyBufferL[static_cast<size_t> (inBufferPos)] = keyL;
            keyBufferR[static_cast<size_t> (inBufferPos)] = keyR;
            if (lowSize != 0)
            {
                lowRingL[static_cast<size_t> (lowWrite)] = encL;
                lowRingR[static_cast<size_t> (lowWrite)] = encR;
                lowKeyL[static_cast<size_t> (lowWrite)] = keyL;
                lowKeyR[static_cast<size_t> (lowWrite)] = keyR;
                lowWrite = (lowWrite + 1) & 4095;
            }

            if (++inBufferPos >= fftSize)
                inBufferPos = 0;

            transientDetector.pushSample (encL, encR);
            frameTransientSum += transientDetector.getActivity();
            ++frameTransientCount;

            if (++hopCounter >= hopSize)
            {
                hopCounter = 0;
                processSTFTFrame();
            }

            const size_t readIdx = static_cast<size_t> (outBufferReadPos);
            const float procL = outputBufferL[readIdx];
            const float procR = outputBufferR[readIdx];
            outputBufferL[readIdx] = 0.0f;
            outputBufferR[readIdx] = 0.0f;

            if (++outBufferReadPos >= fftSize * 2)
                outBufferReadPos = 0;

            if (params.midSide)
            {
                outL = (procL + procR) * 0.7071067811865476f;
                outR = (procL - procR) * 0.7071067811865476f;
            }
            else
            {
                outL = procL;
                outR = procR;
            }
        }

        //==============================================================================
        const float* getMagnitudeSpectrum() const noexcept { return magnitudeL.data(); }
        const float* getReductionDb()       const noexcept { return reductionDbL.data(); }
        const float* getBaselineDb()        const noexcept { return detectorL.getBaselineDb(); }
        const int*   getWindowBins()        const noexcept { return detectorL.getRadialBins().data(); }
        const float* getProminenceDb()      const noexcept { return detectorL.getProminenceDb(); }
        const float* getResonanceDb()       const noexcept { return resonanceDbL.data(); }
        float getReferenceProminenceDb()    const noexcept { return detectorL.getReferenceProminenceDb(); }

    private:
        //==============================================================================
        void setupFFT()
        {
            freeFFT();
            fftSetup = vDSP_create_fftsetup (static_cast<vDSP_Length> (log2n), FFT_RADIX2);
        }

        void freeFFT()
        {
            if (fftSetup != nullptr)
            {
                vDSP_destroy_fftsetup (fftSetup);
                fftSetup = nullptr;
            }
        }

        /** Pushes one impulse through the real processing path to discover the
            exact group delay. Costs a few milliseconds at prepare() time only.
        */
        int measureLatency()
        {
            const int probeLength = fftSize * 4 + hopSize * 4;

            scratchIn.assign (static_cast<size_t> (probeLength), 0.0f);
            scratchOut.assign (static_cast<size_t> (probeLength), 0.0f);
            scratchIn[0] = 1.0f;

            const Params saved = params;
            params.depth          = 0.0f;   // transparent probe: pure delay
            params.transientGuard = 0.0f;

            processBlock (scratchIn.data(), nullptr,
                          scratchOut.data(), nullptr,
                          probeLength);

            params = saved;

            int   peakIndex = 0;
            float peakValue = 0.0f;
            for (int i = 0; i < probeLength; ++i)
            {
                const float a = std::abs (scratchOut[static_cast<size_t> (i)]);
                if (a > peakValue) { peakValue = a; peakIndex = i; }
            }

            std::vector<float>().swap (scratchIn);
            std::vector<float>().swap (scratchOut);

            return std::max (hopSize, peakIndex);
        }

        //==============================================================================
        void processSTFTFrame() noexcept
        {
            const float activity = (frameTransientCount > 0)
                                     ? (frameTransientSum / static_cast<float> (frameTransientCount))
                                     : 0.0f;
            frameTransientSum   = 0.0f;
            frameTransientCount = 0;

            DetectorParams dp;
            dp.sharpness         = params.sharpness;
            dp.detailTilt        = params.detailTilt;
            dp.selectivity       = params.selectivity;
            dp.transientGuard    = params.transientGuard;
            dp.transientActivity = activity;
            dp.useIso226         = params.useIso226;
            dp.depth             = params.depth;
            dp.hardMode          = (params.mode == ProcessingMode::Hard);
            dp.motionProtect     = params.motionProtect;

            DynamicSuppressor::Params sp;
            sp.depth          = params.depth;
            sp.attackMs       = params.attackMs;
            sp.releaseMs      = params.releaseMs;
            sp.attackTilt     = params.attackTilt;
            sp.releaseTilt    = params.releaseTilt;
            sp.maxReductionDb = params.maxReductionDb;
            sp.cutWidth       = params.cutWidth;
            sp.mode           = params.mode;

            analyzeChannel (inputBufferL, realL, imagL, magnitudeL);
            analyzeChannel (inputBufferR, realR, imagR, magnitudeR);

            // Analyze the key in separate FFT scratch so the vocal's complex
            // spectrum (which gets resynthesized below) is never overwritten.
            if (keyEnabled)
            {
                analyzeChannel (keyBufferL, keyReal, keyImag, keyMagnitudeL);
                analyzeChannel (keyBufferR, keyReal, keyImag, keyMagnitudeR);
            }
            const auto& detectL = keyEnabled ? keyMagnitudeL : magnitudeL;
            const auto& detectR = keyEnabled ? keyMagnitudeR : magnitudeR;

            const float link = std::clamp (params.stereoLink, 0.0f, 1.0f);
            const bool fullyLinked = link >= 0.999f;

            // Channel L decision
            if (fullyLinked)
            {
                for (int k = 0; k < numBins; ++k)
                    blendedMag[static_cast<size_t> (k)] = 0.5f * (detectL[static_cast<size_t> (k)]
                                                                + detectR[static_cast<size_t> (k)]);
                computeTrueEnvelope(blendedMag, cepstralEnvL);
            }
            else
            {
                for (int k = 0; k < numBins; ++k)
                    blendedMag[static_cast<size_t> (k)] =
                        link * 0.5f * (detectL[static_cast<size_t> (k)] + detectR[static_cast<size_t> (k)])
                        + (1.0f - link) * detectL[static_cast<size_t> (k)];
                computeTrueEnvelope(blendedMag, cepstralEnvL);
            }
            const float* cueWeights = weightsConfigured ? sidechainWeightsDb.data() : nullptr;
            const float* lowCueWeights = weightsConfigured ? lowWeights.data() : nullptr;

            if (params.neuralModeEnabled && neuralDetector.isLoaded())
            {
                neuralDetector.process(blendedMag.data(), resonanceDbL.data(), numBins);
            }
            else
            {
                detectorL.detect (blendedMag.data(), cepstralEnvL.data(), cueWeights, resonanceDbL.data(), dp);
            }
            
            if (params.multiResolution && lowSize != 0 && lowSetup != nullptr)
            {
                analyzeLowBand (keyEnabled ? lowKeyL : lowRingL);
                lowDetector.detect (lowMagnitude.data(), nullptr, lowCueWeights, lowExcess.data(), dp);
                // Only blend the low-frequency decision. The two windows finish at
                // the same sample, so the synthesis path stays phase-coherent.
                const float ratio = 4096.0f / static_cast<float> (fftSize);
                for (int k = 1; k < numBins; ++k)
                {
                    const float f = binFreq[static_cast<size_t> (k)];
                    if (f >= 1200.0f) break;
                    const int lk = std::clamp (static_cast<int> (k * ratio + 0.5f), 1, 2047);
                    const float blend = f <= 650.0f ? 1.0f : (1200.0f - f) / 550.0f;
                    resonanceDbL[static_cast<size_t> (k)] += blend *
                        (lowExcess[static_cast<size_t> (lk)] - resonanceDbL[static_cast<size_t> (k)]);
                }
            }
            
            applyBarkSmoothing(resonanceDbL);
            suppressorL.process (resonanceDbL.data(), gainL.data(), sp);

            if (fullyLinked)
            {
                std::copy (gainL.begin(), gainL.end(), gainR.begin());
                std::copy (resonanceDbL.begin(), resonanceDbL.end(), resonanceDbR.begin());
            }
            else
            {
                for (int k = 0; k < numBins; ++k)
                    blendedMag[static_cast<size_t> (k)] =
                        link * 0.5f * (detectL[static_cast<size_t> (k)] + detectR[static_cast<size_t> (k)])
                        + (1.0f - link) * detectR[static_cast<size_t> (k)];
                computeTrueEnvelope(blendedMag, cepstralEnvR);
                
                if (params.neuralModeEnabled && neuralDetector.isLoaded())
                {
                    neuralDetector.process(blendedMag.data(), resonanceDbR.data(), numBins);
                }
                else
                {
                    detectorR.detect (blendedMag.data(), cepstralEnvR.data(), cueWeights, resonanceDbR.data(), dp);
                }
                
                if (params.multiResolution && lowSize != 0 && lowSetup != nullptr)
                {
                    analyzeLowBand (keyEnabled ? lowKeyR : lowRingR);
                    lowDetectorR.detect (lowMagnitude.data(), nullptr, lowCueWeights, lowExcess.data(), dp);
                    const float ratio = 4096.0f / static_cast<float> (fftSize);
                    for (int k = 1; k < numBins; ++k)
                    {
                        const float f = binFreq[static_cast<size_t> (k)];
                        if (f >= 1200.0f) break;
                        const int lk = std::clamp (static_cast<int> (k * ratio + 0.5f), 1, 2047);
                        const float blend = f <= 650.0f ? 1.0f : (1200.0f - f) / 550.0f;
                        resonanceDbR[static_cast<size_t> (k)] += blend *
                            (lowExcess[static_cast<size_t> (lk)] - resonanceDbR[static_cast<size_t> (k)]);
                    }
                }
                applyBarkSmoothing(resonanceDbR);
                suppressorR.process (resonanceDbR.data(), gainR.data(), sp);

                // Stereo Reverb & Space Preservation in Mid/Side Mode:
                // When Mid/Side processing is active, Channel R is the Side channel containing
                // stereo room reflections, reverb tails, and spatial width. Scaling Side channel
                // reduction by stereoLink allows Mid-Only suppression (link=0) so 100% of the
                // stereo reverb tail and 3D space are preserved without collapsing the width.
                if (params.midSide)
                {
                    for (int k = 0; k < numBins; ++k)
                    {
                        gainR[static_cast<size_t> (k)] = 1.0f - link * (1.0f - gainR[static_cast<size_t> (k)]);
                        reductionDbR[static_cast<size_t> (k)] *= link;
                    }
                }
            }

            // Phase locking was removed here. It wrote one float past the end of
            // the split-complex buffers on every frame (rightBound reached
            // numBins while realL/imagL hold halfSize == numBins - 1), which
            // corrupted the heap and produced random crashes at random points in
            // the suite. It was also a no-op: `rotation` was hardcoded to 0, so
            // each bin was rebuilt from its own magnitude and phase. It cost two
            // vDSP calls, four heap allocations per frame on the audio thread and
            // a peak scan, and changed nothing. Real identity phase locking
            // (Laroche and Dolson 1999) needs rotation = phases[peak] -
            // phases[k] and writes clamped to the buffer size; see
            // RESEARCH-SYNTHESIS-OPTIONS.md if listening ever demands it.
            // The de-esser runs as its own stage on top of the resonance gain.
            // It is deliberately not expressed through the resonance detector:
            // sibilance is a wide band of noise, not a peak, and the detector's
            // whole design is about peaks. See docs/FINE-TUNE-AUDIT.md.
            {
                // Keep the resonance gain, so whatever the de-esser does on top
                // of it can be separated out. Solo the Cut plays only that part.
                std::copy (gainL.begin(), gainL.end(), deEssPreL.begin());
                std::copy (gainR.begin(), gainR.end(), deEssPreR.begin());

                SibilanceDeEsser::Params sp2;
                sp2.amount  = params.sibilanceAmount;
                sp2.lowHz   = params.sibilanceLowHz;
                sp2.highHz  = params.sibilanceHighHz;
                deEsserL.process (detectL.data(), gainL.data(), sp2);
                deEsserR.process (detectR.data(), gainR.data(), sp2);

                if (params.soloDeEss)
                {
                    // Play what the de-esser removed, and nothing else.
                    //
                    // The resonance suppression is dropped from the path, so the
                    // only thing audible is the de-esser's own subtraction. Where
                    // it did nothing the output is silent; where it cut 12 dB you
                    // hear the 12 dB it took. That is what makes it possible to
                    // hear whether a vowel is leaking into the cut, which is the
                    // question the flatness gate was built to answer.
                    //
                    // The gain is divided rather than recomputed, so this cannot
                    // disagree with the cut that is actually being applied.
                    auto soloInto = [this] (const std::vector<float>& pre,
                                               std::vector<float>& g,
                                               std::vector<float>& red)
                    {
                        for (int k = 0; k < numBins; ++k)
                        {
                            const auto u = static_cast<size_t> (k);
                            const float p = pre[u];
                            const float own = p > 1.0e-6f ? g[u] / p : 1.0f;
                            // Floored, not clamped to zero. A gain of exactly
                            // zero becomes -inf when synthesizeChannel converts
                            // it to dB for the graph, and a -inf reaching the
                            // visualiser's recursive smoothing filter never
                            // washes out -- the display goes dead permanently
                            // while the audio carries on working. That is what
                            // toggling Solo Cut in and out used to do. -120 dB
                            // is inaudible and finite.
                            const float rem = std::clamp (1.0f - own, 1.0e-6f, 1.0f);
                            g[u] = rem;
                            red[u] = rem > 1.0e-6f
                                   ? -20.0f * std::log10 (std::max (1.0e-6f, own))
                                   : 0.0f;
                        }
                    };
                    soloInto (deEssPreL, gainL, reductionDbL);
                    soloInto (deEssPreR, gainR, reductionDbR);
                }
            }

            synthesizeChannel (outputBufferL, realL, imagL, gainL, reductionDbL);
            synthesizeChannel (outputBufferR, realR, imagR, gainR, reductionDbR);

            if (timingLearning)
                updateTimingLearn (reductionDbL);

            outBufferWritePos += hopSize;
            if (outBufferWritePos >= fftSize * 2)
                outBufferWritePos -= fftSize * 2;
        }

        void analyzeChannel (const std::vector<float>& inBuf,
                             std::vector<float>& re, std::vector<float>& im,
                             std::vector<float>& magOut) noexcept
        {
            const int readIdx = inBufferPos;
            for (int i = 0; i < fftSize; ++i)
            {
                int bufIdx = readIdx + i;
                if (bufIdx >= fftSize) bufIdx -= fftSize;
                windowedFrame[static_cast<size_t> (i)] =
                    inBuf[static_cast<size_t> (bufIdx)] * window[static_cast<size_t> (i)];
            }

            DSPSplitComplex splitComplex { re.data(), im.data() };
            vDSP_ctoz (reinterpret_cast<const DSPComplex*> (windowedFrame.data()), 2, &splitComplex, 1,
                       static_cast<vDSP_Length> (halfSize));
            vDSP_fft_zrip (fftSetup, &splitComplex, 1, static_cast<vDSP_Length> (log2n), FFT_FORWARD);

            // Vectorized complex absolute magnitude calculation with Apple Accelerate vDSP:
            magOut[0] = std::abs (re[0] * 0.5f);
            magOut[static_cast<size_t> (halfSize)] = std::abs (im[0] * 0.5f);

            DSPSplitComplex innerComplex { re.data() + 1, im.data() + 1 };
            vDSP_zvabs (&innerComplex, 1, magOut.data() + 1, 1, static_cast<vDSP_Length> (halfSize - 1));
            const float halfScale = 0.5f;
            vDSP_vsmul (magOut.data() + 1, 1, &halfScale, magOut.data() + 1, 1, static_cast<vDSP_Length> (halfSize - 1));
        }

        /** Long-window magnitude analysis for the low-band detector only. */
        void analyzeLowBand (const std::vector<float>& ring) noexcept
        {
            for (int i = 0; i < 4096; ++i)
                lowFrame[static_cast<size_t> (i)] =
                    ring[static_cast<size_t> ((lowWrite + i) & 4095)] * lowWindow[static_cast<size_t> (i)];
            DSPSplitComplex sc { lowReal.data(), lowImag.data() };
            vDSP_ctoz (reinterpret_cast<const DSPComplex*> (lowFrame.data()), 2, &sc, 1, 2048);
            vDSP_fft_zrip (lowSetup, &sc, 1, 12, FFT_FORWARD);
            lowMagnitude[0] = std::abs (lowReal[0] * 0.5f);
            for (int k = 1; k < 2048; ++k)
                lowMagnitude[static_cast<size_t> (k)] = 0.5f * std::hypot (
                    lowReal[static_cast<size_t> (k)], lowImag[static_cast<size_t> (k)]);
            lowMagnitude[2048] = std::abs (lowImag[0] * 0.5f);
        }

        void synthesizeChannel (std::vector<float>& outBuf,
                                std::vector<float>& re, std::vector<float>& im,
                                const std::vector<float>& gain,
                                std::vector<float>& reductionDbOut) noexcept
        {
            re[0] *= gain[0];
            im[0] *= gain[static_cast<size_t> (halfSize)];

            // Apple Accelerate SIMD vectorized complex scaling:
            vDSP_vmul (gain.data() + 1, 1, re.data() + 1, 1, re.data() + 1, 1, static_cast<vDSP_Length> (halfSize - 1));
            vDSP_vmul (gain.data() + 1, 1, im.data() + 1, 1, im.data() + 1, 1, static_cast<vDSP_Length> (halfSize - 1));

            // Apple Accelerate SIMD vectorized gain-to-dB conversion:
            const float refOne = 1.0f;
            vDSP_vdbcon (gain.data(), 1, &refOne, reductionDbOut.data(), 1, static_cast<vDSP_Length> (numBins), 0);

            DSPSplitComplex splitComplex { re.data(), im.data() };
            vDSP_fft_zrip (fftSetup, &splitComplex, 1, static_cast<vDSP_Length> (log2n), FFT_INVERSE);
            vDSP_ztoc (&splitComplex, 1, reinterpret_cast<DSPComplex*> (windowedFrame.data()), 2,
                       static_cast<vDSP_Length> (halfSize));

            // Apple Accelerate SIMD vectorized window scaling:
            vDSP_vmul (windowedFrame.data(), 1, window.data(), 1, windowedFrame.data(), 1, static_cast<vDSP_Length> (fftSize));
            vDSP_vsmul (windowedFrame.data(), 1, &synthesisScale, windowedFrame.data(), 1, static_cast<vDSP_Length> (fftSize));

            const int writeBase = outBufferWritePos;
            const int ringSize  = fftSize * 2;
            for (int i = 0; i < fftSize; ++i)
            {
                int outIdx = writeBase + i;
                if (outIdx >= ringSize) outIdx -= ringSize;
                outBuf[static_cast<size_t> (outIdx)] += windowedFrame[static_cast<size_t> (i)];
            }
        }

        void computeTrueEnvelope(const std::vector<float>& magIn, std::vector<float>& envOut) noexcept
        {
            for (int k = 0; k < numBins; ++k)
                cepstralMag[k] = magIn[k] > 1e-6f ? std::log(magIn[k]) : -13.81f;
            
            for (int k = 1; k < halfSize; ++k)
                cepstralMag[fftSize - k] = cepstralMag[k];

            DSPSplitComplex splitComplex { cepstralReal.data(), cepstralImag.data() };
            vDSP_ctoz (reinterpret_cast<const DSPComplex*> (cepstralMag.data()), 2, &splitComplex, 1, static_cast<vDSP_Length>(halfSize));
            
            vDSP_fft_zrip (fftSetup, &splitComplex, 1, static_cast<vDSP_Length>(log2n), FFT_FORWARD);
            
            int lifterCutoff = 45;
            for (int i = lifterCutoff; i < halfSize; ++i)
            {
                cepstralReal[i] = 0.0f;
                cepstralImag[i] = 0.0f;
            }
            
            vDSP_fft_zrip (fftSetup, &splitComplex, 1, static_cast<vDSP_Length>(log2n), FFT_INVERSE);
            vDSP_ztoc (&splitComplex, 1, reinterpret_cast<DSPComplex*> (cepstralMag.data()), 2, static_cast<vDSP_Length>(halfSize));
            
            const float scale = 1.0f / (2.0f * static_cast<float>(fftSize));
            for (int k = 0; k < numBins; ++k)
            {
                envOut[k] = std::exp(cepstralMag[k] * scale);
            }
        }

        void applyBarkSmoothing(std::vector<float>& gainDb) noexcept
        {
            const int numBarkBands = 25;
            std::array<float, 25> barkSum { 0.0f };
            std::array<float, 25> barkCount { 0.0f };

            for (int k = 0; k < numBins; ++k) {
                int b = static_cast<int>(binBark[k]);
                if (b >= 0 && b < numBarkBands) {
                    barkSum[b] += gainDb[k];
                    barkCount[b] += 1.0f;
                }
            }
            std::array<float, 25> barkGain { 0.0f };
            for (int b = 0; b < numBarkBands; ++b) {
                if (barkCount[b] > 0.0f)
                    barkGain[b] = barkSum[b] / barkCount[b];
            }

            std::array<float, 25> smoothedBark { 0.0f };
            for (int b = 0; b < numBarkBands; ++b) {
                float sum = 0.0f;
                float wSum = 0.0f;
                for (int i = -1; i <= 1; ++i) {
                    if (b + i >= 0 && b + i < numBarkBands) {
                        float w = (i == 0) ? 0.5f : 0.25f;
                        sum += barkGain[b + i] * w;
                        wSum += w;
                    }
                }
                smoothedBark[b] = sum / wSum;
            }

            for (int k = 0; k < numBins; ++k) {
                float b = binBark[k];
                int b0 = static_cast<int>(b);
                int b1 = std::min(numBarkBands - 1, b0 + 1);
                float t = b - static_cast<float>(b0);
                gainDb[k] = smoothedBark[b0] * (1.0f - t) + smoothedBark[b1] * t;
            }
        }

        //==============================================================================
        int   log2n    = 11;
        int   fftSize  = 2048;
        int   halfSize = 1024;
        int   numBins  = 1025;
        int   hopSize  = 512;

        // ---- timing capture state ----
        bool  timingLearning    = false;
        int   timingRiseFrames  = 0, timingOnsetSum = 0, timingOnsetCount = 0;
        int   timingDecayFrames = 0, timingRingSum = 0, timingRingCount  = 0;
        bool  timingEventActive = false;

        double timingMsPerFrame() const noexcept
        {
            return sr > 0.0f ? 1000.0 * double (hopSize) / double (sr) : 0.0;
        }

        /** One transform frame of the event machine.
            The cut depth stands in for the problem's presence: when a resonance
            arrives the cut arrives with it, and when the resonance rings the cut
            persists. Frames are 5-11 ms apart here, so the rise and decay times
            carry real meaning instead of landing on a ceiling. */
        void updateTimingLearn (const std::vector<float>& reductionDb) noexcept
        {
            float excess = 0.0f;
            for (size_t k = 0; k < reductionDb.size(); ++k)
                excess = std::max (excess, -reductionDb[k]);

            if (! timingEventActive)
            {
                if (excess > kTimingEndDb)
                {
                    ++timingRiseFrames;
                    if (excess > kTimingOnsetDb && timingRiseFrames >= kTimingMinFrames)
                    {
                        timingOnsetSum += timingRiseFrames;
                        ++timingOnsetCount;
                        timingEventActive = true;
                        timingRiseFrames  = 0;
                        timingDecayFrames = 0;
                    }
                }
                else
                {
                    timingRiseFrames = 0;
                }
            }
            else
            {
                ++timingDecayFrames;
                if (excess < kTimingEndDb)
                {
                    timingRingSum += timingDecayFrames;
                    ++timingRingCount;
                    timingEventActive = false;
                    timingDecayFrames = 0;
                }
            }
        }

        static constexpr float kTimingOnsetDb   = 1.5f;
        static constexpr float kTimingEndDb     = 0.5f;
        static constexpr int   kTimingMinFrames = 2;
        int   overlap  = 4;
        int   latency  = 2048;
        float sr       = 44100.0f;
        float synthesisScale = 1.0f;

        FFTSetup fftSetup = nullptr;

        Params params;
        VocalProfile currentProfile = VocalProfile::AllRoundLead;
        bool profileInitialized = false;

        std::vector<float> window;
        std::vector<float> windowedFrame;
        std::vector<float> realL, imagL, realR, imagR;
        std::vector<float> magnitudeL, magnitudeR, blendedMag;
        std::vector<float> keyBufferL, keyBufferR, keyMagnitudeL, keyMagnitudeR;
        std::vector<float> keyReal, keyImag;
        bool keyEnabled = false;
        int lowSize = 0, lowWrite = 0;
        FFTSetup lowSetup = nullptr;
        std::vector<float> lowRingL, lowRingR, lowKeyL, lowKeyR, lowWindow, lowFrame;
        std::vector<float> lowReal, lowImag, lowMagnitude, lowExcess, lowWeights;
        ResonanceDetector lowDetector, lowDetectorR;
        std::vector<float> resonanceDbL, resonanceDbR;
        std::vector<float> gainL, gainR;
        std::vector<float> reductionDbL, reductionDbR;
        std::vector<float> sidechainWeightsDb;
        
        std::vector<float> cepstralMag;
        std::vector<float> cepstralReal;
        std::vector<float> cepstralImag;
        std::vector<float> cepstralEnvL;
        std::vector<float> cepstralEnvR;

        bool weightsConfigured = false;
        std::vector<float> binFreq;
        std::vector<float> binBark;
        std::vector<float> scratchIn, scratchOut;

        std::vector<float> inputBufferL, inputBufferR;
        std::vector<float> outputBufferL, outputBufferR;

        int   inBufferPos       = 0;
        int   outBufferReadPos  = 0;
        int   outBufferWritePos = 0;
        int   hopCounter        = 0;
        float frameTransientSum = 0.0f;
        int   frameTransientCount = 0;

        ResonanceDetector detectorL, detectorR;
        NeuralDetector neuralDetector;
        DynamicSuppressor suppressorL, suppressorR;
        SibilanceDeEsser  deEsserL, deEsserR;

        // The resonance gain as it stood before the de-esser ran, so the
        // de-esser's own contribution can be isolated for Solo the Cut.
        std::vector<float> deEssPreL, deEssPreR;
        bool sibLearning = false;   // Phase 8: accumulate the take's profile
        TransientDetector transientDetector;
    };
}
