# Audio USB de Retro53

**Estado actual: Fase 4, audio real de NES y GB/GBC; tono desactivado.** Ver
[PRUEBA_AUDIO_FASE4.md](PRUEBA_AUDIO_FASE4.md) para actualizar el firmware y probar.
El APK de Fase 2 sigue siendo compatible. GB/GBC fue probado por el usuario;
NES queda listo para validación física. Android utiliza buffers PCM reutilizables y telemetría
tipo 6 para observar descartes, memoria y espera en cola.

## Implementación

Se conserva USB Serial/JTAG nativo (VID 303A, PID 1001), su CDC y el
flasheo por COM7. No se cambia el PHY, TinyUSB, UAC, TFT, SD, reservas
PSRAM de NES ni los dos framebuffers de GB. El teléfono sigue siendo host.
PCM mono signed 16-bit little-endian, 22050 Hz: 44100 bytes/s antes de
encapsular. Es tráfico bulk USB, no una UART física limitada a 115200 bps.
El caudal sostenido y la latencia se deben medir con el teléfono real.

`components/retro_audio` copia las muestras a una cola de tres bloques de
hasta 384 muestras (~52 ms). La entrega desde emulación usa esperas cero;
si está llena se elimina el bloque más antiguo. El transmisor tiene prioridad
3, descarta bloques de más de 60 ms y es el único escritor USB de aplicación.
El driver tiene 2048 bytes TX adicionales (~46 ms de PCM sin escapes).
Una escritura espera como máximo 5 ms por operación del driver, sólo en
esa tarea. No hay USB en callbacks de emulación ni en tareas de video.

Gnuboy: `gb_audio_cb_t(void *, size_t)` recibe una cantidad de elementos
`int16_t`, NO bytes. `gnuboy_set_soundbuffer` usa la misma unidad. El buffer
estático tiene 512 muestras y se instala antes del reset. El callback se
invoca al llenarse el buffer desde `gb_sound_emulate` y para el remanente
al terminar `gnuboy_run`. Se copia antes de que Gnuboy reutilice el buffer.
El divisor interno redondea 2097152 / 22050 a 95, por lo que su frecuencia
efectiva es aproximadamente 22075 Hz; existe una pequeña deriva que las
colas acotadas absorben mediante descarte. No hay resincronización adaptativa.

Nofrendo: `osd_setsound` entrega el generador `apu_process`, no PCM ya generado.
Ahora `osd_audio_frame` lo ejecuta una vez por frame emulado, incluso si no se
dibuja. Usa un acumulador fraccionario (367/368 muestras en NTSC) y
`NES_REFRESH_RATE` para PAL. El APU ya usa mono signed 16-bit a 22050 Hz.
Se genera aun sin receptor, para mantener el avance del APU; sólo se omite
el encolado USB. La mezcla por frame mantiene la precisión del núcleo actual;
no introduce un planificador de audio por ciclo de CPU.

## Protocolo v1

SLIP con delimitador `C0` inicial y final. Dentro del cuerpo, `C0` se representa
como `DB DC` y `DB` como `DB DD`. Todos los enteros multibyte son little-endian.

| Offset | Campo |
| --- | --- |
| 0 | Versión u8 = 1 |
| 1 | Tipo u8 |
| 2 | Longitud de payload u16 |
| 4 | Secuencia u32 (TX ESP32 incremental; RX Android reservado = 0) |
| 8 | Payload, hasta 768 bytes |
| 8 + longitud | CRC16 u16 |

CRC-16/CCITT-FALSE: polinomio 1021, inicio FFFF, sin reflexión, xor final 0.
Cubre cabecera y payload, sin los delimitadores ni el propio CRC.
Vector estándar `123456789` => `29B1`.

| Tipo | Dirección | Payload |
| --- | --- | --- |
| 1 | ESP32 → Android | PCM, longitud par, máximo 384 muestras |
| 2 | ESP32 → Android | Estado de formato: rate u32, canales u8, bits u8 |
| 3 | Android → ESP32 | Heartbeat: 1 habilita audio durante 1 s; 0 deshabilita |
| 4 | Android → ESP32 | Máscara de botones u8 con el orden existente |
| 5 | Android → ESP32 | Comando u8: 1 = MENU |
| 6 | ESP32 → Android | 13 valores u32LE de diagnóstico (orden documentado en la guía Fase 2 y `send_stats`) |

