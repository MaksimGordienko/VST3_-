#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr float kMinPitchHz = 20.0f;
constexpr float kMaxPitchHz = 2000.0f;
constexpr double kMaxVoiceDelaySeconds = 0.030;   // upper end of the "Voice Delay" parameter
constexpr double kPitchHoldSeconds     = 0.4;     // keep the last pitch while the singer is unvoiced
}

// ============================================================
// Construction / parameters
// ============================================================
VoiceMultiplierProcessor::VoiceMultiplierProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameters())
{
    voicesParam = apvts.getRawParameterValue ("voices");
    detuneParam = apvts.getRawParameterValue ("detune");
    delayParam  = apvts.getRawParameterValue ("delay");
    widthParam  = apvts.getRawParameterValue ("width");
    dryWetParam = apvts.getRawParameterValue ("dryWet");

    for (auto& w : heldNotes)
        w.store (0);
}

juce::AudioProcessorValueTreeState::ParameterLayout
VoiceMultiplierProcessor::createParameters()
{
    using PID = juce::ParameterID;
    using FloatAttrs = juce::AudioParameterFloatAttributes;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterInt> (
        PID { "voices", 1 }, "Max Polyphony", 1, kMaxVoices, 4));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        PID { "detune", 1 }, "Detune (cents)",
        juce::NormalisableRange<float> (0.0f, 30.0f, 0.1f), 5.0f,
        FloatAttrs()
            .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " ct"; })
            .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue(); })));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        PID { "delay", 1 }, "Voice Delay (ms)",
        juce::NormalisableRange<float> (0.0f, 30.0f, 0.1f), 8.0f,
        FloatAttrs()
            .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " ms"; })
            .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue(); })));

    auto percent = FloatAttrs()
        .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; })
        .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue() / 100.0f; });

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        PID { "width", 1 }, "Stereo Width",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.8f, percent));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        PID { "dryWet", 1 }, "Dry/Wet",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.8f, percent));

    return layout;
}

// ============================================================
// Lifecycle
// ============================================================
void VoiceMultiplierProcessor::allocateScratch (int numSamples)
{
    const auto n = static_cast<size_t> (juce::jmax (1, numSamples));
    monoScratch.assign (n, 0.0f);
    pitchedScratch.assign (n, 0.0f);
    wetL.assign (n, 0.0f);
    wetR.assign (n, 0.0f);
    normRamp.assign (n, 1.0f);
    mixRamp.assign (n, 0.0f);
    scratchCapacity = static_cast<int> (n);
}

void VoiceMultiplierProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    allocateScratch (juce::jmax (samplesPerBlock, 512));

    const int delayLineSize = static_cast<int> (std::ceil (kMaxVoiceDelaySeconds * sampleRate)) + 8;

    for (auto& v : voices)
    {
        v.shifter.prepare (sampleRate);
        v.delayLine.assign (static_cast<size_t> (delayLineSize), 0.0f);
        v.writePos     = 0;
        v.midiNote     = -1;
        v.targetGain   = 0.0f;
        v.currentGain  = 0.0f;
        v.needsInit    = true;
        v.ratio        = 1.0f;
        v.delaySamples = 0.0f;
    }

    // The pitch shifter has a fixed latency; the dry signal is delayed by the
    // same amount and the host is told about it (plugin delay compensation).
    const int latency = vm::SimplePitchShifter::latencyFor (sampleRate);
    for (auto& d : dryDelay)
        d.prepare (latency);
    setLatencySamples (latency);

    pitchDetector.prepare (sampleRate);
    pitchDetector.reset();
    pitchDetectInterval = pitchDetector.getRecommendedInterval();
    pitchDetectCounter  = 0;
    pitchHistory.fill (0.0f);
    pitchHistCount  = 0;
    pitchHistIdx    = 0;
    smoothedPitchHz = 0.0f;
    unvoicedSamples = 0;

    smoothedNorm   = 1.0f;
    smoothedWetMix = 0.0f;

    detectedPitchHz.store (0.0f);
    activeVoiceCount.store (0);
    for (auto& w : heldNotes)
        w.store (0);
}

void VoiceMultiplierProcessor::releaseResources() {}

bool VoiceMultiplierProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // A microphone is usually a mono track, so accept mono or stereo on both sides.
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    const bool inOk  = in  == juce::AudioChannelSet::mono() || in  == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    return inOk && outOk;
}

