#include "PluginProcessor.h"
#include "PluginEditor.h"

#if DD_HAS_PRESET_DATA
 #include "PresetData.h"
#endif

using sg::P;

namespace
{
/** Macro bend for a 0..1 amount (as Afterglow): m = 0 keeps the preset's value, m < 0 scales it toward 0,
    m > 0 moves it toward `ceiling`. */
float bend01 (float v, float m, float ceiling) noexcept { return m >= 0.0f ? v + m * (ceiling - v) : v * (1.0f + m); }
} // namespace

const juce::StringArray& SubGritProcessor::categoryOrder()
{
    static const juce::StringArray order { "TRAVIS", "MIKE DEAN", "UNTILJAPAN", "YE", "YEEZUS", "MOSHC0RE", "KING", "TRIP HOP", "SAFE", "80s", "2000s", "BOUNCE 808", "SOFT", "UNIQUE" };
    return order;
}

SubGritProcessor::SubGritProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SubGrit", sg::createLayout()),
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

    // A fresh instance should sound like a finished preset, not a raw init patch.
    if (presets.getNumFactoryPresets() > 0)
        presets.loadPreset (0);
}

bool SubGritProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void SubGritProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    engine.prepare (sr);
    lfo.prepare (sr);
    dirt.prepare (sr, kChunk);
    chorus.prepare (sr);
    delay.prepare (sr, 3.0f);
    space.prepare (sr);
    glue.prepare (sr);
    clipper.prepare (sr, kChunk);
    for (auto* dcs : { preClipDc, outDc })
        for (int ch = 0; ch < 2; ++ch) { dcs[ch].prepare (sr); dcs[ch].reset(); }

    for (int ch = 0; ch < 2; ++ch)
    {
        subsonic[ch].set (dd::Biquad::Type::HighPass, 22.0f, 0.707f, 0.0f, sr);
        subsonic[ch].reset();
        eqSub[ch].reset(); eqMid[ch].reset(); eqHigh[ch].reset();
        fxSplit[ch].set (200.0f, sr);
        fxSplit[ch].reset();
    }
    sideSplit.set (200.0f, sr);
    sideSplit.reset();
    std::fill (std::begin (lastEq), std::end (lastEq), -999.0f);

    for (auto* s : { &outGainSm, &widthSm, &delayMixSm, &spaceMixSm, &chorusMixSm })
        s->reset (sr, 0.03);
    outGainSm.setCurrentAndTargetValue (dd::dbToGain (params.get (P::outGain)));
    widthSm.setCurrentAndTargetValue (params.get (P::width));
    delayMixSm.setCurrentAndTargetValue (params.get (P::delayMix));
    spaceMixSm.setCurrentAndTargetValue (params.get (P::spaceMix));
    chorusMixSm.setCurrentAndTargetValue (params.get (P::chorusMix));

    delay.setTime ((float) (dd::divisionBeats (params.getInt (P::delayDiv)) * 60.0 / 120.0 * sr), true);

    setLatencySamples (dirt.getLatency() + clipper.getLatency());
}

float SubGritProcessor::glideMs() const noexcept
{
    // Slide: none at -1, the preset's at 0, a long 600 ms slide at +1.
    return bend01 (params.get (P::glide), params.get (P::mGlide), 600.0f);
}

