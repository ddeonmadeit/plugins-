namespace dd
{
PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state, juce::StringArray order,
                              const juce::String& company, const juce::String& pluginName)
    : apvts (state), categoryOrder (std::move (order))
{
    userDir = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                  .getChildFile (company).getChildFile (pluginName).getChildFile ("Presets");
}

bool PresetManager::addFactoryPreset (const juce::String& json, const juce::String& sortKey)
{
    juce::var parsed;
    const auto result = juce::JSON::parse (json, parsed);
    if (result.failed() || ! parsed.isObject())
    {
        DBG ("Bad preset JSON (" << sortKey << "): " << result.getErrorMessage());
        jassertfalse;
        return false;
    }

    Preset p;
    p.name        = parsed.getProperty ("name", sortKey).toString();
    p.category    = parsed.getProperty ("category", "MISC").toString();
    p.author      = parsed.getProperty ("author", "").toString();
    p.description = parsed.getProperty ("description", "").toString();
    p.params      = parsed.getProperty ("params", juce::var());
    p.extra       = parsed.getProperty ("extra", juce::var());
    p.sortKey     = sortKey;
    p.isFactory   = true;
    presets.insert (numFactory++, p);
    return true;
}

void PresetManager::sortFactory()
{
    auto rank = [this] (const juce::String& c)
    {
        const int i = categoryOrder.indexOf (c);
        return i < 0 ? categoryOrder.size() : i;
    };

    std::stable_sort (presets.begin(), presets.begin() + numFactory, [&] (const Preset& a, const Preset& b)
    {
        const int ra = rank (a.category), rb = rank (b.category);
        if (ra != rb) return ra < rb;
        if (a.category != b.category) return a.category < b.category;
        return a.sortKey.compareNatural (b.sortKey) < 0;
    });
}

void PresetManager::finishLoading()
{
    sortFactory();
    rescanUserPresets();
}

void PresetManager::rescanUserPresets()
{
    const auto currentName = getCurrentName();
    const auto currentCat = getCurrentCategory();

    presets.removeRange (numFactory, presets.size() - numFactory);

    if (userDir.isDirectory())
    {
        auto files = userDir.findChildFiles (juce::File::findFiles, false, "*.json");
        files.sort();
        for (auto& f : files)
        {
            juce::var parsed;
            if (juce::JSON::parse (f.loadFileAsString(), parsed).failed() || ! parsed.isObject())
                continue;
            Preset p;
            p.name = parsed.getProperty ("name", f.getFileNameWithoutExtension()).toString();
            p.category = kUserCategory;
            p.author = parsed.getProperty ("author", "").toString();
            p.description = parsed.getProperty ("description", "").toString();
            p.params = parsed.getProperty ("params", juce::var());
            p.extra = parsed.getProperty ("extra", juce::var());
            p.isFactory = false;
            p.file = f;
            p.sortKey = f.getFileName();
            presets.add (p);
        }
    }

    if (currentIndex >= 0)
        currentIndex = indexOf (currentName, currentCat);
    sendChangeMessage();
}

int PresetManager::indexOf (const juce::String& name, const juce::String& category) const
{
    for (int i = 0; i < presets.size(); ++i)
        if (presets.getReference (i).name == name && (category.isEmpty() || presets.getReference (i).category == category))
            return i;
    return -1;
}

juce::StringArray PresetManager::getCategories() const
{
    juce::StringArray cats;
    for (auto& p : presets)
        cats.addIfNotAlreadyThere (p.category);
    return cats;
}

juce::Array<int> PresetManager::getIndicesInCategory (const juce::String& category) const
{
    juce::Array<int> out;
    for (int i = 0; i < presets.size(); ++i)
        if (presets.getReference (i).category == category)
            out.add (i);
    return out;
}

bool PresetManager::loadPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, presets.size()))
        return false;
    applyParams (presets.getReference (index).params);
    if (onApplyExtra)
        onApplyExtra (presets.getReference (index).extra);
    currentIndex = index;
    orphanName.clear();
    sendChangeMessage();
    return true;
}

void PresetManager::loadRelative (int delta)
{
    if (presets.isEmpty())
        return;
    const int n = presets.size();
    const int start = currentIndex < 0 ? (delta > 0 ? -1 : 0) : currentIndex;
    loadPreset (((start + delta) % n + n) % n);
}

void PresetManager::loadInit()
{
    applyParams (juce::var (new juce::DynamicObject()));
    if (onApplyExtra)
        onApplyExtra (juce::var());
    currentIndex = -1;
    orphanName = "Init";
    sendChangeMessage();
}

juce::String PresetManager::getCurrentName() const
{
    if (juce::isPositiveAndBelow (currentIndex, presets.size()))
        return presets.getReference (currentIndex).name;
    return orphanName.isNotEmpty() ? orphanName : juce::String ("Init");
}

juce::String PresetManager::getCurrentCategory() const
{
    return juce::isPositiveAndBelow (currentIndex, presets.size()) ? presets.getReference (currentIndex).category : juce::String();
}

