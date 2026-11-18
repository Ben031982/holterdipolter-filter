#include "EqDisplay.h"

#include "../dsp/SketchFit.h"

namespace hdp::ui
{

namespace
{
    constexpr double displayMinFreq = 10.0;
    constexpr double displayMaxFreq = 30000.0;

    constexpr float displayRanges[] = { 3.0f, 6.0f, 12.0f, 30.0f };
    constexpr float legacyRanges[]  = { 6.0f, 12.0f, 30.0f };          // "uiRange" of 0.2

    // Analyzer options, in the order the menu lists them.
    constexpr float tiltOptions[]  = { 0.0f, 3.0f, 4.5f, 6.0f };       // dB per octave around 1 kHz
    constexpr float floorOptions[] = { -60.0f, -90.0f, -120.0f };      // bottom of the trace
    constexpr int   fftOrders[]    = { 11, 12, 13, 14 };
    constexpr struct { float attack, release; } speedOptions[] = { { 0.3f, 0.05f }, { 0.55f, 0.14f }, { 0.8f, 0.3f } };

    const juce::Identifier rangeProp ("uiRangeDb"), analyzerProp ("uiAnalyzer"), speedProp ("uiAnSpeed"),
                           resolutionProp ("uiAnRes"), tiltProp ("uiAnTilt"), floorProp ("uiAnRange"),
                           pianoProp ("uiPiano"), sideProp ("uiAnSide"), collisionProp ("uiAnCollide"),
                           grabProp ("uiAnGrab"), referenceProp ("uiReference"), referenceShownProp ("uiAnReference");

    constexpr juce::uint32 grabAfterMs = 550;      // how long the mouse has to rest
    constexpr float grabRadius = 14.0f;

    const char* const clipboardTag = "HolterdipolterFilterBands";

    constexpr float headerHeight = 44.0f;
    constexpr float pianoKeyHeight = 16.0f;
    constexpr float pianoLabelHeight = 13.0f;
    constexpr float pianoHeight = pianoKeyHeight + pianoLabelHeight;
    constexpr float lettersFromKeyWidth = 12.0f;   // white keys carry their letter once they are this wide

    constexpr float nodeRadius = 7.0f;
    constexpr float hitRadius = 13.0f;

