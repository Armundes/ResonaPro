#pragma once
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>

namespace ResonaPro
{
    /** Summarises a played vocal without touching the audio thread.

        Call addFrame from the editor timer while the user plays a phrase. A peak
        needs to recur in several frames; transient and random noise spikes will
        not survive that test.

        Two things come out of this. `suggestions()` returns the focus bands, as
        it always did. `profile()` returns a full read of the take: where its
        peaks are, how wide they are, how far apart, how deep, how fast the
        problems arrive and how long they ring, how tilted the spectrum is, and
        how many hard onsets there are. See docs/LEARN-DESIGN.md.

        Nothing here is applied. The user reviews every row and presses APPLY, and
        a test asserts that the ids this can reach are inside the LEARN fence.
    */
    class LearnAnalyzer
    {
    public:
        static constexpr size_t Points = 288;

        // The editor polls the scope at 20 Hz. Frame counts become milliseconds
        // through this, which is why the timings below are approximate.
        static constexpr float kFrameHz = 20.0f;
        static constexpr float kMsPerFrame = 1000.0f / kFrameHz;

        // log spacing of the 288 points, in octaves per point
        static constexpr float kOctPerPoint = 9.9658f / float (Points - 1);

        // Event state machine. kMinOnsetFrames is a debounce: at 20 Hz two
        // frames is 100 ms, and anything shorter is chatter rather than an
        // onset. Without it, material sitting near the threshold counted as a
        // stream of events.
        static constexpr float kOnsetDb        = 3.0f;
        static constexpr float kEndDb          = 1.0f;
        static constexpr int   kMinOnsetFrames = 2;

        // A histogram of per-frame excess, so a percentile can be taken without
        // keeping every frame. 36,000 frames is 144 KB as floats and 1 KB here.
        static constexpr int   kHistBins  = 256;
        static constexpr float kHistMaxDb = 32.0f;

        struct Candidate { float frequency, sensitivityDb, persistence; };

        struct Band
        {
            bool  on      = false;
            float hz      = 1000.0f;
            float gainDb  = 0.0f;
            float q       = 2.0f;
            int   type    = 0;      // 0 Bell, 3 Low Shelf, 4 High Shelf, 5 Band Pass
            float widthOct = 0.0f;
        };

        /** Everything the take had to say. Every field is a proposal, not a
            change; none of it reaches the audio until the user accepts it.
        */
        struct TakeProfile
        {
            bool valid = false;
            std::array<Band, 8> bands {};
            int  bandsUsed = 0;

            // global proposals
            float depth       = 1.0f;
            float sharpness   = 5.4f;
            float selectivity = 0.5f;
            float transient   = 0.5f;
            float maxCutDb    = 18.0f;
            float attackMs    = 8.0f;
            float releaseMs   = 70.0f;
            float detailTilt  = 0.0f;
            float attackTilt  = 0.0f;
            float releaseTilt = 0.0f;

            // what was actually measured, so the review can explain itself
            float worstExcessDb  = 0.0f;
            float excessP95Db    = 0.0f;
            float meanExcessDb   = 0.0f;
            float peakWidthOct   = 0.0f;
            float peakSpacingOct = 0.0f;
            float tiltDbPerOct   = 0.0f;
            float onsetMs        = 0.0f;
            float ringMs         = 0.0f;
            float onsetsPerSec   = 0.0f;
        };

        void reset() noexcept
        {
            sum.fill (0.0f);
            hits.fill (0);
            widthSum.fill (0.0f);
            widthCount.fill (0);
            frames = 0;
            excessMax = 0.0f;
            excessSum = 0.0f;
            excessFrames = 0;
            excessHist.fill (0);
            excessHistCount = 0;
            tiltSum = 0.0f; tiltFrames = 0;
            prevExcess = 0.0f;
            riseFrames = 0; attackSum = 0; attackCount = 0;
            decayFrames = 0; ringSum = 0; ringCount = 0;
            onsetCount = 0;
            eventActive = false;
        }

        int frameCount() const noexcept { return frames; }

        static float frequencyAt (size_t i) noexcept
        {
            return 20.0f * std::pow (1000.0f, static_cast<float> (i) / static_cast<float> (Points - 1));
        }

