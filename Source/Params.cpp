#include "Params.h"

namespace hdp::params
{

juce::StringArray typeNames()
{
    return { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt Shelf", "All Pass",
             "Flat Tilt" };
}

juce::StringArray slopeNames()
{
    juce::StringArray names;

    for (int db : kSlopeDb)
        names.add (juce::String (db) + " dB/oct");

    return names;
}

juce::StringArray placementNames()
{
    return { "Stereo", "Left", "Right", "Mid", "Side" };
}

juce::String formatFrequency (float hz)
{
    if (hz < 100.0f)    return juce::String (hz, 1) + " Hz";
    if (hz < 1000.0f)   return juce::String (juce::roundToInt (hz)) + " Hz";
    if (hz < 10000.0f)  return juce::String (hz / 1000.0f, 2) + " kHz";
    return juce::String (hz / 1000.0f, 1) + " kHz";
}

juce::String formatGain (float db)
{
    if (std::abs (db) < 0.05f)
        return "0.0 dB";

    return (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB";
}

juce::String formatQ (float q)
{
    if (q < 1.0f)   return juce::String (q, 3);
    if (q < 10.0f)  return juce::String (q, 2);
    return juce::String (q, 1);
}

int midiNoteFor (double hz)
{
    return juce::roundToInt (69.0 + 12.0 * std::log2 (juce::jmax (1.0, hz) / 440.0));
}

double nearestNoteFrequency (double hz)
{
    return 440.0 * std::pow (2.0, (midiNoteFor (hz) - 69) / 12.0);
}

juce::String noteName (int midiNote)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int index = ((midiNote % 12) + 12) % 12;
    const int octave = (midiNote - index) / 12 - 1;
    return juce::String (names[index]) + juce::String (octave);
}

juce::String formatNote (double hz)
{
    const int note = midiNoteFor (hz);
    const int cents = juce::roundToInt (1200.0 * std::log2 (juce::jmax (1.0, hz) / nearestNoteFrequency (hz)));

    auto text = noteName (note);

    if (cents != 0)
        text << (cents > 0 ? " +" : " ") << cents << " ct";

    return text;
}

/** "A1", "c#3", "Bb2": a note name with octave. Returns 0 if the text is not one. */
static float parseNote (const juce::String& text)
{
    const auto t = text.trim().toUpperCase();

    if (t.length() < 2 || t[0] < 'A' || t[0] > 'G')
        return 0.0f;

    static const int semitone[7] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
    int note = semitone[t[0] - 'A'];
    int pos = 1;

    if (t[pos] == '#')                       { ++note; ++pos; }
    else if (t[pos] == 'B' && t.length() > 2) { --note; ++pos; }

    const auto octaveText = t.substring (pos);

    if (octaveText.isEmpty() || ! octaveText.containsOnly ("-0123456789"))
        return 0.0f;

    const int midi = note + 12 * (octaveText.getIntValue() + 1);
    return (float) (440.0 * std::pow (2.0, (midi - 69) / 12.0));
}

static float parseFrequency (const juce::String& text)
{
    if (const float fromNote = parseNote (text); fromNote > 0.0f)
        return fromNote;

    const auto t = text.trim().toLowerCase();
    const auto value = t.retainCharacters ("0123456789.,-").replaceCharacter (',', '.').getFloatValue();
    return t.containsChar ('k') ? value * 1000.0f : value;
}

static float parseNumber (const juce::String& text)
{
    return text.trim().retainCharacters ("0123456789.,-+").replaceCharacter (',', '.').getFloatValue();
}

static juce::NormalisableRange<float> logRange (float minValue, float maxValue)
{
    return { minValue, maxValue,
             [] (float start, float end, float norm)  { return start * std::pow (end / start, norm); },
             [] (float start, float end, float value) { return std::log (value / start) / std::log (end / start); },
             [] (float start, float end, float value) { return juce::jlimit (start, end, value); } };
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using Float  = juce::AudioParameterFloat;
    using Choice = juce::AudioParameterChoice;
    using Bool   = juce::AudioParameterBool;
    using FloatAttr = juce::AudioParameterFloatAttributes;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    const auto freqAttr = FloatAttr().withStringFromValueFunction ([] (float v, int) { return formatFrequency (v); })
                                     .withValueFromStringFunction ([] (const juce::String& t) { return parseFrequency (t); });
    const auto gainAttr = FloatAttr().withStringFromValueFunction ([] (float v, int) { return formatGain (v); })
                                     .withValueFromStringFunction ([] (const juce::String& t) { return parseNumber (t); });
    const auto qAttr    = FloatAttr().withStringFromValueFunction ([] (float v, int) { return formatQ (v); })
                                     .withValueFromStringFunction ([] (const juce::String& t) { return parseNumber (t); });
    const auto pctAttr  = FloatAttr().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; })
                                     .withValueFromStringFunction ([] (const juce::String& t) { return parseNumber (t); });

    const auto levelAttr = FloatAttr().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; })
                                      .withValueFromStringFunction ([] (const juce::String& t) { return parseNumber (t); });

    for (int band = 0; band < kMaxBands; ++band)
    {
        const auto name = "Band " + juce::String (band + 1) + " ";
        const auto id = [band] (BandField f) { return juce::ParameterID { bandId (band, f), 1 }; };

        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("band" + juce::String (band + 1),
                                                                           "Band " + juce::String (band + 1), "|");

        group->addChild (std::make_unique<Bool>   (id (Used),  name + "Used", false));
        group->addChild (std::make_unique<Bool>   (id (On),    name + "Enabled", true));
        group->addChild (std::make_unique<Float>  (id (Freq),  name + "Frequency",
                                                   logRange ((float) kMinFreq, (float) kMaxFreq), 1000.0f, freqAttr));
        group->addChild (std::make_unique<Float>  (id (Gain),  name + "Gain",
                                                   juce::NormalisableRange<float> ((float) -kMaxGainDb, (float) kMaxGainDb), 0.0f, gainAttr));
        group->addChild (std::make_unique<Float>  (id (Q),     name + "Q",
                                                   logRange ((float) kMinQ, (float) kMaxQ), 1.0f, qAttr));
        group->addChild (std::make_unique<Choice> (id (Type),  name + "Shape", typeNames(), 0));
        group->addChild (std::make_unique<Choice> (id (Slope), name + "Slope", slopeNames(), 1));
        group->addChild (std::make_unique<Choice> (id (Place), name + "Stereo Placement", placementNames(), 0));
        group->addChild (std::make_unique<Float>  (id (Dyn),   name + "Dynamic Range",
                                                   juce::NormalisableRange<float> ((float) -kMaxGainDb, (float) kMaxGainDb), 0.0f, gainAttr));
        group->addChild (std::make_unique<Float>  (id (Thresh), name + "Threshold",
                                                   juce::NormalisableRange<float> (-80.0f, 0.0f), -30.0f, levelAttr));
        group->addChild (std::make_unique<Bool>   (id (AutoThresh), name + "Auto Threshold", true));
        group->addChild (std::make_unique<Bool>   (id (DynSide), name + "Dynamics From Sidechain", false));

        layout.add (std::move (group));
    }

    layout.add (std::make_unique<Float> (juce::ParameterID { outGainId, 1 }, "Output Gain",
                                         juce::NormalisableRange<float> (-24.0f, 24.0f), 0.0f, gainAttr));
    layout.add (std::make_unique<Float> (juce::ParameterID { gainScaleId, 1 }, "Gain Scale",
                                         juce::NormalisableRange<float> (0.0f, 200.0f), 100.0f, pctAttr));

    layout.add (std::make_unique<Bool> (juce::ParameterID { autoGainId, 1 }, "Auto Gain", false));
    layout.add (std::make_unique<Bool> (juce::ParameterID { phaseInvertId, 1 }, "Phase Invert", false));

    return layout;
}

} // namespace hdp::params
