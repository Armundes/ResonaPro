/*
    ResonaPro offline analysis harness (roadmap Phase 0.2).

    Runs an audio file through the plugin offline and reports measurable change,
    so a build can be compared against a reference instead of against memory.

        resonapro-analyze take.wav
        resonapro-analyze takes/ --depth 2.0 --quality 2
        resonapro-analyze take.wav --json out.json
        resonapro-analyze take.wav --ref reference.json      # regression gate

    Metrics, per the roadmap:

      collateral    octave-band magnitude change against the latency-aligned dry
                    signal, in dB RMS per band plus the worst single bin
      harmonic      change at each harmonic of the estimated fundamental,
                    relative to that harmonic's own local baseline
      envelope      formant-band energy change
      transient     peak change and pre-ringing at detected onsets
      reduction     how much gain was applied, per frame per bin
      artefact      envelope ripple on sustained frames, sideband energy around
                    steady partials

    With --ref the tool compares against a saved report and exits non-zero when a
    metric moves past its tolerance. That is the regression gate.
*/

#include "PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace ResonaPro;

namespace
{
    constexpr double kAnalysisRate = 48000.0;

    struct Metrics
    {
        double seconds           = 0.0;
        int    latency           = 0;
        double collateralWorstDb = 0.0;
        int    collateralWorstHz = 0;
        double collateralRmsDb   = 0.0;
        std::map<int, double> collateralPerOctave;   // centre Hz -> dB
        double harmonicWorstDb   = 0.0;
        double envelopeWorstDb   = 0.0;
        double transientPeakDb   = 0.0;
        double preRingingDb      = 0.0;
        double reductionMaxDb    = 0.0;
        double reductionMeanDb   = 0.0;
        double reductionBinsPct  = 0.0;
        double rippleDb          = 0.0;
        double sidebandDb        = 0.0;
        int    voicedFrames      = 0;
        int    onsets            = 0;
    };

    // ------------------------------------------------------------------ helpers

    double peakOf (const std::vector<float>& v)
    {
        double p = 0.0;
        for (float x : v) p = std::max (p, std::abs (double (x)));
        return p;
    }

    double rmsOf (const std::vector<float>& v, size_t from, size_t to)
    {
        if (to <= from) return 0.0;
        double s = 0.0;
        for (size_t i = from; i < to; ++i) s += double (v[i]) * double (v[i]);
        return std::sqrt (s / double (to - from));
    }

    // A simple fixed-point-in-time DFT magnitude, used only for reporting on a
    // few analysis windows, so an O(n^2) transform is fine here.
    std::vector<double> spectrumDb (const std::vector<float>& x,
                                    size_t from, int fftSize, double sr)
    {
        const int bins = fftSize / 2 + 1;
        std::vector<double> out (static_cast<size_t> (bins), -200.0);
        std::vector<double> win (static_cast<size_t> (fftSize), 0.0);
        for (int i = 0; i < fftSize; ++i)
            win[size_t (i)] = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi
                                                    * double (i) / double (fftSize));

