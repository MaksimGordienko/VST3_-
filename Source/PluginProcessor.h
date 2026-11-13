#pragma once
#include <JuceHeader.h>

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
// PluginProcessor.h
#pragma once
#include <JuceHeader.h>

// Простой питч-шифтер на основе кольцевого буфера с интерполяцией.
// Не даёт студийного качества, но работает в реальном времени и не тянет
// внешних зависимостей. Для гармонайзера этого достаточно.
class SimplePitchShifter
{
public:
    void prepare (double sampleRate, int maxBlockSize)
    {
        bufferSize = juce::jmax (maxBlockSize * 4, 4096);
        buffer.assign (bufferSize, 0.0f);
        writePos = 0;
        readPos  = 0.0;
        ratio    = 1.0;
    }

    void reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
        readPos  = 0.0;
        ratio    = 1.0;
    }

    void setShiftRatio (float newRatio)
    {
        ratio = juce::jlimit (0.25f, 4.0f, newRatio);
    }

    // Обрабатывает блок сэмплов in-place.
    void process (juce::AudioBuffer<float>& block)
    {
        auto* data = block.getWritePointer (0);
        const int n = block.getNumSamples();
        const int bufSize = (int) buffer.size();

        for (int i = 0; i < n; ++i)
        {
            // Пишем входной сэмпл в кольцевой буфер
            buffer[writePos] = data[i];
            writePos = (writePos + 1) % bufSize;

            // Читаем с изменяемой скоростью
            readPos += ratio;
            while (readPos >= bufSize) readPos -= bufSize;
            while (readPos < 0)        readPos += bufSize;

            const int   i0   = (int) readPos;
            const int   i1   = (i0 + 1) % bufSize;
            const float frac = (float) (readPos - i0);

            data[i] = buffer[i0] * (1.0f - frac) + buffer[i1] * frac;
        }
    }

private:
    std::vector<float> buffer;
    int   bufferSize = 4096;
    int   writePos   = 0;
    double readPos   = 0.0;
    float ratio      = 1.0f;
};
class VoiceMultiplierProcessor : public juce::AudioProcessor
{
public:
    VoiceMultiplierProcessor();
    ~VoiceMultiplierProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

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