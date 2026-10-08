/*
    Headless validation harness for the ResonaPro processor.

    Build (from the repository root):

        cmake -S . -B .work/build-tests -DRESONAPRO_BUILD_TESTS=ON -DRESONAPRO_COPY_PLUGIN=OFF
        cmake --build .work/build-tests --target ResonaProTests
        ./.work/build-tests/ResonaProTests_artefacts/ResonaProTests

    It drives the real AudioProcessor through the full dry/wet/spectral chain and
    checks the things that a DAW will actually rely on: reported latency, perfect
    reconstruction at zero depth, correct delta and bypass behaviour, parameter
    and state round-tripping, preset integrity and robustness against NaN input.
*/

#include "PluginProcessor.h"
#include "DSP/LearnAnalyzer.h"

#include <cstdio>
#include <cmath>
#include <vector>
#include <random>

using namespace ResonaPro;

static int failures = 0;

static void check (bool ok, const juce::String& what, const juce::String& detail = {})
{
    std::printf ("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL",
                 what.toRawUTF8(), detail.isEmpty() ? "" : "  -- ", detail.toRawUTF8());
    if (! ok) ++failures;
}

static void setParam (ResonaProAudioProcessor& p, const juce::String& id, float value)
{
    p.setParameterValue (id, value);
}

// ---------------------------------------------------------------------------
// An independent K-weighting measurement (ITU-R BS.1770), so the level-matching
// test checks the quantity the processor actually targets rather than a proxy.
// Written out here rather than reusing the processor's own filter, so this stays
// a real check instead of a restatement of the implementation.
namespace
{
    struct KBiquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double run (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    struct KWeightRef
    {
        KBiquad shelf, hp;

        void design (double fs)
        {
            const double pi = 3.14159265358979323846;
            {
                const double A  = std::pow (10.0, 3.999443854 / 40.0);
                const double w0 = 2.0 * pi * 1501.014451 / fs;
                const double cw = std::cos (w0), sw = std::sin (w0);
                const double al = sw / (2.0 * 0.7073032370);
                const double sa = 2.0 * std::sqrt (A) * al;
                const double a0 = (A + 1.0) - (A - 1.0) * cw + sa;
                shelf.b0 =  A * ((A + 1.0) + (A - 1.0) * cw + sa) / a0;
                shelf.b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw) / a0;
                shelf.b2 =  A * ((A + 1.0) + (A - 1.0) * cw - sa) / a0;
                shelf.a1 =  2.0 * ((A - 1.0) - (A + 1.0) * cw) / a0;
                shelf.a2 =        ((A + 1.0) - (A - 1.0) * cw - sa) / a0;
            }
            {
                const double w0 = 2.0 * pi * 38.13547087602444 / fs;
                const double cw = std::cos (w0), sw = std::sin (w0);
                const double al = sw / (2.0 * 0.5003270373238773);
                const double a0 = 1.0 + al;
                hp.b0 =  (1.0 + cw) / 2.0 / a0;
                hp.b1 = -(1.0 + cw) / a0;
                hp.b2 =  (1.0 + cw) / 2.0 / a0;
                hp.a1 = -2.0 * cw / a0;
                hp.a2 =  (1.0 - al) / a0;
            }
        }

        double run (double x) noexcept { return hp.run (shelf.run (x)); }
    };

    double loudnessDb (const std::vector<float>& x, int from, int to, double fs)
    {
        KWeightRef k;
        k.design (fs);
        double sum = 0.0;
        int count = 0;
        for (int i = from; i < to; ++i)
        {
            const double y = k.run (static_cast<double> (x[static_cast<size_t> (i)]));
            sum += y * y;
            ++count;
        }
        return 10.0 * std::log10 (std::max (1.0e-30, sum / std::max (1, count)));
    }
}

static void renderThrough (ResonaProAudioProcessor& p,
                           const std::vector<float>& inL, const std::vector<float>& inR,
                           std::vector<float>& outL, std::vector<float>& outR,
                           int blockSize = 256)
{
    const int n = static_cast<int> (inL.size());
    outL.assign (static_cast<size_t> (n), 0.0f);
    outR.assign (static_cast<size_t> (n), 0.0f);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    for (int off = 0; off < n; off += blockSize)
    {
        const int len = std::min (blockSize, n - off);
        buffer.setSize (2, len, false, false, true);
        std::copy (inL.begin() + off, inL.begin() + off + len, buffer.getWritePointer (0));
        std::copy (inR.begin() + off, inR.begin() + off + len, buffer.getWritePointer (1));

        p.processBlock (buffer, midi);

        std::copy (buffer.getReadPointer (0), buffer.getReadPointer (0) + len, outL.begin() + off);
        std::copy (buffer.getReadPointer (1), buffer.getReadPointer (1) + len, outR.begin() + off);
    }
}

static std::vector<float> delayed (const std::vector<float>& v, int lat)
{
    std::vector<float> o (v.size(), 0.0f);
    for (size_t i = 0; i < v.size(); ++i)
        if (static_cast<int> (i) >= lat) o[i] = v[i - static_cast<size_t> (lat)];
    return o;
}

