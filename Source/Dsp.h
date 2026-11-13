#pragma once

// ============================================================
// Dsp.h - JUCE-independent DSP building blocks.
// Kept free of JUCE so it can be unit-tested as a plain C++ program
// (see Tests/DspTests.cpp).
// ============================================================

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace vm
{

// ------------------------------------------------------------
// SimplePitchShifter
// ------------------------------------------------------------
// Classic "delay-line modulation" pitch shifter: two read taps move
// through a delay line at a speed of (1 - ratio) samples per sample and
// are crossfaded with a Hann-shaped window, so that the tap that wraps
// around is always silent at that moment. This works for BOTH pitch-up
// (ratio > 1) and pitch-down (ratio < 1) and has a constant, host-block-
// size independent latency of windowSize / 2 samples.
// ------------------------------------------------------------
class SimplePitchShifter
{
public:
    static constexpr double kWindowSeconds = 0.040;

    static int windowSizeFor (double sampleRate)
    {
        return std::max (64, static_cast<int> (std::lround (sampleRate * kWindowSeconds)));
    }

    /** Average delay introduced by the shifter, in samples. */
    static int latencyFor (double sampleRate) { return windowSizeFor (sampleRate) / 2; }

    void prepare (double sampleRate)
    {
        windowSize = windowSizeFor (sampleRate);

        int size = 1;
        while (size < windowSize + 4)
            size <<= 1;

        buffer.assign (static_cast<size_t> (size), 0.0f);
        mask = size - 1;
        reset();
    }

    void reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
        phase    = 0.0f;
        ratio    = 1.0f;
    }

    void setShiftRatio (float newRatio)
    {
        ratio = std::min (4.0f, std::max (0.25f, newRatio));
    }

    /** `in` and `out` may point to the same memory. */
    void process (const float* in, float* out, int numSamples) noexcept
    {
        if (buffer.empty())
        {
            if (in != out)
                std::copy (in, in + numSamples, out);
            return;
        }

        constexpr float pi = 3.14159265358979323846f;
        const float w        = static_cast<float> (windowSize);
        const float phaseInc = (1.0f - ratio) / w;

        for (int i = 0; i < numSamples; ++i)
        {
            writePos = (writePos + 1) & mask;
            buffer[static_cast<size_t> (writePos)] = in[i];

            float p1 = phase + 0.5f;
            if (p1 >= 1.0f) p1 -= 1.0f;

            // sin^2(pi*p) and cos^2(pi*p) always sum to 1 -> constant gain
            const float s  = std::sin (pi * phase);
            const float g0 = s * s;
            const float g1 = 1.0f - g0;

            out[i] = readTap (phase, w) * g0 + readTap (p1, w) * g1;

            phase += phaseInc;
            phase -= std::floor (phase);
        }
    }

private:
    float readTap (float p, float w) const noexcept
    {
        const float d    = p * w;
        const int   di   = static_cast<int> (d);
        const float frac = d - static_cast<float> (di);
        const int   i0   = (writePos - di) & mask;
        const int   i1   = (i0 - 1) & mask;
        const float a    = buffer[static_cast<size_t> (i0)];
        const float b    = buffer[static_cast<size_t> (i1)];
        return a + (b - a) * frac;
    }

    std::vector<float> buffer;
    int   windowSize = 1764;
    int   mask       = 0;
    int   writePos   = 0;
    float phase      = 0.0f;
    float ratio      = 1.0f;
};

// ------------------------------------------------------------
// SimpleDelay - fixed integer delay (used to time-align the dry signal)
// ------------------------------------------------------------
class SimpleDelay
{
public:
    void prepare (int delaySamples)
    {
        len = std::max (0, delaySamples);
        buf.assign (static_cast<size_t> (len) + 1, 0.0f);
        pos = 0;
    }

    float process (float x) noexcept
    {
        const int size = len + 1;
        buf[static_cast<size_t> (pos)] = x;
        int r = pos - len;
        if (r < 0) r += size;
        pos = (pos + 1 == size) ? 0 : pos + 1;
        return buf[static_cast<size_t> (r)];
    }

private:
    std::vector<float> buf { 0.0f };
    int len = 0;
    int pos = 0;
};

