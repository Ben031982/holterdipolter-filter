#include "PluginEditor.h"

using namespace hdp;

namespace
{
    constexpr int defaultWidth = 1100, defaultHeight = 650;
    constexpr int minWidth = 1020, minHeight = 560;

    juce::String arrow (const char* from, const char* to)
    {
        return juce::String (from) + juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 ")) + to;
    }
}

HolterdipolterFilterEditor::HolterdipolterFilterEditor (HolterdipolterFilterProcessor& p)
    : AudioProcessorEditor (&p),
      filter (p),
      display (p),
      strip (p),
      presetBar (p),
      abSelector ({ { "A", theme::accent }, { "B", theme::accent } })
{
    setLookAndFeel (&lookAndFeel);
    lookAndFeel.setColour (juce::TooltipWindow::backgroundColourId, theme::panelRaised);
    lookAndFeel.setColour (juce::TooltipWindow::textColourId, theme::text);
    lookAndFeel.setColour (juce::TooltipWindow::outlineColourId, theme::gridStrong);
    tooltips.setLookAndFeel (&lookAndFeel);

    for (auto* c : std::initializer_list<juce::Component*> { &display, &strip, &presetBar, &abSelector, &copyButton,
                                                              &phaseButton, &scaleField, &outputField, &autoGain,
                                                              &undoButton, &redoButton })
        addAndMakeVisible (c);

    autoGain.setParameter (filter.autoGainParam());
    phaseButton.setParameter (filter.phaseInvertParam());
    phaseButton.setTooltip ("Invert the polarity of the output");

    undoButton.setTooltip ("Undo");
    redoButton.setTooltip ("Redo");
    undoButton.onClick = [this] { filter.history.undo(); timerCallback(); };
    redoButton.onClick = [this] { filter.history.redo(); timerCallback(); };

    // A and B are two settings side by side: one is live, the other waits.
    abSelector.onSelect = [this] (int slot)
    {
        if (slot != filter.presets.getActiveSlot())
            filter.presets.switchSlot();

        timerCallback();
    };

    copyButton.setTooltip ("Copy the live setting over the other one");
    copyButton.onClick = [this] { filter.presets.copyToOtherSlot(); };

    setWantsKeyboardFocus (true);

    scaleField.setParameter (filter.gainScaleParam());
    outputField.setParameter (filter.outGainParam());
    scaleField.setAccent (theme::text);
    outputField.setAccent (theme::text);

    display.onSelectionChanged = [this] (int band, int count)
    {
        strip.setBand (band);
        strip.setSelectionCount (count);
    };

    strip.onRemove = [this] (int band) { display.removeBand (band); };

    const auto& state = filter.apvts.state;

    setResizable (true, true);
    setResizeLimits (minWidth, minHeight, 2600, 1600);
    setSize (juce::jlimit (minWidth, 2600, (int) state.getProperty ("uiWidth", defaultWidth)),
             juce::jlimit (minHeight, 1600, (int) state.getProperty ("uiHeight", defaultHeight)));

    constructed = true;

    timerCallback();
    startTimerHz (10);
}

