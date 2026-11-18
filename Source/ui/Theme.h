// holterdipolter Filter - colours and type
//
// Warm near-black with one acid accent. Colour is used as information:
// a band's hue tells you which part of the stereo image it works on.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../dsp/FilterDesign.h"

namespace hdp::theme
{

inline const juce::Colour background  { 0xff0d0e0c };
inline const juce::Colour panel       { 0xff141612 };
inline const juce::Colour panelRaised { 0xff1d201a };
inline const juce::Colour grid        { 0xff23271f };
inline const juce::Colour gridStrong  { 0xff343a2d };
inline const juce::Colour text        { 0xffecebdd };
inline const juce::Colour textDim     { 0xff8d9282 };
inline const juce::Colour textFaint   { 0xff5a5f52 };

inline const juce::Colour accent      { 0xffc9f53b };   // stereo
inline const juce::Colour left        { 0xffffb23e };
inline const juce::Colour right       { 0xffa98bff };
inline const juce::Colour mid         { 0xff45d9f5 };
inline const juce::Colour side        { 0xffff5ba8 };
inline const juce::Colour warn        { 0xffff5a4a };
inline const juce::Colour sidechain   { 0xffffb23e };   // the other track in the analyzer

inline juce::Colour placementColour (Placement p)
{
    switch (p)
    {
        case Placement::Left:   return left;
        case Placement::Right:  return right;
        case Placement::Mid:    return mid;
        case Placement::Side:   return side;
        case Placement::Stereo: break;
    }

    return accent;
}

inline juce::String sansName()
{
   #if JUCE_WINDOWS
    return "Segoe UI";
   #elif JUCE_MAC
    return "Helvetica Neue";
   #else
    return juce::Font::getDefaultSansSerifFontName();
   #endif
}

inline juce::String monoName()
{
   #if JUCE_WINDOWS
    return "Consolas";
   #elif JUCE_MAC
    return "Menlo";
   #else
    return juce::Font::getDefaultMonospacedFontName();
   #endif
}

inline juce::Font ui (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (sansName(), height, bold ? juce::Font::bold : juce::Font::plain));
}

inline juce::Font mono (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (monoName(), height, bold ? juce::Font::bold : juce::Font::plain));
}

/** Popup menus and inline text editors in the plugin's colours. */
class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColour (juce::PopupMenu::backgroundColourId, panelRaised);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::headerTextColourId, textDim);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
        setColour (juce::PopupMenu::highlightedTextColourId, background);

        setColour (juce::TextEditor::backgroundColourId, background);
        setColour (juce::TextEditor::textColourId, text);
        setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.35f));
        setColour (juce::TextEditor::highlightedTextColourId, text);
        setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        setColour (juce::TextEditor::focusedOutlineColourId, accent);
        setColour (juce::CaretComponent::caretColourId, accent);
    }

    juce::Font getPopupMenuFont() override   { return ui (14.0f); }
};

} // namespace hdp::theme
