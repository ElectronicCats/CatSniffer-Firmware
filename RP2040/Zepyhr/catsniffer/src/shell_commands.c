/*
 * shell_commands.c - Command Table Based Shell
 */

#include "shell_commands.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <pico/bootrom.h>

#include "catsniffer.h"

// External functions from main.c
void shell_reply(const char *msg);
void change_mode(unsigned long new_mode);
void change_band(unsigned long new_band);
void process_lora_command(char *cmd_line);
void set_status_leds(int l0, int l1, int l2);

extern catsniffer_t catsniffer;

// Command handler type
typedef void (*cmd_handler_t)(char *args);

// Command table entry
typedef struct {
    const char *name;
    cmd_handler_t handler;
    const char *help;
    bool prefix_match;  // true for commands with args (TX, TEST)
} shell_cmd_t;

// Forward declarations
static void cmd_help(char *args);
static void cmd_boot(char *args);
static void cmd_exit(char *args);
static void cmd_band1(char *args);
static void cmd_band2(char *args);
static void cmd_band3(char *args);
static void cmd_reboot(char *args);
static void cmd_tx(char *args);
static void cmd_test(char *args);
static void cmd_txtest(char *args);
static void cmd_status(char *args);

// Command table
static const shell_cmd_t commands[] = {
    {"help",   cmd_help,   "Show available commands",    false},
    {"boot",   cmd_boot,   "CC1352 bootloader mode",     false},
    {"exit",   cmd_exit,   "Return to passthrough",      false},
    {"band1",  cmd_band1,  "2.4GHz band",                false},
    {"band2",  cmd_band2,  "SUB-GHz band",               false},
    {"band3",  cmd_band3,  "LoRa band",                  false},
    {"reboot", cmd_reboot, "RP2040 USB bootloader",      false},
    {"status", cmd_status, "Device status",              false},
    {"TXTEST", cmd_txtest, "LoRa TX test mode",          false},
    {"TX",     cmd_tx,     "Send LoRa hex packet",       true},
    {"TEST",   cmd_test,   "LoRa test",                  true},
    {NULL,     NULL,       NULL,                         false}
};

// Command implementations
static void cmd_help(char *args) {
    shell_reply("Commands:\r\n");
    for (const shell_cmd_t *cmd = commands; cmd->name != NULL; cmd++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "  %-8s - %s\r\n", cmd->name, cmd->help);
        shell_reply(buf);
    }
}

static void cmd_boot(char *args) {
    change_mode(BOOT);
    set_status_leds(0, 0, catsniffer.mode);
    shell_reply("BOOT\r\n");
}

static void cmd_exit(char *args) {
    change_mode(PASSTHROUGH);
    set_status_leds(0, 0, 0);
    shell_reply("PASSTHROUGH\r\n");
}

static void cmd_band1(char *args) {
    change_band(GIG);
    set_status_leds(0, 0, 0);
    shell_reply("2.4GHz Band\r\n");
}

static void cmd_band2(char *args) {
    change_band(SUBGIG_1);
    set_status_leds(0, 0, 0);
    shell_reply("SUB-GHz Band\r\n");
}

static void cmd_band3(char *args) {
    change_band(SUBGIG_2);
    set_status_leds(0, 0, 0);
    shell_reply("LoRa Band\r\n");
}

static void cmd_reboot(char *args) {
    shell_reply("Entering USB bootloader...\r\n");
    k_msleep(100);
    reset_usb_boot(0, 0);
}

static void cmd_tx(char *args) {
    process_lora_command(args);
}

static void cmd_test(char *args) {
    process_lora_command(args);
}

static void cmd_txtest(char *args) {
    process_lora_command(args);
}

static void cmd_status(char *args) {
    char buf[128];
    snprintf(buf, sizeof(buf), 
        "Mode: %d, Band: %d, LoRa: %s\r\n",
        catsniffer.mode, 
        catsniffer.band,
        "ready");
    shell_reply(buf);
}

// Main command processor
void process_command(char *cmd, size_t len) {
    if (len == 0) return;
    
    for (const shell_cmd_t *entry = commands; entry->name != NULL; entry++) {
        if (entry->prefix_match) {
            size_t name_len = strlen(entry->name);
            if (strncmp(cmd, entry->name, name_len) == 0) {
                entry->handler(cmd);
                return;
            }
        } else {
            if (strcmp(cmd, entry->name) == 0) {
                entry->handler(cmd);
                return;
            }
        }
    }
    
    shell_reply("Unknown command. Type 'help'\r\n");
}
