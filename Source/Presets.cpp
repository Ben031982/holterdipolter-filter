#include "Presets.h"
#include "PluginProcessor.h"

using namespace hdp;

namespace
{
    const juce::Identifier presetFileType ("HolterdipolterFilterPreset");
    const juce::Identifier valueType ("P");
    const juce::Identifier idProp ("id"), valueProp ("v"), nameProp ("name");

    const juce::Identifier presetNameProp ("presetName"), slotProp ("abSlot"), factoryProp ("presetFactory");
    const juce::Identifier referenceType ("PresetReference"), waitingType ("ABWaiting");
    const juce::Identifier waitingValuesType ("Values"), waitingReferenceType ("Reference");

    bool sameSetting (const std::vector<float>& a, const std::vector<float>& b)
    {
        if (a.size() != b.size())
            return false;

        for (size_t i = 0; i < a.size(); ++i)
            if (std::abs (a[i] - b[i]) > 1.0e-6f)
                return false;

        return true;
    }

    juce::RangedAudioParameter* ranged (juce::AudioProcessorParameter* p)
    {
        return dynamic_cast<juce::RangedAudioParameter*> (p);
    }

    //==========================================================================
    // Factory presets. Starting points, written for techno and its harder kin.
    // Names are in German, as their user is; the bytes are UTF-8 written out, so
    // that no compiler has to guess the encoding of this file.

    constexpr float staticBand = 0.0f;      // no dynamic range
    constexpr int cut12 = 1, cut24 = 3;     // slope indices

    struct FactoryBand
    {
        FilterType type;
        float freq, gainDb, q;
        int slopeIndex = 1;
        Placement placement = Placement::Stereo;
        float dynRangeDb = staticBand;       // dynamic bands use the auto threshold
    };

    struct FactoryPreset
    {
        const char* group;
        const char* name;
        std::vector<FactoryBand> bands;
    };

