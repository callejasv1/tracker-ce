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
    kModeFold,        // West-Coast Wavefolder (PW = Fold Drive, Osc B Lvl = Symmetry/Cross-Fold)
    kModePD,          // Casio CZ Phase Distortion (8 CZ waveforms, PW = DCW distortion/resonance)
    kModeCount
};

enum Wave : uint8_t
{
    kWaveSaw = 0,
    kWavePulse,
    kWaveTri,
    kWaveCount
};

enum CzWave : uint8_t
{
    kCzSaw = 0,  // Cosine phase compressed to Sawtooth
    kCzSquare,   // Cosine phase held at +/-1 plateaus (Square)
    kCzPulse,    // Narrowed full cosine cycle + flat bottom (DC-compensated)
    kCzDblSine,  // Split double-cosine (morphs 1x -> 2x octave)
    kCzSawPulse, // Half-square / half-cosine hybrid (DC-compensated)
    kCzResoSaw,  // Synced resonant cosine * falling saw window
    kCzResoTri,  // Synced resonant cosine * triangle window
    kCzResoTrap, // Synced resonant cosine * trapezoid window
    kCzWaveCount
};

// Returns the valid wave index count for the given oscillator mode.
static inline uint8_t maxWaveCount(uint8_t mode)
{
    return (mode == kModePD) ? static_cast<uint8_t>(kCzWaveCount)
                             : static_cast<uint8_t>(kWaveCount);
}

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
    uint32_t incC;     // Swarm third saw
    uint32_t pwPhase;  // pulse duty / CSaw notch end as phase threshold
    uint32_t pwPhaseB; // Osc B pulse duty with Timbre B mod
    uint32_t csawK;    // CSaw ramp scale (Q16)
    int32_t gainA;     // Q12 gains
    int32_t gainB;
    int32_t gainSub;
    int32_t gainNoise;
    uint8_t fmPw;      // Clamped (pw + pwMod + 25) for FM brightness scaling (25..125)
    int16_t timbreBMod;
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
        lastFmMod = 0;
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
            case kModeFold: renderFold(waveA, waveB, bp, out, n); break;
            case kModePD: renderPD(waveA, waveB, bp, out, n); break;
            default: renderClassicDispatchA(waveA, waveB, bp, out, n); break;
        }
    }

  private:
    uint32_t phase[3];
    int32_t next[3];
    int32_t subNext;
    uint32_t subHigh;
    uint32_t noise;
    int32_t lastFmMod;

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
        const uint32_t incA = bp.incA, incB = bp.incB, pw = bp.pwPhase, pwB = bp.pwPhaseB, k = bp.csawK;
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
                acc += step<B>(1, incB, pwB, wrapB) * gB;
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
        const uint32_t incA = bp.incA, incB = bp.incB;
        uint32_t incC = bp.incC;
        if (bp.timbreBMod != 0)
        {
            int32_t delta = (int32_t)(((int64_t)incC * bp.timbreBMod) >> 11);
            int32_t adj = (int32_t)incC + delta;
            if (adj >= (1 << 17) && adj <= 0x70000000)
                incC = (uint32_t)adj;
        }
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
        const uint32_t incA = bp.incA, incB = bp.incB, pw = bp.pwPhase, pwB = bp.pwPhaseB;
        const int32_t gS = bp.gainSub, gN = bp.gainNoise;
        const int32_t fmDepth = (bp.gainB * (int32_t)bp.fmPw) >> 7; // [0..4000]
        const int32_t fbAmt = bp.timbreBMod;
        int32_t lastMod = lastFmMod;
        uint32_t pA = phase[0];
        uint32_t pB = phase[1];
        do
        {
            pA += incA;
            pB += incB;
            uint32_t wrapA = (pA < incA) ? blepT(pA, incA) : 0xFFFFFFFFu;
            uint32_t pB_fb = fbAmt ? (pB + (static_cast<uint32_t>(lastMod * fbAmt) << 8)) : pB;
            int32_t modVal = evalPhaseWave(waveB, pB_fb, pwB);
            lastMod = modVal;
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
        lastFmMod = lastMod;
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
        const uint32_t incA = bp.incA, incB = bp.incB, pw = bp.pwPhase, pwB = bp.pwPhaseB;
        const int32_t gB = bp.gainB, gS = bp.gainSub, gN = bp.gainNoise;
        const int32_t invGB = 4096 - gB;
        uint32_t wrapA, wrapB;
        do
        {
            int32_t a = step<A>(0, incA, pw, wrapA);
            int32_t b = step<B>(1, incB, pwB, wrapB);
            int32_t ring = (a * b) >> 15;
            int32_t mixed = (a * invGB + ring * gB) >> 12;
            int32_t acc = mixed * 4096 + stepSub(wrapA) * gS;
            if (gN)
                acc += nextNoise() * gN;
            *out++ = (int16_t)sat16(acc >> 13);
        } while (--n);
    }

    // -------------------------------------------------------------- mode: Fold
    // O(1) continuous integer triangle wavefolder mapping any int32 into [-32768, 32767].
    static inline int32_t foldWave(int32_t x)
    {
        uint32_t m = static_cast<uint32_t>(x + 32768) & 0x1FFFFu;
        int32_t v = (m < 65536u) ? static_cast<int32_t>(m) : static_cast<int32_t>(131071u - m);
        return v - 32768;
    }

    void renderFold(uint8_t waveA, uint8_t waveB, const BlockParams& bp, int16_t* out, int n)
    {
        const uint32_t incA = bp.incA, incB = bp.incB, pw = bp.pwPhase, pwB = bp.pwPhaseB;
        const int32_t gB = bp.gainB, gS = bp.gainSub, gN = bp.gainNoise;
        // PW drives fold intensity (1.0x .. 5.7x), Osc B Lvl adds cross-fold & symmetry bias.
        const int32_t foldQ8 = 256 + ((int32_t)(bp.fmPw - 25) * 12) + (gB >> 3);
        const int32_t bias = (gB << 1) + ((int32_t)bp.timbreBMod * 128);
        uint32_t pA = phase[0];
        uint32_t pB = phase[1];
        do
        {
            pA += incA;
            pB += incB;
            uint32_t wrapA = (pA < incA) ? blepT(pA, incA) : 0xFFFFFFFFu;
            int32_t carVal = evalPhaseWave(waveA, pA, pw);
            int32_t pre = ((carVal * foldQ8) >> 8) + bias;
            if (gB)
            {
                int32_t bVal = evalPhaseWave(waveB, pB, pwB);
                pre += (bVal * gB) >> 13;
            }
            int32_t folded = foldWave(pre);
            int32_t acc = folded * 4096;
            acc += stepSub(wrapA) * gS;
            if (gN)
                acc += nextNoise() * gN;
            *out++ = (int16_t)sat16(acc >> 13);
        } while (--n);
        phase[0] = pA;
        phase[1] = pB;
    }

    // ------------------------------------------------------ mode: Casio CZ PD
    struct CzParams
    {
        uint32_t bp;
        uint32_t slope1;
        uint32_t slope2;
        uint32_t rQ12;
        int32_t dcComp;
    };

    // -cos(2*pi*theta) = sin(2*pi*theta - pi/2) in [-32767, +32767]
    static inline int32_t czNegCos(uint32_t theta)
    {
        return sinParabolic(theta - 0x40000000u);
    }

    static inline void prepareCzParams(uint8_t czWave, uint32_t inc, uint8_t dcw, CzParams& cz)
    {
        // Minimum breakpoint width (~1 sample in 2^20 phase units) avoids ultrasonic foldover
        uint32_t minBp20 = (inc >> 12) + 1u;
        if (minBp20 > 262144u)
            minBp20 = 262144u;

        cz.bp = 0;
        cz.slope1 = 4096u;
        cz.slope2 = 4096u;
        cz.rQ12 = 4096u;
        cz.dcComp = 0;

        switch (czWave)
        {
            case kCzSquare:
            {
                uint32_t bp20 = 524288u - (uint32_t)dcw * 5000u;
                if (bp20 < minBp20)
                    bp20 = minBp20;
                cz.bp = bp20 << 12;
                cz.slope1 = 0x80000000u / bp20;
                break;
            }
            case kCzPulse:
            {
                uint32_t width20 = 1048576u - (uint32_t)dcw * 9830u;
                uint32_t minW20 = minBp20 << 1;
                if (width20 < minW20)
                    width20 = minW20;
                if (width20 > 1048576u)
                    width20 = 1048576u;
                cz.bp = (width20 == 1048576u) ? 0xFFFFFFFFu : (width20 << 12);
                cz.slope1 = (width20 == 1048576u) ? 4096u : (0xFFFFFFFFu / width20);
                cz.dcComp = (int32_t)((1048576u - width20) >> 5);
                break;
            }
            case kCzDblSine:
            {
                if (dcw == 0)
                {
                    cz.bp = 0;
                    cz.slope1 = 0;
                    cz.slope2 = 4096u;
                }
                else
                {
                    uint32_t bp20 = (uint32_t)dcw * 5242u;
                    uint32_t minW20 = minBp20 << 1;
                    if (bp20 < minW20)
                        bp20 = minW20;
                    if (bp20 > 524288u)
                        bp20 = 524288u;
                    cz.bp = bp20 << 12;
                    cz.slope1 = 0xFFFFFFFFu / bp20;
                    cz.slope2 = 0xFFFFFFFFu / (1048576u - bp20);
                }
                break;
            }
            case kCzSawPulse:
            {
                uint32_t bp20 = 524288u - (uint32_t)dcw * 5000u;
                if (bp20 < minBp20)
                    bp20 = minBp20;
                cz.bp = bp20 << 12;
                cz.slope1 = 0x80000000u / bp20;
                cz.dcComp = (int32_t)((524288u - bp20) >> 5);
                break;
            }
            case kCzResoSaw:
            case kCzResoTri:
            case kCzResoTrap:
            {
                // Sweep resonant harmonic ratio 1.0x .. 9.98x, capped below ~0.42 * fs
                uint32_t rQ12 = 4096u + (uint32_t)dcw * 368u;
                uint32_t maxRQ12 = 0x6B000000u / ((inc >> 12) | 1u);
                if (maxRQ12 < 4096u)
                    maxRQ12 = 4096u;
                if (rQ12 > maxRQ12)
                    rQ12 = maxRQ12;
                cz.rQ12 = rQ12;
                cz.dcComp = (czWave == kCzResoTrap) ? 24576 : 16384;
                if (rQ12 < 8192u && czWave != kCzResoSaw)
                {
                    uint32_t extra = (czWave == kCzResoTri) ? 6640u : 3320u;
                    cz.dcComp += (int32_t)(((8192u - rQ12) * extra) >> 12);
                }
                break;
            }
            default: // kCzSaw
            {
                uint32_t bp20 = 524288u - (uint32_t)dcw * 4997u;
                if (bp20 < minBp20)
                    bp20 = minBp20;
                cz.bp = bp20 << 12;
                cz.slope1 = 0x80000000u / bp20;
                cz.slope2 = 0x80000000u / (1048576u - bp20);
                break;
            }
        }
    }

    static inline int32_t evalCzWave(uint8_t czWave, uint32_t p, const CzParams& cz)
    {
        switch (czWave)
        {
            case kCzSquare:
            {
                uint32_t halfP = p & 0x7FFFFFFFu;
                uint32_t theta = (halfP < cz.bp) ? ((halfP >> 12) * cz.slope1) : 0x80000000u;
                if (p & 0x80000000u)
                    theta += 0x80000000u;
                return czNegCos(theta);
            }
            case kCzPulse:
            {
                uint32_t theta = (p <= cz.bp) ? ((p >> 12) * cz.slope1) : 0u;
                return czNegCos(theta) + cz.dcComp;
            }
            case kCzDblSine:
            {
                uint32_t theta =
                  (p < cz.bp) ? ((p >> 12) * cz.slope1) : (((p - cz.bp) >> 12) * cz.slope2);
                return czNegCos(theta);
            }
            case kCzSawPulse:
            {
                uint32_t theta;
                if (p < 0x80000000u)
                    theta = (p < cz.bp) ? ((p >> 12) * cz.slope1) : 0x80000000u;
                else
                    theta = p;
                return czNegCos(theta) - cz.dcComp;
            }
            case kCzResoSaw:
            case kCzResoTri:
            case kCzResoTrap:
            {
                uint32_t theta = static_cast<uint32_t>((static_cast<uint64_t>(p) * cz.rQ12) >> 12);
                int32_t carrier = 32767 - sinParabolic(theta + 0x40000000u);
                uint32_t w;
                if (czWave == kCzResoSaw)
                {
                    w = 65535u - (p >> 16);
                }
                else if (czWave == kCzResoTri)
                {
                    uint32_t hp = p >> 15;
                    w = (hp < 65536u) ? hp : (131071u - hp);
                }
                else
                {
                    w = (p < 0x80000000u) ? 65535u : ((0xFFFFFFFFu - p) >> 15);
                }
                return static_cast<int32_t>((static_cast<uint32_t>(carrier) * w) >> 16) - cz.dcComp;
            }
            default: // kCzSaw
            {
                uint32_t theta = (p < cz.bp)
                                   ? ((p >> 12) * cz.slope1)
                                   : (0x80000000u + (((p - cz.bp) >> 12) * cz.slope2));
                return czNegCos(theta);
            }
        }
    }

    void renderPD(uint8_t waveA, uint8_t waveB, const BlockParams& bp, int16_t* out, int n)
    {
        const uint32_t incA = bp.incA, incB = bp.incB;
        const int32_t gB = bp.gainB, gS = bp.gainSub, gN = bp.gainNoise;
        const uint8_t dcw = (uint8_t)(bp.fmPw - 25);
        int32_t dcwB = (int32_t)dcw + bp.timbreBMod;
        if (dcwB < 0)
            dcwB = 0;
        else if (dcwB > kMaxPw)
            dcwB = kMaxPw;

        CzParams czA, czB;
        prepareCzParams(waveA, incA, dcw, czA);
        if (gB)
            prepareCzParams(waveB, incB, (uint8_t)dcwB, czB);

        uint32_t pA = phase[0];
        uint32_t pB = phase[1];
        do
        {
            pA += incA;
            pB += incB;
            uint32_t wrapA = (pA < incA) ? blepT(pA, incA) : 0xFFFFFFFFu;
            int32_t acc = evalCzWave(waveA, pA, czA) * 4096;
            if (gB)
                acc += evalCzWave(waveB, pB, czB) * gB;
            acc += stepSub(wrapA) * gS;
            if (gN)
                acc += nextNoise() * gN;
            *out++ = (int16_t)sat16(acc >> 13);
        } while (--n);
        phase[0] = pA;
        phase[1] = pB;
    }
};