sg::VoiceParams SubGritProcessor::buildVoiceParams() const
{
    // The simple panel's macros bend the preset around itself; at 0 every value is exactly the preset's.
    const float boom = params.get (P::mBoom), punch = params.get (P::mPunch), grit = params.get (P::mGrit);
    const float tone = params.get (P::mTone), length = params.get (P::mLength);

    sg::VoiceParams v;
    v.pitchShift = (float) params.getInt (P::octave) * 12.0f + (float) params.getInt (P::tune) + params.get (P::fine) * 0.01f;
    v.bend = engine.getBend();
    const float glide = glideMs();
    v.glideCoef = glide > 0.5f ? dd::onePoleCoef (glide * 0.001f / 3.0f, sr) : 0.0f;
    // Punch: the pitch drop and knock at the start of each note, and how fast it speaks.
    const float pitchPunch = params.get (P::pitchAmt);
    v.punchAmt = punch >= 0.0f ? pitchPunch + punch * ((pitchPunch < 0.0f ? -18.0f : 18.0f) - pitchPunch) * 0.7f
                               : pitchPunch * (1.0f + punch);
    v.punchTimeMs = params.get (P::pitchTime);
    v.diveAmt = params.get (P::diveAmt);
    v.diveTimeMs = params.get (P::diveTime);
    v.velocitySens = params.get (P::velocity);
    v.drift = params.get (P::drift);

    // Boom: more is a fuller, louder 808; less keeps the 808 audible but thinner (lower and rounder
    // harmonics give way: less level, more drive so it still reads on small speakers).
    v.bodyLevel = boom >= 0.0f ? bend01 (params.get (P::bodyLevel), boom, 1.0f) : params.get (P::bodyLevel) * (1.0f + 0.45f * boom);
    // Grit and brightness both add the 808's harmonics; thinning the boom does too.
    const float bodyHarm = std::max ({ grit, 0.0f }) + std::max (0.0f, tone) * 0.5f + std::max (0.0f, -boom) * 0.5f;
    v.bodyDrive = grit < 0.0f ? params.get (P::bodyDrive) * (1.0f + grit) : bend01 (params.get (P::bodyDrive), std::min (1.0f, bodyHarm), 0.85f);
    v.clickLevel = bend01 (params.get (P::clickLevel), punch, 0.7f);
    v.clickTone = params.get (P::clickTone);

    const P oscIds[2][8] = {
        { P::oscALevel, P::oscAWave, P::oscAOct, P::oscASemi, P::oscAFine, P::oscAVoices, P::oscADetune, P::oscAWidth },
        { P::oscBLevel, P::oscBWave, P::oscBOct, P::oscBSemi, P::oscBFine, P::oscBVoices, P::oscBDetune, P::oscBWidth } };
    for (int o = 0; o < 2; ++o)
    {
        auto& op = v.osc[o];
        op.level = params.get (oscIds[o][0]);
        op.wave = (dd::Wave) params.getInt (oscIds[o][1]);
        op.offset = (float) params.getInt (oscIds[o][2]) * 12.0f + (float) params.getInt (oscIds[o][3]) + params.get (oscIds[o][4]) * 0.01f;
        op.voices = params.getInt (oscIds[o][5]);
        op.detune = params.get (oscIds[o][6]);
        op.width = params.get (oscIds[o][7]);
    }

    v.noiseLevel = params.get (P::noiseLevel);
    v.noiseColor = params.get (P::noiseColor);

    v.filterMode = params.getInt (P::filterMode);
    v.cutoff = juce::jlimit (20.0f, 20000.0f, params.get (P::cutoff) * std::exp2 (tone * 2.5f));
    v.resonance = params.get (P::resonance);
    v.filterDrive = bend01 (params.get (P::filterDrive), grit, 0.9f);
    v.filterEnv = params.get (P::filterEnv);
    v.keyTrack = params.get (P::keyTrack);
    v.bodyToFilter = params.getBool (P::bodyToFilter);

    v.fA = params.get (P::fAttack); v.fD = params.get (P::fDecay); v.fS = params.get (P::fSustain); v.fR = params.get (P::fRelease);
    v.aA = params.get (P::aAttack); v.aD = params.get (P::aDecay); v.aS = params.get (P::aSustain); v.aR = params.get (P::aRelease);
    // Punch below 0 softens the attack (up to 16x slower), above 0 sharpens it.
    v.aA = juce::jlimit (0.1f, 5000.0f, punch < 0.0f ? v.aA * std::exp2 (-punch * 4.0f) : v.aA * (1.0f - 0.8f * punch));
    // Length: shorter or longer notes (decay, release, and a little more sustain when longer).
    v.aD = juce::jlimit (5.0f, 12000.0f, v.aD * std::exp2 (length * 2.5f));
    v.aR = juce::jlimit (5.0f, 8000.0f, v.aR * std::exp2 (length * 2.0f));
    v.aS = bend01 (v.aS, std::max (0.0f, length) * 0.4f, 1.0f);

    v.lfoPitch = params.get (P::lfoPitch);
    v.lfoCutoff = params.get (P::lfoCutoff);
    v.lfoAmp = params.get (P::lfoAmp);
    return v;
}

