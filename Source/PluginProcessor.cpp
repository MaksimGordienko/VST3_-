#include "PluginProcessor.h"
#include "PluginEditor.h"

void YinPitchDetector::prepare (double sr, int minSize)
{
    sampleRate = sr;
    bufferSize = juce::jmax (2048, juce::nextPowerOfTwo (minSize));
    buffer.assign  (bufferSize, 0.0f);
    ordered.resize (bufferSize);
    minPeriod = juce::jmax (2, (int) std::floor (sr / 1000.0));
    maxPeriod = juce::jmin (bufferSize / 2 - 1, (int) std::ceil (sr / 60.0));
    diff.resize     (maxPeriod + 2);
    diffNorm.resize (maxPeriod + 2);
    writePos = 0;
}

void YinPitchDetector::pushSamples (const float* data, int n)
{
    for (int i = 0; i < n; ++i)
    {
        buffer[writePos] = data[i];
        writePos = (writePos + 1) % bufferSize;
    }
}

void YinPitchDetector::reset()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

float YinPitchDetector::detect()
{
    const int N = bufferSize;
    const int W = N / 2;
    for (int i = 0; i < N; ++i)
        ordered[i] = buffer[(writePos + i) % N];

    for (int tau = 1; tau <= maxPeriod; ++tau)
    {
        float sum = 0.0f;
        for (int j = 0; j < W; ++j)
        {
            const float d = ordered[j] - ordered[j + tau];
            sum += d * d;
        }
        diff[tau] = sum;
    }

    diffNorm[0] = 1.0f;
    float running = 0.0f;
    for (int tau = 1; tau <= maxPeriod; ++tau)
    {
        running += diff[tau];
        diffNorm[tau] = running > 1e-10f ? (diff[tau] * tau / running) : 1.0f;
    }

    int tauEst = -1;
    for (int tau = minPeriod; tau <= maxPeriod; ++tau)
    {
        if (diffNorm[tau] < threshold)
        {
            while (tau + 1 <= maxPeriod && diffNorm[tau + 1] < diffNorm[tau]) ++tau;
            tauEst = tau;
            break;
        }
    }
    if (tauEst < 0) return 0.0f;

    float betterTau = (float) tauEst;
    if (tauEst > 0 && tauEst < maxPeriod)
    {
        const float s0 = diffNorm[tauEst - 1];
        const float s1 = diffNorm[tauEst];
        const float s2 = diffNorm[tauEst + 1];
        const float denom = 2.0f * (2.0f * s1 - s2 - s0);
        if (std::abs (denom) > 1e-10f)
            betterTau = (float) tauEst + (s2 - s0) / denom;
    }
    return (float) (sampleRate / betterTau);
}

VoiceMultiplierProcessor::VoiceMultiplierProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameters())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout
VoiceMultiplierProcessor::createParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterInt>(
        "voices", "Max Polyphony", 1, kMaxVoices, 4));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        "detune", "Detune (cents)", 0.0f, 30.0f, 5.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        "delay", "Voice Delay (ms)", 0.0f, 30.0f, 8.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        "width", "Stereo Width", 0.0f, 1.0f, 0.8f));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        "dryWet", "Dry/Wet", 0.0f, 1.0f, 0.8f));

    return layout;
}

void VoiceMultiplierProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    const int delayLineSize = (int) (0.05 * sampleRate);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels      = 1;

    for (auto& v : voices)
    {
        v.shifter = std::make_unique<juce::dsp::PitchShifter<float>>();
        v.shifter->prepare (spec);
        v.shifter->reset();
        v.delayLine.assign (delayLineSize, 0.0f);
        v.writePos      = 0;
        v.midiNote      = -1;
        v.targetGain    = 0.0f;
        v.currentGain   = 0.0f;
        v.delaySamples  = 0;
    }

    pitchDetector.prepare (sampleRate, 2048);
    pitchDetector.reset();
    pitchHistory.fill (0.0f);
    pitchHistCount = 0;
    pitchHistIdx   = 0;
    smoothedPitchHz = 0.0f;
    pitchDetectCounter = 0;
}

void VoiceMultiplierProcessor::releaseResources() {}

bool VoiceMultiplierProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo();
}

void VoiceMultiplierProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numChannels < 1) return;

    const int   maxVoices   = juce::jlimit (1, kMaxVoices,
                                  (int) *apvts.getRawParameterValue ("voices"));
    const float detuneCents = apvts.getRawParameterValue ("detune")->load();
    const float maxDelayMs  = apvts.getRawParameterValue ("delay")->load();
    const float width       = apvts.getRawParameterValue ("width")->load();
    const float dryWet      = apvts.getRawParameterValue ("dryWet")->load();

    juce::AudioBuffer<float> monoIn (1, numSamples);
    {
        auto* m = monoIn.getWritePointer (0);
        const auto* L = buffer.getReadPointer (0);
        const auto* R = buffer.getReadPointer (numChannels > 1 ? 1 : 0);
        if (numChannels > 1)
            for (int i = 0; i < numSamples; ++i) m[i] = 0.5f * (L[i] + R[i]);
        else
            for (int i = 0; i < numSamples; ++i) m[i] = L[i];
    }

    pitchDetector.pushSamples (monoIn.getReadPointer (0), numSamples);
    pitchDetectCounter += numSamples;
    if (pitchDetectCounter >= kPitchDetectInterval)
    {
        pitchDetectCounter = 0;
        const float detected = pitchDetector.detect();
        detectedPitchHz.store (detected > 0 ? detected : 0.0f);

        if (detected > 20.0f && detected < 2000.0f)
        {
            pitchHistory[pitchHistIdx] = detected;
            pitchHistIdx = (pitchHistIdx + 1) % (int) pitchHistory.size();
            if (pitchHistCount < (int) pitchHistory.size()) ++pitchHistCount;

            std::array<float, 5> temp;
            std::copy (pitchHistory.begin(), pitchHistory.end(), temp.begin());
            std::sort (temp.begin(), temp.begin() + pitchHistCount);
            smoothedPitchHz = temp[pitchHistCount / 2];
        }
        else
        {
            smoothedPitchHz *= 0.9f;
        }
    }

    for (const auto meta : midiMessages)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
        {
            const int note = msg.getNoteNumber();

            int existing = -1;
            for (int i = 0; i < kMaxVoices; ++i)
                if (voices[i].midiNote == note) { existing = i; break; }

            if (existing >= 0)
            {
                voices[existing].targetGain = 1.0f;
                continue;
            }

            int activeCount = 0;
            for (int i = 0; i < kMaxVoices; ++i)
                if (voices[i].midiNote >= 0) ++activeCount;

            int slot = -1;
            if (activeCount < maxVoices)
                for (int i = 0; i < kMaxVoices; ++i)
                    if (voices[i].midiNote < 0) { slot = i; break; }

            if (slot < 0)
            {
                float lowest = 2.0f;
                for (int i = 0; i < kMaxVoices; ++i)
                    if (voices[i].targetGain < 0.001f && voices[i].currentGain < lowest)
                        { lowest = voices[i].currentGain; slot = i; }
                if (slot < 0)
                {
                    lowest = 2.0f;
                    for (int i = 0; i < kMaxVoices; ++i)
                        if (voices[i].currentGain < lowest)
                            { lowest = voices[i].currentGain; slot = i; }
                }
            }

            if (slot >= 0)
            {
                voices[slot].midiNote    = note;
                voices[slot].targetGain  = 1.0f;
                voices[slot].currentGain = 0.0f;
                voices[slot].shifter->reset();
                std::fill (voices[slot].delayLine.begin(),
                           voices[slot].delayLine.end(), 0.0f);
                voices[slot].writePos = 0;
            }
        }
        else if (msg.isNoteOff())
        {
            const int note = msg.getNoteNumber();
            for (int i = 0; i < kMaxVoices; ++i)
                if (voices[i].midiNote == note) voices[i].targetGain = 0.0f;
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            for (auto& v : voices) v.targetGain = 0.0f;
        }
    }

    int activeCount = 0;
    for (int i = 0; i < kMaxVoices; ++i)
        if (voices[i].midiNote >= 0 || voices[i].currentGain > 0.001f) ++activeCount;

    juce::AudioBuffer<float> dry;
    dry.makeCopyOf (buffer);
    buffer.clear();

    if (activeCount == 0 || smoothedPitchHz < 20.0f)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.copyFrom (ch, 0, dry, ch, 0, numSamples);
        return;
    }

    const float smoothCoeff = 1.0f - std::exp (-1.0f / (float) (currentSampleRate * 0.01));
    const float voiceGain   = 1.0f / std::sqrt ((float) activeCount);

    int processedCount = 0;
    for (int v = 0; v < kMaxVoices; ++v)
    {
        auto& voice = voices[v];
        if (voice.midiNote < 0 && voice.currentGain < 0.001f) continue;

        const float targetHz = 440.0f * std::pow (2.0f, (voice.midiNote - 69) / 12.0f);
        const float detuneRatio = std::pow (2.0f, detuneCents / 1200.0f);
        const float ratio = juce::jlimit (0.25f, 4.0f,
                                          (targetHz / smoothedPitchHz) * detuneRatio);
        voice.shifter->setShiftRatio (ratio);

        juce::AudioBuffer<float> pitched (1, numSamples);
        pitched.makeCopyOf (monoIn);
        {
            juce::dsp::AudioBlock<float> block (pitched);
            juce::dsp::ProcessContextReplacing<float> ctx (block);
            voice.shifter->process (ctx);
        }

        const int targetDelay = (activeCount > 1)
            ? (int) (maxDelayMs * 0.001 * currentSampleRate
                     * (float) processedCount / (float) (activeCount - 1))
            : 0;
        voice.delaySamples = targetDelay;

        const float pan = (activeCount <= 1) ? 0.0f
            : juce::jmap ((float) processedCount, 0.0f,
                          (float) (activeCount - 1), -width, width);
        const float panL = std::sqrt (0.5f * (1.0f - pan));
        const float panR = std::sqrt (0.5f * (1.0f + pan));

        auto* outL = buffer.getWritePointer (0);
        auto* outR = buffer.getWritePointer (numChannels > 1 ? 1 : 0);
        const auto* src = pitched.getReadPointer (0);
        auto& dl = voice.delayLine;
        const int dlSize = (int) dl.size();
        int& wpos = voice.writePos;

        for (int i = 0; i < numSamples; ++i)
        {
            dl[wpos] = src[i];
            int rp = wpos - voice.delaySamples;
            if (rp < 0) rp += dlSize;
            const float delayed = dl[rp];
            wpos = (wpos + 1) % dlSize;

            voice.currentGain += (voice.targetGain - voice.currentGain) * smoothCoeff;
            const float g = voice.currentGain * voiceGain;

            outL[i] += delayed * panL * g;
            if (numChannels > 1) outR[i] += delayed * panR * g;
        }

        if (voice.targetGain < 0.001f && voice.currentGain < 0.001f)
        {
            voice.currentGain = 0.0f;
            voice.midiNote    = -1;
        }
        ++processedCount;
    }

    const float dryAmount = 1.0f - dryWet;
    if (dryAmount > 0.001f)
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.addFrom (ch, 0, dry, ch, 0, numSamples, dryAmount);
}

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