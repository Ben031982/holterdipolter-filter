// holterdipolter Filter - DSP tests (no JUCE, no framework)
//
// What is checked:
//   - the analog prototypes hit their design targets (bilinear mapping, exact)
//   - every setting gives stable, finite filters, with either mapping
//   - the automatic mapping follows the analog prototype, is clearly better
//     than bilinear near Nyquist, and is practically never worse
//   - the engine outputs exactly the curve the display draws
//   - stereo placement, crossfades, solo, auto gain, smoothing

#include "../Source/dsp/EqEngine.h"
#include "../Source/dsp/SketchFit.h"

#include <cstdio>
#include <initializer_list>
#include <random>
#include <vector>

using namespace hdp;

static int failures = 0;
static int checks = 0;

static void expectNear (double actual, double expected, double tol, const char* what)
{
    ++checks;

    if (! (std::abs (actual - expected) <= tol))
    {
        ++failures;
        std::printf ("  FAIL %-58s got %10.4f, expected %10.4f (+-%.3f)\n", what, actual, expected, tol);
    }
}

static void expectTrue (bool ok, const char* what)
{
    ++checks;

    if (! ok)
    {
        ++failures;
        std::printf ("  FAIL %s\n", what);
    }
}

static const char* typeName (FilterType t)
{
    static const char* names[] = { "Bell", "LowShelf", "HighShelf", "LowCut", "HighCut",
                                   "Notch", "BandPass", "Tilt", "AllPass", "FlatTilt" };
    return names[(int) t];
}

/** Magnitude in dB of a signal at one frequency (single-bin DFT). */
static double measureDb (const std::vector<float>& x, double freq, double sr)
{
    const double w = 2.0 * kPi * freq / sr;
    double re = 0.0, im = 0.0;

    for (size_t n = 0; n < x.size(); ++n)
    {
        re += x[n] * std::cos (w * (double) n);
        im -= x[n] * std::sin (w * (double) n);
    }

    return gainToDb (std::sqrt (re * re + im * im));
}

static double rmsDb (const std::vector<float>& x, size_t from = 0)
{
    double sum = 0.0;

    for (size_t i = from; i < x.size(); ++i)
        sum += (double) x[i] * x[i];

    return 10.0 * std::log10 (std::max (sum / (double) std::max<size_t> (1, x.size() - from), 1.0e-30));
}

static BandSpec specOf (const BandParams& p)
{
    BandSpec s;
    s.type = p.type;
    s.freq = p.freq;
    s.gainDb = p.gainDb;
    s.q = p.q;
    s.slopeIndex = p.slopeIndex;
    return s;
}

static std::vector<float> impulseResponse (const BandParams& p, double sr, int length)
{
    EqEngine engine;
    engine.prepare (sr);
    engine.setBand (0, p);

    std::vector<float> l ((size_t) length, 0.0f), r ((size_t) length, 0.0f);
    l[0] = 1.0f;
    r[0] = 1.0f;

    // Odd block sizes on purpose, to exercise the chunking.
    for (int pos = 0; pos < length;)
    {
        const int n = std::min (509, length - pos);
        engine.process (l.data() + pos, r.data() + pos, n);
        pos += n;
    }

    return l;
}

/** Largest deviation from the analog prototype between 20 Hz and 0.45 fs,
    judged only where the prototype is above floorDb. */
static double errorVsAnalog (const BandSpec& spec, double sr, Mapping mapping, double floorDb)
{
    const auto analog = designAnalog (spec, 0.49 * sr);
    const auto digital = designBand (spec, sr, mapping);
    double worst = 0.0;

    for (int i = 0; i <= 240; ++i)
    {
        const double f = 20.0 * std::pow (0.45 * sr / 20.0, i / 240.0);
        const double ideal = analog.magnitudeDb (f);

        if (ideal >= floorDb)
            worst = std::max (worst, std::abs (digital.magnitudeDb (f, sr) - ideal));
    }

    return worst;
}

//==============================================================================
static void testPrototypes (double sr)
{
    std::printf ("Prototype targets at %.0f Hz\n", sr);

    const double freqs[] = { 30.0, 200.0, 1000.0, 5000.0 };
    const double gains[] = { -18.0, -3.0, 6.0, 24.0 };
    const double qs[]    = { 0.3, 1.0, 6.0 };
    char label[160];

    for (int t = 0; t < kNumFilterTypes; ++t)
        for (int s = 0; s < kNumSlopes; ++s)
            for (double f : freqs)
                for (double g : gains)
                    for (double q : qs)
                    {
                        BandSpec spec;
                        spec.type = (FilterType) t;
                        spec.slopeIndex = s;
                        spec.freq = f;
                        spec.gainDb = g;
                        spec.q = q;

                        // The bilinear mapping is exact at DC, at the band frequency and at
                        // Nyquist, which makes it the right tool to verify the prototypes.
                        const auto c = designBand (spec, sr, Mapping::Bilinear);

                        const double atF  = c.magnitudeDb (f, sr);
                        const double atDc = c.magnitudeDb (0.0001, sr);
                        const double atNy = c.magnitudeDb (0.5 * sr, sr);

                        std::snprintf (label, sizeof (label), "%s %ddB f=%.0f g=%.0f q=%.1f", typeName (spec.type), kSlopeDb[s], f, g, q);

                        switch (spec.type)
                        {
                            case FilterType::Bell:
                                expectNear (atF, g, 0.01, label);
                                expectNear (atDc, 0.0, 0.01, label);
                                expectNear (atNy, 0.0, 0.01, label);
                                break;

                            case FilterType::LowShelf:
                                expectNear (atDc, g, 0.01, label);
                                expectNear (atNy, 0.0, 0.01, label);
                                if (q == 1.0) expectNear (atF, 0.5 * g, 0.01, label);
                                break;

                            case FilterType::HighShelf:
                                expectNear (atDc, 0.0, 0.01, label);
                                expectNear (atNy, g, 0.01, label);
                                if (q == 1.0) expectNear (atF, 0.5 * g, 0.01, label);
                                break;

                            case FilterType::TiltShelf:
                                expectNear (atDc, -0.5 * g, 0.01, label);
                                expectNear (atNy, 0.5 * g, 0.01, label);
                                if (q == 1.0) expectNear (atF, 0.0, 0.01, label);
                                break;

                            case FilterType::LowCut:
                                expectNear (atNy, 0.0, 0.01, label);
                                if (q == 1.0)
                                {
                                    expectNear (atF, -3.0103, 0.01, label);

                                    // Roll-off across the octave below the corner. Only checked for low
                                    // corners, where the bilinear transform has not yet bent the axis.
                                    if (f <= 1000.0)
                                    {
                                        const double order = kSlopeDb[s] / 6.0;
                                        const double ideal = 10.0 * std::log10 ((1.0 + std::pow (16.0, order))
                                                                              / (1.0 + std::pow (4.0, order)));
                                        const double slope = c.magnitudeDb (f / 2.0, sr) - c.magnitudeDb (f / 4.0, sr);
                                        expectNear (slope, ideal, 0.03 * kSlopeDb[s], label);
                                    }
                                }
                                break;

                            case FilterType::HighCut:
                                expectNear (atDc, 0.0, 0.01, label);
                                if (q == 1.0) expectNear (atF, -3.0103, 0.01, label);
                                break;

                            case FilterType::Notch:
                                expectNear (atDc, 0.0, 0.01, label);
                                expectNear (atNy, 0.0, 0.01, label);
                                expectTrue (atF < -60.0, label);
                                break;

                            case FilterType::BandPass:
                                expectNear (atF, 0.0, 0.01, label);
                                expectTrue (atDc < -40.0 && atNy < -40.0, label);
                                break;

                            case FilterType::AllPass:
                                expectNear (atF, 0.0, 0.001, label);
                                expectNear (c.magnitudeDb (3.0 * f, sr), 0.0, 0.001, label);
                                break;

                            case FilterType::FlatTilt:
                                // Judged on the analog prototype further down; here only the pivot.
                                expectNear (atF, 0.0, 0.01, label);
                                break;
                        }
                    }
}

