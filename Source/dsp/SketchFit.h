// holterdipolter Filter - EQ Sketch
//
// Turns a curve drawn by hand into a handful of bands. Pure C++17, no JUCE.
//
// The drawing is read as "this is how I want the level to change over
// frequency". Bands are placed one at a time, each on what is still missing
// the most: a bell on a hump or a dip, a shelf where the drawing stays up or
// down all the way to the end of the spectrum.

#pragma once

#include <utility>
#include <vector>

#include "FilterDesign.h"

namespace hdp
{

struct SketchBand
{
    FilterType type = FilterType::Bell;
    double freq = 1000.0, gainDb = 0.0, q = 1.0;
};

namespace sketch
{
    constexpr int kGridPoints = 240;
    constexpr double kLowHz = 20.0, kHighHz = 20000.0;
    constexpr double kIgnoreDb = 0.75;      // what is left below this (or a fifth of the drawing's height) gets no band
    constexpr double kMatchLeastDb = 1.5;   // a match is about tone in broad strokes: smaller differences stay
    constexpr double kEndIsFlatDb = 1.5;    // a drawing that ends closer to 0 dB than this returns to 0 dB

    inline double gridFreq (int i)
    {
        return kLowHz * std::pow (kHighHz / kLowHz, (double) i / (kGridPoints - 1));
    }

    /** The drawing as a level for every point of the grid.
        Beyond its ends it carries on level if it ended well away from 0 dB (a shelf
        was meant), and returns to 0 dB otherwise. */
    inline std::vector<double> targetFromPoints (std::vector<std::pair<double, double>> points)
    {
        std::vector<double> target ((size_t) kGridPoints, 0.0);

        if (points.size() < 2)
            return target;

        std::stable_sort (points.begin(), points.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });

        const double first = points.front().first, last = points.back().first;
        const double startDb = points.front().second, endDb = points.back().second;

        // Beyond the drawing: an end well away from 0 dB carries on level, a small
        // leftover runs out to 0 dB within half an octave instead of leaving a step.
        const auto beyond = [] (double endValue, double octavesAway)
        {
            return std::abs (endValue) >= kEndIsFlatDb ? endValue
                                                       : endValue * std::max (0.0, 1.0 - octavesAway / 0.5);
        };

        size_t k = 0;

        for (int i = 0; i < kGridPoints; ++i)
        {
            const double f = gridFreq (i);

            if (f <= first)      { target[(size_t) i] = beyond (startDb, std::log2 (first / f)); continue; }
            if (f >= last)       { target[(size_t) i] = beyond (endDb, std::log2 (f / last));   continue; }

            while (k + 1 < points.size() && points[k + 1].first < f)
                ++k;

            const auto& a = points[k];
            const auto& b = points[std::min (k + 1, points.size() - 1)];
            const double span = std::log (b.first / a.first);
            const double t = span > 1.0e-9 ? std::log (f / a.first) / span : 0.0;
            target[(size_t) i] = a.second + std::clamp (t, 0.0, 1.0) * (b.second - a.second);
        }

        // A hand is not steady; take the jitter out.
        for (int pass = 0; pass < 2; ++pass)
        {
            auto smooth = target;

            for (int i = 2; i < kGridPoints - 2; ++i)
                smooth[(size_t) i] = (target[(size_t) i - 2] + 2.0 * target[(size_t) i - 1] + 3.0 * target[(size_t) i]
                                      + 2.0 * target[(size_t) i + 1] + target[(size_t) i + 2]) / 9.0;

            target = smooth;
        }

        return target;
    }

    inline std::vector<double> responseOf (const SketchBand& band)
    {
        BandSpec spec;
        spec.type = band.type;
        spec.freq = band.freq;
        spec.gainDb = band.gainDb;
        spec.q = band.q;
        spec.slopeIndex = 1;

        const auto analog = designAnalog (spec);
        std::vector<double> response ((size_t) kGridPoints);

        for (int i = 0; i < kGridPoints; ++i)
            response[(size_t) i] = analog.magnitudeDb (gridFreq (i));

        return response;
    }