Android envía controles y heartbeat cada 50 ms por el mismo endpoint OUT,
en una sola corrutina IO que también envía MENU. La recepción IN corre en
otro hilo y valida delimitación, versión, longitud, escapes y CRC. Acepta
fragmentación y varios paquetes por lectura. Estado se distingue de audio;
el reproductor v1 utiliza el formato fijo anterior. La secuencia permite
diagnóstico de pérdidas; no se retransmite audio.
Los controles legacy `A5 mask` y `A6 01` siguen aceptándose, sin CRC, para
el joystick anterior y `tcp_usb_bridge.py`. El bridge TCP existente no
implementa recepción de audio; la app usa USB directo.

## Android y reinicio

Proyecto aplicado: `C:/Users/franc/AndroidStudioProjects/RetroController`.
Las copias en `android-integration/` documentan los archivos aplicados.
`UsbAudio` mantiene una cola de tres paquetes y crea `AudioTrack` en modo
streaming con buffer `max(getMinBufferSize, 3528)` (80 ms nominales).
La escritura no bloqueante descarta lo que el sink no puede aceptar.
La latencia total depende además del dispositivo Android y su salida.

Al desconectar se detienen/join los hilos con esperas acotadas desde IO,
se libera AudioTrack y luego la interfaz USB. Sin PCM durante 500 ms se
libera AudioTrack aunque el controlador USB siga enumerado. Si llegan
muestras otra vez se crea una instancia nueva. Los errores de audio hacen
fallar el siguiente envío y disparan la reconexión existente.

Un `esp_restart()` reinicia el protocolo, su secuencia y el heartbeat.
No debe suponerse que siempre produce USB_DEVICE_DETACHED: el controlador
Serial/JTAG es hardware independiente. Si la conexión sigue válida, los
heartbeats habilitan nuevamente el audio; si desaparece o falla OUT, la
app cierra y vuelve a buscar el dispositivo/pedir permiso. Los paquetes
incompletos y texto de arranque se descartan y se resincroniza por SLIP/CRC.
La conducta exacta de enumeración debe verificarse en el teléfono real.

Se conserva MENU de NES con `esp_restart()`. GB/GBC conserva su guardado
actual y reinicio sólo si el guardado tiene éxito, conforme a la política
confirmada por el usuario.

## Logs, monitor y flasheo

Antes, `CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y` duplicaba stdout y
ESP_LOG sobre el mismo USB: no era seguro intercalar PCM binario ahí.
Ahora la consola primaria sigue siendo UART0 y la secundaria es NONE,
tanto en sdkconfig como en sdkconfig.defaults. No se envía audio por printf.
Los mensajes de ROM/boot no son parte del protocolo y se ignoran por CRC.

**Cambio visible:** el monitor de COM7 ya no muestra logs de aplicación.
Los logs siguen saliendo por UART0; no se incorpora hardware para audio.
Restaurar la consola secundaria requiere deshabilitar esta transmisión
binaria o encapsular toda la consola mediante un escritor común.
El flasheo USB nativo no depende de stdout y sigue disponible. Cerrar la
app/monitor antes de flashear: el teléfono y Windows no pueden ser hosts
del mismo cable simultáneamente. No se flasheó hardware durante este trabajo.

Referencias: [USB Serial/JTAG de Espressif](https://docs.espressif.com/projects/esp-idf/en/release-v5.2/esp32s3/api-guides/usb-serial-jtag-console.html)
y [AudioTrack de Android](https://developer.android.com/reference/android/media/AudioTrack).
También se inspeccionó el driver local de ESP-IDF v5.5.5: la escritura al
ringbuffer devuelve el tamaño completo o cero; no mezcla escritores por sí sola.

## Validación

- Firmware: build completo ESP-IDF v5.5.5 y comprobación de tamaño de partición.
- Android: compilación Kotlin y `:app:testDebugUnitTest`.
- Pruebas de protocolo: CRC conocido; las 256 máscaras fragmentadas byte
  por byte (incluidos C0 y DB); CRC corrupto, truncamiento, exceso de longitud,
  texto de arranque y resincronización posterior.
- Pendiente en hardware: NES, GB y GBC con audio; MENU y guardado; SD/TFT;
  desconectar/reconectar y reiniciar sin detach; FPS, underruns y latencia;
  receptor detenido y flasheo desde COM7. No se afirma validación física.

La caché ccache del entorno produjo errores al compilar; se desactivó sólo
en el CMakeCache del build (`CCACHE_ENABLE=0`), sin cambiar el proyecto.
