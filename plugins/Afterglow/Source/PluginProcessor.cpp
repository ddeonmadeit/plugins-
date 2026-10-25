#include "PluginProcessor.h"
#include "PluginEditor.h"

#if DD_HAS_PRESET_DATA
 #include "PresetData.h"
#endif

using ag::P;

namespace
{
/** Macro bend for a 0..1 amount: m = 0 keeps the preset's value, m < 0 scales it toward 0, m > 0 moves it toward `ceiling`. */
float bend01 (float v, float m, float ceiling) noexcept { return m >= 0.0f ? v + m * (ceiling - v) : v * (1.0f + m); }

/** Motion macro as a depth multiplier: still at -1, as designed at 0, three times the movement at +1. */
float motionScale (float m) noexcept { return m < 0.0f ? 1.0f + m : 1.0f + 2.0f * m; }
} // namespace

const juce::StringArray& AfterglowProcessor::categoryOrder()
{
    static const juce::StringArray order { "TAME IMPALA", "MASSIVE ATTACK", "BRENT FAIYAZ", "MIKE DEAN", "TRAVIS SCOTT", "USER" };
    return order;
}

AfterglowProcessor::AfterglowProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Afterglow", ag::createLayout()),
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

bool AfterglowProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void AfterglowProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    engine.prepare (sr);
    lfo2.prepare (sr);
    tape.prepare (sr);
    crusher.reset();
    ensemble.prepare (sr);
    phaser.prepare (sr);
    delay.prepare (sr, 4.0f);
    reverb.prepare (sr);
    reverb.reset();
    shimmerShift.prepare (sr);
    shimmerLp.setCutoff (6000.0f, sr);
    shimmerLp.reset();
    shimmerFb = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
    {
        sideLowCut[ch].set (200.0f, 0.7071f, sr);
        sideLowCut[ch].reset();
        lowCut[ch].reset();
        highCut[ch].reset();
        tilt[ch].reset();
        reverbTone[ch].reset();
    }
    std::fill (std::begin (lastEq), std::end (lastEq), -99.0f);
    glue.prepare (sr);
    clipper.prepare (sr, kChunk);

    for (auto* s : { &outGainSm, &widthSm, &ensMixSm, &phaserMixSm, &delayMixSm, &reverbMixSm, &pumpDepthSm })
        s->reset (sr, 0.05);
    outGainSm.setCurrentAndTargetValue (dd::dbToGain (params.get (P::outGain)));
    widthSm.setCurrentAndTargetValue (params.get (P::width));
    ensMixSm.setCurrentAndTargetValue (params.get (P::ensMix));
    phaserMixSm.setCurrentAndTargetValue (params.get (P::phaserMix));
    delayMixSm.setCurrentAndTargetValue (params.get (P::delayMix));
    reverbMixSm.setCurrentAndTargetValue (params.get (P::reverbMix));
    pumpDepthSm.setCurrentAndTargetValue (params.get (P::pumpDepth));

    delay.setTime ((float) (dd::divisionBeats (params.getInt (P::delayDiv)) * 0.5 * sr), true);
    pumpGain = 1.0f;
    freeBeat = hostBeat = 0.0;
    levelFollow = 0.0f;
    setLatencySamples (clipper.getLatency());
}

