# AGENTS.md — Polyend Tracker CE (Original Tracker / MK66 Focus)

Architectural context, hardware constraints, module maps, and rules for AI agents and developers in this repository.

---

## 1. Primary Target Mandate: Original Polyend Tracker ONLY (`MK66`)

> **CRITICAL DIRECTIVE**:
> All new features, DSP engines, UI changes, optimizations, and bug fixes **MUST target the Original Polyend Tracker (`PLATFORM=MK66`, `PRODUCT=ORIGINAL`)**, **NOT** Tracker+ (`PLUS`) or Tracker Mini (`MINI`).

### Target Hardware Specifications (`Original Polyend Tracker`)
| Component | Specification |
| :--- | :--- |
| **MCU** | NXP Kinetis `MK66FX1M0VMD18` (`ARM Cortex-M4F` @ **180 MHz**, single-precision FPU `fpv4-sp-d16`, DSP SIMD) |
| **Internal Flash (`FLASH`)** | **1,024 KB** (`0x00000000`, `~93%` used — keep LUTs and templates compact) |
| **Internal SRAM (`RAM`)** | **256 KB** (`0x1FFF0000`) — Stack, heap, `.data`, `.bss`, DMA buffers |
| **External SDRAM (`EXTERNAL_RAM`)** | **16,384 KB / 16 MB** (`0x70000000`) — Sample pool, delay/reverb, scratch, cold singletons |
| **Audio Engine** | **44,100 Hz**, **128-sample blocks** (`~2.902 ms`/block), **16-bit signed PCM (`int16_t`)**, **Mono sample pool** (`SAMPLE_CHANNELS = 1`) mixed to stereo |
| **Tracks & Instruments** | **8 Audio/Sample Tracks** + **8 MIDI Tracks** (16 total), **48 Instrument Slots** (`0..47`) |
| **Controls & I/O** | **48 RGB pads**, **rotary jog-wheel**, **8 screen buttons**, function keys, **800×480 FT812 EVE2 display** (SPI), SGTL5000 codec, FM Radio, Line/Mic in |

### Target Scope Rules (`Original` vs `+` / `Mini`)
1. **Do NOT develop for `PolySynth` (`synth/` / `FEATURE_SYNTH`)**:
   - `FEATURE_SYNTH` (`libPolySynth.a`, `libPolyDSP.a`, `core/synth/`) is **only enabled on `PLATFORM=IMX`** (`PLUS`/`MINI`) and is compiled out (`kSynthParts = 0`, `GUI_HANDLE_SYNTH = 0`) on Original Tracker.
   - Any synth/generator for Original Tracker **must live inside the core sample/voice playback engine** (`core/libs/Audio/play_memory*.cpp`, `core/AudioEngine/AudioVoice.*`, `core/interface/samplePlayback/`, `core/interface/instrumentEditor/`) or as a `.ptfx` Sample Editor effect (`editor-fx-sdk/`).
2. **Platform Guards (`#ifndef PLATFORM_IMX`)**:
   - **NEVER use `#ifdef __MK66FX1M0__` inside `core/`**: [`core/makefile`](core/makefile) defines `-D__MK66FX1M0__` on **all** platforms for Teensy Audio compatibility.
   - Use `#ifndef PLATFORM_IMX` (or `#if !defined(PLATFORM_IMX)`) for MK66-only code.
   - Use `#if GUI_HANDLE_SYNTH` (never `#ifdef`), since it is `0` on `MK66` and `1` on `IMX`.
   - Desktop controls (48 pads, jog-wheel, radio) are active when `#ifndef TRACKER_MINI`.
3. **Primary Build Command**: Compile and verify Original Tracker with `make original` -> `build/PolyendTracker_<EDITION>_<VER>.ptf` (e.g., `build/PolyendTracker_CE_CZ_1.1.0.ptf`).

---

## 2. Build System & Toolchain

