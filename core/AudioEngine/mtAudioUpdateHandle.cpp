#include "ampLogarythmicValues.h"
#include "mtAudioEngine.h"
#include "mtSequencer.h"

using namespace mtAudioEngineConstans;

AUDIO_CORE_RAM void playerEngine::update()
{
    if (isBlockedInstrument[currentInstrument_idx])
        return;
    //**************HANDLE UPDATE END RELEASE
    // obsluga tego zdarzenia jako pierwsza zeby zapobiec zawieszaniu nut
    handleUpdateEndReleaseAction();
    //***********************
    //**************HANDLE UPDATE END PLAY DETECT
    handleUpdateEndPlayDetect();
    //***********************
    //**************HANDLE UPDATE ENVELOPE
    // stablicowanie warunkow odswiezenia envelopeow
    const auto& instr = mtProject.instrument[currentInstrument_idx];
    bool envelopeCondition[ACTIVE_ENVELOPES];
    envelopeCondition[envAmp] =
      ((instr.envelope[envAmp].enable == 1) ||
       (trackControlParameter[(int)controlType::sequencerMode][envelopesControlValue[envAmp]]) ||
       (trackControlParameter[(int)controlType::sequencerMode2][envelopesControlValue[envAmp]]) ||
       (trackControlParameter[(int)controlType::performanceMode][envelopesControlValue[envAmp]]));
    envelopeCondition[envCutoff] = (instr.filterEnable == 1);
    envelopeCondition[envPan] = true;
    envelopeCondition[envWtPos] =
      (instr.sample.type == mtSampleTypeWavetable) || (instr.playMode == playModeWavetable) ||
      ((instr.sample.type == mtSampleTypeWaveFile) && (instr.playMode == playModeGranular)) ||
      (instr.playMode == playModeSynth);
    envelopeCondition[envGranPos] =
      ((instr.sample.type == mtSampleTypeWaveFile) && (instr.playMode == playModeGranular)) ||
      (instr.playMode != playModeGranular && instr.modDestination != modDestOff);
    envelopeCondition[envFinetune] = true;
    for (uint8_t slot = envMod2; slot < envEngineMax; slot++)
    {
        envelopeCondition[slot] = instr.isEnvEnabled(slot);
    }
    //*******
    for (uint8_t i = envAmp; i < ACTIVE_ENVELOPES; i++)
    {
        handleUpdateEnvelope(i, envelopeCondition[i]);
    }
    //***********************
    //**************HANDLE UPDATE REFRESH PARAMS
    if (refreshRegister)
    {
        // Jako pierwsza obsluga parametrow sterowanych z envelope
        handleUpdateRefreshCutoff();
        handleUpdateRefreshPanning();
        handleUpdateRefreshGranPos();
        handleUpdateRefreshWtPos();
        handleUpdateRefreshTune();
        handleUpdateRefreshFinetune();
        //**********
        handleUpdateRefreshLoopPoints();
        handleUpdateRefreshVolume();
        handleUpdateRefreshResonance();
        handleUpdateRefreshDelay();
        handleUpdateRefreshReverb();
        handleUpdateRefreshOverdrive();
        handleUpdateRefreshBitDepth();
        handleUpdateRefreshGranLen();
        handleUpdateRefreshGranWave();
        handleUpdateRefreshGranLoop();
        handleUpdateRefreshAmpLFO();
        handleUpdateRefreshCutoffLFO();
        handleUpdateRefreshWtPosLFO();
        handleUpdateRefreshGranPosLFO();
        handleUpdateRefreshMod2LFO();
        handleUpdateRefreshPanningLFO();
        handleUpdateRefreshFinetuneLFO();
    }
    //***********************
}
inline void playerEngine::handleUpdateEndPlayDetect()
{
    currentPlayState = playMemPtr[0]->isPlaying();
    if (currentPlayState == 0 && lastPlayState == 1)
    {
        //		playMemPtr->envelopeNoteOff();
        interfacePlayingEndFlag = 1;
        if (isTrackDisplayed)
            onEndDisplay = true;
    }
    lastPlayState = currentPlayState;
}