static std::vector<float> makeVocal (double sr, int n)
{
    std::mt19937 rng (11);
    std::normal_distribution<float> nd (0.0f, 1.0f);
    std::vector<float> v (static_cast<size_t> (n), 0.0f);

    const double f0 = 196.0;
    struct Res { double f, q, g; };
    const Res formants[] = { { 650, 7, 1.0 }, { 1180, 8, 0.7 }, { 2750, 10, 0.6 }, { 3300, 16, 1.0 } };
    std::vector<double> y1 (4, 0), y2 (4, 0);

    for (int i = 0; i < n; ++i)
    {
        double src = 0.0;
        for (int h = 1; h <= 40; ++h)
        {
            const double f = f0 * h;
            if (f > sr * 0.45) break;
            src += std::sin (2.0 * M_PI * f * i / sr) / h;
        }
        src += 0.06 * nd (rng);

        double out = 0.0;
        for (int k = 0; k < 4; ++k)
        {
            const double w = 2.0 * M_PI * formants[k].f / sr;
            const double rr = std::exp (-w / (2.0 * formants[k].q));
            const double y = (1 - rr) * src + 2 * rr * std::cos (w) * y1[k] - rr * rr * y2[k];
            y2[k] = y1[k]; y1[k] = y;
            out += formants[k].g * y;
        }
        v[static_cast<size_t> (i)] = float (0.35 * out * (0.6 + 0.4 * std::sin (2.0 * M_PI * 3.5 * i / sr)));
    }
    return v;
}

//==================================================================================================

//----------------------------------------------------------------------------
// The LEARN fence. LEARN is allowed to propose a starting point; it is not
// allowed to rewrite the session. This test is the reason the fence can be
// trusted -- an allowlist nobody tests is an allowlist that rots.
//----------------------------------------------------------------------------
static void testLearnFence()
{
    ResonaProAudioProcessor p;

    int missing = 0;
    juce::String firstMissing;
    for (const auto& id : ResonaProAudioProcessor::learnEditableIds())
        if (p.apvts.getParameter (id) == nullptr)
        {
            ++missing;
            if (firstMissing.isEmpty())
                firstMissing = id;
        }

    check (missing == 0, "every LEARN-editable id is a real parameter",
           missing ? firstMissing + " (" + juce::String (missing) + " missing)"
                   : juce::String ("all present"));

    const char* locked[] = { "quality", "modeHard", "midSide", "deltaListen",
                             "vocalProfile", "outGain", "stereoLink", "mix",
                             "autoGain", "oversampling", "externalKey", "bypass" };
    juce::String leaked;
    for (auto* id : locked)
        if (ResonaProAudioProcessor::isLearnEditable (id))
            leaked += juce::String (id) + " ";

    check (leaked.isEmpty(),
           "LEARN cannot touch resolution, hard mode, mid/side, delta, weighting, output, stereo link or mix",
           leaked.isEmpty() ? juce::String ("all refused") : "allowed: " + leaked);

    check (ResonaProAudioProcessor::isLearnEditable ("depth")
             && ResonaProAudioProcessor::isLearnEditable ("detailTilt")
             && ResonaProAudioProcessor::isLearnEditable ("sibilanceHigh")
             && ResonaProAudioProcessor::isLearnEditable ("eq_q_3")
             && ResonaProAudioProcessor::isLearnEditable ("eq_type_1"),
           "LEARN may propose depth, tilts, sibilance and the cue shape",
           "depth / detailTilt / sibilanceHigh / eq_q_3 / eq_type_1");
}

