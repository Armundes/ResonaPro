// Scores what Learn proposes, against what the default patch does.
//
// Why this exists: Learn was reported to produce settings that sound thin or
// boxy, and the default patch was reported to beat them. Neither claim could be
// checked, so neither could be acted on. This turns both into numbers over a
// corpus, and it is the control the Tier 1 and Tier 2 work is measured against.
//
// For each take it renders three times through the real plug-in processor:
//
//   dry       -- depth 0, so the delay line is the only thing in the path and
//                the reference is the input itself
//   default   -- the Init patch, untouched
//   learned   -- the profile Learn proposes from this take, applied
//
// Then it reports, over frames that are actually sounding:
//
//   sibilance reduction   how far 5-16 kHz falls against dry. This is the goal.
//   body damage           how far 100-1000 Hz moves against dry. The de-esser's
//                         range starts at 4 kHz, so any movement here is
//                         collateral. Boxy and thin both live in this number.
//   score                 reduction per dB of damage.
//
// Wet and dry are aligned by measuring the process delay rather than trusting
// the reported figure. An earlier tool in this project aligned by the wrong
// number and reported misalignment as processing, so the delay is measured.
//
//   LearnScore <take.wav> [more.wav ...] [--json out.json]
//                                        [--params "id=v,id=v"]
//                                        [--block N]

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"
#include "DSP/LearnAnalyzer.h"