HolterdipolterFilterEditor::~HolterdipolterFilterEditor()
{
    // Solo is a monitoring aid; it must not stay on with nobody able to see it.
    filter.soloBand.store (-1);

    tooltips.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void HolterdipolterFilterEditor::paintLogo (juce::Graphics& g, juce::Rectangle<float> area)
{
    // Three squares tumbling down a step: holterdipolter.
    const float size = 9.0f;
    const float baseX = area.getX();
    const float midY = area.getCentreY();

    const struct { float dx, dy, angle, alpha; } squares[] = {
        { 0.0f,  -7.0f, 0.0f,  0.45f },
        { 11.0f, -1.0f, 0.42f, 0.7f },
        { 23.0f,  6.0f, 0.95f, 1.0f },
    };

    for (const auto& s : squares)
    {
        juce::Path square;
        square.addRoundedRectangle (-size * 0.5f, -size * 0.5f, size, size, 1.6f);
        g.setColour (theme::accent.withAlpha (s.alpha));
        g.fillPath (square, juce::AffineTransform::rotation (s.angle).translated (baseX + size * 0.5f + s.dx, midY + s.dy));
    }

    auto textArea = area.withTrimmedLeft (44.0f);

    const auto nameFont = theme::ui (20.0f, true);
    const juce::String name ("holterdipolter");
    const float nameWidth = juce::GlyphArrangement::getStringWidth (nameFont, name);

    g.setFont (nameFont);
    g.setColour (theme::text);
    g.drawText (name, textArea, juce::Justification::centredLeft, false);

    g.setFont (theme::mono (11.0f, true));
    g.setColour (theme::accent);
    g.drawText ("FILTER", textArea.withTrimmedLeft (nameWidth + 9.0f).translated (0.0f, 2.5f),
                juce::Justification::centredLeft, false);
}

void HolterdipolterFilterEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    paintLogo (g, topBar.toFloat().withTrimmedLeft (6.0f).withWidth (236.0f));
}

void HolterdipolterFilterEditor::resized()
{
    auto area = getLocalBounds().reduced (12);

    topBar = area.removeFromTop (46);
    area.removeFromTop (10);

    {
        auto bar = topBar;

        // From the right: level, then history, then A/B. The preset field takes what is left.
        outputField.setBounds (bar.removeFromRight (92));
        bar.removeFromRight (8);
        autoGain.setBounds (bar.removeFromRight (80));
        bar.removeFromRight (8);
        scaleField.setBounds (bar.removeFromRight (92));
        bar.removeFromRight (8);
        phaseButton.setBounds (bar.removeFromRight (28).withSizeKeepingCentre (28, 28));
        bar.removeFromRight (14);

        auto history = bar.removeFromRight (60).withSizeKeepingCentre (60, 28);
        redoButton.setBounds (history.removeFromRight (28));
        history.removeFromRight (4);
        undoButton.setBounds (history.removeFromRight (28));
        bar.removeFromRight (14);

        copyButton.setBounds (bar.removeFromRight (54).withSizeKeepingCentre (54, 26));
        bar.removeFromRight (6);
        abSelector.setBounds (bar.removeFromRight (56).withSizeKeepingCentre (56, 26));
        bar.removeFromRight (14);

        bar.removeFromLeft (248);   // logo
        presetBar.setBounds (bar.removeFromLeft (juce::jmin (340, bar.getWidth())));
    }

    strip.setBounds (area.removeFromBottom (106));
    area.removeFromBottom (10);
    display.setBounds (area);

    if (constructed)
    {
        filter.apvts.state.setProperty ("uiWidth", getWidth(), nullptr);
        filter.apvts.state.setProperty ("uiHeight", getHeight(), nullptr);
    }
}

void HolterdipolterFilterEditor::timerCallback()
{
    undoButton.setAvailable (filter.history.canUndo());
    redoButton.setAvailable (filter.history.canRedo());

    autoGain.setReadout (params::formatGain (filter.autoGainDb.load (std::memory_order_relaxed)));

    presetBar.refresh();

    const int slot = filter.presets.getActiveSlot();
    abSelector.setSelected (slot);
    copyButton.setText (slot == 0 ? arrow ("A", "B") : arrow ("B", "A"));
}

bool HolterdipolterFilterEditor::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();

    if (! mods.isCommandDown())
        return false;

    const auto code = key.getKeyCode();

    if (code == 'z' || code == 'Z')
    {
        if (mods.isShiftDown()) filter.history.redo();
        else                    filter.history.undo();

        timerCallback();
        return true;
    }

    if (code == 'y' || code == 'Y')
    {
        filter.history.redo();
        timerCallback();
        return true;
    }

    return false;
}
