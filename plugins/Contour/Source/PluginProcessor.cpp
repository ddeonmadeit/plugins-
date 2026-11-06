#include "PluginProcessor.h"
#include "PluginEditor.h"

#if DD_HAS_PRESET_DATA
 #include "PresetData.h"
#endif

const juce::StringArray& ContourProcessor::categoryOrder()
{
    static const juce::StringArray order { "NATURAL", "TIGHT", "CREATIVE", "NEO SOUL", "SOUL" };
    return order;
}

ContourProcessor::ContourProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Contour", contour::createLayout()),
      presets (apvts, categoryOrder(), DD_PLUGIN_COMPANY, DD_PLUGIN_NAME)
{
   #if DD_HAS_PRESET_DATA
    for (int i = 0; i < PresetData::namedResourceListSize; ++i)
    {
        int size = 0;
        if (const char* data = PresetData::getNamedResource (PresetData::namedResourceList[i], size))
            presets.addFactoryPreset (juce::String::fromUTF8 (data, size),
                                      PresetData::getNamedResourceOriginalFilename (PresetData::namedResourceList[i]));
    }
   #endif
    presets.finishLoading();
    if (presets.getNumFactoryPresets() > 0)
        presets.loadPreset (0);
    engine = std::make_unique<contour::Engine> (apvts);
}

ContourProcessor::~ContourProcessor()
{
    engine.reset();
}

bool ContourProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (layouts.getMainInputChannelSet() != out)
        return false;
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void ContourProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine->prepare (sampleRate, samplesPerBlock);
    setLatencySamples (0);
}

void ContourProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int ch = getTotalNumInputChannels(); ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
    engine->process (buffer, getPlayHead(), isNonRealtime());
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (dd::isBad (d[i]))
            {
                buffer.clear();
                return;
            }
    }
}

int ContourProcessor::getNumPrograms()           { return std::max (1, presets.getNumFactoryPresets()); }
int ContourProcessor::getCurrentProgram()        { return std::max (0, presets.getCurrentIndex()); }
void ContourProcessor::setCurrentProgram (int i)
{
    // Hosts may re-send the current program after restoring a project; don't wipe the user's tweaks.
    if (juce::isPositiveAndBelow (i, presets.getNumFactoryPresets()) && i != presets.getCurrentIndex())
        presets.loadPreset (i);
}

const juce::String ContourProcessor::getProgramName (int i)
{
    if (! juce::isPositiveAndBelow (i, presets.getNumFactoryPresets()))
        return "Init";
    return presets.get (i).category + " - " + presets.get (i).name;
}

void ContourProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName ("Contour"), nullptr);
    presets.writeState (state);
    state.setProperty ("uiScale", editorScale, nullptr);
    engine->writeState (state);   // the take's file and every note edit
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ContourProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            engine->readState (tree);
            auto paramState = tree.createCopy();
            paramState.removeChild (paramState.getChildWithName ("Contour"), nullptr);
            dd::replaceStateExactly (apvts, paramState);
            presets.readState (paramState);
            editorScale = juce::jlimit (0.6f, 1.8f, (float) tree.getProperty ("uiScale", 1.0f));
        }
}

juce::AudioProcessorEditor* ContourProcessor::createEditor() { return new ContourEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ContourProcessor(); }
