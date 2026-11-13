// Plain C++ unit tests for the JUCE-independent DSP code (Source/Dsp.h).
// Build with:  cmake -B build -DVM_BUILD_TESTS=ON && cmake --build build
// Run with:    ctest --test-dir build -C Release --output-on-failure

#include "Dsp.h"

#include <cstdio>
#include <vector>

namespace
{
constexpr double kPi = 3.14159265358979323846;
int failures = 0;

void check (bool ok, const char* what, double a = 0.0, double b = 0.0)
{
    std::printf ("[%s] %s", ok ? " OK " : "FAIL", what);
    if (! ok) { std::printf (" (got %.4f, expected %.4f)", a, b); ++failures; }
    std::printf ("\n");
}

std::vector<float> makeSine (double freq, double sr, int n, float amp = 0.5f)
{
    std::vector<float> x (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
        x[static_cast<size_t> (i)] = amp * static_cast<float> (std::sin (2.0 * kPi * freq * i / sr));
    return x;
}

// Voice-like harmonic signal (fundamental + 4 harmonics)
std::vector<float> makeHarmonic (double f0, double sr, int n)
{
    std::vector<float> x (static_cast<size_t> (n), 0.0f);
    for (int h = 1; h <= 5; ++h)
        for (int i = 0; i < n; ++i)
            x[static_cast<size_t> (i)] += (0.4f / static_cast<float> (h))
                * static_cast<float> (std::sin (2.0 * kPi * f0 * h * i / sr));
    return x;
}

// Frequency estimate from positive-going zero crossings (linear interpolation)
double estimateFreq (const std::vector<float>& x, int from, int to, double sr)
{
    double first = -1.0, last = -1.0;
    int count = 0;
    for (int i = from + 1; i < to; ++i)
    {
        const float a = x[static_cast<size_t> (i - 1)], b = x[static_cast<size_t> (i)];
        if (a < 0.0f && b >= 0.0f)
        {
            const double t = i - 1 + static_cast<double> (-a / (b - a));
            if (first < 0.0) first = t;
            last = t;
            ++count;
        }
    }
    return count > 1 ? (count - 1) * sr / (last - first) : 0.0;
}

void testShifter (double sr)
{
    // 250 Hz: the half-window (20 ms) holds exactly 5 periods, the ideal case
    // for a two-tap shifter. See testShifterOffAlignment() for the general case.
    const double f0 = 250.0;
    const int n = static_cast<int> (sr * 2.0);
    const auto in = makeSine (f0, sr, n);

    for (double ratio : { 0.5, 0.75, 0.9, 1.0, 1.26, 1.5, 2.0, 3.0 })
    {
        vm::SimplePitchShifter sh;
        sh.prepare (sr);
        sh.setShiftRatio (static_cast<float> (ratio));

        std::vector<float> out (static_cast<size_t> (n));
        // process in odd-sized blocks to make sure block size does not matter
        for (int pos = 0; pos < n;)
        {
            const int len = std::min (237, n - pos);
            sh.process (in.data() + pos, out.data() + pos, len);
            pos += len;
        }

        const int skip = static_cast<int> (sr * 0.5);
        const double f = estimateFreq (out, skip, n, sr);
        char msg[128];
        std::snprintf (msg, sizeof msg, "shifter %.0f Hz x %.2f -> %.1f Hz (sr %.0f)", f0, ratio, f0 * ratio, sr);
        check (std::abs (f / (f0 * ratio) - 1.0) < 0.01, msg, f, f0 * ratio);

        // No clicks: largest sample-to-sample step must stay close to that of
        // a clean sine at the target frequency (allow headroom for crossfades).
        float maxStep = 0.0f, maxAbs = 0.0f;
        for (int i = skip + 1; i < n; ++i)
        {
            maxStep = std::max (maxStep, std::abs (out[static_cast<size_t> (i)] - out[static_cast<size_t> (i - 1)]));
            maxAbs  = std::max (maxAbs, std::abs (out[static_cast<size_t> (i)]));
        }
        const double idealStep = 0.5 * 2.0 * kPi * f0 * ratio / sr;
        std::snprintf (msg, sizeof msg, "shifter x%.2f has no clicks (max step %.4f, ideal %.4f)", ratio, maxStep, idealStep);
        check (maxStep < 1.6 * idealStep + 0.002, msg, maxStep, idealStep);

        std::snprintf (msg, sizeof msg, "shifter x%.2f output level is sane (peak %.3f)", ratio, maxAbs);
        check (maxAbs < 0.75f && maxAbs > 0.25f, msg, maxAbs, 0.5);
    }
}


// A two-tap delay-line shifter is only exact when the tap spacing is a whole
// number of signal periods; otherwise the output pitch of a steady tone is
// biased by up to a few percent. This test documents that limit and guards
// against regressions (it must stay within 8 %).
void testShifterOffAlignment (double sr)
{
    const double f0 = 220.0;
    const int n = static_cast<int> (sr * 2.0);
    const auto in = makeSine (f0, sr, n);
    for (double ratio : { 0.75, 1.26, 2.0 })
    {
        vm::SimplePitchShifter sh;
        sh.prepare (sr);
        sh.setShiftRatio (static_cast<float> (ratio));
        std::vector<float> out (static_cast<size_t> (n));
        sh.process (in.data(), out.data(), n);
        const double f = estimateFreq (out, static_cast<int> (sr * 0.5), n, sr);
        char msg[128];
        std::snprintf (msg, sizeof msg, "shifter (unaligned 220 Hz) x%.2f: %.1f Hz vs ideal %.1f Hz (%.1f%%)",
                       ratio, f, f0 * ratio, 100.0 * (f / (f0 * ratio) - 1.0));
        check (std::abs (f / (f0 * ratio) - 1.0) < 0.08, msg, f, f0 * ratio);
    }
}

void testShifterLatency (double sr)
{
    vm::SimplePitchShifter sh;
    sh.prepare (sr);
    sh.setShiftRatio (1.0f);

    std::vector<float> in (static_cast<size_t> (sr * 0.2), 0.0f), out (in.size());
    in[100] = 1.0f;
    sh.process (in.data(), out.data(), static_cast<int> (in.size()));

    int peak = 0;
    for (size_t i = 0; i < out.size(); ++i)
        if (std::abs (out[i]) > std::abs (out[static_cast<size_t> (peak)])) peak = static_cast<int> (i);

    const int expected = 100 + vm::SimplePitchShifter::latencyFor (sr);
    check (std::abs (peak - expected) <= 1, "shifter latency at ratio 1.0 equals latencyFor()", peak, expected);
}

void testDelay()
{
    vm::SimpleDelay d;
    d.prepare (5);
    std::vector<float> out;
    for (int i = 0; i < 12; ++i) out.push_back (d.process (i == 0 ? 1.0f : 0.0f));
    check (out[5] == 1.0f && out[4] == 0.0f && out[6] == 0.0f, "SimpleDelay delays an impulse by exactly N samples");

    vm::SimpleDelay z;
    z.prepare (0);
    check (z.process (0.7f) == 0.7f, "SimpleDelay with 0 samples is transparent");
}

void testYin (double sr)
{
    for (double f0 : { 82.4, 110.0, 220.0, 440.0, 880.0 })
    {
        vm::YinPitchDetector yin;
        yin.prepare (sr);
        const auto x = makeHarmonic (f0, sr, 4096);
        yin.pushSamples (x.data(), static_cast<int> (x.size()));
        const float f = yin.detect();
        char msg[128];
        std::snprintf (msg, sizeof msg, "YIN detects %.1f Hz -> %.2f Hz (sr %.0f)", f0, f, sr);
        check (std::abs (f / f0 - 1.0) < 0.01, msg, f, f0);
    }

    vm::YinPitchDetector yin;
    yin.prepare (sr);
    std::vector<float> silence (4096, 0.0f);
    yin.pushSamples (silence.data(), 4096);
    check (yin.detect() == 0.0f, "YIN reports unvoiced for silence");
}
} // namespace

int main()
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        testShifter (sr);
        testShifterOffAlignment (sr);
        testShifterLatency (sr);
        testYin (sr);
    }
    testDelay();

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                 failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
