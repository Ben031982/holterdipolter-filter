// holterdipolter Filter - filter design
//
// Pure C++17, no JUCE. Every band is described as a cascade of analog
// second-order sections normalised to w0 = 1, which are then mapped to the
// digital domain. The same function is used by the audio engine and by the
// display, so the curve you see is the curve you hear.
//
// Two mappings are used:
//
//  - Matched: the poles are placed exactly where the analog poles sit
//    (z = exp(s T)) and the zeros are solved so that the magnitude follows the
//    analog one all the way to Nyquist. This keeps bells, shelves and high
//    cuts in shape near half the sample rate, where a bilinear transform
//    squeezes them.
//
//  - Bilinear: exact at the band's own frequency, which makes it the better
//    choice for very narrow or very resonant settings. It is also what low
//    cut, notch and all pass always use: those need their zeros in an exact
//    place (DC, the notch frequency, mirrored poles).
//
// designBand() does not trust either blindly. For every setting it builds
// both, measures each against the analog prototype, and returns the one that
// is closer.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

namespace hdp
{

constexpr double kPi = 3.14159265358979323846;

constexpr int kMaxBands    = 24;
constexpr int kMaxSections = 8;

enum class FilterType
{
    Bell = 0,
    LowShelf,
    HighShelf,
    LowCut,
    HighCut,
    Notch,
    BandPass,
    TiltShelf,
    AllPass,
    FlatTilt      // added in 0.3; new shapes go at the end so saved states keep their meaning
};

constexpr int kNumFilterTypes = 10;

/** Flat Tilt is a straight line (in dB over octaves) between these two frequencies. */
constexpr double kFlatTiltLowHz  = 20.0;
constexpr double kFlatTiltHighHz = 20480.0;   // ten octaves above

enum class Placement
{
    Stereo = 0,
    Left,
    Right,
    Mid,
    Side
};

constexpr int kNumPlacements = 5;

constexpr int kNumSlopes = 8;
constexpr int kSlopeDb[kNumSlopes] = { 6, 12, 18, 24, 36, 48, 72, 96 };

constexpr double kMinFreq = 10.0;
constexpr double kMaxFreq = 30000.0;
constexpr double kMinQ    = 0.025;
constexpr double kMaxQ    = 40.0;
constexpr double kMaxGainDb = 30.0;

inline bool typeHasGain (FilterType t)
{
    return t == FilterType::Bell || t == FilterType::LowShelf
        || t == FilterType::HighShelf || t == FilterType::TiltShelf || t == FilterType::FlatTilt;
}

/** Shapes that are fully described by frequency and gain. */
inline bool typeHasQ (FilterType t)      { return t != FilterType::FlatTilt; }
inline bool typeHasSlope (FilterType t)  { return t != FilterType::FlatTilt; }

/** Shapes whose gain can follow the level of the signal (Dynamic EQ). */
inline bool typeHasDynamics (FilterType t)
{
    return t == FilterType::Bell || t == FilterType::LowShelf || t == FilterType::HighShelf;
}

/** Exact zero test, written so that it does not trip float-equality warnings. */
inline bool isExactlyZero (double x) { return ! (x < 0.0) && ! (x > 0.0); }

inline double dbToGain (double db) { return std::pow (10.0, db / 20.0); }
inline double gainToDb (double g)  { return 20.0 * std::log10 (std::max (g, 1.0e-12)); }

//==============================================================================
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    /** Squared magnitude, given phi = sin^2(w/2) for angular frequency w.
        Written this way it stays accurate at very low frequencies, where the
        direct complex evaluation loses precision. */
    double magnitudeSquaredFromPhi (double phi) const
    {
        const double num = (b0 + b1 + b2) * (b0 + b1 + b2)
                         - 4.0 * (b0 * b1 + 4.0 * b0 * b2 + b1 * b2) * phi
                         + 16.0 * b0 * b2 * phi * phi;

        const double den = (1.0 + a1 + a2) * (1.0 + a1 + a2)
                         - 4.0 * (a1 + 4.0 * a2 + a1 * a2) * phi
                         + 16.0 * a2 * phi * phi;

        return std::max (num, 0.0) / std::max (den, 1.0e-300);
    }

