# Fase 3 — audio real de GB/GBC

Se desactiva el tono de 440 Hz y se transmite el PCM mono signed 16-bit
de Gnuboy por el mismo transporte USB probado en la Fase 2.

## Actualización

Conservar el APK de Fase 2 con el que ya se escuchó el tono. No hace falta
reinstalarlo: protocolo y AudioTrack son compatibles. El título de la app
sigue siendo `RETRO · USB AUDIO TEST`; durante el juego mostrará `AUDIO RX:...`
en lugar de `TONE 440Hz`.

Con la consola conectada a Windows, desde una terminal ESP-IDF en Retro53:

```powershell
idf.py -p COM7 flash
```

Usar el puerto realmente enumerado. El build actual `build/Retro53.bin`
corresponde a Fase 3; se conserva una copia en
`artifacts/phase3/Retro53-phase3.bin`. Los binarios de Fase 2 siguen en
`artifacts/phase2/` por si se necesita volver a comprobar el tono.
No flashear los binarios históricos de Fase 2 para esta prueba.

## Resultado esperado

1. Conectar Android a la consola y aceptar el permiso USB si lo pide.
2. En el launcher debe haber silencio. Ya no debe sonar el tono continuo.
3. Abrir una ROM de GB con música conocida: se debe escuchar el juego.
4. Comprobar botones y TFT simultáneamente. Probar también una ROM de GBC.
5. Pulsar MENU: se guarda SRAM antes de reiniciar. Si falla el guardado,
   no se reinicia; se conserva el mecanismo de reintento existente.
6. Al volver al launcher, debe volver el silencio. Reabrir la partida para
   verificar su persistencia.
7. Desconectar/reconectar USB y comprobar que regresa el audio del juego.

NES permanece sin transmitir audio: su activación corresponde a Fase 4.
No se cambiaron reservas de memoria NES, los dos framebuffers GB/GBC ni
las tareas de emulación y video.

## Cambios concretos

- `sdkconfig`, `sdkconfig.defaults` y `components/retro_audio/Kconfig.projbuild`:
  tono desactivado; audio USB de NES desactivado mediante su propia opción.
- `main/gameboy.c`: callback y buffer fijo de 512 muestras; capacidad calculada
  a partir del array, sin asignaciones por bloque ni escrituras USB aquí.
- `components/gnuboy/gnuboy.c`: entrega de PCM pendiente al terminar el frame,
  incluso si LCDC indica pantalla desactivada. Se procesan los ciclos de sonido
  pendientes antes de entregar el buffer parcial.
- `components/gnuboy/gnuboy.h`: contrato de callback/capacidad documentado en
  elementos int16_t, no bytes.
- `components/nofrendo/nes_platform.c`: envío USB condicionado a la opción NES.

## Diagnóstico y límites

En Logcat filtrar por `RetroAudio`. Durante el juego observar RX, descartes,
underruns y antigüedad de cola. En el launcher, `starvation` puede aumentar:
indica que no se produce PCM y es esperable allí; no es el contador de
underruns de AudioTrack. Los contadores de ocupación y memoria del ESP32
siguen disponibles mediante la telemetría existente.

Gnuboy solicita 22050 Hz, pero su divisor entero da aproximadamente 22075 Hz.
La pequeña deriva y el rendimiento de cada ROM deben evaluarse en el
teléfono antes de decidir si se necesita compensación. Las colas continúan
acotadas y descartan audio atrasado.

El firmware se compila y se comprueba contra el tamaño de la partición.
La calidad audible, continuidad, FPS, guardado y reconexión se validan
con consola y teléfono; no se deducen de la compilación.
