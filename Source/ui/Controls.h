// holterdipolter Filter - small controls
//
// The band a control edits changes whenever another band is selected, so every
// control here can be re-pointed at a different parameter at runtime.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Theme.h"

namespace hdp::ui
{

//==============================================================================
/** Connects a control to one parameter; can be re-attached at any time. */
class ParamBinding
{
public:
    std::function<void()> onChange;

    void attach (juce::RangedAudioParameter* newParam);

    juce::RangedAudioParameter* getParam() const   { return param; }
    bool isAttached() const                        { return param != nullptr; }

    float get() const                              { return value; }
    float getNormalised() const                    { return param != nullptr ? param->convertTo0to1 (value) : 0.0f; }
    juce::String getText() const;

    void set (float newValue);                     // one complete gesture
    void beginGesture();
    void setInGesture (float newValue);
    void endGesture();

private:
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    float value = 0.0f;
    bool inGesture = false;
};

//==============================================================================
/** A labelled numeric readout that is dragged to change its value.
    Drag up or right to increase, Shift for fine steps, double-click to type,
    Ctrl/Cmd-click to reset. */
class ParamField final : public juce::Component
{
public:
    explicit ParamField (juce::String labelText);

    void setParameter (juce::RangedAudioParameter* param);
    void setAccent (juce::Colour c);
    void setDimmed (bool shouldBeDimmed);

    /** One line, label left and value right, for rows that have little height. */
    void setCompact (bool shouldBeCompact)   { compact = shouldBeCompact; repaint(); }

    /** Shown instead of the value, e.g. while something else decides it. Empty = the value. */
    void setOverrideText (const juce::String& text);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override   { hover = true;  repaint(); }
    void mouseExit (const juce::MouseEvent&) override    { hover = false; repaint(); }

private:
    void showEditor();
    void hideEditor (bool commit);

    juce::String label;
    ParamBinding binding;
    juce::Colour accent = theme::accent;
    juce::String overrideText;
    bool dimmed = false, hover = false, dragging = false, compact = false;
    float dragNorm = 0.0f;
    juce::Point<float> lastDragPos;
    std::unique_ptr<juce::TextEditor> editor;
};

//==============================================================================
/** A row of mutually exclusive segments. Works standalone or bound to a choice parameter. */
class SegmentedSelector final : public juce::Component
{
public:
    struct Segment
    {
        juce::String text;
        juce::Colour colour;
    };

    explicit SegmentedSelector (std::vector<Segment> segmentsToUse);

    std::function<void (int)> onSelect;

    void setParameter (juce::RangedAudioParameter* param);
    void setSelected (int index);
    int getSelected() const   { return selected; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    std::vector<Segment> segments;
    ParamBinding binding;
    int selected = 0;
};

//==============================================================================
/** A row of small curve glyphs, one per filter shape. */
class TypeSelector final : public juce::Component,
                           public juce::SettableTooltipClient
{
public:
    TypeSelector();

    void setParameter (juce::RangedAudioParameter* param);
    void setAccent (juce::Colour c)   { accent = c; repaint(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    static void drawGlyph (juce::Graphics&, FilterType, juce::Rectangle<float> area, juce::Colour, float thickness);

private:
    int cellAt (juce::Point<int> p) const;

    ParamBinding binding;
    juce::Colour accent = theme::accent;
    int hoverCell = -1;
};

//==============================================================================
/** Minimal icon button. Power and Phase are parameter-bound toggles; the others just click. */
class IconButton final : public juce::Component,
                         public juce::SettableTooltipClient
{
public:
    enum class Icon { Power, Remove, Solo, Undo, Redo, Previous, Next, Phase };

    explicit IconButton (Icon iconToUse);

    std::function<void()> onClick;

    void setParameter (juce::RangedAudioParameter* param);   // for Icon::Power and Icon::Phase
    void setAccent (juce::Colour c)   { accent = c; repaint(); }
    void setActive (bool shouldBeActive);                    // lit, e.g. solo engaged
    void setAvailable (bool shouldBeAvailable);              // dimmed and inert when false

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override   { hover = true;  repaint(); }
    void mouseExit (const juce::MouseEvent&) override    { hover = false; repaint(); }

private:
    Icon icon;
    ParamBinding binding;
    juce::Colour accent = theme::accent;
    bool hover = false, active = false, available = true;
};

//==============================================================================
/** A small text button; can stay lit and can carry a menu arrow. */
class LabelButton final : public juce::Component,
                          public juce::SettableTooltipClient
{
public:
    explicit LabelButton (juce::String textToShow = {});

    std::function<void()> onClick;

    void setText (const juce::String& newText);
    void setActive (bool shouldBeLit);
    void setShowsMenuArrow (bool shouldShow)   { menuArrow = shouldShow; repaint(); }
    void setAccent (juce::Colour c)            { accent = c; repaint(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override   { hover = true;  repaint(); }
    void mouseExit (const juce::MouseEvent&) override    { hover = false; repaint(); }

private:
    juce::String text;
    juce::Colour accent = theme::accent;
    bool hover = false, active = false, menuArrow = false;
};

//==============================================================================
/** An on/off parameter shown as a labelled field with a live readout while it is on. */
class ToggleReadout final : public juce::Component
{
public:
    explicit ToggleReadout (juce::String labelText);

    void setParameter (juce::RangedAudioParameter* param);
    void setReadout (const juce::String& textWhileOn);
    bool isOn() const   { return binding.isAttached() && binding.get() > 0.5f; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override   { hover = true;  repaint(); }
    void mouseExit (const juce::MouseEvent&) override    { hover = false; repaint(); }

private:
    juce::String label, readout;
    ParamBinding binding;
    bool hover = false;
};

} // namespace hdp::ui
