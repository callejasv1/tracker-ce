/*
 * synth_osc.h - Lightweight fixed-point subtractive synth oscillator for the
 * Polyend Tracker "Synth" play mode.
 *
 * Copyright (c) 2026 Polyend Tracker CE contributors. MIT License.
 *
 * Design notes:
 *  - Pure integer DSP (uint32 phase accumulators, int32 math, int16 output) so it
 *    runs cheaply on the original Tracker's Cortex-M4 @ 180 MHz and matches the
 *    int16 audio_block_t voice path without float<->int conversions.
 *  - Saw / pulse / sub / sync discontinuities are band-limited with a 2-sample
 *    polynomial BLEP applied only on the sample where the discontinuity occurs,
 *    with one sample of output latency ("this" / "next" sample accumulation).
 *    This technique is inspired by Mutable Instruments Braids
 *    (analog_oscillator.cc, Copyright (c) 2012 Emilie Gillet, MIT License).
 *  - No dependencies on the Teensy audio library: header-only and host-testable.
 */

#ifndef SYNTH_OSC_H_
#define SYNTH_OSC_H_

#include <stdint.h>

namespace synthosc
{

enum Mode : uint8_t
{
    kModeClassic = 0, // Osc A + Osc B (detuned) + Sub + Noise
    kModeSync,        // Saw Osc B hard-synced to Osc A (sync lead)
    kModeCSaw,        // Notched "CS" saw (PW = notch width) + Osc B
    kModeSwarm,       // 3 detuned saws (light supersaw)
    kModeFM,          // 2-Op Phase Modulation (Osc B -> Osc A phase) + Sub + Noise
    kModeRing,        // Band-limited Ring Modulator (Osc A * Osc B) + Sub + Noise
    kModeCount
};

enum Wave : uint8_t
{
    kWaveSaw = 0,
    kWavePulse,
    kWaveTri,
    kWaveCount
};

// User-facing parameters (stored per instrument, UI units).
struct Params
{
    uint8_t mode;   // Mode
    uint8_t waveA;  // Wave
    uint8_t waveB;  // Wave
    int8_t semiB;   // -24..+24 semitones (Osc B / sync ratio)
    uint8_t detune; // 0..100 cents (Classic/CSaw: Osc B fine; Swarm: spread; Sync: fine)
    uint8_t levelB; // 0..100
    uint8_t sub;    // 0..100
    uint8_t noise;  // 0..100
    uint8_t pw;     // 0..100 (Pulse: 50%..5% duty, CSaw: notch width, FM: timbre)
    uint8_t initialized; // kInitMagic once initialized
};

static constexpr uint8_t kMaxLevel = 100;
static constexpr int8_t kMinSemi = -24;
static constexpr int8_t kMaxSemi = 24;
static constexpr uint8_t kMaxDetune = 100;
static constexpr uint8_t kMaxPw = 100;
static constexpr uint8_t kInitMagic = 0xA5;

// Per-block derived values (computed once per 128-sample block).
struct BlockParams
{
    uint32_t incA;
    uint32_t incB;
    uint32_t incC;    // Swarm third saw
    uint32_t pwPhase; // pulse duty / CSaw notch end as phase threshold
    uint32_t csawK;   // CSaw ramp scale (Q16)
    int32_t gainA;    // Q12 gains
    int32_t gainB;
    int32_t gainSub;
    int32_t gainNoise;
    uint8_t fmPw;     // Clamped (pw + pwMod + 25) for FM brightness scaling (25..125)
};

// Maps a 0..100 level to a Q12 gain (4096 = unity) with a squared curve for
// a more natural fader feel. Max = 4096.
static inline int32_t levelToGainQ12(uint8_t level)
{
    if (level > kMaxLevel)
        level = kMaxLevel;
    return (int32_t)((uint32_t)level * level * 4096u / (kMaxLevel * kMaxLevel));
}

// Converts pw (0..100, possibly modulated) to a phase threshold.
// Pulse: 50% .. 5% duty. CSaw: notch width 2% .. 42%.
static inline uint32_t pwToPhase(int32_t pw, uint8_t mode)
{
    if (pw < 0)
        pw = 0;
    if (pw > kMaxPw)
        pw = kMaxPw;
    // 2^32 / 1000 = 4294967.296
    constexpr uint32_t kPerMille = 4294967u;
    if (mode == kModeCSaw)
        return kPerMille * (uint32_t)(20 + pw * 4); // 2% .. 42%
    return kPerMille * (uint32_t)(500 - (pw * 9) / 2); // 50% .. 5%
}

class Oscillator
{
  public:
    void reset()
    {
        for (int i = 0; i < 3; i++)
        {
            phase[i] = 0x55555555u * (uint32_t)i; // spread swarm phases
            next[i] = 0;
        }
        subNext = 0;
        subHigh = 0;
        noise = 0x1234567u;
    }