    /** What all the bands together do, on the grid. */
    inline std::vector<double> responseOf (const std::vector<SketchBand>& bands)
    {
        std::vector<double> total ((size_t) kGridPoints, 0.0);

        for (const auto& band : bands)
        {
            const auto r = responseOf (band);

            for (int i = 0; i < kGridPoints; ++i)
                total[(size_t) i] += r[(size_t) i];
        }

        return total;
    }
}

/** EQ Match: what would have to be done to a track for its spectrum to become
    that of a reference. Both are levels in dB on the sketch grid.

    Only the difference in tone counts, not the one in loudness, so the average
    difference is taken out. Where one of the two has no signal to speak of, and
    outside 30 Hz to 16 kHz, nothing is asked for; a match must not turn up noise.
    The result is smoothed to about a third of an octave and held within +-maxDb. */
inline std::vector<double> matchTarget (const std::vector<float>& reference, const std::vector<float>& own,
                                        double floorDb, double maxDb = 12.0)
{
    using namespace sketch;

    std::vector<double> target ((size_t) kGridPoints, 0.0);

    if ((int) reference.size() != kGridPoints || (int) own.size() != kGridPoints)
        return target;

    std::vector<bool> valid ((size_t) kGridPoints, false);
    double sum = 0.0;
    int count = 0;

    for (int i = 0; i < kGridPoints; ++i)
    {
        const double f = gridFreq (i);
        const bool ok = f >= 30.0 && f <= 16000.0
                     && (double) reference[(size_t) i] > floorDb + 15.0 && (double) own[(size_t) i] > floorDb + 15.0;

        valid[(size_t) i] = ok;

        if (ok)
        {
            sum += (double) reference[(size_t) i] - (double) own[(size_t) i];
            ++count;
        }
    }

    if (count < kGridPoints / 20)
        return target;

    const double levelDifference = sum / count;

    for (int i = 0; i < kGridPoints; ++i)
        if (valid[(size_t) i])
            target[(size_t) i] = std::clamp ((double) reference[(size_t) i] - (double) own[(size_t) i] - levelDifference, -maxDb, maxDb);

    // Below 30 Hz and above 16 kHz the difference carries on as it was at the edge,
    // so that a darker reference stays darker all the way up instead of turning back.
    int firstInRange = 0, lastInRange = kGridPoints - 1;
    while (firstInRange < kGridPoints - 1 && gridFreq (firstInRange) < 30.0)   ++firstInRange;
    while (lastInRange > 0 && gridFreq (lastInRange) > 16000.0)                --lastInRange;

    for (int i = 0; i < firstInRange; ++i)                  target[(size_t) i] = target[(size_t) firstInRange];
    for (int i = lastInRange + 1; i < kGridPoints; ++i)     target[(size_t) i] = target[(size_t) lastInRange];

    // Smoothing: 24 points make an octave, a window of 9 is a good third of one.
    // Low down a spectrum is measured from few, coarse steps and wobbles more,
    // so the window grows there, up to a little over an octave.
    for (int pass = 0; pass < 3; ++pass)
    {
        auto smooth = target;

        for (int i = 0; i < kGridPoints; ++i)
        {
            const int half = std::clamp ((int) std::lround (600.0 / gridFreq (i)), 4, 14);
            double acc = 0.0;
            int n = 0;

            for (int k = std::max (0, i - half); k <= std::min (kGridPoints - 1, i + half); ++k, ++n)
                acc += target[(size_t) k];

            smooth[(size_t) i] = acc / n;
        }

        target = smooth;
    }

    return target;
}

/** Bands that follow a target given on the sketch grid.
    What is left below leastDb (or below a fifth of the target's height) gets no band. */
