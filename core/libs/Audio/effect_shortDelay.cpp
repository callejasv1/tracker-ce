#include "effect_shortDelay.h"
#include "Audio.h"
#include "mtSequencer.h"
#include "utility/dspinst.h"

static int16_t zeroblock[AUDIO_BLOCK_SAMPLES] = {};

const float tempoSyncRatesShortDelay[19] = {
    8,        6,    4,      3,        2,     1.5,      1,      0.75,     0.5,     0.375,
    0.333333, 0.25, 0.1875, 0.166667, 0.125, 0.083333, 0.0625, 0.041667, 0.03125,
};
extern int16_t externalRamDelayBuff[];

AUDIO_CORE_FLASH void AudioEffectShortDelay::begin(float feedback,
                                             uint32_t timeMs,
                                             bool pingPongEnabled,
                                             bool syncEnabled,
                                             uint8_t syncRateIdx,
                                             float frequency)
{
    isFrozen = false;
    hasTimeInit = false;
    setPingpongEnable(pingPongEnabled);
    setSyncEnable(syncEnabled);
    setTime(timeMs);
    setFeedback(feedback);
    setRate(syncRateIdx);
    setFilterFrequency(frequency);

    setBuffer(externalRamDelayBuff);
    unblockUpdate();
}

AUDIO_CORE_FLASH void AudioEffectShortDelay::setBuffer(int16_t* buffer)
{
    pingpongDelaylineRight = &buffer[0];
    pingpongDelaylineLeft = &buffer[MAX_DELAYLINE_LENGTH];
    monoDelayline = &buffer[2 * MAX_DELAYLINE_LENGTH];

    clear();
}

AUDIO_CORE_FLASH void AudioEffectShortDelay::setFeedback(float feedback)
{
    if (feedback > kMaxFeedback)
        feedback = kMaxFeedback;
    else if (feedback < 0.0f)
        feedback = 0.0f;

    //	__disable_irq();
    AudioNoInterrupts();
    this->feedback = UINT8_MAX * feedback;
    AudioInterrupts();
    //	__enable_irq();
}
AUDIO_CORE_FLASH void AudioEffectShortDelay::setTime(float time)
{
    if (time > MAX_SHORT_DELAY_TIME)
        time = MAX_SHORT_DELAY_TIME;
    uint32_t samples = (uint32_t)(time * 44.1f);
    if (samples >= MAX_DELAYLINE_LENGTH)
        samples = MAX_DELAYLINE_LENGTH - 1;
    if (samples < 1)
        samples = 1;
    AudioNoInterrupts();
    targetTimeInSamples = samples;
    timeInSamples = samples;
    if (!hasTimeInit)
    {
        currentTimeQ8 = (int32_t)(samples << kDelayTimeFracShift);
        hasTimeInit = true;
    }
    AudioInterrupts();
}

AUDIO_CORE_FLASH int AudioEffectShortDelay::getTime()
{
    return int(targetTimeInSamples / 44.1f);
}

AUDIO_CORE_FLASH void AudioEffectShortDelay::setFreeze(bool freeze)
{
    AudioNoInterrupts();
    isFrozen = freeze;
    AudioInterrupts();
}

AUDIO_CORE_FLASH void AudioEffectShortDelay::setRate(uint8_t syncRateIdx)
{
    if (!isSync)
        return;

    float tempo = sequencer.getCurrentTempo();

    float freq = (tempo / 15.0f); // *4,bo  beat = 4 step i /60, bo min = 60s
    float timeMs = (1000 / freq) * tempoSyncRatesShortDelay[syncRateIdx];

    setTime(timeMs);
}
AUDIO_CORE_FLASH void AudioEffectShortDelay::setPingpongEnable(bool pingPongEnabled)
{
    isPingpong = pingPongEnabled;
}
AUDIO_CORE_FLASH void AudioEffectShortDelay::setSyncEnable(bool syncEnabled)
{
    isSync = syncEnabled;
}

AUDIO_CORE_FLASH void AudioEffectShortDelay::setFilterFrequency(float frequency)
{
    float Qc = 2 * M_PI * frequency / 44100.0f;
    float y = (cosf(Qc) / (1 + sinf(Qc)));

    filterCoeffs.a0 = kMaxFilterCoeff * ((1.0f - y) / 2.0f);
    filterCoeffs.a1 = filterCoeffs.a0;
    filterCoeffs.b1 = kMaxFilterCoeff * (-y);

    lastFilterInLeft = 0;
    lastFilterInRight = 0;
    lastFilterInMono = 0;
    lastFilterOutLeft = 0;
    lastFilterOutRight = 0;
    lastFilterOutMono = 0;
}

