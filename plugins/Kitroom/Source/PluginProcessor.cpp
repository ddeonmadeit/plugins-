#include "PluginProcessor.h"
#include "PluginEditor.h"

#if DD_HAS_PRESET_DATA
 #include "PresetData.h"
#endif

using kitroom::SP;
using kitroom::MP;

const juce::StringArray& KitroomProcessor::categoryOrder()
{
    static const juce::StringArray order { "STUDIO", "LIVE ROOM", "VINTAGE", "TIGHT", "BIG", "YEEZUS", "USER" };
    return order;
}

KitroomProcessor::KitroomProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Kitroom", kitroom::createLayout()),
      presets (apvts, categoryOrder(), DD_PLUGIN_COMPANY, DD_PLUGIN_NAME)
{
    params.attach (apvts);
    for (auto& n : noteMap) n.store (-1);
    for (auto& h : hitLevels) h.store (0.0f);

    library.open (kitroom::Library::findInstalled());
    loader = std::make_unique<kitroom::SoundLoader> (library);

    resetKeys();
    loadDefaultKit();

    presets.onApplyExtra = [this] (const juce::var& extra) { applyKit (extra["kit"]); };
    presets.onCaptureExtra = [this]
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("kit", captureKit());
        return juce::var (o);
    };

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

KitroomProcessor::~KitroomProcessor()
{
    loader.reset(); // stop the loader thread before the library it reads goes away
}

//==============================================================================
void KitroomProcessor::loadDefaultKit()
{
    // The DRS studio kit with the Muldjord china; any piece missing from the library falls back to
    // the first sound of its kind (a different one per slot where there are several).
    static const char* const defaults[kr::kNumSlots] = { "drs_kick", "drs_snare", "drs_hihat", "drs_tom1", "drs_tom2",
                                                          "drs_tom3", "drs_crash_beat", "drs_crash_602", "drs_ride", "mj_china" };
    std::map<kr::Category, int> used;
    for (int s = 0; s < kr::kNumSlots; ++s)
    {
        const auto cat = kr::slots()[(size_t) s].category;
        const auto options = library.soundsFor (cat);
        const int pick = used[cat]++;
        if (library.indexOf (defaults[s]) >= 0)
            setSlotSound (s, defaults[s]);
        else
            setSlotSound (s, options.empty() ? juce::String() : library.getSounds()[(size_t) options[(size_t) pick % options.size()]].id);
    }
}

void KitroomProcessor::setSlotSound (int slot, const juce::String& soundId)
{
    loader->request (slot, soundId);
}

juce::var KitroomProcessor::captureKit() const
{
    auto* o = new juce::DynamicObject();
    for (int s = 0; s < kr::kNumSlots; ++s)
        o->setProperty (kr::slots()[(size_t) s].id, getSlotSound (s));
    return juce::var (o);
}

void KitroomProcessor::applyKit (const juce::var& kit)
{
    if (! kit.isObject())
    {
        loadDefaultKit();
        return;
    }
    for (int s = 0; s < kr::kNumSlots; ++s)
    {
        const auto id = kit[juce::Identifier (kr::slots()[(size_t) s].id)].toString();
        if (library.indexOf (id) >= 0)
            setSlotSound (s, id);
    }
}

//==============================================================================
void KitroomProcessor::resetKeys()
{
    for (int s = 0; s < kr::kNumSlots; ++s)
    {
        const auto& arts = kr::slots()[(size_t) s].articulations;
        for (int a = 0; a < kr::kMaxArticulations; ++a)
            primaryKeys[(size_t) s][(size_t) a] = a < (int) arts.size() ? arts[(size_t) a].keys.front() : -1;
    }
    rebuildNoteMap();
}

void KitroomProcessor::setKey (int slot, int art, int note)
{
    // A key drives exactly one articulation: take it away from whoever had it.
    for (auto& s : primaryKeys)
        for (auto& k : s)
            if (k == note)
                k = -1;
    primaryKeys[(size_t) slot][(size_t) art] = note;
    rebuildNoteMap();
}