inline void playerEngine::handleUpdateEnvelope(uint8_t type, bool enableCondition)
{
    if (enableCondition)
    {
        if (type == envAmp)
        {
            uint8_t curAmpLoop = mtProject.instrument[currentInstrument_idx].envelope[envAmp].loop;
            if ((curAmpLoop == 1 || curAmpLoop == 3) ||
                (trackControlParameter[(int)controlType::sequencerMode]
                                      [(int)parameterList::lfoAmp]) ||
                (trackControlParameter[(int)controlType::sequencerMode2]
                                      [(int)parameterList::lfoAmp]) ||
                (trackControlParameter[(int)controlType::performanceMode]
                                      [(int)parameterList::lfoAmp]))
            {
                if (playMemPtr[0]->envelopeGetState() != 0)
                {
                    if (sequencer.getSeqState())
                    {
                        currentSeqTempo = sequencer.getCurrentTempo();

                        if (currentSeqTempo != lastSeqTempo)
                        {
                            uint8_t mostSignificantLFORate =
                              mtProject.instrument[currentInstrument_idx].lfo[envAmp].speed;
                            if (trackControlParameter[(int)controlType::performanceMode]
                                                     [(int)parameterList::lfoAmp])
                            {
                                mostSignificantLFORate = currentPerformanceValues.lfoAmpRate;
                            }
                            else if ((trackControlParameter[(int)controlType::sequencerMode2]
                                                           [(int)parameterList::lfoAmp]) ||
                                     (trackControlParameter[(int)controlType::performanceMode]
                                                           [(int)parameterList::lfoAmp]))
                            {
                                mostSignificantLFORate = currentSeqModValues.lfoAmpRate;
                            }
                            uint8_t targetLoop = (curAmpLoop == 3) ? 3 : 1;
                            calcLfoBasedEnvelope(
                              &lfoBasedEnvelope[envAmp],
                              &mtProject.instrument[currentInstrument_idx].lfo[envAmp],
                              mostSignificantLFORate,
                              1,
                              targetLoop);
                            initEnvelopesParameters(envAmp, &lfoBasedEnvelope[envAmp]);
                        }

                        lastSeqTempo = currentSeqTempo;
                    }
                }
            }
        }
        else
        {
            const auto& instr = mtProject.instrument[currentInstrument_idx];
            auto applyTypeRefresh = [&]()
            {
                const bool isExtraMod =
                  (type >= envMod2 && type < envEngineMax && instr.isExtraModAvailable());
                const bool isPoolMod0 = (type == envMod && instr.playMode != playModeGranular);
                if (type == envGranLen && instr.playMode == playModeGranular)
                {
                    refreshRegister |= GRANULAR_LEN_SEND_MASK;
                }
                else if (isPoolMod0 || isExtraMod)
                {
                    const uint8_t modIdx = isExtraMod ? (uint8_t)(type - envMod2 + 1) : 0;
                    refreshRegister |= getDestRefreshMask(instr.getModDestination(modIdx));
                }
                else
                {
                    refreshRegister |= envTargetRefreshMask[type];
                }
            };

            if (instr.isEnvEnabled(type) || isPerfOrSeqModActive(type))
            {
                bool isActiveEnvelope = ((envelopePtr[type]->isKeyPressed() == 1) ||
                                         (envelopePtr[type]->getPhase() != 0));

                uint8_t curLoop = instr.getEnv(type).loop;
                if ((curLoop == 1 || curLoop == 3) || isPerfOrSeqModActive(type))
                {
                    if (isActiveEnvelope)
                    {
                        if (sequencer.getSeqState())
                        {
                            envelopePtr[type]->syncTrackerSeq(sequencer.getSeqTimer(),
                                                              sequencer.getCurrentTempo());
                            envelopePtr[type]->setSync(1);
                        }
                        else
                        {
                            envelopePtr[type]->setSync(0);
                        }
                    }
                }
                else
                {
                    envelopePtr[type]->setSync(0);
                }
                if (isActiveEnvelope)
                {
                    currentEnvelopeModification[type] = envelopePtr[type]->getOut();
                    applyTypeRefresh();
                }
                else if (currentEnvelopeModification[type] != 0.0f)
                {
                    currentEnvelopeModification[type] = 0.0f;
                    applyTypeRefresh();
                }
            }
            else if (currentEnvelopeModification[type] != 0.0f)
            {
                currentEnvelopeModification[type] = 0.0f;
                envelopePtr[type]->stop();
                applyTypeRefresh();
            }
        }
    }
    else if (type >= envPan && type < envEngineMax && currentEnvelopeModification[type] != 0.0f)
    {
        currentEnvelopeModification[type] = 0.0f;
        const auto& instr = mtProject.instrument[currentInstrument_idx];
        const bool isExtraMod =
          (type >= envMod2 && type < envEngineMax && instr.isExtraModAvailable());
        const bool isPoolMod0 = (type == envMod && instr.playMode != playModeGranular);
        if (type == envGranLen && instr.playMode == playModeGranular)
        {
            refreshRegister |= GRANULAR_LEN_SEND_MASK;
        }
        else if (isPoolMod0 || isExtraMod)
        {
            const uint8_t modIdx = isExtraMod ? (uint8_t)(type - envMod2 + 1) : 0;
            refreshRegister |= getDestRefreshMask(instr.getModDestination(modIdx));
        }
        else
        {
            refreshRegister |= envTargetRefreshMask[type];
        }
    }
}

inline void playerEngine::handleUpdateEndReleaseAction()
{
    if (playMemPtr[0]->envelopeEndRelease())
    {
        for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
        {
            playMemPtr[channel]->envelopeClearEndReleaseFlag();
        }
        interfaceEndReleaseFlag = 1;
        if (isTrackDisplayed)
            onEndDisplay = true;
        //		playMemPtr->stop();
        for (uint8_t i = envPan; i < ACTIVE_ENVELOPES; i++)
        {
            uint8_t curLoop = mtProject.instrument[currentInstrument_idx].getEnv(i).loop;
            if ((mtProject.instrument[currentInstrument_idx].isEnvEnabled(i) &&
                 (curLoop == 1 || curLoop == 3)) ||
                isPerfOrSeqModActive(i))
            {
                currentEnvelopeModification[i] = 0;
                envelopePtr[i]->stop();
            }
        }
    }
}

