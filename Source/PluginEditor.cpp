#include "PluginEditor.h"

namespace
{
// The UI is laid out in a fixed "design" coordinate space and scaled to the
// actual window size, so the whole editor can be resized smoothly.
constexpr float kBaseW = 620.0f;
constexpr float kBaseH = 420.0f;

const juce::Rectangle<float> kPitchCard    { 20.0f,  70.0f, 580.0f,  96.0f };
const juce::Rectangle<float> kKeyboardCard { 20.0f, 174.0f, 580.0f,  58.0f };
const juce::Rectangle<float> kKnobCard     { 20.0f, 242.0f, 580.0f, 158.0f };

constexpr int kKeyboardFirstNote = 36;   // C2
constexpr int kKeyboardLastNote  = 96;   // C7

bool isBlackKey (int note) noexcept
{
    switch (note % 12)
    {
        case 1: case 3: case 6: case 8: case 10: return true;
        default: return false;
    }
}

juce::String noteName (int note)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[(note % 12 + 12) % 12]) + juce::String (note / 12 - 1);
}

void drawTextF (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Justification j)
{
    g.drawText (text, r, j, false);
}

juce::Font makeFont (float size, bool bold = false, float kerning = 0.0f)
{
    return juce::Font (size, bold ? juce::Font::bold : juce::Font::plain).withExtraKerningFactor (kerning);
}
} // namespace

// ============================================================
// VoiceLookAndFeel
// ============================================================
VoiceLookAndFeel::VoiceLookAndFeel()
{
    setColour (juce::Label::textColourId,                 Palette::text);
    setColour (juce::TextEditor::backgroundColourId,      Palette::bgTop);
    setColour (juce::TextEditor::textColourId,            Palette::text);
    setColour (juce::TextEditor::highlightColourId,       Palette::accent.withAlpha (0.35f));
    setColour (juce::TextEditor::highlightedTextColourId, Palette::text);
    setColour (juce::TextEditor::outlineColourId,         Palette::cardEdge);
    setColour (juce::TextEditor::focusedOutlineColourId,  Palette::accent);
    setColour (juce::CaretComponent::caretColourId,       Palette::accent);
}

juce::Font VoiceLookAndFeel::getLabelFont (juce::Label& label)
{
    return makeFont (juce::jmax (10.0f, static_cast<float> (label.getHeight()) * 0.62f), true);
}

void VoiceLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float pos, float startAngle, float endAngle,
                                         juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const float radius  = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto  centre  = bounds.getCentre();
    const float toAngle = startAngle + pos * (endAngle - startAngle);
    const float trackW  = juce::jmax (3.0f, radius * 0.13f);
    const float arcR    = radius - trackW * 0.5f;
    const bool  active  = slider.isMouseOverOrDragging() && slider.isEnabled();

    // Soft glow while hovering / dragging
    if (active)
    {
        juce::ColourGradient glow (Palette::accent.withAlpha (0.20f), centre.x, centre.y,
                                   Palette::accent.withAlpha (0.0f),  centre.x, centre.y - radius * 1.25f, true);
        g.setGradientFill (glow);
        g.fillEllipse (centre.x - radius * 1.25f, centre.y - radius * 1.25f, radius * 2.5f, radius * 2.5f);
    }

    // Background track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (Palette::track);
    g.strokePath (track, juce::PathStrokeType (trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Value arc (violet -> mint)
    if (pos > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, toAngle, true);
        g.setGradientFill (juce::ColourGradient (Palette::accent2, centre.x - arcR, centre.y,
                                                 Palette::accent,  centre.x + arcR, centre.y, false));
        g.strokePath (value, juce::PathStrokeType (trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Knob body
    const float bodyR = arcR - trackW * 1.5f;
    g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xff343c58), centre.y - bodyR,
                                                       juce::Colour (0xff1a2033), centre.y + bodyR));
    g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
    g.setColour (active ? Palette::accent.withAlpha (0.8f) : juce::Colour (0xff465078));
    g.drawEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.2f);

    // Pointer
    const float pw = juce::jmax (2.5f, bodyR * 0.14f);
    juce::Path pointer;
    pointer.addRoundedRectangle (-pw * 0.5f, -bodyR * 0.88f, pw, bodyR * 0.50f, pw * 0.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (toAngle).translated (centre.x, centre.y));
    g.setColour (Palette::text);
    g.fillPath (pointer);
}

