#pragma once
#include <JuceHeader.h>
#include "Dsp.h"

// ============================================================
// Main processor
// ============================================================
class VoiceMultiplierProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kMaxVoices = 32;

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

    // ---- Read-only state for the editor (thread-safe) ----
    float getDetectedPitchHz() const noexcept   { return detectedPitchHz.load(); }
    int   getActiveVoiceCount() const noexcept  { return activeVoiceCount.load(); }
    bool  isNoteHeld (int note) const noexcept
    {
        if (note < 0 || note > 127) return false;
        return (heldNotes[static_cast<size_t> (note >> 6)].load() >> (note & 63)) & 1u;
    }

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameters();

    void allocateScratch (int numSamples);
    void updatePitchEstimate();
    void handleMidi (const juce::MidiBuffer& midi, int maxVoices);
    void setNoteHeld (int note, bool held) noexcept;

    struct VoiceSlot
    {
        int   midiNote    = -1;      // -1 = slot is free
        float targetGain  = 0.0f;
        float currentGain = 0.0f;

        // Smoothed per-voice parameters (snapped to their targets on the first block)
        bool  needsInit    = true;
        float ratio        = 1.0f;
        float panL         = 0.7071f;
        float panR         = 0.7071f;
        float delaySamples = 0.0f;

        vm::SimplePitchShifter shifter;
        std::vector<float>     delayLine;
        int                    writePos = 0;
    };

    std::array<VoiceSlot, kMaxVoices> voices;
    vm::YinPitchDetector pitchDetector;
    std::array<vm::SimpleDelay, 2> dryDelay;   // keeps the dry path aligned with the shifted voices

    // Pitch tracking
    std::array<float, 5> pitchHistory { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    int   pitchHistCount   = 0;
    int   pitchHistIdx     = 0;
    float smoothedPitchHz  = 0.0f;
    int   unvoicedSamples  = 0;
    int   pitchDetectCounter  = 0;
    int   pitchDetectInterval = 512;
    std::atomic<float> detectedPitchHz { 0.0f };

    // UI feedback
    std::atomic<int> activeVoiceCount { 0 };
    std::array<std::atomic<uint64_t>, 2> heldNotes;

    // Pre-allocated scratch memory (no allocations on the audio thread)
    std::vector<float> monoScratch, pitchedScratch, wetL, wetR, normRamp, mixRamp;
    int scratchCapacity = 0;

    // Global smoothed values
    float smoothedNorm   = 1.0f;
    float smoothedWetMix = 0.0f;

    double currentSampleRate = 44100.0;

    // Cached parameter pointers
    std::atomic<float>* voicesParam = nullptr;
    std::atomic<float>* detuneParam = nullptr;
    std::atomic<float>* delayParam  = nullptr;
    std::atomic<float>* widthParam  = nullptr;
    std::atomic<float>* dryWetParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoiceMultiplierProcessor)
};
