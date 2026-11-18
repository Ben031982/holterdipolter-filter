#include "PresetBar.h"

namespace hdp::ui
{

PresetBar::PresetBar (HolterdipolterFilterProcessor& p) : processor (p)
{
    addAndMakeVisible (previous);
    addAndMakeVisible (next);

    previous.setTooltip ("Previous preset");
    next.setTooltip ("Next preset");
    previous.onClick = [this] { processor.presets.step (-1); refresh(); };
    next.onClick     = [this] { processor.presets.step (+1); refresh(); };

    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    refresh();
}

void PresetBar::refresh()
{
    const auto name = processor.presets.getCurrentName();
    const bool modified = processor.presets.isModified();

    if (name != shownName || modified != shownModified)
    {
        shownName = name;
        shownModified = modified;
        repaint();
    }
}

void PresetBar::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();

    g.setColour (hover && editor == nullptr ? theme::panelRaised.brighter (0.06f) : theme::panelRaised);
    g.fillRoundedRectangle (area, 4.0f);

    auto inner = area.reduced (9.0f, 6.0f).withTrimmedRight (46.0f);

    g.setFont (theme::ui (10.0f, true));

    if (editor != nullptr && replaceCandidate.isNotEmpty())
    {
        g.setColour (theme::warn);
        g.drawText ("EXISTS - ENTER AGAIN TO REPLACE", inner.removeFromTop (12.0f), juce::Justification::centredLeft, false);
    }
    else
    {
        g.setColour (theme::textDim);
        g.drawText (editor != nullptr ? "NAME, THEN ENTER" : "PRESET", inner.removeFromTop (12.0f),
                    juce::Justification::centredLeft, false);
    }

    inner.removeFromBottom (2.0f);

    if (editor == nullptr)
    {
        // An untouched default setting reads quieter than anything the user made.
        const bool untouched = shownName.isEmpty() && ! shownModified;

        g.setFont (theme::mono (15.0f));
        g.setColour (untouched ? theme::textDim : theme::text);
        g.drawText ((shownName.isEmpty() ? juce::String ("Default") : shownName) + (shownModified ? " *" : ""),
                    inner, juce::Justification::centredLeft, true);
    }
}

void PresetBar::resized()
{
    auto buttons = getLocalBounds().removeFromRight (50).withSizeKeepingCentre (46, 24);
    previous.setBounds (buttons.removeFromLeft (23));
    next.setBounds (buttons);

    if (editor != nullptr)
    {
        auto bounds = getLocalBounds().reduced (9, 6).withTrimmedRight (46);
        bounds.removeFromTop (12);
        bounds.removeFromBottom (2);
        editor->setBounds (bounds);
    }
}

void PresetBar::mouseDown (const juce::MouseEvent&)
{
    if (editor == nullptr)
        showMenu();
}