    const std::vector<FactoryPreset>& factoryPresets()
    {
        using T = FilterType;
        using P = Placement;

        static const char* const tidy   = "Aufr\xc3\xa4umen";
        static const char* const low    = "Kick und Bass";
        static const char* const high   = "H\xc3\xb6hen";
        static const char* const whole  = "Summe und Effekt";
        static const char* const schranz = "Schranz";

        static const std::vector<FactoryPreset> presets = {
            { tidy, "Low Cut 30 Hz",            { { T::LowCut, 30.0f, 0.0f, 1.0f, cut24 } } },
            { tidy, "Low Cut 120 Hz",           { { T::LowCut, 120.0f, 0.0f, 1.0f, cut24 } } },
            { tidy, "Bass mono",                { { T::LowCut, 120.0f, 0.0f, 1.0f, cut24, P::Side } } },
            { tidy, "Matsch raus",              { { T::Bell, 250.0f, 0.0f, 1.2f, 1, P::Stereo, -5.0f } } },

            { low, "Kick Punch",                { { T::LowCut, 30.0f, 0.0f, 1.0f, cut24 },
                                                  { T::Bell, 60.0f, 3.0f, 1.4f },
                                                  { T::Bell, 300.0f, -3.5f, 1.5f },
                                                  { T::Bell, 4000.0f, 2.5f, 1.2f } } },
            { low, "Rumble z\xc3\xa4hmen",      { { T::LowCut, 28.0f, 0.0f, 1.0f, cut24 },
                                                  { T::Bell, 70.0f, 0.0f, 1.0f, 1, P::Stereo, -6.0f },
                                                  { T::Bell, 250.0f, -3.0f, 1.3f } } },
            { low, "Bass: Platz f\xc3\xbcr Kick", { { T::LowCut, 35.0f, 0.0f, 1.0f, cut24 },
                                                  { T::Bell, 60.0f, -4.0f, 2.5f } } },

            { high, "Sch\xc3\xa4rfe z\xc3\xa4hmen", { { T::Bell, 2500.0f, 0.0f, 2.5f, 1, P::Stereo, -5.0f },
                                                  { T::Bell, 4500.0f, 0.0f, 2.5f, 1, P::Stereo, -5.0f } } },
            { high, "Hi-Hat Glanz",             { { T::LowCut, 300.0f, 0.0f, 1.0f, cut24 },
                                                  { T::Bell, 4000.0f, 0.0f, 2.0f, 1, P::Stereo, -5.0f },
                                                  { T::HighShelf, 8000.0f, 3.0f, 1.0f } } },
            { high, "Air",                      { { T::HighShelf, 12000.0f, 3.0f, 0.7f } } },

            { whole, "Heller (Tilt)",           { { T::FlatTilt, 1000.0f, 2.0f, 1.0f } } },
            { whole, "Dunkler (Tilt)",          { { T::FlatTilt, 1000.0f, -2.0f, 1.0f } } },
            { whole, "Breite oben",             { { T::HighShelf, 6000.0f, 3.0f, 1.0f, 1, P::Side } } },
            { whole, "Telefon",                 { { T::LowCut, 400.0f, 0.0f, 1.0f, cut24 },
                                                  { T::HighCut, 3000.0f, 0.0f, 1.0f, cut24 } } },

            { schranz, "Schranz Kick",          { { T::LowCut, 35.0f, 0.0f, 1.0f, cut24 },
                                                  { T::Bell, 55.0f, 3.0f, 1.4f },
                                                  { T::Bell, 220.0f, 0.0f, 1.5f, 1, P::Stereo, -5.0f },
                                                  { T::Bell, 3000.0f, 3.0f, 1.2f } } },
            { schranz, "Schranz Loop",          { { T::LowCut, 200.0f, 0.0f, 1.0f, cut24 },
                                                  { T::Bell, 3500.0f, 0.0f, 2.0f, 1, P::Stereo, -6.0f },
                                                  { T::HighShelf, 9000.0f, 2.0f, 1.0f } } },
            { schranz, "Verzerrung z\xc3\xa4hmen", { { T::LowCut, 40.0f, 0.0f, 1.0f, cut24 },
                                                  { T::Bell, 2800.0f, 0.0f, 2.0f, 1, P::Stereo, -6.0f },
                                                  { T::Bell, 6000.0f, 0.0f, 2.0f, 1, P::Stereo, -6.0f },
                                                  { T::HighCut, 16000.0f, 0.0f, 1.0f, cut12 } } },
            { schranz, "Rumble unter der Kick", { { T::LowCut, 30.0f, 0.0f, 1.0f, cut24 },
                                                  { T::Bell, 55.0f, 0.0f, 2.0f, 1, P::Stereo, -6.0f },
                                                  { T::HighCut, 180.0f, 0.0f, 1.0f, cut24 } } },
        };

        return presets;
    }

    juce::String utf8 (const char* text)   { return juce::String::fromUTF8 (text); }
}

PresetManager::PresetManager (HolterdipolterFilterProcessor& p)
    : processor (p),
      directory (juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                     .getChildFile ("holterdipolter").getChildFile ("Filter").getChildFile ("Presets"))
{
}

//==============================================================================
PresetManager::Setting PresetManager::defaults() const
{
    const auto& params = processor.getParameters();
    Setting s ((size_t) params.size());

    for (int i = 0; i < params.size(); ++i)
        s[(size_t) i] = params.getUnchecked (i)->getDefaultValue();

    return s;
}

PresetManager::Setting PresetManager::capture() const
{
    const auto& params = processor.getParameters();
    Setting s ((size_t) params.size());

    for (int i = 0; i < params.size(); ++i)
        s[(size_t) i] = params.getUnchecked (i)->getValue();

    // What a band that is not in use still remembers is not part of the setting.
    for (int band = 0; band < kMaxBands; ++band)
    {
        if (processor.isBandUsed (band))
            continue;

        for (int field = 0; field < params::numBandFields; ++field)
        {
            auto* p = processor.bandParam (band, (params::BandField) field);
            s[(size_t) p->getParameterIndex()] = p->getDefaultValue();
        }
    }

    return s;
}