AUDIO_CORE_FLASH void AudioEffectShortDelay::clear()
{
    blockUpdate();
    //    AudioNoInterrupts();
    headIdx = 0;
    currentTimeQ8 = (int32_t)(targetTimeInSamples << kDelayTimeFracShift);
    lastFilterInLeft = 0;
    lastFilterInRight = 0;
    lastFilterInMono = 0;
    lastFilterOutLeft = 0;
    lastFilterOutRight = 0;
    lastFilterOutMono = 0;
    silentSamplesCount = MAX_DELAYLINE_LENGTH;

    memset(pingpongDelaylineRight, 0, MAX_DELAYLINE_LENGTH * sizeof(int16_t));
    memset(pingpongDelaylineLeft, 0, MAX_DELAYLINE_LENGTH * sizeof(int16_t));
    memset(monoDelayline, 0, MAX_DELAYLINE_LENGTH * sizeof(int16_t));
    //    AudioInterrupts();
    unblockUpdate();
}

AUDIO_CORE_FLASH void AudioEffectShortDelay::blockUpdate()
{
    noRefresh = true;
}
AUDIO_CORE_FLASH void AudioEffectShortDelay::unblockUpdate()
{
    noRefresh = false;
}

AUDIO_CORE_RAM void AudioEffectShortDelay::update(void)
{
    audio_block_t* input;
    audio_block_t* outputLeft;
    audio_block_t* outputRight;

    input = receiveReadOnly(0);
    outputLeft = allocate();
    outputRight = allocate();

    bool processed = false;

    if (outputLeft != nullptr && outputRight != nullptr)
    {
        processed = process(input != nullptr ? input->data : zeroblock,
                            outputLeft->data,
                            outputRight->data,
                            AUDIO_BLOCK_SAMPLES);
    }

    if (outputRight != nullptr)
    {
        if (processed)
        {
            transmit(outputRight, 0);
        }
        release(outputRight);
    }
    if (outputLeft != nullptr)
    {
        if (processed)
        {
            transmit(outputLeft, 1);
        }
        release(outputLeft);
    }
    if (input != nullptr)
    {
        release(input);
    }
}