inline void playerEngine::handleUpdateRefreshLoopPoints()
{
    if (refreshRegister & (LP1_MASK | LP2_MASK))
    {
        refreshRegister &= (~(LP1_MASK | LP2_MASK));
        const auto& instr = mtProject.instrument[currentInstrument_idx];
        int32_t minP = (int32_t)instr.startPoint;
        int32_t maxP = (int32_t)instr.endPoint;
        if (maxP < minP)
            maxP = minP;
        int32_t base1 = (int32_t)instr.loopPoint1;
        int32_t base2 = (int32_t)instr.loopPoint2;
        int32_t span = (base2 > base1) ? (base2 - base1) : 0;
        if (span > (maxP - minP))
            span = maxP - minP;

        const float lpMod = getActiveModDelta(instr, modDestLoopPos);
        const int32_t shift =
          (lpMod != 0.0f) ? (int32_t)std::roundf(lpMod * (float)SAMPLE_POINT_POS_MAX) : 0;

        int32_t newLp1 = base1 + shift;
        if (newLp1 < minP)
            newLp1 = minP;
        if (newLp1 + span > maxP)
            newLp1 = maxP - span;
        if (newLp1 < minP)
            newLp1 = minP;
        int32_t newLp2 = newLp1 + span;
        if (newLp2 > maxP)
            newLp2 = maxP;

        const uint16_t targetLp1 = (uint16_t)clamp<int>(newLp1, 0, (int)SAMPLE_POINT_POS_MAX);
        const uint16_t targetLp2 = (uint16_t)clamp<int>(newLp2, 0, (int)SAMPLE_POINT_POS_MAX);

        if (targetLp1 >= lastAppliedLp1)
        {
            modLP2(targetLp2);
            modLP1(targetLp1);
        }
        else
        {
            modLP1(targetLp1);
            modLP2(targetLp2);
        }
        lastAppliedLp1 = targetLp1;
    }
}
inline int8_t playerEngine::computePitchModTuneAndRemCents(const strInstrument& instr,
                                                            int16_t& outRemCents) const
{
    int8_t baseTune = 0;
    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::tune])
    {
        baseTune = currentPerformanceValues.tune;
    }
    else if (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::tune] ||
             trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::tune])
    {
        baseTune = currentSeqModValues.tune;
    }
    else
    {
        baseTune = instr.tune;
    }

    int16_t baseFine = 0;
    if (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::fineTune] ||
        trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::fineTune])
    {
        baseFine = currentSeqModValues.fineTune;
    }
    else
    {
        baseFine = instr.fineTune;
    }

    const float envFineOut =
      (instr.isEnvEnabled(envFinetune) || isPerfOrSeqModActive(envFinetune))
        ? currentEnvelopeModification[envFinetune]
        : 0.0f;

    const float totalCents =
      (float)baseFine + (envFineOut * (float)MAX_INSTRUMENT_FINETUNE) +
      (getActiveModDelta(instr, modDestFinetune) * (float)MAX_INSTRUMENT_FINETUNE) +
      (getActiveModDelta(instr, modDestPitch) * 2400.0f);

    const float scaled = (totalCents + 50.0f) * 0.01f;
    int32_t semiShift = (int32_t)scaled;
    if (scaled < (float)semiShift)
    {
        semiShift--;
    }
    const int32_t roundedCents = (int32_t)(totalCents + ((totalCents >= 0.0f) ? 0.5f : -0.5f));
    outRemCents = (int16_t)clamp<int>(
      (int)(roundedCents - (semiShift * 100)),
      MIN_INSTRUMENT_FINETUNE,
      MAX_INSTRUMENT_FINETUNE);
    return (int8_t)clamp<int>((int)baseTune + (int)semiShift, -120, 120);
}
inline void playerEngine::handleUpdateRefreshFinetune()
{
    if (refreshRegister & FINETUNE_MASK)
    {
        refreshRegister &= (~FINETUNE_MASK);

        if (!isActiveEnvelope(envFinetune))
        {
            currentEnvelopeModification[envFinetune] = 0;
        }

        const auto& instr = mtProject.instrument[currentInstrument_idx];
        int16_t remCents = 0;
        const int8_t localTune = computePitchModTuneAndRemCents(instr, remCents);

        if (localTune != lastAppliedTune)
        {
            modTune(localTune);
            for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
            {
                playMemPtr[channel]->setForcedTune(localTune);
            }
            lastAppliedTune = localTune;
        }

        for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
        {
            playMemPtr[channel]->setForcedFineTune(remCents);
        }

        modFineTune((int8_t)remCents);
    }
}
inline void playerEngine::handleUpdateRefreshTune()
{
    if (refreshRegister & TUNE_MASK)
    {
        refreshRegister &= (~TUNE_MASK);

        const auto& instr = mtProject.instrument[currentInstrument_idx];
        int16_t remCents = 0;
        const int8_t localTune = computePitchModTuneAndRemCents(instr, remCents);

        modTune(localTune);

        for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
        {
            playMemPtr[channel]->setForcedTune(localTune);
        }
        lastAppliedTune = localTune;
        refreshRegister |= FINETUNE_MASK;
    }
}
inline void playerEngine::handleUpdateRefreshVolume()
{
    if (refreshRegister & VOLUME_MASK)
    {
        refreshRegister &= (~VOLUME_MASK);

        uint8_t volume = getMostSignificantVolume();
        float localAmount = getMostSignificantAmount();
        const float modVolDelta =
          getActiveModDelta(mtProject.instrument[currentInstrument_idx], modDestVolume);
        if (modVolDelta != 0.0f)
        {
            localAmount *= clamp<float>(1.0f + modVolDelta, 0.0f, 2.0f);
        }

        modVolume(ampLogValues[volume] * localAmount);
    }
}
inline void playerEngine::handleUpdateRefreshPanning()
{
    if (refreshRegister & PANNING_MASK)
    {
        refreshRegister &= (~PANNING_MASK);

        if (!isActiveEnvelope(envPan))
        {
            currentEnvelopeModification[envPan] = 0;
        }

        int32_t localPanningMod = currentEnvelopeModification[envPan] * PANNING_MAX;
        localPanningMod +=
          getActiveModDelta(mtProject.instrument[currentInstrument_idx], modDestPanning) *
          PANNING_MAX;

        int16_t localPanning = 0;

        if (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::panning] ||
            trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::panning])
        {
            localPanning = currentSeqModValues.panning;
        }
        else
        {
            localPanning = mtProject.instrument[currentInstrument_idx].panning;
        }

        if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::panning])
        {
            localPanning = (int16_t)clamp<int>(
              (int)localPanning + (int)performanceMod.panning, (int)PANNING_MIN, (int)PANNING_MAX);
        }

        localPanning = (int16_t)clamp<int>(
          (int)localPanning + (int)localPanningMod, (int)PANNING_MIN, (int)PANNING_MAX);

        modPanning(localPanning);
    }
}
inline void playerEngine::handleUpdateRefreshCutoff()
{
    if (refreshRegister & CUTOFF_MASK)
    {
        refreshRegister &= (~CUTOFF_MASK);

        if (!isActiveEnvelope(envCutoff))
        {
            currentEnvelopeModification[envCutoff] = 0;
        }

        float localCutoff = 0.0f;

        if (trackControlParameter[(int)controlType::performanceMode]
                                 [(int)parameterList::filterCutoff])
        {
            localCutoff = currentPerformanceValues.filterCutoff;
        }
        else if (trackControlParameter[(int)controlType::sequencerMode]
                                      [(int)parameterList::filterCutoff] ||
                 trackControlParameter[(int)controlType::sequencerMode2]
                                      [(int)parameterList::filterCutoff])
        {
            localCutoff = currentSeqModValues.filterCutoff;
        }
        else
        {
            localCutoff = mtProject.instrument[currentInstrument_idx].cutOff;
        }

        localCutoff +=
          getActiveModDelta(mtProject.instrument[currentInstrument_idx], modDestCutoff);

        if (localCutoff + currentEnvelopeModification[envCutoff] < 0.0f)
            localCutoff = 0.0f;
        else if (localCutoff + currentEnvelopeModification[envCutoff] > 1.0f)
            localCutoff = 1.0f;
        else
            localCutoff += currentEnvelopeModification[envCutoff];

        modCutoff(localCutoff);
    }
}
inline void playerEngine::handleUpdateRefreshResonance()
{
    if (refreshRegister & RESONANCE_MASK)
    {
        refreshRegister &= (~RESONANCE_MASK);
        float res =
          mtProject.instrument[currentInstrument_idx].resonance +
          getActiveModDelta(mtProject.instrument[currentInstrument_idx], modDestResonance);
        if (res < 0.0f)
            res = 0.0f;
        else if (res > 1.0f)
            res = 1.0f;
        modResonance(res);
    }
}
void playerEngine::handleUpdateRefreshDelay()
{
    if (refreshRegister & DELAY_SEND_MASK)
    {
        refreshRegister &= (~DELAY_SEND_MASK);

        uint8_t localDelaySend = 0;

        if (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::delaySend] ||
            trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::delaySend])
        {
            localDelaySend = currentSeqModValues.delaySend;
        }
        else
        {
            localDelaySend = mtProject.instrument[currentInstrument_idx].delaySend;
        }

        if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::delaySend])
        {
            localDelaySend = clamp<int>(
              (int)localDelaySend + (int)performanceMod.delaySend, (int)SEND_MIN, (int)SEND_MAX);
        }

        const float modDelay =
          getActiveModDelta(mtProject.instrument[currentInstrument_idx], modDestDelaySend);
        if (modDelay != 0.0f)
        {
            int send = (int)localDelaySend + (int)(modDelay * 100.0f);
            localDelaySend = clamp<int>(send, (int)SEND_MIN, (int)SEND_MAX);
        }

        if (((muteState == MUTE_DISABLE) && (onlyDelayMuteState == MUTE_DISABLE)) ||
            ((engine.forceDelaySend == 1) && !mtProject.values.trackMute[nChannel]))
        {
            modDelaySend(localDelaySend);
        }
        else
        {
            modDelaySend(AMP_MUTED);
        }
    }
}

