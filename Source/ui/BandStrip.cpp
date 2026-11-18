#include "BandStrip.h"

namespace hdp::ui
{

BandStrip::BandStrip (HolterdipolterFilterProcessor& p)
    : processor (p),
      place ({ { "ST", theme::accent }, { "L", theme::left }, { "R", theme::right },
               { "M", theme::mid }, { "S", theme::side } })
{
    for (auto* c : std::initializer_list<juce::Component*> { &power, &solo, &remove, &types, &freq, &gain, &q, &slope, &place,
                                                              &dynRange, &threshold, &autoThreshold, &fromSidechain })
        addChildComponent (c);

    dynRange.setCompact (true);
    threshold.setCompact (true);
    autoThreshold.setTooltip ("Auto: the band acts on what stands out from the rest of the signal, at any input level");

    autoThreshold.onClick = [this]
    {
        if (band >= 0)
        {
            const bool isAuto = processor.getBandParams (band).autoThreshold;
            HolterdipolterFilterProcessor::setParam (processor.bandParam (band, params::AutoThresh), isAuto ? 0.0f : 1.0f);
            refresh();
        }
    };

    power.setTooltip ("Bypass this band");
    solo.setTooltip ("Solo: listen only to the range this band works on");
    remove.setTooltip ("Remove this band");

    solo.onClick = [this]
    {
        if (band >= 0)
            processor.soloBand.store (processor.soloBand.load() == band ? -1 : band);

        refresh();
    };

    remove.onClick = [this]
    {
        if (band >= 0 && onRemove != nullptr)
            onRemove (band);
    };

    fromSidechain.setTooltip ("Sidechain: the band reacts to the track at the sidechain input instead of its own");
    fromSidechain.onClick = [this]
    {
        if (band >= 0)
        {
            const bool fromSide = processor.getBandParams (band).dynFromSidechain;
            HolterdipolterFilterProcessor::setParam (processor.bandParam (band, params::DynSide), fromSide ? 0.0f : 1.0f);
            refresh();
        }
    };

    startTimerHz (15);
}

void BandStrip::setBand (int newBand)
{
    if (band == newBand)
        return;

    band = newBand;
    const bool has = band >= 0;

    // Solo follows the selection, and ends when nothing is selected.
    if (processor.soloBand.load() >= 0)
        processor.soloBand.store (band);

    const auto param = [this, has] (params::BandField field) -> juce::RangedAudioParameter*
    {
        return has ? processor.bandParam (band, field) : nullptr;
    };

    power.setParameter (param (params::On));
    types.setParameter (param (params::Type));
    freq.setParameter (param (params::Freq));
    gain.setParameter (param (params::Gain));
    q.setParameter (param (params::Q));
    slope.setParameter (param (params::Slope));
    place.setParameter (param (params::Place));
    dynRange.setParameter (param (params::Dyn));
    threshold.setParameter (param (params::Thresh));

    for (auto* c : std::initializer_list<juce::Component*> { &power, &solo, &remove, &types, &freq, &gain, &q, &slope, &place,
                                                              &dynRange, &threshold, &autoThreshold, &fromSidechain })
        c->setVisible (has);

    refresh();
    repaint();
}

void BandStrip::setSelectionCount (int count)
{
    if (selectionCount != count)
    {
        selectionCount = count;
        repaint();
    }
}

void BandStrip::refresh()
{
    if (band < 0)
        return;

    const auto p = processor.getBandParams (band);
    const auto colour = theme::placementColour (p.placement);

    if (colour != accent)
    {
        accent = colour;
        repaint();
    }

    power.setAccent (colour);
    solo.setAccent (colour);
    solo.setActive (processor.soloBand.load() == band);
    types.setAccent (colour);
    freq.setAccent (colour);
    gain.setAccent (colour);
    q.setAccent (colour);
    slope.setAccent (colour);

    gain.setDimmed (! typeHasGain (p.type));

    // First-order shapes have no resonance to set, and Flat Tilt is frequency and gain alone.
    const bool firstOrder = p.slopeIndex == 0
                         && p.type != FilterType::Bell && p.type != FilterType::Notch && p.type != FilterType::BandPass;
    q.setDimmed (firstOrder || ! typeHasQ (p.type));
    slope.setDimmed (! typeHasSlope (p.type));

    // Dynamics: bells and shelves only; with a range of zero the band is static.
    const bool canBeDynamic = typeHasDynamics (p.type);
    const bool isDynamic = canBeDynamic && ! isExactlyZero (p.dynRangeDb);

    dynRange.setAccent (colour);
    threshold.setAccent (colour);
    autoThreshold.setAccent (colour);
    dynRange.setDimmed (! canBeDynamic);
    threshold.setDimmed (! isDynamic || p.autoThreshold);
    threshold.setOverrideText (p.autoThreshold ? "auto" : "");
    autoThreshold.setActive (isDynamic && p.autoThreshold);
    fromSidechain.setAccent (theme::sidechain);
    fromSidechain.setActive (isDynamic && p.dynFromSidechain);

    const float nowDb = isDynamic ? processor.dynamicGainDb[(size_t) band].load (std::memory_order_relaxed) : 0.0f;

    if (std::abs (nowDb - shownDynamicDb) > 0.02f || isDynamic != shownDynamic || ! juce::exactlyEqual ((float) p.dynRangeDb, shownRangeDb))
    {
        shownDynamicDb = nowDb;
        shownDynamic = isDynamic;
        shownRangeDb = (float) p.dynRangeDb;
        repaint (dynMeter.expanded (2));
    }
}

void BandStrip::paint (juce::Graphics& g)
{
    g.setColour (theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    if (band < 0)
    {
        g.setFont (theme::ui (13.0f));
        g.setColour (theme::textDim);
        g.drawText ("No band selected.  Double-click the display to add one, drag a node to shape it, drag across empty space to select several, right-click for options.",
                    getLocalBounds().reduced (18, 0), juce::Justification::centredLeft, true);
        return;
    }

    auto badge = badgeArea.toFloat();

    g.setFont (theme::ui (10.0f, true));
    g.setColour (theme::textDim);
    g.drawText (selectionCount > 1 ? juce::String (selectionCount) + " SEL." : juce::String ("BAND"),
                badge.removeFromTop (12.0f), juce::Justification::centredLeft, false);

    g.setFont (theme::mono (26.0f, true));
    g.setColour (accent);
    g.drawText (juce::String (band + 1).paddedLeft ('0', 2), badge, juce::Justification::centredLeft, false);

    // Dynamics row: caption, and how much of the range is in use right now.
    g.setFont (theme::ui (10.0f, true));
    g.setColour (theme::textDim.withAlpha (shownDynamic ? 1.0f : 0.5f));
    g.drawText ("DYNAMIC", dynCaption, juce::Justification::centredLeft, false);

    auto meter = dynMeter.toFloat();
    auto readout = meter.removeFromRight (64.0f);
    meter = meter.withSizeKeepingCentre (meter.getWidth(), 4.0f);

    g.setColour (theme::grid);
    g.fillRoundedRectangle (meter, 2.0f);

    if (shownDynamic)
    {
        const float scaledRange = shownRangeDb * processor.getGainScale();
        const float used = std::abs (scaledRange) > 0.01f ? juce::jlimit (0.0f, 1.0f, shownDynamicDb / scaledRange) : 0.0f;

        g.setColour (accent);
        g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * used), 2.0f);
    }