    /** Squared magnitude at normalised angular frequency w (radians/sample). */
    double magnitudeSquared (double w) const
    {
        const double s = std::sin (0.5 * w);
        return magnitudeSquaredFromPhi (s * s);
    }

    bool isStable() const
    {
        return std::abs (a2) < 1.0 && std::abs (a1) < 1.0 + a2;
    }
};

struct BandCoeffs
{
    std::array<Biquad, kMaxSections> sections {};
    int numSections = 0;
    bool matched = false;   // which mapping produced these sections

    double magnitudeSquaredFromPhi (double phi) const
    {
        double m2 = 1.0;

        for (int i = 0; i < numSections; ++i)
            m2 *= sections[(size_t) i].magnitudeSquaredFromPhi (phi);

        return m2;
    }

    double magnitudeDb (double freqHz, double sampleRate) const
    {
        const double s = std::sin (kPi * std::min (freqHz, 0.5 * sampleRate) / sampleRate);
        return 10.0 * std::log10 (std::max (magnitudeSquaredFromPhi (s * s), 1.0e-30));
    }
};

struct BandSpec
{
    FilterType type = FilterType::Bell;
    double freq   = 1000.0;
    double gainDb = 0.0;
    double q      = 1.0;
    int slopeIndex = 1;   // index into kSlopeDb
};

enum class Mapping
{
    Automatic,   // whichever of the two is closer to the analog prototype
    Bilinear,
    Matched      // falls back to bilinear for shapes the matched mapping does not cover
};

//==============================================================================
/** (B0 s^2 + B1 s + B2) / (A0 s^2 + A1 s + A2), normalised to w0 = 1.
    A first-order section has A0 = B0 = 0. */
struct AnalogSection
{
    double B0, B1, B2, A0, A1, A2;

    /** Squared magnitude at normalised frequency W (1 = the band's frequency). */
    double magnitudeSquared (double W) const
    {
        const double W2 = W * W;
        const double nr = B2 - B0 * W2, ni = B1 * W;
        const double dr = A2 - A0 * W2, di = A1 * W;
        return (nr * nr + ni * ni) / std::max (dr * dr + di * di, 1.0e-300);
    }

    bool isFirstOrder() const   { return isExactlyZero (A0) && isExactlyZero (B0); }
};

/** The analog prototype of one band. */
struct AnalogBand
{
    std::array<AnalogSection, kMaxSections> sections {};
    int numSections = 0;
    double freq = 1000.0;     // Hz, what W = 1 stands for
    bool matched = false;     // whether the matched mapping applies to this shape
    bool spread = false;      // sections lie all over the spectrum instead of around freq (Flat Tilt)

    void add (const AnalogSection& s)
    {
        if (numSections < kMaxSections)
            sections[(size_t) numSections++] = s;
    }

    void scaleFirst (double g)
    {
        if (numSections > 0)
        {
            sections[0].B0 *= g;
            sections[0].B1 *= g;
            sections[0].B2 *= g;
        }
    }

    /** The ideal response, unaffected by any sample rate. */
    double magnitudeDb (double freqHz) const
    {
        double m2 = 1.0;

        for (int i = 0; i < numSections; ++i)
            m2 *= sections[(size_t) i].magnitudeSquared (freqHz / freq);

        return 10.0 * std::log10 (std::max (m2, 1.0e-30));
    }
};

//==============================================================================
namespace detail
{
    /** Damping (1/Q) of the k-th conjugate pole pair of an N-th order Butterworth filter. */
    inline double butterworthDamping (int k, int order)
    {
        return 2.0 * std::sin (kPi * (2.0 * k + 1.0) / (2.0 * order));
    }

    /** Roots of s^2 - c s + 1 for a complex c, ordered by magnitude. */
    inline void bandTransformRoots (std::complex<double> c,
                                    std::complex<double>& lo, std::complex<double>& hi)
    {
        const auto d  = std::sqrt (c * c - 4.0);
        const auto r1 = 0.5 * (c + d);
        const auto r2 = 0.5 * (c - d);

        if (std::abs (r1) <= std::abs (r2)) { lo = r1; hi = r2; }
        else                                { lo = r2; hi = r1; }
    }

