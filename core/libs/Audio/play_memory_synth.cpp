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
    synthMod2 = 0;
    synthPwMod = 0;
    lastSynthMode = 0xFF;

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

    const auto& instr = mtProject.instrument[currentInstrIdx];
    const auto& sp = instr.synthParams();

    // Pitch ratios for Osc B / Swarm (recompute only when ratio params change)
    if (sp.mode != lastSynthMode || sp.semiB != lastSynthSemiB || sp.detune != lastSynthDetune)
    {
        synthosc::computeRatios(sp, synthRatioB, synthRatioC);
        lastSynthMode = sp.mode;
        lastSynthSemiB = sp.semiB;
        lastSynthDetune = sp.detune;
    }

    const int16_t modVals[2] = { synthMod, synthMod2 };
    const uint8_t modDests[2] = {
        instr.getModDestination(0),
        instr.getModDestination(1),
    };

    // Pulse width modulation:
    // Slot 3 (envWtPos) sets synthPwMod (-100..+100), Sequencer FX Position overrides base PW
    int32_t pwMod = (int32_t)synthPwMod;
    if (wavetableWindowForceFlag)
    {
        pwMod += (int32_t)forcedWavetableWindow - (int32_t)sp.pw;
    }
    int32_t detuneDelta = 0;
    int32_t subDelta = 0;
    int32_t noiseDelta = 0;
    int32_t levelBDelta = 0;
    for (int m = 0; m < 2; m++)
    {
        if (modVals[m] == 0)
            continue;
        switch (modDests[m])
        {
            case modDestPulseWidth: pwMod += (int32_t)modVals[m]; break;
            case modDestDetune: detuneDelta += (int32_t)modVals[m]; break;
            case modDestSubLevel: subDelta += (int32_t)modVals[m]; break;
            case modDestNoiseLevel: noiseDelta += (int32_t)modVals[m]; break;
            case modDestOscBLevel: levelBDelta += (int32_t)modVals[m]; break;
            default: break;
        }
    }

    synthosc::buildBlockParams(sp, incA, synthRatioB, synthRatioC, pwMod, synthBlockParams);

    if (detuneDelta != 0)
    {
        float detRatio = synthosc::centsToRatio(detuneDelta);
        synthBlockParams.incB = (uint32_t)((float)synthBlockParams.incB * detRatio);
    }
    if (subDelta != 0)
    {
        int32_t subLevel = std::clamp<int32_t>((int32_t)sp.sub + subDelta, 0, 100);
        synthBlockParams.gainSub = synthosc::levelToGainQ12((uint8_t)subLevel);
    }
    if (noiseDelta != 0)
    {
        int32_t noiseLevel = std::clamp<int32_t>((int32_t)sp.noise + noiseDelta, 0, 100);
        synthBlockParams.gainNoise = synthosc::levelToGainQ12((uint8_t)noiseLevel) >> 1;
    }
    if (levelBDelta != 0)
    {
        int32_t levelB = std::clamp<int32_t>((int32_t)sp.levelB + levelBDelta, 0, 100);
        synthBlockParams.gainB = synthosc::levelToGainQ12((uint8_t)levelB);
    }

    synthOsc.render(sp.mode, sp.waveA, sp.waveB, synthBlockParams, block->data, AUDIO_BLOCK_SAMPLES);

    return block;
}