inline std::vector<SketchBand> fitSketchTarget (const std::vector<double>& target, int maxBands = 6,
                                                double leastDb = sketch::kIgnoreDb)
{
    using namespace sketch;

    std::vector<SketchBand> bands;

    if ((int) target.size() != kGridPoints)
        return bands;

    auto missing = target;

    // A hand-drawn line always leaves small wiggles; they are not worth a band each.
    double tallest = 0.0;
    for (double v : target)
        tallest = std::max (tallest, std::abs (v));

    const double worthABand = std::max (leastDb, 0.2 * tallest);
    const double octavesPerPoint = std::log2 (kHighHz / kLowHz) / (kGridPoints - 1);

    for (int round = 0; round < maxBands; ++round)
    {
        // Where is the most still missing?
        int peak = 0;

        for (int i = 1; i < kGridPoints; ++i)
            if (std::abs (missing[(size_t) i]) > std::abs (missing[(size_t) peak]))
                peak = i;

        const double height = missing[(size_t) peak];

        if (std::abs (height) < worthABand)
            break;

        // How far does it reach to either side before it has fallen to half?
        int lo = peak, hi = peak;
        while (lo > 0 && missing[(size_t) lo] * height > 0.0 && std::abs (missing[(size_t) lo]) > 0.5 * std::abs (height))               --lo;
        while (hi < kGridPoints - 1 && missing[(size_t) hi] * height > 0.0 && std::abs (missing[(size_t) hi]) > 0.5 * std::abs (height)) ++hi;

        SketchBand band;

        if (hi == kGridPoints - 1 && lo > 0)
        {
            // Stays up (or down) to the top of the spectrum: a high shelf, turning where it is half way.
            band.type = FilterType::HighShelf;
            band.freq = gridFreq (lo);
            band.gainDb = missing[(size_t) kGridPoints - 1];
            band.q = 0.8;
        }
        else if (lo == 0 && hi < kGridPoints - 1)
        {
            band.type = FilterType::LowShelf;
            band.freq = gridFreq (hi);
            band.gainDb = missing[0];
            band.q = 0.8;
        }
        else
        {
            band.type = FilterType::Bell;
            band.freq = gridFreq (peak);
            band.gainDb = height;

            // For a bell, the width at half its gain in dB is close to its bandwidth 1/Q in the classic sense.
            const double octaves = std::max (0.1, (double) (hi - lo) * octavesPerPoint);
            const double ratio = std::pow (2.0, octaves);
            band.q = std::clamp (std::sqrt (ratio) / (ratio - 1.0), 0.3, 12.0);
        }

        band.gainDb = std::clamp (band.gainDb, -kMaxGainDb, kMaxGainDb);

        // Trim gain and width so the band takes away as much of what is missing as it can.
        const auto errorWith = [&missing] (const SketchBand& candidate)
        {
            const auto r = sketch::responseOf (candidate);
            double sum = 0.0;

            for (int i = 0; i < kGridPoints; ++i)
            {
                const double e = missing[(size_t) i] - r[(size_t) i];
                sum += e * e;
            }

            return sum;
        };

        double best = errorWith (band);

        for (int pass = 0; pass < 3; ++pass)
        {
            for (double gainFactor : { 0.85, 0.93, 1.07, 1.15 })
                for (double qFactor : { 0.75, 1.0, 1.33 })
                {
                    SketchBand candidate = band;
                    candidate.gainDb = std::clamp (band.gainDb * gainFactor, -kMaxGainDb, kMaxGainDb);
                    candidate.q = std::clamp (band.q * qFactor, band.type == FilterType::Bell ? 0.3 : 0.5,
                                              band.type == FilterType::Bell ? 12.0 : 1.2);

                    const double e = errorWith (candidate);

                    if (e < best)
                    {
                        best = e;
                        band = candidate;
                    }
                }
        }

        const auto r = responseOf (band);

        for (int i = 0; i < kGridPoints; ++i)
            missing[(size_t) i] -= r[(size_t) i];

        bands.push_back (band);
    }

    return bands;
}

/** Bands that follow a curve drawn as (frequency in Hz, level in dB) points. */
inline std::vector<SketchBand> fitSketch (const std::vector<std::pair<double, double>>& points, int maxBands = 6)
{
    if (points.size() < 2)
        return {};

    return fitSketchTarget (sketch::targetFromPoints (points), maxBands);
}

} // namespace hdp
