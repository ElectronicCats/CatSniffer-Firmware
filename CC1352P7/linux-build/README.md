# Build de los firmwares del CC1352P7 en Linux

Compila los firmwares BLE de este directorio sin Code Composer Studio, con
`make` y el compilador de TI por línea de comandos.

Los proyectos CCS (`.cproject`, `Release/makefile`) se dejan como están: sus
makefiles son de Windows (`SHELL = cmd.exe`, rutas `C:/ti/...`) y siguen
sirviendo para quien compile desde el IDE.

## Requisitos

| Herramienta | Versión | De dónde sale |
|---|---|---|
| simplelink_cc13xx_cc26xx_sdk | 8.30.01.01 o posterior (probado: 8.33.00.16) | instalador `.run` de ti.com |
| SysConfig | 1.21.1 | incluido en el instalador del SDK |
| TI Arm Clang (`tiarmclang`) | 3.2.2.LTS | lo instala `./setup.sh` |

```sh
./setup.sh --check   # informa de lo que falta
./setup.sh           # descarga e instala tiarmclang si no está
```

Las rutas están en [`paths.mk`](paths.mk), que busca bajo `$TI_ROOT` (por
defecto `~/ti`) cualquier `simplelink_cc13xx_cc26xx_sdk_*` instalado y se queda
con el más reciente, junto con el SysConfig y el XDCtools que lo acompañan. No
hay que fijar una versión concreta. Para forzar una ruta sin tocar el archivo
versionado, crea `paths.local.mk` (ignorado por git):

```make
SIMPLELINK_CC13XX_CC26XX_SDK_INSTALL_DIR := /opt/ti/simplelink_cc13xx_cc26xx_sdk_8_33_00_16
```

Ojo con los instaladores de TI: según el `--prefix` que se les pase, crean un
subdirectorio con el nombre del producto dentro del prefijo, así que las rutas
reales pueden quedar duplicadas
(`~/ti/ti-cgt-armllvm_3.2.2.LTS/ti-cgt-armllvm_3.2.2.LTS`). `paths.mk` acepta
tanto esa forma anidada como la plana.

## Uso

```sh
make list                # firmwares disponibles
make airtag_scanner      # compila uno
make all                 # compila todos
make clean               # borra out/
```

El resultado queda en `out/<firmware>/<nombre>.hex`, con el mismo formato Intel
HEX que generaba el post-build de CCS (`tiarmhex -order MS --memwidth=8
--romwidth=8 --intel`), que es el que esperan las herramientas de flasheo.
Junto al `.hex` quedan el `.out` con símbolos y el `.map`.

## Añadir un firmware

Crea `projects/<nombre>.mak`. Para un firmware central basta con:

```make
PROJECT_DIR := mi_firmware_CC1352P_7   # directorio dentro de CC1352P7/
TARGET_NAME := mi_firmware_CC1352P_7   # nombre del .hex y del .out
APP_SOURCES := Application/mi_firmware.c
SYSCFG_FILE := simple_central.syscfg   # relativo a PROJECT_DIR
SDK_REF     := simple_central          # ejemplo del SDK de referencia
```

`SDK_REF` indica de qué ejemplo de
`$SDK/examples/rtos/LP_CC1352P7_1/ble5stack/` se toma el boilerplate que el
proyecto no modifica: `Startup/osal_icall_ble.c` y el linker command file. Se
usa el del SDK y no el del repositorio porque el versionado aquí está escrito
contra el SDK 7.10, donde `GATTServApp_Init()` tenía un parámetro menos.
`Startup/main.c` sí se toma del repositorio, porque llama a la task de la
aplicación.

Si el firmware enlaza módulos extra, actívalos con las banderas que ya entiende
[`common.mak`](common.mak). La lista correcta está en `ORDERED_OBJS` del
`Release/makefile` del proyecto CCS:

```make
USE_RCOSC    := 1   # rcosc_calibration.c
USE_NPI      := 1   # pila NPI + icall_hci_tl.c
USE_PROFILES := 1   # devinfoservice.c + simple_gatt_profile.c
USE_MENU     := 1   # board_key.c + two_btn_menu.c
```

## Diferencias entre el SDK 7.10 y el 8.x

Los proyectos están escritos contra el SDK 7.10.01.24, que TI ya no publica en
su CDN (las versiones archivadas piden cuenta). Este build usa el 8.30.01.01 o
posterior y salva la diferencia sin modificar las fuentes:

- **Renombres de API**: [`compat_sdk8.h`](compat_sdk8.h) se fuerza con
  `-include` en todas las unidades de compilación. El SDK 8.x pasó la
  terminología `master/slave` a `central/peripheral`, y ahí están los alias. Al
  portar un firmware nuevo, un error de `undeclared identifier` que sea un
  renombre simple se arregla añadiendo una línea a ese archivo.
- **`osal_icall_ble.c` y el linker command file**: se toman del SDK (ver
  `SDK_REF` arriba).
- **SysConfig**: migra los `.syscfg` de 7.10 al vuelo, sin intervención.

Los tres firmwares compilan sin advertencias y producen imágenes del mismo
tamaño y rango de direcciones que los `.hex` versionados en `Release/`
(incluida la página de CCFG), pero **no son binarios idénticos**: cambia el SDK.

> Compilar no es validar. Un cambio de dos versiones mayores en un stack BLE
> puede alterar el comportamiento en ejecución aunque el código compile. Prueba
> en hardware antes de publicar un `.hex` generado aquí.

## Qué no cubre

`sniffer_fw_cc1252P_7` no se compila: el repositorio sólo trae el `.hex` de TI,
sin fuentes.