        void addFrame (const std::array<float, Points>& magnitudeDb,
                       const std::array<float, Points>& baselineDb, float referenceDb) noexcept
        {
            if (++frames > 36000) { --frames; return; }  // max ~30 minutes at 20 Hz

            // A local spectral prominence must exceed the adaptive reference.
            // Ignore silent frames and only count local maxima.
            float frameExcess = 0.0f;
            for (size_t i = 3; i + 3 < Points; ++i)
            {
                const float hz = frequencyAt (i);
                if (hz < 150.0f || hz > 12000.0f || magnitudeDb[i] < -75.0f)
                    continue;
                const float excess = magnitudeDb[i] - baselineDb[i] - referenceDb - 2.0f;
                frameExcess = std::max (frameExcess, excess);
                if (excess <= 1.5f) continue;
                bool peak = true;
                for (int d = -3; d <= 3; ++d)
                    if (d != 0 && magnitudeDb[static_cast<size_t> (static_cast<int> (i) + d)] > magnitudeDb[i])
                        peak = false;
                if (! peak) continue;
                hits[i]++;
                sum[i] += std::min (16.0f, excess);

                // How wide is this peak at -3 dB? This is the measurement that
                // decides DETAIL and each band's Q. A narrow spike needs a sharp
                // cut or it smears across the formant; a broad bump needs a wide
                // one or the cut misses it.
                int lo = int (i), hi = int (i);
                while (lo > int (i) - 10 && magnitudeDb[size_t (lo - 1)] > magnitudeDb[i] - 3.0f) --lo;
                while (hi < int (i) + 10 && magnitudeDb[size_t (hi + 1)] > magnitudeDb[i] - 3.0f) ++hi;
                widthSum[i] += float (hi - lo) * kOctPerPoint;
                widthCount[i]++;
            }

            // ---- how deep the take's problems get ----
            excessSum += std::max (0.0f, frameExcess);
            excessFrames++;
            excessMax = std::max (excessMax, frameExcess);

            // ---- how tilted the spectrum is, 300 Hz to 6 kHz ----
            // A dark take tilts steeply downward and the detector is looking at a
            // sloped spectrum, which is what DETAIL TILT compensates for.
            {
                double sx = 0, sy = 0, sxx = 0, sxy = 0;
                int n = 0;
                for (size_t i = 3; i + 3 < Points; ++i)
                {
                    const float hz = frequencyAt (i);
                    if (hz < 300.0f || hz > 6000.0f || magnitudeDb[i] < -90.0f)
                        continue;
                    const double x = std::log2 (double (hz));
                    const double y = double (magnitudeDb[i]);
                    sx += x; sy += y; sxx += x * x; sxy += x * y; ++n;
                }
                if (n > 32)
                {
                    const double den = double (n) * sxx - sx * sx;
                    if (std::abs (den) > 1.0e-9)
                    {
                        tiltSum += float ((double (n) * sxy - sx * sy) / den);
                        tiltFrames++;
                    }
                }
            }

            // ---- how fast the problems arrive and how long they ring ----
            //
            // Two states with a debounce. The previous version flipped on a bare
            // threshold crossing with no minimum length, and it reset its attack
            // accumulator on the line before reading it, so the attack time it
            // reported was never a measurement at all -- it was the 8 ms default
            // every time. Its decay counter was never reset either, so it grew
            // for the whole take and pushed the release into its ceiling.
            //
            // riseFrames    how long the excess climbs from the floor to a real
            //               onset. Short means the problem strikes.
            // decayFrames   how long it takes to fall back. Long means it rings.
            if (! eventActive)
            {
                if (frameExcess > kEndDb)
                {
                    ++riseFrames;
                    if (frameExcess > kOnsetDb && riseFrames >= kMinOnsetFrames)
                    {
                        attackSum += riseFrames;
                        ++attackCount;
                        ++onsetCount;
                        eventActive = true;
                        riseFrames  = 0;
                        decayFrames = 0;
                    }
                }
                else
                {
                    riseFrames = 0;   // fell back before it qualified: debris
                }
            }
            else
            {
                ++decayFrames;
                if (frameExcess < kEndDb)
                {
                    ringSum += decayFrames;
                    ++ringCount;
                    eventActive = false;
                    decayFrames = 0;
                }
            }

            // ---- the distribution of excess, for percentile depth ----
            excessHist[static_cast<size_t> (std::clamp (
                int (frameExcess / kHistMaxDb * float (kHistBins - 1)),
                0, kHistBins - 1))]++;
            ++excessHistCount;

            prevExcess = frameExcess;
        }