    bool sameBand (const BandParams& a, const BandParams& b)
    {
        return a.active == b.active && a.type == b.type && a.placement == b.placement
            && a.slopeIndex == b.slopeIndex
            && juce::exactlyEqual (a.freq, b.freq)
            && juce::exactlyEqual (a.gainDb, b.gainDb)
            && juce::exactlyEqual (a.q, b.q)
            && juce::exactlyEqual (a.dynRangeDb, b.dynRangeDb);
    }
}

bool EqDisplay::Snapshot::operator== (const Snapshot& other) const
{
    if (numPoints != other.numPoints
        || ! juce::exactlyEqual (gainScale, other.gainScale)
        || ! juce::exactlyEqual (sampleRate, other.sampleRate)
        || used != other.used)
        return false;

    for (size_t i = 0; i < bands.size(); ++i)
        if (! sameBand (bands[i], other.bands[i]))
            return false;

    return true;
}

//==============================================================================
EqDisplay::EqDisplay (HolterdipolterFilterProcessor& p)
    : processor (p),
      rangeSelector ({ { "3", theme::text }, { "6", theme::text }, { "12", theme::text }, { "30", theme::text } })
{
    // View settings travel with the project, but are not automatable parameters.
    const auto& state = processor.apvts.state;

    rangeDb = state.hasProperty (rangeProp) ? (float) state.getProperty (rangeProp)
                                            : legacyRanges[juce::jlimit (0, 2, (int) state.getProperty ("uiRange", 1))];

    int rangeIndex = 2;
    for (int i = 0; i < 4; ++i)
        if (std::abs (displayRanges[i] - rangeDb) < 0.01f)
            rangeIndex = i;

    rangeDb = displayRanges[rangeIndex];

    analyzerMode       = juce::jlimit (0, 3, (int) state.getProperty (analyzerProp, (int) analyzerBoth));
    analyzerSpeed      = juce::jlimit (0, 2, (int) state.getProperty (speedProp, 1));
    analyzerResolution = juce::jlimit (0, 3, (int) state.getProperty (resolutionProp, 2));
    analyzerTilt       = juce::jlimit (0, 3, (int) state.getProperty (tiltProp, 2));
    analyzerRange      = juce::jlimit (0, 2, (int) state.getProperty (floorProp, 1));
    pianoVisible       = (bool) state.getProperty (pianoProp, false);
    sidechainVisible   = (bool) state.getProperty (sideProp, true);
    collisionsVisible  = (bool) state.getProperty (collisionProp, true);
    grabEnabled        = (bool) state.getProperty (grabProp, true);
    referenceVisible   = (bool) state.getProperty (referenceShownProp, true);

    // The reference curve travels with the project as tenths of a dB.
    {
        const auto values = juce::StringArray::fromTokens (state.getProperty (referenceProp, juce::String()).toString(), ",", "");

        if (values.size() == sketch::kGridPoints)
            for (const auto& v : values)
                reference.push_back (0.1f * (float) v.getIntValue());
    }

    setUpFft();

    addAndMakeVisible (analyzerButton);
    addAndMakeVisible (pianoButton);
    addAndMakeVisible (rangeSelector);
    addAndMakeVisible (sketchButton);

    sketchButton.setText ("SKETCH");
    sketchButton.setTooltip ("Draw the curve you want across the display; the bands for it are added when you let go.");
    sketchButton.onClick = [this] { setSketching (! sketching); };

    analyzerButton.setShowsMenuArrow (true);
    analyzerButton.setTooltip ("What the analyzer shows, and how");
    analyzerButton.onClick = [this] { showAnalyzerMenu(); };

    pianoButton.setText ("PIANO");
    pianoButton.setTooltip ("Piano keys under the display. While they show, dragging a band snaps to notes; hold Shift to move freely.");
    pianoButton.onClick = [this] { setPianoVisible (! pianoVisible); };

    rangeSelector.setSelected (rangeIndex);
    rangeSelector.onSelect = [this] (int index) { setDisplayRange (displayRanges[juce::jlimit (0, 3, index)]); };

    refreshHeader();

    setWantsKeyboardFocus (true);
    setOpaque (false);

    processor.analyzerEnabled.store (analyzerMode != analyzerOff);
    startTimerHz (30);
}

EqDisplay::~EqDisplay()
{
    endDrag();
    processor.analyzerEnabled.store (false);
}

void EqDisplay::storeView (const juce::Identifier& property, const juce::var& value)
{
    processor.apvts.state.setProperty (property, value, nullptr);
}

void EqDisplay::refreshHeader()
{
    static const char* const modes[] = { "OFF", "PRE", "POST", "PRE+POST" };

    analyzerButton.setText (analyzerFrozen && analyzerMode != analyzerOff
                                ? juce::String ("ANALYZER  FROZEN")
                                : "ANALYZER  " + juce::String (modes[analyzerMode]));
    analyzerButton.setActive (analyzerFrozen && analyzerMode != analyzerOff);
    pianoButton.setActive (pianoVisible);
    sketchButton.setActive (sketching);
}

//==============================================================================
void EqDisplay::showNotice (const juce::String& text)
{
    notice = text;
    noticeUntil = juce::Time::getMillisecondCounter() + 2500;
    repaint();
}

bool EqDisplay::saveViewAsDefault()
{
    // Everything as it is set now, also what was never touched and so never stored.
    storeView (rangeProp, rangeDb);
    storeView (analyzerProp, analyzerMode);
    storeView (speedProp, analyzerSpeed);
    storeView (resolutionProp, analyzerResolution);
    storeView (tiltProp, analyzerTilt);
    storeView (floorProp, analyzerRange);
    storeView (pianoProp, pianoVisible);
    storeView (sideProp, sidechainVisible);
    storeView (collisionProp, collisionsVisible);
    storeView (grabProp, grabEnabled);
    storeView (referenceShownProp, referenceVisible);

    const bool ok = ViewDefaults::save (processor.apvts.state);
    showNotice (ok ? "SAVED  NEW INSTANCES WILL LOOK LIKE THIS ONE" : "COULD NOT SAVE THE DEFAULT");
    return ok;
}

//==============================================================================
void EqDisplay::updateCollisions()
{
    const auto* own = postTrace.visible ? &postTrace : ownTrace();
    const int n = snapshot.numPoints;
    const bool active = analyzerMode != analyzerOff && sidechainVisible && collisionsVisible && own != nullptr
                     && sideTrace.visible && (int) own->db.size() == n && (int) sideTrace.db.size() == n;

    if ((int) collisionHold.size() != n)
        collisionHold.assign ((size_t) juce::jmax (0, n), 0.0f);

    collisionRegions.clear();

    if (! active)
    {
        std::fill (collisionHold.begin(), collisionHold.end(), 0.0f);
        return;
    }

    const float loudest = juce::jmax (*std::max_element (own->db.begin(), own->db.end()),
                                      *std::max_element (sideTrace.db.begin(), sideTrace.db.end()));
    const float floorDb = floorOptions[analyzerRange];
    const int last = juce::jmin (n - 1, (int) ((freqToX (0.49 * snapshot.sampleRate) - plot.getX()) / plot.getWidth() * (float) (n - 1)));

    for (int i = 0; i < n; ++i)
    {
        const float now = i <= last ? collisionStrength (own->db[(size_t) i], sideTrace.db[(size_t) i], loudest, floorDb) : 0.0f;

        // Up at once, down within about a second.
        collisionHold[(size_t) i] = juce::jmax (now, collisionHold[(size_t) i] - 1.0f / 30.0f);
    }

    // Stretches that count for at least half become regions, the widest four get a tag.
    for (int i = 0; i < n; ++i)
    {
        if (collisionHold[(size_t) i] < 0.5f)
            continue;

        int end = i;
        while (end + 1 < n && collisionHold[(size_t) end + 1] >= 0.5f)
            ++end;

        CollisionRegion region;
        region.x0 = pointX (i);
        region.x1 = pointX (end);

        if (region.x1 - region.x0 >= 5.0f)
        {
            region.lowHz = xToFreq (region.x0);
            region.highHz = xToFreq (region.x1);
            collisionRegions.push_back (region);
        }

        i = end;
    }

    std::sort (collisionRegions.begin(), collisionRegions.end(),
               [] (const CollisionRegion& a, const CollisionRegion& b) { return a.x1 - a.x0 > b.x1 - b.x0; });

    if (collisionRegions.size() > 4)
        collisionRegions.resize (4);

    for (auto& region : collisionRegions)
    {
        const float centre = 0.5f * (region.x0 + region.x1);
        region.button = { juce::jlimit (plot.getX() + 2.0f, plot.getRight() - 32.0f, centre - 15.0f), plot.getY() + 7.0f, 30.0f, 16.0f };
    }
}

int EqDisplay::fixCollision (int regionIndex)
{
    if (regionIndex < 0 || regionIndex >= (int) collisionRegions.size())
        return -1;

    const auto region = collisionRegions[(size_t) regionIndex];
    const double centre = std::sqrt (region.lowHz * region.highHz);

    // As wide as the red, with a little to spare on either side.
    const double ratio = juce::jmax (1.2, 1.3 * region.highHz / region.lowHz);
    const double q = juce::jlimit (0.7, 6.0, std::sqrt (ratio) / (ratio - 1.0));

    const int band = createBand (FilterType::Bell, centre, 0.0, q, 1);

    if (band < 0)
        return -1;

    HolterdipolterFilterProcessor::setParam (processor.bandParam (band, params::Dyn), -6.0f);
    HolterdipolterFilterProcessor::setParam (processor.bandParam (band, params::DynSide), 1.0f);

    setSelectedBand (band);
    showNotice ("FIXED  THIS BAND GIVES WAY WHILE THE SIDECHAIN SOUNDS. RANGE SETS HOW FAR.");
    return band;
}

//==============================================================================
void EqDisplay::matchToReference (int numTicks)
{
    if (reference.empty())
    {
        showNotice ("NO REFERENCE TO MATCH TO  RECORD ONE FIRST");
        return;
    }

    captureReference (false, numTicks);
    captureForMatch = true;
    lastMatchCount = -1;
}

void EqDisplay::captureReference (bool fromSidechain, int numTicks)
{
    // A recording needs the analyzer running: neither held by a grab nor frozen by hand.
    endGrab();

    if (analyzerFrozen)
        setAnalyzerFrozen (false);

    captureForMatch = false;
    captureFromSidechain = fromSidechain;
    captureTicks = 0;
    captureTicksLeft = juce::jmax (1, numTicks);
    captureSum.assign ((size_t) sketch::kGridPoints, 0.0);
    repaint();
}

void EqDisplay::updateReferenceCapture()
{
    if (captureTicksLeft <= 0)
        return;

    // A match is about what leaves the EQ as it is set now.
    const auto* source = captureForMatch ? (postTrace.visible ? &postTrace : nullptr)
                       : captureFromSidechain ? (sideTrace.visible ? &sideTrace : nullptr)
                                              : ownTrace();
    const int n = snapshot.numPoints;

    if (source != nullptr && (int) source->db.size() == n && n > 1)
    {
        const float tilt = tiltOptions[analyzerTilt];

        for (int i = 0; i < sketch::kGridPoints; ++i)
        {
            const double freq = sketch::gridFreq (i);
            const float position = (freqToX (freq) - plot.getX()) / plot.getWidth() * (float) (n - 1);
            const int index = juce::jlimit (0, n - 2, (int) position);
            const float frac = juce::jlimit (0.0f, 1.0f, position - (float) index);
            const float shown = source->db[(size_t) index] + frac * (source->db[(size_t) index + 1] - source->db[(size_t) index]);

            // Kept without the analyzer's tilt, so it stays right when the tilt is changed.
            captureSum[(size_t) i] += (double) (shown - tilt * (float) std::log2 (freq / 1000.0));
        }

        ++captureTicks;
    }

    if (--captureTicksLeft > 0)
        return;

    if (captureTicks == 0)
    {
        showNotice (captureForMatch ? "NO MATCH  NO SIGNAL ON THIS TRACK"
                    : captureFromSidechain ? "NO REFERENCE  NOTHING AT THE SIDECHAIN" : "NO REFERENCE  NO SIGNAL");
        lastMatchCount = captureForMatch ? 0 : lastMatchCount;
        captureForMatch = false;
        return;
    }

    if (captureForMatch)
    {
        captureForMatch = false;

        std::vector<float> own ((size_t) sketch::kGridPoints);
        for (int i = 0; i < sketch::kGridPoints; ++i)
            own[(size_t) i] = (float) (captureSum[(size_t) i] / (double) captureTicks);

        int free = 0;
        for (int band = 0; band < kMaxBands; ++band)
            free += processor.isBandUsed (band) ? 0 : 1;

        // The bands are heard through the gain scale, so that is taken out of what they are asked to do.
        const double scale = juce::jmax (0.05, (double) processor.getGainScale());
        auto target = matchTarget (reference, own, (double) floorOptions[analyzerRange]);

        for (auto& v : target)
            v = juce::jlimit (-kMaxGainDb, kMaxGainDb, v / scale);

        lastMatchCount = addFittedBands (fitSketchTarget (target, juce::jmin (8, free), sketch::kMatchLeastDb / scale));

        showNotice (lastMatchCount > 0 ? "MATCHED WITH " + juce::String (lastMatchCount) + (lastMatchCount == 1 ? " BAND" : " BANDS")
                                           + "  GAIN SCALE SETS HOW MUCH OF IT YOU WANT"
                                       : juce::String ("NOTHING TO MATCH  THE TRACK ALREADY SOUNDS LIKE THE REFERENCE"));
        return;
    }

    reference.resize ((size_t) sketch::kGridPoints);

    for (int i = 0; i < sketch::kGridPoints; ++i)
        reference[(size_t) i] = (float) (captureSum[(size_t) i] / (double) captureTicks);

    referenceVisible = true;
    storeView (referenceShownProp, true);
    storeReference();
    showNotice ("REFERENCE KEPT");
}

void EqDisplay::storeReference()
{
    juce::StringArray values;

    for (float v : reference)
        values.add (juce::String (juce::roundToInt (v * 10.0f)));

    if (values.isEmpty())  processor.apvts.state.removeProperty (referenceProp, nullptr);
    else                   storeView (referenceProp, values.joinIntoString (","));
}

void EqDisplay::clearReference()
{
    reference.clear();
    captureTicksLeft = 0;
    storeReference();
    repaint();
}

void EqDisplay::paintReference (juce::Graphics& g)
{
    if (reference.empty() || ! referenceVisible || analyzerMode == analyzerOff)
        return;

    const float tilt = tiltOptions[analyzerTilt];
    const float endX = juce::jmin (plot.getRight(), freqToX (0.49 * snapshot.sampleRate));
    juce::Path line;
    bool started = false;

    for (int i = 0; i < sketch::kGridPoints; ++i)
    {
        const double freq = sketch::gridFreq (i);
        const float x = freqToX (freq);

        if (x > endX)
            break;

        const float y = traceY (reference[(size_t) i] + tilt * (float) std::log2 (freq / 1000.0));

        if (started) line.lineTo (x, y);
        else         line.startNewSubPath (x, y);

        started = true;
    }

    const float dashes[] = { 5.0f, 4.0f };
    juce::Path dashed;
    juce::PathStrokeType (1.4f).createDashedStroke (dashed, line, dashes, 2);

    g.setColour (theme::text.withAlpha (0.85f));
    g.fillPath (dashed);

    g.setFont (theme::ui (10.0f, true));
    g.drawText ("REFERENCE", plot.reduced (10.0f, 8.0f).removeFromTop (12.0f).translated (0.0f, sideTrace.visible ? 14.0f : 0.0f),
                juce::Justification::centredRight, false);
}

//==============================================================================
void EqDisplay::setSketching (bool shouldBeOn)
{
    sketching = shouldBeOn;
    sketchDrawing = false;
    sketchPoints.clear();

    if (sketching)
    {
        endDrag();
        endGrab();
        lassoActive = false;
    }

    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    refreshHeader();
    repaint();
}

int EqDisplay::applySketch (const std::vector<juce::Point<float>>& points)
{
    const double scale = (double) processor.getGainScale();

    if (points.size() < 2 || scale <= 0.02)
        return 0;

    // The drawing shows what should be heard, so the gain scale is taken out again.
    std::vector<std::pair<double, double>> curve;
    curve.reserve (points.size());

    for (const auto& p : points)
        curve.push_back ({ xToFreq (p.x), juce::jlimit (-kMaxGainDb, kMaxGainDb, yToDb (p.y) / scale) });

    int free = 0;
    for (int band = 0; band < kMaxBands; ++band)
        free += processor.isBandUsed (band) ? 0 : 1;

    return addFittedBands (fitSketch (curve, juce::jmin (6, free)));
}

int EqDisplay::addFittedBands (const std::vector<SketchBand>& fitted)
{
    std::array<bool, kMaxBands> added {};
    int first = -1, count = 0;

    for (const auto& b : fitted)
    {
        const int band = createBand (b.type, b.freq, b.gainDb, b.q, 1);

        if (band < 0)
            break;

        added[(size_t) band] = true;
        first = first < 0 ? band : first;
        ++count;
    }

    if (count > 0)
        setSelection (added, first);

    return count;
}


//==============================================================================
int EqDisplay::getNumSelected() const
{
    int count = 0;
    forEachSelected ([&count] (int) { ++count; });
    return count;
}

bool EqDisplay::isSelected (int band) const
{
    return band >= 0 && band < kMaxBands && selected[(size_t) band];
}

void EqDisplay::notifySelection()
{
    const int count = getNumSelected();

    if (primaryBand == notifiedPrimary && count == notifiedCount)
        return;

    notifiedPrimary = primaryBand;
    notifiedCount = count;

    if (onSelectionChanged != nullptr)
        onSelectionChanged (primaryBand, count);
}

void EqDisplay::setSelection (const std::array<bool, kMaxBands>& newSelection, int newPrimary)
{
    selected = newSelection;

    for (int band = 0; band < kMaxBands; ++band)
        if (! processor.isBandUsed (band))
            selected[(size_t) band] = false;

    primaryBand = isSelected (newPrimary) ? newPrimary : -1;

    // Without a valid primary band, the lowest selected one takes the part.
    for (int band = 0; band < kMaxBands && primaryBand < 0; ++band)
        if (selected[(size_t) band])
            primaryBand = band;

    notifySelection();
    repaint();
}

void EqDisplay::setSelectedBand (int band)
{
    std::array<bool, kMaxBands> only {};

    if (band >= 0 && band < kMaxBands)
        only[(size_t) band] = true;

    setSelection (only, band);
}

void EqDisplay::toggleSelected (int band)
{
    auto next = selected;
    next[(size_t) band] = ! next[(size_t) band];
    setSelection (next, next[(size_t) band] ? band : primaryBand);
}

void EqDisplay::makePrimary (int band)
{
    setSelection (selected, band);
}

void EqDisplay::selectAll()
{
    std::array<bool, kMaxBands> all;
    all.fill (true);
    setSelection (all, primaryBand);
}

void EqDisplay::removeBand (int band)
{
    if (band < 0 || band >= kMaxBands)
        return;

    if (dragBand >= 0)
        endDrag();

    HolterdipolterFilterProcessor::setParam (processor.bandParam (band, params::Used), 0.0f);

    if (processor.soloBand.load() == band)
        processor.soloBand.store (-1);

    if (hoverBand == band)
        hoverBand = -1;

    setSelection (selected, primaryBand);   // drops the band that is gone
}

void EqDisplay::removeSelected()
{
    std::vector<int> bands;
    forEachSelected ([&bands] (int band) { bands.push_back (band); });

    for (int band : bands)
        removeBand (band);
}

//==============================================================================
juce::String EqDisplay::copySelection() const
{
    juce::XmlElement root (clipboardTag);

    forEachSelected ([this, &root] (int band)
    {
        const auto p = processor.getBandParams (band);
        auto* e = root.createNewChildElement ("Band");

        e->setAttribute ("type",  (int) p.type);
        e->setAttribute ("freq",  p.freq);
        e->setAttribute ("gain",  p.gainDb);
        e->setAttribute ("q",     p.q);
        e->setAttribute ("slope", p.slopeIndex);
        e->setAttribute ("place", (int) p.placement);
        e->setAttribute ("on",    p.active ? 1 : 0);
        e->setAttribute ("dyn",   p.dynRangeDb);
        e->setAttribute ("thr",   p.thresholdDb);
        e->setAttribute ("athr",  p.autoThreshold ? 1 : 0);
        e->setAttribute ("dsc",   p.dynFromSidechain ? 1 : 0);
    });

    if (root.getNumChildElements() == 0)
        return {};

    return root.toString (juce::XmlElement::TextFormat().singleLine());
}

int EqDisplay::pasteBands (const juce::String& text)
{
    if (! text.contains (clipboardTag))
        return 0;

    const auto xml = juce::parseXML (text);

    if (xml == nullptr || ! xml->hasTagName (clipboardTag))
        return 0;

    endDrag();

    std::array<bool, kMaxBands> pasted {};
    int first = -1, count = 0;

    for (auto* e : xml->getChildWithTagNameIterator ("Band"))
    {
        const int band = processor.findFreeBand();

        if (band < 0)
            break;

        const auto set = [this, band] (params::BandField field, double value)
        {
            HolterdipolterFilterProcessor::setParam (processor.bandParam (band, field), (float) value);
        };

        set (params::Type,  juce::jlimit (0, kNumFilterTypes - 1, e->getIntAttribute ("type")));
        set (params::Slope, juce::jlimit (0, kNumSlopes - 1,      e->getIntAttribute ("slope", 1)));
        set (params::Place, juce::jlimit (0, kNumPlacements - 1,  e->getIntAttribute ("place")));
        set (params::Q,     juce::jlimit (kMinQ, kMaxQ,           e->getDoubleAttribute ("q", 1.0)));
        set (params::Freq,  juce::jlimit (kMinFreq, kMaxFreq,     e->getDoubleAttribute ("freq", 1000.0)));
        set (params::Gain,  juce::jlimit (-kMaxGainDb, kMaxGainDb, e->getDoubleAttribute ("gain")));
        set (params::On,    e->getIntAttribute ("on", 1) != 0 ? 1.0 : 0.0);
        set (params::Dyn,   juce::jlimit (-kMaxGainDb, kMaxGainDb, e->getDoubleAttribute ("dyn")));
        set (params::Thresh, juce::jlimit (-80.0, 0.0,            e->getDoubleAttribute ("thr", -30.0)));
        set (params::AutoThresh, e->getIntAttribute ("athr", 1) != 0 ? 1.0 : 0.0);
        set (params::DynSide, e->getIntAttribute ("dsc", 0) != 0 ? 1.0 : 0.0);
        set (params::Used,  1.0);

        pasted[(size_t) band] = true;
        first = first < 0 ? band : first;
        ++count;
    }

    // The copies sit exactly on their originals; selecting them lets them be dragged away.
    if (count > 0)
        setSelection (pasted, first);

    return count;
}

void EqDisplay::copyToClipboard() const
{
    const auto text = copySelection();

    if (text.isNotEmpty())
        juce::SystemClipboard::copyTextToClipboard (text);
}

void EqDisplay::pasteFromClipboard()
{
    pasteBands (juce::SystemClipboard::getTextFromClipboard());
}

//==============================================================================
void EqDisplay::setDisplayRange (float newRangeDb)
{
    rangeDb = newRangeDb;
    storeView (rangeProp, rangeDb);

    for (int i = 0; i < 4; ++i)
        if (std::abs (displayRanges[i] - rangeDb) < 0.01f)
            rangeSelector.setSelected (i);

    repaint();
}

void EqDisplay::setAnalyzerMode (int mode)
{
    analyzerMode = juce::jlimit ((int) analyzerOff, (int) analyzerBoth, mode);
    processor.analyzerEnabled.store (analyzerMode != analyzerOff);
    storeView (analyzerProp, analyzerMode);

    if (analyzerMode != analyzerPre && analyzerMode != analyzerBoth)   preTrace.visible = false;
    if (analyzerMode != analyzerPost && analyzerMode != analyzerBoth)  postTrace.visible = false;

    refreshHeader();
    repaint();
}

void EqDisplay::setAnalyzerSpeed (int index)
{
    analyzerSpeed = juce::jlimit (0, 2, index);
    storeView (speedProp, analyzerSpeed);
}

void EqDisplay::setAnalyzerResolution (int index)
{
    index = juce::jlimit (0, 3, index);

    if (index != analyzerResolution)
    {
        analyzerResolution = index;
        setUpFft();
    }

    storeView (resolutionProp, analyzerResolution);
}

void EqDisplay::setAnalyzerTilt (int index)
{
    analyzerTilt = juce::jlimit (0, 3, index);
    storeView (tiltProp, analyzerTilt);
}

void EqDisplay::setAnalyzerRange (int index)
{
    analyzerRange = juce::jlimit (0, 2, index);
    storeView (floorProp, analyzerRange);
    repaint();
}

void EqDisplay::setAnalyzerFrozen (bool shouldBeFrozen)
{
    analyzerFrozen = shouldBeFrozen;   // a passing state, not stored
    refreshHeader();
    repaint();
}

void EqDisplay::setPianoVisible (bool shouldBeVisible)
{
    pianoVisible = shouldBeVisible;
    storeView (pianoProp, pianoVisible);
    refreshHeader();
    resized();
    repaint();
}

void EqDisplay::setSidechainVisible (bool shouldBeVisible)
{
    sidechainVisible = shouldBeVisible;
    storeView (sideProp, sidechainVisible);
    sideTrace.visible = sideTrace.visible && sidechainVisible;
    repaint();
}

void EqDisplay::setCollisionsVisible (bool shouldBeVisible)
{
    collisionsVisible = shouldBeVisible;
    storeView (collisionProp, collisionsVisible);
    repaint();
}

void EqDisplay::setGrabEnabled (bool shouldBeEnabled)
{
    grabEnabled = shouldBeEnabled;
    storeView (grabProp, grabEnabled);

    if (! grabEnabled)
        endGrab();
}

//==============================================================================
const EqDisplay::Trace* EqDisplay::ownTrace() const
{
    const bool post = analyzerMode == analyzerPost || analyzerMode == analyzerBoth;
    const bool pre  = analyzerMode == analyzerPre || analyzerMode == analyzerBoth;

    // What is left after the EQ is what there is to work on; before it, if that is all that shows.
    if (post && postTrace.visible) return &postTrace;
    if (pre && preTrace.visible)   return &preTrace;
    return nullptr;
}

float EqDisplay::traceY (float db) const
{
    const float floorDb = floorOptions[analyzerRange];
    return juce::jmap (juce::jlimit (floorDb, 6.0f, db), floorDb, 0.0f, plot.getBottom(), plot.getY() + plot.getHeight() * 0.06f);
}

bool EqDisplay::startGrab()
{
    const auto* trace = ownTrace();
    const int n = trace != nullptr ? (int) trace->db.size() : 0;

    if (n < 8 || n != snapshot.numPoints)
        return false;

    const auto& db = trace->db;
    const float floorDb = floorOptions[analyzerRange];
    const int last = juce::jmin (n - 2, (int) ((freqToX (0.45 * snapshot.sampleRate) - plot.getX()) / plot.getWidth() * (float) (n - 1)));

    struct Candidate { int index; float prominence; };
    std::vector<Candidate> candidates;

    for (int i = 2; i <= last; ++i)
    {
        const float level = db[(size_t) i];

        if (level < floorDb + 12.0f || level < db[(size_t) i - 1] || level <= db[(size_t) i + 1])
            continue;

        // How far the peak rises above the deeper of the two valleys next to it.
        float valleyLeft = level, valleyRight = level;

        for (int k = i - 1; k >= 0 && db[(size_t) k] <= level; --k)   valleyLeft = juce::jmin (valleyLeft, db[(size_t) k]);
        for (int k = i + 1; k < n && db[(size_t) k] <= level; ++k)    valleyRight = juce::jmin (valleyRight, db[(size_t) k]);

        const float prominence = level - juce::jmax (valleyLeft, valleyRight);

        if (prominence >= 6.0f)
            candidates.push_back ({ i, prominence });
    }

    std::sort (candidates.begin(), candidates.end(), [] (const Candidate& a, const Candidate& b) { return a.prominence > b.prominence; });

    grabPeaks.clear();

    for (const auto& c : candidates)
    {
        if (grabPeaks.size() >= 6)
            break;

        GrabPeak peak;
        peak.position = { pointX (c.index), traceY (db[(size_t) c.index]) };
        peak.freq = xToFreq (peak.position.x);

        // Markers that would sit on top of each other are of no use.
        const bool crowded = std::any_of (grabPeaks.begin(), grabPeaks.end(), [&peak] (const GrabPeak& other)
        {
            return std::abs (other.position.x - peak.position.x) < 46.0f;
        });

        if (crowded)
            continue;

        // The band gets the width the peak has 3 dB below its top.
        int lo = c.index, hi = c.index;
        while (lo > 0 && db[(size_t) lo] > db[(size_t) c.index] - 3.0f)       --lo;
        while (hi < n - 1 && db[(size_t) hi] > db[(size_t) c.index] - 3.0f)   ++hi;

        const double width = xToFreq (pointX (hi)) - xToFreq (pointX (lo));
        peak.q = juce::jlimit (1.5, 10.0, width > 0.0 ? peak.freq / width : 4.0);

        grabPeaks.push_back (peak);
    }

    if (grabPeaks.empty())
        return false;

    grabActive = true;
    grabDragging = false;
    grabHover = grabPeakAt (mousePos);
    repaint();
    return true;
}

void EqDisplay::endGrab()
{
    if (! grabActive)
        return;

    grabActive = false;
    grabDragging = false;
    grabPeaks.clear();
    grabHover = -1;
    mouseRestingSince = juce::Time::getMillisecondCounter();
    repaint();
}

int EqDisplay::grabPeakAt (juce::Point<float> p) const
{
    int best = -1;
    float bestDistance = grabRadius;

    for (size_t i = 0; i < grabPeaks.size(); ++i)
    {
        const float distance = grabPeaks[i].position.getDistanceFrom (p);

        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = (int) i;
        }
    }