AUDIO_CORE_RAM bool AudioEffectShortDelay::process(int16_t input[],
                                               int16_t outputLeft[],
                                               int16_t outputRight[],
                                               int sampleCount)
{
    if (noRefresh == true)
    {
        return false;
    }

    int32_t newDelayedSampleLeft = 0;
    int32_t newDelayedSampleRight = 0;
    int32_t newDelayedSampleMono = 0;

    const int32_t targetQ8 = (int32_t)(targetTimeInSamples << kDelayTimeFracShift);
    int32_t curTimeQ8 = currentTimeQ8;
    const bool frozen = isFrozen;
    const uint32_t curFeedback = feedback;

    auto divCoeffShift = [](int32_t num) -> int32_t
    {
        static constexpr int32_t kShiftMask = (1 << kFilterCoeffsShift) - 1;
        return (num + (num < 0 ? kShiftMask : 0)) >> kFilterCoeffsShift;
    };

    for (int sample = 0; sample < sampleCount; sample++)
    {
        uint32_t wholeDelay;
        uint32_t fracQ8 = 0;
        if (curTimeQ8 != targetQ8)
        {
            int32_t diff = targetQ8 - curTimeQ8;
            int32_t step = diff >> 10;
            if (step == 0)
            {
                step = (diff > 0) ? 1 : -1;
            }
            curTimeQ8 += step;
            if (curTimeQ8 < (1 << kDelayTimeFracShift))
            {
                curTimeQ8 = (1 << kDelayTimeFracShift);
            }
            wholeDelay = (uint32_t)(curTimeQ8 >> kDelayTimeFracShift);
            fracQ8 = (uint32_t)(curTimeQ8 & ((1 << kDelayTimeFracShift) - 1));
        }
        else
        {
            wholeDelay = targetTimeInSamples;
        }

        uint32_t delayedSampleIdx = (headIdx - wholeDelay) & delaylineLengthMask;
        int32_t delayedSampleRight;
        int32_t delayedSampleLeft;
        int32_t delayedSampleMono;
        if (fracQ8 == 0)
        {
            delayedSampleRight = pingpongDelaylineRight[delayedSampleIdx];
            delayedSampleLeft = pingpongDelaylineLeft[delayedSampleIdx];
            delayedSampleMono = monoDelayline[delayedSampleIdx];
        }
        else
        {
            uint32_t prevSampleIdx = (delayedSampleIdx - 1u) & delaylineLengthMask;
            int32_t fQ8 = (int32_t)fracQ8;
            int32_t r0 = pingpongDelaylineRight[delayedSampleIdx];
            int32_t l0 = pingpongDelaylineLeft[delayedSampleIdx];
            int32_t m0 = monoDelayline[delayedSampleIdx];
            delayedSampleRight =
              r0 + (((pingpongDelaylineRight[prevSampleIdx] - r0) * fQ8) >> kDelayTimeFracShift);
            delayedSampleLeft =
              l0 + (((pingpongDelaylineLeft[prevSampleIdx] - l0) * fQ8) >> kDelayTimeFracShift);
            delayedSampleMono =
              m0 + (((monoDelayline[prevSampleIdx] - m0) * fQ8) >> kDelayTimeFracShift);
        }

        int32_t filteredSampleRight = divCoeffShift(
          filterCoeffs.a0 * delayedSampleRight + filterCoeffs.a1 * lastFilterInRight -
          filterCoeffs.b1 * lastFilterOutRight);
        int32_t filteredSampleLeft = divCoeffShift(
          filterCoeffs.a0 * delayedSampleLeft + filterCoeffs.a1 * lastFilterInLeft -
          filterCoeffs.b1 * lastFilterOutLeft);
        int32_t filteredSampleMono = divCoeffShift(
          filterCoeffs.a0 * delayedSampleMono + filterCoeffs.a1 * lastFilterInMono -
          filterCoeffs.b1 * lastFilterOutMono);

        lastFilterInRight = delayedSampleRight;
        lastFilterInLeft = delayedSampleLeft;
        lastFilterInMono = delayedSampleMono;
        lastFilterOutRight = filteredSampleRight;
        lastFilterOutLeft = filteredSampleLeft;
        lastFilterOutMono = filteredSampleMono;

        int16_t outL = isPingpong ? filteredSampleLeft : filteredSampleMono;
        int16_t outR = isPingpong ? filteredSampleRight : filteredSampleMono;
        *outputLeft++ = outL;
        *outputRight++ = outR;

        int16_t inSample = *input++;
        int16_t saturatedLeft;
        int16_t saturatedRight;
        int16_t saturatedMono;
        if (frozen)
        {
            // Infinite Freeze: mute new input and recirculate the delay buffer at unity.
            saturatedLeft = (int16_t)delayedSampleRight;
            saturatedRight = (int16_t)delayedSampleLeft;
            saturatedMono = (int16_t)delayedSampleMono;
        }
        else
        {
            int32_t inputShifted = ((int32_t)inSample << kFeedbackShift);

            newDelayedSampleLeft = inputShifted + filteredSampleRight * (int32_t)curFeedback;
            newDelayedSampleRight = filteredSampleLeft * (int32_t)curFeedback;
            newDelayedSampleMono = inputShifted + filteredSampleMono * (int32_t)curFeedback;

            static constexpr int32_t kFeedbackRoundBias = (1 << kFeedbackShift) - 1;
            saturatedLeft = (int16_t)signed_saturate_rshift(
              newDelayedSampleLeft + (newDelayedSampleLeft < 0 ? kFeedbackRoundBias : 0),
              16,
              kFeedbackShift);
            saturatedRight = (int16_t)signed_saturate_rshift(
              newDelayedSampleRight + (newDelayedSampleRight < 0 ? kFeedbackRoundBias : 0),
              16,
              kFeedbackShift);
            saturatedMono = (int16_t)signed_saturate_rshift(
              newDelayedSampleMono + (newDelayedSampleMono < 0 ? kFeedbackRoundBias : 0),
              16,
              kFeedbackShift);
        }

        bool isSampleSilent =
          !frozen &&
          (isPingpong ? (outL == 0 && outR == 0 && saturatedLeft == 0 && saturatedRight == 0)
                      : (outL == 0 && saturatedMono == 0));
        if (isSampleSilent)
        {
            if (silentSamplesCount < MAX_DELAYLINE_LENGTH)
            {
                silentSamplesCount++;
            }
        }
        else
        {
            silentSamplesCount = 0;
        }

        uint32_t headSampleIdx = headIdx++ & delaylineLengthMask;

        pingpongDelaylineLeft[headSampleIdx] = saturatedLeft;
        pingpongDelaylineRight[headSampleIdx] = saturatedRight;
        monoDelayline[headSampleIdx] = saturatedMono;
    }

    currentTimeQ8 = curTimeQ8;
    return true;
}