    /** Quadratic s^2 + c1 s + c0 with roots r and conj(r). */
    inline void quadFromRoot (std::complex<double> r, double& c1, double& c0)
    {
        c1 = -2.0 * r.real();
        c0 = std::norm (r);
    }

    inline int orderForCutOrShelf (int slopeIndex)
    {
        return kSlopeDb[std::clamp (slopeIndex, 0, kNumSlopes - 1)] / 6;
    }

    /** Bell, notch and band pass are built from a prototype of this order;
        each prototype pole becomes one biquad. 6 and 18 dB/oct round up. */
    inline int orderForBandType (int slopeIndex)
    {
        const int db = kSlopeDb[std::clamp (slopeIndex, 0, kNumSlopes - 1)];
        return std::max (1, (db + 11) / 12);
    }

    /** High shelf with linear gain G at high frequencies, unity at DC. */
    inline void addHighShelf (AnalogBand& b, double G, int order, double q)
    {
        const double r  = std::pow (G, 1.0 / (2.0 * order));
        const double ri = 1.0 / r;

        for (int k = 0; k < order / 2; ++k)
        {
            double d = butterworthDamping (k, order);
            if (k == 0) d /= q;
            b.add ({ r * r, d * r, 1.0, ri * ri, d * ri, 1.0 });
        }

        if (order % 2 == 1)
            b.add ({ 0.0, r, 1.0, 0.0, ri, 1.0 });
    }

    /** Low shelf with linear gain G at DC, unity at high frequencies. */
    inline void addLowShelf (AnalogBand& b, double G, int order, double q)
    {
        const double r  = std::pow (G, 1.0 / (2.0 * order));
        const double ri = 1.0 / r;

        for (int k = 0; k < order / 2; ++k)
        {
            double d = butterworthDamping (k, order);
            if (k == 0) d /= q;
            b.add ({ 1.0, d * r, r * r, 1.0, d * ri, ri * ri });
        }

        if (order % 2 == 1)
            b.add ({ 0.0, 1.0, r, 0.0, 1.0, ri });
    }

    /** Band-shelf of prototype order n: gain G at the centre, unity elsewhere.
        For n = 1 this is the classic peaking EQ. */
    inline void addBell (AnalogBand& b, double G, int n, double q)
    {
        const double bw = 1.0 / q;
        const double rz = std::pow (G, 1.0 / (2.0 * n));
        const double rp = 1.0 / rz;

        for (int k = 0; k < n / 2; ++k)
        {
            const double d = butterworthDamping (k, n);
            const std::complex<double> unit (-0.5 * d, std::sqrt (std::max (0.0, 1.0 - 0.25 * d * d)));

            std::complex<double> zLo, zHi, pLo, pHi;
            bandTransformRoots (bw * rz * unit, zLo, zHi);
            bandTransformRoots (bw * rp * unit, pLo, pHi);

            double n1, n0, d1, d0;
            quadFromRoot (zLo, n1, n0); quadFromRoot (pLo, d1, d0);
            b.add ({ 1.0, n1, n0, 1.0, d1, d0 });
            quadFromRoot (zHi, n1, n0); quadFromRoot (pHi, d1, d0);
            b.add ({ 1.0, n1, n0, 1.0, d1, d0 });
        }

        if (n % 2 == 1)
            b.add ({ 1.0, bw * rz, 1.0, 1.0, bw * rp, 1.0 });
    }

