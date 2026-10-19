# Corrección de imagen congelada en GB/GBC

Se encontró una fuga de propiedad de framebuffers: `gnuboy_run(true)` puede
retornar sin callback de video cuando el LCD emulado está apagado. El wrapper
había tomado un buffer libre y sólo lo devolvía a través de la tarea de
display. Dos retornos sin callback agotaban los dos buffers de PSRAM y la
emulación quedaba esperando indefinidamente, sin llegar a procesar MENU.

En `main/gameboy.c`, después de `gnuboy_run`, se devuelve al pool cualquier
buffer que no haya sido publicado al display. Los frames publicados ya
marcan `gb_current_frame_index=-1` y no se devuelven por duplicado.
Se cede un tick cuando no hay frame de video, para evitar monopolizar CPU
sin la contrapresión habitual del display. La espera de buffer libre ahora
vence a los 100 ms para volver a comprobar MENU aunque el display se detenga.

Se conservan los dos framebuffers PSRAM, las tareas actuales, el transporte
de audio y el guardado obligatorio antes del reinicio GB/GBC. La corrección
previa que evita que el launcher consuma MENU también permanece incluida.
No se cambia NES.

Firmware compilado y tamaño de partición verificado. Actualizar desde una
terminal ESP-IDF en Retro53 con `idf.py -p COM7 flash`; usar el puerto real
si cambió. Conservar el APK actual. Copia del firmware:
`artifacts/gbc-frame-fix/Retro53-gbc-frame-fix.bin`.

Pendiente en hardware: repetir la misma ROM GBC y la escena que se congelaba;
comprobar imagen, audio, controles y MENU; recuperar la partida guardada.
La fuga identificada es real, pero hasta repetir ese caso no se afirma que
explique todos los bloqueos de todas las ROMs.
