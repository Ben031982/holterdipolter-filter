#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace hdp;

namespace
{
    juce::File& viewDefaultsFile()
    {
        static juce::File file = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                     .getChildFile ("holterdipolter").getChildFile ("Filter").getChildFile ("view-defaults.xml");
        return file;
    }
}

juce::File ViewDefaults::getFile()                          { return viewDefaultsFile(); }
void ViewDefaults::setFileForTests (const juce::File& f)    { viewDefaultsFile() = f; }

bool ViewDefaults::isProjectOnly (const juce::Identifier& property)
{
    return property.toString() == "uiReference";
}

bool ViewDefaults::save (const juce::ValueTree& state)
{
    juce::XmlElement xml ("HolterdipolterFilterView");

    for (int i = 0; i < state.getNumProperties(); ++i)
    {
        const auto name = state.getPropertyName (i);

        if (name.toString().startsWith ("ui") && ! isProjectOnly (name))
            xml.setAttribute (name, state.getProperty (name).toString());
    }

    const auto file = getFile();
    return file.getParentDirectory().createDirectory().wasOk() && xml.writeTo (file);
}

bool ViewDefaults::clear()
{
    const auto file = getFile();
    return ! file.existsAsFile() || file.deleteFile();
}

void ViewDefaults::applyTo (juce::ValueTree& state)
{
    const auto xml = juce::XmlDocument::parse (getFile());

    if (xml == nullptr || ! xml->hasTagName ("HolterdipolterFilterView"))
        return;

    for (int i = 0; i < xml->getNumAttributes(); ++i)
    {
        const auto name = xml->getAttributeName (i);

        if (! name.startsWith ("ui") || isProjectOnly (name))
            continue;

        // Numbers come back as numbers, so they read the same as in a saved project.
        const auto text = xml->getAttributeValue (i);
        state.setProperty (name, text.containsOnly ("0123456789.-") && text.isNotEmpty() ? juce::var (text.getDoubleValue())
                                                                                        : juce::var (text), nullptr);
    }
}

//==============================================================================
HolterdipolterFilterProcessor::HolterdipolterFilterProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                       .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "HolterdipolterFilter", params::createLayout())
{
    for (int band = 0; band < kMaxBands; ++band)
    {
        for (int field = 0; field < params::numBandFields; ++field)
        {
            const auto id = params::bandId (band, (params::BandField) field);
            bandParams[(size_t) band][(size_t) field] = apvts.getParameter (id);
            bandValues[(size_t) band][(size_t) field] = apvts.getRawParameterValue (id);
            jassert (bandParams[(size_t) band][(size_t) field] != nullptr);
        }
    }

    outGainParameter   = apvts.getParameter (params::outGainId);
    gainScaleParameter = apvts.getParameter (params::gainScaleId);
    autoGainParameter  = apvts.getParameter (params::autoGainId);
    outGainValue       = apvts.getRawParameterValue (params::outGainId);
    gainScaleValue     = apvts.getRawParameterValue (params::gainScaleId);
    autoGainValue      = apvts.getRawParameterValue (params::autoGainId);

    // A new instance looks the way the user set as their default.
    ViewDefaults::applyTo (apvts.state);

    phaseInvertParameter = apvts.getParameter (params::phaseInvertId);
    phaseInvertValue     = apvts.getRawParameterValue (params::phaseInvertId);
}

//==============================================================================
void HolterdipolterFilterProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate.store (sampleRate);

    engine.setOutputGainDb ((double) outGainValue->load());
    engine.setAutoGain (autoGainValue->load() > 0.5f);
    engine.setPhaseInvert (phaseInvertValue->load() > 0.5f);
    engine.prepare (sampleRate);

    preTap.clear();
    postTap.clear();
    sideTap.clear();
}

