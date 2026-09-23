/*
 * Compatibilidad SDK 7.10 -> 8.x para los firmwares del CC1352P7.
 *
 * Los proyectos CCS de este repositorio están escritos contra el
 * simplelink_cc13xx_cc26xx_sdk 7.10.01.24. El build de Linux usa el 8.30.01.01
 * o posterior, donde TI renombró parte de la API. Este archivo se fuerza con
 * -include en todas las unidades de compilación para que las fuentes del
 * repositorio no tengan que tocarse y sigan compilando también en CCS con el
 * SDK 7.10.
 *
 * Ojo: al forzarse con -include, esto se procesa ANTES que cualquier cabecera
 * del SDK, así que aquí no se puede consultar con #ifdef lo que define el SDK.
 * Los alias se declaran sin condición y se apoyan en que un macro tipo objeto
 * se expande en el punto de uso, cuando ti_ble_config.h ya está incluido.
 *
 * Al portar un firmware nuevo, los errores de "undeclared identifier" que sean
 * simples renombres se resuelven añadiendo una línea aquí.
 */

#ifndef CATSNIFFER_COMPAT_SDK8_H
#define CATSNIFFER_COMPAT_SDK8_H

/*
 * El SDK 8.x renombró la terminología master/slave a central/peripheral.
 * ti_ble_config.h (generado por SysConfig) define ahora
 * DEFAULT_UPDATE_PERIPHERAL_LATENCY; el nombre anterior desapareció.
 */
#define DEFAULT_UPDATE_SLAVE_LATENCY DEFAULT_UPDATE_PERIPHERAL_LATENCY
#define DEFAULT_DESIRED_SLAVE_LATENCY DEFAULT_DESIRED_PERIPHERAL_LATENCY

#endif /* CATSNIFFER_COMPAT_SDK8_H */
