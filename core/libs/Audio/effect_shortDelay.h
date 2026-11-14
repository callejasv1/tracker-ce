#ifndef LIBS_AUDIO_EFFECT_SHORTDELAY_H_
#define LIBS_AUDIO_EFFECT_SHORTDELAY_H_
#include "AudioStream.h"

constexpr uint8_t MAX_SHORT_DELAY_VOICES = 12;
constexpr uint16_t MAX_SHORT_DELAY_TIME = 3500;
constexpr uint32_t MAX_DELAYLINE_LENGTH = 262144;

class AudioEffectShortDelay : public AudioStream
{
  public:
    AudioEffectShortDelay(void)
      : AudioStream(1, inputQueueArray)
    {
        blockUpdate();
    }

    void begin(float feedback,
               uint32_t time,
               bool pingPongEnabled,
               bool syncEnabled,
               uint8_t syncRatioIdx,
               float frequency);
    void setBuffer(int16_t* buffer);
    void setFeedback(float f);
    void setTime(float t);
    int getTime();
    void setRate(uint8_t r);
    void setPingpongEnable(bool pp);
    void setSyncEnable(bool se);
    void setFreeze(bool freeze);
    void setFilterFrequency(float frequency);
    virtual void update(void);
    bool process(int16_t input[], int16_t outputLeft[], int16_t outputRight[], int sampleCount);
    void clear();
    void blockUpdate();
    void unblockUpdate();
    bool isSilent() const
    {
        return !isFrozen && silentSamplesCount >= targetTimeInSamples;
    }

  private:
    struct FilterCoeffs
    {
        int32_t a0;
        int32_t a1;
        int32_t b1;
    };

    static constexpr int kFilterCoeffsShift = 10;
    static constexpr int kFeedbackShift = 8;
    static constexpr int kDelayTimeFracShift = 8;
    static constexpr uint16_t kMaxFilterCoeff = (1 << kFilterCoeffsShift) - 1;
    static constexpr float kMaxFeedback = 0.99f;
    static constexpr uint32_t delaylineLengthMask = MAX_DELAYLINE_LENGTH - 1;
    int16_t* pingpongDelaylineRight;
    int16_t* pingpongDelaylineLeft;
    int16_t* monoDelayline;
    uint32_t headIdx = 0;
    audio_block_t* inputQueueArray[1];
    uint32_t timeInSamples = 1;
    uint32_t targetTimeInSamples = 1;
    int32_t currentTimeQ8 = 1 << kDelayTimeFracShift;
    uint8_t feedback = 0;
    bool noRefresh;
    bool isFrozen = false;
    bool hasTimeInit = false;
    uint8_t
      isPingpong; // uint8_t allows to use conditionally execution instead conditionally branch
    bool isSync;
    FilterCoeffs filterCoeffs;
    int32_t lastFilterInRight;
    int32_t lastFilterInLeft;
    int32_t lastFilterInMono;
    int32_t lastFilterOutRight;
    int32_t lastFilterOutLeft;
    int32_t lastFilterOutMono;
    uint32_t silentSamplesCount = MAX_DELAYLINE_LENGTH;
};

#endif /* LIBS_AUDIO_EFFECT_SHORTDELAY_H_ */
