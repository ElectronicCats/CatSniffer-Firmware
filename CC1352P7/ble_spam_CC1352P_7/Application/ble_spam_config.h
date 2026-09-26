/*
 * ble_spam_config.h — Configuración de compilación portátil de ble_spam.
 *
 * Vive en el árbol de fuentes del proyecto (no en linux-build/), de modo que el
 * acoplamiento "SPAM_WITH_SCAN <-> rol Observer del stack" viaja con el código
 * y no con el sistema de build. Se fuerza al inicio de CADA unidad de
 * traducción (app y fuentes del stack del SDK) vía -include, para que
 * HOST_CONFIG sea coherente en toda la imagen. En linux-build esto lo activa
 * ble_spam.mak con PROJECT_PREINCLUDE := Application/ble_spam_config.h y
 * common.mak lo traduce a un -include genérico; en CCS o cualquier otro build,
 * basta con force-includear este mismo header (Project > Preprocessor >
 * Include).
 *
 * IMPORTANTE: este header se procesa ANTES que bcomdef.h. Por eso aquí no se
 * comprueba el invariante (OBSERVER_CFG aún no existe); solo se (re)define
 * HOST_CONFIG de forma diferida. El invariante de seguridad se verifica en
 * ble_spam.c, ya con las macros del stack disponibles (#error si SPAM_WITH_SCAN
 * quedara compilado sin OBSERVER_CFG en el rol efectivo).
 */
#ifndef BLE_SPAM_CONFIG_H
#define BLE_SPAM_CONFIG_H

/*
 * Coexistencia spam + sniffing pasivo.
 *   1 (por defecto): rol combinado Peripheral+Observer; expone "scan on|off".
 *   0 (opt-out):     binario advertise-only histórico, sin regresión.
 *
 * Opt-out sin editar código: -DSPAM_WITH_SCAN=0 en la línea de compilación
 * (en linux-build: `make ble_spam SPAM_WITH_SCAN=0`). El #ifndef respeta
 * cualquier valor que llegue por -D, así que la línea de comando siempre gana.
 */
#ifndef SPAM_WITH_SCAN
#define SPAM_WITH_SCAN 1
#endif

/*
 * Acoplamiento con el rol del stack. SysConfig emite
 * -DHOST_CONFIG=PERIPHERAL_CFG en ti_utils_build_compiler.opt; cuando el scan
 * está compilado, lo redefinimos al rol combinado para que la jump-table
 * icall-lite incluya el scanner. Como este header se fuerza antes de bcomdef.h,
 * PERIPHERAL_CFG/OBSERVER_CFG aún no están definidos: no pasa nada, la macro
 * solo se expande al USARSE HOST_CONFIG (ya con bcomdef.h incluido). El #undef
 * elimina la definición previa de la línea de comando y evita el warning de
 * redefinición.
 *
 * Este es el único punto donde -DSPAM_WITH_SCAN y el override de HOST_CONFIG
 * quedan acoplados, y está a nivel de código: es imposible pedir scan sin, en
 * el mismo paso, pedir el rol Observer.
 */
#if SPAM_WITH_SCAN
#undef HOST_CONFIG
#define HOST_CONFIG (PERIPHERAL_CFG | OBSERVER_CFG)
#endif

#endif // BLE_SPAM_CONFIG_H
