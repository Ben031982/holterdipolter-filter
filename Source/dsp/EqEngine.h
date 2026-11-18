// holterdipolter Filter - audio engine
//
// Pure C++17, no JUCE. Runs up to 24 bands in series, each as a cascade of
// transposed-direct-form-II biquads in double precision.
//
//  - Frequency, gain and Q are smoothed, and the coefficients are refreshed
//    every 32 samples while a value is moving: no zipper noise when dragging.
//  - Anything that changes a band's structure (shape, slope, placement,
//    bypass, or the filter mapping flipping) is crossfaded between the old and
//    the new filter instead of being switched: no clicks.
//  - Solo replaces the output by the frequency range the chosen band works on.
//  - Auto gain estimates how much the curve changes the level of a pink
//    spectrum and compensates for it.
//  - Dynamic EQ: a bell or shelf can add up to its dynamic range to its gain,
//    depending on how loud the input is in the range that band works on.

#pragma once

#include "FilterDesign.h"

namespace hdp
{

struct BandParams
{
    bool active = false;
    FilterType type = FilterType::Bell;
    Placement placement = Placement::Stereo;
    double freq   = 1000.0;
    double gainDb = 0.0;
    double q      = 1.0;
    int slopeIndex = 1;

    // Dynamic EQ. The range is added to the gain as the band's part of the input
    // rises above the threshold; 0 dB makes the band an ordinary, static one.
    double dynRangeDb  = 0.0;
    double thresholdDb = -30.0;
    bool autoThreshold = true;