void KitroomProcessor::rebuildNoteMap()
{
    std::array<int, 128> map;
    map.fill (-1);
    for (int s = 0; s < kr::kNumSlots; ++s)
        for (int a = 0; a < kr::kMaxArticulations; ++a)
            if (const int k = primaryKeys[(size_t) s][(size_t) a]; juce::isPositiveAndBelow (k, 128))
                map[(size_t) k] = kr::packTarget (s, a);

    // General MIDI aliases (e.g. 35 = kick, 50 = tom 1) only where the key is still free.
    for (int s = 0; s < kr::kNumSlots; ++s)
    {
        const auto& arts = kr::slots()[(size_t) s].articulations;
        for (int a = 0; a < (int) arts.size(); ++a)
            for (size_t i = 1; i < arts[(size_t) a].keys.size(); ++i)
                if (const int k = arts[(size_t) a].keys[i]; map[(size_t) k] < 0
                    && std::none_of (primaryKeys.begin(), primaryKeys.end(), [k] (auto& ks) { return std::find (ks.begin(), ks.end(), k) != ks.end(); }))
                    map[(size_t) k] = kr::packTarget (s, a);
    }

    for (int i = 0; i < 128; ++i)
        noteMap[(size_t) i].store (map[(size_t) i]);
}

void KitroomProcessor::pollLearn()
{
    const int target = learnTarget.load();
    const int note = learnedNote.exchange (-1);
    if (target >= 0 && note >= 0)
    {
        learnTarget.store (-1);
        setKey (kr::targetSlot (target), kr::targetArt (target), note);
    }
}

void KitroomProcessor::audition (int slot, int art, float velocity)
{
    const int packed = (kr::packTarget (slot, art) << 8) | juce::jlimit (1, 127, juce::roundToInt (velocity * 127.0f));
    const auto w = auditionFifo.write (1);
    if (w.blockSize1 > 0) auditionQueue[(size_t) w.startIndex1] = packed;
    else if (w.blockSize2 > 0) auditionQueue[(size_t) w.startIndex2] = packed;
}

//==============================================================================
bool KitroomProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void KitroomProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    engine.prepare (sr);
    for (auto& f : sideLowCut)
    {
        f.set (140.0f, 0.7071f, sr);
        f.reset();
    }
    glue.prepare (sr);
    dirt.prepare (sr, kChunk);
    crusher.reset();
    for (auto& f : toneShelf) f.reset();
    lastTone = -9.0f;
    clipper.prepare (sr, kChunk);
    outGainSm.reset (sr, 0.03);
    widthSm.reset (sr, 0.03);
    outGainSm.setCurrentAndTargetValue (dd::dbToGain (params.master (MP::Output)));
    widthSm.setCurrentAndTargetValue (params.master (MP::Width));
    setLatencySamples (dirt.getLatency() + clipper.getLatency());
}

void KitroomProcessor::handleNote (int note, float velocity)
{
    if (learnTarget.load() >= 0)
        learnedNote.store (note);

    const int target = noteMap[(size_t) note].load();
    if (target < 0)
        return;
    const int slot = kr::targetSlot (target), art = kr::targetArt (target);
    if (art >= (int) kr::slots()[(size_t) slot].articulations.size())
        return;

    engine.trigger (slot, art, velocity, loader->get (slot), mixes[(size_t) slot],
                    params.master (MP::VelCurve), params.master (MP::Humanize));
    float old = hitLevels[(size_t) slot].load();
    while (velocity > old && ! hitLevels[(size_t) slot].compare_exchange_weak (old, velocity)) {}
}

void KitroomProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Offline renders (FL export, the QA harness) must not start before the samples are in memory.
    if (isNonRealtime())
        loader->waitUntilIdle();

    for (int s = 0; s < kr::kNumSlots; ++s)
    {
        auto& m = mixes[(size_t) s];
        const float vol = params.slot (s, SP::Volume);
        m.volume = vol <= -47.9f ? 0.0f : dd::dbToGain (vol);
        // Pan offsets the close mic from where the piece already sits in the overheads.
        const auto* sound = loader->get (s);
        const float pan = juce::jlimit (-1.0f, 1.0f, params.slot (s, SP::Pan) + (sound != nullptr ? sound->closePan : 0.0f));
        const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        m.panL = std::cos (angle) * juce::MathConstants<float>::sqrt2;
        m.panR = std::sin (angle) * juce::MathConstants<float>::sqrt2;
        m.tuneRatio = std::exp2 (params.slot (s, SP::Tune) / 12.0f);
        m.decay = params.slot (s, SP::Decay);
        m.close = params.slot (s, SP::Close) * params.master (MP::Close);
        m.overheads = params.slot (s, SP::Overheads) * params.master (MP::Overheads);
        m.room = params.slot (s, SP::Room) * params.master (MP::Room);
    }
    outGainSm.setTargetValue (dd::dbToGain (params.master (MP::Output)));
    widthSm.setTargetValue (params.master (MP::Width));

    // Notes from the UI (clicking a drum) join the host's MIDI at the start of the block.
    {
        const auto rd = auditionFifo.read (auditionFifo.getNumReady());
        auto play = [this] (int packed)
        {
            const int target = packed >> 8;
            engine.trigger (kr::targetSlot (target), kr::targetArt (target), (float) (packed & 0xff) / 127.0f,
                            loader->get (kr::targetSlot (target)), mixes[(size_t) kr::targetSlot (target)],
                            params.master (MP::VelCurve), 0.0f);
            hitLevels[(size_t) kr::targetSlot (target)].store ((float) (packed & 0xff) / 127.0f);
        };
        for (int i = 0; i < rd.blockSize1; ++i) play (auditionQueue[(size_t) (rd.startIndex1 + i)]);
        for (int i = 0; i < rd.blockSize2; ++i) play (auditionQueue[(size_t) (rd.startIndex2 + i)]);
    }

    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    std::array<float, kChunk> monoScratch {};

    auto it = midi.cbegin();
    for (int pos = 0; pos < numSamples; pos += kChunk)
    {
        const int n = std::min (kChunk, numSamples - pos);
        float* cl = L + pos;
        float* cr = R != nullptr ? R + pos : monoScratch.data();
        if (R == nullptr)
            std::fill (monoScratch.begin(), monoScratch.end(), 0.0f);

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
                const auto m = meta.getMessage();
                if (m.isNoteOn())
                    handleNote (m.getNoteNumber(), m.getFloatVelocity());
                else if (m.isAllSoundOff())
                    engine.reset();
            }
            engine.render (cl + done, cr + done, segEnd - done, mixes);
            done = segEnd;
        }

        processChunk (cl, cr, n);
        if (R == nullptr)
            for (int i = 0; i < n; ++i)
                cl[i] = 0.5f * (cl[i] + cr[i]);
    }

    scope.push (L, R, numSamples);
}