    return best;
}

void EqDisplay::setUpFft()
{
    fftOrder = fftOrders[analyzerResolution];
    fftSize = 1 << fftOrder;
    fft = std::make_unique<juce::dsp::FFT> (fftOrder);

    window.resize ((size_t) fftSize);
    fftBuffer.assign ((size_t) fftSize * 2, 0.0f);

    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (fftSize - 1));

    preTrace.db.clear();
    postTrace.db.clear();
    sideTrace.db.clear();
    endGrab();
}

//==============================================================================
float EqDisplay::freqToX (double freq) const
{
    const double norm = std::log (juce::jlimit (displayMinFreq, displayMaxFreq, freq) / displayMinFreq)
                      / std::log (displayMaxFreq / displayMinFreq);
    return plot.getX() + (float) norm * plot.getWidth();
}

double EqDisplay::xToFreq (float x) const
{
    const double norm = juce::jlimit (0.0, 1.0, (double) ((x - plot.getX()) / juce::jmax (1.0f, plot.getWidth())));
    return displayMinFreq * std::pow (displayMaxFreq / displayMinFreq, norm);
}

float EqDisplay::dbToY (double db) const
{
    const double halfSpan = (double) rangeDb * 1.1;
    return plot.getCentreY() - (float) (db / halfSpan) * plot.getHeight() * 0.5f;
}

double EqDisplay::yToDb (float y) const
{
    const double halfSpan = (double) rangeDb * 1.1;
    return (double) ((plot.getCentreY() - y) / (plot.getHeight() * 0.5f)) * halfSpan;
}

float EqDisplay::pointX (int index) const
{
    return plot.getX() + plot.getWidth() * (float) index / (float) juce::jmax (1, snapshot.numPoints - 1);
}

juce::Point<float> EqDisplay::nodePosition (int band) const
{
    const auto p = processor.getBandParams (band);
    const double db = typeHasGain (p.type) ? p.gainDb * (double) processor.getGainScale() : 0.0;

    return { freqToX (p.freq), juce::jlimit (plot.getY(), plot.getBottom(), dbToY (db)) };
}

int EqDisplay::bandAt (juce::Point<float> p) const
{
    int best = -1;
    float bestDistance = hitRadius;

    for (int band = 0; band < kMaxBands; ++band)
    {
        if (! processor.isBandUsed (band))
            continue;

        const float distance = nodePosition (band).getDistanceFrom (p);

        // On a tie the selected band wins, so stacked nodes stay grabbable.
        if (distance < bestDistance || (band == primaryBand && distance <= bestDistance && distance < hitRadius))
        {
            bestDistance = distance;
            best = band;
        }
    }

    return best;
}

//==============================================================================
EqDisplay::Snapshot EqDisplay::takeSnapshot() const
{
    Snapshot s;

    for (int band = 0; band < kMaxBands; ++band)
    {
        s.bands[(size_t) band] = processor.getBandParams (band);
        s.used[(size_t) band] = processor.isBandUsed (band);
    }

    s.gainScale = processor.getGainScale();
    s.sampleRate = processor.getCurrentSampleRate();
    s.numPoints = juce::jmax (2, (int) (plot.getWidth() * 0.5f) + 1);
    return s;
}

