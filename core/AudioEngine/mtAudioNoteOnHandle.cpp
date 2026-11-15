#include "ampLogarythmicValues.h"
#include "mtAudioEngine.h"
#include "mtSequencer.h"
#include "polyassert.h"

using namespace mtAudioEngineConstans;

//********************************** NOTE_ON
uint8_t playerEngine ::noteOn(uint8_t instr_idx, int8_t note, int8_t _velocity)
{
    if (mtProject.instrument[instr_idx].isActive != 1)
        return 0;
    if (isBlockedNoteOn[instr_idx])
        return 0;
    //	engine.clearDelay();

    endFx(lastSeqFx[0], 0);
    endFx(lastSeqFx[1], 1);

    //	__disable_irq();
    AudioNoInterrupts();

    uint8_t status;

    currentInstrument_idx = instr_idx;
    currentNote = note;
    stepVelocity = _velocity;

    lastAppliedLp1 = mtProject.instrument[instr_idx].loopPoint1;
    lastAppliedTune = mtProject.instrument[instr_idx].tune;

    handleInitNoteOnAllEnvelopes(); // kill + init

    handleNoteOnFilter();

    handleNoteOnGain();

    handleNoteOnPanning();

    handleNoteOnDelaySend();

    handleNoteOnReverbSend();

    handleNoteOnBitCrusher();

    for (uint8_t i = 0; i < strInstrument::kDynamicPoolMax; i++)
    {
        if (mtProject.instrument[instr_idx].isPoolSlotAvailable(i) &&
            mtProject.instrument[instr_idx].isEnvEnabled(strInstrument::poolIdxToEnvSlot(i)))
        {
            refreshRegister |=
              getDestRefreshMask(mtProject.instrument[instr_idx].getModDestination(i));
        }
    }

    status = 1; // playMemPtr->play(instr_idx,note);
    if (isTrackDisplayed)
        onEndDisplay = true;

    bool isAmpRandom = (mtProject.instrument[instr_idx].envelope[envAmp].loop == 1) &&
                       (mtProject.instrument[instr_idx].lfo[envAmp].shape == lfoShapeRandom);
    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->envelopeSetIsRandom(isAmpRandom);
        playMemPtr[channel]->envelopeNoteOn(instr_idx, note);
    }

    for (uint8_t i = envPan; i < ACTIVE_ENVELOPES; i++)
    {
        if (mtProject.instrument[instr_idx].isEnvEnabled(i))
        {
            const auto& envCfg = mtProject.instrument[instr_idx].getEnv(i);
            const auto& lfoCfg = mtProject.instrument[instr_idx].getLfo(i);
            bool isRandom = (envCfg.loop == 1) && (lfoCfg.shape == lfoShapeRandom);
            envelopePtr[i]->setIsRandom(isRandom);
            envelopePtr[i]->setWavetableParams(lfoCfg.shape, lfoCfg.wtBend, lfoCfg.wtSust);
            envelopePtr[i]->start();
        }
    }

    //	__enable_irq();
    AudioInterrupts();
    return status;
}
//**********************************
//********************************** NOTE_ON FX
AUDIO_CORE_RAM uint8_t playerEngine ::noteOn(uint8_t instr_idx,
                                             int8_t note,
                                             int8_t velocity,
                                             uint8_t fx1_id,
                                             uint8_t fx1_val,
                                             uint8_t fx2_id,
                                             uint8_t fx2_val)
{
    if (mtProject.instrument[instr_idx].isActive != 1)
        return 0;
    if (isBlockedNoteOn[instr_idx])
        return 0;
    //	__disable_irq();
    AudioNoInterrupts();
    uint8_t status;

    currentInstrument_idx = instr_idx;
    currentNote = note;
    stepVelocity = velocity;

    lastAppliedLp1 = mtProject.instrument[instr_idx].loopPoint1;
    lastAppliedTune = mtProject.instrument[instr_idx].tune;

    handleInitFxNoteOnAllEnvelopes();

    handleFxNoteOnFilter();

    handleFxNoteOnGain();

    handleFxNoteOnPanning();

    handleFxNoteOnBitDepth();

    handleFxNoteOnDelaySend();

    handleFxNoteOnReverbSend();

    //********* obsluga performance parametrow obslugiwanych w play_memory
    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::endPoint])
    {
        changeEndPointPerformanceMode(
          std::roundf(mapClamped<float>((float)performanceMod.endPoint,
                                        (float)-MAX_16BIT,
                                        (float)MAX_16BIT,
                                        -100.0f,
                                        100.0f))); // zalatanie na szybko
    }
    if (trackControlParameter[(int)controlType::performanceMode][(
          int)parameterList::startPoint]) // tu nie ma mapowania bo posredniczy change position
    {
        changeStartPointPerformanceMode(performanceMod.startPoint);
    }
    if (trackControlParameter[(int)controlType::performanceMode]
                             [(int)parameterList::wavetablePosition])
    {
        changeWavetableWindowPerformanceMode(performanceMod.wavetablePosition);
    }
    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::tune])
    {
        changeTunePerformanceMode(performanceMod.tune);
    }
    //******** seqFx
    if (velocity >= 0)
    {
        if (isFxVelocity(fx1_id))
        {
            fx1_id = Sequencer::strFxConsts::FX_TYPE_VOLUME;
            fx1_val = velocity;
        }

        if (isFxVelocity(fx2_id))
        {
            fx2_id = Sequencer::strFxConsts::FX_TYPE_VOLUME;
            fx2_val = velocity;
        }
    }

    seqFx(fx2_id, fx2_val, 1); // mniej wazny najpierw
    seqFx(fx1_id, fx1_val, 0);

    for (uint8_t i = 0; i < strInstrument::kDynamicPoolMax; i++)
    {
        if (mtProject.instrument[instr_idx].isPoolSlotAvailable(i) &&
            mtProject.instrument[instr_idx].isEnvEnabled(strInstrument::poolIdxToEnvSlot(i)))
        {
            refreshRegister |=
              getDestRefreshMask(mtProject.instrument[instr_idx].getModDestination(i));
        }
    }

    bool isFx1Slide = (fx1_id == Sequencer::strFxConsts::FX_TYPE_SLIDE_UP) ||
                      (fx1_id == Sequencer::strFxConsts::FX_TYPE_SLIDE_DOWN);
    bool isFx2Slide = (fx2_id == Sequencer::strFxConsts::FX_TYPE_SLIDE_UP) ||
                      (fx2_id == Sequencer::strFxConsts::FX_TYPE_SLIDE_DOWN);

    if (isFx1Slide || isFx2Slide)
    {
        bool isFx1Glide = (fx1_id == Sequencer::strFxConsts::FX_TYPE_GLIDE);
        if (isFx1Slide)
        {
            for (int channel = 0; channel < playMemPtr.size(); channel++)
            {
                playMemPtr[channel]->setSlideOnPlay(
                  (fx1_id == Sequencer::strFxConsts::FX_TYPE_SLIDE_UP),
                  fx1_val,
                  sequencer.getStepPeriodMs());
            }
        }
        else if (!isFx1Glide && isFx2Slide)
        {
            for (int channel = 0; channel < playMemPtr.size(); channel++)
            {
                playMemPtr[channel]->setSlideOnPlay(
                  (fx2_id == Sequencer::strFxConsts::FX_TYPE_SLIDE_UP),
                  fx2_val,
                  sequencer.getStepPeriodMs());
            }
        }
    }

    bool isAmpRandom = (mtProject.instrument[instr_idx].envelope[envAmp].loop == 1) &&
                       (mtProject.instrument[instr_idx].lfo[envAmp].shape == lfoShapeRandom);

    for (int channel = 0; channel < playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->envelopeSetIsRandom(isAmpRandom);
        playMemPtr[channel]->envelopeNoteOn(instr_idx,
                                            note); // zawsze odpalamy nawet jak nie aktywny
    }

    for (uint8_t i = envPan; i < ACTIVE_ENVELOPES; i++)
    {
        if (isActiveEnvelope(i))
        {
            const auto& envCfg = mtProject.instrument[instr_idx].getEnv(i);
            const auto& lfoCfg = mtProject.instrument[instr_idx].getLfo(i);
            bool isRandom = (envCfg.loop == 1) && (lfoCfg.shape == lfoShapeRandom);
            envelopePtr[i]->setIsRandom(isRandom);
            if (i != envCutoff || (!trackControlParameter[(int)controlType::sequencerMode]
                                                         [(int)parameterList::filterAttack] &&
                                   !trackControlParameter[(int)controlType::sequencerMode2]
                                                         [(int)parameterList::filterAttack]))
            {
                envelopePtr[i]->setWavetableParams(lfoCfg.shape, lfoCfg.wtBend, lfoCfg.wtSust);
            }
            envelopePtr[i]->start();
            setSyncParamsLFO(i);
        }
    }
    //*******

    //*******
    status = 1;
    if (isTrackDisplayed)
        onEndDisplay = true;
    //******* start env
    //	__enable_irq();
    AudioInterrupts();
    return status;
}