void playerEngine::handleUpdateRefreshReverb()
{
    if (refreshRegister & REVERB_SEND_MASK)
    {
        refreshRegister &= (~REVERB_SEND_MASK);

        uint8_t localReverbSend = 0;

        if (trackControlParameter[(int)controlType::sequencerMode]
                                 [(int)parameterList::reverbSend] ||
            trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::reverbSend])
        {
            localReverbSend = currentSeqModValues.reverbSend;
        }
        else
        {
            localReverbSend = mtProject.instrument[currentInstrument_idx].reverbSend;
        }

        if (trackControlParameter[(int)controlType::performanceMode]
                                 [(int)parameterList::reverbSend])
        {
            localReverbSend = clamp<int>(
              (int)localReverbSend + (int)performanceMod.reverbSend, (int)SEND_MIN, (int)SEND_MAX);
        }

        const float modReverb =
          getActiveModDelta(mtProject.instrument[currentInstrument_idx], modDestReverbSend);
        if (modReverb != 0.0f)
        {
            int send = (int)localReverbSend + (int)(modReverb * 100.0f);
            localReverbSend = clamp<int>(send, (int)SEND_MIN, (int)SEND_MAX);
        }

        if (((muteState == 0) && (onlyReverbMuteState == 0)) ||
            ((engine.forceReverbSend == 1) && !mtProject.values.trackMute[nChannel]))
        {
            modReverbSend(localReverbSend);
        }
        else
        {
            modReverbSend(AMP_MUTED);
        }
    }
}

inline void playerEngine::handleUpdateRefreshOverdrive()
{
    if (refreshRegister & OVERDRIVE_MASK)
    {
        uint8_t overdrive;
        refreshRegister &= (~OVERDRIVE_MASK);

        if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::overdrive])
        {
            overdrive = currentPerformanceValues.overdrive;
        }
        else if (trackControlParameter[(int)controlType::sequencerMode]
                                      [(int)parameterList::overdrive] ||
                 trackControlParameter[(int)controlType::sequencerMode2]
                                      [(int)parameterList::overdrive])
        {
            overdrive = currentSeqModValues.overdrive;
        }
        else
        {
            overdrive = mtProject.instrument[currentInstrument_idx].overdrive;
        }

        const float modDrive =
          getActiveModDelta(mtProject.instrument[currentInstrument_idx], modDestOverdrive);
        if (modDrive != 0.0f)
        {
            int drive = (int)overdrive + (int)(modDrive * 100.0f);
            overdrive = clamp<int>(drive, 0, 100);
        }

        modOverdrive(overdrive);
    }
}