    // Renders n samples (n must be > 0) into out.
    void render(uint8_t mode, uint8_t waveA, uint8_t waveB, const BlockParams& bp, int16_t* out, int n)
    {
        switch (mode)
        {
            case kModeSync: renderSync(bp, out, n); break;
            case kModeCSaw: renderClassicDispatchB<kWaveCount>(waveB, bp, out, n); break;
            case kModeSwarm: renderSwarm(bp, out, n); break;
            case kModeFM: renderFM(waveA, waveB, bp, out, n); break;
            case kModeRing: renderRingDispatchA(waveA, waveB, bp, out, n); break;
            default: renderClassicDispatchA(waveA, waveB, bp, out, n); break;
        }
    }

  private:
    uint32_t phase[3];
    int32_t next[3];
    int32_t subNext;
    uint32_t subHigh;
    uint32_t noise;

    // ---------------------------------------------------------------- helpers
    static inline int32_t sat16(int32_t x)
    {
#if defined(__ARM_ARCH_7EM__)
        int32_t r;
        asm("ssat %0, #16, %1" : "=r"(r) : "r"(x));
        return r;
#else
        return x > 32767 ? 32767 : (x < -32768 ? -32768 : x);
#endif
    }

    // Fraction (Q16, 0..65535) of a sample elapsed since a discontinuity that
    // happened `over` phase units ago, given phase increment `inc`.
    static inline uint32_t blepT(uint32_t over, uint32_t inc)
    {
        uint32_t t = over / ((inc >> 16) | 1u);
        return t > 65535u ? 65535u : t;
    }
    // BLEP residuals for a unit step of 65536 (h/2 = 32768 at the boundary).
    static inline int32_t blepPre(uint32_t t) { return (int32_t)((t * t) >> 17); }
    static inline int32_t blepPost(uint32_t t)
    {
        uint32_t u = 65535u - t;
        return (int32_t)((u * u) >> 17);
    }
    // Scaled residuals for an arbitrary step height h (0..65535).
    static inline int32_t blepPreH(uint32_t t, uint32_t h) { return (int32_t)((h * ((t * t) >> 16)) >> 17); }
    static inline int32_t blepPostH(uint32_t t, uint32_t h)
    {
        uint32_t u = 65535u - t;
        return (int32_t)((h * ((u * u) >> 16)) >> 17);
    }

    inline int32_t nextNoise()
    {
        noise = noise * 1664525u + 1013904223u;
        return (int32_t)noise >> 16;
    }

    // Single oscillator steps. Return a sample in [-32768, 32767] delayed by one
    // sample (BLEP look-ahead). `wrapT` receives the BLEP fraction on wrap, or
    // 0xFFFFFFFF when no wrap occurred (used for Sub osc).
    template <Wave W>
    inline int32_t step(int idx, uint32_t inc, uint32_t pw, uint32_t& wrapT)
    {
        int32_t thisS = next[idx];
        int32_t nextS = 0;
        uint32_t p = phase[idx] + inc;
        phase[idx] = p;
        wrapT = 0xFFFFFFFFu;

        if (W == kWaveSaw)
        {
            if (p < inc)
            {
                uint32_t t = blepT(p, inc);
                wrapT = t;
                thisS -= blepPre(t);
                nextS += blepPost(t);
            }
            nextS += (int32_t)(p >> 16);
        }
        else if (W == kWavePulse)
        {
            if (p < inc)
            {
                uint32_t t = blepT(p, inc);
                wrapT = t;
                thisS += blepPre(t);
                nextS -= blepPost(t);
            }
            uint32_t overPw = p - pw;
            if (overPw < inc)
            {
                uint32_t t = blepT(overPw, inc);
                thisS -= blepPre(t);
                nextS += blepPost(t);
            }
            nextS += (p < pw) ? 65535 : 0;
        }
        else // Triangle: naive, very low aliasing
        {
            if (p < inc)
                wrapT = blepT(p, inc);
            uint32_t v = p >> 15; // 0..131071
            nextS += (int32_t)(v < 65536u ? v : 131071u - v);
        }

        next[idx] = nextS;
        return thisS - 32768;
    }