//********************************** NOTE OFF
//@formatter:off
AUDIO_CORE_RAM void playerEngine::noteOff(int8_t option)
{
    switch (option)
    {
        case Sequencer::STEP_NOTE_OFF_FADE: noteOffFade(); break;
        case Sequencer::STEP_NOTE_OFF_CUT: noteOffCut(); break;
        case Sequencer::STEP_NOTE_OFF_DEFAULT: noteOffOrdinary(); break;
        default: noteOffOrdinary(); break;
    }
}
//@formatter:on
//
//************************************* handle noteOff
AUDIO_CORE_RAM void playerEngine::noteOffFade()
{
    //	__disable_irq();
    AudioNoInterrupts();
    for (int channel = 0; channel < playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->envelopeRelease(300);
        playMemPtr[channel]->envelopeNoteOff();
    }
    // lfo ma dzialac do konca fade

    for (uint8_t i = envPan; i < ACTIVE_ENVELOPES; i++)
    {
        uint8_t curLoop = mtProject.instrument[currentInstrument_idx].getEnv(i).loop;
        if ((curLoop == 0 || curLoop == 2) && !isPerfOrSeqModActive(i))
        {
            envelopePtr[i]->stop();
        }
    }

    AudioInterrupts();
    //	__enable_irq();
}
AUDIO_CORE_RAM void playerEngine::noteOffCut()
{
    //	__disable_irq();
    AudioNoInterrupts();
    // caly voice i automatyka zabijane
    for (int channel = 0; channel < playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->envelopeNoteOff();
        playMemPtr[channel]->envelopeSetIdle();
    }

    for (uint8_t i = envPan; i < ACTIVE_ENVELOPES; i++)
    {
        envelopePtr[i]->stop();
        envelopePtr[i]->killToZero();
    }

    for (int channel = 0; channel < playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->stop();
    }

    if (isTrackDisplayed)
        onEndDisplay = true;

    AudioInterrupts();
    //	__enable_irq();
}
AUDIO_CORE_RAM void playerEngine::noteOffOrdinary()
{
    //	__disable_irq();
    AudioNoInterrupts();

    for (int channel = 0; channel < playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->envelopeNoteOff();
    }

    uint8_t curAmpLoop = mtProject.instrument[currentInstrument_idx].envelope[envAmp].loop;
    if ((curAmpLoop == 1 || curAmpLoop == 3) ||
        (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::lfoAmp]) ||
        (trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::lfoAmp]) ||
        (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::lfoAmp]))
    {
        // amp loop nie ma release wiec wszystko zabijamy od razu
        for (uint8_t i = envPan; i < ACTIVE_ENVELOPES; i++)
        {
            envelopePtr[i]->stop();
        }
    }
    else
    {
        // env ampa nie zabija od razu loopow - maja grac do konca env

        for (uint8_t i = envPan; i < ACTIVE_ENVELOPES; i++)
        {
            uint8_t curLoop = mtProject.instrument[currentInstrument_idx].getEnv(i).loop;
            if ((curLoop == 0 || curLoop == 2) && !isPerfOrSeqModActive(i))
            {
                envelopePtr[i]->stop();
            }
        }
    }

    if (!mtProject.instrument[currentInstrument_idx].envelope[envAmp].enable)
    {
        //		playMemPtr->stop();
        if (isTrackDisplayed)
            onEndDisplay = true;
        for (uint8_t i = envPan; i < ACTIVE_ENVELOPES; i++)
        {
            uint8_t curLoop = mtProject.instrument[currentInstrument_idx].getEnv(i).loop;
            if ((mtProject.instrument[currentInstrument_idx].isEnvEnabled(i) &&
                 (curLoop == 1 || curLoop == 3)) ||
                isPerfOrSeqModActive(i))
            {
                currentEnvelopeModification[i] = 0;
            }
        }
    }
    else
    {
        if ((curAmpLoop == 0 &&
             mtProject.instrument[currentInstrument_idx].envelope[envAmp].release == 0.0f) ||
            (envelopePassFlag) || (curAmpLoop == 1 || curAmpLoop == 3))
        {
            //			playMemPtr->stop();
            if (isTrackDisplayed)
                onEndDisplay = true;
            for (uint8_t i = envPan; i < ACTIVE_ENVELOPES; i++)
            {
                uint8_t curLoop = mtProject.instrument[currentInstrument_idx].getEnv(i).loop;
                if ((mtProject.instrument[currentInstrument_idx].isEnvEnabled(i) &&
                     (curLoop == 1 || curLoop == 3)) ||
                    isPerfOrSeqModActive(i))
                {
                    currentEnvelopeModification[i] = 0;
                }
            }
        }
    }
    AudioInterrupts();
    //    __enable_irq();
}

