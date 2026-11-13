#pragma once
#include <JuceHeader.h>

// ============================================================
// SimplePitchShifter
// ------------------------------------------------------------
// Простой питч-шифтер на кольцевом буфере с двумя читающими
// головками и кроссфейдом. Не даёт студийного качества, но
// работает в реальном времени, не дрейфует и не требует
// внешних зависимостей. Для гармонайзера этого достаточно.
// ============================================================
class SimplePitchShifter
{
public:
    void prepare (double /*sampleRate*/, int maxBlockSize)
    {
        bufferSize = juce::jmax (maxBlockSize * 4, 4096);
        buffer.assign (static_cast<size_t> (bufferSize), 0.0f);
        writePos     = 0;
        readPos      = static_cast<double> (bufferSize) * 0.75;
        newReadPos   = static_cast<double> (bufferSize) * 0.25;
        crossfadePos = -1.0;
        ratio        = 1.0f;
    }

    void reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos     = 0;
        readPos      = static_cast<double> (bufferSize) * 0.75;
        newReadPos   = static_cast<double> (bufferSize) * 0.25;
        crossfadePos = -1.0;
        ratio        = 1.0f;
    }

    void setShiftRatio (float newRatio)
    {
        ratio = juce::jlimit (0.25f, 4.0f, newRatio);
    }

    void process (juce::AudioBuffer<float>& block)
    {
        if (buffer.empty() || block.getNumChannels() < 1) return;

        auto* data = block.getWritePointer (0);
        const int n = block.getNumSamples();
        const double bufSize = static_cast<double> (bufferSize);
        const double minDelay = bufSize * 0.25;
        const double crossfadeLen = juce::jmin (bufSize * 0.1, 512.0);

        for (int i = 0; i < n; ++i)
        {
            buffer[static_cast<size_t> (writePos)] = data[i];
            writePos = (writePos + 1) % bufferSize;

            readPos += static_cast<double> (ratio);

            // Distance from readPos to writePos (positive = reading past)
            double dist = static_cast<double> (writePos) - readPos;
            while (dist < 0.0)        dist += bufSize;
            while (dist >= bufSize)   dist -= bufSize;

            // Trigger crossfade when the read head is about to overtake
            if (dist < minDelay && crossfadePos < 0.0)
            {
                crossfadePos = 0.0;
                newReadPos   = readPos - bufSize * 0.5;
            }

            while (readPos >= bufSize) readPos -= bufSize;
            while (readPos < 0.0)      readPos += bufSize;

            const int   i0    = static_cast<int> (readPos);
            const int   i1    = (i0 + 1) % bufferSize;
            const float frac  = static_cast<float> (readPos - static_cast<double> (i0));
            float sampleA = buffer[static_cast<size_t> (i0)] * (1.0f - frac)
                          + buffer[static_cast<size_t> (i1)] * frac;

            float outSample = sampleA;

            if (crossfadePos >= 0.0)
            {
                const float cfGain = juce::jlimit (
                    0.0f, 1.0f,
                    static_cast<float> (crossfadePos / crossfadeLen));

                while (newReadPos >= bufSize) newReadPos -= bufSize;
                while (newReadPos < 0.0)      newReadPos += bufSize;

                const int   ni0   = static_cast<int> (newReadPos);
                const int   ni1   = (ni0 + 1) % bufferSize;
                const float nfrac = static_cast<float> (newReadPos - static_cast<double> (ni0));
                const float sampleB = buffer[static_cast<size_t> (ni0)] * (1.0f - nfrac)
                                    + buffer[static_cast<size_t> (ni1)] * nfrac;

                outSample = sampleA * (1.0f - cfGain) + sampleB * cfGain;

                crossfadePos += 1.0;
                newReadPos   += static_cast<double> (ratio);

                if (crossfadePos >= crossfadeLen)
                {
                    readPos      = newReadPos;
                    crossfadePos = -1.0;
                }
            }

            data[i] = outSample;
        }
    }

private:
    std::vector<float> buffer;
    int    bufferSize   = 4096;
    int    writePos     = 0;
    double readPos      = 0.0;
    double newReadPos   = 0.0;
    double crossfadePos = -1.0;
    float  ratio        = 1.0f;
};

// ============================================================
// YIN pitch detector
// ============================================================
class YinPitchDetector
{
public:
    void prepare (double sampleRate, int minBufferSize);
    void pushSamples (const float* data, int n);
    float detect();
    void reset();
private:
    std::vector<float> buffer, ordered, diff, diffNorm;
    double sampleRate = 44100.0;
    int bufferSize = 2048;
    int writePos = 0;
    int minPeriod = 44;
    int maxPeriod = 1024;
    float threshold = 0.15f;
};

// ============================================================
// Main processor
// ============================================================
class VoiceMultiplierProcessor : public juce::AudioProcessor
{
public:
    VoiceMultiplierProcessor();
    ~VoiceMultiplierProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock; // unhide double overload

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "VoiceMultiplier"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.2; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    float getDetectedPitchHz() const { return detectedPitchHz.load(); }

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameters();

    static constexpr int kMaxVoices = 32;

    struct VoiceSlot
    {
        int   midiNote    = -1;
        float targetGain  = 0.0f;
        float currentGain = 0.0f;
        std::unique_ptr<SimplePitchShifter> shifter;
        std::vector<float> delayLine;
        int   writePos     = 0;
        int   delaySamples = 0;
    };

    std::array<VoiceSlot, kMaxVoices> voices;
    YinPitchDetector pitchDetector;

    std::array<float, 5> pitchHistory { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    int   pitchHistCount = 0;
    int   pitchHistIdx   = 0;
    float smoothedPitchHz = 0.0f;
    std::atomic<float> detectedPitchHz { 0.0f };

    double currentSampleRate = 44100.0;
    int    pitchDetectCounter = 0;
    static constexpr int kPitchDetectInterval = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoiceMultiplierProcessor)
};