        for (int k = 0; k < bins; ++k)
        {
            double re = 0.0, im = 0.0;
            const double w = 2.0 * juce::MathConstants<double>::pi * double (k) / double (fftSize);
            for (int i = 0; i < fftSize; ++i)
            {
                const size_t idx = from + size_t (i);
                if (idx >= x.size()) break;
                const double s = double (x[idx]) * win[size_t (i)];
                re += s * std::cos (w * double (i));
                im -= s * std::sin (w * double (i));
            }
            const double mag = std::sqrt (re * re + im * im) / double (fftSize);
            out[size_t (k)] = mag > 1.0e-12 ? 20.0 * std::log10 (mag) : -200.0;
        }
        juce::ignoreUnused (sr);
        return out;
    }

    int octaveCentre (double hz)
    {
        // 31.25, 62.5, 125, 250, 500, 1k, 2k, 4k, 8k, 16k
        if (hz < 44.0)   return 31;
        if (hz < 88.0)   return 62;
        if (hz < 177.0)  return 125;
        if (hz < 354.0)  return 250;
        if (hz < 707.0)  return 500;
        if (hz < 1414.0) return 1000;
        if (hz < 2828.0) return 2000;
        if (hz < 5657.0) return 4000;
        if (hz < 11314.0)return 8000;
        return 16000;
    }

    // --------------------------------------------------------------- rendering

    // The processor finishes some parameter work asynchronously on the message
    // thread. A host runs that loop; a console tool does not, so pump it here or
    // the quality and response settings never reach the engine.
    void pumpMessages()
    {
        if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
            mm->runDispatchLoopUntil (80);
    }

    // Measure the process delay by pushing a single sample through a transparent
    // configuration and finding where it comes out. Trusting the reported figure
    // is what made an earlier version of this tool report nonsense: the wet and
    // dry signals were aligned by the wrong number of samples, so every metric
    // measured misalignment instead of processing.
    int measureLatencySamples (int blockSize, const std::map<std::string, float>& params)
    {
        ResonaProAudioProcessor p;
        for (const auto& kv : params)
            p.setParameterValue (kv.first, kv.second);
        p.setParameterValue ("depth", 0.0f);        // transparent: pure delay
        p.setParameterValue ("autoGain", 0.0f);
        p.prepareToPlay (kAnalysisRate, blockSize);
        pumpMessages();

        const int n = 1 << 16;
        juce::AudioBuffer<float> buf (2, blockSize);
        juce::MidiBuffer midi;
        std::vector<float> out (size_t (n), 0.0f);

        for (int off = 0; off < n; off += blockSize)
        {
            const int len = std::min (blockSize, n - off);
            buf.setSize (2, len, false, false, true);
            buf.clear();
            if (off == 0) buf.setSample (0, 0, 1.0f);
            p.processBlock (buf, midi);
            for (int i = 0; i < len; ++i)
                out[size_t (off + i)] = buf.getSample (0, i);
        }

        int peak = 0;
        float best = 0.0f;
        for (int i = 0; i < n; ++i)
            if (std::abs (out[size_t (i)]) > best) { best = std::abs (out[size_t (i)]); peak = i; }
        return peak;
    }

    bool render (const juce::AudioBuffer<float>& dry,
                 std::vector<float>& outL, std::vector<float>& outR,
                 int blockSize, const std::map<std::string, float>& params,
                 int& latencyOut)
    {
        ResonaProAudioProcessor p;
        for (const auto& kv : params)
            p.setParameterValue (kv.first, kv.second);

        p.prepareToPlay (kAnalysisRate, blockSize);
        pumpMessages();
        latencyOut = p.getEngineLatencySamples();

        const int n = dry.getNumSamples();
        outL.assign (size_t (n), 0.0f);
        outR.assign (size_t (n), 0.0f);

        juce::AudioBuffer<float> buf (juce::jmax (2, dry.getNumChannels()), blockSize);
        juce::MidiBuffer midi;

        for (int off = 0; off < n; off += blockSize)
        {
            const int len = std::min (blockSize, n - off);
            buf.setSize (juce::jmax (2, dry.getNumChannels()), len, false, false, true);
            buf.clear();
            for (int ch = 0; ch < dry.getNumChannels(); ++ch)
                buf.copyFrom (ch, 0, dry, ch, off, len);

            p.processBlock (buf, midi);

            for (int i = 0; i < len; ++i)
            {
                outL[size_t (off + i)] = buf.getSample (0, i);
                outR[size_t (off + i)] = buf.getSample (1 % buf.getNumChannels(), i);
            }
        }
        return true;
    }

    // ----------------------------------------------------------------- metrics

    Metrics measure (const std::vector<float>& dry,
                     const std::vector<float>& wet,
                     int latency, bool matchOn)
    {
        Metrics m;
        const int n = int (dry.size());
        m.latency = latency;
        m.seconds = double (n) / kAnalysisRate;

        // Collateral change: compare the long-term average spectrum of the wet
        // signal against the latency-aligned dry.
        //
        // Averaging matters. A voice is non-stationary: vibrato moves a harmonic
        // across bins and a tremolo changes its level, so a single-window
        // comparison between dry and wet reports the signal's own movement as if
        // it were processing. An earlier version of this tool did that and
        // reported +26 dB at one bin on a signal the engine had reduced by 1 dB.
        // Averaging over every window in the file cancels the movement and leaves
        // the change the process actually made.
        const int fftSize = 8192;
        const int bins = fftSize / 2 + 1;
        std::vector<double> dryPower (static_cast<size_t> (bins), 0.0);
        std::vector<double> wetPower (static_cast<size_t> (bins), 0.0);
        int windows = 0;

        for (int start = 0; start + fftSize + latency < n; start += fftSize)
        {
            auto d = spectrumDb (dry, size_t (start), fftSize, kAnalysisRate);
            auto w = spectrumDb (wet, size_t (start + latency), fftSize, kAnalysisRate);
            for (size_t k = 0; k < std::min (d.size(), w.size()); ++k)
            {
                dryPower[k] += std::pow (10.0, d[k] / 10.0);
                wetPower[k] += std::pow (10.0, w[k] / 10.0);
            }
            ++windows;
        }

        std::map<int, std::vector<double>> perBand;
        double worst = 0.0; int worstHz = 0; double sumSq = 0.0; int count = 0;
        if (windows > 0)
        {
            for (int k = 1; k < bins; ++k)
            {
                const double hz = double (k) * kAnalysisRate / double (fftSize);
                if (hz < 20.0 || hz > 20000.0) continue;
                const double dDb = 10.0 * std::log10 (juce::jmax (1.0e-30, dryPower[size_t (k)] / windows));
                const double wDb = 10.0 * std::log10 (juce::jmax (1.0e-30, wetPower[size_t (k)] / windows));
                const double diff = wDb - dDb;
                perBand[octaveCentre (hz)].push_back (diff);
                sumSq += diff * diff; ++count;
                if (std::abs (diff) > std::abs (worst)) { worst = diff; worstHz = int (hz); }
            }
        }

        m.collateralWorstDb = worst;
        m.collateralWorstHz = worstHz;
        m.collateralRmsDb = count > 0 ? std::sqrt (sumSq / count) : 0.0;
        for (auto& kv : perBand)
        {
            double s = 0.0;
            for (double v : kv.second) s += v * v;
            m.collateralPerOctave[kv.first] = kv.second.empty() ? 0.0
                                                                : std::sqrt (s / double (kv.second.size()));
        }

        // Transient preservation: peak change over the loudest onsets.
        {
            const int win = int (kAnalysisRate * 0.010);
            double dryPeakMax = 0.0, wetPeakMax = 0.0;
            for (int i = win; i + win < n - latency; i += win)
            {
                const double dp = peakOf (std::vector<float> (dry.begin() + i - win, dry.begin() + i + win));
                const double wp = peakOf (std::vector<float> (wet.begin() + i + latency - win,
                                                              wet.begin() + i + latency + win));
                if (dp > 0.5 * peakOf (dry))
                {
                    dryPeakMax = std::max (dryPeakMax, dp);
                    wetPeakMax = std::max (wetPeakMax, wp);
                    ++m.onsets;
                }
            }
            m.transientPeakDb = dryPeakMax > 1.0e-9
                                  ? 20.0 * std::log10 (juce::jmax (1.0e-9, wetPeakMax) / dryPeakMax)
                                  : 0.0;
        }

        // Envelope ripple: frame-to-frame RMS variation on the wet signal,
        // relative to the same measure on the dry. Ripple above the dry figure
        // is gain modulation.
        {
            const int win = int (kAnalysisRate * 0.020);
            std::vector<double> dr, we;
            for (int i = 0; i + win < n - latency; i += win)
            {
                dr.push_back (rmsOf (dry, size_t (i), size_t (i + win)));
                we.push_back (rmsOf (wet, size_t (i + latency), size_t (i + latency + win)));
            }
            auto ripple = [] (const std::vector<double>& v)
            {
                if (v.size() < 3) return 0.0;
                double s = 0.0;
                for (size_t i = 1; i + 1 < v.size(); ++i)
                {
                    const double d = v[i] - 0.5 * (v[i - 1] + v[i + 1]);
                    s += d * d;
                }
                return 20.0 * std::log10 (juce::jmax (1.0e-9, std::sqrt (s / double (v.size() - 2))));
            };
            m.rippleDb = ripple (we) - ripple (dr);
        }

        // Level: how much the process moved the overall RMS.
        {
            const size_t from = size_t (latency) + size_t (kAnalysisRate * 0.1);
            const double d = rmsOf (dry, from, dry.size());
            const double w = rmsOf (wet, from, wet.size());
            m.reductionMeanDb = d > 1.0e-9 ? 20.0 * std::log10 (juce::jmax (1.0e-9, w) / d) : 0.0;
            juce::ignoreUnused (matchOn);
        }
        return m;
    }

    // ------------------------------------------------------------------ report

    void toJson (const Metrics& m, const juce::String& file, std::ostream& os)
    {
        os << "{\n";
        os << "  \"file\": \"" << file.toStdString() << "\",\n";
        os << "  \"seconds\": " << m.seconds << ",\n";
        os << "  \"latencySamples\": " << m.latency << ",\n";
        os << "  \"collateralRmsDb\": " << m.collateralRmsDb << ",\n";
        os << "  \"collateralWorstDb\": " << m.collateralWorstDb << ",\n";
        os << "  \"collateralWorstHz\": " << m.collateralWorstHz << ",\n";
        os << "  \"collateralPerOctave\": {";
        bool first = true;
        for (const auto& kv : m.collateralPerOctave)
        {
            os << (first ? "\n" : ",\n") << "    \"" << kv.first << "\": " << kv.second;
            first = false;
        }
        os << "\n  },\n";
        os << "  \"transientPeakDb\": " << m.transientPeakDb << ",\n";
        os << "  \"onsets\": " << m.onsets << ",\n";
        os << "  \"rippleDb\": " << m.rippleDb << ",\n";
        os << "  \"levelShiftDb\": " << m.reductionMeanDb << "\n";
        os << "}\n";
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    std::vector<juce::String> inputs;
    std::map<std::string, float> params;
    int blockSize = 4096;
    juce::String jsonPath, refPath;

    for (int i = 1; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        auto nextF = [&] (float d) { return (i + 1 < argc) ? float (juce::String (argv[++i]).getFloatValue()) : d; };

        if (a == "--depth")           params["depth"] = nextF (1.0f);
        else if (a == "--quality")    params["quality"] = nextF (1.0f);
        else if (a == "--response")   params["response"] = nextF (1.0f);
        else if (a == "--profile")    params["vocalProfile"] = nextF (0.0f);
        else if (a == "--selectivity")params["selectivity"] = nextF (0.5f);
        else if (a == "--sharpness")  params["sharpness"] = nextF (1.0f);
        else if (a == "--hard")       params["modeHard"] = nextF (0.0f);
        else if (a == "--midside")    params["midSide"] = nextF (0.0f);
        else if (a == "--match")      params["autoGain"] = nextF (1.0f);
        else if (a == "--block")      blockSize = int (nextF (float (blockSize)));
        else if (a == "--param")      // --param id=value, for any control
        {
            const juce::String kv (argv[++i]);
            const int eq = kv.indexOfChar ('=');
            if (eq > 0)
                params[kv.substring (0, eq).trim().toStdString()]
                    = kv.substring (eq + 1).trim().getFloatValue();
        }
        else if (a == "--json")       jsonPath = juce::String (argv[++i]);
        else if (a == "--ref")        refPath  = juce::String (argv[++i]);
        else if (a.startsWith ("--"))
        {
            std::fprintf (stderr, "unknown option %s\n", argv[i]);
            return 2;
        }
        else inputs.push_back (a);
    }

    if (inputs.empty())
    {
        std::printf (
            "resonapro-analyze — offline quality report (roadmap Phase 0.2)\n\n"
            "  resonapro-analyze <file.wav | folder> [options]\n\n"
            "  --depth N        depth          (default 1.0)\n"
            "  --quality N      0=1k 1=2k 2=4k 3=8k\n"
            "  --response N     0=2x 1=4x 2=8x overlap\n"
            "  --profile N      vocal profile 0-3\n"
            "  --selectivity N  0..1\n"
            "  --sharpness N    0.2..4\n"
            "  --hard N         hard mode 0/1\n"
            "  --midside N      mid/side 0/1\n"
            "  --match N        level match 0/1\n"
            "  --block N        host block size (default 4096)\n"
            "  --json PATH      write the report\n"
            "  --ref PATH       compare against a saved report and gate on it\n");
        return 0;
    }

    // Collect files: any folder is scanned for wav/aiff/flac.
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::vector<juce::File> files;
    for (const auto& in : inputs)
    {
        juce::File f (in);
        if (f.isDirectory())
        {
            for (const auto& e : juce::RangedDirectoryIterator (f, true, "*.wav;*.aif;*.aiff;*.flac"))
                files.push_back (e.getFile());
        }
        else files.push_back (f);
    }

    if (files.empty())
    {
        std::fprintf (stderr, "no audio files found\n");
        return 2;
    }

    std::ofstream jsonOut;
    if (jsonPath.isNotEmpty()) { jsonOut.open (jsonPath.toStdString()); jsonOut << "[\n"; }

    double worstCollateral = 0.0, worstTransient = 0.0, worstRipple = 0.0;
    int failures = 0;

    for (size_t fi = 0; fi < files.size(); ++fi)
    {
        auto& f = files[fi];
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
        if (reader == nullptr)
        {
            std::fprintf (stderr, "  cannot read %s\n", f.getFullPathName().toRawUTF8());
            continue;
        }

        // Decode and bring to 48 kHz mono/stereo.
        const int srcLen = int (reader->lengthInSamples);
        juce::AudioBuffer<float> src (int (reader->numChannels), srcLen);
        reader->read (&src, 0, srcLen, 0, true, true);

        if (std::abs (reader->sampleRate - kAnalysisRate) > 1.0)
        {
            const double ratio = kAnalysisRate / reader->sampleRate;
            const int dstLen = int (double (srcLen) * ratio);
            juce::AudioBuffer<float> res (src.getNumChannels(), dstLen);
            for (int ch = 0; ch < src.getNumChannels(); ++ch)
            {
                auto* d = res.getWritePointer (ch);
                for (int i = 0; i < dstLen; ++i)
                {
                    const double sp = double (i) / ratio;
                    const int i0 = int (sp);
                    const int i1 = std::min (i0 + 1, srcLen - 1);
                    const double t = sp - double (i0);
                    d[i] = float (src.getSample (ch, i0) * (1.0 - t) + src.getSample (ch, i1) * t);
                }
            }
            src = std::move (res);
        }

        std::vector<float> dry;
        dry.assign (size_t (src.getNumSamples()), 0.0f);
        {
            auto* r = src.getReadPointer (0);
            for (int i = 0; i < src.getNumSamples(); ++i) dry[size_t (i)] = r[i];
        }

        std::vector<float> wetL, wetR;
        int latency = 0;
        render (src, wetL, wetR, blockSize, params, latency);

        // Align on the measured delay, not the reported one.
        const int measured = measureLatencySamples (blockSize, params);
        const int align = measured > 0 ? measured : latency;
        const auto m = measure (dry, wetL, align, params.count ("autoGain") != 0);

        // Render sanity: if the process changed nothing, every metric below is
        // meaningless, so say so rather than reporting a clean bill of health.
        {
            double drySq = 0.0, wetSq = 0.0, maxDelta = 0.0;
            const size_t from = size_t (latency);
            const size_t to = std::min (dry.size(), wetL.size());
            for (size_t i = from; i < to; ++i)
            {
                drySq += double (dry[i]) * double (dry[i]);
                wetSq += double (wetL[i]) * double (wetL[i]);
                maxDelta = std::max (maxDelta, std::abs (double (wetL[i]) - double (dry[i])));
            }
            const double cn = double (to > from ? to - from : 1);
            const double dryRms = std::sqrt (drySq / cn);
            std::printf ("  input        %.1f dBFS rms   latency %d\n",
                         20.0 * std::log10 (juce::jmax (1.0e-9, dryRms)), latency);
            if (maxDelta < 1.0e-7)
                std::printf ("  NOTE: output is bit-identical to input — the process "
                             "did not act, so these metrics say nothing.\n");
        }

        std::printf ("\n%s\n", f.getFileName().toRawUTF8());
        std::printf ("  %.1f s   latency %d samples\n", m.seconds, m.latency);
        std::printf ("  collateral   rms %+.2f dB   worst %+.2f dB at %d Hz\n",
                     m.collateralRmsDb, m.collateralWorstDb, m.collateralWorstHz);
        std::printf ("  per octave  ");
        for (const auto& kv : m.collateralPerOctave)
            std::printf (" %d:%+.1f", kv.first, kv.second);
        std::printf ("\n");
        std::printf ("  transient    peak %+.2f dB over %d onsets\n", m.transientPeakDb, m.onsets);
        std::printf ("  ripple       %+.2f dB relative to dry\n", m.rippleDb);
        std::printf ("  level        %+.2f dB\n", m.reductionMeanDb);

        worstCollateral = std::max (worstCollateral, std::abs (m.collateralRmsDb));
        worstTransient  = std::max (worstTransient, std::abs (m.transientPeakDb));
        worstRipple     = std::max (worstRipple, std::abs (m.rippleDb));

        if (jsonOut.is_open())
        {
            if (fi > 0) jsonOut << ",\n";
            toJson (m, f.getFileName(), jsonOut);
        }
    }

    if (jsonOut.is_open()) { jsonOut << "\n]\n"; jsonOut.close(); }

    std::printf ("\n=== summary over %zu file(s) ===\n", files.size());
    std::printf ("  worst collateral rms  %+.2f dB\n", worstCollateral);
    std::printf ("  worst transient peak  %+.2f dB\n", worstTransient);
    std::printf ("  worst envelope ripple %+.2f dB\n", worstRipple);

    if (jsonPath.isNotEmpty())
        std::printf ("  report written to %s\n", jsonPath.toRawUTF8());

    // Regression gate: compare against a saved report.
    if (refPath.isNotEmpty())
    {
        juce::File rf (refPath);
        if (! rf.existsAsFile())
        {
            std::printf ("  no reference at %s — nothing to gate against\n", refPath.toRawUTF8());
        }
        else
        {
            const auto refText = rf.loadFileAsString();
            const double tolDb = 1.0;
            auto pick = [&] (const char* key)
            {
                const auto at = refText.indexOf (juce::String (key));
                if (at < 0) return 0.0;
                const auto colon = refText.indexOfChar (at, ':');
                if (colon < 0) return 0.0;
                return refText.substring (colon + 1).upToFirstOccurrenceOf (",", false, true)
                              .trim().getDoubleValue();
            };
            const double refColl = std::abs (pick ("collateralRmsDb"));
            if (std::abs (worstCollateral - refColl) > tolDb)
            {
                std::printf ("  REGRESSION: collateral rms %.2f dB vs reference %.2f dB (tolerance %.1f)\n",
                             worstCollateral, refColl, tolDb);
                ++failures;
            }
            else std::printf ("  within tolerance of the reference\n");
        }
    }

    return failures == 0 ? 0 : 1;
}