    /** Butterworth band pass (isNotch = false) or band stop (isNotch = true). */
    inline void addBandPassOrNotch (AnalogBand& b, int n, double q, bool isNotch)
    {
        const double bw = 1.0 / q;

        for (int k = 0; k < n / 2; ++k)
        {
            const double d = butterworthDamping (k, n);
            const std::complex<double> unit (-0.5 * d, std::sqrt (std::max (0.0, 1.0 - 0.25 * d * d)));

            std::complex<double> pLo, pHi;
            bandTransformRoots (bw * unit, pLo, pHi);

            double d1, d0;
            quadFromRoot (pLo, d1, d0);
            if (isNotch) b.add ({ 1.0, 0.0, 1.0, 1.0, d1, d0 });
            else         b.add ({ 0.0, bw,  0.0, 1.0, d1, d0 });

            quadFromRoot (pHi, d1, d0);
            if (isNotch) b.add ({ 1.0, 0.0, 1.0, 1.0, d1, d0 });
            else         b.add ({ 0.0, bw,  0.0, 1.0, d1, d0 });
        }

        if (n % 2 == 1)
        {
            if (isNotch) b.add ({ 1.0, 0.0, 1.0, 1.0, bw, 1.0 });
            else         b.add ({ 0.0, bw,  0.0, 1.0, bw, 1.0 });
        }
    }

    /** A constant slope in dB per octave: a straight line on the display.
        G is the linear gain between kFlatTiltLowHz and kFlatTiltHighHz, and the
        band frequency is where the line crosses 0 dB.

        A slope like that is not a rational filter, so it is approximated by
        sixteen gentle first-order shelves, evenly spaced in pitch; their steps
        overlap so far that what is left is a ripple of a few hundredths of a dB.
        The shelves reach two octaves beyond each end, so the line only starts to
        level out outside the audible range. Two shelves make one section. */
    inline void addFlatTilt (AnalogBand& b, double G)
    {
        constexpr int numShelves = 2 * kMaxSections;
        constexpr double margin = 4.0;   // two octaves beyond each end

        const double lowest  = kFlatTiltLowHz / margin;
        const double octaves = std::log2 (kFlatTiltHighHz * margin / lowest);
        const double nominal = std::log2 (kFlatTiltHighHz / kFlatTiltLowHz);

        // sqrt of one shelf's gain: the slope per octave is that of G over the nominal span
        const double halfStep = std::pow (G, 0.5 * octaves / (nominal * numShelves));

        const auto centre = [&] (int i)
        {
            return lowest * std::pow (2.0, (i + 0.5) * octaves / numShelves) / b.freq;
        };

        double atBandFreq = 1.0;

        for (int k = 0; k < kMaxSections; ++k)
        {
            const double z1 = centre (2 * k) / halfStep,  z2 = centre (2 * k + 1) / halfStep;
            const double p1 = centre (2 * k) * halfStep,  p2 = centre (2 * k + 1) * halfStep;

            // (1 + s/z1)(1 + s/z2) / ((1 + s/p1)(1 + s/p2))
            const AnalogSection section { 1.0 / (z1 * z2), 1.0 / z1 + 1.0 / z2, 1.0,
                                          1.0 / (p1 * p2), 1.0 / p1 + 1.0 / p2, 1.0 };
            atBandFreq *= section.magnitudeSquared (1.0);
            b.add (section);
        }

        b.scaleFirst (1.0 / std::sqrt (atBandFreq));
    }

    //==========================================================================
    /** Prewarped bilinear transform of one section. K = tan(w0 / 2). */
    inline Biquad mapBilinear (const AnalogSection& s, double K)
    {
        Biquad q;

        if (s.isFirstOrder())
        {
            // Mapping a first-order section with the second-order formula would
            // leave a cancelling pole/zero pair sitting exactly on Nyquist.
            const double inv1 = 1.0 / (s.A1 + s.A2 * K);
            q.b0 = (s.B1 + s.B2 * K) * inv1;
            q.b1 = (s.B2 * K - s.B1) * inv1;
            q.b2 = 0.0;
            q.a1 = (s.A2 * K - s.A1) * inv1;
            q.a2 = 0.0;
            return q;
        }

        const double K2 = K * K;
        const double inv = 1.0 / (s.A0 + s.A1 * K + s.A2 * K2);

        q.b0 = (s.B0 + s.B1 * K + s.B2 * K2) * inv;
        q.b1 = 2.0 * (s.B2 * K2 - s.B0) * inv;
        q.b2 = (s.B0 - s.B1 * K + s.B2 * K2) * inv;
        q.a1 = 2.0 * (s.A2 * K2 - s.A0) * inv;
        q.a2 = (s.A0 - s.A1 * K + s.A2 * K2) * inv;
        return q;
    }

