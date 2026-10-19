# Launcher visual ? prueba f?sica

1. Copiar el contenido de `filesys` a la ra?z de la SD, conservando ROMs y saves.
2. Flashear desde una terminal ESP-IDF en Retro53: `idf.py -p COM7 flash`,
   usando el puerto real. Conservar el APK actual.
3. Probar inicio con la app Android conectada y recibiendo; tambi?n reiniciar
   con la app abierta. Comprobar logo, sonido y salto con A/START.
4. Recorrer las tarjetas animadas, abrir juegos, probar scroll y volver con B/MENU.
5. Iniciar GB/GBC y NES: comprobar controles, video/audio, MENU y recuperaci?n
   del guardado GB/GBC. El launcher debe quedar silencioso despu?s del inicio.

El firmware est? en `build/Retro53.bin`, con copia en
`artifacts/launcher-visual/Retro53-launcher-visual.bin`.
La gu?a de recursos est? en `filesys/LEEME.txt`.

Cambios: `main/ui_media.c`, `main/launcher.c`, `main/rom_manager.c`,
`main/main.c`, CMake y funciones del transporte para consultar receptor y
limpiar la cola al saltar el inicio. Launcher usa 6144 bytes de stack.
Se conservan los emuladores, las reservas PSRAM y las pol?ticas de guardado.

Validaci?n: build ESP-IDF y tama?o de partici?n; decodificaci?n independiente
de los 300 cuadros, comparados mediante SHA256 con los cuadros preparados
antes de comprimir; dimensiones, tiempos y alineaci?n PCM. Se inspeccionaron
previews. El tiempo de carga SD, fluidez TFT, sincron?a audible y regresi?n
de juegos requieren hardware.

Esta entrega implementa inicio, selector de consolas y portada del juego seleccionado
(cover.png preparado como cover.r53a). Sonidos de navegaci?n, gestor de partidas
y avance r?pido quedan pendientes.

## Descubrimiento de consolas

Las carpetas raiz NES, GameBoy y GameBoyColor aparecen aunque esten vacias.
Comprobar que el aviso de ausencia de juegos aparece solo al entrar en una
consola vacia, no en el selector principal. Este ajuste requiere solo firmware.

Los titulos largos del juego seleccionado se desplazan automaticamente de ida y
vuelta, con una pausa de un segundo en cada extremo. Probar nombres de Zelda
que comparten prefijo y comprobar que al cambiar de juego vuelve al inicio.
