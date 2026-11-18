#include "Controls.h"

namespace hdp::ui
{

//==============================================================================
void ParamBinding::attach (juce::RangedAudioParameter* newParam)
{
    if (inGesture)
        endGesture();

    attachment.reset();
    param = newParam;

    if (param != nullptr)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v)
        {
            value = v;

            if (onChange != nullptr)
                onChange();
        });

        attachment->sendInitialUpdate();
    }
    else if (onChange != nullptr)
    {
        onChange();
    }
}

juce::String ParamBinding::getText() const
{
    return param != nullptr ? param->getText (param->convertTo0to1 (value), 32) : juce::String ("-");
}

void ParamBinding::set (float newValue)
{
    if (attachment != nullptr)
        attachment->setValueAsCompleteGesture (newValue);
}

void ParamBinding::beginGesture()
{
    if (attachment != nullptr && ! inGesture)
    {
        attachment->beginGesture();
        inGesture = true;
    }
}

void ParamBinding::setInGesture (float newValue)
{
    if (attachment != nullptr && inGesture)
        attachment->setValueAsPartOfGesture (newValue);
}

void ParamBinding::endGesture()
{
    if (attachment != nullptr && inGesture)
        attachment->endGesture();

    inGesture = false;
}

//==============================================================================
ParamField::ParamField (juce::String labelText) : label (std::move (labelText))
{
    binding.onChange = [this] { repaint(); };
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void ParamField::setParameter (juce::RangedAudioParameter* param)
{
    hideEditor (false);
    binding.attach (param);
}

void ParamField::setAccent (juce::Colour c)
{
    if (accent != c)
    {
        accent = c;
        repaint();
    }
}

void ParamField::setDimmed (bool shouldBeDimmed)
{
    if (dimmed != shouldBeDimmed)
    {
        dimmed = shouldBeDimmed;
        repaint();
    }
}

void ParamField::setOverrideText (const juce::String& text)
{
    if (overrideText != text)
    {
        overrideText = text;
        repaint();
    }
}

void ParamField::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    const bool active = binding.isAttached() && ! dimmed;

    g.setColour (hover && active ? theme::panelRaised.brighter (0.06f) : theme::panelRaised);
    g.fillRoundedRectangle (area, 4.0f);

    if (compact)
    {
        auto line = area.reduced (9.0f, 0.0f);

        g.setFont (theme::ui (10.0f, true));
        g.setColour (theme::textDim.withAlpha (active ? 1.0f : 0.5f));
        g.drawText (label, line, juce::Justification::centredLeft, false);

        if (active)
        {
            g.setColour (accent.withAlpha (dragging ? 1.0f : 0.8f));
            g.fillRect (area.reduced (9.0f, 0.0f).removeFromBottom (4.0f).removeFromTop (2.0f)
                            .withWidth ((area.getWidth() - 18.0f) * juce::jlimit (0.0f, 1.0f, binding.getNormalised())));
        }

        if (editor == nullptr)
        {
            g.setFont (theme::mono (13.0f));
            g.setColour (active ? theme::text : theme::textFaint);
            g.drawText (overrideText.isNotEmpty() ? overrideText : (binding.isAttached() ? binding.getText() : juce::String ("-")),
                        line, juce::Justification::centredRight, false);
        }

        return;
    }

    auto inner = area.reduced (9.0f, 6.0f);

    g.setFont (theme::ui (10.0f, true));
    g.setColour (theme::textDim.withAlpha (active ? 1.0f : 0.5f));
    g.drawText (label, inner.removeFromTop (12.0f), juce::Justification::centredLeft, false);

    // Position track along the bottom edge.
    auto track = inner.removeFromBottom (2.0f);
    g.setColour (theme::grid);
    g.fillRect (track);

    if (active)
    {
        g.setColour (accent.withAlpha (dragging ? 1.0f : 0.8f));
        g.fillRect (track.withWidth (track.getWidth() * juce::jlimit (0.0f, 1.0f, binding.getNormalised())));
    }

    if (editor == nullptr)
    {
        g.setFont (theme::mono (getWidth() < 92 ? 13.0f : 15.0f));
        g.setColour (active ? theme::text : theme::textFaint);
        g.drawText (binding.isAttached() ? binding.getText() : juce::String ("-"),
                    inner, juce::Justification::centredLeft, false);
    }
}