// Pitch-ratio helper: 2^(cents/1200) for cents in [-7200, 7200], float.
// Called only when parameters change (cached by the caller).
static inline float centsToRatio(int32_t cents)
{
    if (cents < -7200)
        cents = -7200;
    else if (cents > 7200)
        cents = 7200;
    // 2^(x) via exp2 split: exact integer floor octaves + polynomial for fraction.
    int32_t oct = (cents >= 0) ? (cents / 1200) : ((cents - 1199) / 1200);
    float f = (float)(cents - oct * 1200) * (1.0f / 1200.0f); // [0, 1)
    // 2^f, minimax-ish cubic (max err ~1e-4, ~0.2 cents)
    float r = 1.0f + f * (0.6951786f + f * (0.2261649f + f * 0.0782818f));
    if (oct >= 0)
        r *= (float)(1u << oct);
    else
        r /= (float)(1u << (-oct));
    return r;
}

// Builds per-block parameters. `incA` is Osc A's phase increment.
// `pwMod` / `timbreBMod` are additive modulations in UI units (-100..100).
// ratioB / ratioC are cached pitch ratios (see centsToRatio).
static inline void buildBlockParams(const Params& p,
                                    uint32_t incA,
                                    float ratioB,
                                    float ratioC,
                                    int32_t pwMod,
                                    BlockParams& bp,
                                    int32_t timbreBMod = 0)
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
    int32_t tMod = timbreBMod;
    if (tMod < -100)
        tMod = -100;
    else if (tMod > 100)
        tMod = 100;
    bp.timbreBMod = (int16_t)tMod;
    int32_t clampedPwB = (int32_t)p.pw + pwMod + tMod;
    if (clampedPwB < 0)
        clampedPwB = 0;
    else if (clampedPwB > kMaxPw)
        clampedPwB = kMaxPw;
    bp.pwPhaseB = pwToPhase(clampedPwB, kModeClassic);
    // CSaw ramp from 32768 at pw to 65535 at the cycle end: k (Q16) = 32767 / span16 * 65536
    uint32_t span16 = (0xFFFFFFFFu - bp.pwPhase) >> 16;
    bp.csawK = span16 ? (32767u << 16) / span16 : 0;
    bp.gainA = 4096;
    bp.gainB = levelToGainQ12(p.levelB);
    bp.gainSub = levelToGainQ12(p.sub);
    bp.gainNoise = levelToGainQ12(p.noise) >> 1;
}

// Pitch ratios for Osc B / Osc C depending on mode.
static inline void computeRatios(const Params& p,
                                 float& ratioB,
                                 float& ratioC,
                                 int32_t extraCentsB = 0)
{
    int32_t semi = p.semiB;
    if (semi < kMinSemi)
        semi = kMinSemi;
    if (semi > kMaxSemi)
        semi = kMaxSemi;
    int32_t det = p.detune > kMaxDetune ? kMaxDetune : p.detune;
    if (p.mode == kModeSwarm)
    {
        ratioB = centsToRatio(det + extraCentsB);  // +spread
        ratioC = centsToRatio(-det + extraCentsB); // -spread
    }
    else
    {
        ratioB = centsToRatio(semi * 100 + det + extraCentsB);
        ratioC = 1.0f;
    }
}

} // namespace synthosc

#endif /* SYNTH_OSC_H_ */