//==============================================================================
static void testStability()
{
    std::printf ("Stability across the whole parameter range\n");

    const double rates[] = { 22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
    const double freqs[] = { 10.0, 37.0, 250.0, 1000.0, 4300.0, 9000.0, 14000.0, 19500.0, 30000.0 };
    const double gains[] = { -30.0, -12.0, -0.5, 0.5, 12.0, 30.0 };
    const double qs[]    = { 0.025, 0.2, 0.71, 1.0, 5.0, 40.0 };

    int unstable = 0, sections = 0;

    for (double sr : rates)
        for (int t = 0; t < kNumFilterTypes; ++t)
            for (int s = 0; s < kNumSlopes; ++s)
                for (double f : freqs)
                    for (double g : gains)
                        for (double q : qs)
                            for (auto mapping : { Mapping::Automatic, Mapping::Matched, Mapping::Bilinear })
                            {
                                BandSpec spec;
                                spec.type = (FilterType) t;
                                spec.slopeIndex = s;
                                spec.freq = f;
                                spec.gainDb = g;
                                spec.q = q;

                                const auto c = designBand (spec, sr, mapping);

                                for (int i = 0; i < c.numSections; ++i)
                                {
                                    const auto& b = c.sections[(size_t) i];
                                    ++sections;

                                    if (! b.isStable() || ! std::isfinite (b.b0 + b.b1 + b.b2 + b.a1 + b.a2))
                                        ++unstable;
                                }
                            }

    std::printf ("  %d sections designed\n", sections);
    expectTrue (unstable == 0, "every section is stable and finite");
}

//==============================================================================
static void testFollowsAnalog()
{
    std::printf ("Automatic mapping against the analog prototype\n");

    const FilterType shaped[] = { FilterType::Bell, FilterType::LowShelf, FilterType::HighShelf,
                                  FilterType::HighCut, FilterType::BandPass, FilterType::TiltShelf,
                                  FilterType::FlatTilt };
    char label[160];

    // 1. Low in the band, ordinary settings: it simply is the analog curve.
    //    Cuts and band passes are judged down to -20 dB, where their skirts
    //    already reach far up the spectrum, hence the wider limits.
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        // Flat Tilt reaches across the whole spectrum wherever its pivot is, so it is not a "low band".
        double worstShaping = 0.0, worstHighCut = 0.0, worstBandPass = 0.0, worstFlatTilt = 0.0;

        for (auto type : shaped)
            for (int s = 0; s < kNumSlopes; ++s)
                for (double f : { 40.0, 150.0, 600.0 })
                    for (double g : { -12.0, -6.0, 6.0, 12.0 })
                        for (double q : { 0.5, 0.71, 1.0, 2.0, 4.0 })
                        {
                            BandSpec spec;
                            spec.type = type; spec.slopeIndex = s; spec.freq = f; spec.gainDb = g; spec.q = q;

                            const double e = errorVsAnalog (spec, sr, Mapping::Automatic, -20.0);

                            if (type == FilterType::HighCut)        worstHighCut = std::max (worstHighCut, e);
                            else if (type == FilterType::BandPass)  worstBandPass = std::max (worstBandPass, e);
                            else if (type == FilterType::FlatTilt)  worstFlatTilt = std::max (worstFlatTilt, e);
                            else                                    worstShaping = std::max (worstShaping, e);
                        }

        std::printf ("  %.0f Hz, bands up to 600 Hz: bells and shelves within %.3f dB, high cuts %.3f dB, band passes %.3f dB\n",
                     sr, worstShaping, worstHighCut, worstBandPass);

        std::snprintf (label, sizeof (label), "low bands follow the analog curve at %.0f Hz", sr);
        expectTrue (worstShaping < 0.05, label);
        if (worstFlatTilt >= 0.4) std::printf ("  flat tilt: %.3f dB\n", worstFlatTilt);
        expectTrue (worstFlatTilt < 0.4, label);
        expectTrue (worstHighCut < 0.3, label);
        expectTrue (worstBandPass < 1.0, label);
    }

    // 2. High in the band: clearly better than bilinear on ordinary settings ...
    for (double sr : { 44100.0, 48000.0 })
    {
        struct Row { FilterType type; double maxMean; };
        const Row rows[] = { { FilterType::Bell, 0.8 }, { FilterType::HighShelf, 0.6 }, { FilterType::HighCut, 2.2 } };

        for (const auto& row : rows)
        {
            double sumAuto = 0.0, sumBilinear = 0.0;
            int n = 0;

            for (int s = 0; s < kNumSlopes; ++s)
                for (double f : { 6000.0, 9000.0, 12000.0, 15000.0 })
                    for (double g : { -12.0, -6.0, 6.0, 12.0 })
                        for (double q : { 0.5, 0.71, 1.0, 2.0, 4.0 })
                        {
                            BandSpec spec;
                            spec.type = row.type; spec.slopeIndex = s; spec.freq = f; spec.gainDb = g; spec.q = q;
                            sumAuto     += errorVsAnalog (spec, sr, Mapping::Automatic, -30.0);
                            sumBilinear += errorVsAnalog (spec, sr, Mapping::Bilinear, -30.0);
                            ++n;
                        }

            const double meanAuto = sumAuto / n, meanBilinear = sumBilinear / n;
            std::printf ("  %.0f Hz, %-9s at 6-15 kHz: mean deviation %.2f dB (bilinear alone: %.2f dB)\n",
                         sr, typeName (row.type), meanAuto, meanBilinear);

            std::snprintf (label, sizeof (label), "%s near Nyquist at %.0f Hz", typeName (row.type), sr);
            expectTrue (meanAuto < row.maxMean, label);
            expectTrue (meanAuto < meanBilinear / 3.0, label);
        }
    }

    // 3. ... and practically never worse, extreme settings included.
    int total = 0, worse = 0;
    double largestExcess = 0.0;

    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (auto type : shaped)
            for (int s = 0; s < kNumSlopes; ++s)
                for (double f : { 100.0, 1000.0, 3000.0, 6000.0, 10000.0, 14000.0, 18000.0 })
                    for (double g : { -30.0, -12.0, 6.0, 24.0 })
                        for (double q : { 0.05, 0.3, 1.0, 4.0, 20.0 })
                        {
                            BandSpec spec;
                            spec.type = type; spec.slopeIndex = s; spec.freq = f; spec.gainDb = g; spec.q = q;

                            const double excess = errorVsAnalog (spec, sr, Mapping::Automatic, -30.0)
                                                - errorVsAnalog (spec, sr, Mapping::Bilinear, -30.0);
                            ++total;
                            largestExcess = std::max (largestExcess, excess);

                            if (excess > 1.0)
                                ++worse;
                        }

    std::printf ("  %d of %d settings are more than 1 dB worse than bilinear (largest: +%.1f dB)\n",
                 worse, total, largestExcess);
    expectTrue (worse * 200 <= total, "fewer than 0.5 % of all settings lose more than 1 dB against bilinear");
    expectTrue (largestExcess < 8.0, "no setting loses more than 8 dB against bilinear");
}

