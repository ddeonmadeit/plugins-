#include "PluginProcessor.h"
#include "PluginEditor.h"

#if DD_HAS_PRESET_DATA
 #include "PresetData.h"
#endif

using aircheck::P;

const juce::StringArray& AircheckProcessor::categoryOrder()
{
    static const juce::StringArray order { "DEON", "80S", "2000S", "TRAVIS SCOTT", "YE", "TAME IMPALA", "R&B", "TRIP HOP", "MOSHC0RE", "STUDIO", "PITCHED", "NEO SOUL", "SOUL" };
    return order;
}

AircheckProcessor::AircheckProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Aircheck", aircheck::createLayout()),
      presets (apvts, categoryOrder(), DD_PLUGIN_COMPANY, DD_PLUGIN_NAME)
{
    params.attach (apvts);
    // Presets saved before 1.5 (factory overrides, the user's own) may carry the tuner's old Transpose / Formant.
    presets.onApplyExtra = [this] (const juce::var&) { migrateLegacyPitch(); };
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

bool AircheckProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (layouts.getMainInputChannelSet() != out)
        return false;
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void AircheckProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    detector.prepare (sr);
    corrector.prepare (sr, detector.getHopSamples());
    psola.prepare (sr, detector.getDelaySamples());
    gate.prepare (sr);
    mic.prepare (sr);
    preamp.prepare (sr, kChunk);
    proEq.prepare (sr);
    rider.prepare (sr);
    comp.prepare (sr);
    smooth.prepare (sr);
    air.prepare (sr, kChunk);
    dimension.prepare (sr);
    limiter.prepare (sr);
    dryAlign.prepare (kChunk, comp.getLatency() + smooth.getLatency());
    deesser.prepare (sr);
    doubler.prepare (sr);
    vocoder.prepare (sr);
    tape.prepare (sr);
    delay.prepare (sr, 2.5f);
    reverb.prepare (sr);
    pitchCounter = 0;
    duckEnv = gateEnv = revGate = 0.0f;
    revHold = 0;
    duckCoefA = dd::onePoleCoef (0.005f, sr);
    duckCoefR = dd::onePoleCoef (0.25f, sr);

    readSettings();
    for (auto* s : { &inGainSm, &outGainSm, &mixSm, &tuneMixSm })
        s->reset (sr, 0.03);
    inGainSm.setCurrentAndTargetValue (dd::dbToGain (params.get (P::inGain)));
    outGainSm.setCurrentAndTargetValue (dd::dbToGain (params.get (P::outGain)));
    mixSm.setCurrentAndTargetValue (params.get (P::mix));
    tuneMixSm.setCurrentAndTargetValue (pitchActive ? 1.0f : 0.0f);
    delay.setTime ((float) (0.25 * sr), true);

    setLatencySamples (psola.getLatency() + (int) std::lround (preamp.getLatency() + air.getLatency())
                       + comp.getLatency() + smooth.getLatency() + limiter.getLatency());
}