void EqDisplay::rebuildCurves()
{
    const auto n = (size_t) snapshot.numPoints;

    std::vector<double> freqs (n);
    for (size_t i = 0; i < n; ++i)
        freqs[i] = xToFreq (pointX ((int) i));

    stereoTotal.assign (n, 0.0f);
    for (auto& g : groupTotals) g.assign (n, 0.0f);
    groupPresent.fill (false);

    for (int band = 0; band < kMaxBands; ++band)
    {
        auto& curve = bandCurves[(size_t) band];

        if (! snapshot.used[(size_t) band])
        {
            curve.clear();
            continue;
        }

        const auto& p = snapshot.bands[(size_t) band];

        BandSpec spec;
        spec.type = p.type;
        spec.freq = p.freq;
        spec.gainDb = p.gainDb * (double) snapshot.gainScale;
        spec.q = p.q;
        spec.slopeIndex = p.slopeIndex;

        const auto coeffs = designBand (spec, snapshot.sampleRate);

        curve.resize (n);
        for (size_t i = 0; i < n; ++i)
            curve[i] = (float) coeffs.magnitudeDb (freqs[i], snapshot.sampleRate);

        if (! p.active)
            continue;

        if (p.placement == Placement::Stereo)
        {
            for (size_t i = 0; i < n; ++i)
                stereoTotal[i] += curve[i];
        }
        else
        {
            groupPresent[(size_t) p.placement] = true;
            auto& total = groupTotals[(size_t) p.placement];

            for (size_t i = 0; i < n; ++i)
                total[i] += curve[i];
        }
    }

    for (auto& total : groupTotals)
        for (size_t i = 0; i < n; ++i)
            total[i] += stereoTotal[i];
}

void EqDisplay::updateTrace (const SpectrumTap& tap, Trace& trace)
{
    const int n = snapshot.numPoints;

    if (n < 2 || plot.isEmpty())
        return;

    if ((int) trace.db.size() != n)
        trace.db.assign ((size_t) n, -140.0f);

    tap.copyLatest (fftBuffer.data(), fftSize);

    for (int i = 0; i < fftSize; ++i)
        fftBuffer[(size_t) i] *= window[(size_t) i];

    std::fill (fftBuffer.begin() + fftSize, fftBuffer.end(), 0.0f);
    fft->performFrequencyOnlyForwardTransform (fftBuffer.data(), true);

    const double sampleRate = processor.getCurrentSampleRate();
    const double binsPerHz = (double) fftSize / sampleRate;
    const float norm = 4.0f / (float) fftSize;   // Hann window, full-scale sine reads 0 dB
    const int maxBin = fftSize / 2 - 1;
    const float halfStep = 0.5f * plot.getWidth() / (float) (n - 1);

    std::vector<float> target ((size_t) n);

    for (int i = 0; i < n; ++i)
    {
        const float x = pointX (i);
        const double freq = xToFreq (x);
        const double binLo = xToFreq (x - halfStep) * binsPerHz;
        const double binHi = xToFreq (x + halfStep) * binsPerHz;

        float magnitude = 0.0f;

        if (freq < 0.5 * sampleRate)
        {
            if (binHi - binLo < 1.0)
            {
                // Fewer than one bin per point: interpolate.
                const double bin = juce::jlimit (0.0, (double) maxBin, freq * binsPerHz);
                const int b0 = (int) bin;
                const int b1 = juce::jmin (b0 + 1, maxBin);
                const float frac = (float) (bin - (double) b0);
                magnitude = fftBuffer[(size_t) b0] + frac * (fftBuffer[(size_t) b1] - fftBuffer[(size_t) b0]);
            }
            else
            {
                // Several bins per point: keep the peak so narrow tones stay visible.
                const int lo = juce::jlimit (0, maxBin, (int) std::floor (binLo));
                const int hi = juce::jlimit (lo, maxBin, (int) std::ceil (binHi));

                for (int b = lo; b <= hi; ++b)
                    magnitude = juce::jmax (magnitude, fftBuffer[(size_t) b]);
            }
        }

        target[(size_t) i] = juce::Decibels::gainToDecibels (magnitude * norm, -140.0f)
                           + tiltOptions[analyzerTilt] * (float) std::log2 (freq / 1000.0);
    }

    bool anyVisible = false;
    const auto speed = speedOptions[analyzerSpeed];
    const float floorDb = floorOptions[analyzerRange];

    for (int i = 0; i < n; ++i)
    {
        const float a = target[(size_t) juce::jmax (0, i - 1)];
        const float b = target[(size_t) i];
        const float c = target[(size_t) juce::jmin (n - 1, i + 1)];
        const float smoothed = 0.25f * a + 0.5f * b + 0.25f * c;

        float& value = trace.db[(size_t) i];
        value += (smoothed > value ? speed.attack : speed.release) * (smoothed - value);
        anyVisible = anyVisible || value > floorDb;
    }

    trace.visible = anyVisible;
}

void EqDisplay::updateMeter()
{
    for (size_t ch = 0; ch < 2; ++ch)
    {
        const float peak = processor.outputPeak[ch].exchange (0.0f, std::memory_order_relaxed);
        const float db = juce::Decibels::gainToDecibels (peak, -100.0f);

        meterDb[ch] = juce::jmax (db, meterDb[ch] - 1.5f);

        if (db >= meterHoldDb[ch])
        {
            meterHoldDb[ch] = db;
            meterHoldFrames[ch] = 45;
        }
        else if (--meterHoldFrames[ch] <= 0)
        {
            meterHoldDb[ch] = juce::jmax (-100.0f, meterHoldDb[ch] - 1.0f);
        }
    }
}

void EqDisplay::timerCallback()
{
    // Bands can disappear behind the display's back: undo, a preset, the host.
    bool stale = false;

    for (int band = 0; band < kMaxBands; ++band)
        stale = stale || (selected[(size_t) band] && ! processor.isBandUsed (band));

    if (stale)
        setSelection (selected, primaryBand);

    const auto current = takeSnapshot();

    if (! (current == snapshot))
    {
        snapshot = current;
        rebuildCurves();
    }

    if (! analyzerFrozen && ! grabActive)
    {
        if (analyzerMode == analyzerPre || analyzerMode == analyzerBoth)   updateTrace (processor.preTap, preTrace);
        const bool sideShown = analyzerMode != analyzerOff && sidechainVisible
                            && processor.sidechainConnected.load (std::memory_order_relaxed);

        // Collisions are judged on what leaves the EQ, also while only "pre" is on show.
        if (analyzerMode == analyzerPost || analyzerMode == analyzerBoth || sideShown || (captureForMatch && analyzerMode != analyzerOff))
            updateTrace (processor.postTap, postTrace);

        if (sideShown)  updateTrace (processor.sideTap, sideTrace);
        else            sideTrace.visible = false;
    }

    updateCollisions();
    updateReferenceCapture();

    // Spectrum Grab: the mouse has been resting on empty analyzer for a moment.
    if (grabEnabled && ! grabActive && ! sketching && ! analyzerFrozen && captureTicksLeft <= 0
        && hoverBand < 0 && dragBand < 0 && ! lassoActive
        && plot.contains (mousePos) && juce::Time::getMillisecondCounter() - mouseRestingSince > grabAfterMs)
    {
        if (! startGrab())
            mouseRestingSince = juce::Time::getMillisecondCounter();   // nothing to grab yet; look again later
    }

    updateMeter();
    repaint();
}

//==============================================================================
void EqDisplay::resized()
{
    auto area = getLocalBounds().toFloat();

    // Header strip: what is shown on the left, how far it reaches on the right.
    auto header = getLocalBounds().reduced (10, 0).withY (9).withHeight (24);

    analyzerButton.setBounds (header.removeFromLeft (168));
    header.removeFromLeft (8);
    pianoButton.setBounds (header.removeFromLeft (64));
    header.removeFromLeft (8);
    sketchButton.setBounds (header.removeFromLeft (68));

    rangeSelector.setBounds (header.removeFromRight (132));
    header.removeFromRight (8);
    rangeCaption = header.removeFromRight (60);

    const float below = pianoVisible ? 24.0f + pianoHeight + 5.0f : 24.0f;

    plot = area.withTrimmedLeft (40.0f).withTrimmedRight (26.0f).withTrimmedTop (headerHeight).withTrimmedBottom (below);
    meterArea = { plot.getRight() + 9.0f, plot.getY(), 9.0f, plot.getHeight() };
    pianoArea = pianoVisible ? juce::Rectangle<float> (plot.getX(), plot.getBottom() + 3.0f, plot.getWidth(), pianoHeight)
                             : juce::Rectangle<float>();

    snapshot.numPoints = 0;   // forces a rebuild on the next tick
}

juce::Path EqDisplay::curvePath (const std::vector<float>& db, bool closedToZeroLine) const
{
    juce::Path path;
    const int n = (int) db.size();

    if (n < 2)
        return path;

    const float zeroY = dbToY (0.0);
    const float top = plot.getY() - 40.0f, bottom = plot.getBottom() + 40.0f;

    if (closedToZeroLine)
        path.startNewSubPath (plot.getX(), zeroY);

    for (int i = 0; i < n; ++i)
    {
        const juce::Point<float> p (pointX (i), juce::jlimit (top, bottom, dbToY ((double) db[(size_t) i])));

        if (i == 0 && ! closedToZeroLine) path.startNewSubPath (p);
        else                              path.lineTo (p);
    }

    if (closedToZeroLine)
    {
        path.lineTo (plot.getRight(), zeroY);
        path.closeSubPath();
    }

    return path;
}

void EqDisplay::paintGrid (juce::Graphics& g)
{
    // Frequency lines: every multiple within a decade, decades stronger.
    for (double decade = 10.0; decade <= 10000.0; decade *= 10.0)
    {
        for (int m = 1; m <= 9; ++m)
        {
            const double freq = decade * m;

            if (freq <= displayMinFreq || freq >= displayMaxFreq)
                continue;

            g.setColour (m == 1 ? theme::gridStrong : theme::grid);
            g.fillRect (juce::Rectangle<float> (std::round (freqToX (freq)), plot.getY(), 1.0f, plot.getHeight()));
        }
    }

    g.setFont (theme::mono (10.0f));

    static const std::pair<double, const char*> freqLabels[] = {
        { 20.0, "20" }, { 50.0, "50" }, { 100.0, "100" }, { 200.0, "200" }, { 500.0, "500" },
        { 1000.0, "1k" }, { 2000.0, "2k" }, { 5000.0, "5k" }, { 10000.0, "10k" }, { 20000.0, "20k" }
    };

    g.setColour (theme::textFaint);
    const float labelY = (pianoVisible ? pianoArea.getBottom() : plot.getBottom()) + 5.0f;

    for (const auto& [freq, label] : freqLabels)
        g.drawText (label, juce::Rectangle<float> (freqToX (freq) - 20.0f, labelY, 40.0f, 14.0f),
                    juce::Justification::centred, false);

    // Gain lines.
    const float step = rangeDb / 3.0f;

    for (int k = -3; k <= 3; ++k)
    {
        const float db = step * (float) k;
        const float y = std::round (dbToY ((double) db));

        g.setColour (k == 0 ? theme::gridStrong.brighter (0.25f) : theme::grid);
        g.fillRect (juce::Rectangle<float> (plot.getX(), y, plot.getWidth(), 1.0f));

        g.setColour (k == 0 ? theme::textDim : theme::textFaint);
        g.drawText ((k > 0 ? "+" : "") + juce::String (juce::roundToInt (db)),
                    juce::Rectangle<float> (0.0f, y - 7.0f, plot.getX() - 8.0f, 14.0f),
                    juce::Justification::centredRight, false);
    }
}