ag::VoiceParams AfterglowProcessor::buildVoiceParams (float lfo2v, double bpm) const
{
    ag::VoiceParams v;
    const float tone = params.get (P::mTone), grit = params.get (P::mGrit), blend = params.get (P::mBlend);
    const float k = motionScale (params.get (P::mMotion));
    const float swell = std::exp2 (params.get (P::mSwell) * 3.0f), tail = std::exp2 (params.get (P::mTail) * 2.5f);

    for (int l = 0; l < 2; ++l)
    {
        auto get = [&] (P aParam) { return params.get (ag::layerParam (l, aParam)); };
        auto& lp = v.layer[(size_t) l];
        lp.on = get (P::aOn) >= 0.5f;
        lp.source = (ag::Source) juce::jlimit (0, 6, (int) std::lround (get (P::aSource)));
        float level = get (P::aLevel);
        if (l == 0 && blend > 0.0f) level *= 1.0f - blend;
        if (l == 1 && blend < 0.0f) level *= 1.0f + blend;
        const float swap = std::min (1.0f, params.get (P::lfo2Blend) * k);
        level *= 1.0f - swap * 0.5f * (1.0f + (l == 0 ? lfo2v : -lfo2v));
        lp.level = level;
        lp.offset = std::round (get (P::aOct)) * 12.0f + std::round (get (P::aSemi)) + get (P::aFine) * 0.01f;
        lp.voices = (int) std::lround (get (P::aVoices));
        lp.detune = get (P::aDetune);
        lp.spread = get (P::aSpread);
        lp.shape = get (P::aShape);
        lp.character = get (P::aChar);
        lp.shapeEnv = get (P::aShapeEnv);
        lp.shapeOffset = lfo2v * params.get (P::lfo2Morph) * k * 0.5f;
        lp.pan = get (P::aPan);
        lp.drift = std::min (1.0f, get (P::aDrift) * k);
    }

    v.noiseLevel = params.get (P::noiseLevel);
    v.noiseType = params.getInt (P::noiseType);
    v.noiseTone = params.get (P::noiseTone);

    v.filterType = params.getInt (P::filterType);
    v.cutoff = params.get (P::cutoff) * std::exp2 (tone * 2.5f);
    v.reso = params.get (P::reso);
    v.filterDrive = bend01 (params.get (P::filterDrive), grit, 0.8f);
    v.filterEnv = params.get (P::filterEnv);
    v.keyTrack = params.get (P::keyTrack);
    v.velFilter = params.get (P::velFilter);

    v.aA = juce::jlimit (1.0f, 20000.0f, params.get (P::aAttack) * swell);
    v.aD = params.get (P::aDecay);
    v.aS = params.get (P::aSustain);
    v.aR = juce::jlimit (10.0f, 20000.0f, params.get (P::aRelease) * tail);
    v.mA = juce::jlimit (1.0f, 20000.0f, params.get (P::mAttack) * swell);
    v.mD = params.get (P::mDecay);
    v.mS = params.get (P::mSustain);
    v.mR = juce::jlimit (10.0f, 20000.0f, params.get (P::mRelease) * tail);
    v.velocity = params.get (P::velocity);

    const float glideMs = params.get (P::glide);
    v.glideCoef = glideMs > 0.5f ? dd::onePoleCoef (glideMs * 0.001f / 3.0f, sr) : 0.0f;
    v.bend = engine.getBend();

    v.lfo1Shape = (dd::Lfo::Shape) params.getInt (P::lfo1Shape);
    v.lfo1Rate = params.getBool (P::lfo1Sync) ? (float) ((bpm / 60.0) / dd::divisionBeats (params.getInt (P::lfo1Div)))
                                              : params.get (P::lfo1Rate);
    v.lfo1Fade = params.get (P::lfo1Delay);
    v.lfo1Pitch = params.get (P::lfo1Pitch) * k;
    v.lfo1Cutoff = params.get (P::lfo1Cutoff) * k;
    v.lfo1Amp = std::min (1.0f, params.get (P::lfo1Amp) * k);
    v.lfo1Pan = std::min (1.0f, params.get (P::lfo1Pan) * k);
    v.lfo1Morph = params.get (P::lfo1Morph) * k;
    v.globalCutoff = lfo2v * params.get (P::lfo2Cutoff) * k;
    return v;
}

