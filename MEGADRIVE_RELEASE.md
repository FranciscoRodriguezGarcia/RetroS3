# Mega Drive / ESP32-S3 Release experiment

This branch reconnects Mega Drive in the ROM scanner and launcher. It retains
the historical test configuration: 68000 + VDP, video rendered every other
emulated frame, Z80 and sound generation disabled. **Not full-system Genesis
emulation.** No game compatibility or FPS figure is guaranteed before testing
on the actual board.

## Build / flash without accidentally using Debug

Open an ESP-IDF PowerShell environment, then:

```powershell
cd C:\Users\franc\Code\RetroS3
idf.py menuconfig
```

Select `Compiler options > Optimization Level > Optimize for performance (-O2)`.
This must update the effective, local `sdkconfig`; adding a value to
`sdkconfig.defaults` **does not override** a pre-existing `sdkconfig`.

```powershell
.\flash-release.ps1 -Port COM7
```

The script refuses to flash unless the effective optimization is `PERF` and
the target is `esp32s3`, and it stops on build errors. Existing board-specific
configurations are preserved.

ROM directory: `/sdcard/MegaDrive` (`.md`, `.gen`) or the legacy
`/sdcard/Retro53/consolas/md/juegos`. Interleaved `.smd` dumps are deliberately
not recognized because this loader does not convert their format.

`components/gwenesis` is vendored from `bzhxx/gwenesis` at commit
`168e46661a086a0b793a9ee127a206604ebe7df1` with ESP32-S3 integration
changes. Preserve its GPLv3 licensing notices. It is separate from the
privately developed Frantastic cores.

## Performance measurement

The existing `EMU FPS` log counts emulated frames while `DISPLAY FPS` counts
frames sent to the display. The current video-skipping policy aims for about
60 emulated and 30 displayed frames per second, *if the hardware keeps up*.
The next experiment is to measure those logs before re-enabling Z80/audio.