//==============================================================================
static void testFlatTilt()
{
    std::printf ("Flat Tilt\n");

    const double octaves = std::log2 (kFlatTiltHighHz / kFlatTiltLowHz);
    char label[160];

    for (double g : { -18.0, -6.0, 3.0, 12.0 })
        for (double f0 : { 80.0, 632.0, 4000.0 })
        {
            BandSpec spec;
            spec.type = FilterType::FlatTilt;
            spec.freq = f0;
            spec.gainDb = g;

            const auto analog = designAnalog (spec);
            std::snprintf (label, sizeof (label), "flat tilt %+.0f dB, pivot %.0f Hz", g, f0);

            expectNear (analog.magnitudeDb (f0), 0.0, 0.001, label);

            // A straight line: the same number of dB for every octave from 30 Hz to 16 kHz.
            const double perOctave = g / octaves;
            double worst = 0.0;

            for (double f = 30.0; f < 8000.0; f *= 1.25)
                worst = std::max (worst, std::abs ((analog.magnitudeDb (2.0 * f) - analog.magnitudeDb (f)) - perOctave));

            if (worst >= 0.05 * std::abs (perOctave) + 0.003)
                std::printf ("  slope deviates by up to %.4f dB per octave (nominal %.3f)\n", worst, perOctave);

            expectTrue (worst < 0.05 * std::abs (perOctave) + 0.003, label);

            // The gain is what lies between the two ends of the audible range.
            expectNear (analog.magnitudeDb (kFlatTiltHighHz) - analog.magnitudeDb (kFlatTiltLowHz), g, 0.03 * std::abs (g), label);

            // Q and slope are ignored.
            BandSpec other = spec;
            other.q = 7.0;
            other.slopeIndex = 6;
            expectNear (designBand (other, 48000.0).magnitudeDb (1234.0, 48000.0),
                        designBand (spec, 48000.0).magnitudeDb (1234.0, 48000.0), 1.0e-9, label);

            // The digital filter stays within 2 % of the gain all the way up.
            for (double sr : { 44100.0, 48000.0, 96000.0 })
                expectTrue (errorVsAnalog (spec, sr, Mapping::Automatic, -60.0) < 0.02 * std::abs (g) + 0.02, label);
        }
}

//==============================================================================
static void testEngineMatchesDisplay (double sr)
{
    std::printf ("Engine output vs. displayed curve at %.0f Hz\n", sr);

    const int length = 1 << 17;
    const double probes[] = { 40.0, 110.0, 440.0, 1000.0, 3200.0, 9000.0, 15000.0 };
    char label[160];

    for (int t = 0; t < kNumFilterTypes; ++t)
        for (int s = 0; s < kNumSlopes; ++s)
            for (double freq : { 700.0, 11000.0 })
            {
                BandParams p;
                p.active = true;
                p.type = (FilterType) t;
                p.slopeIndex = s;
                p.freq = freq;
                p.gainDb = 9.0;
                p.q = 1.4;

                const auto coeffs = designBand (specOf (p), sr);
                const auto ir = impulseResponse (p, sr, length);

                bool finite = true;
                for (float v : ir) finite = finite && std::isfinite (v);

                std::snprintf (label, sizeof (label), "%s %ddB at %.0f Hz: impulse response finite", typeName (p.type), kSlopeDb[s], freq);
                expectTrue (finite, label);

                for (double f : probes)
                {
                    const double expected = coeffs.magnitudeDb (f, sr);

                    if (expected < -90.0)   // below what a float impulse response resolves
                        continue;

                    std::snprintf (label, sizeof (label), "%s %ddB at %.0f Hz: engine at %.0f Hz", typeName (p.type), kSlopeDb[s], freq, f);
                    expectNear (measureDb (ir, f, sr), expected, 0.02, label);
                }
            }
}

//==============================================================================
static void testPlacement()
{
    std::printf ("Stereo placement\n");

    const double sr = 48000.0;
    const int length = 1 << 15;

    struct Case { Placement place; double l, r; double expectL, expectR; const char* name; };

    // +12 dB bell at 1 kHz, measured at 1 kHz with different input images.
    const Case cases[] = {
        { Placement::Stereo, 1.0,  1.0, 12.0, 12.0, "Stereo: both channels boosted" },
        { Placement::Left,   1.0,  1.0, 12.0,  0.0, "Left: only left boosted" },
        { Placement::Right,  1.0,  1.0,  0.0, 12.0, "Right: only right boosted" },
        { Placement::Mid,    1.0,  1.0, 12.0, 12.0, "Mid: centred signal boosted" },
        { Placement::Mid,    1.0, -1.0,  0.0,  0.0, "Mid: pure side signal untouched" },
        { Placement::Side,   1.0,  1.0,  0.0,  0.0, "Side: centred signal untouched" },
        { Placement::Side,   1.0, -1.0, 12.0, 12.0, "Side: pure side signal boosted" },
    };

    for (const auto& c : cases)
    {
        EqEngine engine;
        engine.prepare (sr);

        BandParams p;
        p.active = true;
        p.type = FilterType::Bell;
        p.placement = c.place;
        p.freq = 1000.0;
        p.gainDb = 12.0;
        p.q = 1.0;
        engine.setBand (0, p);

        std::vector<float> l ((size_t) length, 0.0f), r ((size_t) length, 0.0f);
        l[0] = (float) c.l;
        r[0] = (float) c.r;
        engine.process (l.data(), r.data(), length);

        expectNear (measureDb (l, 1000.0, sr), c.expectL, 0.02, c.name);
        expectNear (measureDb (r, 1000.0, sr), c.expectR, 0.02, c.name);
    }

    // Mono: a mid band still acts, a side band does nothing.
    for (auto place : { Placement::Mid, Placement::Side })
    {
        EqEngine engine;
        engine.prepare (sr);

        BandParams p;
        p.active = true;
        p.placement = place;
        p.freq = 1000.0;
        p.gainDb = 12.0;
        engine.setBand (0, p);

        std::vector<float> m ((size_t) length, 0.0f);
        m[0] = 1.0f;
        engine.process (m.data(), nullptr, length);

        expectNear (measureDb (m, 1000.0, sr), place == Placement::Mid ? 12.0 : 0.0, 0.02, "Mono placement");
    }
}

//==============================================================================
/** Runs a steady sine through the engine, applies `change` half way, and returns
    the largest sample-to-sample step around the change relative to the largest
    step of the settled signal before and after it. 1.0 means: no trace of it. */
template <typename Change>
static double stepRatioAround (EqEngine& engine, double sr, double sineFreq, Change&& change)
{
    const int block = 64;
    const int total = (int) (sr * 1.0);
    const int changeAt = (total / 2 / block) * block;

    std::vector<float> out ((size_t) total);
    double phase = 0.0;

    for (int pos = 0; pos < total; pos += block)
    {
        if (pos == changeAt)
            change();

        const int n = std::min (block, total - pos);
        float l[block], r[block];

        for (int i = 0; i < n; ++i)
        {
            l[i] = r[i] = 0.5f * (float) std::sin (phase);
            phase += 2.0 * kPi * sineFreq / sr;
        }

        engine.process (l, r, n);
        std::copy (l, l + n, out.begin() + pos);
    }

    const auto maxStep = [&out] (int from, int to)
    {
        double m = 0.0;
        for (int i = std::max (1, from); i < to; ++i)
            m = std::max (m, (double) std::abs (out[(size_t) i] - out[(size_t) i - 1]));
        return m;
    };

    const int settle = (int) (0.2 * sr);
    const double before = maxStep (changeAt - settle, changeAt);
    const double after  = maxStep (total - settle, total);
    const double around = maxStep (changeAt, changeAt + settle);

    return around / std::max ({ before, after, 1.0e-6 });
}

