#pragma once

#include <juce_dsp/juce_dsp.h>

#include "PluginProcessor.h"

namespace mirage
{
/**
    Everything the display knows about the sound, updated once per frame on the message thread from the
    processor's stereo tap: the newest samples (scope, goniometer, the front dune), a smoothed log-spaced
    spectrum, levels, low / mid / high energy, a hit detector, a scrolling waveform history, thermal
    columns and older spectra for the far dunes.
*/
class Analyzer
{
public:
    static constexpr int kOrder = 11, kFft = 1 << kOrder, kRecent = 4096;
    static constexpr int kBands = 160, kLed = 40, kLayers = 7, kHistory = 4096, kThermalRows = 128;

    struct Column { float minL, maxL, minR, maxR; };

    Analyzer()
    {
        for (int i = 0; i < kFft; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (kFft - 1));
        history.resize (kHistory, Column { 0, 0, 0, 0 });
        for (auto& l : layers)
            l.fill (0.0f);
    }

    /** Takes the samples the processor wrote since the last call and updates everything. */
    void update (const StereoTap& tap, double sampleRate, float speed, float dt)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        time += dt;
        const auto total = tap.total();
        if (readPos == 0 || total < readPos || total - readPos > (juce::uint64) (StereoTap::kSize - 2048))
            readPos = total > (juce::uint64) (StereoTap::kSize - 2048) ? total - (juce::uint64) (StereoTap::kSize - 2048) : 0;
        int fresh = (int) std::min<juce::uint64> (total - readPos, (juce::uint64) StereoTap::kSize);
        newSamples = fresh;

        const double secondsPerColumn = 0.004 + 0.03 * (1.0 - juce::jlimit (0.0f, 1.0f, speed));
        const int perColumn = std::max (16, (int) (secondsPerColumn * sr));
        while (fresh > 0)
        {
            const int n = std::min (fresh, 512);
            float l[512], r[512];
            tap.copy (readPos, n, l, r);
            readPos += (juce::uint64) n;
            fresh -= n;
            // Newest samples, oldest first.
            std::memmove (recentL.data(), recentL.data() + n, sizeof (float) * (size_t) (kRecent - n));
            std::memmove (recentR.data(), recentR.data() + n, sizeof (float) * (size_t) (kRecent - n));
            std::copy (l, l + n, recentL.end() - n);
            std::copy (r, r + n, recentR.end() - n);
            for (int i = 0; i < n; ++i)
            {
                colAcc.minL = std::min (colAcc.minL, l[i]); colAcc.maxL = std::max (colAcc.maxL, l[i]);
                colAcc.minR = std::min (colAcc.minR, r[i]); colAcc.maxR = std::max (colAcc.maxR, r[i]);
                if (++colCount >= perColumn)
                {
                    history[(size_t) historyHead] = colAcc;
                    historyHead = (historyHead + 1) % kHistory;
                    ++historyColumns;
                    colAcc = { 0, 0, 0, 0 };
                    colCount = 0;
                }
            }
        }
        analyseSpectrum (dt);
        analyseLevels (dt);

        // Older spectra become the far dunes.
        layerTimer += dt;
        if (layerTimer > 0.07f)
        {
            layerTimer = 0.0f;
            for (int k = kLayers - 1; k > 0; --k)
                layers[(size_t) k] = layers[(size_t) (k - 1)];
            layers[0] = band;
        }
    }

    // Results.
    std::array<float, kRecent> recentL {}, recentR {};
    std::array<float, kBands> band {}, bandFast {};
    std::array<float, kLed> led {}, ledPeak {};
    std::array<std::array<float, kBands>, kLayers> layers {};
    std::vector<Column> history;
    int historyHead = 0;
    juce::int64 historyColumns = 0;
    float peakL = 0, peakR = 0, rmsL = 0, rmsR = 0, holdL = 0, holdR = 0;
    float low = 0, mid = 0, high = 0, energy = 0, hit = 0, loudest = 0.0f;
    float lowHz = 60.0f;   // the strongest low frequency, for the readouts
    double time = 0.0;
    int newSamples = 0;

