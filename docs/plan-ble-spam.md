# Plan de implementación: integración de spam Bluetooth (BLE) en el firmware CC1352P7

> **Repositorio:** CatSniffer-Firmware · **Rama:** `dev` · **Fecha:** 2026-09-22
> **SDK de referencia:** `simplelink_cc13xx_cc26xx_sdk` 8.30.01.01+ (local: 8.33.00.16)
> **Firmware objetivo:** `ble_spam_CC1352P_7` (nuevo) · **Estado:** plan listo para ejecución
> **Fuente funcional:** `Minino/firmware/components/bt_spam/bt_spam.c` + `main/apps/ble/spam/spam_module.c` (ESP32/ESP-IDF)

---

## Índice

1. [Resumen Ejecutivo](#1-resumen-ejecutivo)
2. [Análisis de Arquitectura Actual](#2-análisis-de-arquitectura-actual)
3. [Plan de Implementación en Fases](#3-plan-de-implementación-en-fases)
4. [Progress Log](#4-progress-log)
5. [Apéndices](#5-apéndices)

---

# 1. Resumen Ejecutivo

## 1.1 Objetivo general

Portar la funcionalidad de spam Bluetooth Low Energy basada en `spam_module.c` / `bt_spam.c` del proyecto Minino (ESP32 + Bluedroid) a un **nuevo firmware independiente** del repositorio CatSniffer-Firmware, llamado `ble_spam_CC1352P_7`, que se compile para el CC1352P7 (CatSniffer v3) con el sistema de build en Linux ya existente (`CC1352P7/linux-build/`), siguiendo las convenciones de arquitectura, estilo e integración de los firmwares actuales (`airtag_scanner`, `airtag_spoofer`, `justworks_scanner`).

El nuevo firmware publicitará, de forma continua y rotativa, los payloads de los ~82 modelos soportados por el módulo original —Apple (Proximity 0x07 y Action 0x10), Android/Fast Pair, Windows/Swift Pair y Samsung/Easy Setup— con parámetros de advertising idénticos a la referencia (ADV_IND conectable, 20–30 ms, todos los canales, dirección aleatoria estática rotativa cada ~2 s).

## 1.2 Alcance

**Dentro del alcance:**

- Nuevo proyecto CCS-style `CC1352P7/ble_spam_CC1352P_7/` (Application, Startup, SysConfig).
- Nuevo descriptor de build `CC1352P7/linux-build/projects/ble_spam.mak`.
- Porte de la capa de datos (`spam_models[]`, modos, randomización de payloads) y del motor de advertising al BLE5-Stack de TI (`GapAdv_*`).
- Interfaz mínima: banner y estado por UART (`Display_printf`) + manejo de botones BTN-1/BTN-2 (ya declarados en SysConfig).
- Validación en hardware, documentación (`CC1352P7/README.md`, `docs/estado-firmware-cc1352.md`), CI (matriz de `cc1352-ci.yml`) y Progress Log.

**Fuera de alcance (se documentan como decisiones, no se ejecutan en las fases críticas):**

- Proyecto CCS completo (`.cproject`, `Release/makefile` de Windows): el nuevo firmware se construye primero solo con `linux-build`; añadir metadatos CCS es una tarea opcional de la Fase 7.
- Pantallas OLED, animaciones y menús del Minino (`oled_screen`, `animations_task`, `spam_screens`): no existen en la CatSniffer; se sustituyen por salida por UART.
- Gestión de Wi-Fi (`wifi_controller`): no aplica al CC1352P7 (solo BLE en este firmware).
- Perfil P1 (CatSniffer v2 / CC1352P1): opcional, Fase 7.
- Uso del módulo como librería compartida entre firmwares: se incorpora como código propio del proyecto (convención del repo: cada firmware es autocontenido).

## 1.3 Riesgos y consideraciones principales

| # | Riesgo | Impacto | Prob. | Mitigación |
|---|---|---|---|---|
| R1 | Semántica del API `GapAdv` difiere de Bluedroid: actualizar datos exige `GapAdv_prepareLoadByHandle` → `GapAvd_loadByHandle` (con `bleAlreadyInRequestedMode` si el set no está activo) y el reenable es automático | Alto | Media | Implementar la secuencia documentada en `gap_advertiser.h:863-968` con rama de fallback `disable → load → enable`; validar con captura RF |
| R2 | Rotación de MAC en caliente vía `HCI_LE_SetRandomAddressCmd` (`hci.h:1820`) con sets de advertising activos | Alto | Media | Rotar solo durante la ventana de `GapAdv_disable` de cada ciclo (mismo patrón que Minino: stop → set addr → start); fallback: dirección fija (los payloads ya rotan bytes efímeros por ciclo) |
| R3 | Compilar ≠ validar: los proyectos están escritos para SDK 7.10 y el build usa 8.x (`README` de linux-build) | Medio | Alta | Toda la validación funcional se hace en hardware con el SDK 8.33 local; `compat_sdk8.h` absorbe renombres puntuales |
| R4 | Aleatoriedad: no existe `esp_random`; el driver disponible es TRNG (`ti/drivers/TRNG.h`, ya habilitado en SysConfig como `CONFIG_TRNG_0`) | Medio | Alta | Sembrar un PRNG rápido (xorshift) con `TRNG_generateKey()` una vez en `BleSpam_init()`; no depender de TRNG por cada byte |
| R5 | El advertising a 20 ms en los 3 canales eleva consumo y temperatura del amplificador de 20 dBm | Medio | Media | Pruebas de soak de 30–60 min; opción de perfil de potencia/intervalo (igual que Minino) en Fase 7 |
| R6 | Las conexiones entrantes (ADV_IND es conectable) interrumpen el ciclo de spam | Medio | Alta | Manejar `GAP_LINK_ESTABLISHED_EVENT` con `GAP_TerminateLinkReq()` inmediato y re-habilitar el set en `GAP_LINK_TERMINATED_EVENT` |
| R7 | CI no puede descargar el SDK de ti.com sin `TI_SDK_URL` (issue conocido, doc `estado-firmware-cc1352.md`) | Bajo | Alta | No bloqueante: el build local es la fuente de verdad; añadir `ble_spam` a la matriz CI de todas formas |
| R8 | `CC1352P7/linux-build/` sigue **sin trackear en git** (`?? CC1352P7/linux-build/`) | Alto | Alta | Commitear el árbol de build (con `out/` en `.gitignore`) antes o junto con el `.mak` nuevo, o el firmware no será reproducible |
| R9 | Uso no autorizado del spam (disrupción de dispositivos cercanos) | Legal | Media | Disclaimer de uso restringido a entornos controlados/autorizados en `CC1352P7/README.md`; el repo ya publica `airtag_spoofer` con el mismo criterio |
| R10 | Estimación de ahorro incorrecta al limpiar `USE_NPI`/`USE_PROFILES` y romper el link | Bajo | Media | Limpiar flags solo en Fase 6, tras tener el firmware validado en hardware |

## 1.4 Timeline estimado (alto nivel)

| Fase | Nombre | Sesiones est. (2–4 h c/u) | Acumulado | Ventana (ritmo 1 sesión/día hábil) |
|---|---|---|---|---|
| 0 | Preparación del entorno | 0,5 | 0,5 | Semana 1 |
| 1 | Esqueleto del firmware `ble_spam` | 1 | 1,5 | Semana 1 |
| 2 | Porte de modelos y payloads | 1,5 | 3 | Semana 1–2 |
| 3 | Motor de advertising y rotación de MAC | 3–4 | 6–7 | Semana 2–3 |
| 4 | Modos, botones e interfaz UART | 1,5 | 8,5 | Semana 3 |
| 5 | Testing y validación en hardware | 2–3 | 10,5–11,5 | Semana 3–4 |
| 6 | Integración final (docs, CI, commits) | 1 | 11,5–12,5 | Semana 4 |
| 7 | Opcional: perfil P1, limpieza, CCS | 1–2 | — | Post-lanzamiento |

> **Total estimado: 12–13 sesiones (~4 semanas)** con hardware disponible desde la Fase 5. La Fase 3 es la ruta crítica; las Fases 0–2 pueden comprimirse ejecutando tareas en paralelo con la Fase 3 (la capa de modelos, Fase 2, es portable y no depende del toolchain).

---

# 2. Análisis de Arquitectura Actual

## 2.1 Estructura del repositorio

```
CatSniffer-Firmware/
├── README.md                     # Mapa de versiones (v1.x/v2.x = CC1352P1, v3.x = CC1352P7)
├── compile_cc1352p7.sh           # Wrapper de build: -f <fw>, -a, -l, --check, --setup, -o
├── .clang-format                 # Estilo C/C++ (usado por pre-commit, --style=file)
├── .pre-commit-config.yaml       # check-yaml, EOF, trailing-ws, clang-format, conv. commits
├── .github/workflows/
│   ├── cc1352-ci.yml             # Matriz de build CC1352 (P7 local + P1 opcional)
│   └── ...                       # firmware-ci, release, pre-commit
├── docs/
│   └── estado-firmware-cc1352.md # Estado conocido del firmware (tabla de firmwares, issues)
├── RP2040/                       # Coprocesador USB (no interviene en este plan)
├── tools/
└── CC1352P7/
    ├── README.md                 # Lista de firmwares de la v3.x (aquí se documenta el nuevo)
    ├── airtag_scanner_CC1352P_7/ # Central (simple_central): Application/, Startup/, .syscfg, Release/
    ├── airtag_spoofer_CC1352P_7/ # Periférico (simple_peripheral): la mejor plantilla para spam
    ├── justworks_scanner_CC1352P7_1/
    ├── sniffer_fw_cc1252P_7/     # Solo hex, sin fuentes
    └── linux-build/
        ├── Makefile              # Descubre projects/*.mak automáticamente (make list/all)
        ├── common.mak            # Reglas compartidas (SysConfig, tiarmclang, tiarmhex)
        ├── paths.mk              # Detección de SDK/SysConfig/XDC/tiarmclang bajo $TI_ROOT
        ├── compat_sdk8.h         # -include forzado: puentea renombres API 7.10 → 8.x
        ├── setup.sh              # Instala tiarmclang / verifica SDK
        ├── README.md             # Cómo añadir un firmware (variables de projects/*.mak)
        └── projects/
            ├── airtag_scanner.mak / _p1.mak
            ├── airtag_spoofer.mak / _p1.mak
            └── justworks_scanner.mak / _p1.mak
```

**Añadir un firmware nuevo = crear `projects/<nombre>.mak`**, porque el `Makefile` hace `wildcard projects/*.mak`. Ese archivo aporta las variables que `common.mak` necesita:

| Variable | Uso | Valor propuesto para `ble_spam` |
|---|---|---|
| `PROJECT_DIR` | Directorio CCS con las fuentes | `ble_spam_CC1352P_7` |
| `TARGET_NAME` | Base del `.hex` (catnip lo clasifica por el nombre; debe contener `cc1352p_7`) | `ble_spam_CC1352P_7` |
| `APP_SOURCES` | Fuentes de la aplicación (lista, relativa a `PROJECT_DIR`) | `Application/ble_spam.c Application/ble_spam_models.c` |
| `SYSCFG_FILE` | Fichero SysConfig | `ble_spam.syscfg` |
| `SDK_REF` | Ejemplo del SDK del que cuelga boilerplate (`osal_icall_ble.c`, linker cmd) | `simple_peripheral` |
| `USE_RCOSC` / `USE_NPI` / `USE_PROFILES` / `USE_MENU` | Módulos extra (rcosc_calibration, NPI+icall_hci_tl, devinfoservice+simple_gatt_profile, board_key+two_btn_menu) | `USE_RCOSC := 1`, `USE_NPI := 1`, `USE_PROFILES := 1` (Fase 1, clon exacto del spoofer); `USE_MENU := 1` en Fase 4 |
| `SDK_BOARD` / `DEVICE_FAMILY` | Solo variantes P1 (por defecto `LP_CC1352P7_1` / `cc13x2x7_cc26x2x7`) | heredados por defecto |

Salida: `out/<project>/<TARGET_NAME>.hex` (los objetos se aplanan por basename: los nombres de fichero `.c` deben ser únicos en todo el build).

## 2.2 Convenciones y patrones de los firmwares existentes

**Plantilla de proyecto (tomada de `airtag_spoofer_CC1352P_7/`, la referencia por rol periférico):**

- `Application/<app>.c` + `<app>.h`: encabezado TI BSD (`@file`, `@brief`, Group/Target), prefijo de funciones `<Prefijo>_` (`AirtagSpoofer_createTask`, `AirtagSpoofer_taskFxn`, `AirtagSpoofer_init`, `AirtagSpoofer_advCallback`, `AirtagSpoofer_clockHandler`, `AirtagSpoofer_keyChangeHandler`).
- `Startup/main.c`: únicamente llama a `<Prefijo>_createTask()`.
- `<app>.syscfg`: copia del ejemplo del SDK (`simple_peripheral.syscfg` / `simple_central.syscfg`) con toques propios; declara Display UART (XDS110, 921600), GPIO BTN-1/BTN-2 (`CONFIG_GPIO_BTN1/2`), TRNG (`CONFIG_TRNG_0`) y los adv sets (`ble.advSet1` legacy, `ble.advSet2` extended).
- `targetConfigs/`, `cc13x2x7_cc26x2x7_app_tirtos7.cmd`: paridad CCS (el build de Linux toma el linker cmd del ejemplo del SDK).

**Patrón de aplicación (TI-RTOS + ICall + BLE5-Stack), idéntico en los tres firmwares:**

1. `<App>_createTask()` → `Task_construct` con stack de 1024 words y prioridad 1; el `taskFxn` hace `ICall_registerApp` y entra en bucle `Event_pend(syncEvent, ICALL_TIMEOUT, ...)`.
2. `GAP_DeviceInit()` con `GAP_PROFILE_PERIPHERAL` (spoofer) o `GAP_PROFILE_CENTRAL` (scanners); en `GAP_DEVICE_INIT_DONE_EVENT` se configuran y crean los adv sets con `GapAdv_create(&advCb, &advParams, &advHandle)` (`airtag_spoofer.c:902-953`).
3. Callbacks de GAP en contexto de interrupción → `enqueueMsg` → procesamiento en el task vía `Event_post` (los mensajes `spEvt_t`/`spGapAdvEventData_t`).
4. Temporización: `Util_constructClock` → `clockHandler` → post de evento (patrón del `AirtagSpoofer_clockHandler`).
5. Banner e indicadores por UART: `Display_printf(dispHandle, …, "CatSniffer:AirTagSpoofer", …)`; los scanners usan la macro `BLE_LOG`.
6. Relojes/calibración y enlaces extra: flags `USE_*` que replican `ORDERED_OBJS` del proyecto CCS original.

**Generados de SysConfig** (`out/<fw>/syscfg/ti_ble_config.c|h`): `advParams1` (`.eventProps = GAP_ADV_PROP_CONNECTABLE | GAP_ADV_PROP_LEGACY | GAP_ADV_PROP_SCANNABLE` = ADV_IND, `.primIntMin/Max = 160` = 100 ms, `.primChanMap = GAP_ADV_CHAN_ALL`), `advData1[]`, `scanResData1[]`, `advParams2/advData2` (extended), `pRandomAddress`, `DEFAULT_ADDRESS_MODE`, `DEFAULT_TX_POWER`. La app **sobreescribe estos valores en tiempo de ejecución antes de `GapAdv_create`** (`airtag_spoofer.c:942-946`): es exactamente el punto de inyección para los parámetros de spam.

**Código y calidad:** estilo `.clang-format` (tabs, braces Allman en el código TI existente; el resto del repo clang-format con `--style=file`), pre-commit obligatorio (clang-format + whitespace + conventional commits), CI en `.github/workflows/cc1352-ci.yml` con matriz explícita `project: [airtag_scanner, airtag_spoofer, justworks_scanner]` (**hay que ampliarla**).

**Presupuesto de recursos:** los firmwares actuales usan ~134–139 KB de los 712.704 B de FLASH (18,9–19,5 %); hay holgura sobrada para la tabla de modelos (~3,3 KB en `.rodata`).

## 2.3 El módulo de spam de Minino y sus componentes

La implementación de referencia tiene **tres capas**:

| Capa | Fichero | Responsabilidad | ¿Se porta? |
|---|---|---|---|
| UI/menú | `main/apps/ble/spam/spam_module.c` | Máquina de estados por botones, inicio/parada, registro del callback de pantalla, 5 modos (`ble_spam_apple_begin`, `_android_`, `_windows_`, `_samsung_`, `_all_`); alias legacy `ble_module_begin` | **Comportamiento sí** (modos, lifecycle, callback), APIs ESP no |
| Pantallas | `spam_screens.c/.h` | Animación OLED y textos | **No** (sin OLED); se sustituye por `Display_printf` |
| Motor | `components/bt_spam/bt_spam.c/.h` (1027 líneas) | Tabla de modelos, payloads, ciclo de advertising, MAC rotation | **Sí, es el corazón del porte** |

**Componentes de `bt_spam.h/.c` (a portar):**

- `bt_spam_mode_t` — enum `BT_SPAM_MODE_ALL/APPLE/ANDROID/WINDOWS/SAMSUNG` (+`_MAX`).
- API pública: `bt_spam_app_main(mode)`, `bt_spam_register_cb(cb)`, `bt_spam_app_stop()`, `bt_spam_is_running()`, `bt_spam_app_deinit()`, `bt_spam_set_tx_power()`, `bt_spam_set_adv_interval()`.
- `spam_model_t { name, type, len, data[31] }` y la tabla `spam_models[]` con **82 entradas** repartidas en 5 tipos de payload:
  - `APPLE_PROXIMITY` (Continuity type 0x07), `APPLE_ACTION` (type 0x10),
  - `GOOGLE_FAST_PAIR` (Fast Pair 0xFE2C), `MICROSOFT_SWIFT` (Swift Pair),
  - `SAMSUNG_SETUP` (company 0x0075, macros `SPAM_SAMSUNG_WATCH/BUDS`).
  - Longitudes 15–31 bytes (todas caben en ADV legacy de 31 B).
- `model_matches_mode()` / `rebuild_active_indices()` — filtrado por modo → `active_indices[]`.
- `prepare_payload()` — copia el modelo y **randomiza los bytes efímeros según el tipo** (anti-dedup: los teléfonos descartan notificaciones idénticas consecutivas).
- `generate_random_mac()` — dirección aleatoria estática con `0xC0` OR (tipos válidos 0xC0/0x40).
- Task de advertising (FreeRTOS): dwell de **250 ms** por modelo en rodajas de 50 ms, **rotación de MAC cada 8 ciclos (~2 s)** (`mac_rot_cycle`, línea 690), `esp_ble_gap_config_adv_data_raw` + start si no está activo, semáforo `adv_sem` sincronizado con el callback GAP (`wait_gap_event` con timeout de 50 ms y drenado), estadísticas de heap cada 100 ciclos y callback de display con el nombre del modelo.
- Parámetros de advertising (líneas 42-49): `adv_int_min = 0x20` (20 ms), `adv_int_max = 0x30` (30 ms), `ADV_TYPE_IND` (conectable, obligatorio para los modales Apple/Android/Windows), `BLE_ADDR_TYPE_RANDOM`, `ADV_CHNL_ALL`, filtro `ALLOW_SCAN_ANY_CON_ANY`.
- Perfiles de potencia (TX power + intervalo) y GAP callback encadenado (`chained_gap_cb`).

**Dependencias del original y destino en CatSniffer:**

| Dependencia (`spam_module.c`/`bt_spam.c`) | Estado en CatSniffer |
|---|---|
| `esp_bt`, `esp_gap_ble_api`, `esp_bt_main` (Bluedroid) | → BLE5-Stack TI: `GapAdv_*`, `GAP_*`, `HCI_*` |
| `freertos` (task, semáforo) | → TI-RTOS `Task` + `Clock` + `Event/Queue` (el semáforo desaparece: en TI los callbacks ya encolan mensajes al task) |
| `esp_random` / `esp_fill_random` | → driver TRNG (`CONFIG_TRNG_0` ya en SysConfig) sembrando un PRNG local |
| `gap_dispatcher` (mux de GAP callbacks) | → innecesario: `GapAdv_create` registra un callback por set y `GAP_RegisterForMsgs` en el task |
| `esp_log` | → `Display_printf` / macro `BLE_LOG` |
| `oled_screen`, `spam_screens`, `animations_task`, `menus_module` | → **sin equivalente**: banner + estado por UART, botones BTN-1/BTN-2 |
| `wifi_controller` (`wifi_driver_deinit_if_started`) | → no aplica |
| `esp_heap_caps` (stats de heap) | → opcional/omitido (el firmware es de asignación estática) |
| `esp_power_level_t` (TX power) | → `HCI_EXT_SetTxPowerCmd()` (`hci.h:3237`) o `.txPower` de `advParams1` |

## 2.4 Mapeo técnico ESP32/Bluedroid → CC1352P7/BLE5-Stack

| # | Bluedroid (Minino) | BLE5-Stack TI (CatSniffer) | Verificación / nota |
|---|---|---|---|
| M1 | `esp_ble_gap_config_adv_data_raw(len, data)` | `GapAdv_prepareLoadByHandle(handle, GAP_ADV_DATA_CONTENT, …)` + `GapAdv_loadByHandle(...)` | Verificado en `gap_advertiser.h:863-968`: `prepare` **deshabilita el set si está activo** y `load` lo **rehabilita automáticamente**; si el set no está activo, `prepare` devuelve `bleAlreadyInRequestedMode` → fallback `GapAdv_disable → GapAdv_loadByHandle → GapAdv_enable` |
| M2 | `esp_ble_gap_start/stop_advertising()` | `GapAdv_enable(handle, GAP_ADV_ENABLE_OPTIONS_USE_MAX, …)` / `GapAdv_disable(handle)` | Patrón ya usado en `airtag_spoofer.c` |
| M3 | `esp_ble_gap_set_rand_addr(mac)` | `HCI_LE_SetRandomAddressCmd(pRandAddr)` (`hci.h:1820`) | Ejecutar dentro de la ventana `disable` del ciclo (o fijar en `GAP_DeviceInit` con `ADDRMODE_RANDOM`, como ya hace el spoofer) |
| M4 | `esp_ble_tx_power_set()` | `HCI_EXT_SetTxPowerCmd(txPower)` (`hci.h:3237`) o `.txPower = 0` en `advParams1` (patrón `airtag_spoofer.c:945`) | Índice `HCI_EXT_TX_POWER_*_DBM` |
| M5 | `vTaskDelay(50 ms)` / dwell 250 ms | `Util_constructClock` one-shot de 250 ms → post de `SPAM_CYCLE_EVT` | Mismo patrón que `AirtagSpoofer_clockHandler` |
| M6 | Semáforo `adv_sem` + `wait_gap_event` | **No hace falta**: `advCallback → enqueueMsg → Event_post` en el task | Simplificación arquitectónica respecto al original |
| M7 | `xTaskCreate(start_adv, 4096, prio 5)` | Bucle `taskFxn` existente del clon (`Event_pend`); el ciclo corre como evento dentro de ese task | No crear una segunda task |
| M8 | Inicializar/desinicializar controlador + Bluedroid | `GAP_DeviceInit` / stack siempre residente | `bt_spam_app_deinit` se reduce a parar el clock y deshabilitar el set |
| M9 | `esp_log` + OLED | `Display_printf` (UART 921600 ya configurada) | Throttling: 1 línea/ciclo (4 Hz) es aceptable |
| M10 | `wifi_driver_deinit_if_started()` | — | Eliminar de la copia del módulo |
| M11 | Random: `esp_random()` | `TRNG_open(0, NULL)` + `TRNG_generateKey()` una vez → sembrar xorshift32 | TRNG ya declarado en los `.syscfg` actuales |
| M12 | `adv_type = ADV_TYPE_IND` | `.eventProps = GAP_ADV_PROP_CONNECTABLE \| GAP_ADV_PROP_LEGACY \| GAP_ADV_PROP_SCANNABLE` | Coincide por defecto con SysConfig (`ti_ble_config.c:79-91`); intervalo → `.primIntMin = 0x20`, `.primIntMax = 0x30` (unidades 0,625 ms, mismos valores que el original) |

**Consideraciones de compatibilidad e integración derivadas:**

1. El protocolo de actualización de datos (M1) es el punto crítico: cada 250 ms se recarga el payload del único adv set legacy (`advHandle` de `advSet1`); `advSet2` (extended) queda sin habilitar.
2. La dirección aleatoria rota cada 8 ciclos dentro de la ventana de `disable`; si el stack no lo admite con sets múltiples, fallback a dirección fija.
3. `ADV_IND` conectable implica que un teléfono puede conectar: se debe terminar la conexión al instante para no perder ciclos (R6).
4. La UI del Minino no tiene equivalente: interfaz por UART + botones (BTN-1/BTN-2 ya existen en el `.syscfg`), con modo por defecto de compilación.
5. Ramas de código ESP condicionales (`CONFIG_*`) y dependencias UI se eliminan al portar: la capa portable (`ble_spam_models.c`) no debe incluir ni una cabecera ESP o TI.

---

# 3. Plan de Implementación en Fases

> Las fases son secuenciales salvo indicación. "Sesión" ≈ bloque de trabajo de 2–4 h (con IA asistida).

## Fase 0 — Preparación del entorno

**Objetivos:** garantizar que el toolchain reproduce los builds actuales antes de tocar código; dejar el árbol de build en un estado commiteable.

**Tareas:**

1. Ejecutar `./compile_cc1352p7.sh --check` (o `CC1352P7/linux-build/setup.sh --check`) y confirmar SDK `simplelink_cc13xx_cc26xx_sdk_*` (8.30+), SysConfig 1.21.1 y `tiarmclang` 3.2.2.LTS detectados por `paths.mk`.
2. Establecer línea base: `make list` y `make all` en `CC1352P7/linux-build/`; guardar los tamaños de FLASH de los tres `.hex` actuales (referencia: 134.691 / 138.344 / 139.046 B) para la comparación posterior.
3. Confirmar estado de git: los cambios sin commitear (`airtag_spoofer.c` modificado, `?? CC1352P7/linux-build/`, `?? compile_cc1352p7.sh`, `?? docs/`) y planificar su separación respecto al commit del nuevo firmware (riesgo R8).
4. Instalar/activar pre-commit (`pre-commit install`) y verificar `pre-commit run --all-files` limpio en la base actual.
5. Crear la rama de trabajo: `git checkout -b feat/ble-spam`.

**Dependencias:** acceso al toolchain local; ninguna fase previa.

**Criterios de éxito:**

- `make all` compila los 3 firmwares con los tamaños de referencia.
- `pre-commit run --all-files` sin errores.
- Rama `feat/ble-spam` creada desde `dev`.

**Complejidad: Baja** — solo verificación; se complica solo si el toolchain no está instalado (`--setup` lo resuelve).

---

## Fase 1 — Esqueleto del firmware `ble_spam_CC1352P_7`

**Objetivos:** conseguir un `ble_spam` que **compile y arranque** como clon funcionalmente neutro del spoofer, con el descriptor `.mak` en su sitio y el build autodescubierto.

**Tareas:**

1. Crear `CC1352P7/ble_spam_CC1352P_7/` copiando la estructura de `airtag_spoofer_CC1352P_7/`:
   - `Application/ble_spam.c` / `ble_spam.h` (renombrado desde `airtag_spoofer.*`),
   - `Startup/main.c`,
   - `ble_spam.syscfg` (copia de `simple_peripheral.syscfg`),
   - `targetConfigs/` y el `.cmd` (paridad CCS);
   - **no** copiar `Release/` (los artefactos van a `linux-build/out/`, que está en `.gitignore`).
2. Renombrar prefijos con `grep`-replace verificado: funciones `AirtagSpoofer_ → BleSpam_`; macros/eventos `SP_ → SPAM_` (`SP_ICALL_EVT`, `SP_QUEUE_EVT`, `spEvt_t` → `spAMEvt_t`, etc.) para no colisionar con el prefijo del spoofer en futuros merges.
3. Ajustar `Startup/main.c`: `AirtagSpoofer_createTask()` → `BleSpam_createTask()`.
4. Banner: `Display_printf(…, "CatSniffer:BleSpam", …)` en la línea equivalente a `airtag_spoofer.c` (convención de los firmwares existentes).
5. Quitar del clon la lógica específica de AirTag (rotación de claves/rpa), dejando el adv set 1 con el contenido por defecto de SysConfig (aún sin payloads de spam).
6. Crear `linux-build/projects/ble_spam.mak`:

   ```make
   # BLE spam (rol periférico/broadcaster) — payload cycling Apple/Android/Windows/Samsung.
   #
   # Los módulos extra replican los de airtag_spoofer.mak (ORDERED_OBJS del
   # proyecto CCS de referencia): rcosc_calibration, NPI + icall_hci_tl, y los
   # perfiles devinfoservice y simple_gatt_profile.
   PROJECT_DIR  := ble_spam_CC1352P_7
   TARGET_NAME  := ble_spam_CC1352P_7
   APP_SOURCES  := Application/ble_spam.c Application/ble_spam_models.c
   SYSCFG_FILE  := ble_spam.syscfg
   SDK_REF      := simple_peripheral
   USE_RCOSC    := 1
   USE_NPI      := 1
   USE_PROFILES := 1
   ```

   *(Nota: `ble_spam_models.c` se crea vacío/placeholder en esta fase y se llena en la Fase 2.)*
7. SysConfig: en `ble_spam.syscfg` cambiar `ble.advSet1.advParam1` a intervalo 20–30 ms (o sobreescribir en código, ver Fase 3) y deshabilitar `ble.advSet2` (extended) si el ejemplo la deja activa por defecto.
8. `make list` → aparece `ble_spam`; `make ble_spam` → `out/ble_spam/ble_spam_CC1352P_7.hex`.
9. Registrar la decisión en el Progress Log: **sin proyecto CCS `.cproject` en esta fase** (build de Linux como fuente de verdad; CCS opcional en Fase 7).

**Dependencias:** Fase 0 completa.

**Criterios de éxito:**

- `make ble_spam` genera `.hex` con tamaño comparable al spoofer (~138–145 KB) y sin warnings nuevos de `tiarmclang`.
- `grep -r AirtagSpoofer CC1352P7/ble_spam_CC1352P_7/` → 0 coincidencias; `grep -r "SP_"` dentro del proyecto → solo prefijos renombrados.
- Flash y arranque en hardware (opcional en esta fase, recomendado): banner `CatSniffer:BleSpam` por UART a 921600.
- `make all` sigue verde para los 3 firmwares existentes (regresión).

**Complejidad: Media** — volumen alto de renombrado mecánico, pero bajo riesgo técnico: el clon del spoofer ya enlaza con estos flags; lo único nuevo es el descriptor `.mak` (plantilla de 13 líneas).

---

## Fase 2 — Porte de la capa de modelos y payloads (código portable)

**Objetivos:** extraer de `bt_spam.c` toda la lógica **independiente de plataforma** a `ble_spam_models.c/.h`, comprobable en el host sin toolchain de TI.

**Tareas:**

1. Crear `Application/ble_spam_models.h/.c` con:
   - `typedef enum { BT_SPAM_MODE_ALL, BT_SPAM_MODE_APPLE, BT_SPAM_MODE_ANDROID, BT_SPAM_MODE_WINDOWS, BT_SPAM_MODE_SAMSUNG, BT_SPAM_MODE_MAX } bt_spam_mode_t;`
   - `typedef struct { const char *name; uint8_t type; uint8_t len; uint8_t data[31]; } spam_model_t;`
   - API: `spam_models_init(void)`, `spam_models_rebuild_active(bt_spam_mode_t)`, `spam_models_count(void)`, `spam_models_index(uint8_t)`, `spam_models_prepare(uint8_t active_idx, spam_model_t *out)` (copia + randomización de bytes efímeros por tipo), `spam_models_mode_name(bt_spam_mode_t)`.
2. Portar íntegra la tabla `spam_models[]` (82 entradas: Proximity 0x07, Action 0x10, Fast Pair, Swift Pair, Samsung 0x0075 con macros `SPAM_SAMSUNG_WATCH/BUDS`), **sin alterar bytes de payload**.
3. Portar `model_matches_mode()` y `rebuild_active_indices()` con la misma semántica que el original (APPLE = Proximity+Action; ANDROID = Fast Pair; WINDOWS = Swift Pair; SAMSUNG = 0x0075; ALL = todo).
4. Portar `prepare_payload()` replicando **exactamente** la randomización por tipo del original (índices de bytes efímeros distintos para Apple Proximity, Apple Action, Fast Pair, Swift Pair y Samsung): sin ella, los receptores deduplican y no muestran el modal.
5. Aleatoriedad sin dependencias: `static uint32_t spam_models_rand(void)` con xorshift32, **sembrable** vía `spam_models_seed(uint32_t)` (llamada desde `BleSpam_init` con TRNG en la Fase 3; en tests de host, con semilla fija).
6. Restricciones de compilación del fichero: solo `<stdint.h>`, `<string.h>`, `<stddef.h>` — **cero** cabeceras ESP o TI (permitirá el test de host y un futuro re-targeting).
7. Test de host en `tools/`: pequeño `ble_spam_models_test.c` compilado con `gcc` que valida: (a) 82 modelos; (b) `len ≤ 31` y coherencia `len == 1 + data[0]` cuando el modelo es AD-structure completo; (c) recuento por modo (`ALL=82`; contabilizar APPLE/ANDROID/WINDOWS/SAMSUNG reales); (d) con semilla fija, dos llamadas consecutivas a `spam_models_prepare()` del mismo modelo producen distinto payload y ambos `len` correctos.

**Dependencias:** Fase 1 (ficheros en su sitio). Puede ejecutarse en paralelo con el final de la Fase 1: no requiere toolchain TI.

**Criterios de éxito:**

- El test de host compila y pasa los 4 puntos.
- `ble_spam_models.c` no incluye ninguna cabecera ESP/TI (verificable con `grep -E '#include' ble_spam_models.c`).
- Recuentos por modo documentados en el test (se usan en la Fase 5 como esperado).

**Complejidad: Media** — el volumen de datos es grande (82 structs) pero el porte es mecánico; la dificultad está en reproducir con fidelidad la randomización por tipo, que es lo que hace efectivo el spam.

---

## Fase 3 — Motor de advertising: ciclo de payloads y rotación de MAC (ruta crítica)

**Objetivos:** implementar en `ble_spam.c` el ciclo de advertising sobre BLE5-Stack con la misma cadencia que `bt_spam.c`: payload nuevo cada 250 ms, dirección aleatoria rotando cada 8 ciclos (~2 s), ADV_IND 20–30 ms en los 3 canales, sin perder ciclos por conexiones entrantes.

**Tareas:**

1. **Aleatoriedad:** en `BleSpam_init()`, abrir TRNG (`TRNG_open(0, NULL)`, `CONFIG_TRNG_0` ya existe en el `.syscfg`) y sembrar `spam_models_seed()` con `TRNG_generateKey()`; cerrar/evitar dependencia de TRNG por cada byte.
2. **Dirección de origen:** inicializar GAP con `ADDRMODE_RANDOM` y una dirección estática aleatoria con bits `0xC0` (equivalente a `generate_random_mac()`); registrar la dirección efectiva en el log de arranque (la lee `GAP_DEVICE_INIT_DONE_EVENT`, patrón `airtag_spoofer.c:902`).
3. **Creación del adv set:** en `GAP_DEVICE_INIT_DONE_EVENT`:
   - copiar `advParams1` y sobreescribir: `.eventProps = GAP_ADV_PROP_CONNECTABLE | GAP_ADV_PROP_LEGACY | GAP_ADV_PROP_SCANNABLE` (ADV_IND), `.primIntMin = 0x20`, `.primIntMax = 0x30` (20–30 ms), `.primChanMap = GAP_ADV_CHAN_ALL`, `.filterPolicy = GAP_ADV_WL_POLICY_ANY_REQ`;
   - `GapAdv_create(&BleSpam_advCallback, &params, &advHandle)`, `GapAdv_setEventMask(...)` con `START_AFTER_ENABLE | END_AFTER_DISABLE | SET_TERMINATED`, carga inicial del primer payload y `GapAdv_enable(...)`.
4. **Ciclo de dwell:** `Util_constructClock` one-shot de **250 ms** en `BleSpam_init`; en `BleSpam_clockHandler` postear `SPAM_CYCLE_EVT` al queue del task (patrón `AirtagSpoofer_clockHandler`).
5. **Handler de ciclo en el task** (`BleSpam_processSpamCycle`):
   1. avanzar índice activo (`spam_models_index`);
   2. `spam_models_prepare()` (payload aleatorizado);
   3. si `++cycle_count % 8 == 0` → recargar dirección: `GapAdv_disable` → `HCI_LE_SetRandomAddressCmd(new_addr)` → continuar con la recarga (mismo orden stop → addr → start que Minino);
   4. recargar datos: `GapAdv_prepareLoadByHandle` + `GapAdv_loadByHandle` (M1); si `prepare` devuelve `bleAlreadyInRequestedMode` (set no activo), fallback `GapAdv_disable → GapAdv_loadByHandle → GapAdv_enable`;
   5. rearmar el clock de 250 ms;
   6. callback de display: `Display_printf(..., "SPAM: %s (%u/%u)", modelo, idx, total)` con throttling de 1 línea por ciclo.
6. **Manejo de eventos de GAP** en `BleSpam_advCallback → enqueueMsg` (ya existente en el clon):
   - `GAP_EVT_ADV_SET_TERMINATED` / errores → re-habilitar con reintento (backoff de 1 ciclo);
   - `GAP_LINK_ESTABLISHED_EVENT` → `GAP_TerminateLinkReq(connHandle, HCI_DISCONNECT_IMMEDIATELY)` **inmediato** y re-habilitar en `GAP_LINK_TERMINATED_EVENT` (R6);
   - registrar `bleIncorrectMode`/`bleGAPBufferInUse` en el log (diagnóstico de R1/R2).
7. **API de ciclo de vida** (equivalente a `bt_spam.h`): `BleSpam_start(bt_spam_mode_t)` (rebuild de índices + armado del clock + enable), `BleSpam_stop()` (detener clock, `GapAdv_disable`, flag `running`), `BleSpam_is_running()`; mantener vivo el stack (no hay equivalente a `bt_spam_app_deinit`).
8. **Instrumentación:** al arranque imprimir modo, modelo inicial, intervalo y dirección; contador de ciclos en el log cada 100 ciclos (patrón de las stats de heap del original, sin depender de `esp_heap_caps`).
9. **Verificación intermedia por build:** tras esta fase, `make ble_spam` debe seguir dentro del presupuesto de FLASH (< 30 %) — revisar el `.map` (`FLASH … 00021ac0` estilo spoofer).

**Dependencias:** Fase 1 (esqueleto) + Fase 2 (API de modelos). **Ruta crítica**: bloquea Fases 4–6.

**Criterios de éxito:**

- Captura RF (con la propia CatSniffer en modo sniffer/Sniffle o un nRF52 dongle) confirma: intervalo 20–30 ms, PDU ADV_IND legacy en canales 37/38/39, contenido cambiando ~4 veces/s y dirección estática aleatoria cambiando ~cada 2 s.
- Al menos un iPhone muestra el modal Proximity con modo APPLE, y un Android muestra Fast Pair con modo ANDROID (ver matriz de la Fase 5).
- Conexión entrante: si un teléfono conecta, la desconexión es inmediata y el ciclo de spam continúa (no se queda publicitando parado).
- Ejecución continua ≥ 10 min sin assert del stack ni pérdida de ciclos visible en el log.

**Complejidad: Alta** — es la traducción asincrónica completa de Bluedroid al protocolo prepare/load/enable de TI (R1), con rotación de dirección (R2) y conexiones entrantes (R6) intercaladas; exige iteración con captura RF y manejo explícito de códigos de retorno `bStatus_t`. Compensa aplanar el semáforo FreeRTOS con el patrón de cola ya probado del repo.

---

## Fase 4 — Modos, botones e interfaz UART

**Objetivos:** reproducir la UX de `spam_module.c` (inicio/parada y selección de modo) con los recursos reales de la CatSniffer: 2 botones y consola serie.

**Tareas:**

1. Modo por defecto de compilación: `#ifndef BLE_SPAM_DEFAULT_MODE #define BLE_SPAM_DEFAULT_MODE BT_SPAM_MODE_ALL` — permite generar variantes `.hex` por fabricante sin UI.
2. Habilitar `USE_MENU := 1` en `ble_spam.mak` (añade `board_key.c` + `two_btn_menu.c`, ver README de linux-build) y registrar en `BleSpam_init()` `Board_keyInit()` + handler `BleSpam_keyChangeHandler(uint8 keys)` (patrón declarado en los scanners): **BTN-1** = arrancar/parar (`BleSpam_start/stop`), **BTN-2** = ciclar modo (`ALL → APPLE → ANDROID → WINDOWS → SAMSUNG → ALL`). Los pines BTN-1/BTN-2 ya están en `ble_spam.syscfg` (`CONFIG_GPIO_BTN1/2`).
   - Verificar antes (`grep Board_keyInit` de los scanners): los firmwares actuales **declaran** el handler pero no lo registran; en caso de colisión con `two_btn_menu`, registrar solo `Board_keyInit` + handler propio y omitir el menú de dos botones (decisión registrada en el Progress Log).
3. Líneas de estado por UART (reemplazan a `spam_screens`): en arranque banner `CatSniffer:BleSpam <modo>`; en cada cambio de modo; por ciclo la línea `SPAM: <modelo> (<i>/<n>)` implementada en la Fase 3.
4. Sustituir el callback `bt_spam_register_cb(display_cb)` por el equivalente interno: el "callback" apunta a la función de log por UART (mismo flujo que `ble_screens_display_scanning_text` en el original).
5. Estados de error visibles: fallo de `GapAdv_create`/`TRNG_open`, `bStatus_t != SUCCESS` en prepare/load → línea `ERR: <func> <status>`.

**Dependencias:** Fase 3 (API `BleSpam_start/stop` y callback de display). La Fase 4 puede empezar con la interfaz sobre la API mínima mientras se estabiliza la Fase 3, pero la validación final es posterior.

**Criterios de éxito:**

- Desde arranque en frío: el firmware anuncia en el modo por defecto; BTN-1 detiene (silencio RF verificable) y BTN-1 de nuevo reanuda; BTN-2 cambia de modo y el cambio se refleja en los payloads capturados.
- El log de UART muestra siempre el modo y el modelo actual; sin inundar la consola (> 5 líneas/s).
- `make ble_spam` sin warnings nuevos; tamaño < 30 % de FLASH.

**Complejidad: Media** — el grueso (botones, UART) son patrones ya presentes en el repo; el riesgo está en verificar que `USE_MENU` no colisione con el manejo propio de teclas (decisión con fallback claro).

---

## Fase 5 — Testing y validación en hardware

**Objetivos:** validar el comportamiento RF y de UX en un entorno controlado, con evidencias, y confirmar que no se regresiona nada.

**Tareas:**

1. Flash: `./compile_cc1352p7.sh -f ble_spam` y flasheo con la herramienta `cc2538-bsl` de CatSniffer-Tools (wiki de restauración del CC1352) o desde CCS con `targetConfigs/CC1352P7.ccxml`.
2. **Matriz de validación de modos** (ejecutar y anotar resultado en el Progress Log):

   | Modo | Dispositivo receptor | Evidencia esperada |
   |---|---|---|
   | APPLE | iPhone (iOS 16+) | Modal de emparejamiento proximity/action (AirPods/custom) |
   | ANDROID | Android 8+ con Fast Pair | Notificación Fast Pair |
   | WINDOWS | Windows 10/11 | Notificación Swift Pair |
   | SAMSUNG | Samsung con Easy Setup | Modal Galaxy Buds/Watch |
   | ALL | Sniffer (nRF Connect o CatSniffer) | Alternancia de los 5 tipos con cadencia de 250 ms |

3. **Verificación de parámetros RF** por captura: intervalo 20–30 ms, ADV_IND legacy, canales 37/38/39, rotación de dirección ~2 s, longitudes de payload 15–31 B.
4. **Estabilidad:** soak de 30–60 min en modo ALL; sin asserts, sin paradas del ciclo (contar líneas `SPAM:` del log vs. tiempo esperado ≈ 4/s), sin reinicios por watchdog.
5. **Termal/consumo:** revisar temperatura del chip tras el soak (R5) y, si procede, anotar la necesidad de perfil de potencia (Fase 7).
6. **Regresión:** `make all` en verde y tamaños de los 3 firmwares existentes iguales a la línea base de la Fase 0.
7. **Prueba de parada:** BTN-1 → silencio RF ≥ 30 s; BTN-1 → reanudación correcta.

**Dependencias:** Fases 3 y 4 completas; hardware (CatSniffer v3 + al menos un iPhone, un Android, un PC Windows; Samsung si está disponible).

**Criterios de éxito:**

- Matriz de modos completa con evidencia (capturas/screenshots) o, como mínimo, 3 de 5 filas con dispositivo real y las 5 verificadas por sniffer.
- 5 criterios RF de la tarea 3 verificados.
- Soak ≥ 30 min sin incidencias.
- `make all` regresión verde.

**Complejidad: Media** — técnico pero directo; se vuelve **Alta** solo si no hay acceso a dispositivos de los 4 ecosistemas (mitigación: validar por sniffer y dejar la matriz de dispositivos pendiente como bloqueo documentado).

---

## Fase 6 — Integración final: docs, CI y commits

**Objetivos:** que el nuevo firmware quede integrado "como si siempre hubiera estado": documentado, en CI, formateado y con el log al día.

**Tareas:**

1. **CI:** en `.github/workflows/cc1352-ci.yml` ampliar la matriz `matrix.project` con `ble_spam` (junto a `airtag_scanner, airtag_spoofer, justworks_scanner`); documentar la dependencia del secreto `TI_SDK_URL` (R7).
2. **Docs:**
   - `CC1352P7/README.md`: añadir la línea del nuevo firmware (mismo formato que `airtag_spoofer_CC1352P_7`, con nota de plataformas soportadas);
   - `docs/estado-firmware-cc1352.md`: añadir fila a la tabla de firmwares (con FLASH real leído del `.map`) y mención del nuevo archivo de plan/log;
   - `CC1352P7/linux-build/README.md`: añadir `make ble_spam` al ejemplo de uso si aparece en la sección de firmwares (la lista `make list` es automática).
3. **Aviso de uso:** disclaimer de uso restringido a pruebas autorizadas/entornos controlados en `CC1352P7/README.md` junto a la entrada del firmware (R9).
4. **Limpieza opcional pero recomendada (decisión registrada):** tras validar en Fase 5, intentar `USE_NPI := 0` y `USE_PROFILES := 0` en `ble_spam.mak` (spam no sirve GATT ni PTM); si el link falla, revertir y dejar anotado por qué (R10).
5. **Calidad:** `pre-commit run --all-files` (clang-format sobre los `.c/.h` nuevos, EOF, trailing whitespace) y commit con formato convencional, p. ej. `feat(ble-spam): add BLE spam firmware for CC1352P7`.
6. **Git:** commitear `linux-build/` si sigue sin trackear (R8, issue conocido), asegurar que `out/` y artefactos `Release/` no entran.
7. **Progress Log:** cerrar la sesión final con el estado "integrado" y los pendientes de la Fase 7.

**Dependencias:** Fase 5 (los criterios de éxito documentados son los del testing); la tarea de CI puede hacerse en paralelo con la Fase 5.

**Criterios de éxito:**

- `git grep ble_spam` encuentra entrada en README, doc de estado y workflow de CI.
- `pre-commit run --all-files` limpio; commit convencional en la rama.
- La tabla de `docs/estado-firmware-cc1352.md` refleja el FLASH real del `.hex` nuevo.
- `make all` en CI (localmente) verde con 4 firmwares.

**Complejidad: Baja** — edición de documentación y YAML con plantillas existentes; el único riesgo (secreto de SDK en CI) ya está documentado y no bloquea el build local.

---

## Fase 7 — Opcional: perfil P1, optimizaciones y proyecto CCS

**Objetivos:** extensiones post-lanzamiento, ninguna bloqueante.

**Tareas:**

1. **CatSniffer v2 (CC1352P1):** crear `linux-build/projects/ble_spam_p1.mak` siguiendo el patrón de `justworks_scanner_p1.mak` (mismas fuentes, `SYSCFG_FILE := ble_spam_p1.syscfg` con `SDK_BOARD := CC1352P_2_LAUNCHXL`, `DEVICE_FAMILY := cc13x2_cc26x2`) — requiere crear `ble_spam_p1.syscfg` con el diseño de RF de la v2 y revisar FLASH del P1.
2. **Proyecto CCS:** generar los metadatos `.cproject`/`Release` para construcción desde el IDE (recomendado solo si hay usuarios de CCS activos).
3. **Perfiles de potencia** (heredados de `bt_spam.c`): `BleSpam_set_tx_power()` vía `HCI_EXT_SetTxPowerCmd` y preset de intervalo, accesible por mantener BTN-2 pulsado.
4. **Estadísticas de runtime:** filas UART con ciclos/s y dirección actual.

**Dependencias:** Fases 1–6 cerradas.

**Criterios de éxito:** `make ble_spam_p1` genera `.hex` para P1 (si se acomete) sin romper `make all`.

**Complejidad: Baja–Media** — el patrón P1 ya está resuelto en el repo; solo añadir el `.syscfg` de la v2 y verificar presupuestos de FLASH.

---

# 4. Progress Log

## 4.1 Convención de uso

- **Una entrada por sesión de trabajo**, añadidas al final de esta sección (las más nuevas abajo, para poder leer solo las 1–2 últimas y saber el estado).
- Formato: copiar la **plantilla de la §4.3**, rellenar y pegar debajo de la última entrada.
- Campos obligatorios: número de sesión consecutivo, fecha ISO-8601, fase en curso (número y nombre), tareas completadas (viñetas concreto con ficheros/comandos), bloqueos/decisiones, próximos pasos (han de ser tareas de la §3 identificables) y notas.
- Regla: si una sesión no empieza leyendo las **dos últimas entradas**, algo falla.

## 4.2 Entradas

### Session #1 — 2026-09-22

- **Fase en curso:** Fase 0 (Preparación) + elaboración de este plan.
- **Tareas completadas en esta sesión:**
  - Lectura y análisis de las tres fuentes: `CC1352P7/linux-build/README.md` (sistema de build), `Minino/.../spam/spam_module.c` + `Minino/.../components/bt_spam/bt_spam.c|.h` (motor de spam: 82 modelos, 5 tipos, dwell 250 ms, MAC cada 8 ciclos), y los firmwares `airtag_scanner`, `airtag_spoofer`, `justworks_scanner` (patrones de app TI-RTOS, SysConfig, `.mak`, CI).
  - Verificación técnica de APIs TI para el porte: `GapAdv_prepareLoadByHandle`/`GapAdv_loadByHandle` (`gap_advertiser.h:863-968`), `HCI_LE_SetRandomAddressCmd` (`hci.h:1820`), `HCI_EXT_SetTxPowerCmd` (`hci.h:3237`), estructura `advParams1` (`ti_ble_config.c:79`), driver TRNG disponible y ya declarado en los `.syscfg`.
  - Este documento: `docs/plan-ble-spam.md`.
- **Bloqueos o decisiones pendientes:**
  - **Decisión D1:** nombre `ble_spam_CC1352P_7` / prefijos `BleSpam_` + macros `SPAM_` (pendiente de confirmar en Fase 1).
  - **Decisión D2:** sin proyecto CCS `.cproject` inicial (solo `linux-build`); revisar en Fase 7.
  - **Decisión D3:** `USE_NPI`/`USE_PROFILES` = 1 en Fase 1 (clon exacto del spoofer), posible limpieza en Fase 6 tarea 4.
  - **Decisión D4:** UI = modo por defecto de compilación + BTN-1/BTN-2; confirmar compatibilidad de `USE_MENU`/`board_key` en Fase 4 (los scanners declaran `keyChangeHandler` pero no registran `Board_keyInit`).
  - **Bloqueo B1:** `CC1352P7/linux-build/` sigue sin trackear en git (R8) — decidir en Fase 0 si se commitea antes de empezar.
- **Próximos pasos (para Session #2):** ejecutar Fase 0 completa (tareas 1–5) y arrancar Fase 1 (tareas 1–4: crear directorio del proyecto, renombrado de prefijos, `ble_spam.mak`, primer `make ble_spam`).
- **Notas adicionales:** la Fase 2 (modelos portables) es independiente del toolchain y puede hacerse en paralelo si se quiere acortar el timeline; el CI nuevo solo funcionará si el repo tiene configurado `TI_SDK_URL`.

### Session #2 — 2026-09-22

- **Fase en curso:** Fase 0 — Preparación del entorno + Fase 1 — Esqueleto del firmware `ble_spam` (§3), ambas completadas.
- **Tareas completadas en esta sesión:**
  - Fase 0 t.1: `./compile_cc1352p7.sh --check` → SDK `simplelink_cc13xx_cc26xx_sdk_8_33_00_16`, SysConfig 1.21.1 y `tiarmclang` 3.2.2.LTS detectados por `paths.mk`.
  - Fase 0 t.2: línea base con `make list` + `make all` en verde. FLASH medido en la columna `used` de `FLASH` del `.map`: `airtag_scanner` 134.571 B (`0x20dab`), `airtag_spoofer` 138.356 B (`0x21bf4`), `justworks_scanner` 138.922 B (`0x21eaa`), más 88 B de `.ccfg` en `FLASH_LAST_PAGE`. (La tabla de `docs/estado-firmware-cc1352.md` cita 134.691/138.344/139.046 B de una medición anterior; para la regresión se comparan siempre valores linux-build contra linux-build.)
  - Fase 0 t.3: estado de git confirmado: `M airtag_spoofer.c` (cambios locales propios, **sin tocar**), `?? CC1352P7/linux-build/`, `?? compile_cc1352p7.sh`, `?? docs/`, `?? .github/workflows/cc1352-ci.yml`, `?? justworks_scanner_p1.syscfg` → commiteados al final de esta sesión en commits separados (B1 más abajo).
  - Fase 0 t.4: `pre-commit install` + `pre-commit run --all-files`. La primera pasada reformateó uno o más ficheros (candidato único: `compat_sdk8.h`, el único `.c/.h` sin trackear entonces; `git diff` no lo puede confirmar); la segunda pasada pasa limpia (`exit=0`).
  - Fase 0 t.5: rama `feat/ble-spam` creada desde `dev`.
  - Fase 1 t.1: creado `CC1352P7/ble_spam_CC1352P_7/` desde el spoofer: `Application/ble_spam.c|.h`, `Startup/main.c`, `ble_spam.syscfg`, `cc13x2x7_cc26x2x7_app_tirtos7.cmd` y `targetConfigs/`; **sin** `Release/` ni metadatos CCS (D2).
  - Fase 1 t.2–4: renombrado verificado con grep: 161 ocurrencias `BleSpam_`, 102 `SPAM_`, tipos `sp*_t → spam*_t` y `spTask[Stack] → spamTask[Stack]`, banner `Display_printf(…, "CatSniffer:BleSpam")`; `grep -r AirtagSpoofer CC1352P7/ble_spam_CC1352P_7/` → 0 y `grep -rn "SP_"` → 0; `Startup/main.c` llama a `BleSpam_createTask()`.
  - Fase 1 t.5: lógica AirTag/RPA eliminada del clon: bloque de identidad Find My (`airtagPubKeyHead`, `airtagRandomAddr`, derivación de MAC), `BleSpam_updateRPA()` + `clkRpaRead`/`argRpaRead`/`rpa[]`, evento `SPAM_READ_RPA_EVT` (renumeración: `SPAM_SEND_PARAM_UPDATE_EVT=7`, `SPAM_CONN_EVT=8`, `APP_EVT_EVENT_MAX=0x8`, cadenas de evento re-alineadas), `airtag_adv_data[]` → carga de `advData1` (contenido por defecto de SysConfig) y adv set #2 (extended) fuera del código (solo queda `advHandleLegacy`).
  - Fase 1 t.6: creado `CC1352P7/linux-build/projects/ble_spam.mak` (13 líneas, `APP_SOURCES := Application/ble_spam.c Application/ble_spam_models.c`, `USE_RCOSC/NPI/PROFILES := 1`); `Application/ble_spam_models.c` como placeholder compilable a rellenar en la Fase 2.
  - Fase 1 t.7: intervalo de adv 20–30 ms por sobreescripción en código (`advParams1.primIntMin/Max = 0x20/0x30`, opción "en código" que ya anticipa la Fase 3) y líneas `ble.advSet2.*` eliminadas de `ble_spam.syscfg` (quedan 15 líneas `advSet`, todas de `advSet1`).
  - Fase 1 t.8: `make list` → `ble_spam` descubierto por el `wildcard`; `make ble_spam` → `out/ble_spam/ble_spam_CC1352P_7.hex` sin avisos de `tiarmclang`; FLASH 137.939 B (`0x21ad3`, 19,4 % de 712.704 B) — dentro del objetivo ~138–145 KB y < 30 %.
  - Regresión: `make clean && make all -j4` → los 7 targets (3 P7 + 3 P1 + `ble_spam`) en verde y los 3 firmwares previos con FLASH **idéntico** a la línea base (134.571 / 138.356 / 138.922 B).
  - Calidad: `pre-commit run --all-files` limpio tras añadir los ficheros nuevos.
  - Fase 1 t.9: esta entrada del Progress Log (decisión D2 registrada: sin proyecto CCS en esta fase).
  - Commits creados al final de la sesión (adelantando Fase 6 t.5, a petición): `feat(build): add Linux build system for the CC1352P7 firmwares` · `ci: add CC1352P7 firmware build workflow` · `feat(airtag-spoofer): derive BLE address from the impersonated Find My key` · `feat(ble-spam): add BLE spam firmware skeleton for CC1352P7` · `docs: add firmware status doc and BLE spam implementation plan`.
- **Bloqueos o decisiones pendientes:**
  - Decisión D1: **confirmada** — `ble_spam_CC1352P_7`, prefijos `BleSpam_`, macros `SPAM_`. El plan escribía `spEvt_t → spAMEvt_t`; interpretado como errata y aplicado `spamEvt_t`/`spam*_t` (coherente con `SPAM_`); revertir es un rename mecánico si se prefiere el literal.
  - Decisión D2: **confirmada** — sin `.cproject`/`Release/` de CCS; `linux-build` es la fuente de verdad (CCS opcional en Fase 7).
  - Decisión D5 (nueva): aunque se borraron las líneas `ble.advSet2.*` del `.syscfg`, la instancia la crea el módulo `ble` por defecto y `ti_ble_config.c` sigue generando `advParams2`/`advData2` (~12 B en `.rodata`, sin uso). No hay RF extended porque el código nunca crea ni habilita ese set; limpieza residual opcional en Fase 7.
  - Bloqueo B1 (R8): **resuelto en esta sesión** — 5 commits locales en `feat/ble-spam` (sin push) recogen build system, CI, el cambio del spoofer, el esqueleto `ble_spam` y esta documentación; el paso de commit de la Fase 6 t.5 queda anticipado (con alcance de esqueleto).
  - Pendiente de hardware (opcional en Fase 1): flashear y confirmar el banner `CatSniffer:BleSpam` por UART a 921600.
- **Próximos pasos (para Session #3):**
  1. Fase 2, t.1–6: crear `Application/ble_spam_models.h/.c` con `spam_models[]` (82 entradas, bytes sin alterar), `model_matches_mode()`/`rebuild_active_indices()`, `prepare_payload()` con la randomización exacta por tipo y PRNG xorshift32 sembrable (`spam_models_seed`); solo `<stdint.h>/<string.h>/<stddef.h>`.
  2. Fase 2, t.7: test de host `tools/ble_spam_models_test.c` con `gcc -Wall -Wextra` validando (a) 82 modelos, (b) `len ≤ 31`, (c) recuentos por modo, (d) payloads distintos con semilla fija; verificar `grep -E '#include' …/ble_spam_models.c` sin cabeceras ESP/TI.
- **Notas adicionales:**
  - El adv set 1 publicita ahora el contenido por defecto de SysConfig (Flags + nombre corto "SP" + UUID 0xFFF0): suficiente para verificar arranque y parámetros RF; los payloads de spam entran en la Fase 3.
  - La Fase 2 no necesita el toolchain de TI y puede ejecutarse en paralelo con el arranque de la Fase 3 (ruta crítica).
  - Decisión D3 intacta: `USE_NPI`/`USE_PROFILES` = 1 por ahora (clon exacto del spoofer); la limpieza se prueba en la Fase 6 tarea 4 (R10).

## 4.3 Plantilla para copiar en próximas sesiones

```markdown
### Session #N — AAAA-MM-DD

- **Fase en curso:** Fase X — <nombre de la fase> (§3)
- **Tareas completadas en esta sesión:**
  - <qué se hizo, con ruta de fichero o comando exacto>
  - <verificación realizada y su resultado>
- **Bloqueos o decisiones pendientes:**
  - Bloqueo Bn: <qué frena> → <quién/qué lo desbloquea>
  - Decisión Dn: <opciones> → <decidido/no decidido>
- **Próximos pasos (para Session #N+1):**
  1. <tarea concreta de la §3, Fase X, tarea Y>
  2. <verificación que debe pasar para cerrarla>
- **Notas adicionales:**
  - <cambios de scope, lecciones aprendidas, enlaces a evidencias>
```

---

# 5. Apéndices

## Apéndice A — Comandos de build y utilidades

```sh
# --- Entorno (una vez) -------------------------------------------------
./compile_cc1352p7.sh --setup            # instala tiarmclang y verifica el SDK
cd CC1352P7/linux-build && ./setup.sh --check

# --- Build -------------------------------------------------------------
make list                                # firmwares disponibles (debe mostrar ble_spam)
make ble_spam                            # compila solo el nuevo firmware
make all                                 # regresión: los 4 firmwares
make clean                               # borra out/
# Alternativa desde la raíz del repo:
./compile_cc1352p7.sh -f ble_spam -o ~/builds    # compila y copia el .hex
./compile_cc1352p7.sh -a -j 4                      # todos en paralelo

# --- Salidas y verificación de tamaño ---------------------------------
ls -la CC1352P7/linux-build/out/ble_spam/ble_spam_CC1352P_7.hex
grep -A1 "FLASH" CC1352P7/linux-build/out/ble_spam/*.map | head

# --- Flasheo -----------------------------------------------------------
# Serial bootloader (CatSniffer-Tools / cc2538-bsl), ver wiki "Restore CC1352":
#   cc2538-bsl -p /dev/ttyUSB0 -evw CC1352P7/linux-build/out/ble_spam/ble_spam_CC1352P_7.hex
# Desde CCS/XDS110: targetConfigs/CC1352P7.ccxml

# --- Calidad -----------------------------------------------------------
pre-commit run --all-files                # clang-format + whitespace
clang-format -i CC1352P7/ble_spam_CC1352P_7/Application/*.c
gcc -Wall -Wextra -o /tmp/spam_test tools/ble_spam_models_test.c \
    CC1352P7/ble_spam_CC1352P_7/Application/ble_spam_models.c  # test de host (Fase 2)
```

## Apéndice B — Estructura de archivos propuesta

```
CC1352P7/
├── ble_spam_CC1352P_7/
│   ├── Application/
│   │   ├── ble_spam.c              # App TI-RTOS: init GAP, adv set, ciclo 250 ms, botones, log
│   │   ├── ble_spam.h              # API pública: BleSpam_createTask/start/stop, bt_spam_mode_t (UI)
│   │   ├── ble_spam_models.c       # PORTABLE: spam_models[82], filtros, randomización, PRNG
│   │   └── ble_spam_models.h       # spam_model_t, bt_spam_mode_t, API de modelos (solo stdint/string)
│   ├── Startup/
│   │   └── main.c                  # Llama a BleSpam_createTask()
│   ├── ble_spam.syscfg             # Derivada de simple_peripheral.syscfg (adv 20-30 ms, TRNG, BTN-1/2, Display UART)
│   ├── cc13x2x7_cc26x2x7_app_tirtos7.cmd
│   └── targetConfigs/CC1352P7.ccxml
├── linux-build/
│   └── projects/
│       └── ble_spam.mak            # PROJECT_DIR/TARGET_NAME/APP_SOURCES/SYSCFG_FILE/SDK_REF/USE_*
└── (Fase 7) projects/ble_spam_p1.mak + Application/ble_spam_p1.syscfg
```

**Regla de build a recordar:** `common.mak` aplana los objetos por basename → los nombres `ble_spam.c` y `ble_spam_models.c` no deben repetirse en ningún otro directorio del proyecto.

## Apéndice C — Checklist de validación final

- [ ] `make list` incluye `ble_spam`.
- [ ] `make ble_spam` genera `out/ble_spam/ble_spam_CC1352P_7.hex` sin warnings nuevos.
- [ ] `make all` mantiene verde los 3 firmwares existentes con tamaños de FLASH ≈ línea base (134.691 / 138.344 / 139.046 B).
- [ ] FLASH de `ble_spam` < 30 % (712.704 B) — leído del `.map`.
- [ ] Sin restos del clon: `grep -r "AirtagSpoofer" CC1352P7/ble_spam_CC1352P_7/` → 0.
- [ ] `ble_spam_models.c` sin cabeceras ESP/TI (`grep '#include' …` solo stdint/string/stddef).
- [ ] Test de host de modelos pasa (82 modelos, `len ≤ 31`, randomización varía payload).
- [ ] Captura RF: ADV_IND legacy, intervalo 20–30 ms, canales 37/38/39.
- [ ] Captura RF: payload cambia ≈ 4 veces/s y dirección aleatoria ≈ cada 2 s (8 ciclos).
- [ ] Matriz de modos §5 completada (o bloqueo documentado por falta de dispositivos).
- [ ] Conexión entrante se termina y el ciclo se reanuda.
- [ ] BTN-1 arranca/para; BTN-2 cicla modos; log UART muestra banner + modelo actual.
- [ ] Soak ≥ 30 min sin assert/reinicio.
- [ ] `pre-commit run --all-files` limpio; commits convencionales.
- [ ] `ble_spam` añadido a la matriz de `.github/workflows/cc1352-ci.yml`.
- [ ] `CC1352P7/README.md` y `docs/estado-firmware-cc1352.md` actualizados (+ disclaimer de uso).
- [ ] `CC1352P7/linux-build/` commiteado; `out/` y `Release/` fuera del commit.
- [ ] Progress Log con entrada de cierre (estado, pendientes Fase 7).

## Apéndice D — Dependencias externas y notas

| Componente | Versión | Origen | Uso en este plan |
|---|---|---|---|
| `simplelink_cc13xx_cc26xx_sdk` | ≥ 8.30.01.01 (probado 8.33.00.16) | ti.com (`.run`, requiere login) | Pila BLE5-Stack, ejemplos `simple_peripheral`, drivers (TRNG), headers `gap_advertiser.h`/`hci.h` |
| SysConfig | 1.21.1 | incluida en el SDK | Genera `ti_ble_config.c|h` y `ti_drivers_config.*` desde `ble_spam.syscfg` |
| TI Arm Clang (`tiarmclang`) | 3.2.2.LTS | `setup.sh` | Compilación y `tiarmhex` |
| Driver TRNG | del SDK (`ti/drivers/TRNG.h`) | ya en SysConfig (`CONFIG_TRNG_0`) | Semilla del PRGN de randomización de payloads |
| `compat_sdk8.h` | local | `-include` en todo el build | Puentea renombres 7.10 → 8.x si el porte los necesita |
| Código de referencia Minino | ESP-IDF/ESP32 | repo externo (no se linkea) | Solo se **portan datos y algoritmos**: `spam_models[]`, `prepare_payload`, cadencia y parámetros de advertising |
| pre-commit + clang-format | v6.0.0 / v21.1.8 | `.pre-commit-config.yaml` | Formato y hooks de commit |
| `cc2538-bsl` | CatSniffer-Tools | wiki del proyecto | Flasheo por serial |

**No se requieren librerías externas nuevas**: todo el spam se implementa con el SDK de TI + código propio. La UI del Minino (OLED/menús) y `gap_dispatcher` se descartan por no tener equivalente ni ser necesarios.

## Apéndice E — Referencias de código verificadas

| Qué | Dónde |
|---|---|
| Cómo se añade un firmware al build | `CC1352P7/linux-build/README.md` (variables `projects/*.mak`), `Makefile` (wildcard), `common.mak` |
| Plantilla `.mak` periférico | `linux-build/projects/airtag_spoofer.mak` (flags `USE_RCOSC/NPI/PROFILES`) |
| Plantilla `.mak` P1 (Fase 7) | `linux-build/projects/justworks_scanner_p1.mak` |
| Ciclo de vida de app TI-RTOS + adv set | `CC1352P7/airtag_spoofer_CC1352P_7/Application/airtag_spoofer.c` (`_createTask`, `_taskFxn`, `_init`, `_advCallback`, `_clockHandler`, `GAP_DEVICE_INIT_DONE_EVENT:902`, `advParams1.*:942-946`) |
| Parámetros de adv por defecto (SysConfig) | `linux-build/out/airtag_spoofer/syscfg/ti_ble_config.c:79` (`eventProps`, `primIntMin=160`) |
| Protocolo de recarga de payloads | `gap_advertiser.h:863-968` (`GapAdv_prepareLoadByHandle`, `GapAdv_loadByHandle`, `bleAlreadyInRequestedMode`) |
| Rotación de dirección aleatoria | `hci.h:1820` `HCI_LE_SetRandomAddressCmd(uint8 *pRandAddr)` |
| Potencia TX | `hci.h:3237` `HCI_EXT_SetTxPowerCmd` |
| Driver de aleatoriedad | `ti/drivers/TRNG.h` (`TRNG_open`, `TRNG_generateKey`); `CONFIG_TRNG_0` en los `.syscfg` actuales |
| Desconexión entrante | `gap.h:1576` `GAP_TerminateLinkReq` |
| Referencia funcional del spam | Minino: `firmware/main/apps/ble/spam/spam_module.{c,h}` (UI, 5 modos), `spam_screens.{c,h}` (descartado), `firmware/components/bt_spam/bt_spam.{c,h}` (`spam_models[]` 82 entradas, `prepare_payload`, `generate_random_mac`, dwell 250 ms, `mac_rot_cycle>=8` línea 690, adv params líneas 42-49) |
| Convenciones de documentación/estado | `docs/estado-firmware-cc1352.md` (tabla de firmwares, issues conocidos) |
| CI con matriz a ampliar | `.github/workflows/cc1352-ci.yml:39` (`matrix.project`) |
| Aviso de firmwares y flasheo | `CC1352P7/README.md`, `README.md` raíz (cc2538-bsl, wiki) |
