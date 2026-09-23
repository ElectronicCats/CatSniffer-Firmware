# Estado del firmware CC1352 y port al CC1352P1

Fecha: 2026-09-17 · Rama: `dev` · SDK de referencia: `simplelink_cc13xx_cc26xx_sdk 8.30.01.01`

Este documento cubre dos cosas: en qué estado está hoy el firmware del CC1352P7
(rama `dev`/`v3.x`) y qué hace falta exactamente para compilar los mismos
firmwares para el CC1352P1 de la CatSniffer v2.

---

## 1. Estado actual del CC1352P7

### 1.1 Firmwares del repositorio

| Firmware | Fuentes | Build Linux | FLASH usado | Estado |
|---|---|---|---|---|
| `airtag_scanner_CC1352P_7` | sí | sí | 134.691 / 712.704 B (18,9 %) | compila |
| `airtag_spoofer_CC1352P_7` | sí | sí | 138.344 / 712.704 B (19,4 %) | compila |
| `justworks_scanner_CC1352P7_1` | sí | sí | 139.046 / 712.704 B (19,5 %) | compila |
| `sniffer_fw_cc1252P_7` | **no** | no | — | solo binario |

`sniffer_fw_cc1252P_7/` contiene únicamente `sniffer_fw_Catsniffer_v3.x.hex` y
metadatos de CCS (`.cproject`, `.ccsproject`, `.launches/`). **No hay ni un
archivo `.c`**: el firmware principal de sniffing no se puede reconstruir desde
este repositorio, ni para P7 ni para P1. Es un binario preconstruido que se
arrastra entre releases.

### 1.2 Sistema de build

[`CC1352P7/linux-build/`](../CC1352P7/linux-build/) compila los tres firmwares BLE
sin Code Composer Studio, con `make` y `tiarmclang`. Los proyectos CCS se dejan
intactos (sus makefiles son de Windows: `SHELL = cmd.exe`, rutas `C:/ti/...`).

- `paths.mk` — rutas de las herramientas, sobreescribibles vía `paths.local.mk`.
- `setup.sh` — instala `tiarmclang` (descarga directa) y **verifica** el SDK.
- `compat_sdk8.h` — se fuerza con `-include` en todas las unidades de
  compilación y puentea los renombres de API del SDK 7.10 → 8.30
  (`DEFAULT_UPDATE_SLAVE_LATENCY` → `..._PERIPHERAL_LATENCY`, etc.). Los
  proyectos están escritos contra el 7.10; esto evita tocar las fuentes.
- `common.mak` — reglas compartidas; cada `projects/<nombre>.mak` aporta
  `PROJECT_DIR`, `TARGET_NAME`, `APP_SOURCES`, `SYSCFG_FILE`, `SDK_REF` y los
  flags opcionales `USE_RCOSC` / `USE_NPI` / `USE_PROFILES` / `USE_MENU`.

### 1.3 Problemas abiertos

1. **`linux-build/` no está trackeado en git.** Aparece como `?? CC1352P7/linux-build/`.
   Mientras no se commitee, ningún CI puede usarlo.
2. **Ningún workflow compila el CC1352.** `firmware-ci.yml` y
   `firmware-release.yml` compilan el firmware Zephyr del **RP2040**
   (`west build -b rpi_pico`). El workflow `cc1352-ci.yml` existe pero está
   bloqueado por el punto 3.
3. **El SDK de TI no tiene descarga anónima.** `setup.sh` instala solo el
   compilador; el SDK 8.30.01.01 es un `.run` detrás del login de ti.com. En CI
   hay que resolverlo con un mirror propio (variable `TI_SDK_URL`) o un runner
   self-hosted.
4. **El build no es reproducible entre directorios.** El mismo commit compilado
   en dos rutas distintas da `.hex` con distinto SHA-256. Compilado dos veces en
   la *misma* ruta sí es byte a byte idéntico. Si hace falta reproducibilidad
   (firmas, verificación de releases), hay que fijar la ruta de build o
   investigar qué la embebe.
5. **`tools/get_hex_files.py` solo emite `board_v3`.** No contempla la v2.

---

