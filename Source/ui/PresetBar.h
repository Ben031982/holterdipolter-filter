// holterdipolter Filter - preset field
//
// Shows which preset the live setting came from, steps through the presets,
// and opens the menu to load, save and delete them. A new preset is named
// right in the field; nothing here opens a window of its own.

#pragma once

#include "../PluginProcessor.h"
#include "Controls.h"

namespace hdp::ui
{

class PresetBar final : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    explicit PresetBar (HolterdipolterFilterProcessor&);

    /** Brings name and "modified" mark up to date; call regularly. */
    void refresh();

    /** Opens the field for typing a preset name ("Save as"). */
    void showNameEditor();

    /** What Return does while a name is being typed. False if it asks to confirm first. */
    bool commitName (const juce::String& typed);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override   { hover = true;  repaint(); }
    void mouseExit (const juce::MouseEvent&) override    { hover = false; repaint(); }

private:
    void showMenu();
    void hideNameEditor();

    HolterdipolterFilterProcessor& processor;

    IconButton previous { IconButton::Icon::Previous }, next { IconButton::Icon::Next };
    std::unique_ptr<juce::TextEditor> editor;

    juce::String shownName, replaceCandidate;
    bool shownModified = false, hover = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
};

} // namespace hdp::ui