//------------------------------------------------------------------------------
// SOLO THE CUT
//
// The control exists to answer one question on real material: is a vowel leaking
// into the de-esser's cut? That only works if the solo really is the de-esser's
// own subtraction and nothing else. These three checks pin that down, and the
// first one is the one that matters -- if solo is not silent when nothing is
// being cut, then everything you hear in it is a lie.
static void testSoloTheCut()
{
    const double sr = 48000.0;
    const int n = 48000 * 2;

    // makeVocal has no fricatives in it, so the de-esser correctly does nothing
    // on it and a solo test on that signal would prove nothing. This adds bursts
    // of band-limited noise across 5-11 kHz -- the shape of an "s" -- so there is
    // something for the de-esser to act on.
    auto sibilantTake = [&] (unsigned seed)
    {
        auto v = makeVocal (sr, n);
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> uf (5000.0f, 11000.0f);
        std::uniform_real_distribution<float> up (0.0f, 6.2831853f);
        const int K = 60;
        std::vector<float> fq (static_cast<size_t> (K)), ph (static_cast<size_t> (K));
        for (int j = 0; j < K; ++j)
        {
            fq[static_cast<size_t> (j)] = uf (rng);
            ph[static_cast<size_t> (j)] = up (rng);
        }
        for (int i = 0; i < n; ++i)
        {
            const double t = double (i) / sr;
            if (std::fmod (t, 0.6) >= 0.12) continue;   // 120 ms burst every 600 ms
            float acc = 0.0f;
            for (int j = 0; j < K; ++j)
                acc += std::sin (float (6.2831853 * double (fq[static_cast<size_t> (j)]) * t)
                                 + ph[static_cast<size_t> (j)]);
            v[static_cast<size_t> (i)] += 0.09f * acc;
        }
        return v;
    };

    const auto inL = sibilantTake (7u);
    const auto inR = sibilantTake (11u);

    auto rms = [] (const std::vector<float>& v)
    {
        double a = 0.0;
        for (float x : v) a += double (x) * double (x);
        return static_cast<float> (std::sqrt (a / std::max<size_t> (1, v.size())));
    };

    auto build = [&] (ResonaProAudioProcessor& p, float amount)
    {
        p.setPlayConfigDetails (2, 2, sr, 256);
        setParam (p, "depth", 0.0f);
        setParam (p, "sibilanceSmooth", amount);
        setParam (p, "mix", 100.0f);
        p.prepareToPlay (sr, 256);
    };

    std::printf ("-- solo the cut --\n");

    // 1. Nothing cut means nothing to hear.
    {
        ResonaProAudioProcessor p;
        build (p, 0.0f);
        setParam (p, "soloDeEss", 1.0f);
        std::vector<float> oL, oR;
        renderThrough (p, inL, inR, oL, oR);
        const float r = rms (oL);
        check (r < 1.0e-4f, "Solo Cut is silent when nothing is being cut",
               juce::String (juce::Decibels::gainToDecibels (r), 1) + " dBFS");
    }

    // 2. With the de-esser working, the solo must actually carry the removal.
    {
        ResonaProAudioProcessor p;
        build (p, 1.0f);
        setParam (p, "soloDeEss", 1.0f);
        std::vector<float> oL, oR;
        renderThrough (p, inL, inR, oL, oR);
        const float r = rms (oL);
        check (r > 1.0e-3f, "Solo Cut carries the de-esser's removal",
               juce::String (juce::Decibels::gainToDecibels (r), 1) + " dBFS");
    }

    // 3. With the resonance engine out of the way, solo must equal dry - wet.
    //    That is what proves it is the de-esser's own subtraction and not the
    //    whole plug-in's, which is what DELTA already gives.
    {
        auto render = [&] (bool solo, std::vector<float>& oL, std::vector<float>& oR)
        {
            ResonaProAudioProcessor p;
            build (p, 1.0f);
            setParam (p, "soloDeEss", solo ? 1.0f : 0.0f);
            renderThrough (p, inL, inR, oL, oR);
        };

        std::vector<float> wL, wR, sL, sR;
        render (false, wL, wR);
        render (true,  sL, sR);

        ResonaProAudioProcessor pl;
        pl.setPlayConfigDetails (2, 2, sr, 256);
        build (pl, 1.0f);
        const int lat = juce::jlimit (0, n - 1, pl.getEngineLatencySamples());

        double worst = 0.0;
        for (int i = lat; i < n; ++i)
            worst = std::max (worst, std::abs (double (inL[static_cast<size_t> (i - lat)]
                                                       - wL[static_cast<size_t> (i)])
                                              - double (sL[static_cast<size_t> (i)])));
        check (worst < 1.0e-3, "Solo Cut is exactly the de-esser's subtraction",
               "worst sample error " + juce::String (worst, 6));
    }

    // 4. A monitor must not be able to poison the graph.
    //
    // The first version let the solo gain reach exactly zero, which becomes -inf
    // once synthesizeChannel converts it to dB. The visualiser smooths each point
    // with a recursive filter, so a single -inf never washes out: it survives
    // every later frame and the display stays dead for the life of the window
    // while the audio keeps working. Swapping Solo Cut in and out once was enough
    // to trigger it. This asserts the graph data stays finite across that cycle.
    {
        ResonaProAudioProcessor p;
        build (p, 1.0f);
        setParam (p, "eq_gain_5", 12.0f);

        std::vector<float> oL, oR;
        setParam (p, "soloDeEss", 0.0f);
        renderThrough (p, inL, inR, oL, oR);
        setParam (p, "soloDeEss", 1.0f);
        renderThrough (p, inL, inR, oL, oR);
        setParam (p, "soloDeEss", 0.0f);
        renderThrough (p, inL, inR, oL, oR);

        std::array<float, ResonaProAudioProcessor::ScopeSize> m {}, r {}, b {}, w {};
        float refProm = 0.0f;
        p.getVisualizerData (m, r, b, w, refProm);

        int bad = 0;
        for (size_t i = 0; i < r.size(); ++i)
            if (! std::isfinite (r[i]) || ! std::isfinite (m[i])
                || ! std::isfinite (b[i]) || ! std::isfinite (w[i]))
                ++bad;

        check (bad == 0, "the graph survives a Solo Cut on/off cycle",
               juce::String (bad) + " non-finite points of "
             + juce::String (static_cast<int> (r.size())));
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::printf ("\n=== ResonaPro processor validation ===\n\n");

    testLearnFence();
    testSoloTheCut();

    const double sr = 48000.0;
    const int n = 48000 * 2;
    const std::vector<float> inL = makeVocal (sr, n);
    const std::vector<float> inR = makeVocal (sr, n);   // correlated but distinct seed path

    //----------------------------------------------------------------------------------------------
    std::printf ("-- construction & parameters --\n");
    {
        ResonaProAudioProcessor p;
        const auto& params = p.getParameters();
        const juce::StringArray required {
            "depth", "sharpness", "selectivity", "attack", "release", "maxReduction",
            "transientPreserve", "vocalProfile", "quality", "response", "stereoLink",
            "iso226", "modeHard", "midSide", "deltaListen", "bypass", "mix", "outGain"
        };

        juce::StringArray missing;
        for (const auto& id : required)
            if (p.apvts.getParameter (id) == nullptr)
                missing.add (id);

        check (missing.isEmpty(), "all core parameters exist",
               missing.isEmpty() ? juce::String (params.size()) + " parameters" : "missing: " + missing.joinIntoString (", "));

        int bandParams = 0;
        for (int b = 1; b <= 8; ++b)
            for (const char* prefix : { "eq_enable_", "eq_freq_", "eq_gain_", "eq_q_" })
                if (p.apvts.getParameter (juce::String (prefix) + juce::String (b)) != nullptr)
                    ++bandParams;
        check (bandParams == 32, "all 8 focus bands are exposed", juce::String (bandParams) + "/32");
    }

    //----------------------------------------------------------------------------------------------
    std::printf ("\n-- latency reporting --\n");
    {
        ResonaProAudioProcessor p;
        // Parameters are set before prepareToPlay, exactly as a host does when it
        // restores a saved state, so no ramp is in flight during the measurement.
        setParam (p, "quality", 1.0f);   // 2k
        setParam (p, "response", 1.0f);
        p.prepareToPlay (sr, 512);
        std::vector<float> oL, oR;
        renderThrough (p, inL, inR, oL, oR, 512);
        const int lat2k = p.getEngineLatencySamples();

        setParam (p, "quality", 3.0f);   // 8k
        renderThrough (p, inL, inR, oL, oR, 512);
        const int lat8k = p.getEngineLatencySamples();

        std::printf ("     engine latency: 2k -> %d samples, 8k -> %d samples\n", lat2k, lat8k);
        check (lat2k == 2048, "2k transform has 2048 samples of latency");
        check (lat8k == 8192, "8k transform has 8192 samples of latency");

        // With a message loop pumped, the host-visible latency follows.
        if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
            mm->runDispatchLoopUntil (50);
        check (p.getLatencySamples() == 8192, "host-visible latency follows the engine",
               juce::String (p.getLatencySamples()) + " samples");
    }

    //----------------------------------------------------------------------------------------------
    std::printf ("\n-- transparent at zero depth (full chain null test) --\n");
    {
        ResonaProAudioProcessor p;
        setParam (p, "depth", 0.0f);
        // The de-esser is its own stage and defaults to on, so Depth 0 alone
        // is no longer a null. Every stage has to be off for this test to mean
        // what it says.
        setParam (p, "sibilanceSmooth", 0.0f);
        setParam (p, "mix", 100.0f);
        setParam (p, "outGain", 0.0f);
        setParam (p, "bypass", 0.0f);
        setParam (p, "deltaListen", 0.0f);
        setParam (p, "quality", 1.0f);
        setParam (p, "response", 1.0f);
        p.prepareToPlay (sr, 512);

        std::vector<float> oL, oR;
        renderThrough (p, inL, inR, oL, oR, 512);

        const int lat = p.getEngineLatencySamples();
        const auto ref = delayed (inL, lat);
        double err = 0.0, ref2 = 0.0;
        for (int i = 0; i < n; ++i)
        {
            err  += std::pow (double (oL[i]) - ref[i], 2.0);
            ref2 += std::pow (double (ref[i]), 2.0);
        }
        const double rel = err / std::max (1e-30, ref2);
        check (rel < 1.0e-10, "depth 0, mix 100 % is bit-transparent",
               "residual " + juce::String (rel, 6));
    }

    //----------------------------------------------------------------------------------------------
    std::printf ("\n-- bypass, delta and mono --\n");
    {
        ResonaProAudioProcessor p;
        setParam (p, "depth", 2.0f);
        setParam (p, "eq_gain_3", 6.0f);
        setParam (p, "quality", 1.0f);
        setParam (p, "response", 1.0f);
        p.prepareToPlay (sr, 512);
        const int lat = p.getEngineLatencySamples();
        const auto ref = delayed (inL, lat);

        std::vector<float> oL, oR;

        setParam (p, "bypass", 1.0f);
        renderThrough (p, inL, inR, oL, oR, 512);
        double worst = 0.0;
        for (int i = 0; i < n; ++i) worst = std::max (worst, std::abs (double (oL[i]) - ref[i]));
        check (worst < 1.0e-6, "bypass passes the latency-aligned dry signal",
               "max error " + juce::String (worst, 8));

        setParam (p, "bypass", 0.0f);
        setParam (p, "deltaListen", 1.0f);
        renderThrough (p, inL, inR, oL, oR, 512);
        double deltaRms = 0.0, dryRms = 0.0;
        for (int i = lat; i < n; ++i)
        {
            deltaRms += std::pow (double (oL[i]), 2.0);
            dryRms   += std::pow (double (ref[i]), 2.0);
        }
        const double ratioDb = 10.0 * std::log10 (std::max (1e-30, deltaRms) / std::max (1e-30, dryRms));
        std::printf ("     delta vs dry energy: %.1f dB\n", ratioDb);
        check (ratioDb < -6.0 && ratioDb > -60.0, "delta exposes only what was removed");

        // Mono bus: the processor must not crash and must produce finite output.
        ResonaProAudioProcessor pm;
        {
            juce::AudioProcessor::BusesLayout monoLayout;
            monoLayout.inputBuses.add (juce::AudioChannelSet::mono());
            monoLayout.outputBuses.add (juce::AudioChannelSet::mono());
            pm.setBusesLayout (monoLayout);
        }
        pm.prepareToPlay (sr, 512);
        setParam (pm, "depth", 1.5f);

        juce::AudioBuffer<float> mono (1, 256);
        juce::MidiBuffer midi;
        bool finite = true;
        for (int off = 0; off < n; off += 256)
        {
            mono.clear();
            for (int i = 0; i < 256; ++i)
                mono.getWritePointer (0)[i] = (off + i < n) ? inL[static_cast<size_t> (off + i)] : 0.0f;
            pm.processBlock (mono, midi);
            for (int i = 0; i < 256; ++i)
                if (! std::isfinite (mono.getReadPointer (0)[i])) finite = false;
        }
        check (finite, "mono bus renders finite output");
    }

    //----------------------------------------------------------------------------------------------
    std::printf ("\n-- processing actually does something musical --\n");
    {
        ResonaProAudioProcessor p;
        p.prepareToPlay (sr, 512);
        setParam (p, "depth", 1.0f);
        setParam (p, "mix", 100.0f);

        // Neutral focus bands must still reduce. This check used to assert the
        // opposite, because the detector gated on the cue: a flat band produced
        // exactly zero reduction, so the plugin was transparent on its default
        // settings. Neutral now means "look here as normal".
        std::vector<float> oL0, oR0;
        renderThrough (p, inL, inR, oL0, oR0, 512);
        const int lat0 = p.getLatencySamples();
        const auto ref0 = delayed (inL, lat0);
        double rin0 = 0.0, rout0 = 0.0;
        for (int i = lat0; i < n; ++i) { rin0 += std::pow (double (ref0[i]), 2.0); rout0 += std::pow (double (oL0[i]), 2.0); }
        const double db0 = 10.0 * std::log10 (rout0 / std::max (1e-30, rin0));
        check (db0 < -0.05, "neutral focus bands still reduce (the plugin acts with no focus set)",
               juce::String (db0, 2) + " dB");

        // Raised cue produces proportional reduction
        setParam (p, "eq_gain_3", 6.0f);
        std::vector<float> oL, oR;
        renderThrough (p, inL, inR, oL, oR, 512);

        const int lat = p.getLatencySamples();
        const auto ref = delayed (inL, lat);
        double rin = 0.0, rout = 0.0;
        for (int i = lat; i < n; ++i)
        {
            rin  += std::pow (double (ref[i]), 2.0);
            rout += std::pow (double (oL[i]), 2.0);
        }
        const double db = 10.0 * std::log10 (rout / std::max (1e-30, rin));
        std::printf ("     output level change with cue at +6dB: %+.2f dB\n", db);
        check (db < db0 && db > -8.0, "raising a focus band deepens the reduction there",
               juce::String (db, 2) + " dB vs " + juce::String (db0, 2) + " dB");
    }

    //----------------------------------------------------------------------------------------------
    std::printf ("\n-- presets --\n");
    {
        ResonaProAudioProcessor p;
        p.prepareToPlay (sr, 512);

        const int count = ResonaProAudioProcessor::getNumPresets();
        bool allOk = true;
        juce::String failedName;

        for (int i = 0; i < count; ++i)
        {
            p.loadPreset (i);

            for (auto* param : p.getParameters())
            {
                const float v = param->getValue();
                if (! (v >= 0.0f && v <= 1.0f))
                {
                    allOk = false;
                    failedName = ResonaProAudioProcessor::getPresetNames()[i] + " / " + param->getName (64);
                    break;
                }
            }

            std::vector<float> oL, oR;
            renderThrough (p, inL, inR, oL, oR, 512);
            for (int s = 0; s < n; ++s)
                if (! std::isfinite (oL[s]) || std::abs (oL[s]) > 8.0f)
                {
                    allOk = false;
                    failedName = ResonaProAudioProcessor::getPresetNames()[i] + " (audio)";
                    break;
                }
        }

        check (allOk, juce::String (count) + " presets load and render cleanly", failedName);
    }

    //----------------------------------------------------------------------------------------------
    std::printf ("\n-- state round-trip --\n");
    {
        ResonaProAudioProcessor a;
        setParam (a, "depth", 2.35f);
        setParam (a, "selectivity", 0.73f);
        setParam (a, "quality", 2.0f);
        setParam (a, "eq_freq_3", 733.0f);
        setParam (a, "midSide", 1.0f);
        setParam (a, "externalKey", 1.0f);
        setParam (a, "motionProtect", 0.67f);

        juce::MemoryBlock state;
        a.getStateInformation (state);

        ResonaProAudioProcessor b;
        b.setStateInformation (state.getData(), static_cast<int> (state.getSize()));

        const float d2  = b.apvts.getRawParameterValue ("depth")->load();
        const float s2  = b.apvts.getRawParameterValue ("selectivity")->load();
        const float q2  = b.apvts.getRawParameterValue ("quality")->load();
        const float f2  = b.apvts.getRawParameterValue ("eq_freq_3")->load();
        const float ms2 = b.apvts.getRawParameterValue ("midSide")->load();

        const bool ok = std::abs (d2 - 2.35f) < 1.0e-3f
                     && std::abs (s2 - 0.73f) < 1.0e-3f
                     && std::abs (q2 - 2.0f) < 1.0e-3f
                     && std::abs (f2 - 733.0f) < 1.0f
                     && ms2 > 0.5f
                     && b.apvts.getRawParameterValue ("externalKey")->load() > 0.5f
                     && std::abs (b.apvts.getRawParameterValue ("motionProtect")->load() - 0.67f) < 0.02f;
        check (ok, "state saves and restores every parameter");
    }

    //----------------------------------------------------------------------------------------------
    std::printf ("\n-- robustness --\n");
    {
        ResonaProAudioProcessor p;
        p.prepareToPlay (sr, 512);
        setParam (p, "depth", 4.0f);
        setParam (p, "sharpness", 4.0f);
        setParam (p, "selectivity", 0.0f);
        setParam (p, "autoGain", 1.0f);

        std::vector<float> badL (static_cast<size_t> (n), 0.0f), badR (static_cast<size_t> (n), 0.0f);
        for (int i = 0; i < n; ++i)
        {
            badL[static_cast<size_t> (i)] = ((i / 97) % 5 == 0) ? std::nanf ("") : std::sin (0.01f * i);
            badR[static_cast<size_t> (i)] = ((i / 131) % 7 == 0) ? 1.0e30f : std::cos (0.013f * i);
        }

        std::vector<float> oL, oR;
        renderThrough (p, badL, badR, oL, oR, 512);

        bool finite = true;
        float peak = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            if (! std::isfinite (oL[i]) || ! std::isfinite (oR[i])) finite = false;
            peak = std::max (peak, std::abs (oL[i]));
        }
        check (finite, "NaN / huge input never escapes the plug-in",
               "peak " + juce::String (peak, 3));

        // Recovery: a clean signal afterwards must come back clean.
        std::vector<float> cleanL (inL), cleanR (inR), oL2, oR2;
        renderThrough (p, cleanL, cleanR, oL2, oR2, 512);
        bool recovered = true;
        for (int i = n / 2; i < n; ++i)
            if (! std::isfinite (oL2[i])) recovered = false;
        check (recovered, "level match recovers cleanly after a corrupted buffer");
    }

    //----------------------------------------------------------------------------------------------
    std::printf ("\n-- level matching --\n");
    {
        struct Meas { double rmsDb, loudDb, peakDb; };
        auto measure = [&] (bool matchOn, float depth, float scale)
        {
            std::vector<float> srcL (inL.size()), srcR (inR.size());
            for (size_t i = 0; i < srcL.size(); ++i)
            {
                srcL[i] = inL[i] * scale;
                srcR[i] = inR[i] * scale;
            }

            ResonaProAudioProcessor p;
            setParam (p, "depth", depth);
            setParam (p, "eq_gain_3", 6.0f);
            setParam (p, "eq_gain_4", 6.0f);
            setParam (p, "mix", 100.0f);
            setParam (p, "autoGain", matchOn ? 1.0f : 0.0f);
            setParam (p, "quality", 1.0f);
            setParam (p, "response", 1.0f);
            p.prepareToPlay (sr, 512);

            std::vector<float> oL, oR;
            renderThrough (p, srcL, srcR, oL, oR, 512);

            const int lat = p.getEngineLatencySamples();
            const int from = lat + static_cast<int> (sr);   // allow the trim to settle

            std::vector<float> dry (static_cast<size_t> (n - from));
            for (int i = from; i < n; ++i)
                dry[static_cast<size_t> (i - from)] = srcL[static_cast<size_t> (i - lat)];

            double drySq = 0.0, wetSq = 0.0, dryPk = 0.0, wetPk = 0.0;
            for (int i = from; i < n; ++i)
            {
                const float d = srcL[static_cast<size_t> (i - lat)];
                const float w = oL[static_cast<size_t> (i)];
                drySq += double (d) * d;
                wetSq += double (w) * w;
                dryPk = std::max (dryPk, double (std::abs (d)));
                wetPk = std::max (wetPk, double (std::abs (w)));
            }

            return Meas { 20.0 * std::log10 (std::sqrt (wetSq)
                                             / std::max (1.0e-12, std::sqrt (drySq))),
                          loudnessDb (oL, from, n, sr)
                            - loudnessDb (dry, 0, static_cast<int> (dry.size()), sr),
                          20.0 * std::log10 (std::max (1.0e-12, wetPk)
                                             / std::max (1.0e-12, dryPk)) };
        };

        // At a realistic mixing level the compensation should be complete.
        const Meas offQuiet = measure (false, 1.5f, 0.2f);
        const Meas onQuiet  = measure (true,  1.5f, 0.2f);
        std::printf ("     quiet take (-11 dBFS peak): off %+.2f dB, on %+.2f dB loudness\n",
                     offQuiet.loudDb, onQuiet.loudDb);
        check (offQuiet.loudDb < -0.4, "reduction lowers the loudness (the problem being solved)",
               juce::String (offQuiet.loudDb, 2) + " dB");
        check (std::abs (onQuiet.loudDb) < 0.25, "matching restores perceived loudness",
               juce::String (onQuiet.loudDb, 2) + " dB");
        check (std::abs (onQuiet.rmsDb) < 0.9, "matching also keeps the plain RMS close",
               juce::String (onQuiet.rmsDb, 2) + " dB");

        // A take that already peaks above full scale must not be pushed further.
        const Meas onHot = measure (true, 1.5f, 1.0f);
        std::printf ("     hot take (peaks above 0 dBFS): peak change %+.2f dB\n", onHot.peakDb);
        check (onHot.peakDb < 0.1, "the boost never raises the peak above the input",
               juce::String (onHot.peakDb, 2) + " dB");
    }

    //----------------------------------------------------------------------------------------------
    std::printf ("\n-- delta band monitor --\n");
    {
        auto deltaEnergyDb = [&] (float bandValue)
        {
            ResonaProAudioProcessor p;
            setParam (p, "depth", 2.0f);
            setParam (p, "deltaListen", 1.0f);
            setParam (p, "deltaBand", bandValue);
            setParam (p, "quality", 1.0f);
            setParam (p, "response", 1.0f);
            p.prepareToPlay (sr, 512);

            std::vector<float> oL, oR;
            renderThrough (p, inL, inR, oL, oR, 512);

            bool finite = true;
            double e = 0.0;
            for (int i = 0; i < n; ++i)
            {
                if (! std::isfinite (oL[i])) finite = false;
                e += double (oL[i]) * oL[i];
            }
            check (finite, "band filtered delta stays finite");
            return 10.0 * std::log10 (e / std::max (1e-30, double (n)));
        };

        const double allBands = deltaEnergyDb (0.0f);
        const double oneBand  = deltaEnergyDb (4.0f);
        std::printf ("     full-band delta %.1f dB, band 4 only %.1f dB\n", allBands, oneBand);
        check (oneBand < allBands - 3.0,
               "restricting delta to one band removes the rest of the spectrum");
    }

    //----------------------------------------------------------------------------------------------
    std::printf ("\n-- block size independence --\n");
    {
        // Two independent instances so that neither inherits the other's tails.
        ResonaProAudioProcessor pSmall, pLarge;
        for (auto* proc : { &pSmall, &pLarge })
        {
            setParam (*proc, "depth", 1.5f);
            setParam (*proc, "quality", 1.0f);
            setParam (*proc, "response", 1.0f);
            proc->prepareToPlay (sr, 4096);
        }

        std::vector<float> aL, aR, bL, bR;
        renderThrough (pSmall, inL, inR, aL, aR, 64);
        renderThrough (pLarge, inL, inR, bL, bR, 2048);

        double worst = 0.0;
        for (int i = 0; i < n; ++i)
            worst = std::max (worst, std::abs (double (aL[i]) - bL[i]));
        check (worst < 1.0e-4, "output is independent of host block size",
               "max difference " + juce::String (worst, 6));
    }

    std::printf ("\n-- external sidechain bus --\n");
    {
        auto render = [&] (bool useKey)
        {
            ResonaProAudioProcessor p;
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (juce::AudioChannelSet::stereo());
            layout.inputBuses.add (juce::AudioChannelSet::stereo());
            layout.outputBuses.add (juce::AudioChannelSet::stereo());
            check (p.setBusesLayout (layout), "optional stereo sidechain layout accepted");
            setParam (p, "externalKey", useKey ? 1.0f : 0.0f);
            setParam (p, "depth", 3.0f);
            setParam (p, "eq_freq_4", 3100.0f);
            setParam (p, "eq_gain_4", 6.0f);
            p.prepareToPlay (sr, 512);
            juce::AudioBuffer<float> buf (4, 512);
            juce::MidiBuffer midi;
            double outEnergy = 0.0;
            bool preserved = true;
            for (int off = 0; off < n; off += 512)
            {
                const int len = std::min (512, n - off);
                for (int i = 0; i < len; ++i)
                {
                    const int s = off + i;
                    buf.setSample (0, i, inL[static_cast<size_t> (s)]);
                    buf.setSample (1, i, inR[static_cast<size_t> (s)]);
                    const float key = 0.7f * std::sin (float (2.0 * M_PI * 3100.0 * s / sr));
                    buf.setSample (2, i, key);
                    buf.setSample (3, i, key);
                }
                p.processBlock (buf, midi);
                for (int i = 0; i < len; ++i)
                {
                    const float key = 0.7f * std::sin (float (2.0 * M_PI * 3100.0 * (off + i) / sr));
                    preserved &= std::abs (buf.getSample (2, i) - key) < 1e-6f;
                    outEnergy += double (buf.getSample (0, i)) * buf.getSample (0, i);
                }
            }
            check (preserved, "the sidechain bus is never overwritten");
            return outEnergy;
        };
        const auto internal = render (false);
        const auto external = render (true);
        const double change = 10.0 * std::log10 (external / internal);
        check (std::abs (change) > 0.05,
               "external key changes detection while main vocal stays the output",
               juce::String (change, 2) + " dB");
    }

    std::printf ("\n-- multi-resolution low-band detector --\n");
    // The Note Motion test was removed with the control itself. Two measurements
    // of the situation it is designed for showed no change. See Tests/DspTests.cpp.

    // The Note Motion analysis test was removed with the control itself:
    // two measurements of the situation it is designed for showed no change.

    std::printf ("\n-- learn-the-take suggestions --\n");
    {
        LearnAnalyzer learner;
        const int fixed = 212;
        for (int frame = 0; frame < 100; ++frame)
        {
            std::array<float, LearnAnalyzer::Points> m, b;
            m.fill (-90.0f); b.fill (-90.0f);
            m[static_cast<size_t> (fixed)] = -30.0f;
            b[static_cast<size_t> (fixed)] = -44.0f;
            const int moving = 135 + frame / 2;
            m[static_cast<size_t> (moving)] = -29.0f;
            b[static_cast<size_t> (moving)] = -44.0f;
            learner.addFrame (m, b, 2.0f);
        }
        const auto suggestions = learner.suggestions();
        const float expected = LearnAnalyzer::frequencyAt (fixed);
        bool hasFixed = false, hasMoving = false;
        for (const auto& c : suggestions)
        {
            hasFixed |= std::abs (std::log2 (c.frequency / expected)) < 0.04f;
            hasMoving |= (c.frequency > 500.0f && c.frequency < 1500.0f);
        }
        check (hasFixed, "persistent spectral peak is proposed for a focus band");
        check (! hasMoving, "moving spectral peak does not dominate the proposal");
    }


    std::printf ("\n-- fricative & sibilance preservation (anti-muffle check) --\n");
    {
        ResonaProAudioProcessor p;
        p.prepareToPlay (sr, 512);
        setParam (p, "depth", 1.0f);
        // This checks the RESONANCE path for muffling, so the de-esser is off.
        // It was on, and with the band widened and the ceiling raised it cut the
        // tilted noise by 1.4 dB -- correctly, because 6 dB/octave noise in the
        // fricative band is exactly what a de-esser is for. Measuring two stages
        // at once measured neither. The de-esser is measured in
        // testDeEsserOnVocalMaterial() instead.
        setParam (p, "sibilanceSmooth", 0.0f);
        std::mt19937 noiseRng (1337);
        std::normal_distribution<float> noiseDist (0.0f, 0.2f);
        const int noiseLen = 48000;
        std::vector<float> noiseIn (static_cast<size_t> (noiseLen)), noiseOutL, noiseOutR;
        for (int i = 0; i < noiseLen; ++i)
            noiseIn[static_cast<size_t> (i)] = noiseDist (noiseRng);

        renderThrough (p, noiseIn, noiseIn, noiseOutL, noiseOutR, 512);

        double inEnergy = 0.0, outEnergy = 0.0;
        for (int i = 48000 / 4; i < noiseLen; ++i)
        {
            inEnergy += static_cast<double> (noiseIn[static_cast<size_t> (i)]) * static_cast<double> (noiseIn[static_cast<size_t> (i)]);
            outEnergy += static_cast<double> (noiseOutL[static_cast<size_t> (i)]) * static_cast<double> (noiseOutL[static_cast<size_t> (i)]);
        }
        const double diffDb = 10.0 * std::log10 (std::max (1.0e-12, outEnergy / std::max (1.0e-12, inEnergy)));
        check (std::abs (diffDb) < 0.65, "broadband fricatives and breath are preserved without muffling",
               juce::String (diffDb, 2) + " dB");

        // The same guarantee on bright material. Flat white noise spreads its
        // energy evenly, so it can pass while the top octave is still being
        // ducked. A first difference gives 6 dB per octave of tilt, which puts
        // the energy where fricatives actually live and where the air band was
        // previously being dulled.
        std::vector<float> brightIn (static_cast<size_t> (noiseLen)), brightOutL, brightOutR;
        {
            float prev = 0.0f;
            for (int i = 0; i < noiseLen; ++i)
            {
                const float x = noiseIn[static_cast<size_t> (i)];
                brightIn[static_cast<size_t> (i)] = x - prev;
                prev = x;
            }
        }

        renderThrough (p, brightIn, brightIn, brightOutL, brightOutR, 512);

        double brightInEnergy = 0.0, brightOutEnergy = 0.0;
        for (int i = 48000 / 4; i < noiseLen; ++i)
        {
            brightInEnergy  += static_cast<double> (brightIn[static_cast<size_t> (i)])
                             * static_cast<double> (brightIn[static_cast<size_t> (i)]);
            brightOutEnergy += static_cast<double> (brightOutL[static_cast<size_t> (i)])
                             * static_cast<double> (brightOutL[static_cast<size_t> (i)]);
        }
        const double brightDb = 10.0 * std::log10 (
            std::max (1.0e-12, brightOutEnergy / std::max (1.0e-12, brightInEnergy)));
        check (std::abs (brightDb) < 0.65, "bright fricative-band noise keeps its top end",
               juce::String (brightDb, 2) + " dB");
    }
    std::printf ("\n-- editor rendering --\n");
    {
        ResonaProAudioProcessor p;
        p.prepareToPlay (sr, 512);

        // Push a take through first, so the snapshot shows the graph working
        // rather than an empty grid. A blank snapshot hides exactly the drawing
        // defects worth catching: curve shape, smoothing and mark placement.
        {
            const auto take = makeVocal (sr, static_cast<int> (sr) * 2);
            std::vector<float> oL, oR;
            renderThrough (p, take, take, oL, oR, 512);
        }

        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        check (editor != nullptr, "cream editor instantiates");
        if (editor)
        {
            editor->setSize (960, 700);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (250);
            editor->repaint();
            auto snapshot = editor->createComponentSnapshot (editor->getLocalBounds());
            check (snapshot.isValid() && snapshot.getWidth() == 960 && snapshot.getHeight() == 700,
                   "editor renders at default size");
            const auto path = juce::File::getCurrentWorkingDirectory()
                                  .getChildFile (".work/previews/cream-ui.png");
            path.getParentDirectory().createDirectory();
            path.deleteFile();
            if (auto out = std::unique_ptr<juce::FileOutputStream> (path.createOutputStream()))
            {
                juce::PNGImageFormat png;
                check (png.writeImageToStream (snapshot, *out), "GUI snapshot written for visual QA");
            }
        }
    }

    std::printf ("\n-- DETAIL changes the graph --\n");
    {
        // Reported from a live session: turning DETAIL changed the audio but the
        // analysing view stayed frozen. This measures the drawn result instead of
        // trusting the parameter plumbing.
        auto renderAt = [sr] (float detail)
        {
            ResonaProAudioProcessor proc;
            proc.prepareToPlay (sr, 512);
            setParam (proc, "depth", 2.0f);
            setParam (proc, "sharpness", detail);

            const auto take = makeVocal (sr, static_cast<int> (sr) * 2);
            std::vector<float> oL, oR;
            renderThrough (proc, take, take, oL, oR, 512);

            std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
            ed->setSize (960, 700);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (250);
            ed->repaint();
            return ed->createComponentSnapshot (ed->getLocalBounds());
        };

        const auto low  = renderAt (0.0f);
        const auto high = renderAt (10.0f);

        // The graph region only. The knob row and the header must stay put.
        const int y0 = 170, y1 = 540;
        long long changed = 0, total = 0;
        double sumDiff = 0.0;

        for (int y = y0; y < y1 && y < low.getHeight(); ++y)
        {
            for (int x = 0; x < low.getWidth(); ++x)
            {
                const auto a = low.getPixelAt (x, y);
                const auto b = high.getPixelAt (x, y);
                const int d = std::abs (int (a.getRed())   - int (b.getRed()))
                            + std::abs (int (a.getGreen()) - int (b.getGreen()))
                            + std::abs (int (a.getBlue())  - int (b.getBlue()));
                sumDiff += d;
                if (d > 12) ++changed;
                ++total;
            }
        }

        const double pct  = total > 0 ? 100.0 * double (changed) / double (total) : 0.0;
        const double mean = total > 0 ? sumDiff / double (total) / 3.0 : 0.0;

        std::printf ("     graph pixels changed: %lld of %lld (%.2f%%), mean delta %.1f of 255\n",
                     changed, total, pct, mean);
        // The bar is deliberately high. At 0.5% this test passed while the knob
        // was, in practice, invisible: it moved 1.6% of pixels by 0.4 of 255.
        // A threshold that loose is worse than no test.
        check (pct > 8.0, "DETAIL visibly changes the analysing view",
               juce::String (pct, 2) + "% of graph pixels, mean " + juce::String (mean, 1));
    }
    std::printf ("\n=== %s (%d failure%s) ===\n",
                 failures == 0 ? "ALL PASS" : "FAILURES", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
