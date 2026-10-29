#include "PluginProcessor.h"
#include "PluginEditor.h"

#if DD_HAS_PRESET_DATA
 #include "PresetData.h"
#endif

using sunburst::P;

const juce::StringArray& SunburstProcessor::categoryOrder()
{
    static const juce::StringArray order { "ACOUSTIC", "CLEAN", "CRUNCH", "HEAVY", "TRAVIS SCOTT", "MIKE DEAN", "YEEZUS",
                                           "MOSHC0RE", "TRIP HOP", "SAFE", "USER" };
    return order;
}

SunburstProcessor::SunburstProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Sunburst", sunburst::createLayout()),
      presets (apvts, categoryOrder(), DD_PLUGIN_COMPANY, DD_PLUGIN_NAME)
{
    params.attach (apvts);
    for (auto& l : stringLevels) l.store (0.0f);
    for (auto& f : stringFrets) f.store (-1);

    library.open (sunburst::Library::findInstalled());
    loader = std::make_unique<sunburst::Loader> (library);
    apvts.addParameterListener (sunburst::id (P::guitar), this);

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
    requestGuitar();
}

SunburstProcessor::~SunburstProcessor()
{
    apvts.removeParameterListener (sunburst::id (P::guitar), this);
    loader.reset();   // stop the loader thread before the library it reads goes away
}

void SunburstProcessor::parameterChanged (const juce::String&, float)
{
    requestGuitar();
}

void SunburstProcessor::requestGuitar()
{
    const auto& ids = sunburst::choices::guitarIds();
    loader->request (ids[juce::jlimit (0, ids.size() - 1, params.getInt (P::guitar))]);
}

//==============================================================================
bool SunburstProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void SunburstProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    engine.prepare (sr);
    rig.prepare (sr);
    glue.prepare (sr);
    clipper.prepare (sr, kChunk);
    for (auto& f : sideLowCut)
    {
        f.set (dd::Biquad::Type::HighPass, 240.0f, 0.7071f, 0.0f, sr);   // doubles and rooms spread above the low strings only
        f.reset();
    }
    outGainSm.reset (sr, 0.03);
    widthSm.reset (sr, 0.03);
    outGainSm.setCurrentAndTargetValue (dd::dbToGain (params.get (P::outGain)));
    widthSm.setCurrentAndTargetValue (params.get (P::width));
    setLatencySamples (rig.getLatency() + clipper.getLatency());
    modWheel = bendValue = 0.0f;
}

sunburst::PlayParams SunburstProcessor::playParams() const
{
    sunburst::PlayParams p;
    p.stringNoise = params.get (P::stringNoise);
    p.pick = params.get (P::pick);
    p.mute = juce::jlimit (0.0f, 1.0f, params.get (P::mute) + modWheel);
    p.ring = params.get (P::ring);
    p.strumMs = params.get (P::strum);
    p.legato = params.getInt (P::legato);
    p.slideMs = params.get (P::slideTime);
    p.vibrato = params.get (P::vibrato);
    p.humanize = params.get (P::humanize);
    p.velCurve = params.get (P::velCurve);
    p.bend = bendValue * (float) params.getInt (P::bendRange);
    p.doubleTrack = params.get (P::doubleTrack);
    p.transpose = params.getInt (P::transpose);
    return p;
}

void SunburstProcessor::handleMidi (const juce::MidiMessage& m, const sunburst::InstrumentData* g)
{
    const auto p = playParams();
    if (m.isNoteOn())
    {
        const int note = m.getNoteNumber();
        if (note >= sunburst::GuitarEngine::kFxLow && note <= sunburst::GuitarEngine::kFxHigh)
            engine.fxKey (note, m.getFloatVelocity(), g);
        else
            engine.noteOn (note, m.getFloatVelocity(), g, p);
    }
    else if (m.isNoteOff())
    {
        const int note = m.getNoteNumber();
        if (note < sunburst::GuitarEngine::kFxLow || note > sunburst::GuitarEngine::kFxHigh)
            engine.noteOff (note, g, p);
    }
    else if (m.isSustainPedalOn())  engine.setSustain (true, g, p);
    else if (m.isSustainPedalOff()) engine.setSustain (false, g, p);
    else if (m.isController() && m.getControllerNumber() == 1)
        modWheel = (float) m.getControllerValue() / 127.0f;
    else if (m.isPitchWheel())
        bendValue = juce::jlimit (-1.0f, 1.0f, (float) (m.getPitchWheelValue() - 8192) / 8192.0f);
    else if (m.isAllNotesOff() || m.isAllSoundOff())
        engine.allNotesOff();
}

void SunburstProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Offline renders (FL export, the QA harness) must not start before the guitar is in memory.
    if (isNonRealtime())
        loader->waitUntilIdle();
    const auto* g = loader->get();
    if (g != lastGuitar)
    {
        if (lastGuitar != nullptr)
            engine.allNotesOff();
        lastGuitar = g;
    }

    bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm())
                bpm = juce::jlimit (30.0, 400.0, *b);
            if (pos->getIsPlaying())
                if (auto ppq = pos->getPpqPosition())
                {
                    hostBeat = *ppq;
                    playing = true;
                }
        }
    if (! playing)
        hostBeat = freeBeat;

    // Rig settings for this block.
    auto& s = rigSettings;
    s.acoustic = g == nullptr || g->family == "acoustic";
    s.tone = params.get (P::tone);
    s.body = params.get (P::body);
    s.doubleTrack = params.get (P::doubleTrack);
    s.compOn = params.getBool (P::compOn);       s.comp = params.get (P::compAmount);
    s.driveOn = params.getBool (P::driveOn);     s.driveGain = params.get (P::driveGain); s.driveTone = params.get (P::driveTone);
    s.fuzzOn = params.getBool (P::fuzzOn);       s.fuzzGain = params.get (P::fuzzGain);
    s.chorusOn = params.getBool (P::chorusOn);   s.chorusRate = params.get (P::chorusRate); s.chorusDepth = params.get (P::chorusDepth);
    s.tremOn = params.getBool (P::tremOn);       s.tremBeats = dd::divisionBeats (params.getInt (P::tremDiv)); s.tremDepth = params.get (P::tremDepth);
    s.delayOn = params.getBool (P::delayOn);     s.delayFeedback = params.get (P::delayFeedback); s.delayMix = params.get (P::delayMix);
    s.ampOn = params.getBool (P::ampOn);         s.ampModel = params.getInt (P::ampModel);
    s.gain = params.get (P::ampGain);            s.bass = params.get (P::ampBass); s.mid = params.get (P::ampMid);
    s.treble = params.get (P::ampTreble);        s.presence = params.get (P::ampPresence); s.levelDb = params.get (P::ampLevel);
    s.cabOn = params.getBool (P::cabOn);         s.cabType = params.getInt (P::cabType); s.mic = params.get (P::cabMic);
    s.reverbOn = params.getBool (P::reverbOn);   s.reverbMix = params.get (P::reverbMix); s.reverbSize = params.get (P::reverbSize);
    s.beatsPerSample = bpm / 60.0 / sr;
    rig.setDelayTime ((float) (dd::divisionBeats (params.getInt (P::delayDiv)) * 60.0 / bpm * sr), false);
    outGainSm.setTargetValue (dd::dbToGain (params.get (P::outGain)));
    widthSm.setTargetValue (params.get (P::width));

    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    std::array<float, kChunk> monoR {};

    auto it = midi.cbegin();
    for (int pos = 0; pos < numSamples; pos += kChunk)
    {
        const int n = std::min (kChunk, numSamples - pos);
        float* cl = L + pos;
        float* cr = R != nullptr ? R + pos : monoR.data();
        std::fill (cr, cr + n, 0.0f);

        int done = 0;
        while (done < n)
        {
            int segEnd = n;
            for (; it != midi.cend(); ++it)
            {
                const auto meta = *it;
                const int t = juce::jlimit (0, numSamples - 1, meta.samplePosition) - pos;
                if (t >= n) break;
                if (t > done) { segEnd = t; break; }
                handleMidi (meta.getMessage(), g);
            }
            engine.render (cl + done, cr + done, segEnd - done, g, playParams());
            done = segEnd;
        }

        s.beat = hostBeat + (double) pos * s.beatsPerSample;
        rig.process (cl, cr, n, s);
        processChunk (cl, cr, n);
        if (R == nullptr)
            for (int i = 0; i < n; ++i)
                cl[i] = 0.5f * (cl[i] + cr[i]);
    }

    const double blockBeats = (double) numSamples * bpm / 60.0 / sr;
    freeBeat = (playing ? hostBeat : freeBeat) + blockBeats;
    for (int st = 0; st < sunburst::GuitarEngine::kStrings; ++st)
    {
        stringLevels[(size_t) st].store (engine.stringLevel (st));
        stringFrets[(size_t) st].store (engine.stringFret (st));
    }
    scope.push (L, R, numSamples);
}

void SunburstProcessor::processChunk (float* L, float* R, int n)
{
    const float glueAmt = params.get (P::glue);
    for (int i = 0; i < n; ++i)
    {
        float l = L[i], r = R[i];
        const float w = widthSm.getNextValue();
        const float mid = 0.5f * (l + r), side = sideLowCut[1].process (sideLowCut[0].process (0.5f * (l - r))) * w;
        l = mid + side;
        r = mid - side;
        glue.process (l, r, glueAmt);
        L[i] = l;
        R[i] = r;
    }

    float* chans[2] = { L, R };
    juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const float g = outGainSm.getNextValue();
        L[i] *= g;
        R[i] *= g;
    }
    clipper.process (block, 0.0f);

    for (int i = 0; i < n; ++i)
        if (! std::isfinite (L[i]) || ! std::isfinite (R[i]))
        {
            std::fill (L, L + n, 0.0f);
            std::fill (R, R + n, 0.0f);
            engine.reset();
            rig.reset();
            glue.reset();
            clipper.reset();
            for (auto& f : sideLowCut)
                f.reset();
            return;
        }
}

//==============================================================================
int SunburstProcessor::getNumPrograms()    { return std::max (1, presets.getNumFactoryPresets()); }
int SunburstProcessor::getCurrentProgram() { return juce::jmax (0, presets.getCurrentIndex()); }

void SunburstProcessor::setCurrentProgram (int index)
{
    if (juce::isPositiveAndBelow (index, presets.getNumFactoryPresets()) && index != presets.getCurrentIndex())
        presets.loadPreset (index);
}

const juce::String SunburstProcessor::getProgramName (int index)
{
    if (! juce::isPositiveAndBelow (index, presets.getNumFactoryPresets()))
        return "Init";
    return presets.get (index).category + " - " + presets.get (index).name;
}

void SunburstProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    presets.writeState (state);
    state.setProperty ("uiScale", editorScale, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SunburstProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;
    auto tree = juce::ValueTree::fromXml (*xml);
    dd::replaceStateExactly (apvts, tree);
    presets.readState (tree);
    editorScale = juce::jlimit (0.6f, 1.8f, (float) tree.getProperty ("uiScale", 1.0f));
    requestGuitar();
}

juce::AudioProcessorEditor* SunburstProcessor::createEditor() { return new SunburstEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SunburstProcessor(); }
