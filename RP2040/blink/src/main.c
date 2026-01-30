/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS 1000

// Helper macro to simplify GPIO setup
#define INIT_GPIO(name, flags)                                                        \
    const struct gpio_dt_spec name = GPIO_DT_SPEC_GET_OR(DT_ALIAS(name), gpios, {0}); \
    do {                                                                              \
        if(!device_is_ready(name.port)) {                                             \
            printk("Error: " #name " device not ready\n");                            \
            return 1;                                                                 \
        }                                                                             \
        gpio_pin_configure_dt(&name, flags);                                          \
    } while(0)

/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */

int main(void) {
    int ret;
    bool led_state = true;

    // // Inputs with pull-up
    INIT_GPIO(pin_button, GPIO_INPUT | GPIO_PULL_UP);
    INIT_GPIO(pin_boot, GPIO_INPUT | GPIO_PULL_UP);
    INIT_GPIO(pin_reset_viewer, GPIO_INPUT);

    // // Output pin for reset
    INIT_GPIO(pin_reset, GPIO_OUTPUT);
    gpio_pin_set_dt(&pin_reset, 1);

    // LEDs
    INIT_GPIO(led0, GPIO_OUTPUT);
    INIT_GPIO(led1, GPIO_OUTPUT);
    INIT_GPIO(led2, GPIO_OUTPUT);
    gpio_pin_set_dt(&led0, 0);
    gpio_pin_set_dt(&led1, 0);
    gpio_pin_set_dt(&led2, 0);

    // RF switch outputs
    INIT_GPIO(ctf1, GPIO_OUTPUT);
    INIT_GPIO(ctf2, GPIO_OUTPUT);
    INIT_GPIO(ctf3, GPIO_OUTPUT);
    gpio_pin_set_dt(&ctf1, 0);
    gpio_pin_set_dt(&ctf2, 0);
    gpio_pin_set_dt(&ctf3, 0);

    // // cJTAG inputs (11 to 14)
    INIT_GPIO(cjtag0, GPIO_INPUT);
    INIT_GPIO(cjtag1, GPIO_INPUT);
    INIT_GPIO(cjtag2, GPIO_INPUT);
    INIT_GPIO(cjtag3, GPIO_INPUT);

    printk("GPIO setup complete\n");

    while(1) {
        ret = gpio_pin_toggle_dt(&led0);
        if(ret < 0) {
            return 0;
        }

        led_state = !led_state;
        printf("LED state: %s\n", led_state ? "ON" : "OFF");
        k_msleep(SLEEP_TIME_MS);
    }
    return 0;
}