// ============================================================
// Helpers
// ============================================================
void VoiceMultiplierProcessor::setNoteHeld (int note, bool held) noexcept
{
    if (note < 0 || note > 127) return;
    auto& word = heldNotes[static_cast<size_t> (note >> 6)];
    const uint64_t bit = uint64_t { 1 } << (note & 63);
    if (held) word.fetch_or (bit);
    else      word.fetch_and (~bit);
}

void VoiceMultiplierProcessor::updatePitchEstimate()
{
    const float detected = pitchDetector.detect();
    const bool  voiced   = detected > kMinPitchHz && detected < kMaxPitchHz;
    detectedPitchHz.store (voiced ? detected : 0.0f);

    if (voiced)
    {
        unvoicedSamples = 0;

        pitchHistory[static_cast<size_t> (pitchHistIdx)] = detected;
        pitchHistIdx = (pitchHistIdx + 1) % static_cast<int> (pitchHistory.size());
        if (pitchHistCount < static_cast<int> (pitchHistory.size()))
            ++pitchHistCount;

        // Median of the last few estimates rejects octave errors / outliers
        auto sorted = pitchHistory;
        std::sort (sorted.begin(), sorted.begin() + pitchHistCount);
        smoothedPitchHz = sorted[static_cast<size_t> (pitchHistCount / 2)];
    }
    else
    {
        // Hold the last pitch through short unvoiced gaps (consonants, breaths),
        // then forget it so the next phrase starts with a clean history.
        unvoicedSamples += pitchDetectInterval;
        if (unvoicedSamples > static_cast<int> (kPitchHoldSeconds * currentSampleRate))
        {
            smoothedPitchHz = 0.0f;
            pitchHistCount  = 0;
            pitchHistIdx    = 0;
            pitchHistory.fill (0.0f);
        }
    }
}

void VoiceMultiplierProcessor::handleMidi (const juce::MidiBuffer& midi, int maxVoices)
{
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();

        if (msg.isNoteOn())
        {
            const int note = msg.getNoteNumber();
            setNoteHeld (note, true);

            // Re-trigger of a note that is already sounding / releasing
            int existing = -1;
            for (int i = 0; i < kMaxVoices; ++i)
                if (voices[static_cast<size_t> (i)].midiNote == note)
                    { existing = i; break; }

            if (existing >= 0)
            {
                voices[static_cast<size_t> (existing)].targetGain = 1.0f;
                continue;
            }

            int activeCount = 0;
            for (const auto& v : voices)
                if (v.midiNote >= 0) ++activeCount;

            int slot = -1;
            if (activeCount < maxVoices)
                for (int i = 0; i < kMaxVoices; ++i)
                    if (voices[static_cast<size_t> (i)].midiNote < 0)
                        { slot = i; break; }

            if (slot < 0)  // voice stealing: prefer releasing voices, then the quietest one
            {
                float lowest = 2.0f;
                for (int i = 0; i < kMaxVoices; ++i)
                {
                    const auto& v = voices[static_cast<size_t> (i)];
                    if (v.targetGain < 0.001f && v.currentGain < lowest)
                        { lowest = v.currentGain; slot = i; }
                }
                if (slot < 0)
                {
                    lowest = 2.0f;
                    for (int i = 0; i < kMaxVoices; ++i)
                    {
                        const auto& v = voices[static_cast<size_t> (i)];
                        if (v.currentGain < lowest)
                            { lowest = v.currentGain; slot = i; }
                    }
                }
            }

            if (slot >= 0)
            {
                auto& v = voices[static_cast<size_t> (slot)];
                if (v.midiNote >= 0 && v.midiNote != note)
                    setNoteHeld (v.midiNote, false);   // stolen voice

                v.midiNote    = note;
                v.targetGain  = 1.0f;
                v.currentGain = 0.0f;
                v.needsInit   = true;
                v.shifter.reset();
                std::fill (v.delayLine.begin(), v.delayLine.end(), 0.0f);
                v.writePos = 0;
            }
        }
        else if (msg.isNoteOff())
        {
            const int note = msg.getNoteNumber();
            setNoteHeld (note, false);
            for (auto& v : voices)
                if (v.midiNote == note)
                    v.targetGain = 0.0f;
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            for (auto& v : voices) v.targetGain = 0.0f;
            for (auto& w : heldNotes) w.store (0);
        }
    }
}