void KitroomProcessor::processChunk (float* L, float* R, int n)
{
    const float glueAmt = params.master (MP::Glue);
    for (int i = 0; i < n; ++i)
    {
        float l = L[i], r = R[i];
        const float w = widthSm.getNextValue();
        const float mid = 0.5f * (l + r), side = sideLowCut[1].hp (sideLowCut[0].hp (0.5f * (l - r))) * w;
        l = mid + side;
        r = mid - side;
        glue.process (l, r, glueAmt);
        L[i] = l;
        R[i] = r;
    }

    float* chans[2] = { L, R };
    juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);

    // Industrial stage: distortion (the kick's sub partly kept clean), lo-fi crush, then a tilt.
    {
        const float drive = params.master (MP::Drive);
        dd::Dirt::Params dp;
        dp.type = (dd::DirtType) juce::jlimit (0, 7, (int) std::lround (params.master (MP::DriveType)));
        dp.drive = drive;
        dp.bias = 0.3f;
        dp.mix = std::min (1.0f, drive * 4.0f);   // fades in, so Drive at 0 is the clean kit
        dp.subKeep = 0.35f;
        dp.xoverHz = 100.0f;
        dirt.process (block, dp);
    }
    if (const float crush = params.master (MP::Crush); crush > 0.001f)
        for (int i = 0; i < n; ++i)
            crusher.process (L[i], R[i], crush);
    if (const float tone = params.master (MP::Tone); std::abs (tone - lastTone) > 1.0e-3f)
    {
        lastTone = tone;
        for (auto& f : toneShelf)
            f.set (dd::Biquad::Type::HighShelf, 1400.0f, 0.7071f, tone * 9.0f, sr);
    }
    if (std::abs (lastTone) > 0.005f)
        for (int i = 0; i < n; ++i)
        {
            L[i] = toneShelf[0].process (L[i]);
            R[i] = toneShelf[1].process (R[i]);
        }

    clipper.process (block, params.master (MP::Clip));

    for (int i = 0; i < n; ++i)
    {
        const float g = outGainSm.getNextValue();
        L[i] *= g;
        R[i] *= g;
        if (! std::isfinite (L[i]) || ! std::isfinite (R[i]))
        {
            std::fill (L, L + n, 0.0f);
            std::fill (R, R + n, 0.0f);
            engine.reset();
            glue.reset();
            dirt.reset();
            clipper.reset();
            for (auto& f : sideLowCut)
                f.reset();
            for (auto& f : toneShelf)
                f.reset();
            return;
        }
    }
}

//==============================================================================
int KitroomProcessor::getNumPrograms()    { return std::max (1, presets.getNumFactoryPresets()); }
int KitroomProcessor::getCurrentProgram() { return juce::jmax (0, presets.getCurrentIndex()); }

void KitroomProcessor::setCurrentProgram (int index)
{
    if (juce::isPositiveAndBelow (index, presets.getNumFactoryPresets()) && index != presets.getCurrentIndex())
        presets.loadPreset (index);
}

const juce::String KitroomProcessor::getProgramName (int index)
{
    if (! juce::isPositiveAndBelow (index, presets.getNumFactoryPresets()))
        return "Init";
    return presets.get (index).category + " - " + presets.get (index).name;
}

void KitroomProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    presets.writeState (state);
    state.setProperty ("uiScale", editorScale, nullptr);
    state.setProperty ("kit", juce::JSON::toString (captureKit(), true), nullptr);

    juce::Array<juce::var> keys;
    for (auto& s : primaryKeys)
        for (int k : s)
            keys.add (k);
    state.setProperty ("keys", juce::JSON::toString (juce::var (keys), true), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void KitroomProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    dd::replaceStateExactly (apvts, tree);
    presets.readState (tree);
    editorScale = juce::jlimit (0.6f, 1.8f, (float) tree.getProperty ("uiScale", 1.0f));
    applyKit (juce::JSON::parse (tree.getProperty ("kit").toString()));

    const auto keys = juce::JSON::parse (tree.getProperty ("keys").toString());
    if (auto* arr = keys.getArray(); arr != nullptr && arr->size() == kr::kNumSlots * kr::kMaxArticulations)
    {
        for (int i = 0; i < arr->size(); ++i)
            primaryKeys[(size_t) (i / kr::kMaxArticulations)][(size_t) (i % kr::kMaxArticulations)] = (int) (*arr)[i];
        rebuildNoteMap();
    }
}

juce::AudioProcessorEditor* KitroomProcessor::createEditor() { return new KitroomEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new KitroomProcessor(); }
