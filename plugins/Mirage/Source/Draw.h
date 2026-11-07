#pragma once

#include <juce_graphics/juce_graphics.h>

#include "Analysis.h"

namespace mirage
{
inline juce::String pad (int v, int digits)
{
    auto t = juce::String (std::abs (v)).paddedLeft ('0', digits);
    return v < 0 ? "-" + t : t;
}

inline juce::String dbText (float g)
{
    const float d = Analyzer::toDb (g);
    if (d < -60.0f)
        return "-INF";
    return juce::String (d, 1);
}

/** A soft halo and a bright core: phosphor lines. */
inline void glowStroke (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width)
{
    g.setColour (c.withMultipliedAlpha (0.28f));
    g.strokePath (p, juce::PathStrokeType (width * 3.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
}

/** A leader line from a point on the picture to a readout, the way an instrument labels what it measures. */
inline void leader (juce::Graphics& g, juce::Point<float> tip, juce::Point<float> elbow, float run, juce::Colour c, float s)
{
    juce::Path p;
    p.startNewSubPath (tip);
    p.lineTo (elbow);
    p.lineTo (elbow.x + run, elbow.y);
    glowStroke (g, p, c.withAlpha (0.85f), 0.9f * s);
    g.setColour (c);
    g.drawEllipse (juce::Rectangle<float> (6.0f * s, 6.0f * s).withCentre (tip), 1.0f * s);
}

/** floor without a library call (std::floor is one on plain x86-64), for the per-pixel loops. */
inline float fastFloor (float x) noexcept
{
    const float t = (float) (int) x;
    return t > x ? t - 1.0f : t;
}

inline float smoothstep (float a, float b, float x) noexcept
{
    const float t = juce::jlimit (0.0f, 1.0f, (x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}
} // namespace mirage
