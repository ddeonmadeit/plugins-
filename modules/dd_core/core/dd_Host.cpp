namespace dd
{
namespace
{
Host detectHost()
{
    const auto forced = juce::SystemStats::getEnvironmentVariable ("DD_HOST", {}).trim().toLowerCase();
    const juce::PluginHostType h;
    const auto is = [&forced] (const char* id, bool detected) { return forced.isNotEmpty() ? forced == id : detected; };

    if (is ("ableton", h.isAbletonLive()))             return { "Ableton Live", "Ableton", 3, true };
    if (is ("logic", h.isLogic() || h.isMainStage()))  return { "Logic Pro", "Logic", 3, false };
    if (is ("garageband", h.isGarageBand()))           return { "GarageBand", "GarageBand", 3, false };
    if (is ("cubase", h.isCubase() || h.isNuendo()))   return { "Cubase", "Cubase", 3, false };
    if (is ("studioone", h.isStudioOne() || h.isFenderStudioPro())) return { "Studio One", "Studio One", 3, false };
    if (is ("bitwig", h.isBitwigStudio()))             return { "Bitwig Studio", "Bitwig", 3, false };
    if (is ("protools", h.isProTools()))               return { "Pro Tools", "Pro Tools", 3, false };
    if (is ("reaper", h.isReaper()))                   return { "Reaper", "Reaper", 4, false };
    if (is ("fl", h.isFruityLoops()))                  return { "FL Studio", "FL", 5, false };
    return { {}, "FL", 5, false };
}
} // namespace

const Host& host()
{
    static const Host h = detectHost();
    return h;
}

juce::String noteName (int midi)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (midi < 0 || midi > 127)
        return "--";
    return juce::String (names[midi % 12]) + juce::String (midi / 12 + host().middleCOctave - 5);
}

juce::String noteNamesCaption()
{
    return host().shortName.toUpperCase() + " NAMES";
}
} // namespace dd
