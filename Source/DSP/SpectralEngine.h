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
#include "ParametricEQWeighting.h"
#include "TransientDetector.h"

namespace ResonaPro
{
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

        ~SpectralEngine()
        {
            freeFFT();
            if (lowSetup != nullptr) vDSP_destroy_fftsetup (lowSetup);
        }

        struct Params
        {
            float depth           = 0.0f;   // 0 .. 4
            float sharpness       = 1.0f;   // 0.2 .. 4
            float detailTilt      = 0.0f;   // -1 .. +1 (positive = surgical highs, broad lows)
            float selectivity     = 0.5f;   // 0 .. 1
            float attackMs        = 8.0f;
            float releaseMs       = 70.0f;
            float attackTilt      = 0.0f;   // -1 .. +1 (positive = fast highs, slow lows)
            float releaseTilt     = 0.0f;   // -1 .. +1 (positive = fast high air recovery, long low hold)
            float maxReductionDb  = 24.0f;
            float sibilanceSmooth = 0.5f;   // 0 .. 1 (dedicated S/T consonant smoother)
            float stereoLink      = 1.0f;   // 0 = dual mono, 1 = fully linked
            float transientGuard  = 0.5f;   // 0 .. 1
            ProcessingMode mode   = ProcessingMode::Soft;
            bool  midSide         = false;
            bool  useIso226       = true;
            bool  multiResolution = false;  // long low-band analysis; same synthesis latency
            float motionProtect   = 0.0f;   // frame-to-frame moving-harmonic protection
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

            overlap = std::clamp (overlapFactor, 2, 8);
            hopSize = fftSize / overlap;

            setupFFT();

            window.resize (static_cast<size_t> (fftSize));
            for (int i = 0; i < fftSize; ++i)
                window[static_cast<size_t> (i)] =
                    std::sin (3.14159265358979323846f * (static_cast<float> (i) + 0.5f)
                              / static_cast<float> (fftSize));

            synthesisScale = static_cast<float> (hopSize)
                           / (static_cast<float> (fftSize) * static_cast<float> (fftSize));

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
            gainR.assign (static_cast<size_t> (numBins), 1.0f);
            reductionDbL.assign (static_cast<size_t> (numBins), 0.0f);
            reductionDbR.assign (static_cast<size_t> (numBins), 0.0f);
            sidechainWeightsDb.assign (static_cast<size_t> (numBins), 0.0f);

            binFreq.resize (static_cast<size_t> (numBins));
            const float binWidth = (sr * 0.5f) / static_cast<float> (numBins - 1);
            for (int k = 0; k < numBins; ++k)
                binFreq[static_cast<size_t> (k)] = std::max (20.0f, static_cast<float> (k) * binWidth);

            detectorL.prepare (numBins, sr);
            detectorR.prepare (numBins, sr);
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
                lowDetector.prepare (2049, sr);
                lowDetectorR.prepare (2049, sr);
                double sum = 0.0;
                for (float w : lowWindow) sum += w;
                lowDetector.setFullScaleReference (static_cast<float> (sum * 0.5));
                lowDetectorR.setFullScaleReference (static_cast<float> (sum * 0.5));
            }
            suppressorL.prepare (numBins, sr, hopSize, binFreq);
            suppressorR.prepare (numBins, sr, hopSize, binFreq);
            transientDetector.prepare (sr);

            // Magnitude the FFT would report for a full-scale sine at a bin centre:
            // used to express magnitudes on an approximate dBFS scale for Hard mode.
            double windowSum = 0.0;
            for (int i = 0; i < fftSize; ++i)
                windowSum += static_cast<double> (window[static_cast<size_t> (i)]);
            const float fullScaleMag = static_cast<float> (0.5 * windowSum);
            detectorL.setFullScaleReference (fullScaleMag);
            detectorR.setFullScaleReference (fullScaleMag);

            reset();

            // Discover the true group delay of this exact configuration.
            latency = measureLatency();

            reset();
        }

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
            suppressorR.reset();
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
            dp.sibilanceSmooth   = params.sibilanceSmooth;
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
            }
            else
            {
                for (int k = 0; k < numBins; ++k)
                    blendedMag[static_cast<size_t> (k)] =
                        link * 0.5f * (detectL[static_cast<size_t> (k)] + detectR[static_cast<size_t> (k)])
                        + (1.0f - link) * detectL[static_cast<size_t> (k)];
            }
            detectorL.detect (blendedMag.data(), sidechainWeightsDb.data(), resonanceDbL.data(), dp);
            if (params.multiResolution && lowSize != 0 && lowSetup != nullptr)
            {
                analyzeLowBand (keyEnabled ? lowKeyL : lowRingL);
                lowDetector.detect (lowMagnitude.data(), lowWeights.data(), lowExcess.data(), dp);
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
                detectorR.detect (blendedMag.data(), sidechainWeightsDb.data(), resonanceDbR.data(), dp);
                if (params.multiResolution && lowSize != 0 && lowSetup != nullptr)
                {
                    analyzeLowBand (keyEnabled ? lowKeyR : lowRingR);
                    lowDetectorR.detect (lowMagnitude.data(), lowWeights.data(), lowExcess.data(), dp);
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

            synthesizeChannel (outputBufferL, realL, imagL, gainL, reductionDbL);
            synthesizeChannel (outputBufferR, realR, imagR, gainR, reductionDbR);

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

        //==============================================================================
        int   log2n    = 11;
        int   fftSize  = 2048;
        int   halfSize = 1024;
        int   numBins  = 1025;
        int   hopSize  = 512;
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
        std::vector<float> binFreq;
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
        DynamicSuppressor suppressorL, suppressorR;
        TransientDetector transientDetector;
    };
}