//************************************* handle noteOn
AUDIO_CORE_RAM void playerEngine::handleInitNoteOnAllEnvelopes()
{
    // envAmp killuje sie wewnetrznie
    for (uint8_t i = envPan; i < ACTIVE_ENVELOPES; i++)
    {
        envelopePtr[i]->kill();
    }

    handleInitNoteOnEnvelope(envAmp);

    const auto& instr = mtProject.instrument[currentInstrument_idx];

    if (instr.filterEnable)
    {
        handleInitNoteOnEnvelope(envCutoff);
    }

    if ((instr.playMode == playModeWavetable) || (instr.playMode == playModeSynth))
    {
        handleInitNoteOnEnvelope(envWtPos);
    }

    if (instr.playMode == playModeGranular)
    {
        handleInitNoteOnEnvelope(envGranPos);
        handleInitNoteOnEnvelope(envGranLen);
    }
    else if (instr.modDestination != modDestOff)
    {
        handleInitNoteOnEnvelope(envMod);
    }
    for (uint8_t slot = envMod2; slot < envEngineMax; slot++)
    {
        if (instr.isEnvEnabled(slot))
        {
            handleInitNoteOnEnvelope(slot);
        }
    }
    handleInitNoteOnEnvelope(envPan);
    handleInitNoteOnEnvelope(envFinetune);
}