    /** |1 - r e^{j theta} e^{-jw}|^2, written so it keeps full relative precision for r -> 1. */
    inline double poleFactor (double oneMinusR, double r, double halfAngle)
    {
        const double s = std::sin (halfAngle);
        return oneMinusR * oneMinusR + 4.0 * r * s * s;
    }

    /** Squared magnitude of a digital pole pair, evaluated without cancellation. */
    struct MatchedPoles
    {
        bool complexPair = false;
        double r = 0.0, oneMinusR = 0.0, theta = 0.0;      // complex pair r e^{+-j theta}
        double e1 = 0.0, m1 = 0.0, e2 = 0.0, m2 = 0.0;     // two real poles e, with m = 1 - e
        double a1 = 0.0, a2 = 0.0;

        double power (double w) const
        {
            if (complexPair)
                return poleFactor (oneMinusR, r, 0.5 * (theta - w)) * poleFactor (oneMinusR, r, 0.5 * (theta + w));

            return poleFactor (m1, e1, 0.5 * w) * poleFactor (m2, e2, 0.5 * w);
        }
    };

    /** Places the digital poles exactly where the analog ones are: z = exp(s T). */
    inline MatchedPoles matchPoles (const AnalogSection& s, double w0)
    {
        MatchedPoles p;
        const double disc = s.A1 * s.A1 - 4.0 * s.A0 * s.A2;

        if (disc < 0.0)
        {
            const double re = -s.A1 / (2.0 * s.A0);
            double theta = std::sqrt (-disc) / (2.0 * s.A0) * w0;

            // A pole pair above Nyquist cannot exist. Bend its angle smoothly into
            // the top of the band instead; the numerator fit below absorbs the difference.
            const double knee = 0.8 * kPi;

            if (theta > knee)
                theta = knee + (kPi - knee) * (1.0 - std::exp (-(theta - knee) / (kPi - knee)));

            p.complexPair = true;
            p.theta = theta;
            p.r = std::exp (re * w0);
            p.oneMinusR = -std::expm1 (re * w0);
            p.a1 = -2.0 * p.r * std::cos (theta);
            p.a2 = p.r * p.r;
        }
        else
        {
            const double root = std::sqrt (disc);
            const double p1 = (-s.A1 + root) / (2.0 * s.A0);
            const double p2 = (-s.A1 - root) / (2.0 * s.A0);

            p.e1 = std::exp (p1 * w0);  p.m1 = -std::expm1 (p1 * w0);
            p.e2 = std::exp (p2 * w0);  p.m2 = -std::expm1 (p2 * w0);
            p.a1 = -(p.e1 + p.e2);
            p.a2 = p.e1 * p.e2;
        }

        return p;
    }