void PresetBar::showMenu()
{
    enum { defaultId = 1, saveId, saveAsId, deleteId, folderId, presetBase = 100, factoryBase = 5000 };

    auto& presets = processor.presets;
    const auto names = presets.getPresetNames();
    const auto current = presets.getCurrentName();
    const bool factoryIsCurrent = presets.isCurrentFactory();
    const auto factoryNames = PresetManager::getFactoryNames ({});

    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());

    menu.addItem (defaultId, "Default setting", true, current.isEmpty() && ! presets.isModified());
    menu.addSeparator();
    menu.addSectionHeader ("Factory");

    for (const auto& group : PresetManager::getFactoryGroups())
    {
        juce::PopupMenu sub;
        bool holdsCurrent = false;

        for (const auto& name : PresetManager::getFactoryNames (group))
        {
            const bool ticked = factoryIsCurrent && name == current;
            holdsCurrent = holdsCurrent || ticked;
            sub.addItem (factoryBase + factoryNames.indexOf (name), name, true, ticked);
        }

        menu.addSubMenu (group, sub, true, nullptr, holdsCurrent);
    }

    menu.addSeparator();
    menu.addSectionHeader ("Your presets");

    if (names.isEmpty())
        menu.addItem (-1, "(none saved yet)", false);

    // Presets in a folder go into a submenu of that folder's name.
    {
        juce::StringArray folders;

        for (const auto& name : names)
            if (name.containsChar ('/'))
                folders.addIfNotAlreadyThere (name.upToFirstOccurrenceOf ("/", false, false));

        for (const auto& folder : folders)
        {
            juce::PopupMenu sub;
            bool holdsCurrent = false;

            for (int i = 0; i < names.size(); ++i)
            {
                if (! names[i].startsWith (folder + "/"))
                    continue;

                const bool ticked = ! factoryIsCurrent && names[i] == current;
                holdsCurrent = holdsCurrent || ticked;
                sub.addItem (presetBase + i, names[i].fromFirstOccurrenceOf ("/", false, false), true, ticked);
            }

            menu.addSubMenu (folder, sub, true, nullptr, holdsCurrent);
        }

        for (int i = 0; i < names.size(); ++i)
            if (! names[i].containsChar ('/'))
                menu.addItem (presetBase + i, names[i], true, ! factoryIsCurrent && names[i] == current);
    }

    // A factory preset is part of the plugin: it can be saved under a name of your own, not over.
    const bool ownIsCurrent = current.isNotEmpty() && ! factoryIsCurrent;

    menu.addSeparator();
    menu.addItem (saveId, ownIsCurrent ? "Save \"" + current + "\"" : juce::String ("Save"), ownIsCurrent);
    menu.addItem (saveAsId, "Save as ...   (\"Folder/Name\" sorts it into a folder)");
    menu.addItem (deleteId, ownIsCurrent ? "Delete \"" + current + "\"" : juce::String ("Delete"), ownIsCurrent);
    menu.addSeparator();
    menu.addItem (folderId, "Show preset folder");

    juce::Component::SafePointer<PresetBar> safe (this);

    menu.showMenuAsync (juce::PopupMenu::Options()
                            .withTargetComponent (this)
                            .withParentComponent (getTopLevelComponent()),
                        [safe, names, current, factoryNames] (int result)
    {
        if (safe == nullptr || result <= 0)
            return;

        auto& manager = safe->processor.presets;

        if (result == defaultId)        manager.loadDefault();
        else if (result == saveId)      manager.save (current);
        else if (result == saveAsId)    safe->showNameEditor();
        else if (result == deleteId)    manager.remove (current);
        else if (result == folderId)
        {
            const auto folder = manager.getDirectory();
            folder.createDirectory();
            folder.revealToUser();
        }
        else if (result >= factoryBase) manager.loadFactory (factoryNames[result - factoryBase]);
        else if (result >= presetBase)  manager.load (names[result - presetBase]);

        safe->refresh();
    });
}

void PresetBar::showNameEditor()
{
    replaceCandidate.clear();

    editor = std::make_unique<juce::TextEditor>();
    editor->setFont (theme::mono (15.0f));
    editor->setJustification (juce::Justification::centredLeft);
    editor->setBorder ({});
    editor->setIndents (0, 0);
    editor->setInputRestrictions (120);
    editor->setText (processor.presets.isCurrentFactory() ? juce::String() : processor.presets.getCurrentName(), false);
    editor->setTextToShowWhenEmpty ("Preset name", theme::textFaint);
    editor->setSelectAllWhenFocused (true);

    addAndMakeVisible (*editor);
    resized();
    editor->grabKeyboardFocus();

    juce::Component::SafePointer<PresetBar> safe (this);

    // The editor must not be destroyed from inside its own callback.
    const auto later = [safe] (std::function<void (PresetBar&)> action)
    {
        juce::MessageManager::callAsync ([safe, action]
        {
            if (safe != nullptr)
                action (*safe);
        });
    };

    editor->onReturnKey = [later]
    {
        later ([] (PresetBar& bar)
        {
            if (bar.editor != nullptr && bar.commitName (bar.editor->getText()))
                bar.hideNameEditor();
        });
    };

    editor->onEscapeKey  = [later] { later ([] (PresetBar& bar) { bar.hideNameEditor(); }); };
    editor->onFocusLost  = [later] { later ([] (PresetBar& bar) { bar.hideNameEditor(); }); };
    editor->onTextChange = [this]
    {
        if (replaceCandidate.isNotEmpty())
        {
            replaceCandidate.clear();
            repaint();
        }
    };

    repaint();
}

bool PresetBar::commitName (const juce::String& typed)
{
    auto& presets = processor.presets;
    auto name = PresetManager::cleanPath (typed);

    if (name.isEmpty())
        return true;    // nothing typed: just close

    // Saving over another preset has to be said twice. Upper and lower case do not
    // make a different preset: the one that is there keeps its spelling.
    const auto names = presets.getPresetNames();
    const int existing = names.indexOf (name, true);
    const bool exists = existing >= 0;

    if (exists)
        name = names[existing];

    const bool isCurrentOwn = ! presets.isCurrentFactory() && name.equalsIgnoreCase (presets.getCurrentName());

    if (exists && ! isCurrentOwn && ! name.equalsIgnoreCase (replaceCandidate))
    {
        replaceCandidate = name;
        repaint();
        return false;
    }

    presets.save (name);
    refresh();
    return true;
}

void PresetBar::hideNameEditor()
{
    editor.reset();
    replaceCandidate.clear();
    repaint();
}

} // namespace hdp::ui