static void testNoClicks()
{
    std::printf ("Structural changes are crossfaded\n");

    const double sr = 48000.0;

    const auto makeBand = [] (FilterType type, double freq, double gain, double q, int slope,
                              Placement place = Placement::Stereo, bool active = true)
    {
        BandParams p;
        p.active = active; p.type = type; p.freq = freq; p.gainDb = gain; p.q = q; p.slopeIndex = slope; p.placement = place;
        return p;
    };

    struct Case { const char* name; BandParams from, to; double sine; };

    const Case cases[] = {
        { "bell to steep high shelf",   makeBand (FilterType::Bell, 1000, 12, 1, 1),   makeBand (FilterType::HighShelf, 300, 12, 1, 7), 440 },
        { "low cut 12 to 96 dB/oct",    makeBand (FilterType::LowCut, 120, 0, 1, 1),   makeBand (FilterType::LowCut, 120, 0, 1, 7),     90 },
        { "band switched off",          makeBand (FilterType::Bell, 200, 18, 2, 1),    makeBand (FilterType::Bell, 200, 18, 2, 1, Placement::Stereo, false), 200 },
        { "band switched on",           makeBand (FilterType::LowShelf, 400, -18, 1, 3, Placement::Stereo, false), makeBand (FilterType::LowShelf, 400, -18, 1, 3), 150 },
        { "stereo to mid",              makeBand (FilterType::Bell, 500, 15, 1, 1),    makeBand (FilterType::Bell, 500, 15, 1, 1, Placement::Mid), 500 },
        { "bell to notch on the tone",  makeBand (FilterType::Bell, 300, 9, 1, 1),     makeBand (FilterType::Notch, 300, 0, 4, 1),      300 },
    };

    for (const auto& c : cases)
    {
        EqEngine engine;
        engine.prepare (sr);
        engine.setBand (0, c.from);

        const double ratio = stepRatioAround (engine, sr, c.sine, [&] { engine.setBand (0, c.to); });
        std::printf ("  %-28s step ratio %.3f\n", c.name, ratio);
        expectTrue (ratio < 1.15, c.name);
    }

    // Sweeping a high bell across the range where the mapping may flip must stay smooth too.
    {
        EqEngine engine;
        engine.prepare (sr);

        std::vector<float> out;
        double phase = 0.0;
        int flips = 0;
        bool lastMatched = true;

        for (int block = 0; block < 3000; ++block)
        {
            BandParams p = makeBand (FilterType::Bell, 9000.0 * std::pow (2.4, block / 3000.0), -24.0, 12.0, 5);
            engine.setBand (0, p);

            const bool matched = designBand (specOf (p), sr).matched;
            if (block > 0 && matched != lastMatched) ++flips;
            lastMatched = matched;

            float l[64];
            for (auto& v : l)
            {
                v = 0.5f * (float) std::sin (phase);
                phase += 2.0 * kPi * 3000.0 / sr;
            }

            engine.process (l, nullptr, 64);
            out.insert (out.end(), l, l + 64);
        }

        double worstStep = 0.0;
        for (size_t i = 1; i < out.size(); ++i)
            worstStep = std::max (worstStep, (double) std::abs (out[i] - out[i - 1]));

        // A 3 kHz sine of amplitude 0.5 steps by at most 0.196 per sample at 48 kHz.
        std::printf ("  sweep across %d mapping flips: largest step %.3f (clean sine: 0.196)\n", flips, worstStep);
        expectTrue (worstStep < 0.25, "sweeping across mapping flips stays click-free");
    }
}

//==============================================================================
static void testPhaseInvert()
{
    std::printf ("Phase invert\n");

    const double sr = 48000.0;
    EqEngine normal, inverted;
    normal.prepare (sr);
    inverted.setPhaseInvert (true);
    inverted.prepare (sr);

    BandParams p;
    p.active = true;
    p.freq = 300.0;
    p.gainDb = 6.0;
    normal.setBand (0, p);
    inverted.setBand (0, p);

    std::vector<float> a (4096), b (4096);
    for (size_t i = 0; i < a.size(); ++i)
        a[i] = b[i] = (float) std::sin (0.05 * (double) i);

    normal.process (a.data(), nullptr, (int) a.size());
    inverted.process (b.data(), nullptr, (int) b.size());

    double worst = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        worst = std::max (worst, (double) std::abs (a[i] + b[i]));

    expectTrue (worst < 1.0e-6, "inverted output is the exact negative");

    // Switching while audio runs must not click.
    EqEngine engine;
    engine.prepare (sr);
    const double ratio = stepRatioAround (engine, sr, 200.0, [&] { engine.setPhaseInvert (true); });
    std::printf ("  polarity flip: step ratio %.3f\n", ratio);
    expectTrue (ratio < 1.15, "polarity flip without a click");
}

//==============================================================================
static void testSolo()
{
    std::printf ("Solo\n");

    const double sr = 48000.0;
    const int length = 1 << 16;

    const auto run = [&] (BandParams band, int soloBand, double freq, double l0, double r0, bool right = false)
    {
        EqEngine engine;
        engine.prepare (sr);
        engine.setBand (0, band);
        engine.setSoloBand (soloBand);

        std::vector<float> l ((size_t) length), r ((size_t) length);

        for (int i = 0; i < length; ++i)
        {
            const double s = std::sin (2.0 * kPi * freq * i / sr);
            l[(size_t) i] = (float) (l0 * s);
            r[(size_t) i] = (float) (r0 * s);
        }

        engine.process (l.data(), r.data(), length);
        return rmsDb (right ? r : l, (size_t) length / 2) + 3.0103;   // peak level of a sine
    };

    BandParams bell;
    bell.active = true;
    bell.freq = 1000.0;
    bell.gainDb = 12.0;
    bell.q = 2.0;

    expectNear (run (bell, 0, 1000.0, 0.5, 0.5), -6.02, 0.3, "solo passes the band's own frequency at its original level");
    expectTrue (run (bell, 0, 100.0, 0.5, 0.5) < -40.0, "solo removes what lies far below the band");
    expectTrue (run (bell, 0, 10000.0, 0.5, 0.5) < -40.0, "solo removes what lies far above the band");
    expectNear (run (bell, -1, 1000.0, 0.5, 0.5), -6.02 + 12.0, 0.1, "without solo the band boosts as usual");

    BandParams shelf = bell;
    shelf.type = FilterType::HighShelf;
    shelf.freq = 4000.0;
    expectNear (run (shelf, 0, 12000.0, 0.5, 0.5), -6.02, 0.5, "soloing a high shelf keeps what is above it");
    expectTrue (run (shelf, 0, 400.0, 0.5, 0.5) < -40.0, "soloing a high shelf removes what is below it");

    BandParams side = bell;
    side.placement = Placement::Side;
    expectTrue (run (side, 0, 1000.0, 0.5, 0.5) < -80.0, "soloing a side band on a centred signal gives silence");
    expectNear (run (side, 0, 1000.0, 0.5, -0.5), -6.02, 0.3, "soloing a side band keeps the side signal");

    BandParams leftOnly = bell;
    leftOnly.placement = Placement::Left;
    expectTrue (run (leftOnly, 0, 1000.0, 0.5, 0.5, true) < -80.0, "soloing a left band silences the right channel");

    // Switching solo on and off must not click.
    for (bool turnOn : { true, false })
    {
        EqEngine engine;
        engine.prepare (sr);
        engine.setBand (0, bell);
        engine.setSoloBand (turnOn ? -1 : 0);

        const double ratio = stepRatioAround (engine, sr, 1000.0, [&] { engine.setSoloBand (turnOn ? 0 : -1); });
        std::printf ("  solo %s: step ratio %.3f\n", turnOn ? "on " : "off", ratio);
        expectTrue (ratio < 1.15, turnOn ? "solo on without a click" : "solo off without a click");
    }
}