AUDIO_CORE_RAM void playerEngine::handleNoteOnFilter()
{
    if (mtProject.instrument[currentInstrument_idx].filterEnable)
    {
        filterConnect();

        for (int channel = 0; channel < (int)filterPtr.size(); channel++)
        {
            filterPtr[channel]->resonance(mtProject.instrument[currentInstrument_idx].resonance +
                                          RESONANCE_OFFSET);
        }
        if (!isActiveEnvelope(envCutoff))
            modCutoff(mtProject.instrument[currentInstrument_idx].cutOff);

        changeFilterType(mtProject.instrument[currentInstrument_idx].filterType);
    }
    else if (!mtProject.instrument[currentInstrument_idx].filterEnable)
    {
        filterDisconnect();
    }

    for (int channel = 0; channel < (int)filterPtr.size(); channel++)
    {
        filterPtr[channel]->setOverdriveAmount(
          mtProject.instrument[currentInstrument_idx].overdrive);
    }
}

AUDIO_CORE_RAM void playerEngine::handleNoteOnGain()
{
    assert(playMemPtr[0] != nullptr);
    if (!playMemPtr[0]->isPlaying())
    {
        for (int channel = 0; channel < (int)ampPtr.size(); channel++)
        {
            ampPtr[channel]->gain(0.0f); // start approximation from 0 point if track isn't audible
            ampPtr[channel]->gain(0.0f); // twice to be sure that previous and current values are
                                         // cleared (even with synchronization problems)
        }
        // todo: proper solution is setting playMemPtr and ampPtr parameters in the same place
        // (update in audio thread after the seq and before the processing) and force this gain once
    }

    setRefreshBit(VOLUME_MASK);
}

AUDIO_CORE_RAM void playerEngine::handleNoteOnPanning()
{
    assert(playMemPtr[0] != nullptr);
    if (!playMemPtr[0]->isPlaying()) // todo: see handleNoteOnGain() description
    {
#ifndef PLATFORM_IMX
        monoToStereoPtr->gain(1.0f, MonoToStereo::Channel::left);
        monoToStereoPtr->gain(1.0f, MonoToStereo::Channel::left);
        monoToStereoPtr->gain(1.0f, MonoToStereo::Channel::right);
        monoToStereoPtr->gain(1.0f, MonoToStereo::Channel::right);
#else
        engine.getMixerAndFx()->getTracksMixer(MixerAndFxChannelType::kLeft)->gain(nChannel, 1.0f);
        engine.getMixerAndFx()->getTracksMixer(MixerAndFxChannelType::kRight)->gain(nChannel, 1.0f);
#endif
    }

    setRefreshBit(PANNING_MASK);
}

