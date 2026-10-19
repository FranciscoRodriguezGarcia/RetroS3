# Fase 4 — audio real de NES, GB y GBC

GB/GBC ya fue probado por el usuario. Esta versión habilita también el PCM
de Nofrendo: mono signed 16-bit a 22050 Hz, por el mismo USB Serial/JTAG.
Conservar el APK instalado; no cambia el protocolo ni AudioTrack.

Desde una terminal ESP-IDF en Retro53, con la consola conectada a Windows:

```powershell
idf.py -p COM7 flash
```

Usar el puerto que realmente aparezca. `build/Retro53.bin` es el build nuevo;
su copia queda en `artifacts/phase4/Retro53-phase4.bin`.

## Qué comprobar

1. En el launcher debe haber silencio, sin el tono de prueba.
2. Abrir una ROM NES con música conocida: deben sonar música y efectos.
3. Comprobar botones, TFT y continuidad de audio en escenas con movimiento.
   Los frames emulados que no se dibujan también generan audio.
4. MENU en NES debe reiniciar completamente y volver al launcher en silencio.
5. Reabrir una ROM y comprobar que vuelve el audio; desconectar/reconectar USB.
6. Probar una partida GB/GBC como regresión y verificar su guardado con MENU.

## Implementación y alcance

- `CONFIG_RETRO_USB_AUDIO_NES=y` en sdkconfig, defaults y Kconfig.
- Nofrendo usa `osd_audio_frame` después de cada frame emulado, dibujado o no.
  Llama al generador APU con un buffer fijo y encola una copia sin esperar USB.
- El acumulador alterna 367/368 muestras en NTSC para producir 22050 muestras
  por 60 frames; usa `NES_REFRESH_RATE` también para builds PAL.
- `osd_getsoundinfo` usa la misma constante de frecuencia que el transporte.
- Se eliminó un printf repetido en cada iteración del bucle NES; se conserva
  el mensaje de entrada, evitando trabajo continuo de consola durante el audio.
- Sin cambios en las reservas PSRAM de NES, framebuffers GB/GBC, SD, TFT,
  protocolo Android ni políticas de guardado/reinicio.

En Logcat, filtrar por `RetroAudio` para observar RX, descartes, underruns,
espera en cola y memoria. La ausencia de PCM en el launcher es normal;
`starvation` del productor puede aumentar allí. La validación de calidad
audible, FPS y latencia requiere la consola y el teléfono reales.
La mezcla conserva la precisión por frame del núcleo Nofrendo existente.