//==============================================================================
static void testAutoGain()
{
    std::printf ("Auto gain\n");

    const double sr = 48000.0;
    const int length = (int) (sr * 6.0);

    // Pink noise: white noise through a -3 dB/oct filter (Paul Kellet's economy version).
    std::vector<float> pink ((size_t) length);
    {
        std::mt19937 rng (99);
        std::uniform_real_distribution<double> uni (-1.0, 1.0);
        double b0 = 0.0, b1 = 0.0, b2 = 0.0;

        for (auto& v : pink)
        {
            const double white = uni (rng);
            b0 = 0.99765 * b0 + white * 0.0990460;
            b1 = 0.96300 * b1 + white * 0.2965164;
            b2 = 0.57000 * b2 + white * 1.0526913;
            v = (float) (0.05 * (b0 + b1 + b2 + white * 0.1848));
        }
    }

    const auto band = [] (FilterType type, double freq, double gain, double q, int slope)
    {
        BandParams p;
        p.active = true; p.type = type; p.freq = freq; p.gainDb = gain; p.q = q; p.slopeIndex = slope;
        return p;
    };

    // Auto gain models a spectrum that is pink between 30 Hz and 16 kHz, so that is
    // what the test signal is limited to. Unfiltered pink noise has most of its
    // extra energy below 30 Hz, where music has next to none.
    {
        EqEngine limiter;
        limiter.prepare (sr);
        limiter.setBand (0, band (FilterType::LowCut, 30.0, 0.0, 1.0, 5));
        limiter.setBand (1, band (FilterType::HighCut, 16000.0, 0.0, 1.0, 5));
        limiter.process (pink.data(), nullptr, length);
    }

    struct Case { const char* name; std::vector<BandParams> bands; };

    const Case cases[] = {
        { "high shelf +9 dB at 3 kHz",       { band (FilterType::HighShelf, 3000, 9, 1, 1) } },
        { "low shelf -12 dB at 300 Hz",      { band (FilterType::LowShelf, 300, -12, 1, 1) } },
        { "wide bell +15 dB at 2 kHz",       { band (FilterType::Bell, 2000, 15, 0.7, 1) } },
        { "low cut plus two bells",          { band (FilterType::LowCut, 120, 0, 1, 3), band (FilterType::Bell, 400, -8, 1, 1),
                                               band (FilterType::Bell, 5000, 6, 1.5, 1) } },
        { "tilt +6 dB around 1 kHz",         { band (FilterType::TiltShelf, 1000, 6, 1, 1) } },
    };

    const double inputDb = rmsDb (pink, (size_t) sr);

    for (const auto& c : cases)
    {
        double level[2] = { 0.0, 0.0 };
        double applied = 0.0;

        for (int withAuto = 0; withAuto < 2; ++withAuto)
        {
            EqEngine engine;
            engine.prepare (sr);
            engine.setAutoGain (withAuto == 1);

            for (size_t i = 0; i < c.bands.size(); ++i)
                engine.setBand ((int) i, c.bands[i]);

            auto l = pink, r = pink;

            for (int pos = 0; pos < length; pos += 512)
                engine.process (l.data() + pos, r.data() + pos, std::min (512, length - pos));

            level[withAuto] = rmsDb (l, (size_t) sr) - inputDb;
            applied = engine.getAutoGainDb();
        }

        std::printf ("  %-28s level change %+5.2f dB, with auto gain %+5.2f dB (compensation %+.2f dB)\n",
                     c.name, level[0], level[1], applied);
        // It is an estimate for a pink spectrum, not a loudness measurement.
        expectTrue (std::abs (level[1]) < 1.0, c.name);

        if (std::abs (level[0]) > 1.5)
            expectTrue (std::abs (level[1]) < 0.5 * std::abs (level[0]), c.name);
    }

    EqEngine empty;
    empty.prepare (sr);
    empty.setAutoGain (true);
    std::vector<float> silence (512, 0.0f);
    empty.process (silence.data(), nullptr, 512);
    expectNear (empty.getAutoGainDb(), 0.0, 1.0e-9, "no bands: no compensation");
}

//==============================================================================
/** Runs a signal made by `source (sampleIndex, left, right)` through the engine and
    returns how many dB the level of `freq` on the left channel changed in the last half second. */
template <typename Source>
static double dynamicChangeDb (EqEngine& engine, double sr, double freq, double seconds, Source&& source)
{
    const int total = (int) (sr * seconds);
    const int block = 96;
    std::vector<float> in ((size_t) total), out ((size_t) total);

    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        float l[block], r[block];

        for (int i = 0; i < n; ++i)
        {
            double a = 0.0, b = 0.0;
            source (pos + i, a, b);
            l[i] = (float) a;
            r[i] = (float) b;
            in[(size_t) (pos + i)] = l[i];
        }

        engine.process (l, r, n);
        std::copy (l, l + n, out.begin() + pos);
    }

    const size_t tail = (size_t) (0.5 * sr);
    const std::vector<float> inTail (in.end() - (std::ptrdiff_t) tail, in.end());
    const std::vector<float> outTail (out.end() - (std::ptrdiff_t) tail, out.end());
    return measureDb (outTail, freq, sr) - measureDb (inTail, freq, sr);
}

