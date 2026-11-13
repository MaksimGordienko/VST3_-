# VoiceMultiplier

VST3 MIDI-controlled vocal harmonizer.

## How it works
- Input: audio (microphone) + MIDI notes.
- YIN pitch detector determines the pitch of the incoming voice.
- Each MIDI note creates a copy of the voice, pitch-shifted to the target note.
- Up to 32 simultaneous voices (voice stealing by quietest voice).
- ADT effect: per-voice delay, detune, and pan.

## Changes in 1.1
- Pitch shifter rewritten (two-tap delay-line with Hann crossfade): works for both
  pitch-up and pitch-down, no clicks, constant latency independent of host block size.
- Latency is reported to the host (PDC) and the dry signal is delayed to stay in phase.
- No heap allocations on the audio thread; smoothed pitch/pan/delay (no zipper noise).
- Detune is spread symmetrically across voices; unvoiced input passes through unshifted.
- Mono input/output layouts supported; correct sample-rate handling up to 192 kHz.
- Plugin parameters use versioned `ParameterID`s and show units in the host.
- New resizable UI: pitch/tuning display, voice counter, live keyboard, rotary knobs.
- DSP unit tests (`-DVM_BUILD_TESTS=ON`).

## Parameters
- Max Polyphony (1-32) - maximum simultaneous voices.
- Detune (cents) - detune of each copy.
- Voice Delay (ms) - delay between copies (ADT).
- Stereo Width - stereo spread.
- Dry/Wet - dry vs processed mix.

## Build
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

Run the DSP tests: `cmake -B build -DVM_BUILD_TESTS=ON && cmake --build build && ctest --test-dir build`

JUCE is pulled automatically via FetchContent (internet needed on first build).

## Notes
The built-in pitch shifter is a simple two-head crossfade delay-line.
It works in real time with a fixed latency of 20 ms (reported to the host) and
some artifacts on rapid pitch changes. For studio-grade quality,
consider integrating RubberBand or a PSOLA-based shifter later.

## CI
GitHub Actions build - see .github/workflows/build.yml.