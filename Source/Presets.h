// holterdipolter Filter - presets and A/B
//
// A "setting" is the value of every parameter. Three things are built on it:
//
//   Presets   files in <Documents>/holterdipolter/Filter/Presets, one per preset,
//             plus the factory presets that are part of the plugin.
//   A/B       two settings side by side; one is live, the other waits.
//   Modified  whether the live setting still is what the preset held.
//
// Loading a preset or switching A/B changes parameters the same way a hand on
// a control would, so the host records it and undo takes it back.
//
// Message thread only, except writeTo() and readFrom(), which the host may call
// from any thread.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class HolterdipolterFilterProcessor;

class PresetManager final
{
public:
    explicit PresetManager (HolterdipolterFilterProcessor&);

    //==============================================================================
    static constexpr const char* fileExtension = ".hdpfilter";

    juce::File getDirectory() const;
    void setDirectory (const juce::File& newDirectory);     // for tests

    /** Names of all presets on disk, sorted the way a person would. A preset in a
        subfolder is named with its folder, as in "Kick/Punchy". */
    juce::StringArray getPresetNames() const;

    /** Name of the preset the live setting came from; empty for the default setting. */
    juce::String getCurrentName() const;

    /** True if that preset is one of the factory presets, which cannot be saved over or deleted. */
    bool isCurrentFactory() const;

    /** The factory presets: groups in menu order, and the names within one group. */
    static juce::StringArray getFactoryGroups();
    static juce::StringArray getFactoryNames (const juce::String& group);

    bool loadFactory (const juce::String& name);

    /** True if the live setting no longer is what the preset (or the default) holds. */
    bool isModified() const;

    bool save (const juce::String& name);
    bool load (const juce::String& name);
    bool remove (const juce::String& name);

    /** Back to the default setting: no bands, everything at its default. */
    void loadDefault();

    /** Loads the next (+1) or previous (-1) preset, wrapping around:
        first the factory presets in menu order, then the user's own. */
    bool step (int delta);

    /** What a name turns into as a file name; empty if nothing usable is left. */
    static juce::String cleanName (const juce::String& name);

    /** The same for a name that may carry folders ("Kick / Punchy" becomes "Kick/Punchy"). */
    static juce::String cleanPath (const juce::String& name);

    //==============================================================================
    int getActiveSlot() const;          // 0 = A, 1 = B

    /** Makes the waiting setting live and puts the live one in its place. */
    void switchSlot();

    /** Copies the live setting over the waiting one. */
    void copyToOtherSlot();

    //==============================================================================
    /** Adds what this class holds to a state that is about to be saved. */
    void writeTo (juce::ValueTree& state) const;

    /** Takes what this class holds out of a state that is being loaded. */
    void readFrom (juce::ValueTree& state);

private:
    using Setting = std::vector<float>;     // normalised values in parameter order; empty = default

    struct Slot
    {
        Setting values;         // unused for the live slot: its values are the parameters
        juce::String presetName;
        bool factory = false;   // presetName is a factory preset, not a file
        Setting reference;      // what presetName held when it was loaded or saved
    };

    Setting capture() const;
    Setting defaults() const;
    void apply (const Setting&);

    juce::ValueTree toTree (const juce::Identifier& type, const Setting&) const;
    Setting fromTree (const juce::ValueTree&) const;

    juce::File fileFor (const juce::String& name) const;

    HolterdipolterFilterProcessor& processor;

    juce::CriticalSection lock;
    juce::File directory;
    Slot live, waiting;
    int activeSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
};