using namespace ResonaPro;

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace
{
    constexpr double kRate = 48000.0;

    // The learner's timing assumes 20 frames a second (LearnAnalyzer::kFrameHz),
    // and one frame is published per audio block. A 2400 sample block at 48 kHz
    // is exactly 20 a second, so the block size is not a free choice here -- a
    // 4096 block would make every learned timing 1.7x wrong and this tool would
    // report a defect that the plug-in does not have.
    constexpr int kFrameBlock = 2400;

    void pump()
    {
        if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
            mm->runDispatchLoopUntil (60);
    }

    std::vector<float> readMono (const juce::File& f, double& srOut)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        std::vector<float> out;
        if (r == nullptr) return out;
        srOut = r->sampleRate;
        const int n = int (r->lengthInSamples);
        juce::AudioBuffer<float> buf (int (r->numChannels), n);
        r->read (&buf, 0, n, 0, true, true);
        out.resize (size_t (n));
        for (int i = 0; i < n; ++i)
        {
            double s = 0.0;
            for (int c = 0; c < buf.getNumChannels(); ++c)
                s += double (buf.getSample (c, i));
            out[size_t (i)] = float (s / std::max (1, buf.getNumChannels()));
        }
        return out;
    }

    // Push the take through the plug-in and return the output.
    std::vector<float> render (const std::vector<float>& x,
                               const std::map<std::string, float>& params,
                               int block)
    {
        ResonaProAudioProcessor p;
        for (const auto& kv : params)
            p.setParameterValue (kv.first, kv.second);
        p.prepareToPlay (kRate, block);
        pump();

        std::vector<float> out (x.size(), 0.0f);
        juce::AudioBuffer<float> buf (1, block);
        juce::MidiBuffer midi;
        for (size_t off = 0; off < x.size(); off += size_t (block))
        {
            const int len = int (std::min (size_t (block), x.size() - off));
            buf.setSize (1, len, false, false, true);
            buf.clear();
            for (int i = 0; i < len; ++i) buf.setSample (0, i, x[off + size_t (i)]);
            p.processBlock (buf, midi);
            for (int i = 0; i < len; ++i) out[off + size_t (i)] = buf.getSample (0, i);
        }
        return out;
    }

    // Measured, not reported. depth 0 makes the plug-in a pure delay, so the
    // impulse comes back out at exactly the process latency.
    int measureLatency (int block)
    {
        ResonaProAudioProcessor p;
        p.setParameterValue ("depth", 0.0f);
        p.prepareToPlay (kRate, block);
        pump();
        const int n = 1 << 16;
        juce::AudioBuffer<float> buf (1, block);
        juce::MidiBuffer midi;
        int peak = 0; float best = 0.0f;
        for (int off = 0; off < n; off += block)
        {
            const int len = std::min (block, n - off);
            buf.setSize (1, len, false, false, true);
            buf.clear();
            if (off == 0) buf.setSample (0, 0, 1.0f);
            p.processBlock (buf, midi);
            for (int i = 0; i < len; ++i)
                if (std::abs (buf.getSample (0, i)) > best)
                {
                    best = std::abs (buf.getSample (0, i));
                    peak = off + i;
                }
        }
        return peak;
    }

    LearnAnalyzer::TakeProfile learn (const std::vector<float>& x)
    {
        ResonaProAudioProcessor p;
        p.prepareToPlay (kRate, kFrameBlock);
        pump();

        LearnAnalyzer learner;
        std::array<float, ResonaProAudioProcessor::ScopeSize> mag {}, red {}, base {}, win {};
        float reference = 0.0f;
        uint64_t seq = 0, last = 0;

        juce::AudioBuffer<float> buf (1, kFrameBlock);
        juce::MidiBuffer midi;
        for (size_t off = 0; off < x.size(); off += size_t (kFrameBlock))
        {
            const int len = int (std::min (size_t (kFrameBlock), x.size() - off));
            buf.setSize (1, len, false, false, true);
            buf.clear();
            for (int i = 0; i < len; ++i) buf.setSample (0, i, x[off + size_t (i)]);
            p.processBlock (buf, midi);

            // Only feed the learner when the scope has actually advanced. The
            // editor does the same, so a paused DAW contributes no frames.
            p.getVisualizerData (mag, red, base, win, reference, &seq);
            if (seq == 0 || seq == last) continue;
            last = seq;
            learner.addFrame (mag, base, reference);
        }
        return learner.profile();
    }

    // Per-frame band energy, so the comparison can be restricted to the frames
    // that actually contain sibilance.
    //
    // The first version averaged the whole take and reported a 0.36 dB reduction
    // even with the de-esser driven hard. That was the metric, not the plug-in:
    // sibilants occupy a small fraction of a vocal, so a 6 dB cut on 10% of the
    // frames averages to under a dB. Averaging over everything measures how much
    // of the take is sibilant, which is not a question anyone asked.
    struct FrameBands { double sibDb = -200.0, bodyDb = -200.0; bool sounding = false; };

    std::vector<FrameBands> frameBands (const std::vector<float>& x, size_t from, size_t to)
    {
        std::vector<FrameBands> out;
        if (to <= from) return out;

        constexpr int order = 11;                 // 2048, ~43 ms at 48 kHz
        constexpr int fftSize = 1 << order;
        constexpr int bins = fftSize / 2 + 1;
        juce::dsp::FFT fft (order);

        std::vector<float> win (static_cast<size_t> (fftSize), 0.0f);
        for (int i = 0; i < fftSize; ++i)
            win[size_t (i)] = float (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi
                                                           * double (i) / double (fftSize)));

        std::vector<float> buf (static_cast<size_t> (2 * fftSize), 0.0f);
        constexpr int hop = fftSize / 2;

        for (size_t s0 = from; s0 + size_t (fftSize) <= to; s0 += size_t (hop))
        {
            double rms = 0.0;
            for (int i = 0; i < fftSize; ++i)
                rms += double (x[s0 + size_t (i)]) * double (x[s0 + size_t (i)]);
            rms = std::sqrt (rms / double (fftSize));

            FrameBands fb;
            fb.sounding = rms >= 1.0e-4;
            if (! fb.sounding) { out.push_back (fb); continue; }

            std::fill (buf.begin(), buf.end(), 0.0f);
            for (int i = 0; i < fftSize; ++i)
                buf[size_t (i)] = x[s0 + size_t (i)] * win[size_t (i)];
            fft.performFrequencyOnlyForwardTransform (buf.data());

            double ps = 0.0, pb = 0.0;
            for (int k = 0; k < bins; ++k)
            {
                const double hz = double (k) * kRate / double (fftSize);
                const double p  = double (buf[size_t (k)]) * double (buf[size_t (k)]);
                if (hz >= 5000.0 && hz <= 16000.0) ps += p;
                if (hz >= 100.0  && hz <= 1000.0)  pb += p;
            }
            fb.sibDb  = 10.0 * std::log10 (std::max (ps, 1.0e-20));
            fb.bodyDb = 10.0 * std::log10 (std::max (pb, 1.0e-20));
            out.push_back (fb);
        }
        return out;
    }

    // Restrict to frames that are sibilant in the dry signal: the top quarter by
    // 5-16 kHz level among sounding frames. Those are the frames the de-esser is
    // for, so those are the frames the score should be about.
    struct Bands { double sibReduce = 0.0, bodyDamage = 0.0; int frames = 0; };

    Bands compareOnSibilants (const std::vector<FrameBands>& dry,
                              const std::vector<FrameBands>& wet)
    {
        Bands out;
        std::vector<double> levels;
        const size_t n = std::min (dry.size(), wet.size());
        for (size_t i = 0; i < n; ++i)
            if (dry[i].sounding) levels.push_back (dry[i].sibDb);
        if (levels.size() < 8) return out;

        std::sort (levels.begin(), levels.end());
        const double threshold = levels[size_t (double (levels.size() - 1) * 0.75)];

        double ds = 0.0, ws = 0.0, db = 0.0, wb = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            if (! dry[i].sounding || dry[i].sibDb < threshold) continue;
            ds += dry[i].sibDb; ws += wet[i].sibDb;
            db += dry[i].bodyDb; wb += wet[i].bodyDb;
            ++out.frames;
        }
        if (out.frames == 0) return out;

        const double m = double (out.frames);
        out.sibReduce  = (ds - ws) / m;
        out.bodyDamage = std::abs ((db - wb) / m);
        return out;
    }

    // Positive control. Everything so far only proved the metric reads zero when
    // nothing happens; that says nothing about whether it can see a cut that is
    // really there. This applies a first-order high shelf to the wet signal and
    // the metric has to report it.
    void applyHighShelf (std::vector<float>& x, double fcHz, double shelfDb, double sr)
    {
        const double a = std::exp (-2.0 * juce::MathConstants<double>::pi * fcHz / sr);
        const double g = std::pow (10.0, shelfDb / 20.0);
        double lp = 0.0;
        for (auto& v : x)
        {
            lp += a * (double (v) - lp);
            v = float (lp + (double (v) - lp) * g);
        }
    }

    struct Result
    {
        std::string name;
        double sibReduceDefault = 0.0, sibReduceLearned = 0.0;
        double bodyDamageDefault = 0.0, bodyDamageLearned = 0.0;
        double scoreDefault = 0.0, scoreLearned = 0.0;
        bool   learnedValid = false;
        int    bands = 0;
        int    sibFrames = 0;
        double depth = 0.0, sharpness = 0.0, selectivity = 0.0;
        double maxCut = 0.0, attack = 0.0, release = 0.0;
    };
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    std::vector<juce::String> inputs;
    std::map<std::string, float> extra;
    juce::String jsonPath;
    int block = kFrameBlock;
    double shelfDb = 0.0;

    for (int i = 1; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a == "--json" && i + 1 < argc)        jsonPath = juce::String (argv[++i]);
        else if (a == "--block" && i + 1 < argc)  block = juce::String (argv[++i]).getIntValue();
        else if (a == "--shelfDb" && i + 1 < argc) shelfDb = juce::String (argv[++i]).getDoubleValue();
        else if (a == "--params" && i + 1 < argc)
        {
            const juce::String spec (argv[++i]);
            for (const auto& part : juce::StringArray::fromTokens (spec, ",", ""))
            {
                const int eq = part.indexOfChar ('=');
                if (eq > 0)
                    extra[part.substring (0, eq).trim().toStdString()]
                        = part.substring (eq + 1).trim().getFloatValue();
            }
        }
        else inputs.push_back (a);
    }

    if (inputs.empty())
    {
        std::fprintf (stderr, "usage: LearnScore <take.wav> [...] [--json out.json]\n");
        return 1;
    }

    const int latency = measureLatency (block);
    std::printf ("  measured process latency: %d samples (%.2f ms)\n\n", latency,
                 1000.0 * double (latency) / kRate);

    std::vector<Result> results;
    for (const auto& in : inputs)
    {
        juce::File f (in);
        if (! f.existsAsFile()) { std::fprintf (stderr, "  missing %s\n", in.toRawUTF8()); continue; }

        double sr = kRate;
        auto raw = readMono (f, sr);
        if (raw.size() < size_t (kRate)) { std::fprintf (stderr, "  too short %s\n", in.toRawUTF8()); continue; }

        // The tool works at 48 kHz; resample if the take is not.
        std::vector<float> x;
        if (std::abs (sr - kRate) > 1.0)
        {
            const int n = int (double (raw.size()) * kRate / sr);
            x.resize (size_t (n));
            for (int i = 0; i < n; ++i)
            {
                const double t = double (i) * sr / kRate;
                const size_t j = size_t (t);
                const double fr = t - double (j);
                x[size_t (i)] = float ((1.0 - fr) * double (raw[std::min (j, raw.size() - 1)])
                                     + fr        * double (raw[std::min (j + 1, raw.size() - 1)]));
            }
        }
        else x = raw;

        Result r;
        r.name = f.getFileNameWithoutExtension().toStdString();

        // dry reference: depth 0 leaves the signal alone but for the delay
        std::map<std::string, float> dryParams = extra;
        dryParams["depth"] = 0.0f;
        dryParams["autoGain"] = 0.0f;
        const auto dry = render (x, dryParams, block);

        // control: the Init patch
        std::map<std::string, float> defParams = extra;
        const auto wetDef = render (x, defParams, block);

        // learned
        const auto prof = learn (x);
        r.learnedValid = prof.valid;
        r.bands = prof.bandsUsed;
        std::map<std::string, float> learnParams = extra;
        {
            using P = LearnAnalyzer::TakeProfile;
            learnParams["depth"]       = prof.depth;
            learnParams["sharpness"]   = prof.sharpness;
            learnParams["selectivity"] = prof.selectivity;
            learnParams["transient"]   = prof.transient;
            learnParams["maxReduction"] = prof.maxCutDb;
            learnParams["attack"]      = prof.attackMs;
            learnParams["release"]     = prof.releaseMs;
            learnParams["detailTilt"]  = prof.detailTilt;
            learnParams["attackTilt"]  = prof.attackTilt;
            learnParams["releaseTilt"] = prof.releaseTilt;
            (void) sizeof (P);
        }
        for (int b = 0; b < prof.bandsUsed && b < 8; ++b)
        {
            const auto& bd = prof.bands[size_t (b)];
            // The layout names these with an underscore before the index.
            const std::string n = std::to_string (b + 1);
            learnParams["eq_enable_" + n] = bd.on ? 1.0f : 0.0f;
            learnParams["eq_freq_"   + n] = bd.hz;
            learnParams["eq_gain_"   + n] = bd.gainDb;
            learnParams["eq_q_"      + n] = bd.q;
            learnParams["eq_type_"   + n] = float (bd.type);
        }
        auto wetLearn = render (x, learnParams, block);

        // --shelfDb is the positive control: it puts a known cut in the wet
        // signal so the metric can be shown to detect one.
        if (shelfDb != 0.0)
            applyHighShelf (wetLearn, 4000.0, shelfDb, kRate);

        // Measure over the part of the file that is sounding in the dry signal.
        const size_t n = x.size();
        const size_t from = size_t (latency);
        const size_t to = n;

        const auto fDry = frameBands (dry, from, to);
        const auto defB = compareOnSibilants (fDry, frameBands (wetDef, from, to));
        const auto lrnB = compareOnSibilants (fDry, frameBands (wetLearn, from, to));
        r.sibFrames = defB.frames;

        r.sibReduceDefault = defB.sibReduce;
        r.sibReduceLearned = lrnB.sibReduce;

        // Body: the de-esser's range starts at 4 kHz, so movement down here is
        // collateral rather than intent. Both boxy and thin show up in it.
        r.bodyDamageDefault = defB.bodyDamage;
        r.bodyDamageLearned = lrnB.bodyDamage;

        // Reduction per dB of damage. Damage is floored so a take that happens
        // to move nothing does not divide by zero and score as infinite.
        r.scoreDefault = r.sibReduceDefault / std::max (0.25, r.bodyDamageDefault);
        r.scoreLearned = r.sibReduceLearned / std::max (0.25, r.bodyDamageLearned);

        r.depth = prof.depth; r.sharpness = prof.sharpness;
        r.selectivity = prof.selectivity; r.maxCut = prof.maxCutDb;
        r.attack = prof.attackMs; r.release = prof.releaseMs;

        results.push_back (r);
        std::printf ("  %-40s default %+6.2f/%5.2f = %6.2f   learned %+6.2f/%5.2f = %6.2f   %s\n",
                     r.name.c_str(),
                     r.sibReduceDefault, r.bodyDamageDefault, r.scoreDefault,
                     r.sibReduceLearned, r.bodyDamageLearned, r.scoreLearned,
                     r.learnedValid ? "" : "(learn produced nothing)");
    }

    if (results.empty()) return 1;

    double sd = 0, sl = 0, dd = 0, dl = 0;
    int learnedWins = 0;
    for (const auto& r : results)
    {
        sd += r.scoreDefault; sl += r.scoreLearned;
        dd += r.bodyDamageDefault; dl += r.bodyDamageLearned;
        if (r.scoreLearned > r.scoreDefault) ++learnedWins;
    }
    const double n = double (results.size());
    std::printf ("\n  == summary, %d takes ==\n", int (n));
    std::printf ("  mean body damage    default %5.2f dB   learned %5.2f dB\n", dd / n, dl / n);
    std::printf ("  mean score          default %6.2f      learned %6.2f\n", sd / n, sl / n);
    std::printf ("  learned beats default on %d of %d takes\n", learnedWins, int (n));

    if (jsonPath.isNotEmpty())
    {
        std::ofstream os (jsonPath.toRawUTF8());
        os << "{\n  \"latency\": " << latency << ",\n  \"takes\": [\n";
        for (size_t i = 0; i < results.size(); ++i)
        {
            const auto& r = results[i];
            os << "    {\"name\": \"" << r.name << "\", "
               << "\"sibReduceDefault\": " << r.sibReduceDefault << ", "
               << "\"sibReduceLearned\": " << r.sibReduceLearned << ", "
               << "\"bodyDamageDefault\": " << r.bodyDamageDefault << ", "
               << "\"bodyDamageLearned\": " << r.bodyDamageLearned << ", "
               << "\"scoreDefault\": " << r.scoreDefault << ", "
               << "\"scoreLearned\": " << r.scoreLearned << ", "
               << "\"learnedValid\": " << (r.learnedValid ? "true" : "false") << ", "
               << "\"depth\": " << r.depth << ", \"sharpness\": " << r.sharpness
               << ", \"maxCut\": " << r.maxCut << ", \"attack\": " << r.attack
               << ", \"release\": " << r.release << "}"
               << (i + 1 < results.size() ? "," : "") << "\n";
        }
        os << "  ]\n}\n";
        std::printf ("  wrote %s\n", jsonPath.toRawUTF8());
    }
    return 0;
}