# Frantastic NES benchmark port (ESP32-S3)

Branch: `feature/nes-frantastic-benchmark`

This experimental port preserves Nofrendo. When opening an NES ROM, RetroS3
displays a core selector: **Nofrendo** or **Frantastic**. Use UP/DOWN (or LEFT/
RIGHT) to choose, A to start and B to cancel.

## What is included

- Frantastic's platform-independent NES CPU6502, PPU, APU, bus, cartridge
  parser and mapper implementations, copied from Frantastic `main`
  (2026-10-22). Sources are in `components/frantastic_nes/`.
- C++17 ESP-IDF integration in `main/nes_frantastic.cpp`.
- Video: 256x224 displayed at (32,8) on a 320x240 RGB565 TFT, matching
  the current Nofrendo viewport. Colors are converted from 0xAARRGGBB.
- Controller port 1 mapped to RetroS3 inputs; MENU returns to the library.
- APU float samples converted to 22,050 Hz mono int16 USB audio.
- On-screen displayed-frame FPS, updated once per second, in both NES
  cores, Game Boy/Game Boy Color, and Mega Drive.

## Required configuration

Frantastic's PPU owns a 256x240 32-bit framebuffer (~240 KiB), plus internal
CPU/PPU state and cartridge allocations. ESP-IDF must permit the regular C++
heap to allocate in PSRAM:

- `CONFIG_SPIRAM_USE_MALLOC=y`
- `CONFIG_COMPILER_CXX_EXCEPTIONS=y`

These are provided in `sdkconfig.defaults`, but a pre-existing local
`sdkconfig` may retain older values. Check with:

```powershell
Select-String -Path sdkconfig -Pattern "CONFIG_SPIRAM_USE_MALLOC|CONFIG_COMPILER_CXX_EXCEPTIONS"
```

If needed, enable them through `idf.py menuconfig` and reconfigure.

## Test procedure

1. Flash this branch using `./flash-release.ps1 -Port COM7`.
2. Start the same small iNES ROM with Nofrendo and Frantastic, separately.
3. Record on-screen FPS during a static screen and during scrolling.
4. Compare sound, image colors, controller response, and game behavior.
5. Test MENU exit and a ROM with a mapper supported by both cores.

FPS here measures frames **successfully drawn to the TFT** and includes
emulation, conversion and display transfer. No frame skipping is added in the
new Frantastic path. Do not infer CPU-only performance from this number.

## Status / limitations

The full port has been staged but **has not been built or tested on an
ESP32-S3** in this environment. Expect integration testing and possible
toolchain fixes on first firmware build. Frantastic may be slower than Nofrendo;
this port is for an A/B measurement, not a speed improvement claim.