void AfterglowProcessor::updateFx (double bpm)
{
    const float tone = params.get (P::mTone);
    const float eq[3] = { params.get (P::lowCut), juce::jlimit (1000.0f, 20000.0f, params.get (P::highCut) * std::exp2 (tone * 1.5f)), tone };
    if (! std::equal (std::begin (eq), std::end (eq), std::begin (lastEq)))
    {
        std::copy (std::begin (eq), std::end (eq), std::begin (lastEq));
        for (int ch = 0; ch < 2; ++ch)
        {
            lowCut[ch].set (dd::Biquad::Type::HighPass, eq[0], 0.7071f, 0.0f, sr);
            highCut[ch].set (dd::Biquad::Type::LowPass, std::min (eq[1], (float) sr * 0.45f), 0.7071f, 0.0f, sr);
            tilt[ch].set (dd::Biquad::Type::HighShelf, 2500.0f, 0.7071f, tone * 4.0f, sr);
        }
    }

    const float space = params.get (P::mSpace), tail = params.get (P::mTail);
    reverb.setSize (juce::jlimit (0.0f, 1.0f, bend01 (params.get (P::reverbSize), space * 0.5f, 1.0f) + tail * 0.12f));
    const float toneHz = 1500.0f * std::exp2 (params.get (P::reverbTone) * 3.5f);
    for (auto& f : reverbTone)
        f.setCutoff (std::min (toneHz, (float) sr * 0.45f), sr);
    delay.setTone (params.get (P::delayTone));
    delayBaseSamples = (float) (dd::divisionBeats (params.getInt (P::delayDiv)) * 60.0 / bpm * sr);

    outGainSm.setTargetValue (dd::dbToGain (params.get (P::outGain)));
    widthSm.setTargetValue (params.get (P::width));
    ensMixSm.setTargetValue (params.get (P::ensMix));
    phaserMixSm.setTargetValue (params.get (P::phaserMix));
    delayMixSm.setTargetValue (bend01 (params.get (P::delayMix), params.get (P::mEcho), 0.7f));
    reverbMixSm.setTargetValue (bend01 (params.get (P::reverbMix), space, 0.85f));
    pumpDepthSm.setTargetValue (params.get (P::pumpDepth));
}

void AfterglowProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    double bpm = 120.0;
    hostPlaying = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm())
                bpm = juce::jlimit (30.0, 400.0, *b);
            if (pos->getIsPlaying())
                if (auto ppq = pos->getPpqPosition())
                {
                    hostBeat = *ppq;
                    hostPlaying = true;
                }
        }
    if (! hostPlaying)
        hostBeat = freeBeat;

    ag::Engine::Settings es;
    es.mode = (ag::Engine::Mode) params.getInt (P::voiceMode);
    es.glideMs = params.get (P::glide);
    es.bendRange = (float) params.getInt (P::bendRange);
    engine.setSettings (es);
    updateFx (bpm);

    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();
    buffer.clear();
    float* L = buffer.getWritePointer (0);
    float* R = numCh > 1 ? buffer.getWritePointer (1) : nullptr;
    std::array<float, kChunk> monoScratch {};

    const float lfo2Rate = params.getBool (P::lfo2Sync) ? (float) ((bpm / 60.0) / dd::divisionBeats (params.getInt (P::lfo2Div)))
                                                        : params.get (P::lfo2Rate);
    const auto lfo2Shape = (dd::Lfo::Shape) params.getInt (P::lfo2Shape);
    auto midiIt = midi.cbegin();
    const auto midiEnd = midi.cend();
    float lfo2v = 0.0f;

    for (int pos = 0; pos < numSamples; pos += kChunk)
    {
        const int n = std::min (kChunk, numSamples - pos);
        float* cl = L + pos;
        float* cr = R != nullptr ? R + pos : monoScratch.data();
        if (R == nullptr)
            std::fill (monoScratch.begin(), monoScratch.end(), 0.0f);

        for (int i = 0; i < n; ++i)
            lfo2v = lfo2.process (lfo2Rate, lfo2Shape);
        auto vp = buildVoiceParams (lfo2v, bpm);

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
                engine.handleMidi (meta.getMessage(), vp);
                vp.bend = engine.getBend();
                ++midiIt;
            }
            engine.render (cl + done, cr + done, segEnd - done, vp);
            done = segEnd;
        }

        processChunk (cl, cr, n, bpm);
        hostBeat += (double) n * bpm / 60.0 / sr;

        if (R == nullptr)
            for (int i = 0; i < n; ++i)
                cl[i] = 0.5f * (cl[i] + cr[i]);
    }

    freeBeat = hostBeat;
    displayMotion.store (lfo2v);
    scope.push (L, R, numSamples);
}

