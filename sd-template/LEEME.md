> Plantilla anterior. Usar ahora **filesys/** o **artifacts/Retro53-filesys.zip**, con el inicio en la raiz de SD y carpetas NES/GameBoy/GameBoyColor.

# Inicio y selector animado ? copiar a la SD

Copiar la carpeta **Retro53** de esta plantilla a la ra?z de la SD.
Conservar las carpetas actuales de ROMs y saves.

```
Retro53/
  interfaz/
    inicio.gif
    inicio.mp3
    inicio.r53a
    inicio.pcm
  consolas/
    gb/
      console.gif
      console.r53a
    nes/
      console.gif
      console.r53a
```

El firmware reproduce `.r53a` y `.pcm`; GIF y MP3 son las fuentes editables.
Despu?s de reemplazarlas, ejecutar `python tools/prepare_ui_assets.py`
desde Retro53 y copiar los recursos regenerados. Requiere Pillow e
imageio-ffmpeg, instalados en `.asset-tools`.

`console.gif` en cada consola sigue siendo la convenci?n de los originales.
La preparaci?n usa una paleta compartida de 128 colores sin tramado,
rect?ngulos de cambios y compresi?n por repeticiones/literales. Conserva
proporciones, cuadros y tiempos. El archivo contiene 256 entradas de paleta
para simplificar la indexaci?n.

| Recurso | Tama?o visual | Cuadros | Duraci?n | Bytes preparados |
| --- | --- | --- | --- | --- |
| Inicio | 320x76 | 25 | 1960 ms | 69716 |
| Game Boy | 192x144 | 144 | 5760 ms | 94989 |
| NES | 192x144 | 131 | 3930 ms | 353472 |
| Audio | mono S16LE, 22050 Hz | ? | 2115 ms | 93312 |

El audio se prepara con volumen 0,5 y un l?mite de 10 s. Los originales
aportados se conservan sin modificar.

Al prender/reiniciar se reproduce el inicio una vez, manteniendo el cuadro central
del logo hasta terminar el sonido. A, START o MENU permiten saltarlo. Android
con el APK actual debe estar recibiendo para escuchar audio: hay una espera
limitada de reconexi?n de 1 s despu?s de cargar los recursos. Sin receptor,
el inicio visual contin?a; conectarlo despu?s no repite el inicio.
La latencia de USB/AudioTrack requiere verificaci?n f?sica de sincron?a.

Izquierda/derecha selecciona consola; arriba/abajo tambi?n funciona. A abre
los juegos; B/MENU vuelve. GBC reutiliza la animaci?n GB si no hay recurso
propio `consolas/gbc/console.r53a`. La lista de juegos tiene selecci?n y scroll.
Las car?tulas por juego y perfiles/save states corresponden a otra entrega.

Cada animaci?n seleccionada se cachea en PSRAM; la primera carga puede tardar
unos segundos con SD a 1 MHz e indica CARGANDO. No se lee SD por cada cuadro.
Todas las cach?s se liberan antes de iniciar el emulador. Sin recurso gr?fico
se usa la interfaz b?sica; sin ROMs se muestra un aviso; si no monta SD se
muestra un mensaje fijo de error.

Las rutas viejas GameBoy, GameBoyColor y NES siguen funcionando. Tambi?n:

```
Retro53/consolas/gb/juegos/<juego>/<archivo>.gb
Retro53/consolas/gbc/juegos/<juego>/<archivo>.gbc
Retro53/consolas/nes/juegos/<juego>/<archivo>.nes
```

No renombrar ROMs al moverlas: sus nombres identifican el guardado GB/GBC
actual. Evitar duplicarlas entre ambas estructuras. Se conserva el l?mite de
64 ROMs y 256 bytes de ruta. Las partidas existentes no se migran ni borran.