## 2. Compilar los mismos firmwares para el CC1352P1

**Resultado: verificado, funciona.** Ambos firmwares compilan y enlazan contra
el mapa de memoria correcto del P1. Lo que sigue es el procedimiento exacto.

### 2.1 Dónde están las fuentes

No están en esta rama. Están en `origin/v1.x/v2.x`, bajo `CC1352P2/`:

| Proyecto v2 | Equivalente P7 |
|---|---|
| `airtag_scanner_CC1352P_2` | `airtag_scanner_CC1352P_7` |
| `airtag_spoofer_CC1352P_2_LAUNCHXL_tirtos7_ticlang` | `airtag_spoofer_CC1352P_7` |
| — | `justworks_scanner_CC1352P7_1` (no existe en v2) |

Cada proyecto v2 trae todo lo necesario: `Application/*.c`, `Startup/main.c`,
`Startup/osal_icall_ble.c`, su `.syscfg` y su propio
`cc13x2_cc26x2_app_tirtos7.cmd`.

### 2.2 La diferencia real entre P7 y P1

Comparando los makefiles de referencia del SDK
(`examples/rtos/LP_CC1352P7_1/...` contra `examples/rtos/CC1352P_2_LAUNCHXL/...`),
la diferencia completa son **dos líneas**:

```diff
- "-I$(SDK)/source/ti/devices/cc13x2x7_cc26x2x7"
+ "-I$(SDK)/source/ti/devices/cc13x2_cc26x2"

- ../../tirtos7/ticlang/cc13x2x7_cc26x2x7_app_tirtos7.cmd
+ ../../tirtos7/ticlang/cc13x2_cc26x2_app_tirtos7.cmd
```

Mismas fuentes, mismos flags, mismo `rom_init.c` (`rom/agama_r1/`), mismo
Cortex-M4F. No hace falta un `compat` distinto: `compat_sdk8.h` sirve igual.

### 2.3 Paso 1 — parametrizar `common.mak`

Sustituir la línea de `SDK_EX` por:

```make
# Board del SDK y familia de dispositivo: P7 por defecto, P1 los sobreescribe.
SDK_BOARD     ?= LP_CC1352P7_1
DEVICE_FAMILY ?= cc13x2x7_cc26x2x7
SDK_EX  := $(SDK)/examples/rtos/$(SDK_BOARD)/ble5stack/$(SDK_REF)
```

y reemplazar los dos usos literales de la familia:

```make
# en CFLAGS
"-I$(SDK)/source/ti/devices/$(DEVICE_FAMILY)" \
# en LFLAGS
$(SDK_EX)/tirtos7/ticlang/$(DEVICE_FAMILY)_app_tirtos7.cmd \
```

El `?=` funciona porque `common.mak` incluye `projects/$(PROJECT).mak` **antes**
de esas asignaciones, así que un proyecto que las defina con `:=` gana.

> Verificado: con este cambio el `.hex` del P7 sale **byte a byte idéntico** al
> anterior (mismo SHA-256, compilando en la misma ruta). No es una regresión.

### 2.4 Paso 2 — añadir los proyectos P1

`projects/airtag_scanner_p1.mak`:

```make
PROJECT_DIR   := airtag_scanner_CC1352P_2
TARGET_NAME   := airtag_scanner_CC1352P1
APP_SOURCES   := Application/airtag_scanner.c
SYSCFG_FILE   := airtag_scanner.syscfg
SDK_REF       := simple_central
SDK_BOARD     := CC1352P_2_LAUNCHXL
DEVICE_FAMILY := cc13x2_cc26x2
```

`projects/airtag_spoofer_p1.mak`:

```make
PROJECT_DIR   := airtag_spoofer_CC1352P_2_LAUNCHXL_tirtos7_ticlang
TARGET_NAME   := airtag_spoofer_CC1352P1
APP_SOURCES   := Application/airtag_spoofer.c
SYSCFG_FILE   := airtag_spoofer.syscfg
SDK_REF       := simple_peripheral
SDK_BOARD     := CC1352P_2_LAUNCHXL
DEVICE_FAMILY := cc13x2_cc26x2
USE_RCOSC     := 1
USE_NPI       := 1
USE_PROFILES  := 1
```