void SubGritProcessor::updateFxSettings (double bpm)
{
    // Boom moves the sub shelf; Tone opens a presence lift when bright and closes the high cut when dark.
    const float boom = params.get (P::mBoom), tone = params.get (P::mTone);
    const float eq[4] = { juce::jlimit (-12.0f, 12.0f, params.get (P::eqSub) + boom * (boom >= 0.0f ? 6.0f : 9.0f)), params.get (P::eqMidFreq),
                          juce::jlimit (-12.0f, 12.0f, params.get (P::eqMidGain) + std::max (0.0f, tone) * 5.0f),
                          juce::jlimit (500.0f, 20000.0f, std::min (20000.0f, params.get (P::eqHighCut)) * std::exp2 (std::min (0.0f, tone) * 3.5f)) };
    if (! std::equal (std::begin (eq), std::end (eq), std::begin (lastEq)))
    {
        std::copy (std::begin (eq), std::end (eq), std::begin (lastEq));
        for (int ch = 0; ch < 2; ++ch)
        {
            eqSub[ch].set (dd::Biquad::Type::LowShelf, 70.0f, 0.7f, eq[0], sr);
            eqMid[ch].set (dd::Biquad::Type::Peak, eq[1], 0.9f, eq[2], sr);
            eqHigh[ch].set (dd::Biquad::Type::LowPass, std::min (eq[3], (float) (sr * 0.45)), 0.707f, 0.0f, sr);
        }
    }

    const double beatSec = 60.0 / bpm;
    delay.setTime ((float) std::min (2.9 * sr, dd::divisionBeats (params.getInt (P::delayDiv)) * beatSec * sr));
    delay.setTone (params.get (P::delayTone));
    space.setSize (params.get (P::spaceSize));

    outGainSm.setTargetValue (dd::dbToGain (params.get (P::outGain)));
    widthSm.setTargetValue (params.get (P::width));
    delayMixSm.setTargetValue (params.get (P::delayMix));
    spaceMixSm.setTargetValue (params.get (P::spaceMix));
    chorusMixSm.setTargetValue (params.get (P::chorusMix));
}

void SubGritProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                bpm = juce::jlimit (30.0, 400.0, *b);

    sg::SynthEngine::Settings es;
    // Poly Keys plays chords on any preset; off, the preset's own mode applies (mono 808 slides).
    es.mono = params.getInt (P::voiceMode) == 0 && ! params.getBool (P::perfPoly);
    es.legato = params.getBool (P::legato);
    es.glideAlways = params.getInt (P::glideMode) == 1;
    es.glideMs = glideMs();
    es.bendRange = (float) params.getInt (P::bendRange);
    engine.setSettings (es);

    updateFxSettings (bpm);
    lfo.setFade (params.get (P::lfoFade));

    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();
    buffer.clear();

    float* L = buffer.getWritePointer (0);
    float* R = numCh > 1 ? buffer.getWritePointer (1) : nullptr;

    // Mono output: render stereo into scratch, then fold down.
    std::array<float, kChunk> monoScratch {};

    auto vp = buildVoiceParams();
    auto midiIt = midi.cbegin();
    const auto midiEnd = midi.cend();

    for (int pos = 0; pos < numSamples; pos += kChunk)
    {
        const int n = std::min (kChunk, numSamples - pos);
        float* cl = L + pos;
        float* cr = R != nullptr ? R + pos : monoScratch.data();
        if (R == nullptr)
            std::fill (monoScratch.begin(), monoScratch.end(), 0.0f); // voices add into it

        int done = 0;
        while (done < n)
        {
            int segEnd = n;
            while (midiIt != midiEnd)
            {
                const auto meta = *midiIt;
                const int t = juce::jlimit (0, numSamples - 1, meta.samplePosition) - pos;
                if (t >= n)
                    break;
                if (t > done) { segEnd = t; break; }
                vp.bend = engine.getBend();
                engine.handleMidi (meta.getMessage(), vp);
                if (meta.getMessage().isNoteOn())
                    lastNote.store (meta.getMessage().getNoteNumber(), std::memory_order_relaxed);
                if (meta.getMessage().isPitchWheel())
                    vp.bend = engine.getBend();
                ++midiIt;
            }

            if (engine.takeFreshNoteFlag())
                lfo.retrigger();

            const int segLen = segEnd - done;
            const float rate = params.getBool (P::lfoSync)
                                   ? (float) ((bpm / 60.0) / dd::divisionBeats (params.getInt (P::lfoDiv)))
                                   : params.get (P::lfoRate);
            const auto shape = (dd::Lfo::Shape) params.getInt (P::lfoShape);
            for (int i = 0; i < segLen; ++i)
                lfoBuf[(size_t) (done + i)] = lfo.process (rate, shape);

            vp.bend = engine.getBend();
            engine.render (cl + done, cr + done, segLen, vp, lfoBuf.data() + done);
            done = segEnd;
        }

        processChunk (cl, cr, n, bpm);

        if (R == nullptr)
            for (int i = 0; i < n; ++i)
                cl[i] = 0.5f * (cl[i] + cr[i]);
    }

    scope.push (L, R, numSamples);
}

