#include "PluginProcessor.h"
#include "PluginEditor.h"

#if DD_HAS_PRESET_DATA
 #include "PresetData.h"
#endif

using __NS__::P;

__NAME__Processor::__NAME__Processor()
//@SYNTH
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
//@END
//@EFFECT
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
//@END
      apvts (*this, nullptr, "__NAME__", __NS__::createLayout()),
      presets (apvts, { "INIT" }, DD_PLUGIN_COMPANY, DD_PLUGIN_NAME)
{
    params.attach (apvts);
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

bool __NAME__Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
//@EFFECT
    if (layouts.getMainInputChannelSet() != out)
        return false;
//@END
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void __NAME__Processor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
//@SYNTH
    for (auto& v : voices)
    {
        v.env.setSampleRate (sr);
        v.env.reset();
        v.filter.reset();
        v.gate = false;
    }
//@END
//@EFFECT
    dirt.prepare (sr, kChunk);
//@END
    clipper.prepare (sr, kChunk);
    outGainSm.reset (sr, 0.03);
    outGainSm.setCurrentAndTargetValue (dd::dbToGain (params.get (P::outGain)));
//@SYNTH
    setLatencySamples (clipper.getLatency());
//@END
//@EFFECT
    setLatencySamples (dirt.getLatency() + clipper.getLatency());
//@END
}
//@SYNTH

void __NAME__Processor::noteOn (int note, float velocity)
{
    Voice* target = nullptr;
    for (auto& v : voices)
        if (! v.env.isActive()) { target = &v; break; }
    if (target == nullptr)
        for (auto& v : voices)
            if (target == nullptr || v.age < target->age) target = &v;

    target->note = note;
    target->velocity = velocity;
    target->age = ++stamp;
    target->gate = true;
    target->osc.reset (0.0f);
    target->env.noteOn();
}

void __NAME__Processor::noteOff (int note)
{
    for (auto& v : voices)
        if (v.gate && v.note == note)
        {
            v.gate = false;
            v.env.noteOff();
        }
}
//@END

void __NAME__Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();
    outGainSm.setTargetValue (dd::dbToGain (params.get (P::outGain)));
//@EFFECT
    juce::ignoreUnused (midi);
//@END

//@SYNTH
    buffer.clear();
    for (auto& v : voices)
        v.env.setParams (params.get (P::attack), params.get (P::decay), params.get (P::sustain), params.get (P::release));
    const auto wave = (dd::Wave) params.getInt (P::wave);
    const float level = params.get (P::level);
    const float g = dd::tptG (params.get (P::cutoff), sr);
    const float k = 1.0f / (0.6f + params.get (P::resonance) * 10.0f);

    float* L = buffer.getWritePointer (0);
    auto it = midi.cbegin();
    for (int i = 0; i < numSamples; ++i)
    {
        for (; it != midi.cend() && (*it).samplePosition <= i; ++it)
        {
            const auto m = (*it).getMessage();
            if (m.isNoteOn())       noteOn (m.getNoteNumber(), m.getFloatVelocity());
            else if (m.isNoteOff()) noteOff (m.getNoteNumber());
        }
        float s = 0.0f;
        for (auto& v : voices)
        {
            if (! v.env.isActive())
                continue;
            v.filter.setCoeffs (g, k);
            const float dt = dd::midiToHz ((float) v.note) / (float) sr;
            float lp, bp, hp;
            v.filter.process (v.osc.process (dt, wave), lp, bp, hp);
            s += lp * v.env.process() * v.velocity * level * 0.5f;
        }
        L[i] = s;
    }
    if (numCh > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);
//@END

    for (int pos = 0; pos < numSamples; pos += kChunk)
    {
        const int n = std::min (kChunk, numSamples - pos);
        if (numCh > 1)
            processChunk (buffer.getWritePointer (0) + pos, buffer.getWritePointer (1) + pos, n);
        else
        {
            std::array<float, kChunk> scratch {};
            std::copy (buffer.getReadPointer (0) + pos, buffer.getReadPointer (0) + pos + n, scratch.begin());
            processChunk (buffer.getWritePointer (0) + pos, scratch.data(), n);
        }
    }
    scope.push (buffer.getReadPointer (0), numCh > 1 ? buffer.getReadPointer (1) : nullptr, numSamples);
}

void __NAME__Processor::processChunk (float* L, float* R, int n)
{
    float* chans[2] = { L, R };
    juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
//@EFFECT

    dd::Dirt::Params dp;
    dp.type = (dd::DirtType) params.getInt (P::dirtType);
    dp.drive = params.get (P::drive);
    dp.tone = params.get (P::tone);
    dp.mix = params.get (P::mix);
    dp.subKeep = 0.5f;
    dirt.process (block, dp);
//@END

    clipper.process (block, 0.0f);
    for (int i = 0; i < n; ++i)
    {
        const float gain = outGainSm.getNextValue();
        L[i] *= gain;
        R[i] *= gain;
    }
}

int __NAME__Processor::getNumPrograms()           { return std::max (1, presets.getNumFactoryPresets()); }
int __NAME__Processor::getCurrentProgram()        { return std::max (0, presets.getCurrentIndex()); }
void __NAME__Processor::setCurrentProgram (int i)
{
    // Hosts may re-send the current program after restoring a project; don't wipe the user's tweaks.
    if (juce::isPositiveAndBelow (i, presets.getNumFactoryPresets()) && i != presets.getCurrentIndex())
        presets.loadPreset (i);
}

const juce::String __NAME__Processor::getProgramName (int i)
{
    if (! juce::isPositiveAndBelow (i, presets.getNumFactoryPresets()))
        return "Init";
    return presets.get (i).category + " - " + presets.get (i).name;
}

void __NAME__Processor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    presets.writeState (state);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void __NAME__Processor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            dd::replaceStateExactly (apvts, tree);
            presets.readState (tree);
        }
}

juce::AudioProcessorEditor* __NAME__Processor::createEditor() { return new __NAME__Editor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new __NAME__Processor(); }