inline void playerEngine::handleUpdateRefreshBitDepth()
{
    if (refreshRegister & BIT_DEPTH_MASK)
    {
        uint8_t bitDepth;
        refreshRegister &= (~BIT_DEPTH_MASK);

        if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::bitDepth])
        {
            bitDepth = currentPerformanceValues.bitDepth;
        }
        else if (trackControlParameter[(int)controlType::sequencerMode]
                                      [(int)parameterList::bitDepth] ||
                 trackControlParameter[(int)controlType::sequencerMode2]
                                      [(int)parameterList::bitDepth])
        {
            bitDepth = currentSeqModValues.bitDepth;
        }
        else
        {
            bitDepth = mtProject.instrument[currentInstrument_idx].bitDepth;
        }

        const float modBd =
          getActiveModDelta(mtProject.instrument[currentInstrument_idx], modDestBitDepth);
        if (modBd != 0.0f)
        {
            int bd = (int)bitDepth - (int)(modBd * 12.0f);
            bitDepth = clamp<int>(bd, 4, 16);
        }

        modBitDepth(bitDepth);
    }
}

inline void playerEngine::handleUpdateRefreshWtPos()
{
    if (refreshRegister & WT_POS_SEND_MASK)
    {
        refreshRegister &= (~WT_POS_SEND_MASK);
        const auto& instr = mtProject.instrument[currentInstrument_idx];
        if (instr.playMode == playModeGranular)
        {
            return;
        }
        if ((!instr.isWtPosEnvEnabled()) &&
            (!trackControlParameter[(int)controlType::sequencerMode]
                                   [(int)parameterList::lfoWavetablePosition]) &&
            (!trackControlParameter[(int)controlType::sequencerMode2]
                                   [(int)parameterList::lfoWavetablePosition]) &&
            (!trackControlParameter[(int)controlType::performanceMode]
                                   [(int)parameterList::lfoWavetablePosition]))
        {
            currentEnvelopeModification[envWtPos] = 0;
        }

        const int32_t localWtPosisionMod = (int32_t)std::roundf(
          (currentEnvelopeModification[envWtPos] + getActiveModDelta(instr, modDestWtPos)) *
          (float)instr.sample.wavetableWindowNumber);
        uint32_t localWtPos = 0;

        if (trackControlParameter[(int)controlType::performanceMode]
                                 [(int)parameterList::wavetablePosition])
        {
            localWtPos = currentPerformanceValues.wavetablePosition;
        }
        else if (trackControlParameter[(int)controlType::sequencerMode]
                                      [(int)parameterList::wavetablePosition] ||
                 trackControlParameter[(int)controlType::sequencerMode2]
                                      [(int)parameterList::wavetablePosition])
        {
            localWtPos = currentSeqModValues.wavetablePosition;
        }
        else
        {
            localWtPos = instr.wavetableCurrentWindow;
        }

        if (instr.playMode == playModeSynth)
        {
            const int16_t pw = (int16_t)std::roundf(
              (currentEnvelopeModification[envWtPos] + getActiveModDelta(instr, modDestPulseWidth)) *
              100.0f);
            const int16_t timbreB =
              (int16_t)std::roundf(getActiveModDelta(instr, modDestTimbreB) * 100.0f);
            const int16_t oscBPitchCents =
              (int16_t)std::roundf(getActiveModDelta(instr, modDestOscBPitch) * 2400.0f);
            const int16_t detune =
              (int16_t)std::roundf(getActiveModDelta(instr, modDestDetune) * 100.0f);
            const int16_t subLevel =
              (int16_t)std::roundf(getActiveModDelta(instr, modDestSubLevel) * 100.0f);
            const int16_t noiseLevel =
              (int16_t)std::roundf(getActiveModDelta(instr, modDestNoiseLevel) * 100.0f);
            const int16_t oscBLevel =
              (int16_t)std::roundf(getActiveModDelta(instr, modDestOscBLevel) * 100.0f);
            for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
            {
                playMemPtr[channel]->setSynthPwMod(pw);
                playMemPtr[channel]->setSynthTimbreBMod(timbreB);
                playMemPtr[channel]->setSynthOscBPitchModCents(oscBPitchCents);
                playMemPtr[channel]->setSynthDetuneMod(detune);
                playMemPtr[channel]->setSynthSubLevelMod(subLevel);
                playMemPtr[channel]->setSynthNoiseLevelMod(noiseLevel);
                playMemPtr[channel]->setSynthOscBLevelMod(oscBLevel);
            }
        }
        else
        {
            const uint32_t maxWin =
              (instr.sample.wavetableWindowNumber > 0) ? (instr.sample.wavetableWindowNumber - 1) : 0;
            if ((int)(localWtPos + localWtPosisionMod) < 0)
            {
                localWtPos = 0;
            }
            else if ((int)(localWtPos + localWtPosisionMod) > (int)maxWin)
            {
                localWtPos = maxWin;
            }
            else
            {
                localWtPos += localWtPosisionMod;
            }

            for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
            {
                playMemPtr[channel]->setWavetableWindow(localWtPos);
                playMemPtr[channel]->setForcedWavetableWindow(localWtPos);
            }
        }
    }
}
inline void playerEngine::handleUpdateRefreshGranPos()
{
    if (refreshRegister & GRANULAR_POS_SEND_MASK)
    {
        refreshRegister &= (~GRANULAR_POS_SEND_MASK);
        const auto& instr = mtProject.instrument[currentInstrument_idx];
        if (instr.playMode != playModeGranular)
        {
            return;
        }
        if ((!instr.envelope[envGranPos].enable) &&
            (!trackControlParameter[(int)controlType::sequencerMode]
                                   [(int)parameterList::lfoGranularPosition]) &&
            (!trackControlParameter[(int)controlType::sequencerMode2]
                                   [(int)parameterList::lfoGranularPosition]) &&
            (!trackControlParameter[(int)controlType::performanceMode]
                                   [(int)parameterList::lfoGranularPosition]))
        {
            currentEnvelopeModification[envGranPos] = 0;
        }

        int32_t localGranPosisionMod = (int32_t)std::roundf(
          (currentEnvelopeModification[envGranPos] + getActiveModDelta(instr, modDestGranPos)) *
          (float)MAX_16BIT);

        uint16_t localGranPosition = 0;
        if (trackControlParameter[(int)controlType::performanceMode]
                                 [(int)parameterList::granularPosition])
        {
            localGranPosition = currentPerformanceValues.granularPosition;
        }
        else if (trackControlParameter[(int)controlType::sequencerMode]
                                      [(int)parameterList::granularPosition] ||
                 trackControlParameter[(int)controlType::sequencerMode2]
                                      [(int)parameterList::granularPosition])
        {
            localGranPosition = currentSeqModValues.granularPosition;
        }
        else
        {
            localGranPosition = instr.granular.currentPosition;
        }

        if ((int)(localGranPosition + localGranPosisionMod) < 0)
            localGranPosition = 0;
        else if ((int)(localGranPosition + localGranPosisionMod) >= MAX_16BIT)
            localGranPosition = MAX_16BIT;
        else
            localGranPosition += localGranPosisionMod;

        modGranularPosition(localGranPosition);
    }
}
inline void playerEngine::handleUpdateRefreshGranLen()
{
    if (refreshRegister & GRANULAR_LEN_SEND_MASK)
    {
        refreshRegister &= (~GRANULAR_LEN_SEND_MASK);
        const auto& instr = mtProject.instrument[currentInstrument_idx];
        if (instr.playMode != playModeGranular)
        {
            return;
        }
        if (!instr.isGranLenEnvEnabled())
        {
            currentEnvelopeModification[envGranLen] = 0;
        }

        uint32_t sampleLen = instr.sample.length;
        int32_t maxGrainLen = (sampleLen > 0 && sampleLen < MAX_GRANULAR_LENGTH)
                                ? static_cast<int32_t>(sampleLen)
                                : static_cast<int32_t>(MAX_GRANULAR_LENGTH);
        if (maxGrainLen < MIN_GRANULAR_LENGTH)
            maxGrainLen = MIN_GRANULAR_LENGTH;

        int32_t localGranLenMod = static_cast<int32_t>(std::roundf(
          (currentEnvelopeModification[envGranLen] + getActiveModDelta(instr, modDestGranLen)) *
          (float)(maxGrainLen - MIN_GRANULAR_LENGTH)));

        int32_t localGranLength = 0;
        if (trackControlParameter[(int)controlType::sequencerMode]
                                 [(int)parameterList::granularLength] ||
            trackControlParameter[(int)controlType::sequencerMode2]
                                 [(int)parameterList::granularLength])
        {
            localGranLength = currentSeqModValues.granularLength;
        }
        else
        {
            localGranLength = instr.granular.grainLength;
        }

        localGranLength =
          clamp<int32_t>(localGranLength + localGranLenMod, MIN_GRANULAR_LENGTH, maxGrainLen);

        modGranularGrainLength(static_cast<uint16_t>(localGranLength));
    }
}
inline void playerEngine::handleUpdateRefreshGranWave()
{
    if (refreshRegister & GRANULAR_WAVE_SEND_MASK)
    {
        refreshRegister &= (~GRANULAR_WAVE_SEND_MASK);
        for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
        {
            playMemPtr[channel]->setGranularWave(
              mtProject.instrument[currentInstrument_idx].granular.shape);
        }
    }
}
inline void playerEngine::handleUpdateRefreshGranLoop()
{
    if (refreshRegister & GRANULAR_LOOP_SEND_MASK)
    {
        refreshRegister &= (~GRANULAR_LOOP_SEND_MASK);
        for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
        {
            playMemPtr[channel]->setGranularLoopMode(
              mtProject.instrument[currentInstrument_idx].granular.type);
        }
    }
}
inline void playerEngine::handleUpdateRefreshAmpLFO()
{
    if (refreshRegister & LFO_AMP_SEND_MASK)
    {
        refreshRegister &= (~LFO_AMP_SEND_MASK);

        if ((mtProject.instrument[currentInstrument_idx].envelope[envAmp].enable) &&
            (!trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::lfoAmp]) &&
            (!trackControlParameter[(int)controlType::sequencerMode2]
                                   [(int)parameterList::lfoAmp]) &&
            (!trackControlParameter[(int)controlType::performanceMode][(int)parameterList::lfoAmp]))
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
                if (currentInstrument_idx == mtProject.values.lastUsedInstrument)
                {
                    setSyncParamsLFO(envAmp);
                }
                bool isRandom =
                  (curAmpLoop == 1) &&
                  (mtProject.instrument[currentInstrument_idx].lfo[envAmp].shape == lfoShapeRandom);

                for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
                {
                    playMemPtr[channel]->envelopeSetIsRandom(isRandom);
                }
            }
            else
            {
                initEnvelopesParameters(
                  envAmp, &mtProject.instrument[currentInstrument_idx].envelope[envAmp]);
                for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
                {
                    playMemPtr[channel]->envelopeSetIsRandom(false);
                }
            }
        }
    }
}
inline void playerEngine::handleUpdateRefreshCutoffLFO()
{
    if (refreshRegister & LFO_FILTER_SEND_MASK)
    {
        refreshRegister &= (~LFO_FILTER_SEND_MASK);

        if (mtProject.instrument[currentInstrument_idx].filterEnable)
        {
            if ((mtProject.instrument[currentInstrument_idx].envelope[envCutoff].enable) &&
                (!trackControlParameter[(int)controlType::sequencerMode]
                                       [(int)parameterList::lfoCutoff]) &&
                (!trackControlParameter[(int)controlType::sequencerMode2]
                                       [(int)parameterList::lfoCutoff]) &&
                (!trackControlParameter[(int)controlType::performanceMode]
                                       [(int)parameterList::lfoCutoff]))
            {
                uint8_t curCutoffLoop =
                  mtProject.instrument[currentInstrument_idx].envelope[envCutoff].loop;
                if (curCutoffLoop == 1 || curCutoffLoop == 3)
                {
                    calcLfoBasedEnvelope(
                      &lfoBasedEnvelope[envCutoff],
                      &mtProject.instrument[currentInstrument_idx].lfo[envCutoff],
                      mtProject.instrument[currentInstrument_idx].lfo[envCutoff].speed,
                      0,
                      curCutoffLoop);
                    initEnvelopesParameters(envCutoff, &lfoBasedEnvelope[envCutoff]);
                    if (currentInstrument_idx == mtProject.values.lastUsedInstrument)
                    {
                        setSyncParamsLFO(envCutoff);
                    }

                    bool isRandom =
                      (curCutoffLoop == 1) &&
                      (mtProject.instrument[currentInstrument_idx].lfo[envCutoff].shape ==
                       lfoShapeRandom);
                    envelopePtr[envCutoff]->setIsRandom(isRandom);
                }
                else
                {
                    initEnvelopesParameters(
                      envCutoff, &mtProject.instrument[currentInstrument_idx].envelope[envCutoff]);
                    envelopePtr[envCutoff]->setIsRandom(false);
                }
            }
        }
    }
}
inline void playerEngine::handleUpdateRefreshWtPosLFO()
{
    if (refreshRegister & LFO_WT_POS_SEND_MASK)
    {
        refreshRegister &= (~LFO_WT_POS_SEND_MASK);

        const bool isGranular =
          (mtProject.instrument[currentInstrument_idx].sample.type == mtSampleTypeWaveFile) &&
          (mtProject.instrument[currentInstrument_idx].playMode == playModeGranular);
        const bool isWavetable =
          (mtProject.instrument[currentInstrument_idx].sample.type == mtSampleTypeWavetable) ||
          (mtProject.instrument[currentInstrument_idx].playMode == playModeWavetable);
        const bool isSynth =
          (mtProject.instrument[currentInstrument_idx].playMode == playModeSynth);

        if (isWavetable || isGranular || isSynth)
        {
            if ((mtProject.instrument[currentInstrument_idx].isEnvEnabled(envWtPos)) &&
                (!trackControlParameter[(int)controlType::sequencerMode]
                                       [(int)parameterList::lfoWavetablePosition]) &&
                (!trackControlParameter[(int)controlType::sequencerMode2]
                                       [(int)parameterList::lfoWavetablePosition]) &&
                (!trackControlParameter[(int)controlType::performanceMode]
                                       [(int)parameterList::lfoWavetablePosition]))
            {
                uint8_t curWtLoop =
                  mtProject.instrument[currentInstrument_idx].envelope[envWtPos].loop;
                if (curWtLoop == 1 || curWtLoop == 3)
                {
                    calcLfoBasedEnvelope(
                      &lfoBasedEnvelope[envWtPos],
                      &mtProject.instrument[currentInstrument_idx].lfo[envWtPos],
                      mtProject.instrument[currentInstrument_idx].lfo[envWtPos].speed,
                      0,
                      curWtLoop);
                    initEnvelopesParameters(envWtPos, &lfoBasedEnvelope[envWtPos]);
                    if (currentInstrument_idx == mtProject.values.lastUsedInstrument)
                    {
                        setSyncParamsLFO(envWtPos);
                    }

                    bool isRandom =
                      (curWtLoop == 1) &&
                      (mtProject.instrument[currentInstrument_idx].lfo[envWtPos].shape ==
                       lfoShapeRandom);
                    envelopePtr[envWtPos]->setIsRandom(isRandom);
                }
                else
                {
                    initEnvelopesParameters(
                      envWtPos, &mtProject.instrument[currentInstrument_idx].envelope[envWtPos]);
                    envelopePtr[envWtPos]->setIsRandom(false);
                }
            }
            else if (isGranular)
            {
                currentEnvelopeModification[envGranLen] = 0;
                refreshRegister |= GRANULAR_LEN_SEND_MASK;
            }
            else if (isWavetable || isSynth)
            {
                currentEnvelopeModification[envWtPos] = 0;
                refreshRegister |= WT_POS_SEND_MASK;
            }
        }
    }
}
inline void playerEngine::handleUpdateRefreshGranPosLFO()
{
    if (refreshRegister & LFO_GRAN_POS_SEND_MASK)
    {
        refreshRegister &= (~LFO_GRAN_POS_SEND_MASK);
        auto& instr = mtProject.instrument[currentInstrument_idx];
        const bool isGranular =
          (instr.sample.type == mtSampleTypeWaveFile) && (instr.playMode == playModeGranular);
        const bool isModActive =
          (instr.playMode != playModeGranular) && (instr.modDestination != modDestOff);

        if (isGranular || isModActive)
        {
            if ((instr.isEnvEnabled(envGranPos)) &&
                (!trackControlParameter[(int)controlType::sequencerMode]
                                       [(int)parameterList::lfoGranularPosition]) &&
                (!trackControlParameter[(int)controlType::sequencerMode2]
                                       [(int)parameterList::lfoGranularPosition]) &&
                (!trackControlParameter[(int)controlType::performanceMode]
                                       [(int)parameterList::lfoGranularPosition]))
            {
                uint8_t curGranLoop = instr.envelope[envGranPos].loop;
                if (curGranLoop == 1 || curGranLoop == 3)
                {
                    calcLfoBasedEnvelope(&lfoBasedEnvelope[envGranPos],
                                         &instr.lfo[envGranPos],
                                         instr.lfo[envGranPos].speed,
                                         0,
                                         curGranLoop);
                    initEnvelopesParameters(envGranPos, &lfoBasedEnvelope[envGranPos]);
                    if (currentInstrument_idx == mtProject.values.lastUsedInstrument)
                    {
                        setSyncParamsLFO(envGranPos);
                    }

                    bool isRandom =
                      (curGranLoop == 1) && (instr.lfo[envGranPos].shape == lfoShapeRandom);
                    envelopePtr[envGranPos]->setIsRandom(isRandom);
                }
                else
                {
                    initEnvelopesParameters(envGranPos, &instr.envelope[envGranPos]);
                    envelopePtr[envGranPos]->setIsRandom(false);
                }
            }
            else if (isModActive)
            {
                currentEnvelopeModification[envGranPos] = 0;
            }
        }
        else if (instr.playMode != playModeGranular)
        {
            currentEnvelopeModification[envGranPos] = 0;
        }
        if (!isGranular)
        {
            refreshRegister |= kAllModRefreshMask;
        }
    }
}
inline void playerEngine::handleUpdateRefreshMod2LFO()
{
    if (refreshRegister & LFO_MOD2_SEND_MASK)
    {
        refreshRegister &= (~LFO_MOD2_SEND_MASK);
        auto& instr = mtProject.instrument[currentInstrument_idx];
        for (uint8_t envSlot = envMod2; envSlot < envEngineMax; envSlot++)
        {
            if (instr.isEnvEnabled(envSlot))
            {
                const auto& envCfg = instr.getEnv(envSlot);
                const auto& lfoCfg = instr.getLfo(envSlot);
                uint8_t curLoop = envCfg.loop;
                if (curLoop == 1 || curLoop == 3)
                {
                    calcLfoBasedEnvelope(&lfoBasedEnvelope[envSlot],
                                         const_cast<strInstrument::strEnvBasedLfo*>(&lfoCfg),
                                         lfoCfg.speed,
                                         0,
                                         curLoop);
                    initEnvelopesParameters(envSlot, &lfoBasedEnvelope[envSlot]);
                    if (currentInstrument_idx == mtProject.values.lastUsedInstrument)
                    {
                        setSyncParamsLFO(envSlot);
                    }

                    bool isRandom = (curLoop == 1) && (lfoCfg.shape == lfoShapeRandom);
                    envelopePtr[envSlot]->setIsRandom(isRandom);
                }
                else
                {
                    initEnvelopesParameters(envSlot, const_cast<envelopeGenerator::strEnv*>(&envCfg));
                    envelopePtr[envSlot]->setIsRandom(false);
                }
            }
            else
            {
                currentEnvelopeModification[envSlot] = 0;
                envelopePtr[envSlot]->stop();
            }
        }
        // Refresh all mod-targetable voice params so changing/disabling pooled modulators clears stale delta
        refreshRegister |= kAllModRefreshMask;
    }
}
inline void playerEngine::handleUpdateRefreshPanningLFO()
{
    if (refreshRegister & LFO_PANNING_SEND_MASK)
    {
        refreshRegister &= (~LFO_PANNING_SEND_MASK);

        if ((mtProject.instrument[currentInstrument_idx].envelope[envPan].enable) &&
            (!trackControlParameter[(int)controlType::sequencerMode]
                                   [(int)parameterList::lfoPanning]) &&
            (!trackControlParameter[(int)controlType::sequencerMode2]
                                   [(int)parameterList::lfoPanning]) &&
            (!trackControlParameter[(int)controlType::performanceMode]
                                   [(int)parameterList::lfoPanning]))
        {
            uint8_t curPanLoop = mtProject.instrument[currentInstrument_idx].envelope[envPan].loop;
            if (curPanLoop == 1 || curPanLoop == 3)
            {
                calcLfoBasedEnvelope(&lfoBasedEnvelope[envPan],
                                     &mtProject.instrument[currentInstrument_idx].lfo[envPan],
                                     mtProject.instrument[currentInstrument_idx].lfo[envPan].speed,
                                     0,
                                     curPanLoop);
                initEnvelopesParameters(envPan, &lfoBasedEnvelope[envPan]);
                if (currentInstrument_idx == mtProject.values.lastUsedInstrument)
                {
                    setSyncParamsLFO(envPan);
                }

                bool isRandom =
                  (curPanLoop == 1) &&
                  (mtProject.instrument[currentInstrument_idx].lfo[envPan].shape == lfoShapeRandom);
                envelopePtr[envPan]->setIsRandom(isRandom);
            }
            else
            {
                initEnvelopesParameters(
                  envPan, &mtProject.instrument[currentInstrument_idx].envelope[envPan]);
                envelopePtr[envPan]->setIsRandom(false);
            }
        }
    }
}