// ============================================================
// LabeledKnob
// ============================================================
LabeledKnob::LabeledKnob (juce::AudioProcessorValueTreeState& state, const juce::String& paramID,
                          const juce::String& captionText)
    : caption (captionText)
{
    addAndMakeVisible (slider);
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 22);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (200);

    slider.setColour (juce::Slider::textBoxTextColourId,       Palette::text);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxHighlightColourId,  Palette::accent.withAlpha (0.35f));

    // Text, range, default value (double-click) all come from the parameter.
    attachment = std::make_unique<SliderAttachment> (state, paramID, slider);
}

void LabeledKnob::paint (juce::Graphics& g)
{
    g.setColour (Palette::textDim);
    g.setFont (makeFont (juce::jmax (9.0f, static_cast<float> (getHeight()) * 0.085f), true, 0.12f));
    drawTextF (g, caption.toUpperCase(), getLocalBounds().removeFromTop (captionHeight).toFloat(),
               juce::Justification::centred);
}

void LabeledKnob::resized()
{
    auto area = getLocalBounds();
    captionHeight = juce::roundToInt (static_cast<float> (getHeight()) * 0.12f);
    area.removeFromTop (captionHeight);

    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false,
                            juce::jmax (40, getWidth() - 16),
                            juce::jmax (16, juce::roundToInt (static_cast<float> (getHeight()) * 0.15f)));
    slider.setBounds (area);
}

// ============================================================
// VoiceMultiplierEditor
// ============================================================
VoiceMultiplierEditor::VoiceMultiplierEditor (VoiceMultiplierProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p),
      voicesKnob (p.apvts, "voices", "Polyphony"),
      detuneKnob (p.apvts, "detune", "Detune"),
      delayKnob  (p.apvts, "delay",  "Voice Delay"),
      widthKnob  (p.apvts, "width",  "Stereo Width"),
      dryWetKnob (p.apvts, "dryWet", "Dry / Wet")
{
    setLookAndFeel (&laf);

    for (auto* k : { &voicesKnob, &detuneKnob, &delayKnob, &widthKnob, &dryWetKnob })
        addAndMakeVisible (*k);

    setResizable (true, true);
    setResizeLimits (496, 336, 1240, 840);          // keeps the 620:420 aspect ratio
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (static_cast<double> (kBaseW / kBaseH));
    setSize (static_cast<int> (kBaseW), static_cast<int> (kBaseH));

    startTimerHz (30);
}

VoiceMultiplierEditor::~VoiceMultiplierEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void VoiceMultiplierEditor::timerCallback()
{
    const float hz = audioProcessor.getDetectedPitchHz();

    if (hz > 20.0f && hz < 2000.0f)
    {
        const double midi = 69.0 + 12.0 * std::log2 (static_cast<double> (hz) / 440.0);
        displayNote = juce::roundToInt (midi);
        targetCents = static_cast<float> ((midi - displayNote) * 100.0);
        displayHz   = hz;
        pitchValid  = true;
        missedTicks = 0;
    }
    else if (++missedTicks > 6)      // short hold so the readout does not flicker
    {
        pitchValid  = false;
        targetCents = 0.0f;
    }

    smoothedCents += (targetCents - smoothedCents) * 0.35f;
    activeVoices   = audioProcessor.getActiveVoiceCount();
    maxVoices      = juce::roundToInt (audioProcessor.apvts.getRawParameterValue ("voices")->load());

    repaint (dynamicArea);
}