void ParamField::mouseDown (const juce::MouseEvent& e)
{
    if (! binding.isAttached())
        return;

    if (e.mods.isCommandDown())
    {
        if (auto* p = binding.getParam())
            binding.set (p->convertFrom0to1 (p->getDefaultValue()));

        return;
    }

    dragging = true;
    dragNorm = binding.getNormalised();
    lastDragPos = e.position;
    binding.beginGesture();
    repaint();
}

void ParamField::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    const auto delta = e.position - lastDragPos;
    lastDragPos = e.position;

    const float speed = e.mods.isShiftDown() ? 0.1f : 1.0f;
    dragNorm = juce::jlimit (0.0f, 1.0f, dragNorm + speed * (delta.x - delta.y) / 220.0f);

    if (auto* p = binding.getParam())
        binding.setInGesture (p->convertFrom0to1 (dragNorm));
}

void ParamField::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
    {
        dragging = false;
        binding.endGesture();
        repaint();
    }
}

void ParamField::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (binding.isAttached() && ! e.mods.isCommandDown())
        showEditor();
}

void ParamField::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    auto* p = binding.getParam();

    if (p == nullptr || juce::approximatelyEqual (wheel.deltaY, 0.0f))
        return;

    const int steps = p->getNumSteps();
    const bool discrete = steps > 1 && steps < 64;
    const float direction = wheel.deltaY > 0.0f ? 1.0f : -1.0f;

    const float step = discrete ? direction / (float) (steps - 1)
                                : wheel.deltaY * (e.mods.isShiftDown() ? 0.02f : 0.15f);

    binding.set (p->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, binding.getNormalised() + step)));
}

void ParamField::showEditor()
{
    if (dragging)
    {
        dragging = false;
        binding.endGesture();
    }

    editor = std::make_unique<juce::TextEditor>();
    editor->setFont (theme::mono (15.0f));
    editor->setJustification (juce::Justification::centredLeft);
    editor->setBorder ({});
    editor->setIndents (0, 0);
    editor->setText (binding.getText(), false);
    editor->setSelectAllWhenFocused (true);

    auto bounds = getLocalBounds().reduced (9, 6);

    if (compact)
    {
        bounds = getLocalBounds().reduced (9, 3);
        bounds.removeFromLeft (bounds.getWidth() / 2);
        editor->setFont (theme::mono (13.0f));
    }
    else
    {
        bounds.removeFromTop (12);
        bounds.removeFromBottom (3);
    }

    editor->setBounds (bounds);

    addAndMakeVisible (*editor);
    editor->grabKeyboardFocus();

    juce::Component::SafePointer<ParamField> safe (this);
    const auto finish = [safe] (bool commit)
    {
        // The editor must not be destroyed from inside its own callback.
        juce::MessageManager::callAsync ([safe, commit]
        {
            if (safe != nullptr)
                safe->hideEditor (commit);
        });
    };

    editor->onReturnKey = [finish] { finish (true); };
    editor->onEscapeKey = [finish] { finish (false); };
    editor->onFocusLost = [finish] { finish (true); };

    repaint();
}

void ParamField::hideEditor (bool commit)
{
    if (editor == nullptr)
        return;

    const auto typed = editor->getText();
    editor.reset();

    if (commit && typed.containsAnyOf ("0123456789"))
        if (auto* p = binding.getParam())
            binding.set (p->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, p->getValueForText (typed))));

    repaint();
}

//==============================================================================
SegmentedSelector::SegmentedSelector (std::vector<Segment> segmentsToUse)
    : segments (std::move (segmentsToUse))
{
    binding.onChange = [this]
    {
        if (binding.isAttached())
        {
            selected = juce::roundToInt (binding.get());
            repaint();
        }
    };

    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void SegmentedSelector::setParameter (juce::RangedAudioParameter* param)
{
    binding.attach (param);
}

void SegmentedSelector::setSelected (int index)
{
    if (selected != index)
    {
        selected = index;
        repaint();
    }
}

void SegmentedSelector::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();

    g.setColour (theme::panelRaised);
    g.fillRoundedRectangle (area, 4.0f);

    const float cell = area.getWidth() / (float) segments.size();

    for (size_t i = 0; i < segments.size(); ++i)
    {
        const auto r = juce::Rectangle<float> (area.getX() + cell * (float) i, area.getY(), cell, area.getHeight());
        const bool on = (int) i == selected;

        if (on)
        {
            g.setColour (segments[i].colour);
            g.fillRoundedRectangle (r.reduced (2.0f), 3.0f);
        }

        g.setFont (theme::ui (11.0f, true));
        g.setColour (on ? theme::background : theme::textDim);
        g.drawText (segments[i].text, r, juce::Justification::centred, false);
    }
}

