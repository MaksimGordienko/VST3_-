#include "PluginEditor.h"

VoiceMultiplierEditor::VoiceMultiplierEditor (VoiceMultiplierProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    auto setupLabel = [this] (juce::Label& l, const juce::String& t)
    {
        addAndMakeVisible (l);
        l.setText (t, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centredRight);
    };
    auto setupSlider = [this] (juce::Slider& s)
    {
        addAndMakeVisible (s);
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
    };

    setupLabel  (voicesLabel, "Max Polyphony");  setupSlider (voicesSlider);
    voicesSlider.setRange (1, 32, 1);
    voicesSlider.setNumDecimalPlacesToDisplay (0);

    setupLabel  (detuneLabel, "Detune (cents)"); setupSlider (detuneSlider);
    detuneSlider.setRange (0, 30, 0.1); detuneSlider.setTextValueSuffix (" ct");

    setupLabel  (delayLabel,  "Voice Delay (ms)"); setupSlider (delaySlider);
    delaySlider.setRange (0, 30, 0.1); delaySlider.setTextValueSuffix (" ms");

    setupLabel  (widthLabel,  "Stereo Width");   setupSlider (widthSlider);
    widthSlider.setRange (0, 1, 0.01); widthSlider.setNumDecimalPlacesToDisplay (2);

    setupLabel  (dryWetLabel, "Dry/Wet");        setupSlider (dryWetSlider);
    dryWetSlider.setRange (0, 1, 0.01); dryWetSlider.setNumDecimalPlacesToDisplay (2);

    addAndMakeVisible (pitchLabel);
    pitchLabel.setJustificationType (juce::Justification::centred);
    pitchLabel.setColour (juce::Label::textColourId, juce::Colours::lightgreen);

    voicesAtt = std::make_unique<SliderAttachment>(processor.apvts, "voices",  voicesSlider);
    detuneAtt = std::make_unique<SliderAttachment>(processor.apvts, "detune",  detuneSlider);
    delayAtt  = std::make_unique<SliderAttachment>(processor.apvts, "delay",   delaySlider);
    widthAtt  = std::make_unique<SliderAttachment>(processor.apvts, "width",   widthSlider);
    dryWetAtt = std::make_unique<SliderAttachment>(processor.apvts, "dryWet",  dryWetSlider);

    startTimerHz (15);
    setSize (400, 310);
}

void VoiceMultiplierEditor::timerCallback()
{
    const float hz = processor.getDetectedPitchHz();
    if (hz > 20.0f && hz < 2000.0f)
    {
        const int midiNote = static_cast<int> (std::round (69.0 + 12.0 * std::log2 (hz / 440.0)));
        static const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
        const int octave = midiNote / 12 - 1;
        pitchLabel.setText ("Pitch: " + juce::String (names[(midiNote % 12 + 12) % 12])
                            + juce::String (octave) + "  (" + juce::String (hz, 1) + " Hz)",
                            juce::dontSendNotification);
    }
    else
    {
        pitchLabel.setText ("Pitch: -", juce::dontSendNotification);
    }
}

void VoiceMultiplierEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::darkgrey);
    g.setColour (juce::Colours::white);
    g.setFont (16.0f);
    g.drawFittedText ("Voice Multiplier - MIDI Harmonizer",
                      getLocalBounds().removeFromTop (28),
                      juce::Justification::centred, 1);
}

void VoiceMultiplierEditor::resized()
{
    auto area = getLocalBounds().reduced (12);
    area.removeFromTop (24);

    const int rowH   = 30;
    const int labelW = 130;

    auto layoutRow = [&] (juce::Label& l, juce::Slider& s)
    {
        auto row = area.removeFromTop (rowH);
        l.setBounds (row.removeFromLeft (labelW));
        s.setBounds (row);
        area.removeFromTop (4);
    };

    layoutRow (voicesLabel, voicesSlider);
    layoutRow (detuneLabel, detuneSlider);
    layoutRow (delayLabel,  delaySlider);
    layoutRow (widthLabel,  widthSlider);
    layoutRow (dryWetLabel, dryWetSlider);

    area.removeFromTop (8);
    pitchLabel.setBounds (area.removeFromTop (26));
}