void VoiceMultiplierEditor::resized()
{
    const float s = static_cast<float> (getWidth()) / kBaseW;

    const float knobW = kKnobCard.getWidth() / 5.0f;
    int i = 0;
    for (auto* k : { &voicesKnob, &detuneKnob, &delayKnob, &widthKnob, &dryWetKnob })
    {
        juce::Rectangle<float> r (kKnobCard.getX() + knobW * static_cast<float> (i++),
                                  kKnobCard.getY() + 4.0f, knobW, kKnobCard.getHeight() - 8.0f);
        k->setBounds ((r * s).toNearestInt());
    }

    dynamicArea = (kPitchCard.getUnion (kKeyboardCard) * s).getSmallestIntegerContainer().expanded (2);
}

// ---------------------------------------------------------------- painting
void VoiceMultiplierEditor::drawCard (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (Palette::card);
    g.fillRoundedRectangle (r, 14.0f);
    g.setColour (Palette::cardEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, 1.0f);
}

void VoiceMultiplierEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient::vertical (Palette::bgTop, 0.0f,
                                                       Palette::bgBottom, static_cast<float> (getHeight())));
    g.fillAll();

    g.addTransform (juce::AffineTransform::scale (static_cast<float> (getWidth()) / kBaseW));

    drawHeader (g);
    drawPitchCard (g);
    drawKeyboardCard (g);
    drawCard (g, kKnobCard);     // the knobs themselves are child components on top
}

void VoiceMultiplierEditor::drawHeader (juce::Graphics& g) const
{
    // Title: "VOICE" in white, "MULTIPLIER" in the accent colour
    const auto titleFont = makeFont (25.0f, true, 0.10f);
    g.setFont (titleFont);
    const float voiceW = titleFont.getStringWidthFloat ("VOICE ");

    g.setColour (Palette::text);
    drawTextF (g, "VOICE", juce::Rectangle<float> (28.0f, 12.0f, voiceW + 4.0f, 32.0f),
                juce::Justification::centredLeft);
    g.setColour (Palette::accent);
    drawTextF (g, "MULTIPLIER", juce::Rectangle<float> (28.0f + voiceW, 12.0f, 260.0f, 32.0f),
                juce::Justification::centredLeft);

    g.setColour (Palette::textDim);
    g.setFont (makeFont (10.5f, true, 0.22f));
    drawTextF (g, "MIDI-CONTROLLED VOCAL HARMONIZER", juce::Rectangle<float> (29.0f, 41.0f, 320.0f, 14.0f),
                juce::Justification::centredLeft);

    // "VST3" pill
    const juce::Rectangle<float> pill (528.0f, 20.0f, 72.0f, 24.0f);
    g.setColour (Palette::accent2.withAlpha (0.16f));
    g.fillRoundedRectangle (pill, 12.0f);
    g.setColour (Palette::accent2);
    g.drawRoundedRectangle (pill.reduced (0.5f), 12.0f, 1.0f);
    g.setFont (makeFont (11.5f, true, 0.18f));
    drawTextF (g, "VST3", pill, juce::Justification::centred);

    // Accent separator line
    juce::ColourGradient line (Palette::accent2.withAlpha (0.0f), 20.0f, 0.0f,
                               Palette::accent.withAlpha (0.0f), 600.0f, 0.0f, false);
    line.addColour (0.30, Palette::accent2.withAlpha (0.9f));
    line.addColour (0.70, Palette::accent.withAlpha (0.9f));
    g.setGradientFill (line);
    g.fillRect (20.0f, 61.0f, 580.0f, 1.5f);
}