inline void playerEngine::handleUpdateRefreshFinetuneLFO()
{
    if (refreshRegister & LFO_FINETUNE_SEND_MASK)
    {
        refreshRegister &= (~LFO_FINETUNE_SEND_MASK);

        if ((mtProject.instrument[currentInstrument_idx].envelope[envFinetune].enable) &&
            (!trackControlParameter[(int)controlType::sequencerMode]
                                   [(int)parameterList::lfoFinetune]) &&
            (!trackControlParameter[(int)controlType::sequencerMode2]
                                   [(int)parameterList::lfoFinetune]) &&
            (!trackControlParameter[(int)controlType::performanceMode]
                                   [(int)parameterList::lfoFinetune]))
        {
            uint8_t curFineLoop =
              mtProject.instrument[currentInstrument_idx].envelope[envFinetune].loop;
            if (curFineLoop == 1 || curFineLoop == 3)
            {
                calcLfoBasedEnvelope(
                  &lfoBasedEnvelope[envFinetune],
                  &mtProject.instrument[currentInstrument_idx].lfo[envFinetune],
                  mtProject.instrument[currentInstrument_idx].lfo[envFinetune].speed,
                  0,
                  curFineLoop);
                initEnvelopesParameters(envFinetune, &lfoBasedEnvelope[envFinetune]);
                if (currentInstrument_idx == mtProject.values.lastUsedInstrument)
                {
                    setSyncParamsLFO(envFinetune);
                }
                bool isRandom =
                  (curFineLoop == 1) &&
                  (mtProject.instrument[currentInstrument_idx].lfo[envFinetune].shape ==
                   lfoShapeRandom);
                envelopePtr[envFinetune]->setIsRandom(isRandom);
            }
            else
            {
                initEnvelopesParameters(
                  envFinetune, &mtProject.instrument[currentInstrument_idx].envelope[envFinetune]);
                envelopePtr[envFinetune]->setIsRandom(false);
            }
        }
    }
}