        std::vector<Candidate> suggestions() const
        {
            std::vector<Candidate> pool, selected;
            if (frames < 20) return selected;  // at least one second of evidence
            for (size_t i = 3; i + 3 < Points; ++i)
            {
                const float persistence = static_cast<float> (hits[i]) / static_cast<float> (frames);
                if (persistence < 0.10f) continue;
                const float score = sum[i] / static_cast<float> (frames);
                if (score < 0.45f) continue;
                pool.push_back ({ frequencyAt (i), std::clamp (score * 2.0f, 1.0f, 6.0f), persistence });
            }
            std::sort (pool.begin(), pool.end(), [] (const Candidate& a, const Candidate& b)
            { return a.sensitivityDb * a.persistence > b.sensitivityDb * b.persistence; });
            for (const auto& c : pool)
            {
                bool tooClose = false;
                for (const auto& existing : selected)
                    if (std::abs (std::log2 (c.frequency / existing.frequency)) < 0.22f)
                        tooClose = true;
                if (! tooClose) selected.push_back (c);
                if (selected.size() == 8) break;
            }
            std::sort (selected.begin(), selected.end(), [] (const Candidate& a, const Candidate& b)
            { return a.frequency < b.frequency; });
            return selected;
        }

        /** The full read of the take. Every value is a suggestion inside the
            LEARN fence; see ResonaProAudioProcessor::isLearnEditable.
        */
        TakeProfile profile() const
        {
            TakeProfile out;
            if (frames < 20)
                return out;

            const auto found = suggestions();

            // ---- what was measured ----
            out.worstExcessDb = excessMax;
            out.meanExcessDb  = excessFrames > 0 ? excessSum / float (excessFrames) : 0.0f;
            out.tiltDbPerOct  = tiltFrames > 0 ? tiltSum / float (tiltFrames) : 0.0f;
            out.onsetsPerSec  = float (onsetCount) / (float (frames) / kFrameHz);
            out.onsetMs       = attackCount > 0 ? (float (attackSum) / float (attackCount)) * kMsPerFrame : 0.0f;
            out.ringMs        = ringCount   > 0 ? (float (ringSum)   / float (ringCount))   * kMsPerFrame : 0.0f;

            // median peak width over the points that were ever peaks
            {
                // Weighted by how often each point was a peak. The previous
                // version gave a spike that appeared once the same weight as a
                // peak that persisted for the whole take.
                double wsum = 0.0, wgt = 0.0;
                for (size_t i = 0; i < Points; ++i)
                    if (widthCount[i] > 0 && hits[i] > 0)
                    {
                        const double w = double (hits[i]);
                        wsum += w * double (widthSum[i] / float (widthCount[i]));
                        wgt  += w;
                    }
                out.peakWidthOct = wgt > 0.0 ? float (wsum / wgt) : 0.0f;
            }

            // median spacing between the selected peaks, in octaves
            if (found.size() >= 2)
            {
                float ssum = 0.0f;
                for (size_t i = 1; i < found.size(); ++i)
                    ssum += std::abs (std::log2 (found[i].frequency / found[i - 1].frequency));
                out.peakSpacingOct = ssum / float (found.size() - 1);
            }

            // ---- the bands themselves ----
            out.bandsUsed = int (found.size());
            for (size_t i = 0; i < found.size() && i < 8; ++i)
            {
                Band b;
                b.on      = true;
                b.hz      = found[i].frequency;
                b.gainDb  = found[i].sensitivityDb;
                b.widthOct = peakWidthAt (found[i].frequency);
                // Q from the measured width: a peak 0.3 octaves wide wants Q ~ 3.
                b.q       = std::clamp (1.0f / std::max (0.08f, b.widthOct), 0.4f, 8.0f);
                b.type    = filterTypeFor (b.hz, b.widthOct, found[i].sensitivityDb);
                out.bands[i] = b;
            }

            // ---- the global proposals ----
            //
            // DEPTH follows the 95th percentile of the take's excess, not its
            // maximum. One cough or plosive used to set the depth and the
            // reduction ceiling for the whole take: the per-band accumulation
            // capped at 16 dB but the maximum did not, so a single outlier frame
            // could drive both to their limits.
            const float depthExcess = excessP95();
            out.excessP95Db = depthExcess;
            out.depth = std::clamp (depthExcess / 4.0f, 0.4f, 3.2f);

            // DETAIL follows how sharp the peaks are. A 0.1 octave spike needs a
            // sharp cut; a 0.8 octave hump needs a blunt one.
            out.sharpness = std::clamp (2.0f + (0.45f - out.peakWidthOct) * 14.0f, 1.0f, 9.5f);

            // HOW NARROW follows how close the peaks are. Closely spaced peaks
            // need a narrow analysis window or the detector averages them into
            // one; widely spaced peaks tolerate a wider one.
            out.selectivity = found.size() >= 2
                            ? std::clamp (1.0f - (out.peakSpacingOct - 0.15f) / 0.85f, 0.15f, 0.9f)
                            : 0.5f;

            // MAX CUT has to be able to reach the worst peak.
            out.maxCutDb = std::clamp (depthExcess * 1.6f, 6.0f, 30.0f);

            // TRANSIENT from how many hard onsets the take has. A take full of
            // plosives wants protection; a legato take does not, and setting it
            // high anyway costs definition.
            out.transient = std::clamp (0.25f + out.onsetsPerSec / 12.0f, 0.25f, 0.9f);

            // ATTACK and RELEASE from what the take actually does. Three
            // quarters of the observed rise, three fifths of the observed ring:
            // fast enough to catch the problem, slow enough not to chatter.
            out.attackMs  = std::clamp (out.onsetMs > 0.0f ? out.onsetMs * 0.75f : 8.0f, 1.0f, 30.0f);
            out.releaseMs = std::clamp (out.ringMs  > 0.0f ? out.ringMs  * 0.60f : 70.0f, 15.0f, 300.0f);

            // DETAIL TILT from the take's own slope. A natural vocal falls about
            // 6 dB per octave; darker than that and the detector is looking at a
            // sloped spectrum, which this compensates for.
            out.detailTilt = std::clamp ((-out.tiltDbPerOct - 6.0f) / 6.0f, -1.0f, 1.0f);

            // The tilts are left alone unless the take gives a clear reason. A
            // release that is much longer than the onset means the problems ring
            // rather than strike, so the release side wants the gentler slope.
            const float ringRatio = out.onsetMs > 0.1f ? out.ringMs / out.onsetMs : 1.0f;
            out.releaseTilt = std::clamp ((ringRatio - 3.0f) / 9.0f, 0.0f, 0.7f);
            out.attackTilt  = std::clamp ((3.0f - ringRatio) / 9.0f, 0.0f, 0.5f);

            out.valid = true;
            return out;
        }