    /** Matched mapping of one section. w0 is the band frequency in radians/sample.

        The poles are exact. The numerator is written as a power spectrum
            |N(w)|^2 = N0 (1 - phi) + N1 phi + N2 4 phi (1 - phi),   phi = sin^2(w/2)
        whose three numbers are chosen so that |N|^2 / |D|^2 follows the analog
        magnitude, and is then factored into a minimum-phase biquad numerator.

        Low in the band, three exact points (DC, the section's own frequency,
        Nyquist) are all it takes. Higher up, where the section's shape reaches
        Nyquist, N1 and N2 come from a least-squares fit over the whole range.
        The two are blended by frequency so that sweeping a band never jumps. */
    inline bool mapMatched (const AnalogSection& s, double w0, Biquad& out, bool ownFrequency = false)
    {
        const double nyquistW = kPi / w0;

        if (s.isFirstOrder())
        {
            const double pole = -s.A2 / s.A1;
            const double e = std::exp (pole * w0);
            const double oneMinusE = -std::expm1 (pole * w0);

            const double atDc = std::sqrt (s.magnitudeSquared (0.0)) * oneMinusE;
            const double atNyquist = std::sqrt (s.magnitudeSquared (nyquistW)) * (1.0 + e);

            out.b0 = 0.5 * (atDc + atNyquist);
            out.b1 = 0.5 * (atDc - atNyquist);
            out.b2 = 0.0;
            out.a1 = -e;
            out.a2 = 0.0;
            return out.b0 > 0.0;
        }

        const MatchedPoles poles = matchPoles (s, w0);
        out.a1 = poles.a1;
        out.a2 = poles.a2;

        const auto target = [&] (double w) { return s.magnitudeSquared (w / w0) * poles.power (w); };

        // Where the section does its work. A resonant numerator is most sensitive
        // around its own dip, so that is where it gets pinned.
        const bool fullNumerator = ! isExactlyZero (s.B0) && ! isExactlyZero (s.B2);
        const double poleW = std::sqrt (s.A2 / s.A0);
        const double zeroW = fullNumerator ? std::sqrt (s.B2 / s.B0) : poleW;

        double matchW = fullNumerator ? std::sqrt (poleW * zeroW) : poleW;

        if (fullNumerator && s.B1 * s.B1 < 2.0 * s.B0 * s.B2)
            matchW = zeroW;

        // A band that spreads gentle sections all over the spectrum (Flat Tilt) is served
        // best by the three exact points alone, whatever its band frequency: measured, the
        // result stays within 1.5 % of the gain, the least-squares fit strays twice as far.
        const double wm = ownFrequency ? std::min (matchW * w0, 0.6 * kPi)
                                       : std::min (matchW * w0, 0.5 * (w0 + kPi));
        const double sm = std::sin (0.5 * wm);
        const double phi1 = sm * sm, phi0 = 1.0 - phi1;

        const double n0 = target (0.0);
        const double nyq = target (kPi);

        double n1 = nyq;
        double n2 = (target (wm) - n0 * phi0 - nyq * phi1) / (4.0 * phi0 * phi1);

        // Blend towards the least-squares fit between 0.03 pi and 0.12 pi.
        const double t = ownFrequency ? 0.0 : std::clamp ((w0 - 0.03 * kPi) / (0.09 * kPi), 0.0, 1.0);
        const double blend = t * t * (3.0 - 2.0 * t);

        if (blend > 0.0)
        {
            constexpr int numSweep = 6;
            const double top = 0.97 * kPi;
            const double lo = std::min (top, 0.5 * std::min (poleW, zeroW) * w0);
            const double ratio = std::pow (top / lo, 1.0 / (numSweep - 1));

            double a11 = 0.0, a12 = 0.0, a22 = 0.0, r1 = 0.0, r2 = 0.0;

            const auto accumulate = [&] (double w, double weight)
            {
                const double sn = std::sin (0.5 * w);
                const double p1 = sn * sn, p0 = 1.0 - p1, p2 = 4.0 * p0 * p1;
                const double inv = 1.0 / std::max (target (w), 1.0e-300);
                const double x1 = p1 * inv, x2 = p2 * inv, y = 1.0 - n0 * p0 * inv;

                a11 += weight * x1 * x1;  a12 += weight * x1 * x2;  a22 += weight * x2 * x2;
                r1  += weight * x1 * y;   r2  += weight * x2 * y;
            };

            double w = lo;
            for (int i = 0; i < numSweep; ++i, w *= ratio)
                accumulate (std::min (w, top), 1.0);

            if (poleW * w0 < top) accumulate (poleW * w0, 2.0);
            if (zeroW * w0 < top) accumulate (zeroW * w0, 2.0);
            accumulate (kPi, 3.0);

            const double det = a11 * a22 - a12 * a12;

            if (std::abs (det) > 1.0e-300)
            {
                // Never let the fit drift more than 12 dB from the true value at Nyquist.
                const double fitN1 = std::clamp ((r1 * a22 - r2 * a12) / det, 0.0625 * nyq, 16.0 * nyq);
                const double fitN2 = (a11 * r2 - a12 * r1) / det;

                if (std::isfinite (fitN1) && std::isfinite (fitN2))
                {
                    n1 += blend * (fitN1 - n1);
                    n2 += blend * (fitN2 - n2);
                }
            }
        }

        // Minimum-phase spectral factor. A spectrum that dips below zero cannot
        // be built; clamping it would put a notch where none belongs, so such a
        // section is reported as failed and the band falls back to bilinear.
        const double r0 = std::sqrt (std::max (n0, 0.0)), rn = std::sqrt (std::max (n1, 0.0));
        const double sum = 0.5 * (r0 + rn);
        const double rad = sum * sum + n2;

        if (! (rad >= 0.0) || ! std::isfinite (rad) || ! (sum > 0.0))
            return false;

        out.b0 = 0.5 * (sum + std::sqrt (rad));
        out.b1 = 0.5 * (r0 - rn);
        out.b2 = -n2 / (4.0 * out.b0);
        return true;
    }
}