static void testDynamics()
{
    std::printf ("Dynamic EQ\n");

    const double sr = 48000.0;
    const double w1k = 2.0 * kPi * 1000.0 / sr, w100 = 2.0 * kPi * 100.0 / sr;

    expectNear (dynamicAmount (-5.0), 0.0, 1.0e-12, "nothing below the threshold");
    expectNear (dynamicAmount (0.0), 0.0, 1.0e-12, "nothing at the threshold");
    expectNear (dynamicAmount (0.5 * kDynamicSpanDb), 0.5, 1.0e-12, "half the range half way up");
    expectNear (dynamicAmount (kDynamicSpanDb), 1.0, 1.0e-12, "the full range at the top");
    expectNear (dynamicAmount (40.0), 1.0, 1.0e-12, "and never more");

    for (double over = -2.0; over < 14.0; over += 0.25)
        expectTrue (dynamicAmount (over + 0.25) >= dynamicAmount (over), "more level never means less action");

    const auto bell = [] (double range, double threshold, bool autoThreshold)
    {
        BandParams p;
        p.active = true;
        p.type = FilterType::Bell;
        p.freq = 1000.0;
        p.q = 4.0;
        p.dynRangeDb = range;
        p.thresholdDb = threshold;
        p.autoThreshold = autoThreshold;
        return p;
    };

    const auto tone = [w1k] (double levelDb)
    {
        const double amp = dbToGain (levelDb);
        return [amp, w1k] (int i, double& l, double& r) { l = r = amp * std::sin (w1k * i); };
    };

    const auto run = [&] (const BandParams& p, double levelDb, double scale = 1.0)
    {
        EqEngine engine;
        engine.setGainScale (scale);
        engine.prepare (sr);
        engine.setBand (0, p);
        return dynamicChangeDb (engine, sr, 1000.0, 1.5, tone (levelDb));
    };

    // Manual threshold: a tone in the band, quieter than, around and above the threshold.
    expectNear (run (bell (-12.0, -30.0, false), -45.0), 0.0, 0.05, "below the threshold the band does nothing");
    expectNear (run (bell (-12.0, -30.0, false), -24.0), -6.0, 0.3, "6 dB above it: half the range");
    expectNear (run (bell (-12.0, -30.0, false), -10.0), -12.0, 0.15, "well above it: the whole range, as a cut");
    expectNear (run (bell (+9.0, -30.0, false), -10.0), 9.0, 0.15, "a positive range boosts what is loud");
    expectNear (run (bell (-12.0, -30.0, false), -10.0, 0.5), -6.0, 0.15, "gain scale scales the range too");

    {
        auto p = bell (-9.0, -30.0, false);
        p.gainDb = 4.0;
        expectNear (run (p, -60.0), 4.0, 0.05, "the set gain is where a dynamic band rests");
        expectNear (run (p, -10.0), -5.0, 0.15, "and the range is added to it");
    }

    // What lies outside the band does not move it.
    {
        EqEngine engine;
        engine.prepare (sr);
        engine.setBand (0, bell (-12.0, -30.0, false));
        const double change = dynamicChangeDb (engine, sr, 1000.0, 1.5, [&] (int i, double& l, double& r)
        {
            l = r = 0.5 * std::sin (w100 * i) + 0.001 * std::sin (w1k * i);
        });
        expectNear (change, 0.0, 0.1, "a loud tone three octaves away leaves the band alone");
        expectNear (engine.getDynamicGainDb (0), 0.0, 0.05, "and the engine reports no gain change");
    }

    // Shapes without dynamics ignore the range; a range of zero is a static band.
    {
        auto p = bell (-12.0, -30.0, false);
        p.type = FilterType::Notch;
        auto plain = p;
        plain.dynRangeDb = 0.0;
        expectNear (run (p, -10.0), run (plain, -10.0), 1.0e-9, "a notch has no dynamics");

        p.type = FilterType::LowShelf;
        p.freq = 4000.0;
        p.q = 1.0;
        expectNear (run (p, -10.0), -12.0, 0.4, "a low shelf does: the tone below its corner drives it");

        p.type = FilterType::HighShelf;
        expectNear (run (p, -10.0), 0.0, 0.3, "a high shelf above the tone stays where it is");
    }

    // Auto threshold: the same at any input level, and only for what stands out.
    expectNear (run (bell (-12.0, 0.0, true), -6.0), -12.0, 0.15, "auto: a lone loud tone gets the whole range");
    expectNear (run (bell (-12.0, 0.0, true), -50.0), -12.0, 0.15, "auto: so does a lone quiet tone");
    expectNear (run (bell (-12.0, 0.0, true), -90.0), 0.0, 0.05, "auto: silence does not");

    {
        EqEngine engine;
        engine.prepare (sr);
        engine.setBand (0, bell (-12.0, 0.0, true));
        const double change = dynamicChangeDb (engine, sr, 1000.0, 1.5, [&] (int i, double& l, double& r)
        {
            l = r = 0.5 * std::sin (w100 * i) + 0.02 * std::sin (w1k * i);   // 28 dB apart
        });
        expectNear (change, 0.0, 0.1, "auto: a tone far below the rest of the signal is left alone");
    }

    // Placement: a mid band hears the mid signal only.
    {
        auto p = bell (-12.0, -30.0, false);
        p.placement = Placement::Mid;

        EqEngine engine;
        engine.prepare (sr);
        engine.setBand (0, p);
        dynamicChangeDb (engine, sr, 1000.0, 1.0, [&] (int i, double& l, double& r) { l = 0.3 * std::sin (w1k * i); r = -l; });
        expectNear (engine.getDynamicGainDb (0), 0.0, 0.05, "a mid band does not react to a side-only signal");

        dynamicChangeDb (engine, sr, 1000.0, 1.0, [&] (int i, double& l, double& r) { l = r = 0.3 * std::sin (w1k * i); });
        expectNear (engine.getDynamicGainDb (0), -12.0, 0.05, "but to a mid signal it does");

        // Mono: no side channel to hear.
        p.placement = Placement::Side;
        EqEngine mono;
        mono.prepare (sr);
        mono.setBand (0, p);
        std::vector<float> x ((size_t) sr);
        for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (0.3 * std::sin (w1k * (double) i));
        mono.process (x.data(), nullptr, (int) x.size());
        expectNear (mono.getDynamicGainDb (0), 0.0, 1.0e-12, "in mono a side band has nothing to react to");
    }

    // Listening to the sidechain instead of the own track.
    {
        const auto runWithSide = [&] (double ownDb, double sideDb, bool connected, bool autoThreshold)
        {
            auto p = bell (-12.0, -30.0, autoThreshold);
            p.dynFromSidechain = true;

            EqEngine engine;
            engine.prepare (sr);
            engine.setBand (0, p);

            const double own = dbToGain (ownDb), side = sideDb > -200.0 ? dbToGain (sideDb) : 0.0;
            const int total = (int) (sr * 1.5);
            std::vector<float> in ((size_t) total), out ((size_t) total);

            for (int pos = 0; pos < total; pos += 64)
            {
                float l[64], r[64], sl[64], sr2[64];

                for (int i = 0; i < 64; ++i)
                {
                    l[i] = r[i] = (float) (own * std::sin (w1k * (pos + i)));
                    sl[i] = sr2[i] = (float) (side * std::sin (w1k * (pos + i) + 1.0));
                    in[(size_t) (pos + i)] = l[i];
                }

                engine.process (l, r, 64, connected ? sl : nullptr, connected ? sr2 : nullptr);
                std::copy (l, l + 64, out.begin() + pos);
            }

            const size_t tail = (size_t) (0.5 * sr);
            return measureDb (std::vector<float> (out.end() - (std::ptrdiff_t) tail, out.end()), 1000.0, sr)
                 - measureDb (std::vector<float> (in.end() - (std::ptrdiff_t) tail, in.end()), 1000.0, sr);
        };

        expectNear (runWithSide (-45.0, -6.0, true, false), -12.0, 0.15, "sidechain: a loud sidechain turns a quiet track down");
        expectNear (runWithSide (-6.0, -60.0, true, false), 0.0, 0.05, "sidechain: the track's own level no longer matters");
        expectNear (runWithSide (-6.0, -300.0, true, false), 0.0, 0.05, "sidechain: silence at the sidechain leaves the band at rest");
        expectNear (runWithSide (-6.0, -6.0, false, false), 0.0, 1.0e-6, "sidechain: nothing connected, nothing happens");
        expectNear (runWithSide (-6.0, -6.0, false, true), 0.0, 1.0e-6, "sidechain: nor with the auto threshold");
        expectNear (runWithSide (-30.0, -40.0, true, true), -12.0, 0.15, "sidechain with auto threshold: a tone alone in the sidechain counts fully");
    }

    // Timing: the gain follows a burst quickly and lets go again; no clicks on the way.
    {
        EqEngine engine;
        engine.prepare (sr);
        engine.setBand (0, bell (-12.0, -30.0, false));

        const int total = (int) (sr * 2.0), burstFrom = (int) (sr * 0.5), burstTo = (int) (sr * 1.0);
        std::vector<float> l ((size_t) total), r ((size_t) total);

        for (int i = 0; i < total; ++i)
            l[(size_t) i] = r[(size_t) i] = (float) ((i >= burstFrom && i < burstTo ? 0.5 : 0.002) * std::sin (w1k * i));

        double atAttack = 0.0, atRelease = 0.0, atEnd = 0.0;
        double worstStep = 0.0;
        const double expectedStep = 0.5 * w1k;   // the steepest step of the loud tone itself

        for (int pos = 0; pos < total; pos += 32)
        {
            engine.process (l.data() + pos, r.data() + pos, 32);

            if (pos == burstFrom + 32 * 75)   atAttack = engine.getDynamicGainDb (0);    // 50 ms in
            if (pos == burstTo + 32 * 750)    atRelease = engine.getDynamicGainDb (0);   // 500 ms after
            atEnd = engine.getDynamicGainDb (0);
        }

        for (int i = 1; i < total; ++i)
            worstStep = std::max (worstStep, (double) std::abs (l[(size_t) i] - l[(size_t) i - 1]));

        expectTrue (atAttack < -11.0, "50 ms into a burst the band has reacted");
        expectTrue (atRelease > -1.0, "half a second after it the band has let go");
        expectNear (atEnd, 0.0, 0.05, "and it comes to rest completely");
        expectTrue (worstStep < 1.05 * expectedStep, "the gain change makes no clicks");
    }

    // Auto gain compensates for the curve as set, not for what the dynamics do.
    {
        auto p = bell (-12.0, -30.0, false);
        p.q = 0.7;

        EqEngine engine;
        engine.setAutoGain (true);
        engine.prepare (sr);
        engine.setBand (0, p);
        dynamicChangeDb (engine, sr, 1000.0, 1.0, tone (-6.0));
        expectNear (engine.getDynamicGainDb (0), -12.0, 0.05, "driven to its full range");
        expectNear (engine.getAutoGainDb(), 0.0, 1.0e-9, "auto gain does not undo the dynamics");

        p.dynRangeDb = 0.0;
        p.gainDb = -12.0;
        engine.setBand (0, p);
        dynamicChangeDb (engine, sr, 1000.0, 1.0, tone (-6.0));
        expectTrue (engine.getAutoGainDb() > 1.0, "the same cut set by hand is compensated");
        expectNear (engine.getDynamicGainDb (0), 0.0, 1.0e-12, "a band that stopped being dynamic reports none");
    }

    // Many dynamic bands on noise, with everything moving: finite and bounded.
    {
        EqEngine engine;
        engine.prepare (sr);
        std::mt19937 rng (5);
        std::uniform_real_distribution<double> uni (0.0, 1.0);
        double peak = 0.0;

        for (int block = 0; block < 600; ++block)
        {
            for (int b = 0; b < kMaxBands; ++b)
            {
                BandParams p;
                p.active = true;
                p.type = (b % 3 == 0) ? FilterType::LowShelf : (b % 3 == 1 ? FilterType::Bell : FilterType::HighShelf);
                p.placement = (Placement) (b % kNumPlacements);
                p.freq = 30.0 * std::pow (600.0, uni (rng));
                p.q = 0.3 + 10.0 * uni (rng);
                p.gainDb = -12.0 + 24.0 * uni (rng);
                p.dynRangeDb = (block % 50 < 25) ? -30.0 + 60.0 * uni (rng) : 0.0;
                p.autoThreshold = b % 2 == 0;
                p.thresholdDb = -60.0 * uni (rng);
                p.slopeIndex = b % kNumSlopes;

                if (block % 7 == 0)
                    engine.setBand (b, p);
            }

            float l[128], r[128];

            for (int i = 0; i < 128; ++i)
            {
                l[i] = (float) (0.2 * (uni (rng) - 0.5));
                r[i] = (float) (0.2 * (uni (rng) - 0.5));
            }

            engine.process (l, r, 128);

            for (int i = 0; i < 128; ++i)
                peak = std::max ({ peak, (double) std::abs (l[i]), (double) std::abs (r[i]) });
        }

        std::printf ("  24 dynamic bands in motion on noise: peak %.1f\n", peak);
        expectTrue (std::isfinite (peak) && peak < 1.0e6, "24 dynamic bands in motion stay bounded");
    }
}