    // What the dynamics listen to: this track (false) or the track at the sidechain
    // input (true). With the sidechain chosen and nothing connected, the band rests.
    bool dynFromSidechain = false;
};

/** A dynamic band has reached its full range this many dB above its threshold. */
constexpr double kDynamicSpanDb = 12.0;

/** Auto threshold: the band starts to act when its range holds this many dB less
    than the whole signal, so it responds to what stands out, at any input level. */
constexpr double kAutoThresholdBelowInputDb = 12.0;

/** Below this the input counts as silence and no dynamic band acts. */
constexpr double kDynamicFloorDb = -70.0;

/** Timing of a dynamic band: how long the level is averaged over, and how fast
    the band takes up its range and lets go of it again (time constants). */
constexpr double kDynamicLevelSeconds   = 0.005;
constexpr double kDynamicAttackSeconds  = 0.005;
constexpr double kDynamicReleaseSeconds = 0.1;

inline bool bandIsDynamic (const BandParams& p)
{
    return p.active && typeHasDynamics (p.type) && ! isExactlyZero (p.dynRangeDb);
}

/** How much of its range a dynamic band uses: 0 at or below the threshold,
    1 from kDynamicSpanDb above it, and a smooth curve in between. */
inline double dynamicAmount (double dbAboveThreshold)
{
    const double x = std::clamp (dbAboveThreshold / kDynamicSpanDb, 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

/** The filter you hear while a band is soloed: the range that band works on. */
inline BandSpec soloSpecFor (const BandParams& p)
{
    BandSpec spec;
    spec.freq = p.freq;
    spec.gainDb = 0.0;
    spec.slopeIndex = 3;   // 24 dB/oct

    switch (p.type)
    {
        case FilterType::LowShelf:
        case FilterType::LowCut:
            spec.type = FilterType::HighCut;    // everything below the corner
            spec.q = 1.0;
            break;

        case FilterType::HighShelf:
        case FilterType::HighCut:
            spec.type = FilterType::LowCut;     // everything above the corner
            spec.q = 1.0;
            break;

        case FilterType::Bell:
        case FilterType::Notch:
        case FilterType::BandPass:
        case FilterType::TiltShelf:
        case FilterType::AllPass:
        case FilterType::FlatTilt:
            spec.type = FilterType::BandPass;
            spec.q = std::clamp (p.q, 0.5, 12.0);
            break;
    }

    return spec;
}

class EqEngine
{
public:
    static constexpr int kChunk = 32;

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;

        // One-pole smoothing applied once per chunk, about 15 ms time constant.
        smoothCoeff = 1.0 - std::exp (-(double) kChunk / (0.015 * sampleRate));
        gainSmoothCoeff = 1.0 - std::exp (-1.0 / (0.03 * sampleRate));
        fadeLength = std::max (kChunk, (int) (0.012 * sampleRate));

        // Auto gain looks at 30 Hz to 16 kHz, equally spaced in pitch.
        const double top = std::min (16000.0, 0.45 * sampleRate);

        for (int i = 0; i < kAutoPoints; ++i)
        {
            const double freq = 30.0 * std::pow (top / 30.0, (double) i / (kAutoPoints - 1));
            const double s = std::sin (kPi * freq / sampleRate);
            autoPhi[(size_t) i] = s * s;
        }

        reset();
    }

    void reset()
    {
        int index = 0;

        for (auto& b : bands)
        {
            b.current = Voice();
            b.previous = Voice();
            b.dynamics = Dynamics();
            b.fading = false;
            b.chunksSinceChoice = index++ % 8;
        }

        solo = Voice();
        soloCurrent = -1;
        soloMix = 0.0;
        inputPower.fill (0.0);
        sidePower.fill (0.0);
        justReset = true;
    }

    /** Sets the target values of a band. Call from the audio thread before process(). */
    void setBand (int index, const BandParams& p)
    {
        if (index >= 0 && index < kMaxBands)
            bands[(size_t) index].target = p;
    }

    void setGainScale (double scale)       { gainScale = scale; }
    void setOutputGainDb (double db)       { outputGainDb = db; }
    void setAutoGain (bool shouldBeOn)     { autoGainOn = shouldBeOn; }
    void setPhaseInvert (bool shouldInvert) { polarity = shouldInvert ? -1.0 : 1.0; }
    void setSoloBand (int band)            { soloTarget = (band >= 0 && band < kMaxBands) ? band : -1; }

    /** What a dynamic band is adding to its gain right now, in dB (0 for a static band). */
    double getDynamicGainDb (int band) const
    {
        return band >= 0 && band < kMaxBands ? bands[(size_t) band].dynamics.offsetDb : 0.0;
    }

    /** The compensation auto gain currently aims for, in dB (0 when it is off). */
    double getAutoGainDb() const           { return autoGainOn ? autoGainDb : 0.0; }

    /** Processes in place. right may be nullptr for mono.
        sideLeft / sideRight are the sidechain input, for dynamic bands that listen to
        it; both may be nullptr (nothing connected), sideRight alone for a mono sidechain. */
    void process (float* left, float* right, int numSamples,
                  const float* sideLeft = nullptr, const float* sideRight = nullptr)
    {
        const bool stereo = right != nullptr;
        double l[kChunk], r[kChunk], dryL[kChunk], dryR[kChunk], sideL[kChunk], sideR[kChunk];

        for (int offset = 0; offset < numSamples; offset += kChunk)
        {
            const int n = std::min (kChunk, numSamples - offset);

            for (int i = 0; i < n; ++i)
            {
                l[i] = (double) left[offset + i];
                r[i] = stereo ? (double) right[offset + i] : 0.0;
            }

            const bool soloInvolved = soloTarget >= 0 || soloCurrent >= 0 || soloMix > 0.0;

            bool anyDynamic = false;
            for (const auto& b : bands)
                anyDynamic = anyDynamic || b.dynamics.running || bandIsDynamic (b.target);

            if (soloInvolved || anyDynamic)
            {
                std::copy (l, l + n, dryL);
                std::copy (r, r + n, dryR);
            }

            // Dynamic bands listen to the input of the plugin, not to each other.
            DetectorInput detector;

            if (anyDynamic)
            {
                detector.left = dryL;
                detector.right = stereo ? dryR : nullptr;
                detector.numSamples = n;
                detector.level   = 1.0 - std::exp (-(double) n / (kDynamicLevelSeconds * sampleRate));
                detector.attack  = 1.0 - std::exp (-(double) n / (kDynamicAttackSeconds * sampleRate));
                detector.release = 1.0 - std::exp (-(double) n / (kDynamicReleaseSeconds * sampleRate));
                followLevel (detector.left, detector.right, n, detector.level, inputPower);
                detector.inputPower = inputPower.data();

                if (sideLeft != nullptr)
                {
                    for (int i = 0; i < n; ++i)
                    {
                        sideL[i] = (double) sideLeft[offset + i];
                        sideR[i] = sideRight != nullptr ? (double) sideRight[offset + i] : sideL[i];
                    }

                    detector.sideLeft = sideL;
                    detector.sideRight = sideR;
                    followLevel (sideL, sideR, n, detector.level, sidePower);
                    detector.sidePower = sidePower.data();
                }
            }

            bool curveChanged = justReset;

            for (auto& b : bands)
            {
                if (anyDynamic)
                    curveChanged = b.updateDynamics (detector, sampleRate, gainScale) || curveChanged;

                const bool moved = b.update (sampleRate, smoothCoeff, gainScale, justReset);

                // Auto gain compensates for the curve as it is set, not for what the
                // dynamics do to it; otherwise it would undo them.
                curveChanged = (b.dynamics.running ? b.refreshStaticCurve (sampleRate, gainScale) : moved) || curveChanged;

                b.process (l, stereo ? r : nullptr, n, fadeLength);
            }

            updateAutoGain (curveChanged, stereo);

            if (soloInvolved)
                processSolo (dryL, dryR, l, r, n, stereo);

            // Flipping the polarity glides through zero with the gain smoothing: a short dip, no click.
            const double gainTarget = polarity * dbToGain (outputGainDb + getAutoGainDb());

            if (justReset)
                outGain = gainTarget;

            for (int i = 0; i < n; ++i)
            {
                outGain += gainSmoothCoeff * (gainTarget - outGain);
                left[offset + i] = (float) (l[i] * outGain);

                if (stereo)
                    right[offset + i] = (float) (r[i] * outGain);
            }

            justReset = false;
        }
    }

private:
    static constexpr int kAutoPoints = 48;

    struct ChannelState
    {
        std::array<double, kMaxSections> z1 {}, z2 {};
    };

    /** What the dynamic bands get to hear in one chunk. */
    struct DetectorInput
    {
        const double* left = nullptr;
        const double* right = nullptr;      // nullptr for mono
        const double* inputPower = nullptr; // level of the whole input, one per placement
        const double* sideLeft = nullptr;   // the sidechain; nullptr if none is connected
        const double* sideRight = nullptr;
        const double* sidePower = nullptr;
        int numSamples = 0;
        double level = 1.0, attack = 1.0, release = 1.0;   // per-chunk smoothing factors
    };

    static double meanSquare (const double* x, int n)
    {
        double sum = 0.0;

        for (int i = 0; i < n; ++i)
            sum += x[i] * x[i];

        return sum / (double) std::max (1, n);
    }

    /** Level in dB of a mean square, read so that a full-scale sine is 0 dB. */
    static double powerToDb (double meanSquareValue)
    {
        return 10.0 * std::log10 (2.0 * meanSquareValue + 1.0e-30);
    }

    /** Tracks the level of a whole signal for each way a band can be placed. */
    static void followLevel (const double* left, const double* right, int n, double smoothing,
                             std::array<double, kNumPlacements>& envelope)
    {
        struct { const double* left; const double* right; } in { left, right };
        double power[kNumPlacements] = {};

        if (in.right == nullptr)
        {
            const double p = meanSquare (in.left, n);
            power[(int) Placement::Stereo] = power[(int) Placement::Left] = power[(int) Placement::Mid] = p;
        }
        else
        {
            double mid = 0.0, side = 0.0;

            for (int i = 0; i < n; ++i)
            {
                const double m = 0.5 * (in.left[i] + in.right[i]), sd = 0.5 * (in.left[i] - in.right[i]);
                mid += m * m;
                side += sd * sd;
            }

            power[(int) Placement::Left]   = meanSquare (in.left, n);
            power[(int) Placement::Right]  = meanSquare (in.right, n);
            power[(int) Placement::Stereo] = std::max (power[(int) Placement::Left], power[(int) Placement::Right]);
            power[(int) Placement::Mid]    = mid / (double) std::max (1, n);
            power[(int) Placement::Side]   = side / (double) std::max (1, n);
        }

        for (int i = 0; i < kNumPlacements; ++i)
            envelope[(size_t) i] += smoothing * (power[i] - envelope[(size_t) i]);
    }

    //==========================================================================
    /** One filter instance: a cascade, its state, and how it sits in the stereo image. */
    struct Voice
    {
        bool on = false;   // false: passes audio through untouched
        FilterType type = FilterType::Bell;
        Placement placement = Placement::Stereo;
        int slopeIndex = 1;
        double logFreq = 0.0, gainDb = 0.0, logQ = 0.0;

        BandCoeffs coeffs;
        ChannelState state[2];

        void clearState()
        {
            for (auto& s : state)
            {
                s.z1.fill (0.0);
                s.z2.fill (0.0);
            }
        }

        BandSpec spec() const
        {
            BandSpec s;
            s.type = type;
            s.freq = std::exp (logFreq);
            s.gainDb = gainDb;
            s.q = std::exp (logQ);
            s.slopeIndex = slopeIndex;
            return s;
        }

        static void runCascade (const BandCoeffs& c, ChannelState& st, double* x, int n)
        {
            for (int s = 0; s < c.numSections; ++s)
            {
                const Biquad& q = c.sections[(size_t) s];
                double z1 = st.z1[(size_t) s], z2 = st.z2[(size_t) s];

                for (int i = 0; i < n; ++i)
                {
                    const double in  = x[i];
                    const double out = q.b0 * in + z1;
                    z1 = q.b1 * in - q.a1 * out + z2;
                    z2 = q.b2 * in - q.a2 * out;
                    x[i] = out;
                }

                st.z1[(size_t) s] = z1;
                st.z2[(size_t) s] = z2;
            }
        }

        /** Filters the part of the signal this voice is placed on; the rest passes. */
        void process (double* l, double* r, int n)
        {
            if (! on || coeffs.numSections == 0)
                return;

            if (r == nullptr)
            {
                // Mono: everything except "right only" and "side only" acts on the one channel.
                if (placement != Placement::Right && placement != Placement::Side)
                    runCascade (coeffs, state[0], l, n);

                return;
            }

            switch (placement)
            {
                case Placement::Stereo:
                    runCascade (coeffs, state[0], l, n);
                    runCascade (coeffs, state[1], r, n);
                    break;

                case Placement::Left:
                    runCascade (coeffs, state[0], l, n);
                    break;

                case Placement::Right:
                    runCascade (coeffs, state[1], r, n);
                    break;

                case Placement::Mid:
                case Placement::Side:
                {
                    double m[kChunk], s[kChunk];

                    for (int i = 0; i < n; ++i)
                    {
                        m[i] = 0.5 * (l[i] + r[i]);
                        s[i] = 0.5 * (l[i] - r[i]);
                    }

                    if (placement == Placement::Mid) runCascade (coeffs, state[0], m, n);
                    else                             runCascade (coeffs, state[1], s, n);

                    for (int i = 0; i < n; ++i)
                    {
                        l[i] = m[i] + s[i];
                        r[i] = m[i] - s[i];
                    }
                    break;
                }
            }
        }

        /** Solo: only the part of the signal this voice is placed on, filtered. */
        void processIsolated (double* l, double* r, int n)
        {
            if (r == nullptr)
            {
                runCascade (coeffs, state[0], l, n);
                return;
            }

            switch (placement)
            {
                case Placement::Stereo:
                    runCascade (coeffs, state[0], l, n);
                    runCascade (coeffs, state[1], r, n);
                    break;

                case Placement::Left:
                    runCascade (coeffs, state[0], l, n);
                    std::fill (r, r + n, 0.0);
                    break;

                case Placement::Right:
                    runCascade (coeffs, state[1], r, n);
                    std::fill (l, l + n, 0.0);
                    break;

                case Placement::Mid:
                    for (int i = 0; i < n; ++i)
                        l[i] = 0.5 * (l[i] + r[i]);

                    runCascade (coeffs, state[0], l, n);
                    std::copy (l, l + n, r);
                    break;

                case Placement::Side:
                    for (int i = 0; i < n; ++i)
                        l[i] = 0.5 * (l[i] - r[i]);

                    runCascade (coeffs, state[1], l, n);

                    for (int i = 0; i < n; ++i)
                        r[i] = -l[i];
                    break;
            }
        }
    };

    //==========================================================================
    /** The listening half of a dynamic band. */
    struct Dynamics
    {
        bool running = false;
        BandCoeffs detector;              // isolates the range the band works on
        ChannelState state[2];
        FilterType designedType = FilterType::Bell;
        double designedFreq = 0.0, designedQ = 0.0;
        double detectorTrim = 1.0;        // makes a tone at the band's frequency read its true level

        double power = 0.0;               // level in that range, smoothed
        double amount = 0.0;              // how much of the range is in use, 0 to 1
        double offsetDb = 0.0;            // what that adds to the band's gain

        BandCoeffs staticCurve;           // the band without its dynamics, for auto gain
        bool staticValid = false;
        double staticFreq = 0.0, staticQ = 0.0, staticGain = 0.0;
        int staticSlope = -1;
        FilterType staticType = FilterType::Bell;
    };

    struct Band
    {
        BandParams target;
        Dynamics dynamics;

        Voice current;     // what the band is, or is becoming
        Voice previous;    // what it was; only heard while fading
        bool fading = false;
        int fadePos = 0;
        int chunksSinceChoice = 0;   // staggered across bands so they do not all choose at once

        static double targetLogFreq (const BandParams& p)   { return std::log (std::clamp (p.freq, kMinFreq, kMaxFreq)); }
        static double targetLogQ (const BandParams& p)      { return std::log (std::clamp (p.q, kMinQ, kMaxQ)); }

        static double staticGain (const BandParams& p, double scale)
        {
            return std::clamp (p.gainDb * scale, -kMaxGainDb, kMaxGainDb);
        }

        /** The gain the band is heading for: what is set, plus what the dynamics add. */
        double targetGain (const BandParams& p, double scale) const
        {
            return std::clamp (p.gainDb * scale + dynamics.offsetDb, -kMaxGainDb, kMaxGainDb);
        }

        /** Listens to the input and works out how much of the dynamic range to use.
            Returns true when the band has just stopped being dynamic. */
        bool updateDynamics (const DetectorInput& in, double sr, double scale)
        {
            auto& d = dynamics;

            // In mono there is no right and no side channel for such a band to hear.
            const bool unheard = in.right == nullptr
                              && (target.placement == Placement::Right || target.placement == Placement::Side);

            if (! bandIsDynamic (target) || unheard)
            {
                if (! d.running)
                    return false;

                d = Dynamics();
                return true;
            }

            const double freq = std::clamp (target.freq, kMinFreq, kMaxFreq);
            const double q = std::clamp (target.q, kMinQ, kMaxQ);

            if (! d.running || d.designedType != target.type
                || ! isExactlyZero (d.designedFreq - freq) || ! isExactlyZero (d.designedQ - q))
            {
                if (! d.running || d.designedType != target.type)
                    for (auto& st : d.state) { st.z1.fill (0.0); st.z2.fill (0.0); }

                BandParams shaped = target;
                shaped.freq = freq;
                shaped.q = q;

                d.detector = designBand (soloSpecFor (shaped), sr, Mapping::Bilinear);

                // A band pass is not exactly at 0 dB in its centre; the cuts used for shelves are flat.
                d.detectorTrim = target.type == FilterType::Bell
                                     ? dbToGain (-2.0 * d.detector.magnitudeDb (std::min (freq, 0.49 * sr), sr))
                                     : 1.0;
                d.designedType = target.type;
                d.designedFreq = freq;
                d.designedQ = q;
                d.running = true;
            }

            const int n = in.numSamples;
            double x[kChunk];
            double heard = 0.0;

            const auto listen = [&] (int channel)
            {
                Voice::runCascade (d.detector, d.state[channel], x, n);
                return meanSquare (x, n);
            };

            // The band hears either its own track or the one at the sidechain input.
            const bool fromSide = target.dynFromSidechain;
            const bool deaf = fromSide && in.sideLeft == nullptr;

            struct { const double* left; const double* right; } source { fromSide ? in.sideLeft : in.left,
                                                                         fromSide ? in.sideRight : in.right };
            const double* wholePower = fromSide ? in.sidePower : in.inputPower;

            if (deaf)
            {
                heard = 0.0;    // nothing connected: the band comes to rest
            }
            else if (source.right == nullptr)
            {
                std::copy (source.left, source.left + n, x);
                heard = listen (0);
            }
            else
            {
                const auto& in2 = source;

                switch (target.placement)
                {
                    case Placement::Stereo:
                        // Both channels get the same gain, so the louder one decides.
                        std::copy (in2.left, in2.left + n, x);
                        heard = listen (0);
                        std::copy (in2.right, in2.right + n, x);
                        heard = std::max (heard, listen (1));
                        break;

                    case Placement::Left:
                        std::copy (in2.left, in2.left + n, x);
                        heard = listen (0);
                        break;

                    case Placement::Right:
                        std::copy (in2.right, in2.right + n, x);
                        heard = listen (1);
                        break;

                    case Placement::Mid:
                        for (int i = 0; i < n; ++i) x[i] = 0.5 * (in2.left[i] + in2.right[i]);
                        heard = listen (0);
                        break;

                    case Placement::Side:
                        for (int i = 0; i < n; ++i) x[i] = 0.5 * (in2.left[i] - in2.right[i]);
                        heard = listen (1);
                        break;
                }
            }

            d.power += in.level * (heard * d.detectorTrim - d.power);

            const double levelDb = powerToDb (d.power);
            const double threshold = target.autoThreshold && ! deaf
                                         ? powerToDb (wholePower[(int) target.placement]) - kAutoThresholdBelowInputDb
                                         : target.thresholdDb;

            const double wanted = levelDb > kDynamicFloorDb ? dynamicAmount (levelDb - threshold) : 0.0;

            d.amount += (wanted > d.amount ? in.attack : in.release) * (wanted - d.amount);

            if (d.amount < 1.0e-6)
                d.amount = 0.0;

            d.offsetDb = d.amount * target.dynRangeDb * scale;
            return false;
        }

        /** Keeps the band's curve without dynamics up to date. Returns true if it changed. */
        bool refreshStaticCurve (double sr, double scale)
        {
            auto& d = dynamics;
            const double freq = std::clamp (target.freq, kMinFreq, kMaxFreq);
            const double q = std::clamp (target.q, kMinQ, kMaxQ);
            const double gain = staticGain (target, scale);

            if (d.staticValid && d.staticType == target.type && d.staticSlope == target.slopeIndex
                && isExactlyZero (d.staticFreq - freq) && isExactlyZero (d.staticQ - q) && isExactlyZero (d.staticGain - gain))
                return false;

            BandSpec spec;
            spec.type = target.type;
            spec.freq = freq;
            spec.q = q;
            spec.gainDb = gain;
            spec.slopeIndex = target.slopeIndex;

            d.staticCurve = designBand (spec, sr);
            d.staticValid = true;
            d.staticType = target.type;
            d.staticSlope = target.slopeIndex;
            d.staticFreq = freq;
            d.staticQ = q;
            d.staticGain = gain;
            return true;
        }

        Voice voiceFromTarget (double sr, double scale) const
        {
            Voice v;
            v.on = target.active;
            v.type = target.type;
            v.placement = target.placement;
            v.slopeIndex = target.slopeIndex;
            v.logFreq = targetLogFreq (target);
            v.logQ = targetLogQ (target);
            v.gainDb = targetGain (target, scale);

            if (v.on)
                v.coeffs = designBand (v.spec(), sr);

            return v;
        }

        bool structureDiffers() const
        {
            if (current.on != target.active)
                return true;

            return target.active && (current.type != target.type
                                     || current.slopeIndex != target.slopeIndex
                                     || current.placement != target.placement);
        }

        void startFade()
        {
            fading = true;
            fadePos = 0;
        }

        /** Moves the band towards its target. Returns true if its response changed. */
        bool update (double sr, double smooth, double scale, bool snap)
        {
            if (snap)
            {
                current = voiceFromTarget (sr, scale);
                previous = Voice();
                fading = false;
                return true;
            }

            if (! fading && structureDiffers())
            {
                // A different filter altogether: let the old one ring out underneath the new one.
                previous = current;
                current = voiceFromTarget (sr, scale);
                startFade();
                return true;
            }

            if (! current.on)
                return false;

            const double dF = targetLogFreq (target) - current.logFreq;
            const double dQ = targetLogQ (target) - current.logQ;
            const double dG = targetGain (target, scale) - current.gainDb;

            if (isExactlyZero (dF) && isExactlyZero (dQ) && isExactlyZero (dG))
                return false;

            const bool closeEnough = std::abs (dF) < 1.0e-5 && std::abs (dQ) < 1.0e-5 && std::abs (dG) < 1.0e-4;
            const double step = closeEnough ? 1.0 : smooth;

            current.logFreq += step * dF;
            current.logQ    += step * dQ;
            current.gainDb  += step * dG;

            const int previousSections = current.coeffs.numSections;

            const Mapping sameAsNow = current.coeffs.matched ? Mapping::Matched : Mapping::Bilinear;

            if (fading)
            {
                // Mid-fade the new voice keeps the mapping it started with.
                current.coeffs = designBand (current.spec(), sr, sameAsNow);
            }
            else
            {
                // Choosing the better mapping means designing both and measuring them.
                // While a value is in motion that is done on every eighth chunk only
                // (and once more when it comes to rest); in between the band simply
                // keeps the mapping it has. This keeps moving many bands at once cheap.
                const bool chooseAgain = closeEnough || previousSections == 0 || ++chunksSinceChoice >= 8;

                if (chooseAgain)
                    chunksSinceChoice = 0;

                const BandCoeffs designed = designBand (current.spec(), sr, chooseAgain ? Mapping::Automatic : sameAsNow);

                if (designed.matched != current.coeffs.matched && previousSections > 0 && designed.numSections > 0)
                {
                    // The better mapping for this setting changed. The two sound almost
                    // alike but have different coefficients, so cross over instead of jumping.
                    previous = current;
                    current.coeffs = designed;
                    current.clearState();
                    startFade();
                    return true;
                }

                current.coeffs = designed;
            }

            // A band at exactly 0 dB has no sections; do not reuse stale state when it comes back.
            if (current.coeffs.numSections != previousSections)
                current.clearState();

            return true;
        }

        void process (double* l, double* r, int n, int fadeSamples)
        {
            if (! fading)
            {
                current.process (l, r, n);
                return;
            }

            double oldL[kChunk], oldR[kChunk];
            std::copy (l, l + n, oldL);

            if (r != nullptr)
                std::copy (r, r + n, oldR);

            previous.process (oldL, r != nullptr ? oldR : nullptr, n);
            current.process (l, r, n);

            const double step = 1.0 / (double) fadeSamples;

            for (int i = 0; i < n; ++i)
            {
                const double w = std::min (1.0, (double) (fadePos + i + 1) * step);
                l[i] = oldL[i] + w * (l[i] - oldL[i]);

                if (r != nullptr)
                    r[i] = oldR[i] + w * (r[i] - oldR[i]);
            }

            fadePos += n;

            if (fadePos >= fadeSamples)
            {
                fading = false;
                previous = Voice();
            }
        }
    };

    //==========================================================================
    void updateAutoGain (bool curveChanged, bool stereo)
    {
        if (! autoGainOn)
        {
            autoWasOn = false;
            return;
        }

        autoDirty = autoDirty || curveChanged || ! autoWasOn;
        autoWasOn = true;

        // The estimate is cheap but not free; while a band is being dragged,
        // refreshing it every eighth chunk is plenty.
        if (! autoDirty || (--autoCountdown > 0 && ! justReset))
            return;

        autoDirty = false;
        autoCountdown = 8;

        double sum = 0.0;

        for (int k = 0; k < kAutoPoints; ++k)
        {
            const double phi = autoPhi[(size_t) k];
            double group[kNumPlacements] = { 1.0, 1.0, 1.0, 1.0, 1.0 };

            for (const auto& b : bands)
            {
                const auto& curve = b.dynamics.running && b.dynamics.staticValid ? b.dynamics.staticCurve : b.current.coeffs;

                if (b.current.on && curve.numSections > 0)
                    group[(int) b.current.placement] *= curve.magnitudeSquaredFromPhi (phi);
            }

            const double st = group[(int) Placement::Stereo];

            // Left/right carry half the power each; of mid and side, mid carries most.
            sum += stereo ? st * 0.5 * (group[(int) Placement::Left] + group[(int) Placement::Right])
                               * (0.75 * group[(int) Placement::Mid] + 0.25 * group[(int) Placement::Side])
                          : st * group[(int) Placement::Left] * group[(int) Placement::Mid];
        }

        const double meanPower = std::max (sum / kAutoPoints, 1.0e-12);
        autoGainDb = std::clamp (-10.0 * std::log10 (meanPower), -24.0, 24.0);
    }

    void processSolo (double* dryL, double* dryR, double* l, double* r, int n, bool stereo)
    {
        // Changing to another band, or switching off: fade out first.
        const bool switching = soloCurrent != soloTarget;

        if (switching && soloMix <= 0.0)
        {
            soloCurrent = soloTarget;
            solo = Voice();

            if (soloCurrent >= 0)
            {
                const auto& p = bands[(size_t) soloCurrent].target;
                solo.on = true;
                solo.placement = p.placement;
                solo.logFreq = Band::targetLogFreq (p);
                solo.logQ = Band::targetLogQ (p);
            }
        }

        if (soloCurrent < 0)
        {
            soloMix = 0.0;
            return;
        }

        // Follow the band while it is being moved.
        const auto& p = bands[(size_t) soloCurrent].target;
        solo.logFreq += smoothCoeff * (Band::targetLogFreq (p) - solo.logFreq);
        solo.logQ    += smoothCoeff * (Band::targetLogQ (p) - solo.logQ);
        solo.placement = p.placement;

        BandParams shaped = p;
        shaped.freq = std::exp (solo.logFreq);
        shaped.q = std::exp (solo.logQ);

        const BandSpec spec = soloSpecFor (shaped);

        // The solo filter changes shape with the band's type; a fresh state avoids a burst.
        if (spec.type != solo.type)
        {
            solo.type = spec.type;
            solo.clearState();
        }

        solo.coeffs = designBand (spec, sampleRate, Mapping::Bilinear);
        solo.processIsolated (dryL, stereo ? dryR : nullptr, n);

        const double mixTarget = (soloCurrent == soloTarget) ? 1.0 : 0.0;
        const double step = 1.0 / (double) fadeLength;

        for (int i = 0; i < n; ++i)
        {
            if (soloMix < mixTarget)       soloMix = std::min (mixTarget, soloMix + step);
            else if (soloMix > mixTarget)  soloMix = std::max (mixTarget, soloMix - step);

            l[i] += soloMix * (dryL[i] - l[i]);

            if (stereo)
                r[i] += soloMix * (dryR[i] - r[i]);
        }
    }

    std::array<Band, kMaxBands> bands;

    double sampleRate = 44100.0;
    double smoothCoeff = 0.05;
    double gainSmoothCoeff = 0.001;
    int fadeLength = 512;
    bool justReset = true;

    double gainScale = 1.0;
    double outputGainDb = 0.0;
    double polarity = 1.0;
    double outGain = 1.0;

    bool autoGainOn = false, autoWasOn = false, autoDirty = true;
    int autoCountdown = 0;
    double autoGainDb = 0.0;
    std::array<double, kAutoPoints> autoPhi {};

    std::array<double, kNumPlacements> inputPower {}, sidePower {};

    Voice solo;
    int soloTarget = -1, soloCurrent = -1;
    double soloMix = 0.0;
};

} // namespace hdp