- **Toolchain**: Bundled GNU Arm Embedded Toolchain (`10.3-2021.10`) at `./agt/10.3/bin/arm-none-eabi-` (auto-detected by [`Makefile`](Makefile)).
- **Standards**: `core/` `-std=gnu++20`, `platforms/mk66/` `-std=gnu++17`, both `-fno-rtti -fno-exceptions -O2 -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb`.

### Common Commands (Run from `tracker-ce/`)
```bash
make original   # Build Original Polyend Tracker firmware (.ptf) — PRIMARY TARGET
make ptfx       # Build modular Sample Editor effects (.ptfx)
make version    # Show version and build tag info
make -C core clean PLATFORM=MK66 PRODUCT=ORIGINAL && make -C platforms/mk66 clean PRODUCT=ORIGINAL
```

### Changing Firmware Version or Edition Name
- Update `EDITION_NAME` and `ORIGINAL_VER_*` in [`version.mk`](version.mk) **and** keep fallback `#ifndef` defaults (`FW_EDITION_NAME`, `FW_VER_MAJOR/MINOR/PATCH`) aligned in [`core/source/mtStructs.h`](core/source/mtStructs.h).
- Ensure [`Makefile`](Makefile) removes `PlatformImpl.o`, `interface.o`, and `mtConfig.o` before building `core/` so `FW_EDITION_NAME` and `BUILD_ID_STR` (`HEAD` short hash) recompile into UI/version strings cleanly; if changing `version.mk` alone, rebuild those objects or touch dependents.
- Keep output `.ptf` filename `<= 31` chars (e.g., `PolyendTracker_CE_CZ_1.1.0.ptf`).

---

## 3. Repository & Module Map