    static float bandHz (int b) noexcept { return 30.0f * std::pow (16000.0f / 30.0f, (float) b / (float) (kBands - 1)); }
    static float toDb (float g) noexcept { return 20.0f * std::log10 (std::max (g, 1.0e-6f)); }

private:
    void analyseSpectrum (float dt)
    {
        std::fill (fftData.begin(), fftData.end(), 0.0f);
        for (int i = 0; i < kFft; ++i)
            fftData[(size_t) i] = 0.5f * (recentL[(size_t) (kRecent - kFft + i)] + recentR[(size_t) (kRecent - kFft + i)]) * window[(size_t) i];
        fft.performFrequencyOnlyForwardTransform (fftData.data(), true);
        const float binHz = (float) sr / (float) kFft;
        const float norm = 4.0f / (float) kFft;

        // The loudest band sets the scale (slowly), so quiet mixes still move the dunes.
        float frameMax = -120.0f;
        std::array<float, kBands> db {};
        for (int b = 0; b < kBands; ++b)
        {
            const float f0 = bandHz (b) / std::pow (16000.0f / 30.0f, 0.5f / (float) (kBands - 1));
            const float f1 = bandHz (b) * std::pow (16000.0f / 30.0f, 0.5f / (float) (kBands - 1));
            const int i0 = std::max (1, (int) (f0 / binHz)), i1 = std::max (i0, std::min (kFft / 2 - 1, (int) (f1 / binHz)));
            float m = 0.0f;
            for (int i = i0; i <= i1; ++i)
                m = std::max (m, fftData[(size_t) i]);
            // Pink tilt: +4 dB per octave so cymbals show as clearly as the kick.
            db[(size_t) b] = toDb (m * norm) + 4.0f * std::log2 (bandHz (b) / 1000.0f);
            frameMax = std::max (frameMax, db[(size_t) b]);
        }
        loudest = std::max (frameMax, loudest - 6.0f * dt);
        const float top = std::max (loudest, -50.0f);

        float flux = 0.0f, lo = 0.0f, mi = 0.0f, hi = 0.0f;
        int nl = 0, nm = 0, nh = 0, strongest = 0;
        for (int b = 0; b < kBands; ++b)
        {
            const float v = juce::jlimit (0.0f, 1.0f, (db[(size_t) b] - (top - 54.0f)) / 54.0f);
            const float prev = bandFast[(size_t) b];
            bandFast[(size_t) b] = v;
            if (b < kBands * 2 / 3)
                flux += std::max (0.0f, v - prev);
            auto& s = band[(size_t) b];
            s += (v - s) * (v > s ? 0.55f : 0.10f);
            const float hz = bandHz (b);
            if (hz < 200.0f) { lo += v; ++nl; if (hz > 35.0f && v > bandFast[(size_t) strongest]) strongest = b; }
            else if (hz < 2500.0f) { mi += v; ++nm; }
            else { hi += v; ++nh; }
        }
        low += (lo / (float) std::max (1, nl) - low) * 0.35f;
        mid += (mi / (float) std::max (1, nm) - mid) * 0.3f;
        high += (hi / (float) std::max (1, nh) - high) * 0.3f;
        lowHz += (bandHz (strongest) - lowHz) * 0.2f;
        hit = std::max (hit * 0.86f, juce::jlimit (0.0f, 1.0f, (flux - 2.2f) * 0.22f));

        for (int i = 0; i < kLed; ++i)
        {
            float m = 0.0f;
            for (int b = i * kBands / kLed; b < (i + 1) * kBands / kLed; ++b)
                m = std::max (m, band[(size_t) b]);
            led[(size_t) i] = m;
            ledPeak[(size_t) i] = std::max (m, ledPeak[(size_t) i] - 0.6f * dt);
        }
    }

    void analyseLevels (float dt)
    {
        const int n = std::min (newSamples, kRecent);
        float pl = 0, pr = 0, el = 0, er = 0;
        for (int i = kRecent - std::max (n, 256); i < kRecent; ++i)
        {
            pl = std::max (pl, std::abs (recentL[(size_t) i]));
            pr = std::max (pr, std::abs (recentR[(size_t) i]));
            el += recentL[(size_t) i] * recentL[(size_t) i];
            er += recentR[(size_t) i] * recentR[(size_t) i];
        }
        const float cnt = (float) std::max (n, 256);
        peakL = std::max (pl, peakL - 1.2f * dt);
        peakR = std::max (pr, peakR - 1.2f * dt);
        rmsL += (std::sqrt (el / cnt) - rmsL) * 0.25f;
        rmsR += (std::sqrt (er / cnt) - rmsR) * 0.25f;
        holdL = pl > holdL ? pl : holdL - 0.25f * dt;
        holdR = pr > holdR ? pr : holdR - 0.25f * dt;
        energy += (juce::jlimit (0.0f, 1.0f, (toDb (0.5f * (rmsL + rmsR)) + 48.0f) / 42.0f) - energy) * 0.2f;
    }

    juce::dsp::FFT fft { kOrder };
    std::array<float, kFft * 2> fftData {};
    std::array<float, kFft> window {};
    double sr = 44100.0;
    juce::uint64 readPos = 0;
    Column colAcc { 0, 0, 0, 0 };
    int colCount = 0;
    float layerTimer = 0.0f;
};
} // namespace mirage