//==============================================================================
/** Builds the analog prototype for one band. Real-time safe: no allocation. */
inline AnalogBand designAnalog (const BandSpec& spec, double maxFreq = kMaxFreq)
{
    using namespace detail;

    AnalogBand b;
    b.freq = std::clamp (spec.freq, kMinFreq, std::min (kMaxFreq, maxFreq));

    const double q   = std::clamp (spec.q, kMinQ, kMaxQ);
    const double gdb = std::clamp (spec.gainDb, -kMaxGainDb, kMaxGainDb);
    const double G   = dbToGain (gdb);
    const bool flat  = std::abs (gdb) < 1.0e-6;

    switch (spec.type)
    {
        case FilterType::Bell:
            b.matched = true;
            if (! flat)
                addBell (b, G, orderForBandType (spec.slopeIndex), q);
            break;

        case FilterType::LowShelf:
            b.matched = true;
            if (! flat)
                addLowShelf (b, G, orderForCutOrShelf (spec.slopeIndex), q);
            break;

        case FilterType::HighShelf:
            b.matched = true;
            if (! flat)
                addHighShelf (b, G, orderForCutOrShelf (spec.slopeIndex), q);
            break;

        case FilterType::TiltShelf:
            b.matched = true;
            if (! flat)
            {
                addHighShelf (b, G, orderForCutOrShelf (spec.slopeIndex), q);
                b.scaleFirst (1.0 / std::sqrt (G));
            }
            break;

        case FilterType::LowCut:
        {
            const int order = orderForCutOrShelf (spec.slopeIndex);

            for (int k = 0; k < order / 2; ++k)
            {
                double d = butterworthDamping (k, order);
                if (k == 0) d /= q;
                b.add ({ 1.0, 0.0, 0.0, 1.0, d, 1.0 });
            }

            if (order % 2 == 1)
                b.add ({ 0.0, 1.0, 0.0, 0.0, 1.0, 1.0 });
            break;
        }

        case FilterType::HighCut:
        {
            b.matched = true;
            const int order = orderForCutOrShelf (spec.slopeIndex);

            for (int k = 0; k < order / 2; ++k)
            {
                double d = butterworthDamping (k, order);
                if (k == 0) d /= q;
                b.add ({ 0.0, 0.0, 1.0, 1.0, d, 1.0 });
            }

            if (order % 2 == 1)
                b.add ({ 0.0, 0.0, 1.0, 0.0, 1.0, 1.0 });
            break;
        }

        case FilterType::Notch:
            addBandPassOrNotch (b, orderForBandType (spec.slopeIndex), q, true);
            break;

        case FilterType::BandPass:
            b.matched = true;
            addBandPassOrNotch (b, orderForBandType (spec.slopeIndex), q, false);
            break;

        case FilterType::AllPass:
            if (spec.slopeIndex == 0)
                b.add ({ 0.0, -1.0, 1.0, 0.0, 1.0, 1.0 });
            else
                b.add ({ 1.0, -1.0 / q, 1.0, 1.0, 1.0 / q, 1.0 });
            break;

        case FilterType::FlatTilt:
            b.matched = true;
            b.spread = true;
            if (! flat)
                addFlatTilt (b, G);
            break;
    }

    return b;
}

/** Maps an analog prototype to biquads with one specific mapping.
    If the matched mapping cannot represent a section, the whole band is
    mapped bilinearly instead and BandCoeffs::matched says so. */
