# NES/Nofrendo – Diagnóstico de rendimiento ESP32-S3

## Baseline del dispositivo
- Zelda con Nofrendo: ~20 FPS presentados; audio percibido normal.
- Zelda con Frantastic NES: ~0,8 FPS emulados.
- Los 20 FPS anteriores de Nofrendo eran blits de pantalla, no la cadencia real de CPU/PPU.

## Métricas de la pantalla (promedios por ventana de ~2 s)
- **E**: cuadros emulados/s (incluye cuadros omitidos).
- **D**: blits de imagen enviados/s.
- **SK**: porcentaje de cuadros emulados omitidos por frameskip automático.
- **C**: tiempo medio de nes_renderframe(true) + osd_audio_frame, en ms.
- **V**: tiempo medio de system_video(true), en ms (GUI, vid_blit, vid_flush e input).
- **X**: tiempo de conversión indexado -> RGB565 medio por blit, en ms.
- **T**: tiempo medio de display_draw_rgb565_blocks por blit, en ms (incluye X, envío y esperas).

No sumar X y T: X está incluido en T, y la conversión se solapa parcialmente con DMA.
Las métricas se actualizan aproximadamente cada dos segundos, fuera de la imagen del juego.

## Sospechosos ordenados y criterio para identificarlos

1. **Throughput ST7789/SPI**: 256×224×16 bits = 114.688 bytes por imagen, en catorce bloques de 16 líneas. A 60 MHz, el tiempo físico ideal es ~15,3 ms sin comandos ni huecos; a 60 FPS solo hay 16,7 ms. Si V y T son altos y X bajo, pantalla/SPI es el cuello.
2. **Frame skipping/scheduler**: nes_emulate limita cuadros dibujados usando frames_to_render y nofrendo_ticks. E≈60, D≈20 y SK≈67 % indican velocidad de emulación casi completa con presentaciones omitidas. Confirmar timer ~60 Hz en NES EMU.
3. **Core CPU/PPU/APU**: si E es bajo y C alto, analizar ppu_scanline, nes6502_execute, osd_audio_frame. Comparar C con render(false) del log NES EMU.
4. **Conversión de video**: nes_convert_block consulta paleta RGB565 precalculada; si X alto, examinar la ubicación del framebuffer SRAM/PSRAM y patrón de lecturas.
5. **Tiempo fuera de SPI**: V muy superior a T apunta a GUI, vid_blit/vid_flush, input o scheduling. Las métricas DMA TIMING (gap, resume, wait, fill_late) también ayudarán si se consigue acceso a logs.

## Experimento siguiente
Jugar Zelda durante ~10 segundos y anotar E, D, SK, C, V, X, T. No modificar simultáneamente frecuencia SPI, CPU ni frameskip antes de esta medición.
