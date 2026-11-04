#pragma once

namespace dd
{
/** Half-open span of timeline samples. */
struct Span
{
    int64_t start = 0, end = 0;
    int64_t length() const noexcept { return end - start; }
    bool contains (int64_t t) const noexcept { return t >= start && t < end; }
    bool overlaps (const Span& o) const noexcept { return start < o.end && o.start < end; }
};

/** A sorted set of disjoint spans (what has been captured). */
class SpanSet
{
public:
    const std::vector<Span>& spans() const noexcept { return list; }
    bool isEmpty() const noexcept { return list.empty(); }
    void clear() { list.clear(); }

    /** Adds a span, merging anything it touches (spans closer than `join` samples are merged too). */
    void add (Span s, int64_t join = 0)
    {
        if (s.length() <= 0)
            return;
        std::vector<Span> out;
        out.reserve (list.size() + 1);
        bool placed = false;
        for (const auto& c : list)
        {
            if (c.end + join < s.start)
                out.push_back (c);
            else if (s.end + join < c.start)
            {
                if (! placed)
                {
                    out.push_back (s);
                    placed = true;
                }
                out.push_back (c);
            }
            else
            {
                s.start = std::min (s.start, c.start);
                s.end = std::max (s.end, c.end);
            }
        }
        if (! placed)
            out.push_back (s);
        list = std::move (out);
    }

    /** The span containing t, or nullptr. */
    const Span* find (int64_t t) const noexcept
    {
        auto it = std::upper_bound (list.begin(), list.end(), t, [] (int64_t v, const Span& s) { return v < s.end; });
        return it != list.end() && it->contains (t) ? &*it : nullptr;
    }

    Span bounds() const noexcept { return list.empty() ? Span {} : Span { list.front().start, list.back().end }; }

    juce::String toString (double sampleRate) const
    {
        juce::StringArray parts;
        for (const auto& s : list)
            parts.add (juce::String ((double) s.start / sampleRate, 6) + ":" + juce::String ((double) s.end / sampleRate, 6));
        return parts.joinIntoString (";");
    }

    static SpanSet fromString (const juce::String& text, double sampleRate)
    {
        SpanSet set;
        for (const auto& p : juce::StringArray::fromTokens (text, ";", ""))
        {
            const auto a = (int64_t) std::llround (p.upToFirstOccurrenceOf (":", false, false).getDoubleValue() * sampleRate);
            const auto b = (int64_t) std::llround (p.fromFirstOccurrenceOf (":", false, false).getDoubleValue() * sampleRate);
            set.add ({ a, b });
        }
        return set;
    }

private:
    std::vector<Span> list;
};
} // namespace dd
