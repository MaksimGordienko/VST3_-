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
        std::unique_ptr<juce::dsp::PitchShifter<float>> shifter;
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