void SegmentedSelector::mouseDown (const juce::MouseEvent& e)
{
    const int count = (int) segments.size();
    const int index = juce::jlimit (0, count - 1, (int) ((float) e.x / (float) juce::jmax (1, getWidth()) * (float) count));

    if (binding.isAttached())
        binding.set ((float) index);
    else
        setSelected (index);

    if (onSelect != nullptr)
        onSelect (index);
}

//==============================================================================
TypeSelector::TypeSelector()
{
    binding.onChange = [this] { repaint(); };
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void TypeSelector::setParameter (juce::RangedAudioParameter* param)
{
    binding.attach (param);
}

int TypeSelector::cellAt (juce::Point<int> p) const
{
    if (! getLocalBounds().contains (p))
        return -1;

    return juce::jlimit (0, kNumFilterTypes - 1, p.x * kNumFilterTypes / juce::jmax (1, getWidth()));
}

void TypeSelector::drawGlyph (juce::Graphics& g, FilterType type, juce::Rectangle<float> area,
                              juce::Colour colour, float thickness)
{
    // Each shape is a function y(x) on the unit square, 1 = top.
    const auto shape = [type] (float x) -> float
    {
        const auto bump = [] (float t, float width) { return std::exp (-(t * t) / (width * width)); };

        switch (type)
        {
            case FilterType::Bell:      return 0.3f + 0.7f * bump (x - 0.5f, 0.17f);
            case FilterType::LowShelf:  return 0.5f - 0.38f * std::tanh ((x - 0.5f) * 7.0f);
            case FilterType::HighShelf: return 0.5f + 0.38f * std::tanh ((x - 0.5f) * 7.0f);
            case FilterType::LowCut:    return 0.9f - 0.9f * std::pow (juce::jlimit (0.0f, 1.0f, (0.55f - x) / 0.5f), 1.7f);
            case FilterType::HighCut:   return 0.9f - 0.9f * std::pow (juce::jlimit (0.0f, 1.0f, (x - 0.45f) / 0.5f), 1.7f);
            case FilterType::Notch:     return 0.9f - 0.9f * bump (x - 0.5f, 0.09f);
            case FilterType::BandPass:  return 0.95f - 0.95f * std::pow (juce::jlimit (0.0f, 1.0f, std::abs (x - 0.5f) / 0.45f), 1.5f);
            case FilterType::TiltShelf: return 0.5f + 0.36f * std::tanh ((x - 0.5f) * 2.6f) / std::tanh (1.3f);
            case FilterType::AllPass:   return 0.5f + 0.32f * std::sin (x * juce::MathConstants<float>::twoPi);
            case FilterType::FlatTilt:  return 0.14f + 0.72f * x;
        }

        return 0.5f;
    };

    juce::Path path;
    const int steps = 28;

    for (int i = 0; i <= steps; ++i)
    {
        const float x = (float) i / (float) steps;
        const juce::Point<float> p (area.getX() + x * area.getWidth(),
                                    area.getBottom() - shape (x) * area.getHeight());

        if (i == 0) path.startNewSubPath (p);
        else        path.lineTo (p);
    }

    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void TypeSelector::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();

    g.setColour (theme::panelRaised);
    g.fillRoundedRectangle (area, 4.0f);

    const float cell = area.getWidth() / (float) kNumFilterTypes;
    const int current = binding.isAttached() ? juce::roundToInt (binding.get()) : -1;

    for (int i = 0; i < kNumFilterTypes; ++i)
    {
        const auto r = juce::Rectangle<float> (area.getX() + cell * (float) i, area.getY(), cell, area.getHeight());
        const bool on = i == current;

        if (on)
        {
            g.setColour (accent);
            g.fillRoundedRectangle (r.reduced (2.0f), 3.0f);
        }
        else if (i == hoverCell && binding.isAttached())
        {
            g.setColour (theme::text.withAlpha (0.06f));
            g.fillRoundedRectangle (r.reduced (2.0f), 3.0f);
        }

        const auto colour = on ? theme::background
                               : (binding.isAttached() ? theme::textDim : theme::textFaint);

        drawGlyph (g, (FilterType) i, r.reduced (7.0f, 9.0f), colour, on ? 1.8f : 1.4f);
    }
}

void TypeSelector::mouseDown (const juce::MouseEvent& e)
{
    const int cell = cellAt (e.getPosition());

    if (cell >= 0 && binding.isAttached())
        binding.set ((float) cell);
}

void TypeSelector::mouseMove (const juce::MouseEvent& e)
{
    const int cell = cellAt (e.getPosition());

    if (cell != hoverCell)
    {
        hoverCell = cell;

        static const char* names[kNumFilterTypes] = { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut",
                                                      "Notch", "Band Pass", "Tilt Shelf", "All Pass",
                                                      "Flat Tilt: a straight slope across the whole spectrum" };
        setTooltip (cell >= 0 ? names[cell] : "");
        repaint();
    }
}

void TypeSelector::mouseExit (const juce::MouseEvent&)
{
    hoverCell = -1;
    repaint();
}

//==============================================================================
IconButton::IconButton (Icon iconToUse) : icon (iconToUse)
{
    binding.onChange = [this] { repaint(); };
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void IconButton::setParameter (juce::RangedAudioParameter* param)
{
    binding.attach (param);
}

void IconButton::setActive (bool shouldBeActive)
{
    if (active != shouldBeActive)
    {
        active = shouldBeActive;
        repaint();
    }
}

void IconButton::setAvailable (bool shouldBeAvailable)
{
    if (available != shouldBeAvailable)
    {
        available = shouldBeAvailable;
        setMouseCursor (available ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void IconButton::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const auto centre = area.getCentre();
    const float radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
    const auto stroke = juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    const bool lit = active || (icon == Icon::Phase && binding.isAttached() && binding.get() > 0.5f);

    if (lit)
    {
        g.setColour (accent);
        g.fillEllipse (area.withSizeKeepingCentre (radius * 2.0f, radius * 2.0f));
    }
    else if (hover && available)
    {
        g.setColour (theme::text.withAlpha (0.07f));
        g.fillEllipse (area.withSizeKeepingCentre (radius * 2.0f, radius * 2.0f));
    }

    switch (icon)
    {
        case Icon::Power:
        {
            const bool on = ! binding.isAttached() || binding.get() > 0.5f;
            const float r = radius * 0.48f;

            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, r, r, 0.0f, 0.65f, juce::MathConstants<float>::twoPi - 0.65f, true);
            arc.startNewSubPath (centre.x, centre.y - r * 1.25f);
            arc.lineTo (centre.x, centre.y - r * 0.15f);

            g.setColour (on ? accent : theme::textFaint);
            g.strokePath (arc, stroke);
            break;
        }

        case Icon::Remove:
        {
            const float r = radius * 0.34f;

            juce::Path cross;
            cross.startNewSubPath (centre.x - r, centre.y - r);
            cross.lineTo (centre.x + r, centre.y + r);
            cross.startNewSubPath (centre.x + r, centre.y - r);
            cross.lineTo (centre.x - r, centre.y + r);

            g.setColour (hover ? theme::warn : theme::textDim);
            g.strokePath (cross, stroke);
            break;
        }

        case Icon::Solo:
        {
            // Headphones: a band over the top and two ear cups.
            const float r = radius * 0.5f;
            const float cy = centre.y + r * 0.15f;

            juce::Path band;
            band.addCentredArc (centre.x, cy, r, r, 0.0f, -juce::MathConstants<float>::halfPi,
                                juce::MathConstants<float>::halfPi, true);

            const auto colour = active ? theme::background : (hover ? theme::text : theme::textDim);
            g.setColour (colour);
            g.strokePath (band, stroke);

            const float cupW = r * 0.55f, cupH = r * 0.95f;
            g.fillRoundedRectangle (centre.x - r - cupW * 0.35f, cy - cupH * 0.1f, cupW, cupH, 1.4f);
            g.fillRoundedRectangle (centre.x + r - cupW * 0.65f, cy - cupH * 0.1f, cupW, cupH, 1.4f);
            break;
        }

        case Icon::Undo:
        case Icon::Redo:
        {
            // An arrow that comes up from below and turns back.
            const float r = radius * 0.42f;
            const float dir = icon == Icon::Undo ? 1.0f : -1.0f;
            const juce::Point<float> tip (centre.x - dir * r * 0.8f, centre.y - r * 0.25f);

            juce::Path arrow;
            arrow.startNewSubPath (centre.x + dir * r, centre.y + r * 0.8f);
            arrow.quadraticTo (centre.x + dir * r, tip.y, tip.x, tip.y);
            arrow.startNewSubPath (tip.x + dir * r * 0.55f, tip.y - r * 0.55f);
            arrow.lineTo (tip);
            arrow.lineTo (tip.x + dir * r * 0.55f, tip.y + r * 0.55f);

            g.setColour (! available ? theme::textFaint : (hover ? theme::text : theme::textDim));
            g.strokePath (arrow, stroke);
            break;
        }

        case Icon::Previous:
        case Icon::Next:
        {
            const float r = radius * 0.34f;
            const float dir = icon == Icon::Next ? 1.0f : -1.0f;

            juce::Path chevron;
            chevron.startNewSubPath (centre.x - dir * r * 0.5f, centre.y - r);
            chevron.lineTo (centre.x + dir * r * 0.5f, centre.y);
            chevron.lineTo (centre.x - dir * r * 0.5f, centre.y + r);

            g.setColour (! available ? theme::textFaint : (hover ? theme::text : theme::textDim));
            g.strokePath (chevron, stroke);
            break;
        }

        case Icon::Phase:
        {
            // The polarity sign: a circle with a stroke through it.
            const float r = radius * 0.42f;

            juce::Path sign;
            sign.addEllipse (centre.x - r, centre.y - r, r * 2.0f, r * 2.0f);
            sign.startNewSubPath (centre.x - r * 0.95f, centre.y + r * 1.35f);
            sign.lineTo (centre.x + r * 0.95f, centre.y - r * 1.35f);

            g.setColour (lit ? theme::background : (hover ? theme::text : theme::textDim));
            g.strokePath (sign, stroke);
            break;
        }
    }
}

void IconButton::mouseDown (const juce::MouseEvent&)
{
    if (! available)
        return;

    if ((icon == Icon::Power || icon == Icon::Phase) && binding.isAttached())
        binding.set (binding.get() > 0.5f ? 0.0f : 1.0f);

    if (onClick != nullptr)
        onClick();
}

//==============================================================================
LabelButton::LabelButton (juce::String textToShow) : text (std::move (textToShow))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void LabelButton::setText (const juce::String& newText)
{
    if (text != newText)
    {
        text = newText;
        repaint();
    }
}

void LabelButton::setActive (bool shouldBeLit)
{
    if (active != shouldBeLit)
    {
        active = shouldBeLit;
        repaint();
    }
}

void LabelButton::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();

    g.setColour (active ? accent : (hover ? theme::panelRaised.brighter (0.08f) : theme::panelRaised));
    g.fillRoundedRectangle (area, 4.0f);

    const auto colour = active ? theme::background : (hover ? theme::text : theme::textDim);
    g.setColour (colour);
    g.setFont (theme::ui (11.0f, true));

    if (menuArrow)
    {
        auto arrowArea = area.removeFromRight (20.0f);
        const auto c = arrowArea.getCentre();

        juce::Path arrow;
        arrow.startNewSubPath (c.x - 3.5f, c.y - 1.5f);
        arrow.lineTo (c.x, c.y + 2.0f);
        arrow.lineTo (c.x + 3.5f, c.y - 1.5f);
        g.strokePath (arrow, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.drawText (text, area.withTrimmedLeft (9.0f), juce::Justification::centredLeft, false);
    }
    else
    {
        g.drawText (text, area, juce::Justification::centred, false);
    }
}

void LabelButton::mouseDown (const juce::MouseEvent&)
{
    if (onClick != nullptr)
        onClick();
}

//==============================================================================
ToggleReadout::ToggleReadout (juce::String labelText) : label (std::move (labelText))
{
    binding.onChange = [this] { repaint(); };
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ToggleReadout::setParameter (juce::RangedAudioParameter* param)
{
    binding.attach (param);
}

void ToggleReadout::setReadout (const juce::String& textWhileOn)
{
    if (readout != textWhileOn)
    {
        readout = textWhileOn;
        repaint();
    }
}

void ToggleReadout::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const bool on = isOn();

    g.setColour (hover ? theme::panelRaised.brighter (0.06f) : theme::panelRaised);
    g.fillRoundedRectangle (area, 4.0f);

    auto inner = area.reduced (9.0f, 6.0f);

    g.setFont (theme::ui (10.0f, true));
    g.setColour (theme::textDim);
    g.drawText (label, inner.removeFromTop (12.0f), juce::Justification::centredLeft, false);

    auto track = inner.removeFromBottom (2.0f);
    g.setColour (on ? theme::accent : theme::grid);
    g.fillRect (track);

    g.setFont (theme::mono (getWidth() < 92 ? 13.0f : 15.0f));
    g.setColour (on ? theme::accent : theme::textFaint);
    g.drawText (on ? readout : juce::String ("off"), inner, juce::Justification::centredLeft, false);
}

void ToggleReadout::mouseDown (const juce::MouseEvent&)
{
    if (binding.isAttached())
        binding.set (isOn() ? 0.0f : 1.0f);
}

} // namespace hdp::ui