void EqDisplay::paintPiano (juce::Graphics& g)
{
    if (! pianoVisible || pianoArea.isEmpty())
        return;

    const auto keys = pianoArea.withHeight (pianoKeyHeight);

    // One cell per semitone, each as wide as the semitone is on the frequency axis.
    const auto keyBounds = [this, keys] (int note)
    {
        const double centre = 440.0 * std::pow (2.0, (note - 69) / 12.0);
        const float x0 = freqToX (centre * std::pow (2.0, -1.0 / 24.0));
        const float x1 = freqToX (centre * std::pow (2.0,  1.0 / 24.0));
        return juce::Rectangle<float> (x0, keys.getY(), x1 - x0, keys.getHeight());
    };

    const int firstNote = params::midiNoteFor (displayMinFreq) + 1;
    const int lastNote  = params::midiNoteFor (displayMaxFreq) - 1;
    const bool letters = keyBounds (60).getWidth() >= lettersFromKeyWidth;

    for (int note = firstNote; note <= lastNote; ++note)
    {
        const int step = ((note % 12) + 12) % 12;
        const bool black = step == 1 || step == 3 || step == 6 || step == 8 || step == 10;
        const auto key = keyBounds (note);

        g.setColour (black ? theme::grid : theme::text.withAlpha (step == 0 ? 0.82f : 0.6f));
        g.fillRect (key.reduced (0.5f, 0.0f));

        // In a wide window every white key says which note it is.
        if (letters && ! black)
        {
            g.setFont (theme::ui (10.0f, true));
            g.setColour (theme::background);
            g.drawText (params::noteName (note).substring (0, 1), key, juce::Justification::centred, false);
        }

        // Every C is named with its octave underneath: C4 is middle C.
        if (step == 0)
        {
            const float x = key.getCentreX();

            g.setColour (theme::textDim);
            g.fillRect (juce::Rectangle<float> (x - 0.5f, keys.getBottom(), 1.0f, 3.0f));

            g.setFont (theme::mono (10.0f));
            g.setColour (theme::text.withAlpha (0.8f));
            g.drawText (params::noteName (note), juce::Rectangle<float> (x - 16.0f, keys.getBottom() + 2.0f, 32.0f, pianoLabelHeight - 1.0f),
                        juce::Justification::centred, false);
        }
    }

    // The key each band sits on, brighter for the ones in hand.
    for (int band = 0; band < kMaxBands; ++band)
    {
        if (! processor.isBandUsed (band))
            continue;

        const auto p = processor.getBandParams (band);
        const bool inHand = isSelected (band) || band == hoverBand || band == dragBand;

        g.setColour (theme::placementColour (p.placement).withAlpha (inHand ? 1.0f : 0.5f));
        g.fillRect (keyBounds (params::midiNoteFor (p.freq)).reduced (0.5f, 0.0f));
    }

    // Tags that spell the note out: one for the mouse, one for every band in hand.
    const auto tag = [&] (int note, juce::Colour fill, juce::Colour ink)
    {
        const auto text = params::noteName (note);
        const auto font = theme::mono (11.0f, true);
        const float width = juce::GlyphArrangement::getStringWidth (font, text) + 10.0f;
        const float x = juce::jlimit (plot.getX(), plot.getRight() - width, keyBounds (note).getCentreX() - width * 0.5f);
        const juce::Rectangle<float> box (x, keys.getY() - 19.0f, width, 16.0f);

        g.setColour (fill);
        g.fillRoundedRectangle (box, 3.0f);

        juce::Path pointer;
        const float cx = keyBounds (note).getCentreX();
        pointer.addTriangle (cx - 3.5f, box.getBottom() - 0.5f, cx + 3.5f, box.getBottom() - 0.5f, cx, box.getBottom() + 3.5f);
        g.fillPath (pointer);

        g.setFont (font);
        g.setColour (ink);
        g.drawText (text, box, juce::Justification::centred, false);
    };

    const bool bandInHand = hoverBand >= 0 || dragBand >= 0;

    if (mouseX >= plot.getX() && mouseX <= plot.getRight() && ! bandInHand && ! lassoActive)
        tag (juce::jlimit (firstNote, lastNote, params::midiNoteFor (xToFreq (mouseX))), theme::gridStrong, theme::text);

    for (int pass = 0; pass < 2; ++pass)
        for (int band = 0; band < kMaxBands; ++band)
        {
            const bool inHand = isSelected (band) || band == hoverBand || band == dragBand;

            if (! processor.isBandUsed (band) || ! inHand || (band == primaryBand) != (pass == 1))
                continue;

            const auto p = processor.getBandParams (band);
            tag (params::midiNoteFor (p.freq), theme::placementColour (p.placement), theme::background);
        }
}

void EqDisplay::paintTrace (juce::Graphics& g, const Trace& trace, juce::Colour fill, juce::Colour line)
{

    const int n = (int) trace.db.size();

    if (! trace.visible || n < 2 || n != snapshot.numPoints)
        return;

    const float top = plot.getY() + plot.getHeight() * 0.06f;
    const float bottom = plot.getBottom();

    // Nothing exists above Nyquist, so the trace ends there instead of dropping to the floor.
    const float endX = juce::jmin (plot.getRight(), freqToX (0.49 * snapshot.sampleRate));

    juce::Path outline;
    float lastX = plot.getX();

    for (int i = 0; i < n; ++i)
    {
        const float x = pointX (i);

        if (x > endX)
            break;

        const float y = traceY (trace.db[(size_t) i]);

        if (i == 0) outline.startNewSubPath (x, y);
        else        outline.lineTo (x, y);

        lastX = x;
    }

    if (! fill.isTransparent())
    {
        juce::Path area (outline);
        area.lineTo (lastX, bottom + 2.0f);
        area.lineTo (plot.getX(), bottom + 2.0f);
        area.closeSubPath();

        g.setGradientFill (juce::ColourGradient (fill, 0.0f, top, fill.withMultipliedAlpha (0.25f), 0.0f, bottom, false));
        g.fillPath (area);
    }

    if (! line.isTransparent())
    {
        g.setColour (line);
        g.strokePath (outline, juce::PathStrokeType (1.0f));
    }
}

float EqDisplay::collisionStrength (float ownDb, float sideDb, float loudestDb, float floorDb)
{
    const float quieter = juce::jmin (ownDb, sideDb);

    if (quieter < floorDb + 10.0f)
        return 0.0f;

    // Loud: from 24 dB below the strongest peak on screen a place starts to count, from 12 dB fully.
    const float loud = juce::jlimit (0.0f, 1.0f, (quieter - (loudestDb - 24.0f)) / 12.0f);

    // In the way: level with the other track or above it counts fully, 12 dB below it not at all.
    const float inTheWay = juce::jlimit (0.0f, 1.0f, (ownDb - sideDb + 12.0f) / 12.0f);

    return loud * inTheWay;
}

void EqDisplay::paintCollisions (juce::Graphics& g)
{
    const int n = snapshot.numPoints;

    if (sideTrace.visible)
    {
        g.setFont (theme::ui (10.0f, true));
        g.setColour (theme::sidechain);
        g.drawText ("SIDECHAIN", plot.reduced (10.0f, 8.0f).removeFromTop (12.0f), juce::Justification::centredRight, false);
    }

    if ((int) collisionHold.size() != n || n < 2)
        return;

    const float endX = juce::jmin (plot.getRight(), freqToX (0.49 * snapshot.sampleRate));

    // Column by column on whole pixels, so that neighbours neither overlap nor leave seams.
    for (int x = (int) std::ceil (plot.getX()); (float) x < endX; ++x)
    {
        const int i = juce::jlimit (0, n - 1, juce::roundToInt (((float) x + 0.5f - plot.getX()) / plot.getWidth() * (float) (n - 1)));
        const float strength = collisionHold[(size_t) i];

        if (strength <= 0.0f)
            continue;

        g.setColour (theme::warn.withAlpha (0.2f * strength));
        g.fillRect (x, (int) plot.getY(), 1, (int) plot.getHeight());
        g.setColour (theme::warn.withAlpha (strength));
        g.fillRect (x, (int) plot.getY(), 1, 3);
    }

    // A tag on each collision: one click puts a band there that gives way to the other track.
    g.setFont (theme::ui (9.5f, true));

    for (const auto& region : collisionRegions)
    {
        const bool hot = region.button.contains (mousePos);

        g.setColour (hot ? theme::text : theme::warn);
        g.fillRoundedRectangle (region.button, 3.0f);
        g.setColour (theme::background);
        g.drawText ("FIX", region.button, juce::Justification::centred, false);
    }
}

void EqDisplay::paintGrab (juce::Graphics& g)
{
    if (! grabActive)
        return;

    const auto font = theme::mono (11.0f);

    for (size_t i = 0; i < grabPeaks.size(); ++i)
    {
        const auto& peak = grabPeaks[i];
        const bool hot = (int) i == grabHover;
        const auto pos = peak.position;

        g.setColour (theme::background.withAlpha (0.7f));
        g.fillEllipse (pos.x - 5.0f, pos.y - 5.0f, 10.0f, 10.0f);
        g.setColour (hot ? theme::accent : theme::text);
        g.drawEllipse (pos.x - 5.0f, pos.y - 5.0f, 10.0f, 10.0f, hot ? 2.2f : 1.5f);

        auto text = params::formatFrequency ((float) peak.freq);

        if (pianoVisible)
            text << "  " << params::noteName (params::midiNoteFor (peak.freq));

        const float width = juce::GlyphArrangement::getStringWidth (font, text) + 12.0f;
        const float x = juce::jlimit (plot.getX() + 2.0f, plot.getRight() - width - 2.0f, pos.x - width * 0.5f);
        const float y = pos.y - 28.0f < plot.getY() + 2.0f ? pos.y + 10.0f : pos.y - 28.0f;
        const juce::Rectangle<float> box (x, y, width, 18.0f);

        g.setColour (hot ? theme::accent : theme::gridStrong);
        g.fillRoundedRectangle (box, 3.0f);
        g.setFont (font);
        g.setColour (hot ? theme::background : theme::text);
        g.drawText (text, box, juce::Justification::centred, false);
    }

    g.setFont (theme::ui (10.0f, true));
    g.setColour (theme::accent);
    g.drawText (grabDragging ? "GRAB  PULL DOWN TO CUT, UP TO BOOST" : "GRAB  SPECTRUM HELD: DRAG A PEAK",
                plot.reduced (10.0f, 8.0f).removeFromTop (12.0f).translated (0.0f, soloedBand() >= 0 ? 14.0f : 0.0f),
                juce::Justification::centredLeft, false);
}