static void testSketch()
{
    std::printf ("EQ Sketch\n");

    const auto worstError = [] (const std::vector<double>& target, const std::vector<SketchBand>& bands)
    {
        const auto got = sketch::responseOf (bands);
        double worst = 0.0;

        for (int i = 0; i < sketch::kGridPoints; ++i)
            worst = std::max (worst, std::abs (got[(size_t) i] - target[(size_t) i]));

        return worst;
    };

    const auto drawn = [] (const std::vector<SketchBand>& intended)
    {
        // What a hand would draw if it meant exactly these bands: their curve, as points.
        const auto curve = sketch::responseOf (intended);
        std::vector<std::pair<double, double>> points;

        for (int i = 0; i < sketch::kGridPoints; i += 3)
            points.push_back ({ sketch::gridFreq (i), curve[(size_t) i] });

        return points;
    };

    expectTrue (fitSketch ({}).empty() && fitSketch ({ { 100.0, 3.0 } }).empty(), "no drawing, no bands");
    expectTrue (fitSketch ({ { 100.0, 0.2 }, { 1000.0, -0.3 }, { 8000.0, 0.4 } }).empty(), "a line along 0 dB needs no band");

    struct Case { const char* name; std::vector<SketchBand> intended; int maxBands; double tolerance; };

    const Case cases[] = {
        { "a hump at 1 kHz",                { { FilterType::Bell, 1000.0, 6.0, 1.5 } }, 2, 0.8 },
        { "a narrow dip at 300 Hz",         { { FilterType::Bell, 300.0, -8.0, 5.0 } }, 3, 1.0 },
        { "more treble",                    { { FilterType::HighShelf, 5000.0, 5.0, 0.8 } }, 2, 0.8 },
        { "less bass",                      { { FilterType::LowShelf, 150.0, -6.0, 0.8 } }, 2, 0.8 },
        { "a smile",                        { { FilterType::LowShelf, 120.0, 4.0, 0.8 }, { FilterType::Bell, 700.0, -3.0, 0.8 },
                                              { FilterType::HighShelf, 7000.0, 4.0, 0.8 } }, 5, 1.2 },
        { "two humps and a dip",            { { FilterType::Bell, 90.0, 5.0, 2.0 }, { FilterType::Bell, 600.0, -6.0, 1.5 },
                                              { FilterType::Bell, 4000.0, 4.0, 1.0 } }, 5, 1.2 },
    };

    char label[160];

    for (const auto& c : cases)
    {
        const auto points = drawn (c.intended);
        const auto bands = fitSketch (points);
        const double error = worstError (sketch::targetFromPoints (points), bands);

        std::printf ("  %-22s %d band(s), within %.2f dB\n", c.name, (int) bands.size(), error);

        std::snprintf (label, sizeof (label), "sketch '%s' is followed closely", c.name);
        expectTrue (error < c.tolerance, label);
        std::snprintf (label, sizeof (label), "sketch '%s' takes few bands", c.name);
        expectTrue (! bands.empty() && (int) bands.size() <= c.maxBands, label);

        for (const auto& b : bands)
            expectTrue (b.freq >= sketch::kLowHz && b.freq <= sketch::kHighHz && std::abs (b.gainDb) <= kMaxGainDb
                        && b.q >= 0.3 && b.q <= 12.0, "sketch bands stay within the plugin's ranges");
    }

    // The kind of band matters: a rise that stays up is a shelf, a hump is a bell.
    {
        const auto treble = fitSketch ({ { 1500.0, 0.0 }, { 3000.0, 1.0 }, { 6000.0, 4.5 }, { 12000.0, 5.0 } });
        expectTrue (! treble.empty() && treble.front().type == FilterType::HighShelf && treble.front().gainDb > 3.5,
                    "a line that rises and stays up becomes a high shelf");

        const auto hump = fitSketch ({ { 400.0, 0.0 }, { 700.0, 3.0 }, { 1000.0, 6.0 }, { 1400.0, 3.0 }, { 2500.0, 0.0 } });
        expectTrue (! hump.empty() && hump.front().type == FilterType::Bell && std::abs (hump.front().freq / 1000.0 - 1.0) < 0.15
                    && hump.front().gainDb > 4.5, "a hump becomes a bell on its top");

        // Drawn backwards and with a shaky hand: the same thing.
        const auto shaky = fitSketch ({ { 2500.0, 0.2 }, { 1400.0, 3.3 }, { 1000.0, 5.8 }, { 1020.0, 6.2 }, { 700.0, 2.8 }, { 400.0, -0.2 } });
        expectTrue (! shaky.empty() && shaky.front().type == FilterType::Bell && std::abs (shaky.front().freq / 1000.0 - 1.0) < 0.2,
                    "drawing from right to left makes no difference");

        expectTrue (fitSketch (drawn ({ { FilterType::Bell, 1000.0, 6.0, 1.5 } }), 1).size() == 1, "the number of bands can be limited");
    }
}

