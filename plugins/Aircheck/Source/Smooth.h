#pragma once

#include <dd_dsp/dd_dsp.h>

/*
    Resonance suppressor (the job soothe2 does): short-time spectra of the voice are compared with their
    own smoothed spectral envelope; frequencies that poke out above it (whistly resonances, harsh 2-8 kHz
    peaks, ringing room modes) are turned down by as much as they stick out, only while they do. Gains are
    smoothed over time and across neighbouring frequencies and applied to both channels alike, so the
    image stays put. Overlap-add STFT (sqrt-Hann windows, 75 % overlap); fixed latency of one frame.
*/
namespace aircheck
{
class Smooth
{
public:
    struct Settings
    {
        float depth = 0.0f;       // 0..1: up to 14 dB on the worst peaks
        float sharpness = 0.5f;   // 0 = broad reductions, 1 = only narrow peaks
        float lowHz = 1500.0f, highHz = 12000.0f;
    };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        order = sr > 100000.0 ? 12 : (sr > 50000.0 ? 11 : 10);
        N = 1 << order;
        H = N / 4;
        fft = std::make_unique<juce::dsp::FFT> (order);
        window.resize ((size_t) N);
        for (int i = 0; i < N; ++i)
            window[(size_t) i] = (float) std::sqrt (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / N));
        for (int ch = 0; ch < 2; ++ch)
        {
            in[ch].assign ((size_t) N, 0.0f);
            out[ch].assign ((size_t) (2 * N), 0.0f);
            frame[ch].assign ((size_t) (2 * N), 0.0f);
        }
        const int bins = N / 2 + 1;
        logMag.assign ((size_t) bins, 0.0f);
        prefix.assign ((size_t) bins + 1, 0.0);
        red.assign ((size_t) bins, 0.0f);
        tmp.assign ((size_t) bins, 0.0f);
        relCoef = (float) std::exp (-(double) H / (0.07 * sr));
        // The inverse transform's scaling differs between FFT engines: measure it once.
        std::vector<float> probe ((size_t) (2 * N), 0.0f);
        probe[0] = 1.0f;
        fft->performRealOnlyForwardTransform (probe.data(), true);
        fft->performRealOnlyInverseTransform (probe.data());
        invScale = std::abs (probe[0]) > 1.0e-9f ? 1.0f / probe[0] : 1.0f;
        reset();
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill (in[ch].begin(), in[ch].end(), 0.0f);
            std::fill (out[ch].begin(), out[ch].end(), 0.0f);
        }
        std::fill (red.begin(), red.end(), 0.0f);
        redPeak = 0.0f;
        pos = 0;
        hopCount = 0;
        activity.store (0.0f);
    }

    int getLatency() const noexcept { return N; }

    /** Largest reduction in the last frame, dB (for a meter). */
    float getActivityDb() const noexcept { return activity.load (std::memory_order_relaxed); }

    void process (float* L, float* R, int n, const Settings& s) noexcept
    {
        settings = s;
        const int outSize = 2 * N;
        for (int i = 0; i < n; ++i)
        {
            in[0][(size_t) (pos % N)] = L[i];
            in[1][(size_t) (pos % N)] = R[i];
            const int o = (int) (pos % outSize);
            L[i] = out[0][(size_t) o];
            R[i] = out[1][(size_t) o];
            out[0][(size_t) o] = out[1][(size_t) o] = 0.0f;
            ++pos;
            if (++hopCount >= H)
            {
                hopCount = 0;
                processFrame();
            }
        }
    }

