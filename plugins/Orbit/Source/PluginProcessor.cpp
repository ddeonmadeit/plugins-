#include "PluginProcessor.h"
#include "PluginEditor.h"

#if DD_HAS_PRESET_DATA
 #include "PresetData.h"
#endif

using orbit::P;

const juce::StringArray& OrbitProcessor::categoryOrder()
{
    static const juce::StringArray order { "AROUND YOU", "ROOMS", "MOTION", "TRAVIS SCOTT", "TAME IMPALA", "TRIP HOP", "MIX", "INTERNET GIRL", "NEO SOUL", "SOUL", "USER" };
    return order;
}

OrbitProcessor::OrbitProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Orbit", orbit::createLayout()),
      presets (apvts, categoryOrder(), DD_PLUGIN_COMPANY, DD_PLUGIN_NAME)
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

bool OrbitProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Binaural output is always stereo; a mono track goes in as one point.
    const auto in = layouts.getMainInputChannelSet();
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
           && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void OrbitProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    spatial.prepare (sr);
    outGainSm.reset (sr, 0.03);
    outGainSm.setCurrentAndTargetValue (dd::dbToGain (params.get (P::outGain)));
    mixSm.reset (sr, 0.03);
    mixSm.setCurrentAndTargetValue (params.get (P::mix));
    setLatencySamples (0);
}

void OrbitProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midi);
    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    if (numSamples == 0 || buffer.getNumChannels() < 2)
        return;

    // The fourth dimension follows the song: locked to the host's beat while it plays, free when stopped.
    double bpm = 120.0, ppq = 0.0;
    bool locked = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm())
                bpm = juce::jlimit (20.0, 400.0, *b);
            if (pos->getIsPlaying())
                if (auto q = pos->getPpqPosition())
                {
                    ppq = *q;
                    locked = true;
                }
        }
    const double beatsPerSample = bpm / 60.0 / sr;
    const double cycle = orbit::motionCycleBeats (params.getInt (P::motionRate));

    outGainSm.setTargetValue (dd::dbToGain (params.get (P::outGain)));
    mixSm.setTargetValue (params.get (P::mix));

    orbit::SpatialParams sp;
    sp.width = params.get (P::width);
    sp.room = params.getInt (P::room);
    sp.size = params.get (P::size);
    sp.roomLevel = params.get (P::roomLevel);
    sp.doppler = params.get (P::doppler);
    sp.speakers = params.getInt (P::perfOutput) == 1;
    sp.head = params.getInt (P::perfHead);
    const orbit::Vec3 placed { params.get (P::posX), params.get (P::posY), params.get (P::posZ) };
    const int mode = params.getInt (P::motion);
    const float amount = params.get (P::motionAmount);

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    const float* inR = numIn > 1 ? R : L;
    double phase = 0.0;
    float energy = 0.0f;
    for (int pos = 0; pos < numSamples; pos += kChunk)
    {
        const int n = std::min (kChunk, numSamples - pos);
        phase = locked ? (ppq + pos * beatsPerSample) / cycle : freePhase;
        sp.position = orbit::movedPosition (placed, mode, phase, amount);
        spatial.process (L + pos, inR + pos, wetL.data(), wetR.data(), n, sp);
        if (! locked)
        {
            freePhase += n * beatsPerSample / cycle;
            freePhase -= std::floor (freePhase);
        }
        for (int i = 0; i < n; ++i)
        {
            const float dl = L[pos + i], dr = inR[pos + i];
            energy += 0.5f * (dl * dl + dr * dr);
            const float m = mixSm.getNextValue();
            const float g = outGainSm.getNextValue();
            // A soft ceiling just under full scale, after the output level (transparent below -2 dBFS).
            constexpr float ceiling = 0.944f;
            L[pos + i] = ceiling * dd::kneeClip ((dl + (wetL[(size_t) i] - dl) * m) * g / ceiling, 0.85f);
            R[pos + i] = ceiling * dd::kneeClip ((dr + (wetR[(size_t) i] - dr) * m) * g / ceiling, 0.85f);
        }
    }
    if (locked)
        freePhase = phase - std::floor (phase);
    scope.push (L, R, numSamples);

    const float rms = std::sqrt (energy / (float) numSamples);
    inputRms += (rms - inputRms) * (rms > inputRms ? 0.5f : 0.08f);
    const auto heard = spatial.heardPosition();
    telemetry.x.store (heard.x);
    telemetry.y.store (heard.y);
    telemetry.z.store (heard.z);
    telemetry.input.store (inputRms);
    telemetry.late.store (spatial.lateLevel());
    telemetry.phase.store (phase);
}

int OrbitProcessor::getNumPrograms()           { return std::max (1, presets.getNumFactoryPresets()); }
int OrbitProcessor::getCurrentProgram()        { return std::max (0, presets.getCurrentIndex()); }
void OrbitProcessor::setCurrentProgram (int i)
{
    // Hosts may re-send the current program after restoring a project; don't wipe the user's tweaks.
    if (juce::isPositiveAndBelow (i, presets.getNumFactoryPresets()) && i != presets.getCurrentIndex())
        presets.loadPreset (i);
}

const juce::String OrbitProcessor::getProgramName (int i)
{
    if (! juce::isPositiveAndBelow (i, presets.getNumFactoryPresets()))
        return "Init";
    return presets.get (i).category + " - " + presets.get (i).name;
}

void OrbitProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    presets.writeState (state);
    state.setProperty ("uiScale", editorScale, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void OrbitProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            dd::replaceStateExactly (apvts, tree);
            presets.readState (tree);
            editorScale = juce::jlimit (0.5f, 1.8f, (float) tree.getProperty ("uiScale", 1.0f));
        }
}

juce::AudioProcessorEditor* OrbitProcessor::createEditor() { return new OrbitEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new OrbitProcessor(); }