static void testMatch()
{
    std::printf ("EQ Match\n");

    using namespace sketch;

    // A track that falls off towards the top, and a reference that is the same
    // track with a known EQ on it, played 7 dB louder.
    const std::vector<SketchBand> difference = { { FilterType::Bell, 900.0, 6.0, 1.2 }, { FilterType::HighShelf, 6000.0, -5.0, 0.8 },
                                                 { FilterType::Bell, 120.0, -4.0, 1.5 } };
    const auto curve = responseOf (difference);

    std::vector<float> own ((size_t) kGridPoints), reference ((size_t) kGridPoints);

    for (int i = 0; i < kGridPoints; ++i)
    {
        own[(size_t) i] = (float) (-30.0 - 3.0 * std::log2 (gridFreq (i) / 1000.0));
        reference[(size_t) i] = own[(size_t) i] + (float) curve[(size_t) i] + 7.0f;
    }

    const auto target = matchTarget (reference, own, -90.0);

    double worst = 0.0, mean = 0.0;
    int counted = 0;

    for (int i = 0; i < kGridPoints; ++i)
    {
        if (gridFreq (i) < 60.0 || gridFreq (i) > 9000.0)
            continue;

        worst = std::max (worst, std::abs (target[(size_t) i] - curve[(size_t) i]));
        mean += target[(size_t) i] - curve[(size_t) i];
        ++counted;
    }

    mean /= counted;
    std::printf ("  target follows the known difference within %.2f dB (offset %.2f dB)\n", worst, mean);
    expectTrue (worst < 1.6, "the target is the difference in tone between the two");
    expectTrue (std::abs (mean) < 1.2, "the difference in loudness is left out");

    const auto bands = fitSketchTarget (target, 8, kMatchLeastDb);
    const auto got = responseOf (bands);
    double fitWorst = 0.0;

    for (int i = 0; i < kGridPoints; ++i)
        if (gridFreq (i) >= 60.0 && gridFreq (i) <= 9000.0)
            fitWorst = std::max (fitWorst, std::abs (got[(size_t) i] - curve[(size_t) i]));

    std::printf ("  %d bands bring the track within %.2f dB of the reference\n", (int) bands.size(), fitWorst);
    expectTrue (! bands.empty() && bands.size() <= 8 && fitWorst < 2.2, "a handful of bands does the matching");

    // Nothing is asked for where there is no signal, and never more than the limit.
    auto quietTop = own;
    for (int i = 0; i < kGridPoints; ++i)
        if (gridFreq (i) > 3000.0)
            quietTop[(size_t) i] = -110.0f;

    const auto careful = matchTarget (reference, quietTop, -90.0);
    double atTop = 0.0, largest = 0.0;

    for (int i = 0; i < kGridPoints; ++i)
    {
        largest = std::max (largest, std::abs (careful[(size_t) i]));

        if (gridFreq (i) > 5000.0)
            atTop = std::max (atTop, std::abs (careful[(size_t) i]));
    }

    expectTrue (atTop < 0.01, "where the track has no signal, a match asks for nothing");
    expectTrue (largest <= 12.0 + 1.0e-9, "a match never asks for more than 12 dB");

    std::vector<float> silent ((size_t) kGridPoints, -120.0f);
    const auto none = matchTarget (reference, silent, -90.0);
    expectTrue (fitSketchTarget (none).empty() && matchTarget (reference, std::vector<float> (10), -90.0).size() == (size_t) kGridPoints,
                "silence or a broken measurement gives no bands");
    expectTrue (fitSketchTarget (matchTarget (own, own, -90.0)).empty(), "a track matched to itself needs nothing");

    // A reference that is darker stays darker to the very top, instead of coming back up at 16 kHz.
    expectNear (target[(size_t) kGridPoints - 1], target[(size_t) kGridPoints - 12], 0.6, "above 16 kHz the difference carries on level");

    // Measurements wobble. One or two dB of difference here and there is not worth a band.
    {
        std::mt19937 rng (4);
        std::uniform_real_distribution<float> wobble (-1.2f, 1.2f);
        auto shaky = own;

        for (auto& v : shaky)
            v += wobble (rng);

        const auto fromWobble = fitSketchTarget (matchTarget (shaky, own, -90.0), 8, kMatchLeastDb);
        std::printf ("  a track against a wobbly copy of itself: %d band(s)\n", (int) fromWobble.size());
        expectTrue (fromWobble.empty(), "measurement wobble alone gives no bands");
    }
}

//==============================================================================
static void testAutomationStaysBounded()
{
    std::printf ("Parameter movement under load\n");

    const double sr = 44100.0;
    EqEngine engine;
    engine.prepare (sr);
    engine.setAutoGain (true);

    std::mt19937 rng (1234);
    std::uniform_real_distribution<double> uni (0.0, 1.0);
    std::uniform_real_distribution<float> noise (-0.5f, 0.5f);

    std::vector<float> l (256), r (256);
    float peak = 0.0f;
    bool finite = true;

    for (int block = 0; block < 4000; ++block)
    {
        // Every block, throw all 24 bands somewhere else: worst case for smoothing and fades.
        for (int b = 0; b < kMaxBands; ++b)
        {
            BandParams p;
            p.active = uni (rng) > 0.1;
            p.type = (FilterType) (int) (uni (rng) * ((double) kNumFilterTypes - 0.001));
            p.placement = (Placement) (int) (uni (rng) * 4.999);
            p.slopeIndex = (int) (uni (rng) * 7.999);
            p.freq = kMinFreq * std::pow (kMaxFreq / kMinFreq, uni (rng));
            p.gainDb = (uni (rng) * 2.0 - 1.0) * 6.0;
            p.q = kMinQ * std::pow (4.0 / kMinQ, uni (rng));
            engine.setBand (b, p);
        }

        engine.setSoloBand (block % 97 < 20 ? (int) (uni (rng) * 23.999) : -1);

        for (auto& v : l) v = noise (rng);
        for (auto& v : r) v = noise (rng);

        engine.process (l.data(), r.data(), (int) l.size());

        for (size_t i = 0; i < l.size(); ++i)
        {
            finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
            peak = std::max ({ peak, std::abs (l[i]), std::abs (r[i]) });
        }
    }

    expectTrue (finite, "output stays finite while all bands jump around");
    std::printf ("  peak during stress run: %.1f dBFS\n", gainToDb (peak));

    // A slow sweep of a narrow +18 dB bell over a sine must not blow up.
    EqEngine sweep;
    sweep.prepare (sr);
    double phase = 0.0;
    peak = 0.0f;

    for (int block = 0; block < 2000; ++block)
    {
        BandParams p;
        p.active = true;
        p.freq = 100.0 * std::pow (100.0, block / 2000.0);
        p.gainDb = 18.0;
        p.q = 8.0;
        sweep.setBand (0, p);

        for (auto& v : l)
        {
            v = 0.25f * (float) std::sin (phase);
            phase += 2.0 * kPi * 1000.0 / sr;
        }

        sweep.process (l.data(), nullptr, (int) l.size());
        for (float v : l) peak = std::max (peak, std::abs (v));
    }

    // 0.25 * 18 dB = 1.99; allow a little transient overshoot on top.
    expectTrue (peak < 2.6f, "swept resonant bell stays within its steady-state gain");
    std::printf ("  peak during sweep: %.2f (steady-state maximum 1.99)\n", peak);
}

//==============================================================================
static void testGainScaleAndOutput()
{
    std::printf ("Gain scale and output gain\n");

    const double sr = 48000.0;
    const int length = 1 << 15;

    EqEngine engine;
    engine.setOutputGainDb (-6.0);
    engine.prepare (sr);
    engine.setGainScale (0.5);

    BandParams p;
    p.active = true;
    p.freq = 1000.0;
    p.gainDb = 12.0;
    engine.setBand (0, p);

    std::vector<float> l ((size_t) length, 0.0f);
    l[0] = 1.0f;
    engine.process (l.data(), nullptr, length);

    expectNear (measureDb (l, 1000.0, sr), 12.0 * 0.5 - 6.0, 0.02, "12 dB bell at 50% scale with -6 dB output");
}

int main()
{
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        testPrototypes (sr);

    testStability();
    testFollowsAnalog();
    testFlatTilt();

    for (double sr : { 44100.0, 96000.0 })
        testEngineMatchesDisplay (sr);

    testPlacement();
    testNoClicks();
    testPhaseInvert();
    testSolo();
    testAutoGain();
    testDynamics();
    testSketch();
    testMatch();
    testAutomationStaysBounded();
    testGainScaleAndOutput();

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