void AircheckProcessor::readSettings()
{
    tuneOn = params.getBool (P::tuneOn);
    tune.key = params.getInt (P::tuneKey);
    tune.scale = params.getInt (P::tuneScale);
    tune.retuneMs = params.get (P::tuneSpeed);
    tune.humanize = params.get (P::tuneHumanize);
    tune.flex = params.get (P::tuneFlex);
    tune.vibrato = params.get (P::tuneVibrato);
    tune.amount = params.get (P::tuneAmount);
    // The Pitch Shift panel, plus the tuner's old Transpose / Formant (only non-zero in automation that
    // still drives them: sessions and presets are moved over by migrateLegacyPitch).
    tune.transpose = juce::jlimit (-12.0f, 12.0f, params.get (P::pitchShift) + (float) params.getInt (P::tuneTranspose));
    const float formantSemis = juce::jlimit (-7.0f, 7.0f, params.get (P::pitchFormant) + params.get (P::tuneFormant));
    formantRatio = dd::semitonesToRatio (formantSemis);
    pitchActive = tuneOn || std::abs (tune.transpose) > 0.005f || std::abs (formantSemis) > 0.01f;

    // The eight EQ bands (parameters b1On..b8Place, nine per band, in table order).
    for (int i = 0; i < aircheck::ProEq::kBands; ++i)
    {
        const int base = (int) P::b1On + i * 9;
        auto& b = bands[(size_t) i];
        b.on = params.getBool ((P) base);
        b.type = params.getInt ((P) (base + 1));
        b.hz = params.get ((P) (base + 2));
        b.db = params.get ((P) (base + 3));
        b.q = params.get ((P) (base + 4));
        b.slope = params.getInt ((P) (base + 5));
        b.dynRange = params.get ((P) (base + 6));
        b.dynThresh = params.get ((P) (base + 7));
        b.place = params.getInt ((P) (base + 8));
    }
    proEq.set (bands);

    smoothSettings.depth = params.get (P::smoothDepth);
    smoothSettings.sharpness = params.get (P::smoothSharp);
    smoothSettings.lowHz = params.get (P::smoothLow);
    smoothSettings.highHz = std::max (params.get (P::smoothHigh), smoothSettings.lowHz * 1.5f);

    compSettings.type = params.getInt (P::compType);
    compSettings.amount = params.get (P::compAmount);
    compSettings.attackMs = params.get (P::compAttack);
    compSettings.releaseMs = params.get (P::compRelease);
    compSettings.ratio = params.get (P::compRatio);
    compSettings.mix = params.get (P::compMix);
    compSettings.scHpf = params.get (P::compScHpf);

    mic.setType (params.getInt (P::micType));

    // Reverb character per space.
    const float size = params.get (P::revSize), tone = params.get (P::revTone);
    switch (params.getInt (P::revType))
    {
        case 1:  reverb.setSize (0.05f + 0.3f * size);  reverb.setDamping (3000.0f + 6000.0f * tone); break;   // room
        case 2:  reverb.setSize (0.5f + 0.5f * size);   reverb.setDamping (3000.0f + 5000.0f * tone); break;   // hall
        case 3:  reverb.setSize (0.2f + 0.4f * size);   reverb.setDamping (4000.0f + 6000.0f * tone); break;   // chamber
        case 4:  reverb.setSize (0.6f + 0.3f * size);   reverb.setDamping (5000.0f + 6000.0f * tone); break;   // gated
        default: reverb.setSize (0.25f + 0.5f * size);  reverb.setDamping (7000.0f + 7000.0f * tone); break;   // plate
    }
    reverb.setPreDelay (params.get (P::revPreDelay));

    const float delaySec = params.getBool (P::dlySync) ? (float) (dd::divisionBeats (params.getInt (P::dlyDiv)) * 60.0 / bpm)
                                                       : params.get (P::dlyMs) * 0.001f;
    delay.setTime (juce::jlimit (1.0f, 2.4f * (float) sr, delaySec * (float) sr));
    delay.setTone (params.get (P::dlyTone));
}

void AircheckProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midi);
    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm(); b.hasValue() && *b > 20.0)
                bpm = *b;
    readSettings();
    inGainSm.setTargetValue (dd::dbToGain (params.get (P::inGain)));
    outGainSm.setTargetValue (dd::dbToGain (params.get (P::outGain)));
    mixSm.setTargetValue (params.get (P::mix));
    tuneMixSm.setTargetValue (pitchActive ? 1.0f : 0.0f);

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
    for (int ch = 2; ch < numCh; ++ch)
        buffer.clear (ch, 0, numSamples);
    scope.push (buffer.getReadPointer (0), numCh > 1 ? buffer.getReadPointer (1) : nullptr, numSamples);
    compGr.store (comp.getGainReductionDb(), std::memory_order_relaxed);
    deessGr.store (deesser.getReductionDb(), std::memory_order_relaxed);
    smoothGr.store (smooth.getActivityDb(), std::memory_order_relaxed);
    limitGr.store (limiter.getGainReductionDb(), std::memory_order_relaxed);
    for (int i = 0; i < aircheck::ProEq::kBands; ++i)
        bandDynDb[(size_t) i].store (proEq.getDynamicDb (i), std::memory_order_relaxed);
}