void playerEngine::handleNoteOnBitCrusher()
{
    modBitDepth(mtProject.instrument[currentInstrument_idx].bitDepth);
}

AUDIO_CORE_RAM void playerEngine::handleNoteOnDelaySend()
{
    assert(playMemPtr[0] != nullptr);
    if (!playMemPtr[0]->isPlaying()) // todo: see handleNoteOnGain() description
    {
        preDelayMixer->gain(nChannel, 0.0f);
        preDelayMixer->gain(nChannel, 0.0f);
#ifdef PLATFORM_IMX
        static constexpr int kInputsChannels = 2;
        preDelayMixer->gain(nChannel + audioEngine::kVoices + kInputsChannels, 0.0f);
        preDelayMixer->gain(nChannel + audioEngine::kVoices + kInputsChannels, 0.0f);
#endif
    }
    setRefreshBit(DELAY_SEND_MASK);
}

AUDIO_CORE_RAM void playerEngine::handleNoteOnReverbSend()
{
    assert(playMemPtr[0] != nullptr);
    if (!playMemPtr[0]->isPlaying()) // todo: see handleNoteOnGain() description
    {
        prePolyverbMixer->gain(nChannel, 0.0f);
        prePolyverbMixer->gain(nChannel, 0.0f);
#ifdef PLATFORM_IMX
        static constexpr int kInputsChannels = 2;
        prePolyverbMixer->gain(nChannel + audioEngine::kVoices + kInputsChannels, 0.0f);
        prePolyverbMixer->gain(nChannel + audioEngine::kVoices + kInputsChannels, 0.0f);
#endif
    }

    setRefreshBit(REVERB_SEND_MASK);
}

void playerEngine::handleInitNoteOnAmpEnvelope()
{
    if (mtProject.instrument[currentInstrument_idx].envelope[envAmp].enable)
    {
        uint8_t curAmpLoop = mtProject.instrument[currentInstrument_idx].envelope[envAmp].loop;
        if (curAmpLoop == 1 || curAmpLoop == 3)
        {
            calcLfoBasedEnvelope(&lfoBasedEnvelope[envAmp],
                                 &mtProject.instrument[currentInstrument_idx].lfo[envAmp],
                                 mtProject.instrument[currentInstrument_idx].lfo[envAmp].speed,
                                 1,
                                 curAmpLoop);

            initEnvelopesParameters(envAmp, &lfoBasedEnvelope[envAmp]);
            setSyncParamsLFO(envAmp);
        }
        else
        {
            initEnvelopesParameters(envAmp,
                                    &mtProject.instrument[currentInstrument_idx].envelope[envAmp]);
        }
    }
    else
    {
        initEnvelopesParameters(envAmp, (envelopeGenerator::strEnv*)&passEnvelope);
    }
}

