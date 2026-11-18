// holterdipolter Filter - controls for the selected band

#pragma once

#include "../PluginProcessor.h"
#include "Controls.h"

namespace hdp::ui
{

class BandStrip final : public juce::Component,
                        private juce::Timer
{
public:
    explicit BandStrip (HolterdipolterFilterProcessor&);

    std::function<void (int)> onRemove;

    void setBand (int newBand);

    /** How many bands are selected in the display; the strip edits the first of them. */
    void setSelectionCount (int count);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override   { refresh(); }
    void refresh();

    HolterdipolterFilterProcessor& processor;
    int band = -1, selectionCount = 0;
    juce::Colour accent = theme::accent;
    juce::Rectangle<int> badgeArea;

    IconButton power { IconButton::Icon::Power };
    IconButton solo { IconButton::Icon::Solo };
    IconButton remove { IconButton::Icon::Remove };
    TypeSelector types;
    ParamField freq { "FREQ" }, gain { "GAIN" }, q { "Q" }, slope { "SLOPE" };
    SegmentedSelector place;

    // Second row: dynamics.
    ParamField dynRange { "RANGE" }, threshold { "THRESHOLD" };
    LabelButton autoThreshold { "AUTO" }, fromSidechain { "SC" };
    juce::Rectangle<int> dynCaption, dynMeter;
    float shownDynamicDb = 0.0f, shownRangeDb = 0.0f;
    bool shownDynamic = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandStrip)
};

} // namespace hdp::ui
