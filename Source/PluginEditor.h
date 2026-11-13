#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class VoiceMultiplierEditor : public juce::AudioProcessorEditor,
                              public juce::Timer
{
public:
    explicit VoiceMultiplierEditor (VoiceMultiplierProcessor&);
    ~VoiceMultiplierEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    VoiceMultiplierProcessor& processor;

    juce::Label voicesLabel, detuneLabel, delayLabel, widthLabel, dryWetLabel, pitchLabel;
    juce::Slider voicesSlider, detuneSlider, delaySlider, widthSlider, dryWetSlider;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttachment> voicesAtt, detuneAtt, delayAtt, widthAtt, dryWetAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoiceMultiplierEditor)
};