void VoiceMultiplierEditor::drawPitchCard (juce::Graphics& g) const
{
    drawCard (g, kPitchCard);

    const auto& c = kPitchCard;
    const auto label = [&g] (const juce::String& t, juce::Rectangle<float> r, juce::Justification j)
    {
        g.setColour (Palette::textDim);
        g.setFont (makeFont (10.0f, true, 0.2f));
        drawTextF (g, t, r, j);
    };

    // Column dividers
    g.setColour (Palette::cardEdge);
    g.fillRect (c.getX() + 190.0f, c.getY() + 14.0f, 1.0f, c.getHeight() - 28.0f);
    g.fillRect (c.getX() + 420.0f, c.getY() + 14.0f, 1.0f, c.getHeight() - 28.0f);

    // ---- Input pitch ----
    label ("INPUT PITCH", { c.getX() + 20.0f, c.getY() + 11.0f, 150.0f, 14.0f }, juce::Justification::centredLeft);

    g.setFont (makeFont (44.0f, true));
    g.setColour (pitchValid ? Palette::text : Palette::textDim.withAlpha (0.6f));
    drawTextF (g, pitchValid ? noteName (displayNote) : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")),
                { c.getX() + 18.0f, c.getY() + 26.0f, 160.0f, 48.0f }, juce::Justification::centredLeft);

    g.setFont (makeFont (13.0f));
    g.setColour (Palette::textDim);
    drawTextF (g, pitchValid ? juce::String (displayHz, 1) + " Hz" : juce::String ("no signal"),
                { c.getX() + 20.0f, c.getY() + 72.0f, 150.0f, 16.0f }, juce::Justification::centredLeft);

    // ---- Tuning meter ----
    const float mx = c.getX() + 216.0f, mw = 176.0f, my = c.getY() + 50.0f;
    label ("TUNING", { mx, c.getY() + 11.0f, 80.0f, 14.0f }, juce::Justification::centredLeft);

    const bool inTune = pitchValid && std::abs (smoothedCents) < 10.0f;
    if (pitchValid)
    {
        g.setColour (inTune ? Palette::accent : Palette::warn);
        g.setFont (makeFont (12.0f, true));
        drawTextF (g, juce::String::formatted ("%+d ct", juce::roundToInt (smoothedCents)),
                    { mx + mw - 70.0f, c.getY() + 10.0f, 70.0f, 16.0f }, juce::Justification::centredRight);
    }

    g.setColour (Palette::track);
    g.fillRoundedRectangle (mx, my, mw, 6.0f, 3.0f);

    g.setColour (Palette::textDim.withAlpha (0.55f));
    for (int t = -2; t <= 2; ++t)
    {
        const float tx = mx + mw * 0.5f + static_cast<float> (t) * mw * 0.25f;
        g.fillRect (tx - 0.5f, my - (t == 0 ? 7.0f : 4.0f), 1.0f, (t == 0 ? 20.0f : 14.0f));
    }

    g.setFont (makeFont (10.0f));
    g.setColour (Palette::textDim);
    drawTextF (g, "-50", { mx - 14.0f, my + 17.0f, 28.0f, 12.0f }, juce::Justification::centred);
    drawTextF (g, "0",   { mx + mw * 0.5f - 14.0f, my + 17.0f, 28.0f, 12.0f }, juce::Justification::centred);
    drawTextF (g, "+50", { mx + mw - 14.0f, my + 17.0f, 28.0f, 12.0f }, juce::Justification::centred);

    if (pitchValid)
    {
        const float norm = juce::jlimit (-1.0f, 1.0f, smoothedCents / 50.0f);
        const float cx   = mx + mw * 0.5f + norm * mw * 0.5f;
        const auto  col  = inTune ? Palette::accent : Palette::warn;

        g.setColour (col.withAlpha (0.45f));
        g.fillRoundedRectangle (juce::jmin (cx, mx + mw * 0.5f), my,
                                std::abs (cx - (mx + mw * 0.5f)), 6.0f, 3.0f);

        g.setColour (col.withAlpha (0.25f));
        g.fillEllipse (cx - 11.0f, my + 3.0f - 11.0f, 22.0f, 22.0f);
        g.setColour (col);
        g.fillEllipse (cx - 6.5f, my + 3.0f - 6.5f, 13.0f, 13.0f);
    }

    // ---- Voices ----
    label ("VOICES", { c.getX() + 446.0f, c.getY() + 11.0f, 100.0f, 14.0f }, juce::Justification::centredLeft);

    const auto bigFont = makeFont (44.0f, true);
    const juce::String countText (activeVoices);
    g.setFont (bigFont);
    g.setColour (activeVoices > 0 ? Palette::accent : Palette::textDim.withAlpha (0.6f));
    drawTextF (g, countText, { c.getX() + 444.0f, c.getY() + 26.0f, 90.0f, 48.0f }, juce::Justification::centredLeft);

    const float countW = bigFont.getStringWidthFloat (countText);
    g.setFont (makeFont (15.0f, true));
    g.setColour (Palette::textDim);
    drawTextF (g, "/ " + juce::String (maxVoices),
                { c.getX() + 450.0f + countW, c.getY() + 46.0f, 70.0f, 22.0f }, juce::Justification::centredLeft);
}

