// holterdipolter Filter - audio processor

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "EditHistory.h"
#include "Params.h"
#include "Presets.h"
#include "dsp/EqEngine.h"

//==============================================================================
/** Lock-free tap that the audio thread writes to and the analyzer reads from.
    The reader may see a block that is being written; for a display that is fine. */
class SpectrumTap
{
public:
    static constexpr int capacity = 1 << 15;

    void clear() noexcept
    {
        data.fill (0.0f);
        writePos.store (0, std::memory_order_relaxed);
    }

    void push (const float* left, const float* right, int numSamples) noexcept
    {
        int pos = writePos.load (std::memory_order_relaxed);

        for (int i = 0; i < numSamples; ++i)
        {
            data[(size_t) pos] = right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];
            pos = (pos + 1) & (capacity - 1);
        }

        writePos.store (pos, std::memory_order_release);
    }

    void copyLatest (float* dest, int numSamples) const noexcept
    {
        int pos = (writePos.load (std::memory_order_acquire) - numSamples) & (capacity - 1);

        for (int i = 0; i < numSamples; ++i)
        {
            dest[i] = data[(size_t) pos];
            pos = (pos + 1) & (capacity - 1);
        }
    }

private:
    std::array<float, capacity> data {};
    std::atomic<int> writePos { 0 };
};

//==============================================================================
/** How a new instance looks: the view settings (everything stored under a "ui..."
    name: analyzer options, display range, piano, window size) as the user last
    saved them as their default. One small file for all instances; projects keep
    their own view settings and are not affected. */
struct ViewDefaults
{
    static juce::File getFile();
    static void setFileForTests (const juce::File&);

    static bool save (const juce::ValueTree& state);
    static bool clear();
    static void applyTo (juce::ValueTree& state);

    /** View state that belongs to one project only and never becomes a default. */
    static bool isProjectOnly (const juce::Identifier& property);
};

//==============================================================================
class HolterdipolterFilterProcessor final : public juce::AudioProcessor
{
public:
    HolterdipolterFilterProcessor();
    ~HolterdipolterFilterProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                         { return true; }

    const juce::String getName() const override             { return "holterdipolter Filter"; }
    bool acceptsMidi() const override                       { return false; }
    bool producesMidi() const override                      { return false; }
    bool isMidiEffect() const override                      { return false; }
    double getTailLengthSeconds() const override            { return 0.0; }

    int getNumPrograms() override                           { return 1; }
    int getCurrentProgram() override                        { return 0; }
    void setCurrentProgram (int) override                   {}
    const juce::String getProgramName (int) override        { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Access for the editor.

    juce::AudioProcessorValueTreeState apvts;

    juce::RangedAudioParameter* bandParam (int band, hdp::params::BandField field) const
    {
        return bandParams[(size_t) band][(size_t) field];
    }

    juce::RangedAudioParameter* outGainParam() const        { return outGainParameter; }
    juce::RangedAudioParameter* gainScaleParam() const      { return gainScaleParameter; }
    juce::RangedAudioParameter* autoGainParam() const       { return autoGainParameter; }
    juce::RangedAudioParameter* phaseInvertParam() const    { return phaseInvertParameter; }

    bool isBandUsed (int band) const
    {
        return bandValues[(size_t) band][hdp::params::Used]->load (std::memory_order_relaxed) > 0.5f;
    }

    /** Current parameter values of one band. `active` is used && enabled. */
    hdp::BandParams getBandParams (int band) const;

    float getGainScale() const                              { return gainScaleValue->load (std::memory_order_relaxed) * 0.01f; }
    double getCurrentSampleRate() const                     { return currentSampleRate.load (std::memory_order_relaxed); }

    /** Index of the first band that is not in use, or -1 if all 24 are taken. */
    int findFreeBand() const;

    /** Sets a parameter from the UI as one complete host gesture. */
    static void setParam (juce::RangedAudioParameter* param, float value);

    SpectrumTap preTap, postTap;
    std::atomic<bool> analyzerEnabled { false };

    /** What arrives at the sidechain input: another track, shown in the analyzer for comparison.
        It is never heard. sidechainConnected says whether the host feeds that input at all. */
    SpectrumTap sideTap;
    std::atomic<bool> sidechainConnected { false };

    /** Band to listen to in isolation, or -1. A monitoring aid: not a parameter, not saved. */
    std::atomic<int> soloBand { -1 };

    /** The level compensation auto gain is applying right now, in dB. */
    std::atomic<float> autoGainDb { 0.0f };

    /** What each dynamic band is adding to its gain right now, in dB. */
    std::array<std::atomic<float>, hdp::kMaxBands> dynamicGainDb {};

    /** Undo / redo for everything the parameters hold. Message thread only. */
    EditHistory history { *this };

    /** Presets on disk and the A/B pair. Message thread only. */
    PresetManager presets { *this };

    /** Output peak per channel since the last read; the reader resets it. */
    std::array<std::atomic<float>, 2> outputPeak { { { 0.0f }, { 0.0f } } };

private:
    hdp::EqEngine engine;

    std::array<std::array<juce::RangedAudioParameter*, hdp::params::numBandFields>, hdp::kMaxBands> bandParams {};
    std::array<std::array<std::atomic<float>*, hdp::params::numBandFields>, hdp::kMaxBands> bandValues {};

    juce::RangedAudioParameter* outGainParameter = nullptr;
    juce::RangedAudioParameter* gainScaleParameter = nullptr;
    juce::RangedAudioParameter* autoGainParameter = nullptr;
    juce::RangedAudioParameter* phaseInvertParameter = nullptr;
    std::atomic<float>* outGainValue = nullptr;
    std::atomic<float>* gainScaleValue = nullptr;
    std::atomic<float>* autoGainValue = nullptr;
    std::atomic<float>* phaseInvertValue = nullptr;

    std::atomic<double> currentSampleRate { 48000.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HolterdipolterFilterProcessor)
};