// ------------------------------------------------------------
// YIN pitch detector
// ------------------------------------------------------------
// At sample rates above ~64 kHz the input is decimated (boxcar average)
// so the analysis window always covers the same amount of time and the
// CPU cost stays flat. Valid range: ~60 Hz .. 1 kHz.
// ------------------------------------------------------------
class YinPitchDetector
{
public:
    void prepare (double sr)
    {
        decimation = std::max (1, static_cast<int> (std::lround (sr / 48000.0)));
        sampleRate = sr / static_cast<double> (decimation);

        bufferSize = 2048;
        buffer.assign  (static_cast<size_t> (bufferSize), 0.0f);
        ordered.assign (static_cast<size_t> (bufferSize), 0.0f);

        minPeriod = std::max (2, static_cast<int> (std::floor (sampleRate / 1000.0)));
        maxPeriod = std::min (bufferSize / 2 - 1,
                              static_cast<int> (std::ceil (sampleRate / 60.0)));

        diff.assign     (static_cast<size_t> (maxPeriod + 2), 0.0f);
        diffNorm.assign (static_cast<size_t> (maxPeriod + 2), 1.0f);
        writePos = 0;
        accum = 0.0f;
        accumCount = 0;
    }

    /** Number of input samples after which detect() should be called again. */
    int getRecommendedInterval() const noexcept { return 512 * decimation; }

    void pushSamples (const float* data, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            accum += data[i];
            if (++accumCount < decimation)
                continue;

            buffer[static_cast<size_t> (writePos)] = accum / static_cast<float> (decimation);
            if (++writePos == bufferSize) writePos = 0;
            accum = 0.0f;
            accumCount = 0;
        }
    }

    void reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
        accum = 0.0f;
        accumCount = 0;
    }

    /** Returns the detected fundamental in Hz, or 0 if unvoiced. */
    float detect() noexcept
    {
        const int N = bufferSize;
        const int W = N / 2;

        for (int i = 0; i < N; ++i)
            ordered[static_cast<size_t> (i)] =
                buffer[static_cast<size_t> ((writePos + i) % N)];

        for (int tau = 1; tau <= maxPeriod; ++tau)
        {
            float sum = 0.0f;
            for (int j = 0; j < W; ++j)
            {
                const float d = ordered[static_cast<size_t> (j)]
                              - ordered[static_cast<size_t> (j + tau)];
                sum += d * d;
            }
            diff[static_cast<size_t> (tau)] = sum;
        }

        diffNorm[0] = 1.0f;
        float running = 0.0f;
        for (int tau = 1; tau <= maxPeriod; ++tau)
        {
            running += diff[static_cast<size_t> (tau)];
            diffNorm[static_cast<size_t> (tau)] =
                running > 1e-10f
                    ? diff[static_cast<size_t> (tau)] * static_cast<float> (tau) / running
                    : 1.0f;
        }

        int tauEst = -1;
        for (int tau = minPeriod; tau <= maxPeriod; ++tau)
        {
            if (diffNorm[static_cast<size_t> (tau)] < threshold)
            {
                while (tau + 1 <= maxPeriod
                       && diffNorm[static_cast<size_t> (tau + 1)]
                          < diffNorm[static_cast<size_t> (tau)])
                    ++tau;
                tauEst = tau;
                break;
            }
        }
        if (tauEst < 0)
            return 0.0f;

        float betterTau = static_cast<float> (tauEst);
        if (tauEst > 1 && tauEst < maxPeriod)
        {
            const float s0 = diffNorm[static_cast<size_t> (tauEst - 1)];
            const float s1 = diffNorm[static_cast<size_t> (tauEst)];
            const float s2 = diffNorm[static_cast<size_t> (tauEst + 1)];
            const float denom = 2.0f * (2.0f * s1 - s2 - s0);
            if (std::abs (denom) > 1e-10f)
            {
                const float offset = (s2 - s0) / denom;
                betterTau += std::max (-1.0f, std::min (1.0f, offset));
            }
        }
        return static_cast<float> (sampleRate / static_cast<double> (betterTau));
    }

private:
    std::vector<float> buffer, ordered, diff, diffNorm;
    double sampleRate = 44100.0;
    int decimation = 1;
    int bufferSize = 2048;
    int writePos   = 0;
    int minPeriod  = 44;
    int maxPeriod  = 1024;
    int accumCount = 0;
    float accum    = 0.0f;
    float threshold = 0.15f;
};

} // namespace vm
