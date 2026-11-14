#include "Audio.h"
#include "play_memory.h"
#include "utility/dspinst.h"
#include <algorithm>

AUDIO_CORE_FLASH void AudioPlayMemory::playSynth(uint8_t instrIdx, int8_t note)
{
    AudioNoInterrupts();

    if (instrIdx > kSampleInstrumentsCount - 1)
        instrIdx = kSampleInstrumentsCount - 1;
    if (note > MAX_NOTE)
        note = MAX_NOTE;

    lastInstrIdxInPlay = currentInstrIdxInPlay;
    currentInstrIdxInPlay = instrIdx;
    currentInstrIdx = instrIdx;

    refreshStartParameters();

    constrainCurrentTune(note);

    setStartPitch(note);

    applyFinetuneOnPitch(note);

    calculateGlidePitch(note);

    lastNote = note;

    currentPlayMode = playModeSynth;

    if (!synthOscInitialized)
    {
        synthOsc.reset();
        synthOscInitialized = true;
    }
    synthMod = 0;
    synthPwMod = 0;

    playing = 1;

    AudioInterrupts();
}

AUDIO_CORE_RAM audio_block_t* AudioPlayMemory::updateSynth()
{
    audio_block_t* block = allocate();
    if (!block)
        return nullptr;

    if (!playing)
    {
        release(block);
        return nullptr;
    }

    // Handle pitch smoothing for glide / slide
    if (constrainsInSamples.pitchSmoothing)
    {
        if (pitchSmoothingControlCounter <= constrainsInSamples.pitchSmoothing)
        {
            pitchControl += pitchSmoothingControl;
            if (pitchControl > notes[MAX_NOTE - 1])
            {
                pitchControl = notes[MAX_NOTE - 1];
                pitchSmoothingControlCounter = constrainsInSamples.pitchSmoothing;
            }
            else if (pitchControl < notes[MIN_NOTE])
            {
                pitchControl = notes[MIN_NOTE];
                pitchSmoothingControlCounter = constrainsInSamples.pitchSmoothing;
            }
            pitchSmoothingControlCounter++;
        }
    }

    // Convert pitchControl (1.0 = Note 60 C5 = 523.25113 Hz) to 32-bit phase increment
    constexpr float kBaseIncMiddleNote = 50965320.0f;
    uint32_t incA = (uint32_t)(pitchControl * kBaseIncMiddleNote);

    const auto& sp = mtProject.instrument[currentInstrIdx].synthParams();

    // Pitch ratios for Osc B / Swarm
    synthosc::computeRatios(sp, synthRatioB, synthRatioC);

    // Pulse width modulation:
    // Slot 3 (envWtPos) sets synthPwMod (-100..+100)
    int32_t pwMod = (int32_t)synthPwMod;
    if (synthMod != 0 && mtProject.instrument[currentInstrIdx].modDestination == modDestPulseWidth)
    {
        pwMod += (int32_t)synthMod;
    }

    synthosc::buildBlockParams(sp, incA, synthRatioB, synthRatioC, pwMod, synthBlockParams);

    // Extra assignable mod target handling in synth mode
    if (synthMod != 0)
    {
        float modVal = (float)synthMod / 100.0f;
        if (mtProject.instrument[currentInstrIdx].modDestination == modDestDetune)
        {
            float detRatio = synthosc::centsToRatio((int32_t)(modVal * 100.0f));
            synthBlockParams.incB = (uint32_t)((float)synthBlockParams.incB * detRatio);
        }
        else if (mtProject.instrument[currentInstrIdx].modDestination == modDestSubLevel)
        {
            int32_t subLevel = std::clamp<int32_t>((int32_t)sp.sub + (int32_t)(modVal * 100.0f), 0, 100);
            synthBlockParams.gainSub = synthosc::levelToGainQ12((uint8_t)subLevel);
        }
        else if (mtProject.instrument[currentInstrIdx].modDestination == modDestNoiseLevel)
        {
            int32_t noiseLevel = std::clamp<int32_t>((int32_t)sp.noise + (int32_t)(modVal * 100.0f), 0, 100);
            synthBlockParams.gainNoise = synthosc::levelToGainQ12((uint8_t)noiseLevel) >> 1;
        }
    }

    synthOsc.render(sp.mode, sp.waveA, sp.waveB, synthBlockParams, block->data, AUDIO_BLOCK_SAMPLES);

    return block;
}