void playerEngine::handleInitNoteOnEnvelope(uint8_t n)
{
    if (n == envAmp)
    {
        handleInitNoteOnAmpEnvelope();
    }
    else
    {
        currentEnvelopeModification[n] = 0;
        const auto& instr = mtProject.instrument[currentInstrument_idx];
        if (instr.isEnvEnabled(n))
        {
            const auto& envCfg = instr.getEnv(n);
            const auto& lfoCfg = instr.getLfo(n);
            uint8_t curLoop = envCfg.loop;
            if (curLoop == 1 || curLoop == 3)
            {
                calcLfoBasedEnvelope(&lfoBasedEnvelope[n],
                                     const_cast<strInstrument::strEnvBasedLfo*>(&lfoCfg),
                                     lfoCfg.speed,
                                     0,
                                     curLoop);
                initEnvelopesParameters(n, &lfoBasedEnvelope[n]);
                setSyncParamsLFO(n);
            }
            else
            {
                initEnvelopesParameters(n, const_cast<envelopeGenerator::strEnv*>(&envCfg));
            }
        }
    }
}
//*************************************** FX NOTE ON
void playerEngine::handleInitFxNoteOnEnvelope(uint8_t n)
{
    if (n == envAmp)
    {
        handleInitFxNoteOnAmpEnvelope();
    }
    else
    {
        if (!isPerfOrSeqModActive(n))
        {
            handleInitNoteOnEnvelope(n);
        }

        if (n == envCutoff && (trackControlParameter[(int)controlType::sequencerMode]
                                                    [(int)parameterList::filterAttack] ||
                               trackControlParameter[(int)controlType::sequencerMode2]
                                                    [(int)parameterList::filterAttack]))
        {
            uint8_t curFilterLoop =
              mtProject.instrument[currentInstrument_idx].envelope[envCutoff].loop;
            if (curFilterLoop >= 2)
            {
                envelopePtr[envCutoff]->setWavetableDurationMs(currentSeqModValues.filterAttack);
            }
            else
            {
                envelopePtr[envCutoff]->putMidiParameter(
                  0, 41, (currentSeqModValues.filterAttack / 1000) * 8);
                envelopePtr[envCutoff]->putMidiParameter(
                  0, 43, (currentSeqModValues.filterAttack / 1000) * 8);
                envelopePtr[envCutoff]->putMidiParameter(
                  0, 45, (currentSeqModValues.filterRelease / 1000) * 8);
            }
        }

        if (mtProject.instrument[currentInstrument_idx].isEnvEnabled(n))
        {
            uint8_t curLoop = mtProject.instrument[currentInstrument_idx].getEnv(n).loop;
            if (curLoop == 1 || curLoop == 3)
            {
                if (n < envMax && trackControlParameter[(int)controlType::performanceMode][n])
                {
                    switch (n)
                    {
                        case envCutoff:
                            changeCutoffLfoRatePerformanceMode(performanceMod.lfoCutoffRate);
                            break;
                        case envGranPos:
                            changePositionLfoRatePerformanceMode(performanceMod.lfoPositionRate);
                            break;
                        case envWtPos:
                            changePositionLfoRatePerformanceMode(performanceMod.lfoPositionRate);
                            break;
                        case envPan:
                            changePositionLfoRatePerformanceMode(performanceMod.lfoPanningRate);
                            break;
                        case envFinetune:
                            changePositionLfoRatePerformanceMode(performanceMod.lfoFinetuneRate);
                            break;
                    }
                }
            }
        }
    }
}
void playerEngine::handleInitFxNoteOnAmpEnvelope()
{
    if ((!trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::lfoAmp]) &&
        (!trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::lfoAmp]) &&
        (!trackControlParameter[(int)controlType::performanceMode][(int)parameterList::lfoAmp]))
    {
        handleInitNoteOnAmpEnvelope();
    }
    if (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::ampAttack] ||
        trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::ampAttack])
    {
        uint8_t curAmpLoop = mtProject.instrument[currentInstrument_idx].envelope[envAmp].loop;
        for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
        {
            playMemPtr[channel]->envelopeAttack(currentSeqModValues.ampAttack);
            playMemPtr[channel]->envelopeDecay(curAmpLoop >= 2 ? currentSeqModValues.ampAttack
                                                               : currentSeqModValues.ampRelease);
            playMemPtr[channel]->envelopeRelease(currentSeqModValues.ampRelease);
        }
    }

    if (mtProject.instrument[currentInstrument_idx].envelope[envAmp].enable)
    {
        uint8_t curAmpLoop = mtProject.instrument[currentInstrument_idx].envelope[envAmp].loop;
        if (curAmpLoop == 1 || curAmpLoop == 3)
        {
            if (trackControlParameter[(int)controlType::performanceMode]
                                     [(int)parameterList::lfoAmp])
            {
                changeCutoffLfoRatePerformanceMode(performanceMod.lfoAmpRate);
            }
        }
    }
}
AUDIO_CORE_RAM void playerEngine::handleInitFxNoteOnAllEnvelopes()
{
    for (uint8_t i = envPan; i < ACTIVE_ENVELOPES; i++)
    {
        envelopePtr[i]->kill();
    }

    handleInitFxNoteOnEnvelope(envAmp);

    const auto& instr = mtProject.instrument[currentInstrument_idx];

    if (instr.filterEnable)
    {
        handleInitFxNoteOnEnvelope(envCutoff);
    }

    if ((instr.sample.type == mtSampleTypeWavetable) || (instr.playMode == playModeWavetable) ||
        (instr.playMode == playModeSynth))
    {
        handleInitFxNoteOnEnvelope(envWtPos);
    }

    if ((instr.sample.type == mtSampleTypeWaveFile) && (instr.playMode == playModeGranular))
    {
        handleInitFxNoteOnEnvelope(envGranPos);
        handleInitFxNoteOnEnvelope(envGranLen);
    }
    else if (instr.modDestination != modDestOff)
    {
        handleInitFxNoteOnEnvelope(envMod);
    }
    for (uint8_t slot = envMod2; slot < envEngineMax; slot++)
    {
        if (instr.isEnvEnabled(slot))
        {
            handleInitFxNoteOnEnvelope(slot);
        }
    }
    handleInitFxNoteOnEnvelope(envPan);
    handleInitFxNoteOnEnvelope(envFinetune);
}

