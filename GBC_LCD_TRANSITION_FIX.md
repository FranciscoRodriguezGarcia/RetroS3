# LCD apagado durante un frame — Link's Awakening DX

El usuario informa congelamiento de imagen y audio al pulsar A en la intro.
La corrección previa de framebuffers no resolvió ese caso.

Se encontró un bucle sin salida posible dentro de `gnuboy_run`: la condición
visible esperaba que LY superara 144, pero la ROM puede apagar el LCD desde
`gb_cpu_emulate`, lo que deja LY en cero. Entonces la función no retorna,
no se actualiza input/MENU y tampoco se llega a devolver el framebuffer.

Ahora ambos bucles de espera de LY comprueban LCDC y tienen un límite de
308 pasos (dos períodos de 154 líneas). Si se interrumpe la parte visible,
se entrega el audio pendiente y se retorna sin publicar un frame incompleto.
El wrapper devuelve el framebuffer no publicado con la corrección previa.
Esto permite actualizar controles y procesar MENU antes de continuar.

Validación:

- Firmware ESP-IDF compilado y tamaño de partición verificado.
- `tests/gnuboy_lcd_regression.py`: toma el cuerpo de producción y adapta
  su sintaxis C a C# para ejecutarlo con CPU/LCD simulados mediante Add-Type.
  Pasan frame normal, LCD apagado durante visible/vblank, LY detenido en
  ambos bucles y LCD apagado desde el inicio. Al retirar los guards en una
  copia generada, la transición a LCD apagado reproduce el bucle sin retorno.
  Es una prueba de condiciones de salida; no ejecuta la ROM ni la CPU real.

Actualizar con `idf.py -p COM7 flash` desde Retro53 y conservar el APK.
Firmware: `build/Retro53.bin`, copia en
`artifacts/gbc-lcd-transition/Retro53-gbc-lcd-transition.bin`.

Pendiente: repetir A en la intro de Link's Awakening DX, comprobar transición,
audio y controles; probar MENU y recuperación de SRAM. El cambio no omite
el guardado ni fuerza reinicios por timeout. NES permanece sin cambios.