bool HolterdipolterFilterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainInputChannelSet() != out)
        return false;

    // The sidechain is only looked at, so it may be absent, mono or stereo.
    if (layouts.inputBuses.size() > 1)
    {
        const auto& side = layouts.inputBuses.getReference (1);

        if (! side.isDisabled() && side != juce::AudioChannelSet::mono() && side != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

BandParams HolterdipolterFilterProcessor::getBandParams (int band) const
{
    const auto& v = bandValues[(size_t) band];
    const auto get = [&v] (params::BandField f) { return v[(size_t) f]->load (std::memory_order_relaxed); };

    BandParams p;
    p.active     = get (params::Used) > 0.5f && get (params::On) > 0.5f;
    p.type       = (FilterType) juce::jlimit (0, kNumFilterTypes - 1, juce::roundToInt (get (params::Type)));
    p.placement  = (Placement)  juce::jlimit (0, kNumPlacements - 1,  juce::roundToInt (get (params::Place)));
    p.slopeIndex = juce::jlimit (0, kNumSlopes - 1, juce::roundToInt (get (params::Slope)));
    p.freq       = (double) get (params::Freq);
    p.gainDb     = (double) get (params::Gain);
    p.q          = (double) get (params::Q);

    p.dynRangeDb    = (double) get (params::Dyn);
    p.thresholdDb   = (double) get (params::Thresh);
    p.autoThreshold = get (params::AutoThresh) > 0.5f;
    p.dynFromSidechain = get (params::DynSide) > 0.5f;
    return p;
}

int HolterdipolterFilterProcessor::findFreeBand() const
{
    for (int band = 0; band < kMaxBands; ++band)
        if (! isBandUsed (band))
            return band;

    return -1;
}

void HolterdipolterFilterProcessor::setParam (juce::RangedAudioParameter* param, float value)
{
    if (param == nullptr)
        return;

    const float normalised = param->convertTo0to1 (value);

    if (juce::approximatelyEqual (normalised, param->getValue()))
        return;

    param->beginChangeGesture();
    param->setValueNotifyingHost (normalised);
    param->endChangeGesture();
}

//==============================================================================
void HolterdipolterFilterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // The buffer holds the main channels first, then those of the sidechain.
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = juce::jmin (getMainBusNumOutputChannels(), buffer.getNumChannels());

    if (numSamples == 0 || numChannels == 0)
        return;

    const int sideChannels = getBusCount (true) > 1 ? getChannelCountOfBus (true, 1) : 0;
    const bool hasSide = sideChannels > 0 && buffer.getNumChannels() >= numChannels + sideChannels;
    sidechainConnected.store (hasSide, std::memory_order_relaxed);

    for (int band = 0; band < kMaxBands; ++band)
        engine.setBand (band, getBandParams (band));

    engine.setGainScale ((double) getGainScale());
    engine.setOutputGainDb ((double) outGainValue->load (std::memory_order_relaxed));
    engine.setAutoGain (autoGainValue->load (std::memory_order_relaxed) > 0.5f);
    engine.setPhaseInvert (phaseInvertValue->load (std::memory_order_relaxed) > 0.5f);

    const int solo = soloBand.load (std::memory_order_relaxed);
    engine.setSoloBand (solo >= 0 && solo < kMaxBands && isBandUsed (solo) ? solo : -1);

    float* left  = buffer.getWritePointer (0);
    float* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    const bool analyse = analyzerEnabled.load (std::memory_order_relaxed);

    const float* sideLeft  = hasSide ? buffer.getReadPointer (numChannels) : nullptr;
    const float* sideRight = hasSide && sideChannels > 1 ? buffer.getReadPointer (numChannels + 1) : nullptr;

    if (analyse)
    {
        preTap.push (left, right, numSamples);

        if (hasSide)
            sideTap.push (sideLeft, sideRight, numSamples);
    }

    engine.process (left, right, numSamples, sideLeft, sideRight);

    if (analyse)
        postTap.push (left, right, numSamples);

    autoGainDb.store ((float) engine.getAutoGainDb(), std::memory_order_relaxed);

    for (int band = 0; band < kMaxBands; ++band)
        dynamicGainDb[(size_t) band].store ((float) engine.getDynamicGainDb (band), std::memory_order_relaxed);

    for (int ch = 0; ch < 2; ++ch)
    {
        const int source = juce::jmin (ch, numChannels - 1);
        const float peak = buffer.getMagnitude (source, 0, numSamples);
        auto& slot = outputPeak[(size_t) ch];

        if (peak > slot.load (std::memory_order_relaxed))
            slot.store (peak, std::memory_order_relaxed);
    }
}

//==============================================================================
void HolterdipolterFilterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    presets.writeTo (state);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void HolterdipolterFilterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            presets.readFrom (state);     // takes its own part out again
            apvts.replaceState (state);
            history.reset();   // a loaded state is a new starting point, not an edit
        }
}

juce::AudioProcessorEditor* HolterdipolterFilterProcessor::createEditor()
{
    return new HolterdipolterFilterEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HolterdipolterFilterProcessor();
}