void AfterglowProcessor::processChunk (float* L, float* R, int n, double bpm)
{
    const float k = motionScale (params.get (P::mMotion));
    const float grit = params.get (P::mGrit);

    if (params.getBool (P::tapeOn))
    {
        const float drive = bend01 (params.get (P::tapeDrive), grit, 1.0f);
        const float wow = std::min (1.0f, params.get (P::tapeWow) * k);
        const float flutter = std::min (1.0f, params.get (P::tapeFlutter) * k);
        for (int i = 0; i < n; ++i)
            tape.process (L[i], R[i], drive, wow, flutter);
    }

    const float lofi = bend01 (params.get (P::lofi), grit, 0.35f);
    if (lofi > 0.001f)
        for (int i = 0; i < n; ++i)
            crusher.process (L[i], R[i], lofi);

    if (params.getBool (P::ensOn))
    {
        const auto mode = (dd::Ensemble::Mode) params.getInt (P::ensMode);
        const float depth = std::min (2.5f, k);
        for (int i = 0; i < n; ++i)
        {
            float wl, wr;
            ensemble.process (L[i], R[i], wl, wr, mode, depth);
            const float m = ensMixSm.getNextValue();
            const float norm = 1.0f / (1.0f + 0.35f * m);
            L[i] = (L[i] + m * wl) * norm;
            R[i] = (R[i] + m * wr) * norm;
        }
    }

    if (params.getBool (P::phaserOn))
    {
        const float rate = params.get (P::phaserRate) * (0.6f + 0.4f * k);
        const float depth = std::min (1.0f, params.get (P::phaserDepth) * (0.5f + 0.5f * k));
        const float fb = params.get (P::phaserFeedback);
        for (int i = 0; i < n; ++i)
        {
            float wl, wr;
            phaser.process (L[i], R[i], wl, wr, rate, depth, fb);
            const float m = phaserMixSm.getNextValue() * 0.5f;
            L[i] += m * (wl - L[i]);
            R[i] += m * (wr - R[i]);
        }
    }

    if (params.getBool (P::delayOn))
    {
        delayModPhase += (double) n * 0.37 / sr;
        if (delayModPhase >= 1.0) delayModPhase -= 1.0;
        const float wobble = params.get (P::delayMod) * k * 0.004f * dd::sine01 ((float) delayModPhase);
        delay.setTime (delayBaseSamples * (1.0f + wobble));
        const float fb = bend01 (params.get (P::delayFeedback), params.get (P::mEcho) * 0.4f, 0.88f);
        const bool pingPong = params.getBool (P::delayPingPong);
        for (int i = 0; i < n; ++i)
        {
            float dl, dr;
            delay.process (L[i], R[i], dl, dr, fb, pingPong);
            const float m = delayMixSm.getNextValue();
            L[i] += dl * m;
            R[i] += dr * m;
        }
    }

    if (params.getBool (P::reverbOn))
    {
        const float shimmer = params.get (P::shimmer) * 0.55f;
        for (int i = 0; i < n; ++i)
        {
            float wl, wr;
            reverb.process (L[i] + shimmerFb, R[i] + shimmerFb, wl, wr);
            wl = reverbTone[0].lp (wl);
            wr = reverbTone[1].lp (wr);
            // Shimmer: the tail an octave up, fed back into the room so it keeps climbing.
            shimmerFb = shimmer > 0.0f ? shimmerLp.lp (shimmerShift.process (0.5f * (wl + wr))) * shimmer : 0.0f;
            const float m = reverbMixSm.getNextValue();
            L[i] = L[i] * (1.0f - 0.4f * m) + wl * m * 1.3f;
            R[i] = R[i] * (1.0f - 0.4f * m) + wr * m * 1.3f;
        }
    }

    if (params.getBool (P::pumpOn))
    {
        const double divBeats = dd::divisionBeats (params.getInt (P::pumpDiv));
        const double beatsPerSample = bpm / 60.0 / sr;
        const bool gate = params.getInt (P::pumpMode) == 1;
        const float smooth = dd::onePoleCoef (0.002f, sr);
        for (int i = 0; i < n; ++i)
        {
            const double beat = hostBeat + (double) i * beatsPerSample;
            const float phase = (float) (beat / divBeats - std::floor (beat / divBeats));
            const float depth = pumpDepthSm.getNextValue();
            float target;
            if (gate)
                target = phase < 0.5f ? 1.0f : 1.0f - depth;
            else
            {
                // Sidechain duck: a fast dip on the beat, then an exponential recovery.
                const float env = phase < 0.03f ? phase / 0.03f : std::exp (-(phase - 0.03f) * 5.5f);
                target = 1.0f - depth * env;
            }
            pumpGain = target + (pumpGain - target) * smooth;
            L[i] *= pumpGain;
            R[i] *= pumpGain;
        }
    }

    for (int i = 0; i < n; ++i)
    {
        float l = tilt[0].process (highCut[0].process (lowCut[0].process (L[i])));
        float r = tilt[1].process (highCut[1].process (lowCut[1].process (R[i])));

        // Width on mid/side, with the side high-passed so the low end is always mono.
        const float w = widthSm.getNextValue();
        const float mid = 0.5f * (l + r);
        const float side = sideLowCut[1].hp (sideLowCut[0].hp (0.5f * (l - r))) * w;
        l = mid + side;
        r = mid - side;
        glue.process (l, r, params.get (P::glue));
        L[i] = l;
        R[i] = r;
    }

    // Output gain goes before the oversampled safety clipper, so a bell or pluck attack that pokes over
    // -1 dBFS is shaved by a dB or two instead of the whole patch being turned down for it.
    for (int i = 0; i < n; ++i)
    {
        const float g = outGainSm.getNextValue();
        L[i] *= g;
        R[i] *= g;
    }
    float* chans[2] = { L, R };
    juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
    clipper.process (block, 0.0f);

    float sum = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        if (! std::isfinite (L[i]) || ! std::isfinite (R[i]))
        {
            std::fill (L, L + n, 0.0f);
            std::fill (R, R + n, 0.0f);
            engine.reset();
            tape.reset(); ensemble.reset(); phaser.reset(); delay.reset(); reverb.reset(); shimmerShift.reset();
            shimmerFb = 0.0f;
            glue.reset();
            clipper.reset();
            for (auto& f : sideLowCut) f.reset();
            return;
        }
        sum += L[i] * L[i] + R[i] * R[i];
    }
    const float rms = std::sqrt (sum / (2.0f * (float) n));
    levelFollow = std::max (rms, levelFollow * 0.97f);
    displayLevel.store (levelFollow);
}

int AfterglowProcessor::getNumPrograms()           { return std::max (1, presets.getNumFactoryPresets()); }
int AfterglowProcessor::getCurrentProgram()        { return std::max (0, presets.getCurrentIndex()); }
void AfterglowProcessor::setCurrentProgram (int i)
{
    // Hosts may re-send the current program after restoring a project; don't wipe the user's tweaks.
    if (juce::isPositiveAndBelow (i, presets.getNumFactoryPresets()) && i != presets.getCurrentIndex())
        presets.loadPreset (i);
}

const juce::String AfterglowProcessor::getProgramName (int i)
{
    if (! juce::isPositiveAndBelow (i, presets.getNumFactoryPresets()))
        return "Init";
    return presets.get (i).category + " - " + presets.get (i).name;
}

void AfterglowProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    presets.writeState (state);
    state.setProperty ("uiScale", editorScale, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AfterglowProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            dd::replaceStateExactly (apvts, tree);
            presets.readState (tree);
            editorScale = juce::jlimit (0.6f, 1.8f, (float) tree.getProperty ("uiScale", 1.0f));
        }
}

juce::AudioProcessorEditor* AfterglowProcessor::createEditor() { return new AfterglowEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AfterglowProcessor(); }