    private:
        float peakWidthAt (float hz) const noexcept
        {
            // nearest analysis point
            const float t = std::log (hz / 20.0f) / std::log (1000.0f) * float (Points - 1);
            const int idx = std::clamp (int (t + 0.5f), 0, int (Points) - 1);
            float wsum = 0.0f; int wn = 0;
            for (int d = -3; d <= 3; ++d)
            {
                const int j = idx + d;
                if (j < 0 || j >= int (Points) || widthCount[size_t (j)] == 0) continue;
                wsum += widthSum[size_t (j)] / float (widthCount[size_t (j)]);
                ++wn;
            }
            return wn > 0 ? wsum / float (wn) : 0.35f;
        }

        /** Which filter shape fits this peak. A peak at the very bottom or top of
            the range with a one-sided skirt is a shelf, not a bell. A very narrow
            isolated spike is better served by a band pass. Everything else is a
            bell. This is the difference between a proposal that looks plausible
            in a table and one that sounds right.
        */
        static int filterTypeFor (float hz, float widthOct, float gainDb) noexcept
        {
            if (hz < 110.0f)  return 3;   // Low Shelf
            if (hz > 9500.0f) return 4;   // High Shelf
            if (widthOct < 0.12f && gainDb > 3.0f) return 5;  // Band Pass
            return 0;                     // Bell
        }

        std::array<float, Points> sum {};
        std::array<int, Points> hits {};
        std::array<float, Points> widthSum {};
        std::array<int, Points> widthCount {};
        int frames = 0;

        float excessMax = 0.0f, excessSum = 0.0f;
        int   excessFrames = 0;
        float tiltSum = 0.0f;
        int   tiltFrames = 0;

        float prevExcess = 0.0f;

        // Completed event measurements. attackSum/attackCount hold the rise
        // times of events that qualified; ringSum/ringCount hold how long they
        // took to fall back.
        int   riseFrames = 0, attackSum = 0, attackCount = 0;
        int   decayFrames = 0, ringSum = 0, ringCount = 0;
        int   onsetCount = 0;
        bool  eventActive = false;

        std::array<int, kHistBins> excessHist {};
        int excessHistCount = 0;

        /** The 95th percentile of per-frame excess over the take. */
        float excessP95() const noexcept
        {
            if (excessHistCount == 0) return 0.0f;
            const int target = std::max (1, int (float (excessHistCount) * 0.95f));
            int acc = 0;
            for (int b = 0; b < kHistBins; ++b)
            {
                acc += excessHist[size_t (b)];
                if (acc >= target)
                    return float (b) / float (kHistBins - 1) * kHistMaxDb;
            }
            return kHistMaxDb;
        }
    };
}