void EqDisplay::paintCurves (juce::Graphics& g)
{
    // Per-band shapes, filled towards the 0 dB line in the colour of their placement.
    for (int band = 0; band < kMaxBands; ++band)
    {
        const auto& curve = bandCurves[(size_t) band];

        if (! snapshot.used[(size_t) band] || (int) curve.size() != snapshot.numPoints)
            continue;

        const auto& p = snapshot.bands[(size_t) band];
        const auto colour = theme::placementColour (p.placement);
        const bool primary = band == primaryBand;
        const bool hovered = band == hoverBand || band == dragBand || isSelected (band);

        if (p.active)
        {
            g.setColour (colour.withAlpha (primary ? 0.26f : (hovered ? 0.18f : 0.09f)));
            g.fillPath (curvePath (curve, true));

            if (primary || hovered)
            {
                g.setColour (colour.withAlpha (primary ? 0.9f : 0.5f));
                g.strokePath (curvePath (curve, false), juce::PathStrokeType (1.2f));
            }
        }
        else
        {
            // Bypassed band: outline only.
            const float dashes[] = { 4.0f, 4.0f };
            juce::Path dashed;
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, curvePath (curve, false), dashes, 2);
            g.setColour (theme::textFaint);
            g.fillPath (dashed);
        }
    }

    // Resulting response. With L/R or M/S bands there is one curve per affected channel.
    const bool anyGroup = std::any_of (groupPresent.begin(), groupPresent.end(), [] (bool b) { return b; });

    if ((int) stereoTotal.size() == snapshot.numPoints)
    {
        g.setColour (theme::text.withAlpha (anyGroup ? 0.45f : 1.0f));
        g.strokePath (curvePath (stereoTotal, false),
                      juce::PathStrokeType (anyGroup ? 1.3f : 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    for (int placement = 1; placement < kNumPlacements; ++placement)
    {
        if (! groupPresent[(size_t) placement] || (int) groupTotals[(size_t) placement].size() != snapshot.numPoints)
            continue;

        g.setColour (theme::placementColour ((Placement) placement));
        g.strokePath (curvePath (groupTotals[(size_t) placement], false),
                      juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void EqDisplay::paintNodes (juce::Graphics& g)
{
    // Selected bands last, so they sit on top.
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int band = 0; band < kMaxBands; ++band)
        {
            if (! snapshot.used[(size_t) band] || ! processor.isBandUsed (band) || isSelected (band) != (pass == 1))
                continue;

            const auto& p = snapshot.bands[(size_t) band];
            const auto colour = theme::placementColour (p.placement);
            const auto pos = nodePosition (band);
            const bool isChosen = isSelected (band);
            const bool hovered = band == hoverBand || band == dragBand;
            const float r = band == primaryBand ? nodeRadius + 1.0f : nodeRadius;

            // A dynamic band shows how far its gain can travel, and how far it has gone.
            if (p.active && typeHasDynamics (p.type) && ! isExactlyZero (p.dynRangeDb))
            {
                const double scale = (double) processor.getGainScale();
                const auto yFor = [&] (double offsetDb)
                {
                    return juce::jlimit (plot.getY(), plot.getBottom(), dbToY (p.gainDb * scale + offsetDb));
                };

                const float endY = yFor (p.dynRangeDb * scale);
                const float nowY = yFor ((double) processor.dynamicGainDb[(size_t) band].load (std::memory_order_relaxed));

                g.setColour (colour.withAlpha (0.45f));
                g.fillRect (juce::Rectangle<float> (pos.x - 0.5f, juce::jmin (pos.y, endY), 1.0f, std::abs (endY - pos.y)));
                g.fillRect (juce::Rectangle<float> (pos.x - 5.0f, endY - 0.5f, 10.0f, 1.5f));

                g.setColour (colour);
                g.fillRect (juce::Rectangle<float> (pos.x - 1.5f, juce::jmin (pos.y, nowY), 3.0f, std::abs (nowY - pos.y)));
            }

            if (isChosen || hovered)
            {
                g.setColour (colour.withAlpha (isChosen ? (band == primaryBand ? 0.75f : 0.5f) : 0.28f));
                g.drawEllipse (pos.x - r - 4.0f, pos.y - r - 4.0f, (r + 4.0f) * 2.0f, (r + 4.0f) * 2.0f, 1.5f);
            }

            if (p.active)
            {
                g.setColour (colour);
                g.fillEllipse (pos.x - r, pos.y - r, r * 2.0f, r * 2.0f);
            }
            else
            {
                g.setColour (theme::panel);
                g.fillEllipse (pos.x - r, pos.y - r, r * 2.0f, r * 2.0f);
                g.setColour (colour.withAlpha (0.6f));
                g.drawEllipse (pos.x - r, pos.y - r, r * 2.0f, r * 2.0f, 1.4f);
            }

            g.setFont (theme::ui (9.5f, true));
            g.setColour (p.active ? theme::background : colour.withAlpha (0.8f));
            g.drawText (juce::String (band + 1),
                        juce::Rectangle<float> (pos.x - r, pos.y - r, r * 2.0f, r * 2.0f),
                        juce::Justification::centred, false);
        }
    }
}

void EqDisplay::paintReadout (juce::Graphics& g, int band)
{
    if (band < 0 || ! processor.isBandUsed (band))
        return;

    const auto p = processor.getBandParams (band);
    const auto pos = nodePosition (band);

    juce::String text = params::formatFrequency ((float) p.freq) + "   ";

    if (pianoVisible)
        text << params::formatNote (p.freq) << "   ";

    text << (typeHasGain (p.type) ? params::formatGain ((float) p.gainDb)
                                  : params::slopeNames()[p.slopeIndex]);
    if (typeHasQ (p.type))
        text << "   Q " << params::formatQ ((float) p.q);

    if (typeHasDynamics (p.type) && ! isExactlyZero (p.dynRangeDb))
        text << "   dyn " << params::formatGain ((float) p.dynRangeDb);

    const auto font = theme::mono (11.0f);
    const float width = juce::GlyphArrangement::getStringWidth (font, text) + 18.0f;
    const float height = 22.0f;

    float x = juce::jlimit (plot.getX() + 2.0f, plot.getRight() - width - 2.0f, pos.x - width * 0.5f);
    float y = pos.y - nodeRadius - 12.0f - height;

    if (y < plot.getY() + 2.0f)
        y = pos.y + nodeRadius + 12.0f;

    const juce::Rectangle<float> box (x, y, width, height);

    g.setColour (theme::background.withAlpha (0.92f));
    g.fillRoundedRectangle (box, 4.0f);
    g.setColour (theme::placementColour (p.placement).withAlpha (0.55f));
    g.drawRoundedRectangle (box.reduced (0.5f), 4.0f, 1.0f);

    g.setFont (font);
    g.setColour (theme::text);
    g.drawText (text, box, juce::Justification::centred, false);
}

void EqDisplay::paintMeter (juce::Graphics& g)
{
    constexpr float floorDb = -60.0f, topDb = 6.0f;
    const float barWidth = (meterArea.getWidth() - 1.0f) * 0.5f;

    for (int ch = 0; ch < 2; ++ch)
    {
        const juce::Rectangle<float> bar (meterArea.getX() + (float) ch * (barWidth + 1.0f), meterArea.getY(),
                                          barWidth, meterArea.getHeight());

        g.setColour (theme::grid);
        g.fillRect (bar);

        const auto yFor = [&] (float db)
        {
            return juce::jmap (juce::jlimit (floorDb, topDb, db), floorDb, topDb, bar.getBottom(), bar.getY());
        };

        const float level = meterDb[(size_t) ch];

        if (level > floorDb)
        {
            const float y = yFor (level);
            const float zeroY = yFor (0.0f);

            g.setColour (theme::accent.withAlpha (0.85f));
            g.fillRect (bar.withTop (juce::jmax (y, zeroY)));

            if (y < zeroY)
            {
                g.setColour (theme::warn);
                g.fillRect (bar.withTop (y).withBottom (zeroY));
            }
        }

        const float hold = meterHoldDb[(size_t) ch];

        if (hold > floorDb)
        {
            g.setColour (hold > 0.0f ? theme::warn : theme::text);
            g.fillRect (bar.withY (yFor (hold)).withHeight (1.5f));
        }
    }
}

int EqDisplay::soloedBand() const
{
    const int band = processor.soloBand.load (std::memory_order_relaxed);
    return band >= 0 && band < kMaxBands && processor.isBandUsed (band) ? band : -1;
}

void EqDisplay::paintSolo (juce::Graphics& g)
{
    const int band = soloedBand();

    if (band < 0 || snapshot.numPoints < 2)
        return;

    // Darken everything the solo filter removes, so the audible range stays lit.
    const auto params = processor.getBandParams (band);
    const auto coeffs = designBand (soloSpecFor (params), snapshot.sampleRate, Mapping::Bilinear);
    const float stripWidth = plot.getWidth() / (float) (snapshot.numPoints - 1);

    for (int i = 0; i < snapshot.numPoints; ++i)
    {
        const double freq = xToFreq (pointX (i));
        const double s = std::sin (kPi * juce::jmin (freq, 0.5 * snapshot.sampleRate) / snapshot.sampleRate);
        const float audible = (float) juce::jlimit (0.0, 1.0, std::sqrt (coeffs.magnitudeSquaredFromPhi (s * s)));

        g.setColour (theme::background.withAlpha (0.72f * (1.0f - audible)));
        g.fillRect (juce::Rectangle<float> (pointX (i) - 0.5f * stripWidth, plot.getY(), stripWidth + 0.5f, plot.getHeight()));
    }

    g.setFont (theme::ui (10.0f, true));
    g.setColour (theme::placementColour (params.placement));
    g.drawText ("SOLO  BAND " + juce::String (band + 1), plot.reduced (10.0f, 8.0f).removeFromTop (12.0f),
                juce::Justification::centredLeft, false);
}

void EqDisplay::paint (juce::Graphics& g)
{
    g.setColour (theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    g.setFont (theme::ui (10.0f, true));
    g.setColour (theme::textDim);
    g.drawText ("RANGE dB", rangeCaption, juce::Justification::centredRight, false);

    paintGrid (g);

    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (plot.getSmallestIntegerContainer());

        if (analyzerMode == analyzerPre || analyzerMode == analyzerBoth)
            paintTrace (g, preTrace, theme::text.withAlpha (0.13f), juce::Colours::transparentBlack);

        if (analyzerMode == analyzerPost || analyzerMode == analyzerBoth)
            paintTrace (g, postTrace, theme::accent.withAlpha (0.10f), theme::accent.withAlpha (0.55f));

        if (analyzerMode != analyzerOff && sidechainVisible)
        {
            paintCollisions (g);
            paintTrace (g, sideTrace, juce::Colours::transparentBlack, theme::sidechain.withAlpha (0.9f));
        }

        paintReference (g);

        paintCurves (g);
        paintSolo (g);

        if (lassoActive)
        {
            const juce::Rectangle<float> lasso (lassoStart, lassoEnd);

            if (lasso.getWidth() > 2.0f || lasso.getHeight() > 2.0f)
            {
                g.setColour (theme::accent.withAlpha (0.07f));
                g.fillRect (lasso);
                g.setColour (theme::accent.withAlpha (0.6f));
                g.drawRect (lasso, 1.0f);
            }
        }
    }

    paintNodes (g);
    paintGrab (g);

    // A short message, or the countdown of a recording.
    {
        juce::String message;

        if (captureTicksLeft > 0)
            message = juce::String (captureForMatch ? "LISTENING TO THIS TRACK  " : "RECORDING THE REFERENCE  ")
                    + juce::String (captureTicksLeft / 30 + 1) + " s";
        else if (juce::Time::getMillisecondCounter() < noticeUntil)
            message = notice;

        if (message.isNotEmpty() && ! sketching && ! grabActive)
        {
            g.setFont (theme::ui (10.0f, true));
            g.setColour (theme::accent);
            g.drawText (message, plot.reduced (10.0f, 8.0f).removeFromTop (12.0f).translated (0.0f, soloedBand() >= 0 ? 14.0f : 0.0f),
                        juce::Justification::centredLeft, false);
        }
    }

    if (sketching)
    {
        if (sketchPoints.size() > 1)
        {
            juce::Path drawn;
            drawn.startNewSubPath (sketchPoints.front());

            for (size_t i = 1; i < sketchPoints.size(); ++i)
                drawn.lineTo (sketchPoints[i]);

            g.setColour (theme::accent);
            g.strokePath (drawn, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        g.setFont (theme::ui (10.0f, true));
        g.setColour (theme::accent);
        g.drawText ("SKETCH  DRAW THE CURVE YOU WANT, LET GO TO GET THE BANDS  (ESC TO CANCEL)",
                    plot.reduced (10.0f, 8.0f).removeFromTop (12.0f), juce::Justification::centredLeft, false);
    }
    paintPiano (g);
    paintReadout (g, dragBand >= 0 ? dragBand : hoverBand);
    paintMeter (g);

    const bool empty = std::none_of (snapshot.used.begin(), snapshot.used.end(), [] (bool b) { return b; });

    if (empty && ! sketching && ! grabActive)
    {
        g.setFont (theme::ui (13.0f));
        g.setColour (theme::textDim);
        g.drawText ("Double-click anywhere to add a band",
                    plot.withTrimmedTop (16.0f).withHeight (20.0f),
                    juce::Justification::centred, false);
    }
}

//==============================================================================
void EqDisplay::mouseMove (const juce::MouseEvent& e)
{
    mouseX = e.position.y >= plot.getY() ? e.position.x : -1.0f;
    mousePos = e.position;
    mouseRestingSince = juce::Time::getMillisecondCounter();

    if (sketching)
        return;

    const int band = bandAt (e.position);

    if (grabActive)
    {
        if (! plot.expanded (6.0f).contains (e.position))
            endGrab();
        else if (const int peak = band < 0 ? grabPeakAt (e.position) : -1; peak != grabHover)
        {
            grabHover = peak;
            repaint();
        }
    }

    if (band != hoverBand)
    {
        hoverBand = band;
        repaint();
    }

    const bool onPiano = band < 0 && pianoVisible && primaryBand >= 0
                      && pianoArea.withHeight (pianoKeyHeight).expanded (0.0f, 2.0f).contains (e.position);

    const bool onFix = std::any_of (collisionRegions.begin(), collisionRegions.end(),
                                    [&e] (const CollisionRegion& r) { return r.button.contains (e.position); });

    setMouseCursor (band >= 0 ? juce::MouseCursor::DraggingHandCursor
                    : (onPiano || onFix || (grabActive && grabHover >= 0)) ? juce::MouseCursor::PointingHandCursor
                              : juce::MouseCursor::CrosshairCursor);
}

void EqDisplay::mouseExit (const juce::MouseEvent&)
{
    mouseX = -1.0f;
    mousePos = { -1.0f, -1.0f };
    hoverBand = -1;
    endGrab();
    repaint();
}

void EqDisplay::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    lastMousePos = e.position;
    collapseOnRelease = false;

    mousePos = e.position;
    mouseRestingSince = juce::Time::getMillisecondCounter();

    // EQ Sketch: every drag is a drawing, wherever it starts.
    if (sketching)
    {
        sketchPoints.clear();
        sketchDrawing = ! e.mods.isPopupMenu() && plot.expanded (4.0f).contains (e.position);

        if (sketchDrawing)
            sketchPoints.push_back (e.position);
        else
            setSketching (false);

        return;
    }

    const int band = bandAt (e.position);

    // Spectrum Grab: picking up a marked peak makes a band there, ready to be pulled.
    if (grabActive && band < 0 && ! e.mods.isPopupMenu())
    {
        if (const int peak = grabPeakAt (e.position); peak >= 0)
        {
            const auto picked = grabPeaks[(size_t) peak];
            const int created = createBand (FilterType::Bell, picked.freq, 0.0, picked.q, 1);

            if (created >= 0)
            {
                grabDragging = true;
                setSelectedBand (created);
                hoverBand = created;
                beginDrag (created, e.position);
            }

            return;
        }
    }

    endGrab();

    // A click on a collision's tag fixes it.
    if (band < 0 && ! e.mods.isPopupMenu())
        for (size_t i = 0; i < collisionRegions.size(); ++i)
            if (collisionRegions[i].button.contains (e.position))
            {
                fixCollision ((int) i);
                return;
            }

    if (band < 0)
    {
        if (e.mods.isPopupMenu())
        {
            showBackgroundMenu (e.getScreenPosition());
            return;
        }

        // A click on a piano key puts the band in hand on that note.
        if (pianoVisible && primaryBand >= 0 && pianoArea.withHeight (pianoKeyHeight).expanded (0.0f, 2.0f).contains (e.position))
        {
            setBandFrequency (primaryBand, params::nearestNoteFrequency (xToFreq (e.position.x)));
            return;
        }

        // Empty space: start a selection rectangle. Ctrl/Cmd adds to what is selected.
        lassoBase.fill (false);

        if (e.mods.isCommandDown())
            lassoBase = selected;
        else
            setSelectedBand (-1);

        lassoActive = plot.expanded (4.0f).contains (e.position);
        lassoStart = lassoEnd = e.position;
        return;
    }

    if (e.mods.isPopupMenu())
    {
        // A right-click inside a selection works on all of it.
        if (isSelected (band)) makePrimary (band);
        else                   setSelectedBand (band);

        showBandMenu (band, e.getScreenPosition());
        return;
    }

    if (e.mods.isCommandDown())
    {
        toggleSelected (band);
        return;
    }

    if (e.mods.isAltDown())
    {
        setSelectedBand (band);
        const auto p = processor.getBandParams (band);
        HolterdipolterFilterProcessor::setParam (processor.bandParam (band, params::On), p.active ? 0.0f : 1.0f);
        return;
    }

    if (isSelected (band))
    {
        // Grabbing one of several selected bands moves them all; a plain click
        // without moving picks this one alone.
        collapseOnRelease = getNumSelected() > 1;
        makePrimary (band);
    }
    else
    {
        setSelectedBand (band);
    }

    beginDrag (band, e.position);
}

void EqDisplay::beginDrag (int band, juce::Point<float> startPos)
{
    endDrag();

    const auto lead = processor.getBandParams (band);

    dragBand = band;
    dragChangesGain = typeHasGain (lead.type);
    dragVirtualPos = nodePosition (band);
    dragStartY = dragVirtualPos.y;
    dragMoved = false;
    lastMousePos = startPos;

    // The band in hand comes first; every other selected band follows it.
    const auto add = [this] (int b)
    {
        const auto p = processor.getBandParams (b);

        DragItem item;
        item.band = b;
        item.freq = p.freq;
        item.gainDb = p.gainDb;
        item.q = p.q;
        item.gainGesture = dragChangesGain && typeHasGain (p.type);
        item.qGesture = ! dragChangesGain && ! typeHasGain (p.type);

        processor.bandParam (b, params::Freq)->beginChangeGesture();
        if (item.gainGesture) processor.bandParam (b, params::Gain)->beginChangeGesture();
        if (item.qGesture)    processor.bandParam (b, params::Q)->beginChangeGesture();

        dragItems.push_back (item);
    };

    add (band);

    if (isSelected (band))
        forEachSelected ([&] (int b) { if (b != band) add (b); });
}

void EqDisplay::endDrag()
{
    if (dragBand < 0)
        return;

    for (const auto& item : dragItems)
    {
        processor.bandParam (item.band, params::Freq)->endChangeGesture();
        if (item.gainGesture) processor.bandParam (item.band, params::Gain)->endChangeGesture();
        if (item.qGesture)    processor.bandParam (item.band, params::Q)->endChangeGesture();
    }

    dragItems.clear();
    dragBand = -1;
    repaint();
}

void EqDisplay::updateLasso (juce::Point<float> p)
{
    lassoEnd = { juce::jlimit (plot.getX(), plot.getRight(), p.x), juce::jlimit (plot.getY(), plot.getBottom(), p.y) };

    const juce::Rectangle<float> lasso (lassoStart, lassoEnd);
    auto next = lassoBase;

    for (int band = 0; band < kMaxBands; ++band)
        if (processor.isBandUsed (band) && lasso.expanded (0.5f).contains (nodePosition (band)))
            next[(size_t) band] = true;

    setSelection (next, primaryBand);
}

void EqDisplay::mouseDrag (const juce::MouseEvent& e)
{
    mouseX = e.position.x;
    mousePos = e.position;
    mouseRestingSince = juce::Time::getMillisecondCounter();

    if (sketching)
    {
        if (sketchDrawing)
        {
            const juce::Point<float> p (juce::jlimit (plot.getX(), plot.getRight(), e.position.x),
                                        juce::jlimit (plot.getY(), plot.getBottom(), e.position.y));

            if (sketchPoints.empty() || sketchPoints.back().getDistanceFrom (p) >= 2.0f)
                sketchPoints.push_back (p);

            repaint();
        }

        return;
    }

    if (dragBand < 0)
    {
        if (lassoActive)
            updateLasso (e.position);

        return;
    }

    const auto delta = e.position - lastMousePos;
    lastMousePos = e.position;

    if (delta.isOrigin() || dragItems.empty())
        return;

    dragMoved = true;
    dragVirtualPos += delta * (e.mods.isShiftDown() ? 0.2f : 1.0f);
    dragVirtualPos.x = juce::jlimit (plot.getX(), plot.getRight(), dragVirtualPos.x);
    dragVirtualPos.y = juce::jlimit (plot.getY(), plot.getBottom(), dragVirtualPos.y);

    const auto& lead = dragItems.front();

    // With the piano showing, the band in hand lands on notes; Shift moves freely.
    double freq = xToFreq (dragVirtualPos.x);

    if (pianoVisible && ! e.mods.isShiftDown())
        freq = params::nearestNoteFrequency (freq);

    freq = juce::jlimit (kMinFreq, kMaxFreq, freq);

    // What the band in hand does, every other dragged band does too:
    // the same interval in pitch, the same number of dB, the same change in Q.
    const double freqRatio = freq / lead.freq;
    double gainDelta = 0.0, qFactor = 1.0;

    if (dragChangesGain)
    {
        // The node shows the scaled gain, so undo the scale when writing the parameter.
        const double scale = (double) processor.getGainScale();

        if (scale > 0.02)
            gainDelta = juce::jlimit (-kMaxGainDb, kMaxGainDb, yToDb (dragVirtualPos.y) / scale) - lead.gainDb;
    }
    else
    {
        // Shapes without gain: dragging up narrows / adds resonance.
        qFactor = std::exp ((double) (dragStartY - dragVirtualPos.y) / 80.0);
    }

    for (const auto& item : dragItems)
    {
        const auto setInGesture = [this, &item] (params::BandField field, double value)
        {
            auto* param = processor.bandParam (item.band, field);
            param->setValueNotifyingHost (param->convertTo0to1 ((float) value));
        };

        setInGesture (params::Freq, juce::jlimit (kMinFreq, kMaxFreq, item.freq * freqRatio));

        if (item.gainGesture)
            setInGesture (params::Gain, juce::jlimit (-kMaxGainDb, kMaxGainDb, item.gainDb + gainDelta));

        if (item.qGesture)
            setInGesture (params::Q, juce::jlimit (kMinQ, kMaxQ, item.q * qFactor));
    }
}

void EqDisplay::mouseUp (const juce::MouseEvent&)
{
    if (sketching)
    {
        // A click without a drawing leaves sketch mode on; a drawing ends it.
        if (sketchDrawing && sketchPoints.size() > 3)
        {
            const auto drawn = sketchPoints;
            setSketching (false);
            applySketch (drawn);
        }

        sketchDrawing = false;
        sketchPoints.clear();
        repaint();
        return;
    }

    const int band = dragBand;
    const bool clicked = band >= 0 && ! dragMoved;

    endDrag();
    endGrab();

    if (lassoActive)
    {
        lassoActive = false;
        repaint();
    }

    if (clicked && collapseOnRelease)
        setSelectedBand (band);

    collapseOnRelease = false;
}

void EqDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || sketching)
        return;

    const int band = bandAt (e.position);

    if (band >= 0)
        removeBand (band);
    else if (plot.expanded (2.0f).contains (e.position))
        addBandAt (e.position);
}

int EqDisplay::createBand (FilterType type, double freq, double gainDb, double q, int slopeIndex)
{
    const int band = processor.findFreeBand();

    if (band < 0)
        return -1;

    const auto set = [this, band] (params::BandField field, double value)
    {
        HolterdipolterFilterProcessor::setParam (processor.bandParam (band, field), (float) value);
    };

    set (params::Type,  (double) (int) type);
    set (params::Slope, (double) slopeIndex);
    set (params::Place, 0.0);
    set (params::Q,     juce::jlimit (kMinQ, kMaxQ, q));
    set (params::Freq,  juce::jlimit (kMinFreq, kMaxFreq, freq));
    set (params::Gain,  juce::jlimit (-kMaxGainDb, kMaxGainDb, gainDb));
    set (params::On,    1.0);
    set (params::Dyn,   0.0);
    set (params::Thresh, -30.0);
    set (params::AutoThresh, 1.0);
    set (params::DynSide, 0.0);
    set (params::Used,  1.0);

    return band;
}

void EqDisplay::addBandAt (juce::Point<float> pos)
{
    const int band = processor.findFreeBand();

    if (band < 0)
        return;

    lassoActive = false;

    // Near the edges a cut is what you almost always want.
    const float xNorm = (pos.x - plot.getX()) / juce::jmax (1.0f, plot.getWidth());
    const auto type = xNorm < 0.07f ? FilterType::LowCut
                    : xNorm > 0.93f ? FilterType::HighCut
                                    : FilterType::Bell;

    double gain = 0.0;

    if (typeHasGain (type) && std::abs (pos.y - dbToY (0.0)) > 6.0f)
    {
        const double scale = (double) processor.getGainScale();
        gain = scale > 0.02 ? juce::jlimit (-kMaxGainDb, kMaxGainDb, yToDb (pos.y) / scale) : 0.0;
    }

    double freq = xToFreq (pos.x);

    if (pianoVisible)
        freq = params::nearestNoteFrequency (freq);

    createBand (type, freq, gain, 1.0, typeHasGain (type) ? 1 : 3);   // 12 dB/oct bells, 24 dB/oct cuts

    setSelectedBand (band);
    hoverBand = band;

    // Keep holding after the double-click to drag the new band straight away.
    beginDrag (band, pos);
}

void EqDisplay::adjustQ (int band, float amount)
{
    const auto p = processor.getBandParams (band);
    const double q = juce::jlimit (kMinQ, kMaxQ, p.q * std::exp ((double) amount));
    HolterdipolterFilterProcessor::setParam (processor.bandParam (band, params::Q), (float) q);
}

void EqDisplay::setBandFrequency (int band, double freq)
{
    HolterdipolterFilterProcessor::setParam (processor.bandParam (band, params::Freq),
                                             (float) juce::jlimit (kMinFreq, kMaxFreq, freq));
}

void EqDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int band = dragBand >= 0 || sketching ? -1 : bandAt (e.position);

    if (band < 0 || juce::approximatelyEqual (wheel.deltaY, 0.0f))
        return;

    const bool several = isSelected (band) && getNumSelected() > 1;

    // Alt + wheel: the dynamic range, one dB (with Shift a tenth) per notch.
    if (e.mods.isAltDown())
    {
        const float step = (wheel.deltaY > 0.0f ? 1.0f : -1.0f) * (e.mods.isShiftDown() ? 0.1f : 1.0f);
        const auto nudge = [this, step] (int b)
        {
            const auto p = processor.getBandParams (b);

            if (typeHasDynamics (p.type))
                HolterdipolterFilterProcessor::setParam (processor.bandParam (b, params::Dyn),
                                                         (float) juce::jlimit (-kMaxGainDb, kMaxGainDb, p.dynRangeDb + (double) step));
        };

        if (several) forEachSelected (nudge);
        else         nudge (band);

        return;
    }

    const float amount = wheel.deltaY * (e.mods.isShiftDown() ? 0.4f : 2.0f);

    // Over one of several selected bands, the wheel turns them all.
    if (several)
        forEachSelected ([this, amount] (int b) { adjustQ (b, amount); });
    else
        adjustQ (band, amount);
}

bool EqDisplay::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const bool anySelected = getNumSelected() > 0;

    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && anySelected)
    {
        removeSelected();
        return true;
    }

    if (key == juce::KeyPress::escapeKey && sketching)
    {
        setSketching (false);
        return true;
    }

    if (key == juce::KeyPress::escapeKey && grabActive)
    {
        endGrab();
        return true;
    }

    if (key == juce::KeyPress::escapeKey && anySelected)
    {
        setSelectedBand (-1);
        return true;
    }

    if (mods.isCommandDown() && ! mods.isShiftDown() && ! mods.isAltDown())
    {
        switch (juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) key.getKeyCode()))
        {
            case 'a':   selectAll();                                             return true;
            case 'c':   if (anySelected) copyToClipboard();                      return anySelected;
            case 'x':   if (anySelected) { copyToClipboard(); removeSelected(); } return anySelected;
            case 'v':   pasteFromClipboard();                                    return true;
            default:    break;
        }
    }

    return false;
}

