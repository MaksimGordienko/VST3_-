#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// ============================================================
// Look & feel
// ============================================================
namespace Palette
{
    inline const juce::Colour bgTop    { 0xff0e1119 };
    inline const juce::Colour bgBottom { 0xff181d2c };
    inline const juce::Colour card     { 0xff1b2130 };
    inline const juce::Colour cardEdge { 0xff2a3350 };
    inline const juce::Colour track    { 0xff2a3148 };
    inline const juce::Colour text     { 0xffeef1fb };
    inline const juce::Colour textDim  { 0xff8a93b0 };
    inline const juce::Colour accent   { 0xff39e0c0 };   // mint
    inline const juce::Colour accent2  { 0xff8b6cff };   // violet
    inline const juce::Colour warn     { 0xffffb454 };   // amber (out of tune)
}

class VoiceLookAndFeel : public juce::LookAndFeel_V4
{
public:
    VoiceLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    juce::Font getLabelFont (juce::Label&) override;
};

/** A rotary slider with a caption on top and a value box below. */
class LabeledKnob : public juce::Component
{
public:
    LabeledKnob (juce::AudioProcessorValueTreeState&, const juce::String& paramID,
                 const juce::String& caption);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    juce::Slider slider;
    juce::String caption;
    int captionHeight = 18;
    std::unique_ptr<SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LabeledKnob)
};

// ============================================================
// Editor
// ============================================================
class VoiceMultiplierEditor : public juce::AudioProcessorEditor,
                              private juce::Timer
{
public:
    explicit VoiceMultiplierEditor (VoiceMultiplierProcessor&);
    ~VoiceMultiplierEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    void drawHeader (juce::Graphics&) const;
    void drawPitchCard (juce::Graphics&) const;
    void drawKeyboardCard (juce::Graphics&) const;
    static void drawCard (juce::Graphics&, juce::Rectangle<float>);

    VoiceMultiplierProcessor& audioProcessor;
    VoiceLookAndFeel laf;

    LabeledKnob voicesKnob, detuneKnob, delayKnob, widthKnob, dryWetKnob;

    // Live display state (updated by the timer)
    bool  pitchValid    = false;
    int   displayNote   = 0;
    float displayHz     = 0.0f;
    float targetCents   = 0.0f;
    float smoothedCents = 0.0f;
    int   missedTicks   = 0;
    int   activeVoices  = 0;
    int   maxVoices     = 0;

    juce::Rectangle<int> dynamicArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoiceMultiplierEditor)
};