AUDIO_CORE_RAM void playerEngine::handleFxNoteOnFilter()
{
    //***** resonance
    if (mtProject.instrument[currentInstrument_idx].filterEnable)
    {
        for (int channel = 0; channel < filterPtr.size(); channel++)
        {
            filterPtr[channel]->resonance(mtProject.instrument[currentInstrument_idx].resonance +
                                          RESONANCE_OFFSET);
        }
    }
    //*****
    //***** cutoff + enable

    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::filterCutoff])
    {
        changeCutoffPerformanceMode(performanceMod.cutoff); // włączenie filtra jest w środku
    }
    else if ((trackControlParameter[(int)controlType::sequencerMode]
                                   [(int)parameterList::filterCutoff]) ||
             (trackControlParameter[(int)controlType::sequencerMode2]
                                   [(int)parameterList::filterCutoff]))
    {
        filterConnect();
        if (!isActiveEnvelope(envCutoff))
            modCutoff(currentSeqModValues.filterCutoff);
    }
    else
    {
        if (mtProject.instrument[currentInstrument_idx].filterEnable)
        {
            filterConnect();
            if (!isActiveEnvelope(envCutoff))
                modCutoff(mtProject.instrument[currentInstrument_idx].cutOff);
        }
        else if (!mtProject.instrument[currentInstrument_idx].filterEnable)
            filterDisconnect();
    }
    //*****
    //***** filter type
    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::filterType])
    {
        changeFilterTypePerformanceMode(performanceMod.filterType);
    }
    else if ((trackControlParameter[(int)controlType::sequencerMode]
                                   [(int)parameterList::filterType]) ||
             (trackControlParameter[(int)controlType::sequencerMode2]
                                   [(int)parameterList::filterType]))
    {
        changeFilterType(currentSeqModValues.filterType);
    }
    else
    {
        if (mtProject.instrument[currentInstrument_idx].filterEnable)
        {
            changeFilterType(mtProject.instrument[currentInstrument_idx].filterType);
        }
    }
    //*****

    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::overdrive])
    {
        changeOverdrivePerformanceMode(performanceMod.overdrive);
    }
    else if ((trackControlParameter[(int)controlType::sequencerMode]
                                   [(int)parameterList::overdrive]) ||
             (trackControlParameter[(int)controlType::sequencerMode2]
                                   [(int)parameterList::overdrive]))
    {
        modOverdrive(currentSeqModValues.overdrive);
    }
    else
    {
        modOverdrive(mtProject.instrument[currentInstrument_idx].overdrive);
    }
}

AUDIO_CORE_RAM void playerEngine::handleFxNoteOnGain()
{
    handleNoteOnGain();
}

AUDIO_CORE_RAM void playerEngine::handleFxNoteOnPanning()
{
    handleNoteOnPanning();
}

AUDIO_CORE_RAM void playerEngine::handleFxNoteOnBitDepth()
{
    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::bitDepth])
    {
        changeBitDepthPerformanceMode(performanceMod.bitDepth);
    }
    else if ((trackControlParameter[(int)controlType::sequencerMode]
                                   [(int)parameterList::bitDepth]) ||
             (trackControlParameter[(int)controlType::sequencerMode2]
                                   [(int)parameterList::bitDepth]))
    {
        modBitDepth(currentSeqModValues.bitDepth);
    }
    else
    {
        modBitDepth(mtProject.instrument[currentInstrument_idx].bitDepth);
    }
}

AUDIO_CORE_RAM void playerEngine::handleFxNoteOnDelaySend()
{
    handleNoteOnDelaySend();
}

AUDIO_CORE_RAM void playerEngine::handleFxNoteOnReverbSend()
{
    handleNoteOnReverbSend();
}