//==============================================================================
void EqDisplay::showBandMenu (int band, juce::Point<int> screenPos)
{
    const auto p = processor.getBandParams (band);

    // Everything in this menu works on the whole selection the band belongs to.
    std::vector<int> targets;
    forEachSelected ([&targets] (int b) { targets.push_back (b); });

    if (targets.empty())
        targets.push_back (band);

    const bool several = targets.size() > 1;

    enum { toggleId = 1, removeId = 2, soloId = 3, copyId = 4, dynamicId = 5, typeBase = 100, slopeBase = 200, placeBase = 300 };

    juce::PopupMenu types, slopes, places;

    const auto typeNames = params::typeNames();
    for (int i = 0; i < typeNames.size(); ++i)
        types.addItem (typeBase + i, typeNames[i], true, i == (int) p.type);

    const auto slopeNames = params::slopeNames();
    for (int i = 0; i < slopeNames.size(); ++i)
        slopes.addItem (slopeBase + i, slopeNames[i], true, i == p.slopeIndex);

    const auto placeNames = params::placementNames();
    for (int i = 0; i < placeNames.size(); ++i)
        places.addItem (placeBase + i, placeNames[i], true, i == (int) p.placement);

    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());
    menu.addSectionHeader (several ? juce::String ((int) targets.size()) + " bands" : "Band " + juce::String (band + 1));
    menu.addSubMenu ("Shape", types);
    menu.addSubMenu ("Slope", slopes, typeHasSlope (p.type) || several);
    menu.addSubMenu ("Placement", places);

    const bool isDynamic = typeHasDynamics (p.type) && ! isExactlyZero (p.dynRangeDb);
    menu.addItem (dynamicId, "Dynamic (reacts to the level)", typeHasDynamics (p.type) || several, isDynamic);
    menu.addSeparator();

    if (! several)
        menu.addItem (soloId, "Solo band", true, soloedBand() == band);

    menu.addItem (toggleId, several ? (p.active ? "Bypass bands" : "Enable bands")
                                    : (p.active ? "Bypass band" : "Enable band"));
    menu.addItem (copyId, several ? "Copy bands" : "Copy band");
    menu.addItem (removeId, several ? "Remove bands" : "Remove band");

    juce::Component::SafePointer<EqDisplay> safe (this);
    const bool turnOn = ! p.active;

    menu.showMenuAsync (juce::PopupMenu::Options()
                            .withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 })
                            .withParentComponent (getTopLevelComponent()),
                        [safe, band, targets, turnOn, isDynamic] (int result)
    {
        if (safe == nullptr || result == 0)
            return;

        auto& proc = safe->processor;

        if (result == soloId)
        {
            if (proc.isBandUsed (band))
                proc.soloBand.store (proc.soloBand.load() == band ? -1 : band);

            return;
        }

        if (result == copyId)
        {
            safe->copyToClipboard();
            return;
        }

        for (int target : targets)
        {
            if (! proc.isBandUsed (target))
                continue;

            const auto set = [&proc, target] (params::BandField field, float value)
            {
                HolterdipolterFilterProcessor::setParam (proc.bandParam (target, field), value);
            };

            if (result == dynamicId)
            {
                // Switching on starts with a 6 dB cut of what gets loud: the usual use.
                if (typeHasDynamics (proc.getBandParams (target).type))
                    set (params::Dyn, isDynamic ? 0.0f : -6.0f);
            }
            else if (result == toggleId)    set (params::On, turnOn ? 1.0f : 0.0f);
            else if (result == removeId)    safe->removeBand (target);
            else if (result >= placeBase)   set (params::Place, (float) (result - placeBase));
            else if (result >= slopeBase)   set (params::Slope, (float) (result - slopeBase));
            else if (result >= typeBase)    set (params::Type, (float) (result - typeBase));
        }
    });
}