void AircheckProcessor::processChunk (float* L, float* R, int n)
{
    // Input trim and the gate, before anything listens to the pitch.
    const float gateDb = params.get (P::gate);
    for (int i = 0; i < n; ++i)
    {
        const float g = inGainSm.getNextValue();
        L[i] *= g;
        R[i] *= g;
        gate.process (L[i], R[i], gateDb);
    }

    // Pitch: detect, decide the correction, and tell the shifter (stamped with the input time).
    const float transposeRatio = dd::semitonesToRatio (tune.transpose);
    for (int i = 0; i < n; ++i)
    {
        if (! detector.push (0.5f * (L[i] + R[i])))
            continue;
        const bool voiced = detector.isVoiced();
        const float hz = detector.getHz();
        const float shift = corrector.step (voiced, hz, tune);
        const float correction = shift - tune.transpose;
        aircheck::Psola::Frame f;
        f.time = detector.getTime();
        f.voiced = voiced;
        f.period = voiced ? (float) (sr / hz) : 0.0f;
        f.ratio = tuneOn ? dd::semitonesToRatio (shift) : transposeRatio;
        psola.pushFrame (f);
        const float inMidi = voiced ? corrector.getInputMidi() : 0.0f;
        pitchHistory.push (inMidi, voiced ? inMidi + (tuneOn ? correction : 0.0f) + tune.transpose : 0.0f,
                           tuneOn && voiced ? corrector.getTargetNote() : -1);
        lastCorrection = correction;
    }

    // The shifter's output time for this chunk, and the dry signal at the same moment (after the
    // preamp's oversampling delay too, so a dry/wet blend never combs).
    const int64_t r0 = psola.getWritePosition() + n - psola.getLatency();

    psola.process (L, R, n, formantRatio);
    for (int i = 0; i < n; ++i)
    {
        const int64_t t = r0 - n + i;
        dryL[(size_t) i] = psola.readInput (0, (double) t);
        dryR[(size_t) i] = psola.readInput (1, (double) t);
        const float tm = tuneMixSm.getNextValue();
        const float bl = psola.readInput (0, (double) t), br = psola.readInput (1, (double) t);
        L[i] = bl + (L[i] - bl) * tm;
        R[i] = br + (R[i] - br) * tm;
    }

    {
        float* dry[2] = { dryL.data(), dryR.data() };
        juce::dsp::AudioBlock<float> block (dry, 2, (size_t) n);
        dryAlign.process (block);
    }

    // Mic voicing and preamp.
    for (int i = 0; i < n; ++i)
        mic.process (L[i], R[i]);
    {
        float* chans[2] = { L, R };
        juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
        preamp.process (block, params.getInt (P::preType), params.get (P::preDrive), params.get (P::preTone), params.get (P::preMix));
    }

    // EQ (the analyzer sees both sides of it) before the dynamics, or after them ("EQ Post"), then the
    // rider, compressor and de-esser.
    const bool eqPost = params.getBool (P::eqPost);
    auto runEq = [&]
    {
        preEq.push (L, R, n);
        for (int i = 0; i < n; ++i)
            proEq.process (L[i], R[i]);
        postEq.push (L, R, n);
    };
    if (! eqPost)
        runEq();
    const float deessAmt = params.get (P::deess), deessHz = params.get (P::deessFreq), riderAmt = params.get (P::rider);
    for (int i = 0; i < n; ++i)
    {
        rider.process (L[i], R[i], riderAmt);
        comp.process (L[i], R[i], compSettings);
        deesser.process (L[i], R[i], deessAmt, deessHz);
    }
    if (eqPost)
        runEq();

    // Resonance smoothing and air.
    smooth.process (L, R, n, smoothSettings);
    {
        float* chans[2] = { L, R };
        juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
        air.process (block, params.get (P::airMid), params.get (P::airHigh));
    }

    // Vocoder, playing the sung (corrected) pitch.
    const float vocMix = params.get (P::vocMix);
    if (vocMix > 0.001f)
    {
        const auto f = psola.frameAt (r0 - n);
        const float pitchHz = f.voiced && f.period > 0.0f ? (float) (sr / f.period) * (pitchActive ? f.ratio : 1.0f) : 0.0f;
        std::array<int, 4> iv { 0, 0, 0, 0 };
        int count = 1;
        const int root = corrector.getTargetNote() >= 0 ? corrector.getTargetNote() : (int) std::lround (corrector.getInputMidi());
        const uint16_t mask = aircheck::scaleMask (tune.scale);
        const int third = aircheck::inScale (root + 4, tune.key, mask) || ! aircheck::inScale (root + 3, tune.key, mask) ? 4 : 3;
        const int seventh = aircheck::inScale (root + 11, tune.key, mask) && ! aircheck::inScale (root + 10, tune.key, mask) ? 11 : 10;
        switch (params.getInt (P::vocChord))
        {
            case 1:  iv = { -12, 0, 12, 0 };          count = 3; break;
            case 2:  iv = { 0, 7, 12, 0 };            count = 3; break;
            case 3:  iv = { 0, third, 7, 0 };         count = 3; break;
            case 4:  iv = { 0, third, 7, seventh };   count = 4; break;
            default: break;
        }
        const int carrier = params.getInt (P::vocCarrier);
        for (int i = 0; i < n; ++i)
        {
            const float v = vocoder.process (0.5f * (L[i] + R[i]), pitchHz, carrier, iv, count);
            L[i] += (v - L[i]) * vocMix;
            R[i] += (v - R[i]) * vocMix;
        }
    }

    // Doubled takes, tape.
    const float dbl = params.get (P::dblMix), dblWidth = params.get (P::dblWidth);
    if (dbl > 0.001f)
        for (int i = 0; i < n; ++i)
        {
            float a, b;
            doubler.process (L[i], R[i], a, b, dblWidth);
            L[i] += a * dbl * 0.8f;
            R[i] += b * dbl * 0.8f;
        }
    {
        const float rate = params.get (P::chRate), depth = params.get (P::chDepth), chMix = params.get (P::chMix);
        const int dimMode = params.getInt (P::dimMode);
        const float dimMix = params.get (P::dimMix);
        for (int i = 0; i < n; ++i)
            dimension.process (L[i], R[i], rate, depth, chMix, dimMode, dimMix);
    }
    const float tapeAmt = params.get (P::tape);
    if (tapeAmt > 0.001f)
        for (int i = 0; i < n; ++i)
            tape.process (L[i], R[i], tapeAmt * 0.65f, tapeAmt * 0.1f, tapeAmt * 0.3f);

    // Sends: tape-style delay (ducked under the voice) and the reverb (gated for the 80s space).
    const float dlyMix = params.get (P::dlyMix), revMix = params.get (P::revMix);
    const float fb = params.get (P::dlyFeedback), duck = params.get (P::dlyDuck);
    const bool ping = params.getBool (P::dlyPingPong), gated = params.getInt (P::revType) == 4;
    for (int i = 0; i < n; ++i)
    {
        const float vl = L[i], vr = R[i];
        const float lvl = std::max (std::abs (vl), std::abs (vr));
        duckEnv = lvl + (duckEnv - lvl) * (lvl > duckEnv ? duckCoefA : duckCoefR);
        float dl = 0.0f, dr = 0.0f;
        if (dlyMix > 0.001f)
        {
            delay.process (vl, vr, dl, dr, fb, ping);
            const float dg = dlyMix / (1.0f + duck * 10.0f * duckEnv);
            dl *= dg;
            dr *= dg;
        }
        float rl = 0.0f, rr = 0.0f;
        if (revMix > 0.001f)
        {
            reverb.process (vl + dl * 0.4f, vr + dr * 0.4f, rl, rr);
            if (gated)
            {
                // Open while the voice is there, hold a quarter second, then slam shut.
                gateEnv = lvl + (gateEnv - lvl) * (lvl > gateEnv ? 0.5f : 0.999f);
                if (gateEnv > 0.03f)
                    revHold = (int) (0.26 * sr);
                const float target = revHold-- > 0 ? 1.0f : 0.0f;
                revGate += (target - revGate) * (target > revGate ? 0.02f : 0.004f);
                rl *= revGate * 1.6f;
                rr *= revGate * 1.6f;
            }
            rl *= revMix;
            rr *= revMix;
        }
        L[i] = vl + dl + rl;
        R[i] = vr + dr + rr;
    }

    // Dry / wet, output level, then the true-peak limiter, always in: nothing passes -1 dBTP.
    const float limitAmt = params.get (P::limit);
    for (int i = 0; i < n; ++i)
    {
        const float m = mixSm.getNextValue();
        const float g = outGainSm.getNextValue();
        L[i] = (dryL[(size_t) i] + (L[i] - dryL[(size_t) i]) * m) * g;
        R[i] = (dryR[(size_t) i] + (R[i] - dryR[(size_t) i]) * m) * g;
        limiter.process (L[i], R[i], limitAmt);
        if (dd::isBad (L[i]) || dd::isBad (R[i]))
        {
            // Never pass NaN/Inf on: silence this chunk and start the stateful parts fresh.
            std::fill (L, L + n, 0.0f);
            std::fill (R, R + n, 0.0f);
            comp.reset(); deesser.reset(); proEq.reset(); mic.reset(); vocoder.reset(); doubler.reset();
            delay.reset(); reverb.reset(); tape.reset(); preamp.reset(); smooth.reset(); air.reset();
            rider.reset(); dimension.reset(); limiter.reset(); dryAlign.reset();
            break;
        }
    }
}

