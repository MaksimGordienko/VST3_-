# VoiceMultiplier

VST3 MIDI-controlled vocal harmonizer.

## How it works
- Input: audio (microphone) + MIDI notes.
- YIN pitch detector determines the pitch of the incoming voice.
- Each MIDI note creates a copy of the voice, pitch-shifted to the target note.
- Up to 32 simultaneous voices (voice stealing by quietest voice).
- ADT effect: per-voice delay, detune, and pan.

## Parameters
- Max Polyphony (1-32) - maximum simultaneous voices.
- Detune (cents) - detune of each copy.
- Voice Delay (ms) - delay between copies (ADT).
- Stereo Width - stereo spread.
- Dry/Wet - dry vs processed mix.

## Build
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

JUCE is pulled automatically via FetchContent (internet needed on first build).

## CI
GitHub Actions build - see .github/workflows/build.yml.