void SubGritProcessor::processChunk (float* L, float* R, int n, double)
{
    // Dirt (4x oversampled, multiband)
    {
        float* chans[2] = { L, R };
        juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);

        dd::Dirt::Params dp;
        dp.type = (dd::DirtType) params.getInt (P::dirtType);
        const float grit = params.get (P::mGrit);
        dp.drive = bend01 (params.get (P::dirtDrive), grit, 1.0f);
        dp.tone = juce::jlimit (-1.0f, 1.0f, params.get (P::dirtTone) + params.get (P::mTone) * 0.5f);
        dp.bias = params.get (P::dirtBias);
        dp.mix = bend01 (params.get (P::dirtMix), grit, 1.0f);
        // More Boom keeps more of the sub clean under the distortion.
        dp.subKeep = bend01 (params.get (P::subKeep), std::max (0.0f, params.get (P::mBoom)), 1.0f);
        dp.xoverHz = params.get (P::xover);

        // LFO > Dirt uses the chunk's mean LFO value (the Dirt smooths drive internally).
        float lfoMean = 0.0f;
        for (int i = 0; i < n; ++i) lfoMean += lfoBuf[(size_t) i];
        lfoMean /= (float) n;
        dirt.process (block, dp, params.get (P::lfoDirt) * 0.5f * lfoMean);
    }

    const float chorusRate = params.get (P::chorusRate);
    const float chorusDepth = params.get (P::chorusDepth);
    const float feedback = params.get (P::delayFeedback);
    const bool pingPong = params.getBool (P::delayPingPong);
    const float glueAmt = params.get (P::glue);

    for (int i = 0; i < n; ++i)
    {
        float l = L[i], r = R[i];

        l = eqHigh[0].process (eqMid[0].process (eqSub[0].process (subsonic[0].process (l))));
        r = eqHigh[1].process (eqMid[1].process (eqSub[1].process (subsonic[1].process (r))));

        // The mid (where the 808 lives) passes untouched, so note onsets keep their exact shape.
        // Only the side signal is split: below 200 Hz it is removed (mono sub), above it is scaled by Width.
        const float mid = 0.5f * (l + r);
        const float side = sideSplit.highOnly (0.5f * (l - r)) * widthSm.getNextValue();

        // Chorus / delay / space are sends fed from the band above the sub.
        const float hiL = fxSplit[0].highOnly (l);
        const float hiR = fxSplit[1].highOnly (r);
        float cl, cr, dl, dr, sl, sr_;
        const float cm = chorusMixSm.getNextValue();
        chorus.process (hiL, hiR, cl, cr, chorusRate, chorusDepth);
        delay.process (hiL + cl * cm, hiR + cr * cm, dl, dr, feedback, pingPong);
        space.process (hiL + dl * 0.4f, hiR + dr * 0.4f, sl, sr_);

        const float dm = delayMixSm.getNextValue();
        const float sm = spaceMixSm.getNextValue();
        l = mid + side + cl * cm * 0.9f + dl * dm + sl * sm;
        r = mid - side + cr * cm * 0.9f + dr * dm + sr_ * sm;

        glue.process (l, r, glueAmt);

        // Asymmetric curves leave DC; strip it before the (symmetric) clipper so peaks stay under its ceiling.
        L[i] = preClipDc[0].process (l);
        R[i] = preClipDc[1].process (r);
    }

    {
        float* chans[2] = { L, R };
        juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
        clipper.process (block, bend01 (params.get (P::clip), params.get (P::mGrit) * 0.5f, 0.8f));
    }

    bool bad = false;
    for (int i = 0; i < n; ++i)
    {
        // Clipping a lopsided waveform creates a little DC of its own; remove that too.
        const float g = outGainSm.getNextValue();
        L[i] = outDc[0].process (L[i]) * g;
        R[i] = outDc[1].process (R[i]) * g;
        bad = bad || dd::isBad (L[i]) || dd::isBad (R[i]);
    }

    if (bad)
    {
        // Never let a NaN reach the host; reset everything and output silence for this chunk.
        std::fill (L, L + n, 0.0f);
        std::fill (R, R + n, 0.0f);
        dirt.reset(); chorus.reset(); delay.reset(); space.reset(); clipper.reset(); glue.reset();
        for (int ch = 0; ch < 2; ++ch) { eqSub[ch].reset(); eqMid[ch].reset(); eqHigh[ch].reset(); subsonic[ch].reset(); fxSplit[ch].reset(); }
        sideSplit.reset();
        engine.reset();
    }
}

int SubGritProcessor::getNumPrograms()
{
    return std::max (1, presets.getNumFactoryPresets());
}

int SubGritProcessor::getCurrentProgram()
{
    const int i = presets.getCurrentIndex();
    return juce::isPositiveAndBelow (i, presets.getNumFactoryPresets()) ? i : 0;
}

void SubGritProcessor::setCurrentProgram (int index)
{
    // Some hosts re-send the current program after restoring a project; reloading it then would
    // wipe the user's tweaks, so only an actual change of program loads the preset.
    if (juce::isPositiveAndBelow (index, presets.getNumFactoryPresets()) && index != presets.getCurrentIndex())
        presets.loadPreset (index);
}

const juce::String SubGritProcessor::getProgramName (int index)
{
    if (! juce::isPositiveAndBelow (index, presets.getNumFactoryPresets()))
        return "Init";
    const auto& p = presets.get (index);
    return p.category + " - " + p.name;
}

void SubGritProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    presets.writeState (state);
    state.setProperty ("uiScale", editorScale, nullptr);
    state.setProperty ("allKnobs", allKnobs, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SubGritProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            dd::replaceStateExactly (apvts, tree);
            presets.readState (tree);
            editorScale = juce::jlimit (0.6f, 1.8f, (float) tree.getProperty ("uiScale", 1.0f));
            allKnobs = (bool) tree.getProperty ("allKnobs", false);
            dd::keepOldDefault (apvts, tree, "perfPoly", 0.0f);
        }
    }
}

juce::AudioProcessorEditor* SubGritProcessor::createEditor()
{
    return new SubGritEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SubGritProcessor();
}