    // Square sub-oscillator one octave below Osc A, band-limited using Osc A's
    // wrap fraction. Toggles every Osc A cycle.
    inline int32_t stepSub(uint32_t wrapT)
    {
        int32_t thisS = subNext;
        int32_t nextS = 0;
        if (wrapT != 0xFFFFFFFFu)
        {
            subHigh ^= 1u;
            if (subHigh)
            {
                thisS += blepPre(wrapT);
                nextS -= blepPost(wrapT);
            }
            else
            {
                thisS -= blepPre(wrapT);
                nextS += blepPost(wrapT);
            }
        }
        nextS += subHigh ? 65535 : 0;
        subNext = nextS;
        return thisS - 32768;
    }

    // ------------------------------------------------------------ mode: classic
    void renderClassicDispatchA(uint8_t waveA, uint8_t waveB, const BlockParams& bp, int16_t* out, int n)
    {
        switch (waveA)
        {
            case kWavePulse: renderClassicDispatchB<kWavePulse>(waveB, bp, out, n); break;
            case kWaveTri: renderClassicDispatchB<kWaveTri>(waveB, bp, out, n); break;
            default: renderClassicDispatchB<kWaveSaw>(waveB, bp, out, n); break;
        }
    }

    // A == kWaveCount selects the CSaw oscillator for Osc A.
    template <uint8_t A>
    void renderClassicDispatchB(uint8_t waveB, const BlockParams& bp, int16_t* out, int n)
    {
        switch (waveB)
        {
            case kWavePulse: renderClassic<A, kWavePulse>(bp, out, n); break;
            case kWaveTri: renderClassic<A, kWaveTri>(bp, out, n); break;
            default: renderClassic<A, kWaveSaw>(bp, out, n); break;
        }
    }

    // CSaw: 0 during the notch [0, pw), then a ramp from mid-level to the top.
    inline int32_t stepCSaw(uint32_t inc, uint32_t pw, uint32_t k, uint32_t& wrapT)
    {
        int32_t thisS = next[0];
        int32_t nextS = 0;
        uint32_t p = phase[0] + inc;
        phase[0] = p;
        wrapT = 0xFFFFFFFFu;
        if (p < inc) // top (65535) -> notch (0)
        {
            uint32_t t = blepT(p, inc);
            wrapT = t;
            thisS -= blepPre(t);
            nextS += blepPost(t);
        }
        uint32_t overPw = p - pw;
        if (overPw < inc) // notch (0) -> mid (32768)
        {
            uint32_t t = blepT(overPw, inc);
            thisS += blepPre(t) >> 1;
            nextS -= blepPost(t) >> 1;
        }
        if (p >= pw)
            nextS += 32768 + (int32_t)((((p - pw) >> 16) * k) >> 16);
        next[0] = nextS;
        return thisS - 32768;
    }

    template <uint8_t A, Wave B>
    void renderClassic(const BlockParams& bp, int16_t* out, int n)
    {
        const uint32_t incA = bp.incA, incB = bp.incB, pw = bp.pwPhase, k = bp.csawK;
        const int32_t gA = bp.gainA, gB = bp.gainB, gS = bp.gainSub, gN = bp.gainNoise;
        uint32_t wrapA, wrapB;
        do
        {
            int32_t a;
            if (A == kWaveCount)
                a = stepCSaw(incA, pw, k, wrapA);
            else
                a = step<(Wave)A>(0, incA, pw, wrapA);
            int32_t acc = a * gA;
            if (gB)
                acc += step<B>(1, incB, pw, wrapB) * gB;
            int32_t s = stepSub(wrapA);
            acc += s * gS;
            if (gN)
                acc += nextNoise() * gN;
            *out++ = (int16_t)sat16(acc >> 13);
        } while (--n);
    }

    // --------------------------------------------------------------- mode: sync
    // Osc A (master, silent) resets saw Osc B (slave) on every cycle.
    void renderSync(const BlockParams& bp, int16_t* out, int n)
    {
        const uint32_t incA = bp.incA, incB = bp.incB;
        const int32_t gS = bp.gainSub, gN = bp.gainNoise;
        uint32_t pA = phase[0];
        do
        {
            // Master
            pA += incA;
            uint32_t wrapA = 0xFFFFFFFFu;
            if (pA < incA)
                wrapA = blepT(pA, incA);

            // Slave saw with hard reset
            int32_t thisS = next[1];
            int32_t nextS = 0;
            uint32_t pB = phase[1];
            if (wrapA != 0xFFFFFFFFu)
            {
                // Slave phase at the reset instant and the step height there.
                uint32_t frac = 65535u - wrapA; // portion of this step before reset
                uint32_t atReset = pB + (uint32_t)(((uint64_t)incB * frac) >> 16);
                uint32_t h = atReset >> 16;
                pB = (uint32_t)(((uint64_t)incB * wrapA) >> 16);
                thisS -= blepPreH(wrapA, h);
                nextS += blepPostH(wrapA, h);
            }
            else
            {
                pB += incB;
                if (pB < incB)
                {
                    uint32_t t = blepT(pB, incB);
                    thisS -= blepPre(t);
                    nextS += blepPost(t);
                }
            }
            phase[1] = pB;
            nextS += (int32_t)(pB >> 16);
            next[1] = nextS;

            int32_t acc = (thisS - 32768) * 4096;
            acc += stepSub(wrapA) * gS;
            if (gN)
                acc += nextNoise() * gN;
            *out++ = (int16_t)sat16(acc >> 13);
        } while (--n);
        phase[0] = pA;
    }

