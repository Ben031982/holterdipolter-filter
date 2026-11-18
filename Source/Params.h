// holterdipolter Filter - parameter ids and layout

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/FilterDesign.h"

namespace hdp::params
{

enum BandField
{
    Used = 0,   // the band exists
    On,         // the band is audible (not bypassed)
    Freq,
    Gain,
    Q,
    Type,
    Slope,
    Place,
    Dyn,        // dynamic range in dB; 0 = an ordinary, static band
    Thresh,     // threshold of the dynamics in dB
    AutoThresh, // threshold follows the level of the input
    DynSide,    // the dynamics listen to the sidechain instead of this track
    numBandFields
};

inline constexpr const char* bandSuffix[numBandFields] = { "used", "on", "freq", "gain", "q", "type", "slope", "place",
                                                                   "dyn", "thr", "athr", "dsc" };

inline constexpr const char* outGainId   = "out_gain";
inline constexpr const char* gainScaleId = "gain_scale";
inline constexpr const char* autoGainId  = "auto_gain";
inline constexpr const char* phaseInvertId = "phase_invert";

inline juce::String bandId (int band, BandField field)
{
    return "b" + juce::String (band + 1) + "_" + bandSuffix[field];
}

juce::StringArray typeNames();
juce::StringArray slopeNames();
juce::StringArray placementNames();

juce::String formatFrequency (float hz);
juce::String formatGain (float db);
juce::String formatQ (float q);

/** Equal temperament, A4 = 440 Hz. */
double nearestNoteFrequency (double hz);
int midiNoteFor (double hz);
juce::String noteName (int midiNote);                 // "A1", "C#3"
juce::String formatNote (double hz);                  // "A1" or "A1 +23 ct"

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

} // namespace hdp::params