```text
tracker-ce/
├── Makefile                        # Top-level build (`make original`, `make ptfx`, `sd-projects`)
├── version.mk                      # Firmware version numbers & Git short-hash build tag
├── core/
│   ├── makefile                    # Builds `core/build-MK66-ORIGINAL/libTracker.a`
│   ├── config/config.h             # Constants & section macros (DMAMEM, AUDIO_CORE_FLASH/RAM)
│   ├── include/                    # Abstract platform interfaces (Platform.h, DisplayDriver.h, etc.)
│   ├── source/                     # Data structs, sequencer, config, themes, envelopes, input
│   │   ├── Tracker.cpp / TrackerImpl.h     # Main loop, init, global singletons, SDRAM buffers
│   │   ├── mtStructs.h / mtStructs.cpp     # Central data structures (`strProject`, `strInstrument`, `strPattern`)
│   │   ├── playModeDefs.h                  # Play mode parameter descriptors & constraints
│   │   ├── mtSequencer.cpp / _patternEdit  # Pattern playback, step evaluation & editing
│   │   ├── mtEnvelopeGenerator.cpp/.h      # Per-instrument ADSR envelopes & LFO modulation
│   │   ├── mtWavetableEnvelopes.cpp/.h     # 256-slot WT Env & WT LFO waveforms
│   │   ├── mtConfig.cpp/.h                 # Persistent device settings (`Config`)
│   │   └── ThemeManager.cpp / Themes.cpp   # `/Themes/*.json` loader & UI palettes
│   ├── AudioEngine/                # Voice allocation, mixer, step FX & SD streaming
│   │   ├── mtAudioEngine.cpp/.h            # Audio graph wiring & 8-voice pool
│   │   ├── AudioVoice.cpp/.h               # Per-track chain (PlayMemory -> Filter -> Overdrive -> Crusher -> Amp -> Pan)
│   │   ├── mtAudioNoteOnHandle.cpp         # Note trigger, step FX, envelope/mod setup
│   │   ├── mtAudioUpdateHandle.cpp         # Real-time parameter/envelope/LFO updates per audio tick
│   │   ├── mtAudioSequencer.cpp            # Sequencer clock & step scheduling
│   │   ├── MixerAndFxs.cpp/.h              # Master mixer, Delay, Polyverb, Limiter/EQ/Enhancer
│   │   └── VoiceStreamManager.cpp/.h       # Hybrid SD streaming for samples >= 256 KB
│   ├── libs/Audio/                 # 128-sample `AudioStream` DSP blocks
│   │   ├── play_memory*.cpp / .h           # `AudioPlayMemory` & modes (`single`, `loops`, `slice`, `beatslice`, `wavetable`, `granular`, `synth`)
│   │   ├── synth_osc.h                     # Fixed-point PolyBLEP/FM/Fold/CZ-PD oscillator for `Synth` mode
│   │   ├── filter_variable.cpp/.h          # Per-voice State Variable Filter (LP/BP/HP)
│   │   ├── effect_overdrive.cpp/.h         # Per-voice overdrive
│   │   ├── effect_bitcrusher.cpp/.h        # Per-voice bitcrusher / SR reducer
│   │   ├── Polyverb/                       # Master stereo reverb
│   │   └── utility/dspinst.h               # Cortex-M4 SIMD intrinsics (`signed_saturate_rshift`, etc.)
│   ├── libs/NewDisplay/ & FT812/   # UI widgets (`cList`, `cBar`, `cWave`, `cPopup`) & FT812 SPI driver
│   ├── interface/
│   │   ├── core/                           # `cInterface` module switching (`interface.cpp`)
│   │   ├── samplePlayback/                 # **Sample Playback** (`samplePlayback.h`, `gSamplePlay.cpp`, `fSamplePlay.cpp`)
│   │   ├── instrumentEditor/               # **Instrument Params (1/2) & Automation (2/2)** (`instrumentEditor.h`, `gInstrEdit.cpp`, `fIinstrEdit.cpp`)
│   │   ├── patternEditor/ & songEditor/    # Sequencer & Song arranger UI
│   │   ├── sampleEditor/ & sampleRecorder/ # Offline waveform/`ptfx` UI & sampling
│   │   ├── sampleImporter/ & projectEditor/# Sample Loader & File screens
│   │   └── performanceMode/ masterParams/ configEditor/ imageViewer/
│   ├── effector/                   # Offline Sample Editor FX & `.ptfx` host (`ptfx_loader.cpp`, `ptfx_manager.cpp`)
│   ├── FileManager/ & Exporter/    # SD serialization (`/Workspace/instruments/`), Undo, WAV export
├── platforms/mk66/                 # MK66 HAL, Teensy core, FatFs SD & `mk66fx1m0.ld`
├── editor-fx-sdk/                  # `.ptfx` plugin SDK (`tracker_plugin.h`, `ptfx_builder.py`)
├── docs/MANUAL.md & generate_mockups.py # User manual & 800×480 SVG mockups (`docs/images/mockups/*.svg`)
└── util/                           # Header-only utilities (`PolyUtil`, `fmt`, `enum_util.h`)
```

---

## 4. Original Tracker (`MK66`) Memory Architecture

1. **`FLASH` (`0x00000000`, `1,024 KB`)**: Holds `.text`, `.rodata`, `.audio_flash_functions`, `.audio_ram_functions`, fonts, and bitmaps. On `MK66`, both `AUDIO_CORE_FLASH` and `AUDIO_CORE_RAM` map to `FLASH` (not ITCM). Keep lookup tables compact and avoid bloated templates.
2. **`RAM` (`0x1FFF0000`, `256 KB` Internal SRAM)**: Holds `.dmabuffers` (`DMAMEM` audio blocks), `.data`, `.bss`, heap, and main stack (`_estack`). Put small, hot real-time voice state here (`AudioPlayMemory`, `AudioVoice`). Never put large non-realtime buffers in `.bss` when `EXTERNAL_RAM` / `externalRamScratchBuff` works.
3. **`EXTERNAL_RAM` (`0x70000000`, `16,384 KB / 16 MB` SDRAM, `.noinit_RAM4` `NOLOAD`)**:
   - Contains `.scratch_memory` (`externalRamScratchBuff`, `768 KB`), `.delay_memory` (`1.5 MB`), `.reverb_memory` (`32 KB`), `.sample_memory` (`11.25 MB` = `7.5 MB` sample pool + `3.75 MB` `EDIT_BUFF_SIZE`), `VoiceStreamManager` buffers, `g_wtEnvelopeBank` (`64 KB`), and cold `__NOINIT(EXTERNAL_RAM)` singletons.
   - **`NOLOAD` Initialization**: `__NOINIT(EXTERNAL_RAM)` is **not zeroed before static constructors**. Explicitly `memset(..., 0, ...)` or placement-`new` during `TrackerImpl::init()`.
   - **SDRAM Bandwidth**: Slower than internal SRAM; keep 128-sample inner DSP state in registers or internal `RAM`, not `EXTERNAL_RAM`.

---

## 5. Real-Time Audio Engine & DSP Guidelines (`MK66`)

- **Pipeline**: 8 mono voices (`mtAudioEngine.voice[0..7]`, `AudioVoice`) chaining `AudioPlayMemory` -> `AudioFilterStateVariable` (LP/BP/HP) -> `AudioEffectOverdrive` -> `AudioEffectBitcrusher` -> `AudioAmplifier` stages -> stereo `panning` -> master mixer + Delay/Reverb sends.
- **`AudioPlayMemory` Modes**: `playModeSingleShot`, `playModeLoopForward`, `playModeLoopBackward`, `playModePingpong`, `playModeSlice`, `playModeBeatSlice`, `playModeWavetable`, `playModeGranular`, and `playModeSynth` (`synth_osc.h`).
- **Strict Real-Time Rules (Cortex-M4 @ 180 MHz)**:
  1. **Zero allocations or blocking in audio ISR**: Never call `malloc`, `free`, `new`, `delete`, `std::vector`, `std::string`, or FatFs SD calls in `*::update*()`.
  2. **Fixed-point (`int32_t`/`uint32_t`) or single-precision `float` (`0.0f`) ONLY**: **NEVER use `double`** (or unsuffixed literals like `0.5`, `<cmath>` `sin()`/`pow()`), which triggers software float emulation and audio dropouts. Use `f` literals and `sinf`/`powf`/`fabsf` or LUTs.
  3. **Hoist per-block math**: Compute pitch ratios, phase increments, filter coefficients, and modulated values once per 128-sample block outside the sample loop.
  4. **ISR safety**: Wrap multi-field voice mutations from the UI/sequencer thread with `AudioNoInterrupts()` / `AudioInterrupts()`.

---

## 6. Binary Compatibility & Serialization (`.pti`, `.mt`, `.mtp`)

1. **`sizeof(strInstrument) == 372` Must Never Change**:
   - `strInstrument` ([`core/source/mtStructs.h`](core/source/mtStructs.h)) serializes directly into `.pti` files and `/Workspace/instruments/instrument_%02d.pti` (`sizeof(strInstrumentFile) == 392`), guarded by `static_assert(sizeof(strInstrument) == 372)`.
   - To add per-instrument parameters without breaking `.pti` files:
     - Reuse alignment padding bytes inside `strInstrument` (verify offset and size).
     - Overlay mutually-exclusive mode structs onto existing arrays (e.g., `strSynthParams` inside `uint16_t slices[48]`, `96` bytes).
     - Share `envelope[6]`/`lfo[6]` slots across mutually-exclusive modes using bitmask flags in `envelope[slot].enable`.
     - Extra dynamic modulation slots (`kExtraModSlotCount = 3` × 28-byte `strExtraModSlot` overlays inside `slices[48]`, giving up to `9` simultaneous generators via `acquireEnvSlotForDest()` / `findEnvSlotForDest()`) must preserve `static_assert(sizeof(strInstrument) == 372)` and guard shared `slices[48]` bytes when `playMode == playModeSlice || playMode == playModeBeatSlice`.
2. **Never Reorder Serialized Enums**: Append new values at the end before `*Count` / `*Max` sentinels.
3. **Streamable Play Modes (`isPlayModeStreamable`)**: On `#ifndef PLATFORM_IMX`, only `playModeSingleShot`, `playModeLoopForward`, `playModeLoopBackward`, and `playModePingpong` stream from SD via `VoiceStreamManager`. `Slice`, `Beat Slice`, `Wavetable`, `Granular`, and `Synth` return `false`.
4. **Sample-less Modes (`playModeSynth`)**: Check all gates on `sampleMemory[idx].length == 0` / `isActive` so synth instruments can trigger and edit without a loaded WAV sample.

---

## 7. UI & Control Ergonomics on Original Tracker

- **Preserve the Original Workflow**: Keep dedicated buttons, the 8 screen buttons (`interfaceButtonScreen1..8`), jog-wheel, and 48 pads intuitive and consistent. Extend screens using established Tracker patterns (contextual `[1..8]` buttons, `1/2` & `2/2` pages, mode sub-views) without breaking muscle memory.
- **No redundant modulation sub-screens or generic slots**: Do NOT expose abstract `Mod 1`/`Mod 2` slots, separate `Mod Matrix` screens, or hidden `Shift+Wheel` destination reassignments in `Instrument Automation (2/2)`. Expose every modulatable parameter through the single context-filtered `[1–2] Destination` list (`rebuildVisibleAutoDests()` / `getContextualAutoDestName()`), mark active rows with `" *"`, clear on `Delete` over `[3] Type`, and allocate dynamic slots transparently (`Pool Full (N/N)` feedback only on overflow).
- **Screen**: `800 × 480` via FT812 (`core/libs/NewDisplay/`).
- **Key Instrument & Playback UI Files**:
  - **Sample Playback**: [`core/interface/samplePlayback/samplePlayback.h`](core/interface/samplePlayback/samplePlayback.h), [`gSamplePlay.cpp`](core/interface/samplePlayback/gSamplePlay.cpp), [`fSamplePlay.cpp`](core/interface/samplePlayback/fSamplePlay.cpp)
  - **Instrument Parameters (1/2) & Automation (2/2)**: [`core/interface/instrumentEditor/instrumentEditor.h`](core/interface/instrumentEditor/instrumentEditor.h), [`gInstrEdit.cpp`](core/interface/instrumentEditor/gInstrEdit.cpp), [`fIinstrEdit.cpp`](core/interface/instrumentEditor/fIinstrEdit.cpp)
- **UI Gotchas**:
  - **Never shift `mtInterface.modules[]` indices** in [`core/interface/core/interface.cpp`](core/interface/core/interface.cpp).
  - **Bootloader `.ptf` filename limit**: Keep `.ptf` filenames `<= 31` chars (`PolyendTracker_CE_CZ_1.1.0.ptf` is 29 chars) so the MK66 bootloader's 32-byte `/Firmware/_fwinfo` buffer stays null-terminated.

---

## 8. Coding Style & Workflow Checklist

1. **Formatting ([`core/.clang-format`](core/.clang-format))**: `Mozilla`, `Allman` braces (`{` on new line), `IndentWidth: 4`, spaces only, `ColumnLimit: 100`. Format only modified lines/files.
2. **Pre-Completion Verification**: Run `make original` and verify zero compiler errors, no new warnings in modified files, `static_assert(sizeof(strInstrument) == 372)` passes, and linker memory (`FLASH`, `RAM`, `EXTERNAL_RAM`) fits `MK66` limits.
3. **Manual & Mockup Update ([`docs/MANUAL.md`](docs/MANUAL.md))**: Whenever a feature or UI changes, update [`docs/MANUAL.md`](docs/MANUAL.md) and regenerate `800×480` SVG mockups in `docs/images/mockups/` via [`docs/generate_mockups.py`](docs/generate_mockups.py) showing top bar, viewport, and bottom `[1]..[8]` buttons.
