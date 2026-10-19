# Corrección de MENU durante GB/GBC

Se encontró una carrera entre el launcher y la tarea Gnuboy: ambos podían
consumir el mismo evento MENU. El launcher también podía dibujar la lista
de sistemas mientras la tarea de video GB utilizaba el TFT. Eso puede
impedir que MENU llegue al guardado/reinicio y provocar acceso concurrente
al pipeline de pantalla.

`main/launcher.c` ahora omite controles y dibujo mientras
`emulator_manager_is_running()` sea verdadero. `main/emulator_manager.c`
comparte ese estado mediante un booleano atómico entre tareas.

No se cambia la escritura de SRAM ni se fuerza un reinicio si falla.
GB/GBC sigue guardando primero; NES sigue usando `esp_restart()`.
NES se ejecuta sincrónicamente desde el launcher, por lo que el problema
identificado no explica por sí solo un bloqueo de MENU en NES.

Build ESP-IDF y comprobación de tamaño completados. Actualizar con
`idf.py -p COM7 flash` desde Retro53; conservar el APK instalado.
El firmware compilado queda en `build/Retro53.bin` y su copia en
`artifacts/menu-fix/Retro53-menu-fix.bin`.

Validación física pendiente: dentro de GB y GBC pulsar MENU, comprobar
reinicio al launcher y recuperar la partida. Verificar también controles
durante el juego, que el launcher no aparezca superpuesto y MENU de NES.
Si persiste un bloqueo, distinguir si el juego continúa, si aparece
`ERROR SD: MENU reintenta` y si afecta GB/GBC o NES antes de atribuirlo
a `fsync` o a otra operación del guardado.