inline BandCoeffs mapBand (const AnalogBand& analog, double sampleRate, bool matched)
{
    const double w0 = 2.0 * kPi * analog.freq / sampleRate;

    BandCoeffs out;
    out.numSections = analog.numSections;
    out.matched = matched && analog.matched;

    if (out.matched)
    {
        for (int i = 0; i < analog.numSections && out.matched; ++i)
            out.matched = detail::mapMatched (analog.sections[(size_t) i], w0, out.sections[(size_t) i], analog.spread);
    }

    if (! out.matched)
    {
        const double K = std::tan (0.5 * w0);

        for (int i = 0; i < analog.numSections; ++i)
            out.sections[(size_t) i] = detail::mapBilinear (analog.sections[(size_t) i], K);
    }

    return out;
}

/** Largest deviation in dB between a digital cascade and its analog prototype.
    Probed where either of them has its features: at every section's analog pole
    and zero frequency, at the frequencies where the digital sections actually
    dip, and on a sweep from two octaves below the band up to the top.
    Regions the prototype attenuates by more than 40 dB are not judged. */
inline double mappingError (const AnalogBand& analog, const BandCoeffs& digital, double sampleRate)
{
    const double top = 0.475 * sampleRate;
    double worst = 0.0;

    const auto probe = [&] (double freq)
    {
        if (! (freq > 1.0) || freq > top)
            return;

        double ideal = 1.0;
        for (int i = 0; i < analog.numSections; ++i)
            ideal *= analog.sections[(size_t) i].magnitudeSquared (freq / analog.freq);

        if (ideal < 1.0e-4)
            return;

        const double s = std::sin (kPi * freq / sampleRate);
        const double actual = std::max (digital.magnitudeSquaredFromPhi (s * s), 1.0e-30);

        worst = std::max (worst, std::abs (10.0 * std::log10 (actual / ideal)));
    };

    for (int i = 0; i < analog.numSections; ++i)
    {
        const auto& sec = analog.sections[(size_t) i];

        if (! sec.isFirstOrder())
        {
            probe (analog.freq * std::sqrt (sec.A2 / sec.A0));

            if (! isExactlyZero (sec.B0) && ! isExactlyZero (sec.B2))
                probe (analog.freq * std::sqrt (sec.B2 / sec.B0));
        }

        // Where this digital section's zeros and poles really sit.
        const auto& d = digital.sections[(size_t) i];

        if (d.b0 * d.b2 > 0.0 && d.b1 * d.b1 < 4.0 * d.b0 * d.b2)
            probe (std::acos (std::clamp (-d.b1 / (2.0 * std::sqrt (d.b0 * d.b2)), -1.0, 1.0)) * sampleRate / (2.0 * kPi));

        if (d.a2 > 0.0 && d.a1 * d.a1 < 4.0 * d.a2)
            probe (std::acos (std::clamp (-d.a1 / (2.0 * std::sqrt (d.a2)), -1.0, 1.0)) * sampleRate / (2.0 * kPi));
    }

    constexpr int numSweep = 20;
    const double lo = std::min (0.25 * analog.freq, 0.25 * top);
    const double ratio = std::pow (top / lo, 1.0 / (numSweep - 1));

    double f = lo;
    for (int i = 0; i < numSweep; ++i, f *= ratio)
        probe (std::min (f, top));

    probe (analog.freq);

    return worst;
}

/** Designs the biquad cascade for one band. Real-time safe: no allocation. */
inline BandCoeffs designBand (const BandSpec& spec, double sampleRate, Mapping mapping = Mapping::Automatic)
{
    const AnalogBand analog = designAnalog (spec, 0.49 * sampleRate);

    if (! analog.matched || mapping == Mapping::Bilinear || analog.numSections == 0)
        return mapBand (analog, sampleRate, false);

    const BandCoeffs matched = mapBand (analog, sampleRate, true);

    if (mapping == Mapping::Matched || ! matched.matched)
        return matched;

    const BandCoeffs bilinear = mapBand (analog, sampleRate, false);

    // A small bias keeps the choice from flickering where both are near perfect.
    const double errorMatched  = mappingError (analog, matched, sampleRate);
    const double errorBilinear = mappingError (analog, bilinear, sampleRate);

    return errorMatched <= errorBilinear + 0.1 ? matched : bilinear;
}

} // namespace hdp
