# Bad Piggies para PS2 - Nivel 1 (prueba)

Version de prueba del nivel 1. **El nucleo del juego (fisica y reglas) esta probado en PC; la parte de PS2 (dibujo y mando) NO se ha podido probar todavia.** Si no compila o no arranca, copia el error y se corrige.

## Como compilarlo (sin instalar nada, solo con el navegador)
1. Crea una cuenta gratis en github.com y un repositorio nuevo (boton "New"), publico, llamado por ejemplo `bad-piggies-ps2`.
2. En el repositorio: **Add file > Upload files**, y arrastra TODO el contenido de esta carpeta (`src`, `tools`, `Makefile`, `LEEME.md`).
3. La carpeta oculta `.github` a veces no se sube al arrastrar. Para asegurarte: **Add file > Create new file**, en el nombre escribe `.github/workflows/build.yml`, pega el contenido del archivo `build.yml` que viene en el paquete y pulsa **Commit changes**.
4. Abre la pestana **Actions**. Veras una ejecucion "Compilar para PS2"; tarda unos minutos. Si sale con una cruz roja, entra, copia el texto del error y mandamelo.
5. Si sale con tilde verde, entra en la ejecucion y baja el archivo **bpiggies-elf** (un zip con `bpiggies.elf` adentro).

## Como ejecutarlo en la PS2
Copia `bpiggies.elf` a un pendrive (o a la memory card) y abrelo con un lanzador de homebrew (uLaunchELF u OPL). Esto exige que la PS2 pueda ejecutar programas caseros (por ejemplo con Free McBoot).

## Controles
- Stick izquierdo o cruceta: mover el cursor
- X: elegir una pieza de la bandeja de abajo (cerdito, caja, rueda; los puntitos son las que quedan) y colocarla en una casilla
- Cuadrado: quitar la pieza de la casilla
- Triangulo: iniciar (hace falta haber puesto el cerdito)
- Circulo: volver a construir

Barra verde arriba = llegaste a la meta. Barra roja = perdiste.
Armado que funciona: caja abajo en el centro, una rueda a cada lado, el cerdito arriba.

## Limitaciones conocidas
- La fisica usa Box2D estandar, no el codigo original (esta ofuscado), asi que el movimiento no es identico.
- El vehiculo recibe un empujon inicial al arrancar para salir de la plataforma.
- La meta es aproximada (el final de la rampa) y el terreno se dibuja con colores planos, no con la textura original.
- Solo existe el nivel 1; no hay menus ni sonido todavia.