    // -------------------------------------------------------------- mode: swarm
    void renderSwarm(const BlockParams& bp, int16_t* out, int n)
    {
        const uint32_t incA = bp.incA, incB = bp.incB, incC = bp.incC;
        const int32_t gS = bp.gainSub, gN = bp.gainNoise;
        constexpr int32_t kSawGain = 2730; // ~2/3 each
        uint32_t wrapA, dummy;
        do
        {
            int32_t acc = step<kWaveSaw>(0, incA, 0, wrapA) * kSawGain;
            acc += step<kWaveSaw>(1, incB, 0, dummy) * kSawGain;
            acc += step<kWaveSaw>(2, incC, 0, dummy) * kSawGain;
            acc += stepSub(wrapA) * gS;
            if (gN)
                acc += nextNoise() * gN;
            *out++ = (int16_t)sat16(acc >> 13);
        } while (--n);
    }

    // ---------------------------------------------------------------- mode: FM
    // Fast integer parabolic sine approximation: phase (0..2^32-1) -> [-32767, +32767]
    static inline int32_t sinParabolic(uint32_t p)
    {
        int32_t sign = (p & 0x80000000u) ? -1 : 1;
        int32_t u = (int32_t)((p >> 16) & 0x7FFFu) - 16384; // [-16384, +16383]
        int32_t q = (268435456 - (u * u)) >> 13;            // [0, 32768]
        int32_t corr = (((q * (32768 - q)) >> 15) * 29) >> 7;
        int32_t y = q - corr;
        if (y > 32767)
            y = 32767;
        return sign * y;
    }

    static inline int32_t evalPhaseWave(uint8_t wave, uint32_t p, uint32_t pw)
    {
        if (wave == kWaveSaw)
            return (int32_t)(p >> 16) - 32768;
        if (wave == kWavePulse)
            return (p < pw) ? 32767 : -32768;
        return sinParabolic(p); // Triangle selects clean FM sine wave
    }

    void renderFM(uint8_t waveA, uint8_t waveB, const BlockParams& bp, int16_t* out, int n)
    {
        const uint32_t incA = bp.incA, incB = bp.incB, pw = bp.pwPhase;
        const int32_t gS = bp.gainSub, gN = bp.gainNoise;
        const int32_t fmDepth = (bp.gainB * (int32_t)bp.fmPw) >> 7; // [0..4000]
        uint32_t pA = phase[0];
        uint32_t pB = phase[1];
        do
        {
            pA += incA;
            pB += incB;
            uint32_t wrapA = (pA < incA) ? blepT(pA, incA) : 0xFFFFFFFFu;
            int32_t modVal = evalPhaseWave(waveB, pB, pw);
            uint32_t modPhase = pA + (static_cast<uint32_t>(modVal * fmDepth) << 5);
            int32_t carVal = evalPhaseWave(waveA, modPhase, pw);
            int32_t acc = carVal * 4096;
            acc += stepSub(wrapA) * gS;
            if (gN)
                acc += nextNoise() * gN;
            *out++ = (int16_t)sat16(acc >> 13);
        } while (--n);
        phase[0] = pA;
        phase[1] = pB;
    }

    // -------------------------------------------------------------- mode: Ring
    void renderRingDispatchA(uint8_t waveA, uint8_t waveB, const BlockParams& bp, int16_t* out, int n)
    {
        switch (waveA)
        {
            case kWavePulse: renderRingDispatchB<kWavePulse>(waveB, bp, out, n); break;
            case kWaveTri: renderRingDispatchB<kWaveTri>(waveB, bp, out, n); break;
            default: renderRingDispatchB<kWaveSaw>(waveB, bp, out, n); break;
        }
    }

    template <Wave A>
    void renderRingDispatchB(uint8_t waveB, const BlockParams& bp, int16_t* out, int n)
    {
        switch (waveB)
        {
            case kWavePulse: renderRing<A, kWavePulse>(bp, out, n); break;
            case kWaveTri: renderRing<A, kWaveTri>(bp, out, n); break;
            default: renderRing<A, kWaveSaw>(bp, out, n); break;
        }
    }