Diferencias con los `.mak` del P7, a tener en cuenta al portar:

- El `.syscfg` del v2 se llama como el proyecto (`airtag_scanner.syscfg`), no
  como el ejemplo del SDK (`simple_central.syscfg`).
- El `.syscfg` del v2 declara `--board "/ti/boards/CC1352P_2_LAUNCHXL"` y
  `--product simplelink_cc13xx_cc26xx_sdk@7.10.01.24`; SysConfig 1.21.1 lo migra
  al 8.30 sin intervención.
- `TARGET_NAME` **debe contener `cc1352p1`** — ver §2.6.

### 2.5 Paso 3 — compilar

```sh
cd CC1352P7/linux-build     # o donde viva el build system unificado
make airtag_scanner_p1
make airtag_spoofer_p1
```

Resultado verificado:

| Firmware | FLASH usado | Mapa de memoria |
|---|---|---|
| `airtag_scanner_CC1352P1` | 137.380 / 352.256 B (39,0 %) | `FLASH 0x00000000 len 0x56000` + `FLASH_LAST_PAGE 0x56000 len 0x2000` |
| `airtag_spoofer_CC1352P1` | 140.868 / 352.256 B (40,0 %) | ídem |

Total `0x58000` = 352 KB, que es exactamente `CC1352P1_FLASH_SIZE` en catnip.
Que el linker use ese mapa y no el de 704 KB es la prueba de que se aplicó la
familia de dispositivo correcta.

### 2.6 Paso 4 — nombrar los artefactos para catnip

catnip clasifica las imágenes por **nombre de archivo**
(`modules/firmware/board.py`):

```python
_P7_MARKERS = ("cc1352p7", "cc1352p_7", "p7_1", "_p7", "cc1352p7_1m")
_P1_MARKERS = ("cc1352p1", "cc1352p_1", "cc2652p1", "_p1")
```

Los directorios del v2 se llaman `CC1352P_2` (P**2**), que **no matchea ninguna
de las dos listas**: `airtag_scanner_CC1352P_2.hex` le queda a catnip sin
clasificar. Por eso los `.mak` de arriba fuerzan `TARGET_NAME := ..._CC1352P1`.

Flashear una imagen P7 en un P1 (o al revés) deshabilita el bootloader serie y
hace falta un programador cJTAG para recuperar la placa. El nombre es lo único
que impide eso, así que no es cosmético.

### 2.7 Pendientes del lado de catnip

- `BOARD_V2.ships_cc1352_hex_assets = False`, con el comentario
  *"tools/get_hex_files.py only emits board_v3"*. Publicar hex de v2 invalida ese
  invariante: hay que ponerlo en `True` y revisar lo que dependa de él.
- `tools/get_hex_files.py` debe emitir también `board_v2`.
- Los releases de v2 se taguean `v2.X.Y.Z` (`BOARD_V2.tag_prefix`), no `v3.`.

---

## 3. Resumen de trabajo pendiente

| # | Tarea | Bloquea |
|---|---|---|
| 1 | Commitear `CC1352P7/linux-build/` | todo lo de CI |
| 2 | Parametrizar `common.mak` (§2.3) | build P1 |
| 3 | Añadir `projects/*_p1.mak` (§2.4) | build P1 |
| 4 | Decidir dónde vive el build unificado (¿mover fuentes v2 a esta rama, o portar `linux-build/` a `v1.x/v2.x`?) | CI de v2 |
| 5 | Resolver la obtención del SDK en CI (mirror o self-hosted) | CI de ambos |
| 6 | `get_hex_files.py` → `board_v2` + `ships_cc1352_hex_assets` en catnip | releases v2 |
| 7 | Recuperar las fuentes de `sniffer_fw` | reconstruir el sniffer |

El punto 4 es la única decisión de diseño real. Las fuentes v2 están en otra
rama, así que hoy compilarlas exige un segundo `checkout`. Unificar ambas
generaciones en un solo árbol simplificaría el CI, pero es un cambio de
estructura del repositorio que conviene decidir aparte.