void VoiceMultiplierEditor::drawKeyboardCard (juce::Graphics& g) const
{
    drawCard (g, kKeyboardCard);

    const auto area = kKeyboardCard.reduced (12.0f, 9.0f);

    int whiteCount = 0;
    for (int n = kKeyboardFirstNote; n <= kKeyboardLastNote; ++n)
        if (! isBlackKey (n)) ++whiteCount;

    const float keyW   = area.getWidth() / static_cast<float> (whiteCount);
    const float blackW = keyW * 0.62f;
    const float blackH = area.getHeight() * 0.62f;

    const int detectedNote = pitchValid ? displayNote : -1;

    // White keys
    int whiteIndex = 0;
    for (int n = kKeyboardFirstNote; n <= kKeyboardLastNote; ++n)
    {
        if (isBlackKey (n)) continue;

        const juce::Rectangle<float> k (area.getX() + keyW * static_cast<float> (whiteIndex++),
                                        area.getY(), keyW, area.getHeight());
        const bool held = audioProcessor.isNoteHeld (n);

        if (held)
            g.setGradientFill (juce::ColourGradient::vertical (Palette::accent.brighter (0.2f), k.getY(),
                                                               Palette::accent.darker (0.2f),   k.getBottom()));
        else
            g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xffd9deee), k.getY(),
                                                               juce::Colour (0xffaab2cc), k.getBottom()));
        g.fillRoundedRectangle (k.reduced (0.6f, 0.0f), 2.5f);

        if (n == detectedNote)
        {
            g.setColour (Palette::accent2);
            g.fillEllipse (k.getCentreX() - 3.0f, k.getBottom() - 9.0f, 6.0f, 6.0f);
        }
    }

    // Black keys
    whiteIndex = 0;
    for (int n = kKeyboardFirstNote; n <= kKeyboardLastNote; ++n)
    {
        if (! isBlackKey (n)) { ++whiteIndex; continue; }

        const juce::Rectangle<float> k (area.getX() + keyW * static_cast<float> (whiteIndex) - blackW * 0.5f,
                                        area.getY(), blackW, blackH);
        const bool held = audioProcessor.isNoteHeld (n);

        if (held)
            g.setGradientFill (juce::ColourGradient::vertical (Palette::accent, k.getY(),
                                                               Palette::accent.darker (0.5f), k.getBottom()));
        else
            g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xff2b3350), k.getY(),
                                                               juce::Colour (0xff0e1119), k.getBottom()));
        g.fillRoundedRectangle (k, 2.0f);

        if (n == detectedNote)
        {
            g.setColour (Palette::accent2);
            g.fillEllipse (k.getCentreX() - 2.5f, k.getBottom() - 8.0f, 5.0f, 5.0f);
        }
    }
}