// ============================================================
// Audio
// ============================================================
void VoiceMultiplierProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn      = juce::jmin (2, getTotalNumInputChannels());
    const int numOut     = juce::jmin (2, getTotalNumOutputChannels());

    if (numSamples <= 0 || numIn < 1 || numOut < 1 || buffer.getNumChannels() < 1)
    {
        buffer.clear();
        return;
    }

    // Only reallocates if the host exceeds the block size it announced.
    if (numSamples > scratchCapacity)
        allocateScratch (numSamples);

    const int   maxVoices   = juce::jlimit (1, kMaxVoices, juce::roundToInt (voicesParam->load()));
    const float detuneCents = detuneParam->load();
    const float maxDelayMs  = delayParam->load();
    const float width       = widthParam->load();
    const float dryWet      = dryWetParam->load();

    float* ch0 = buffer.getWritePointer (0);
    float* ch1 = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    const bool stereoIn = numIn > 1 && ch1 != nullptr;
    const bool stereoOut = numOut > 1 && ch1 != nullptr;

    // --- Mono sum of the input ---
    float* mono = monoScratch.data();
    for (int i = 0; i < numSamples; ++i)
        mono[i] = stereoIn ? 0.5f * (ch0[i] + ch1[i]) : ch0[i];

    // --- Pitch detection ---
    pitchDetector.pushSamples (mono, numSamples);
    pitchDetectCounter += numSamples;
    if (pitchDetectCounter >= pitchDetectInterval)
    {
        pitchDetectCounter = 0;
        updatePitchEstimate();
    }

    // --- MIDI ---
    handleMidi (midiMessages, maxVoices);

    int activeCount = 0;
    for (const auto& v : voices)
        if (v.midiNote >= 0) ++activeCount;
    activeVoiceCount.store (activeCount);

    // --- Per-sample global ramps (voice-count normalisation, wet mix) ---
    const float sr          = static_cast<float> (currentSampleRate);
    const float smoothCoeff = 1.0f - std::exp (-1.0f / (sr * 0.01f));
    const float normTarget  = 1.0f / std::sqrt (static_cast<float> (juce::jmax (1, activeCount)));
    const float mixTarget   = activeCount > 0 ? dryWet : 0.0f;   // no notes held = clean bypass

    for (int i = 0; i < numSamples; ++i)
    {
        smoothedNorm   += (normTarget - smoothedNorm)   * smoothCoeff;
        smoothedWetMix += (mixTarget  - smoothedWetMix) * smoothCoeff;
        normRamp[static_cast<size_t> (i)] = smoothedNorm;
        mixRamp[static_cast<size_t> (i)]  = smoothedWetMix;
    }

    // --- Voices ---
    float* wl = wetL.data();
    float* wr = wetR.data();
    std::fill_n (wl, numSamples, 0.0f);
    std::fill_n (wr, numSamples, 0.0f);

    const float n          = static_cast<float> (numSamples);
    const float ratioCoeff = 1.0f - std::exp (-n / (sr * 0.03f));
    const float panCoeff   = 1.0f - std::exp (-n / (sr * 0.05f));
    const float maxDelaySamples = maxDelayMs * 0.001f * sr;
    const float* ramp = normRamp.data();
    float* pitched = pitchedScratch.data();

    int processed = 0;
    for (auto& voice : voices)
    {
        if (voice.midiNote < 0)
            continue;

        // Position of this voice inside the chord: -1 .. +1 (used for detune, pan, delay)
        const float position = activeCount > 1
            ? static_cast<float> (processed) / static_cast<float> (activeCount - 1)
            : 0.5f;
        const float spread = position * 2.0f - 1.0f;

        // --- Pitch shift ---
        // Unvoiced input (consonants, silence) passes through unshifted.
        float targetRatio = 1.0f;
        if (smoothedPitchHz >= kMinPitchHz)
        {
            const float targetHz = 440.0f * std::pow (2.0f, static_cast<float> (voice.midiNote - 69) / 12.0f);
            targetRatio = (targetHz / smoothedPitchHz) * std::pow (2.0f, detuneCents * spread / 1200.0f);
        }
        targetRatio = juce::jlimit (0.25f, 4.0f, targetRatio);

        if (voice.needsInit) voice.ratio = targetRatio;
        else                 voice.ratio += (targetRatio - voice.ratio) * ratioCoeff;

        voice.shifter.setShiftRatio (voice.ratio);
        voice.shifter.process (mono, pitched, numSamples);

        // --- Pan / delay targets (smoothed so voices never jump when chords change) ---
        const float pan = activeCount > 1 ? spread * width : 0.0f;
        const float targetPanL = std::sqrt (0.5f * (1.0f - pan));
        const float targetPanR = std::sqrt (0.5f * (1.0f + pan));
        const float targetDelay = juce::jlimit (0.0f,
                                                static_cast<float> (voice.delayLine.size()) - 4.0f,
                                                maxDelaySamples * position * (activeCount > 1 ? 1.0f : 0.0f));

        const float startL = voice.needsInit ? targetPanL  : voice.panL;
        const float startR = voice.needsInit ? targetPanR  : voice.panR;
        const float startD = voice.needsInit ? targetDelay : voice.delaySamples;
        voice.needsInit = false;

        const float endL = startL + (targetPanL  - startL) * panCoeff;
        const float endR = startR + (targetPanR  - startR) * panCoeff;
        const float endD = startD + (targetDelay - startD) * panCoeff;
        const float stepL = (endL - startL) / n;
        const float stepR = (endR - startR) / n;
        const float stepD = (endD - startD) / n;
        voice.panL = endL;
        voice.panR = endR;
        voice.delaySamples = endD;

        // --- Delay line (fractional, interpolated), gain envelope, mix ---
        auto& dl = voice.delayLine;
        const int dlSize = static_cast<int> (dl.size());
        int wpos = voice.writePos;
        float gL = startL, gR = startR, dly = startD;

        for (int i = 0; i < numSamples; ++i)
        {
            gL += stepL;  gR += stepR;  dly += stepD;

            dl[static_cast<size_t> (wpos)] = pitched[i];

            float rp = static_cast<float> (wpos) - dly;
            if (rp < 0.0f) rp += static_cast<float> (dlSize);
            int i0 = static_cast<int> (rp);
            if (i0 >= dlSize) i0 -= dlSize;
            const float frac = rp - static_cast<float> (i0);
            int i1 = i0 + 1;
            if (i1 >= dlSize) i1 = 0;

            const float a = dl[static_cast<size_t> (i0)];
            const float b = dl[static_cast<size_t> (i1)];
            const float delayed = a + (b - a) * frac;

            wpos = (wpos + 1 == dlSize) ? 0 : wpos + 1;

            voice.currentGain += (voice.targetGain - voice.currentGain) * smoothCoeff;
            const float g = voice.currentGain * ramp[i];

            wl[i] += delayed * gL * g;
            wr[i] += delayed * gR * g;
        }
        voice.writePos = wpos;

        if (voice.targetGain < 0.001f && voice.currentGain < 0.001f)   // release finished
        {
            voice.currentGain = 0.0f;
            voice.midiNote    = -1;
        }
        ++processed;
    }

    // --- Final mix: time-aligned dry + wet voices ---
    const float* mix = mixRamp.data();
    constexpr float kMonoSum = 0.70710678f;

    for (int i = 0; i < numSamples; ++i)
    {
        const float in0 = ch0[i];
        const float in1 = stereoIn ? ch1[i] : in0;

        const float d0 = dryDelay[0].process (in0);
        const float d1 = dryDelay[1].process (in1);
        const float m  = mix[i];

        if (stereoOut)
        {
            ch0[i] = d0 * (1.0f - m) + wl[i] * m;
            ch1[i] = d1 * (1.0f - m) + wr[i] * m;
        }
        else
        {
            const float dry = stereoIn ? 0.5f * (d0 + d1) : d0;
            ch0[i] = dry * (1.0f - m) + (wl[i] + wr[i]) * kMonoSum * m;
        }
    }
}

// ============================================================
// Editor / state
// ============================================================
juce::AudioProcessorEditor* VoiceMultiplierProcessor::createEditor()
{
    return new VoiceMultiplierEditor (*this);
}

void VoiceMultiplierProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void VoiceMultiplierProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}

// ============================================================
// Factory function - REQUIRED by JUCE to instantiate the plugin.
// Without it, linking fails with "undefined reference to
// 'createPluginFilter()'".
// ============================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VoiceMultiplierProcessor();
}