void EqDisplay::showBackgroundMenu (juce::Point<int> screenPos)
{
    enum { pasteId = 1, selectAllId, pianoId };

    const bool canPaste = juce::SystemClipboard::getTextFromClipboard().contains (clipboardTag)
                       && processor.findFreeBand() >= 0;
    const bool anyBand = std::any_of (snapshot.used.begin(), snapshot.used.end(), [] (bool b) { return b; });

    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());
    menu.addItem (pasteId, "Paste bands", canPaste);
    menu.addItem (selectAllId, "Select all bands", anyBand);
    menu.addSeparator();
    menu.addItem (pianoId, "Piano keys", true, pianoVisible);

    juce::Component::SafePointer<EqDisplay> safe (this);

    menu.showMenuAsync (juce::PopupMenu::Options()
                            .withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 })
                            .withParentComponent (getTopLevelComponent()),
                        [safe] (int result)
    {
        if (safe == nullptr)
            return;

        if (result == pasteId)          safe->pasteFromClipboard();
        else if (result == selectAllId) safe->selectAll();
        else if (result == pianoId)     safe->setPianoVisible (! safe->pianoVisible);
    });
}

void EqDisplay::showAnalyzerMenu()
{
    enum { modeBase = 1, sideId = 6, collisionId = 7, grabId = 8, freezeId = 9,
           speedBase = 10, resolutionBase = 20, tiltBase = 30, rangeBase = 40,
           refOwnId = 50, refSideId, refShowId, refClearId, defaultSaveId, defaultClearId, matchId };

    const auto options = [] (int base, int current, std::initializer_list<const char*> names)
    {
        juce::PopupMenu sub;
        int index = 0;

        for (const char* name : names)
        {
            sub.addItem (base + index, name, true, index == current);
            ++index;
        }

        return sub;
    };

    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());

    menu.addSectionHeader ("Show");
    menu.addItem (modeBase + analyzerOff,  "Off",                           true, analyzerMode == analyzerOff);
    menu.addItem (modeBase + analyzerPre,  "Before the EQ (pre)",           true, analyzerMode == analyzerPre);
    menu.addItem (modeBase + analyzerPost, "After the EQ (post)",           true, analyzerMode == analyzerPost);
    menu.addItem (modeBase + analyzerBoth, "Both",                          true, analyzerMode == analyzerBoth);
    menu.addSeparator();
    menu.addItem (freezeId, "Freeze", analyzerMode != analyzerOff, analyzerFrozen);
    menu.addItem (grabId, "Spectrum Grab (rest the mouse, drag a peak)", analyzerMode != analyzerOff, grabEnabled);
    menu.addSeparator();
    menu.addSectionHeader (processor.sidechainConnected.load() ? "Sidechain" : "Sidechain (not connected)");
    menu.addItem (sideId, "Show the sidechain's spectrum", analyzerMode != analyzerOff, sidechainVisible);
    menu.addItem (collisionId, "Mark where both are loud", analyzerMode != analyzerOff && sidechainVisible, collisionsVisible);
    menu.addSeparator();
    menu.addSectionHeader ("Reference curve");
    menu.addItem (refOwnId, "Record 4 seconds of this track", analyzerMode != analyzerOff);
    menu.addItem (refSideId, "Record 4 seconds of the sidechain", analyzerMode != analyzerOff && sideTrace.visible);
    menu.addItem (refShowId, "Show the reference", ! reference.empty(), referenceVisible && ! reference.empty());
    menu.addItem (matchId, "EQ Match: make this track sound like the reference (listens 4 s)",
                  ! reference.empty() && analyzerMode != analyzerOff);
    menu.addItem (refClearId, "Forget the reference", ! reference.empty());
    menu.addSeparator();
    menu.addSubMenu ("Speed",      options (speedBase, analyzerSpeed, { "Slow", "Medium", "Fast" }));
    menu.addSubMenu ("Resolution", options (resolutionBase, analyzerResolution, { "Low", "Medium", "High", "Maximum" }));
    menu.addSubMenu ("Tilt",       options (tiltBase, analyzerTilt, { "0 dB/oct", "3 dB/oct", "4.5 dB/oct", "6 dB/oct" }));
    menu.addSubMenu ("Range",      options (rangeBase, analyzerRange, { "60 dB", "90 dB", "120 dB" }));
    menu.addSeparator();
    menu.addSectionHeader ("New instances");
    menu.addItem (defaultSaveId, "Save this view as my default");
    menu.addItem (defaultClearId, "Back to the built-in default", ViewDefaults::getFile().existsAsFile());

    juce::Component::SafePointer<EqDisplay> safe (this);

    menu.showMenuAsync (juce::PopupMenu::Options()
                            .withTargetComponent (analyzerButton)
                            .withParentComponent (getTopLevelComponent()),
                        [safe] (int result)
    {
        if (safe == nullptr || result == 0)
            return;

        if (result == matchId)              safe->matchToReference();
        else if (result == refOwnId)        safe->captureReference (false);
        else if (result == refSideId)       safe->captureReference (true);
        else if (result == refShowId)
        {
            safe->referenceVisible = ! safe->referenceVisible;
            safe->storeView (referenceShownProp, safe->referenceVisible);
            safe->repaint();
        }
        else if (result == refClearId)      safe->clearReference();
        else if (result == defaultSaveId)   safe->saveViewAsDefault();
        else if (result == defaultClearId)
        {
            ViewDefaults::clear();
            safe->showNotice ("NEW INSTANCES START WITH THE BUILT-IN VIEW AGAIN");
        }
        else if (result == freezeId)        safe->setAnalyzerFrozen (! safe->analyzerFrozen);
        else if (result == grabId)          safe->setGrabEnabled (! safe->grabEnabled);
        else if (result == sideId)          safe->setSidechainVisible (! safe->sidechainVisible);
        else if (result == collisionId)     safe->setCollisionsVisible (! safe->collisionsVisible);
        else if (result >= rangeBase)       safe->setAnalyzerRange (result - rangeBase);
        else if (result >= tiltBase)        safe->setAnalyzerTilt (result - tiltBase);
        else if (result >= resolutionBase)  safe->setAnalyzerResolution (result - resolutionBase);
        else if (result >= speedBase)       safe->setAnalyzerSpeed (result - speedBase);
        else                                safe->setAnalyzerMode (result - modeBase);
    });
}

} // namespace hdp::ui
