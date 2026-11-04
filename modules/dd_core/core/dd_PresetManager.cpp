namespace dd
{
PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state, juce::StringArray order,
                              const juce::String& company, const juce::String& pluginName)
    : apvts (state), categoryOrder (std::move (order))
{
    userDir = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                  .getChildFile (company).getChildFile (pluginName).getChildFile ("Presets");
    // Watch every sound parameter so the preset bar can show that the sound was edited.
    for (auto* p : apvts.processor.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
        perfParam.push_back (rp == nullptr || isPerformanceParam (rp->getParameterID()));
        p->addListener (this);
    }
}

PresetManager::~PresetManager()
{
    for (auto* p : apvts.processor.getParameters())
        p->removeListener (this);
    cancelPendingUpdate();
}

void PresetManager::parameterValueChanged (int index, float)
{
    // Can arrive on any thread (host automation): only flip the flag and repaint later.
    if (applying || ! juce::isPositiveAndBelow (index, (int) perfParam.size()) || perfParam[(size_t) index])
        return;
    if (! modified.exchange (true))
        triggerAsyncUpdate();
}

void PresetManager::apply (const Preset& p)
{
    applying = true;
    applyParams (p.params);
    if (onApplyExtra)
        onApplyExtra (p.extra);
    applying = false;
    modified = false;
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
    p.factoryParams = p.params;
    p.factoryExtra = p.extra;
    p.factoryDescription = p.description;
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

    // Back to the factory originals, then lay the user's files over them.
    presets.removeRange (numFactory, presets.size() - numFactory);
    for (int i = 0; i < numFactory; ++i)
    {
        auto& f = presets.getReference (i);
        f.params = f.factoryParams;
        f.extra = f.factoryExtra;
        f.description = f.factoryDescription;
        f.isOverride = false;
        f.file = juce::File();
    }

    if (userDir.isDirectory())
    {
        auto files = userDir.findChildFiles (juce::File::findFiles, false, "*.json");
        files.sort();
        for (auto& f : files)
        {
            juce::var parsed;
            if (juce::JSON::parse (f.loadFileAsString(), parsed).failed() || ! parsed.isObject())
                continue;
            const auto name = parsed.getProperty ("name", f.getFileNameWithoutExtension()).toString();
            const auto category = parsed.getProperty ("category", kUserCategory).toString();
            if ((bool) parsed.getProperty ("overrides", false))
            {
                int target = -1;
                for (int i = 0; i < numFactory && target < 0; ++i)
                    if (presets.getReference (i).name == name && presets.getReference (i).category == category)
                        target = i;
                if (target >= 0)
                {
                    auto& t = presets.getReference (target);
                    t.params = parsed.getProperty ("params", juce::var());
                    t.extra = parsed.getProperty ("extra", juce::var());
                    if (parsed.hasProperty ("description"))
                        t.description = parsed.getProperty ("description", "").toString();
                    t.isOverride = true;
                    t.file = f;
                    continue;
                }
            }
            Preset p;
            p.name = name;
            p.category = category.isNotEmpty() ? category : juce::String (kUserCategory);
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
    apply (presets.getReference (index));
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
    Preset init;
    init.params = juce::var (new juce::DynamicObject());
    apply (init);
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

bool PresetManager::writeFile (const juce::File& file, const juce::String& name, const juce::String& category,
                               const juce::String& description, bool overrides)
{
    if (! userDir.isDirectory() && ! userDir.createDirectory())
        return false;
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("name", name);
    obj->setProperty ("category", category);
    obj->setProperty ("author", "user");
    if (description.isNotEmpty())
        obj->setProperty ("description", description);
    if (overrides)
        obj->setProperty ("overrides", true);
    obj->setProperty ("params", captureParams());
    if (onCaptureExtra)
        obj->setProperty ("extra", onCaptureExtra());
    return file.replaceWithText (juce::JSON::toString (juce::var (obj), false));
}

bool PresetManager::saveUserPreset (const juce::String& rawName, const juce::String& rawCategory)
{
    const auto name = rawName.trim();
    const auto category = rawCategory.trim().isNotEmpty() ? rawCategory.trim().toUpperCase() : juce::String (kUserCategory);
    if (name.isEmpty())
        return false;
    // USER presets keep the plain file name older versions used; other categories are prefixed so
    // the same name can live in two categories.
    const auto base = category == kUserCategory ? sanitiseFileName (name) : sanitiseFileName (category) + "__" + sanitiseFileName (name);
    if (! writeFile (userDir.getChildFile (base + ".json"), name, category, {}, false))
        return false;

    rescanUserPresets();
    currentIndex = indexOf (name, category);
    for (int i = numFactory; i < presets.size(); ++i)   // prefer the user copy if a factory one shares the name
        if (presets.getReference (i).name == name && presets.getReference (i).category == category)
            currentIndex = i;
    modified = false;
    sendChangeMessage();
    return true;
}

bool PresetManager::saveOverCurrent()
{
    if (! juce::isPositiveAndBelow (currentIndex, presets.size()))
        return false;
    const auto p = presets.getReference (currentIndex);
    const auto file = p.file != juce::File() ? p.file
                    : userDir.getChildFile (sanitiseFileName (p.category) + "__" + sanitiseFileName (p.name) + ".override.json");
    if (! writeFile (file, p.name, p.category, p.isFactory ? juce::String() : p.description, p.isFactory))
        return false;
    const int keep = currentIndex;
    rescanUserPresets();
    currentIndex = keep;
    modified = false;
    sendChangeMessage();
    return true;
}

bool PresetManager::restoreFactory (int index)
{
    if (! juce::isPositiveAndBelow (index, numFactory) || ! presets.getReference (index).isOverride)
        return false;
    presets.getReference (index).file.deleteFile();
    rescanUserPresets();
    return loadPreset (index);
}

bool PresetManager::deleteUserPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, presets.size()) || presets.getReference (index).isFactory)
        return false;
    presets.getReference (index).file.deleteFile();
    const bool wasCurrent = index == currentIndex;
    rescanUserPresets();
    if (wasCurrent)
    {
        currentIndex = -1;
        orphanName = "Init";
        modified = true;
        sendChangeMessage();
    }
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
    state.setProperty ("presetModified", isModified(), nullptr);
}

void PresetManager::readState (const juce::ValueTree& state)
{
    const auto name = state.getProperty ("presetName").toString();
    const auto cat = state.getProperty ("presetCategory").toString();
    currentIndex = indexOf (name, cat);
    orphanName = currentIndex < 0 ? name : juce::String();
    modified = (bool) state.getProperty ("presetModified", false);
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