int AircheckProcessor::getNumPrograms()           { return std::max (1, presets.getNumFactoryPresets()); }
int AircheckProcessor::getCurrentProgram()        { return std::max (0, presets.getCurrentIndex()); }
void AircheckProcessor::setCurrentProgram (int i)
{
    // Hosts may re-send the current program after restoring a project; don't wipe the user's tweaks.
    if (juce::isPositiveAndBelow (i, presets.getNumFactoryPresets()) && i != presets.getCurrentIndex())
        presets.loadPreset (i);
}

const juce::String AircheckProcessor::getProgramName (int i)
{
    if (! juce::isPositiveAndBelow (i, presets.getNumFactoryPresets()))
        return "Init";
    return presets.get (i).category + " - " + presets.get (i).name;
}

void AircheckProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    presets.writeState (state);
    state.setProperty ("uiScale", editorScale, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AircheckProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            dd::replaceStateExactly (apvts, tree);
            migrateLegacyEq (tree);
            migrateLegacyPitch();
            presets.readState (tree);
            editorScale = juce::jlimit (0.6f, 1.8f, (float) tree.getProperty ("uiScale", 1.0f));
        }
}

void AircheckProcessor::migrateLegacyEq (const juce::ValueTree& saved)
{
    // Sessions from 1.0 / 1.1 had a fixed six-band EQ: carry its settings into the eight bands.
    for (const auto& child : saved)
        if (child.getProperty ("id").toString() == "b1Type")
            return;
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    auto set = [this] (const juce::String& id, float v) { if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    auto band = [&] (int n, bool on, int type, float hz, float db, float q, int slope)
    {
        const auto b = "b" + juce::String (n);
        set (b + "On", on ? 1.0f : 0.0f); set (b + "Type", (float) type); set (b + "Freq", hz);
        set (b + "Gain", db); set (b + "Q", q); set (b + "Slope", (float) slope);
    };
    band (1, get ("eqLowCut") > 21.0f, 3, get ("eqLowCut"), 0.0f, 0.71f, 1);
    band (2, std::abs (get ("eqLsGain")) > 0.05f, 1, get ("eqLsFreq"), get ("eqLsGain"), 0.7f, 1);
    band (3, std::abs (get ("eq1Gain")) > 0.05f, 0, get ("eq1Freq"), get ("eq1Gain"), get ("eq1Q"), 1);
    band (4, std::abs (get ("eq2Gain")) > 0.05f, 0, get ("eq2Freq"), get ("eq2Gain"), get ("eq2Q"), 1);
    band (5, std::abs (get ("eq3Gain")) > 0.05f, 0, get ("eq3Freq"), get ("eq3Gain"), get ("eq3Q"), 1);
    band (6, std::abs (get ("eqHsGain")) > 0.05f, 2, get ("eqHsFreq"), get ("eqHsGain"), 0.7f, 1);
    band (7, get ("eqHighCut") < 21500.0f, 4, std::min (get ("eqHighCut"), 20000.0f), 0.0f, 0.71f, 1);
}

void AircheckProcessor::migrateLegacyPitch()
{
    // Up to 1.4 Transpose and Formant sat in the tuner (whole semitones, +-4 st formant). 1.5 has a Pitch
    // Shift panel: move their values there, so what a session or preset does is what the panel shows.
    auto* transpose = apvts.getParameter ("tuneTranspose");
    auto* formant = apvts.getParameter ("tuneFormant");
    const float t = transpose->convertFrom0to1 (transpose->getValue()), f = formant->convertFrom0to1 (formant->getValue());
    if (std::abs (t) < 0.5f && std::abs (f) < 0.005f)
        return;
    auto set = [this] (const char* id, float v) { if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    set ("pitchShift", juce::jlimit (-12.0f, 12.0f, apvts.getRawParameterValue ("pitchShift")->load() + t));
    set ("pitchFormant", juce::jlimit (-7.0f, 7.0f, apvts.getRawParameterValue ("pitchFormant")->load() + f));
    set ("tuneTranspose", 0.0f);
    set ("tuneFormant", 0.0f);
}

juce::AudioProcessorEditor* AircheckProcessor::createEditor() { return new AircheckEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AircheckProcessor(); }