juce::String PresetManager::getCurrentDescription() const
{
    return juce::isPositiveAndBelow (currentIndex, presets.size()) ? presets.getReference (currentIndex).description : juce::String();
}

juce::String PresetManager::sanitiseFileName (const juce::String& name)
{
    return juce::File::createLegalFileName (name.trim()).replaceCharacter ('.', '_');
}

bool PresetManager::saveUserPreset (const juce::String& rawName)
{
    const auto name = rawName.trim();
    if (name.isEmpty())
        return false;

    if (! userDir.isDirectory() && ! userDir.createDirectory())
        return false;

    auto* obj = new juce::DynamicObject();
    obj->setProperty ("name", name);
    obj->setProperty ("category", kUserCategory);
    obj->setProperty ("author", "user");
    obj->setProperty ("params", captureParams());
    if (onCaptureExtra)
        obj->setProperty ("extra", onCaptureExtra());

    const auto file = userDir.getChildFile (sanitiseFileName (name) + ".json");
    if (! file.replaceWithText (juce::JSON::toString (juce::var (obj), false)))
        return false;

    rescanUserPresets();
    currentIndex = indexOf (name, kUserCategory);
    sendChangeMessage();
    return true;
}

juce::var PresetManager::captureParams() const   { return captureParameterValues (apvts.processor); }
void PresetManager::applyParams (const juce::var& p)  { applyParameterValues (apvts.processor, p); }

juce::var captureParameterValues (juce::AudioProcessor& processor)
{
    auto* obj = new juce::DynamicObject();
    for (auto* p : processor.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (rp == nullptr)
            continue;

        const auto id = rp->getParameterID();
        if (isPerformanceParam (id))
            continue;
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (rp))
            obj->setProperty (id, c->getCurrentChoiceName());
        else if (auto* b = dynamic_cast<juce::AudioParameterBool*> (rp))
            obj->setProperty (id, b->get());
        else if (auto* n = dynamic_cast<juce::AudioParameterInt*> (rp))
            obj->setProperty (id, n->get());
        else
        {
            const float v = rp->convertFrom0to1 (rp->getValue());
            obj->setProperty (id, std::round (v * 10000.0) / 10000.0);
        }
    }
    return juce::var (obj);
}

juce::StringArray applyParameterValues (juce::AudioProcessor& processor, const juce::var& values)
{
    juce::StringArray known;
    for (auto* p : processor.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (rp == nullptr)
            continue;

        const auto id = rp->getParameterID();
        known.add (id);
        if (isPerformanceParam (id))
            continue;
        float norm = rp->getDefaultValue();

        if (values.hasProperty (id))
        {
            const auto v = values[juce::Identifier (id)];
            if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (rp); c != nullptr && v.isString())
            {
                const int idx = c->choices.indexOf (v.toString(), true);
                jassert (idx >= 0); // unknown choice name in preset
                if (idx >= 0)
                    norm = c->convertTo0to1 ((float) idx);
            }
            else if (v.isBool())
                norm = (bool) v ? 1.0f : 0.0f;
            else
            {
                const auto& range = rp->getNormalisableRange();
                norm = range.convertTo0to1 (range.snapToLegalValue ((float) (double) v));
            }
        }

        rp->beginChangeGesture();
        rp->setValueNotifyingHost (norm);
        rp->endChangeGesture();
    }

    juce::StringArray unknown;
    if (auto* obj = values.getDynamicObject())
        for (auto& prop : obj->getProperties())
            if (! known.contains (prop.name.toString()))
                unknown.add (prop.name.toString());
    return unknown;
}

void replaceStateExactly (juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& newState)
{
    apvts.replaceState (newState);

    for (auto* p : apvts.processor.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (rp == nullptr)
            continue;
        const auto child = newState.getChildWithProperty ("id", rp->getParameterID());
        if (! child.isValid() || ! child.hasProperty ("value"))
            continue;
        const float norm = rp->convertTo0to1 ((float) child.getProperty ("value"));
        if (! juce::approximatelyEqual (rp->getValue(), norm))
            rp->setValueNotifyingHost (norm);
    }
}

void PresetManager::writeState (juce::ValueTree& state) const
{
    state.setProperty ("presetName", getCurrentName(), nullptr);
    state.setProperty ("presetCategory", getCurrentCategory(), nullptr);
}

void PresetManager::readState (const juce::ValueTree& state)
{
    const auto name = state.getProperty ("presetName").toString();
    const auto cat = state.getProperty ("presetCategory").toString();
    currentIndex = indexOf (name, cat);
    orphanName = currentIndex < 0 ? name : juce::String();
    sendChangeMessage();
}
void keepOldDefault (juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& savedState,
                     const juce::String& paramId, float oldValue)
{
    for (const auto& child : savedState)
        if (child.getProperty ("id").toString() == paramId)
            return;
    if (auto* p = apvts.getParameter (paramId))
        p->setValueNotifyingHost (p->convertTo0to1 (oldValue));
}
} // namespace dd
