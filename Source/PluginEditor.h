// holterdipolter Filter - editor

#pragma once

#include "PluginProcessor.h"
#include "ui/BandStrip.h"
#include "ui/Controls.h"
#include "ui/EqDisplay.h"
#include "ui/PresetBar.h"
#include "ui/Theme.h"

class HolterdipolterFilterEditor final : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit HolterdipolterFilterEditor (HolterdipolterFilterProcessor&);
    ~HolterdipolterFilterEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    /** Selects a band as if its node had been clicked. */
    void selectBand (int band)   { display.setSelectedBand (band); }

    hdp::ui::EqDisplay& getDisplay()     { return display; }
    hdp::ui::PresetBar& getPresetBar()   { return presetBar; }

private:
    void paintLogo (juce::Graphics&, juce::Rectangle<float> area);
    void timerCallback() override;

    HolterdipolterFilterProcessor& filter;

    hdp::theme::LookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    hdp::ui::EqDisplay display;
    hdp::ui::BandStrip strip;

    hdp::ui::PresetBar presetBar;
    hdp::ui::SegmentedSelector abSelector;
    hdp::ui::LabelButton copyButton;
    hdp::ui::IconButton phaseButton { hdp::ui::IconButton::Icon::Phase };
    hdp::ui::ParamField scaleField { "GAIN SCALE" }, outputField { "OUTPUT" };
    hdp::ui::ToggleReadout autoGain { "AUTO" };
    hdp::ui::IconButton undoButton { hdp::ui::IconButton::Icon::Undo };
    hdp::ui::IconButton redoButton { hdp::ui::IconButton::Icon::Redo };

    juce::Rectangle<int> topBar;
    bool constructed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HolterdipolterFilterEditor)
};