    g.setFont (theme::mono (12.0f));
    g.setColour (shownDynamic ? theme::text : theme::textFaint);
    g.drawText (shownDynamic ? params::formatGain (shownDynamicDb) : juce::String ("static"), readout,
                juce::Justification::centredRight, false);
}

void BandStrip::resized()
{
    auto area = getLocalBounds().reduced (14, 12);

    // Second row: the dynamics of the band, from below the shape selector to the right edge.
    auto dynRow = area.removeFromBottom (26).withTrimmedLeft (142);
    area.removeFromBottom (8);

    dynCaption = dynRow.removeFromLeft (62);
    dynRange.setBounds (dynRow.removeFromLeft (150));
    dynRow.removeFromLeft (8);
    threshold.setBounds (dynRow.removeFromLeft (176));
    dynRow.removeFromLeft (8);
    autoThreshold.setBounds (dynRow.removeFromLeft (54));
    dynRow.removeFromLeft (8);
    fromSidechain.setBounds (dynRow.removeFromLeft (44));
    dynRow.removeFromLeft (16);
    dynMeter = dynRow;

    // Band number, with bypass and remove next to it.
    auto left = area.removeFromLeft (132);
    badgeArea = left.removeFromLeft (48);
    left.removeFromLeft (4);
    power.setBounds (left.removeFromLeft (26).withSizeKeepingCentre (26, 26));
    solo.setBounds (left.removeFromLeft (26).withSizeKeepingCentre (26, 26));
    remove.setBounds (left.removeFromLeft (26).withSizeKeepingCentre (26, 26));

    area.removeFromLeft (10);

    const int placeWidth = 5 * 32;
    place.setBounds (area.removeFromRight (placeWidth).withSizeKeepingCentre (placeWidth, 28));
    area.removeFromRight (12);

    const int typesWidth = juce::jlimit (kNumFilterTypes * 28, kNumFilterTypes * 34, (int) ((float) area.getWidth() * 0.42f));
    types.setBounds (area.removeFromLeft (typesWidth).withSizeKeepingCentre (typesWidth, 34));
    area.removeFromLeft (12);

    const int gap = 8;
    const int fieldWidth = (area.getWidth() - 3 * gap) / 4;

    for (auto* field : { &freq, &gain, &q, &slope })
    {
        field->setBounds (area.removeFromLeft (fieldWidth));
        area.removeFromLeft (gap);
    }
}

} // namespace hdp::ui