    template <Wave A, Wave B>
    void renderRing(const BlockParams& bp, int16_t* out, int n)
    {
        const uint32_t incA = bp.incA, incB = bp.incB, pw = bp.pwPhase;
        const int32_t gB = bp.gainB, gS = bp.gainSub, gN = bp.gainNoise;
        const int32_t invGB = 4096 - gB;
        uint32_t wrapA, wrapB;
        do
        {
            int32_t a = step<A>(0, incA, pw, wrapA);
            int32_t b = step<B>(1, incB, pw, wrapB);
            int32_t ring = (a * b) >> 15;
            int32_t mixed = (a * invGB + ring * gB) >> 12;
            int32_t acc = mixed * 4096 + stepSub(wrapA) * gS;
            if (gN)
                acc += nextNoise() * gN;
            *out++ = (int16_t)sat16(acc >> 13);
        } while (--n);
    }
};

// Pitch-ratio helper: 2^(cents/1200) for cents in [-4800, 4800], float.
// Called only when parameters change (cached by the caller).
static inline float centsToRatio(int32_t cents)
{
    // 2^(x) via exp2 split: integer octaves + polynomial for the fraction.
    float x = (float)cents / 1200.0f;
    int32_t oct = (int32_t)(x >= 0 ? x : x - 1.0f);
    float f = x - (float)oct; // [0, 1)
    // 2^f, minimax-ish cubic (max err ~1e-4, ~0.2 cents)
    float r = 1.0f + f * (0.6951786f + f * (0.2261649f + f * 0.0782818f));
    if (oct >= 0)
        r *= (float)(1u << oct);
    else
        r /= (float)(1u << (-oct));
    return r;
}

// Builds per-block parameters. `incA` is Osc A's phase increment.
// `pwMod` is an additive modulation of the pw parameter in UI units (-100..100).
// ratioB / ratioC are cached pitch ratios (see centsToRatio).
static inline void buildBlockParams(const Params& p,
                                    uint32_t incA,
                                    float ratioB,
                                    float ratioC,
                                    int32_t pwMod,
                                    BlockParams& bp)
{
    constexpr uint32_t kMinInc = 1u << 17;
    constexpr uint32_t kMaxInc = 0x70000000u; // ~0.44 * fs
    auto clampInc = [](float v) -> uint32_t
    {
        if (v < (float)kMinInc)
            return kMinInc;
        if (v > (float)kMaxInc)
            return kMaxInc;
        return (uint32_t)v;
    };
    if (incA < kMinInc)
        incA = kMinInc;
    if (incA > kMaxInc)
        incA = kMaxInc;
    bp.incA = incA;
    bp.incB = clampInc((float)incA * ratioB);
    bp.incC = clampInc((float)incA * ratioC);
    int32_t clampedPw = (int32_t)p.pw + pwMod;
    if (clampedPw < 0)
        clampedPw = 0;
    if (clampedPw > kMaxPw)
        clampedPw = kMaxPw;
    bp.pwPhase = pwToPhase(clampedPw, p.mode);
    bp.fmPw = (uint8_t)(clampedPw + 25);
    // CSaw ramp from 32768 at pw to 65535 at the cycle end: k (Q16) = 32767 / span16 * 65536
    uint32_t span16 = (0xFFFFFFFFu - bp.pwPhase) >> 16;
    bp.csawK = span16 ? (32767u << 16) / span16 : 0;
    bp.gainA = 4096;
    bp.gainB = levelToGainQ12(p.levelB);
    bp.gainSub = levelToGainQ12(p.sub);
    bp.gainNoise = levelToGainQ12(p.noise) >> 1;
}

// Pitch ratios for Osc B / Osc C depending on mode.
static inline void computeRatios(const Params& p, float& ratioB, float& ratioC)
{
    int32_t semi = p.semiB;
    if (semi < kMinSemi)
        semi = kMinSemi;
    if (semi > kMaxSemi)
        semi = kMaxSemi;
    int32_t det = p.detune > kMaxDetune ? kMaxDetune : p.detune;
    if (p.mode == kModeSwarm)
    {
        ratioB = centsToRatio(det / 2 * 0 + det);  // +spread
        ratioC = centsToRatio(-det);               // -spread
    }
    else
    {
        ratioB = centsToRatio(semi * 100 + det);
        ratioC = 1.0f;
    }
}

} // namespace synthosc

#endif /* SYNTH_OSC_H_ */

