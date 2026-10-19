# Prueba local de audio USB — Fase 2

Esta versión transmite un tono suave de **440 Hz** generado en el ESP32,
mono PCM16 a 22050 Hz. Se escucha desde el launcher y no depende del audio
de NES/GB/GBC. Los juegos y los controles siguen disponibles, pero su PCM
no se transmite mientras `CONFIG_RETRO_USB_AUDIO_TEST_TONE=y`.

## Dónde están los cambios

- Firmware: `C:\Users\franc\Code\Retro53\components\retro_audio\`.
- Configuración: `sdkconfig`, `sdkconfig.defaults` y
  `components/retro_audio/Kconfig.projbuild`.
- Android real: `C:\Users\franc\AndroidStudioProjects\RetroController\app\src\main\java\com\retro\retrocontroller\`.
- Copias de referencia Android: `Retro53\android-integration\`.
- Binarios de esta prueba: `Retro53\artifacts\phase2\`.

Los cambios están escritos en disco. Retro53 no tiene `.git`, por lo que
Source Control no muestra un diff de ese repositorio. Los fuentes Kotlin
reales se abren en el proyecto RetroController de Android Studio; no son
parte del árbol de fuentes C de ESP-IDF. En VS Code, abrir `components`
en el explorador permite ver la capa nueva. No se modificó `_stdint.h`
de la toolchain de Espressif.

## Cómo probar

1. Conectar la consola a Windows. En una terminal ESP-IDF, desde Retro53,
   ejecutar `idf.py -p COM7 flash`, usando el puerto que realmente aparezca.
   Este trabajo no ejecuta el flasheo automáticamente.
2. Instalar en el teléfono `artifacts\phase2\RetroController-phase2-debug.apk`.
   También puede ejecutarse la app desde el proyecto RetroController en Android
   Studio. Se necesita firmware y APK nuevos para esta prueba.
3. Conectar teléfono y consola por USB-C como con el joystick anterior;
   aceptar el permiso USB. No mantener un monitor Windows abierto sobre el puerto.
4. Confirmar el título **RETRO · USB AUDIO TEST**. Debe aparecer
   **TONE 440Hz RX:... DROP:...**, con RX aumentando. Empezar con volumen bajo.
5. En el launcher, comprobar el tono y los controles. Entrar a un juego:
   el tono debe continuar y el TFT debe seguir funcionando.
6. Probar MENU. NES reinicia; GB/GBC guarda SRAM antes de reiniciar y no
   reinicia si el guardado falla. Luego de volver al launcher debe regresar
   el tono. No se cambiaron estas políticas.
7. Desconectar: se detiene la reproducción. Reconectar y comprobar otra vez
   el contador RX y el tono. Probar también un reinicio de la consola.

La app no tiene un botón adicional para iniciar el tono: el firmware de
Fase 2 lo genera automáticamente cuando recibe heartbeat de Android.
Si RX crece pero no se oye, registrar la salida de audio elegida y el volumen
multimedia del teléfono, y revisar los errores AudioTrack en Logcat.

## Diagnóstico

En Android Studio Logcat filtrar por **RetroAudio**. Al abrir USB se registran
la interfaz CDC y las direcciones/tamaños de los endpoints BULK IN y OUT
observados realmente; no se asume que COM7 represente otro canal USB.

Cada segundo se registra:

- ESP32: bloques encolados, descartados por cola llena o antigüedad,
  fallos TX, errores de recepción, ocupación máxima y antigüedad máxima,
  heap interno libre/mínimo, PSRAM libre, margen de stack TX y esperas sin PCM.
- Android: paquetes, descartes por cola/antigüedad/escritura parcial,
  paquetes rechazados, underruns AudioTrack, espera máxima de cola y
  estimación del audio pendiente en AudioTrack (`sinkMs`).

Son mediciones de buffers, no una medición acústica extremo a extremo.
La latencia audible final requiere medir con el teléfono real.
La memoria y las métricas Android pueden consultarse además en el profiler.
Los contadores son acumulados desde el arranque o conexión correspondiente.

La cola ESP32 es estática. Android reserva cinco buffers PCM al conectar:
tres en cola, uno en recepción y otro en reproducción. El parser presta
su buffer al callback; no reserva un array por paquete de audio.

Los logs de firmware continúan en UART0. COM7 queda para protocolo y flasheo,
sin logs de aplicación mezclados con PCM. Los mensajes de arranque se
descartan mediante delimitación y CRC. El timeout TX solicitado de 5 ms
se redondea a cero ticks con los 100 Hz actuales: TX no espera espacio.

## Alcance

Se entrega firmware y APK compilados, con pruebas automatizadas de protocolo.
La audición del tono, el caudal, los FPS y la reconexión física requieren esta
prueba en consola/teléfono. La integración real de GB y NES queda para las
fases siguientes; sus conexiones adelantadas no aportan audio en este modo.
No desactivar el modo de tono hasta cerrar la prueba de transporte.