private:
    void processFrame() noexcept
    {
        const int bins = N / 2 + 1;
        const int outSize = 2 * N;
        const bool active = settings.depth > 0.001f;
        if (! active && redPeak < 1.0e-4f)
        {
            // Nothing to take out (Smooth is off and the last reductions have faded): the transforms would only
            // hand back the windowed frame, so overlap-add that straight away and skip all four FFTs.
            for (int ch = 0; ch < 2; ++ch)
                for (int k = 0; k < N; ++k)
                {
                    const float w = window[(size_t) k];
                    out[ch][(size_t) ((pos + k) % outSize)] += in[ch][(size_t) ((pos - N + k + N) % N)] * w * w * 0.5f;
                }
            activity.store (0.0f, std::memory_order_relaxed);
            return;
        }

        // The newest N samples, windowed, into both spectra.
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& f = frame[ch];
            for (int k = 0; k < N; ++k)
                f[(size_t) k] = in[ch][(size_t) ((pos - N + k + N) % N)] * window[(size_t) k];
            std::fill (f.begin() + N, f.end(), 0.0f);
            fft->performRealOnlyForwardTransform (f.data(), true);
        }

        if (active)
        {
            // Mid-channel level per bin, in dB.
            for (int k = 0; k < bins; ++k)
            {
                const float re = 0.5f * (frame[0][(size_t) (2 * k)] + frame[1][(size_t) (2 * k)]);
                const float im = 0.5f * (frame[0][(size_t) (2 * k + 1)] + frame[1][(size_t) (2 * k + 1)]);
                logMag[(size_t) k] = 10.0f * std::log10 (re * re + im * im + 1.0e-20f);
            }
            prefix[0] = 0.0;
            for (int k = 0; k < bins; ++k)
                prefix[(size_t) k + 1] = prefix[(size_t) k] + logMag[(size_t) k];

            // Envelope: the average level over a band a fraction of an octave wide around each bin.
            const float widthOct = juce::jmap (settings.sharpness, 1.6f, 0.25f);
            const float r = std::exp2 (widthOct * 0.5f);
            const float binHz = (float) sr / (float) N;
            const float maxRed = 14.0f * settings.depth;
            const float floorDb = 20.0f * std::log10 ((float) N * 0.25f) - 75.0f;   // ignore bins near silence
            float peak = 0.0f;
            for (int k = 1; k < bins; ++k)
            {
                const float hz = (float) k * binHz;
                const int lo = std::max (1, (int) ((float) k / r)), hi = std::min (bins - 1, (int) std::ceil ((float) k * r));
                const float env = (float) ((prefix[(size_t) hi + 1] - prefix[(size_t) lo]) / (double) (hi - lo + 1));
                const float excess = logMag[(size_t) k] - env;
                // Focus range with soft third-octave edges.
                const float edgeLo = juce::jlimit (0.0f, 1.0f, std::log2 (hz / settings.lowHz) * 3.0f + 1.0f);
                const float edgeHi = juce::jlimit (0.0f, 1.0f, std::log2 (settings.highHz / hz) * 3.0f + 1.0f);
                float want = juce::jlimit (0.0f, maxRed, (excess - 2.5f) * 0.85f * settings.depth) * edgeLo * edgeHi;
                if (logMag[(size_t) k] < floorDb)
                    want = 0.0f;
                tmp[(size_t) k] = want;
            }
            tmp[0] = 0.0f;
            // Spread across neighbours (no isolated notches), then attack fast / release slowly.
            for (int k = 0; k < bins; ++k)
            {
                float sum = 0.0f, wsum = 0.0f;
                for (int d = -2; d <= 2; ++d)
                {
                    const int j = k + d;
                    if (j < 0 || j >= bins) continue;
                    const float w = 3.0f - (float) std::abs (d);
                    sum += tmp[(size_t) j] * w;
                    wsum += w;
                }
                const float want = sum / wsum;
                auto& g = red[(size_t) k];
                g = want > g ? g + (want - g) * 0.6f : want + (g - want) * relCoef;
                peak = std::max (peak, g);
            }
            activity.store (peak, std::memory_order_relaxed);
        }
        else
        {
            for (auto& g : red)
                g *= relCoef;
            activity.store (0.0f, std::memory_order_relaxed);
        }

        // The gain per bin, once for both channels (most bins take nothing out).
        redPeak = 0.0f;
        for (int k = 0; k < bins; ++k)
        {
            const float r = red[(size_t) k];
            redPeak = std::max (redPeak, r);
            tmp[(size_t) k] = r > 1.0e-4f ? std::exp2 (r * -0.16609640f) : 1.0f;   // 10^(-r / 20)
        }

        // Apply the same gains to both channels, back to time, window, overlap-add one frame ahead.
        const float norm = invScale * 0.5f;   // sqrt-Hann analysis x synthesis = Hann, which sums to 2 at 75 % overlap
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& f = frame[ch];
            for (int k = 0; k < bins; ++k)
            {
                const float g = tmp[(size_t) k];
                f[(size_t) (2 * k)] *= g;
                f[(size_t) (2 * k + 1)] *= g;
            }
            fft->performRealOnlyInverseTransform (f.data());
            for (int k = 0; k < N; ++k)
            {
                const int o = (int) ((pos + k) % outSize);
                out[ch][(size_t) o] += f[(size_t) k] * window[(size_t) k] * norm;
            }
        }
    }

    double sr = 48000.0;
    int order = 10, N = 1024, H = 256, hopCount = 0;
    int64_t pos = 0;
    float relCoef = 0.9f, invScale = 1.0f, redPeak = 0.0f;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, in[2], out[2], frame[2], logMag, red, tmp;
    std::vector<double> prefix;
    Settings settings;
    std::atomic<float> activity { 0.0f };
};
} // namespace aircheck
