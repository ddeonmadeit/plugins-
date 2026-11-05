#include "PluginProcessor.h"
#include "PluginEditor.h"

#if DD_HAS_PRESET_DATA
 #include "PresetData.h"
#endif

MirageProcessor::MirageProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Mirage", mirage::createLayout()),
      presets (apvts, { "SCENES", "USER" }, DD_PLUGIN_COMPANY, DD_PLUGIN_NAME)
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
}

bool MirageProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (layouts.getMainInputChannelSet() != out)
        return false;
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void MirageProcessor::prepareToPlay (double sampleRate, int)
{
    transport.sampleRate.store (sampleRate);
    setLatencySamples (0);
}

void MirageProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Nothing is changed: the sound is only watched.
    const int n = buffer.getNumSamples();
    if (n == 0)
        return;
    const float* l = buffer.getReadPointer (0);
    const float* r = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : l;
    tap.push (l, r, n);

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm())
                transport.bpm.store (*b);
            transport.playing.store (pos->getIsPlaying());
            if (auto q = pos->getPpqPosition())
                transport.ppq.store (*q);
        }
}

int MirageProcessor::getNumPrograms()           { return std::max (1, presets.getNumFactoryPresets()); }
int MirageProcessor::getCurrentProgram()        { return std::max (0, presets.getCurrentIndex()); }
void MirageProcessor::setCurrentProgram (int i)
{
    if (juce::isPositiveAndBelow (i, presets.getNumFactoryPresets()) && i != presets.getCurrentIndex())
        presets.loadPreset (i);
}

const juce::String MirageProcessor::getProgramName (int i)
{
    if (! juce::isPositiveAndBelow (i, presets.getNumFactoryPresets()))
        return "Init";
    return presets.get (i).category + " - " + presets.get (i).name;
}

void MirageProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    presets.writeState (state);
    state.setProperty ("editorWidth", editorWidth.load(), nullptr);
    state.setProperty ("editorHeight", editorHeight.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void MirageProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            editorWidth.store ((int) tree.getProperty ("editorWidth", 1600));
            editorHeight.store ((int) tree.getProperty ("editorHeight", 440));
            dd::replaceStateExactly (apvts, tree);
            presets.readState (tree);
        }
}

juce::AudioProcessorEditor* MirageProcessor::createEditor() { return new MirageEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MirageProcessor(); }