void PresetManager::apply (const Setting& setting)
{
    const auto& params = processor.getParameters();

    for (int i = 0; i < params.size() && (size_t) i < setting.size(); ++i)
    {
        auto* p = params.getUnchecked (i);
        const float value = setting[(size_t) i];

        if (! juce::exactlyEqual (p->getValue(), value))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (value);
            p->endChangeGesture();
        }
    }
}

juce::ValueTree PresetManager::toTree (const juce::Identifier& type, const Setting& setting) const
{
    juce::ValueTree tree (type);
    const auto& params = processor.getParameters();

    // Only what differs from the default is written; whatever is missing is the default.
    for (int i = 0; i < params.size() && (size_t) i < setting.size(); ++i)
    {
        auto* p = ranged (params.getUnchecked (i));

        if (p == nullptr || std::abs (setting[(size_t) i] - p->getDefaultValue()) <= 1.0e-6f)
            continue;

        juce::ValueTree entry (valueType);
        entry.setProperty (idProp, p->getParameterID(), nullptr);
        entry.setProperty (valueProp, (double) p->convertFrom0to1 (setting[(size_t) i]), nullptr);
        tree.appendChild (entry, nullptr);
    }

    return tree;
}

PresetManager::Setting PresetManager::fromTree (const juce::ValueTree& tree) const
{
    auto setting = defaults();
    const auto& params = processor.getParameters();

    for (int i = 0; i < params.size(); ++i)
    {
        auto* p = ranged (params.getUnchecked (i));

        if (p == nullptr)
            continue;

        const auto entry = tree.getChildWithProperty (idProp, p->getParameterID());

        if (entry.isValid() && entry.hasProperty (valueProp))
        {
            const auto& range = p->getNormalisableRange();
            const float value = juce::jlimit (range.start, range.end, (float) (double) entry.getProperty (valueProp));
            setting[(size_t) i] = p->convertTo0to1 (value);
        }
    }

    return setting;
}

//==============================================================================
juce::File PresetManager::getDirectory() const
{
    const juce::ScopedLock sl (lock);
    return directory;
}

void PresetManager::setDirectory (const juce::File& newDirectory)
{
    const juce::ScopedLock sl (lock);
    directory = newDirectory;
}

juce::String PresetManager::cleanName (const juce::String& name)
{
    return juce::File::createLegalFileName (name.trim()).substring (0, 60).trim();
}

juce::String PresetManager::cleanPath (const juce::String& name)
{
    juce::StringArray parts;

    for (const auto& part : juce::StringArray::fromTokens (name.replaceCharacter ('\\', '/'), "/", ""))
        if (const auto clean = cleanName (part); clean.isNotEmpty() && clean != "." && clean != "..")
            parts.add (clean);

    // Folders are for sorting, not for building trees: three levels are plenty.
    while (parts.size() > 3)
        parts.remove (0);

    return parts.joinIntoString ("/");
}

juce::File PresetManager::fileFor (const juce::String& name) const
{
    auto file = getDirectory();
    const auto parts = juce::StringArray::fromTokens (cleanPath (name), "/", "");

    for (int i = 0; i < parts.size(); ++i)
        file = file.getChildFile (i == parts.size() - 1 ? parts[i] + fileExtension : parts[i]);

    return file;
}

juce::StringArray PresetManager::getPresetNames() const
{
    juce::StringArray names;
    const auto root = getDirectory();

    for (const auto& file : root.findChildFiles (juce::File::findFiles, true, juce::String ("*") + fileExtension))
        names.add (file.getRelativePathFrom (root).replaceCharacter ('\\', '/').upToLastOccurrenceOf (fileExtension, false, true));

    names.sortNatural();
    return names;
}

juce::String PresetManager::getCurrentName() const
{
    const juce::ScopedLock sl (lock);
    return live.presetName;
}

bool PresetManager::isCurrentFactory() const
{
    const juce::ScopedLock sl (lock);
    return live.factory && live.presetName.isNotEmpty();
}

