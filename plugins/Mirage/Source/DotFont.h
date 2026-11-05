#pragma once

#include <juce_graphics/juce_graphics.h>

namespace mirage
{
/**
    A 5 x 7 dot-matrix font, drawn as little squares like a phosphor terminal: the lettering of an old
    control panel. Upper case, digits and the usual punctuation (lower case is shown as upper case).
*/
class DotFont
{
public:
    /** Width of `text` at `dot` pixels per dot (each glyph is 5 dots plus 1 of spacing). */
    static float width (const juce::String& text, float dot) noexcept { return (float) text.length() * 6.0f * dot - dot; }

    /** Adds the dots of `text` (top-left at x, y) to `rects`. */
    static void layout (juce::RectangleList<float>& rects, const juce::String& text, float x, float y, float dot, float fill = 0.78f)
    {
        const float d = dot * fill;
        for (auto c : text)
        {
            if (const auto* g = glyph (c))
                for (int row = 0; row < 7; ++row)
                    for (int col = 0; col < 5; ++col)
                        if (g[row] & (0x10 >> col))
                            rects.addWithoutMerging ({ x + (float) col * dot, y + (float) row * dot, d, d });
            x += 6.0f * dot;
        }
    }

    /** Draws glowing dot text. `just` aligns it inside `area` (left, centred or right; vertically centred). */
    static void draw (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float dot, juce::Colour colour,
                      juce::Justification just = juce::Justification::centredLeft, float glow = 1.0f)
    {
        const float w = width (text, dot), h = 7.0f * dot;
        float x = area.getX();
        if (just.testFlags (juce::Justification::horizontallyCentred))
            x = area.getCentreX() - 0.5f * w;
        else if (just.testFlags (juce::Justification::right))
            x = area.getRight() - w;
        const float y = area.getCentreY() - 0.5f * h;
        juce::RectangleList<float> rects;
        layout (rects, text, std::round (x), std::round (y), dot);
        if (glow > 0.0f)
        {
            juce::RectangleList<float> halo;
            for (auto& r : rects)
                halo.addWithoutMerging (r.expanded (dot * 0.55f));
            g.setColour (colour.withAlpha (0.16f * glow));
            g.fillRectList (halo);
        }
        g.setColour (colour);
        g.fillRectList (rects);
    }

private:
    static const uint8_t* glyph (juce::juce_wchar c) noexcept
    {
        if (c >= 'a' && c <= 'z')
            c = c - 'a' + 'A';
        static const uint8_t letters[26][7] {
            { 14, 17, 17, 31, 17, 17, 17 }, { 30, 17, 17, 30, 17, 17, 30 }, { 14, 17, 16, 16, 16, 17, 14 }, { 28, 18, 17, 17, 17, 18, 28 },
            { 31, 16, 16, 30, 16, 16, 31 }, { 31, 16, 16, 30, 16, 16, 16 }, { 14, 17, 16, 23, 17, 17, 15 }, { 17, 17, 17, 31, 17, 17, 17 },
            { 14, 4, 4, 4, 4, 4, 14 },      { 7, 2, 2, 2, 2, 18, 12 },      { 17, 18, 20, 24, 20, 18, 17 }, { 16, 16, 16, 16, 16, 16, 31 },
            { 17, 27, 21, 21, 17, 17, 17 }, { 17, 17, 25, 21, 19, 17, 17 }, { 14, 17, 17, 17, 17, 17, 14 }, { 30, 17, 17, 30, 16, 16, 16 },
            { 14, 17, 17, 17, 21, 18, 13 }, { 30, 17, 17, 30, 20, 18, 17 }, { 15, 16, 16, 14, 1, 1, 30 },   { 31, 4, 4, 4, 4, 4, 4 },
            { 17, 17, 17, 17, 17, 17, 14 }, { 17, 17, 17, 17, 17, 10, 4 },  { 17, 17, 17, 21, 21, 21, 10 }, { 17, 17, 10, 4, 10, 17, 17 },
            { 17, 17, 17, 10, 4, 4, 4 },    { 31, 1, 2, 4, 8, 16, 31 },
        };
        static const uint8_t digits[10][7] {
            { 14, 17, 19, 21, 25, 17, 14 }, { 4, 12, 4, 4, 4, 4, 14 },  { 14, 17, 1, 2, 4, 8, 31 },  { 31, 2, 4, 2, 1, 17, 14 },
            { 2, 6, 10, 18, 31, 2, 2 },     { 31, 16, 30, 1, 1, 17, 14 }, { 6, 8, 16, 30, 17, 17, 14 }, { 31, 1, 2, 4, 8, 8, 8 },
            { 14, 17, 17, 14, 17, 17, 14 }, { 14, 17, 17, 15, 1, 2, 12 },
        };
        struct Sym { juce::juce_wchar c; uint8_t rows[7]; };
        static const Sym symbols[] {
            { ' ', { 0, 0, 0, 0, 0, 0, 0 } },      { '.', { 0, 0, 0, 0, 0, 12, 12 } },    { ':', { 0, 12, 12, 0, 12, 12, 0 } },
            { '%', { 24, 25, 2, 4, 8, 19, 3 } },    { '-', { 0, 0, 0, 31, 0, 0, 0 } },     { '+', { 0, 4, 4, 31, 4, 4, 0 } },
            { '/', { 0, 1, 2, 4, 8, 16, 0 } },      { '>', { 8, 4, 2, 1, 2, 4, 8 } },      { '<', { 2, 4, 8, 16, 8, 4, 2 } },
            { '[', { 14, 8, 8, 8, 8, 8, 14 } },     { ']', { 14, 2, 2, 2, 2, 2, 14 } },    { '#', { 10, 10, 31, 10, 31, 10, 10 } },
            { '=', { 0, 0, 31, 0, 31, 0, 0 } },     { '_', { 0, 0, 0, 0, 0, 0, 31 } },     { '|', { 4, 4, 4, 4, 4, 4, 4 } },
            { '!', { 4, 4, 4, 4, 4, 0, 4 } },       { '?', { 14, 17, 1, 2, 4, 0, 4 } },    { '(', { 2, 4, 8, 8, 8, 4, 2 } },
            { ')', { 8, 4, 2, 2, 2, 4, 8 } },       { '*', { 0, 4, 21, 14, 21, 4, 0 } },   { '\'', { 4, 4, 8, 0, 0, 0, 0 } },
            { ',', { 0, 0, 0, 0, 12, 4, 8 } },      { '^', { 12, 18, 18, 12, 0, 0, 0 } },  // ^ is the degree sign
        };
        if (c >= 'A' && c <= 'Z')
            return letters[c - 'A'];
        if (c >= '0' && c <= '9')
            return digits[c - '0'];
        for (auto& s : symbols)
            if (s.c == c)
                return s.rows;
        return nullptr;
    }
};
} // namespace mirage