juce::StringArray PresetManager::getFactoryGroups()
{
    juce::StringArray groups;

    for (const auto& preset : factoryPresets())
        groups.addIfNotAlreadyThere (utf8 (preset.group));

    return groups;
}

juce::StringArray PresetManager::getFactoryNames (const juce::String& group)
{
    juce::StringArray names;

    for (const auto& preset : factoryPresets())
        if (group.isEmpty() || utf8 (preset.group) == group)
            names.add (utf8 (preset.name));

    return names;
}

bool PresetManager::loadFactory (const juce::String& name)
{
    for (const auto& preset : factoryPresets())
    {
        if (utf8 (preset.name) != name)
            continue;

        auto setting = defaults();
        int band = 0;

        for (const auto& b : preset.bands)
        {
            const auto put = [this, &setting, band] (params::BandField field, float value)
            {
                auto* p = processor.bandParam (band, field);
                setting[(size_t) p->getParameterIndex()] = p->convertTo0to1 (value);
            };

            put (params::Used,  1.0f);
            put (params::On,    1.0f);
            put (params::Type,  (float) (int) b.type);
            put (params::Freq,  b.freq);
            put (params::Gain,  b.gainDb);
            put (params::Q,     b.q);
            put (params::Slope, (float) b.slopeIndex);
            put (params::Place, (float) (int) b.placement);
            put (params::Dyn,   b.dynRangeDb);
            ++band;
        }

        apply (setting);

        const juce::ScopedLock sl (lock);
        live.presetName = name;
        live.factory = true;
        live.reference = capture();
        return true;
    }

    return false;
}

bool PresetManager::isModified() const
{
    const auto now = capture();

    const juce::ScopedLock sl (lock);
    return ! sameSetting (now, live.reference.empty() ? defaults() : live.reference);
}

bool PresetManager::save (const juce::String& name)
{
    const auto clean = cleanPath (name);

    if (clean.isEmpty())
        return false;

    const auto setting = capture();

    auto tree = toTree (presetFileType, setting);
    tree.setProperty (nameProp, clean, nullptr);
    tree.setProperty ("version", 1, nullptr);

    const auto file = fileFor (clean);

    if (! file.getParentDirectory().createDirectory().wasOk())
        return false;

    const auto xml = tree.createXml();

    if (xml == nullptr || ! xml->writeTo (file))
        return false;

    const juce::ScopedLock sl (lock);
    live.presetName = clean;
    live.factory = false;
    live.reference = setting;
    return true;
}

bool PresetManager::load (const juce::String& name)
{
    const auto file = fileFor (name);
    const auto xml = juce::XmlDocument::parse (file);

    if (xml == nullptr || ! xml->hasTagName (presetFileType))
        return false;

    const auto setting = fromTree (juce::ValueTree::fromXml (*xml));
    apply (setting);

    const juce::ScopedLock sl (lock);
    live.presetName = cleanPath (name);
    live.factory = false;
    live.reference = capture();     // as the parameters took it, so it compares equal
    return true;
}

bool PresetManager::remove (const juce::String& name)
{
    const auto file = fileFor (name);

    if (! file.existsAsFile() || ! file.deleteFile())
        return false;

    const juce::ScopedLock sl (lock);

    // The setting stays as it is; it just no longer belongs to a preset.
    // A folder that has just lost its last preset goes too.
    if (const auto folder = file.getParentDirectory(); folder != getDirectory() && folder.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0)
        folder.deleteFile();

    const auto gone = cleanPath (name);

    if (! live.factory && live.presetName == gone)
        live.presetName.clear();

    if (! waiting.factory && waiting.presetName == gone)
        waiting.presetName.clear();

    return true;
}

void PresetManager::loadDefault()
{
    apply (defaults());

    const juce::ScopedLock sl (lock);
    live.presetName.clear();
    live.factory = false;
    live.reference.clear();
}

bool PresetManager::step (int delta)
{
    const auto factory = getFactoryNames ({});
    const auto own = getPresetNames();
    const int count = factory.size() + own.size();

    if (count == 0)
        return false;

    const auto name = getCurrentName();
    const int current = name.isEmpty() ? -1
                      : isCurrentFactory() ? factory.indexOf (name)
                                           : (own.indexOf (name) >= 0 ? factory.size() + own.indexOf (name) : -1);

    const int next = current < 0 ? (delta > 0 ? 0 : count - 1)
                                 : ((current + delta) % count + count) % count;

    return next < factory.size() ? loadFactory (factory[next]) : load (own[next - factory.size()]);
}

//==============================================================================
int PresetManager::getActiveSlot() const
{
    const juce::ScopedLock sl (lock);
    return activeSlot;
}

void PresetManager::switchSlot()
{
    Slot leaving;
    leaving.values = capture();

    Slot arriving;

    {
        const juce::ScopedLock sl (lock);
        leaving.presetName = live.presetName;
        leaving.factory = live.factory;
        leaving.reference = live.reference;

        // The first time, the other side starts out as a copy of this one.
        arriving = waiting.values.empty() ? leaving : waiting;
    }

    apply (arriving.values);

    const juce::ScopedLock sl (lock);
    live.presetName = arriving.presetName;
    live.factory = arriving.factory;
    live.reference = arriving.reference;
    live.values.clear();
    waiting = std::move (leaving);
    activeSlot = 1 - activeSlot;
}

void PresetManager::copyToOtherSlot()
{
    auto values = capture();

    const juce::ScopedLock sl (lock);
    waiting.values = std::move (values);
    waiting.presetName = live.presetName;
    waiting.factory = live.factory;
    waiting.reference = live.reference;
}

//==============================================================================
void PresetManager::writeTo (juce::ValueTree& state) const
{
    const juce::ScopedLock sl (lock);

    state.setProperty (presetNameProp, live.presetName, nullptr);
    state.setProperty (factoryProp, live.factory, nullptr);
    state.setProperty (slotProp, activeSlot, nullptr);

    if (! live.reference.empty())
        state.appendChild (toTree (referenceType, live.reference), nullptr);

    if (! waiting.values.empty())
    {
        juce::ValueTree other (waitingType);
        other.setProperty (presetNameProp, waiting.presetName, nullptr);
        other.setProperty (factoryProp, waiting.factory, nullptr);
        other.appendChild (toTree (waitingValuesType, waiting.values), nullptr);

        if (! waiting.reference.empty())
            other.appendChild (toTree (waitingReferenceType, waiting.reference), nullptr);

        state.appendChild (other, nullptr);
    }
}

void PresetManager::readFrom (juce::ValueTree& state)
{
    Slot newLive, newWaiting;

    newLive.presetName = state.getProperty (presetNameProp, juce::String()).toString();
    newLive.factory = (bool) state.getProperty (factoryProp, false);

    if (const auto reference = state.getChildWithName (referenceType); reference.isValid())
    {
        newLive.reference = fromTree (reference);
        state.removeChild (reference, nullptr);
    }

    if (const auto other = state.getChildWithName (waitingType); other.isValid())
    {
        newWaiting.presetName = other.getProperty (presetNameProp, juce::String()).toString();
        newWaiting.factory = (bool) other.getProperty (factoryProp, false);
        newWaiting.values = fromTree (other.getChildWithName (waitingValuesType));

        if (const auto reference = other.getChildWithName (waitingReferenceType); reference.isValid())
            newWaiting.reference = fromTree (reference);

        state.removeChild (other, nullptr);
    }

    const int slot = juce::jlimit (0, 1, (int) state.getProperty (slotProp, 0));
    state.removeProperty (presetNameProp, nullptr);
    state.removeProperty (factoryProp, nullptr);
    state.removeProperty (slotProp, nullptr);

    const juce::ScopedLock sl (lock);
    live = std::move (newLive);
    waiting = std::move (newWaiting);
    activeSlot = slot;
}
