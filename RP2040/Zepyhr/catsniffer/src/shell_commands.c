/*
 * shell_commands.c - Shell Logic Implementation
 */

#include "shell_commands.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/kernel.h>

// RP2040 ROM functions for bootloader
#include <pico/bootrom.h>

// External Hardware Actions (Implemented in main.c)
void shell_reply(const char *msg);
void change_mode(unsigned long new_mode);
void change_band(unsigned long new_band);
void process_lora_command(char *cmd_line);

// Shared definitions
#include "catsniffer.h"

extern catsniffer_t catsniffer; 

void set_status_leds(int l0, int l1, int l2);

void process_command(char *cmd, size_t len)
{
    const char *response = NULL;
    
    if (strcmp(cmd, "boot") == 0) {
        change_mode(BOOT);
        response = "BOOT\r\n";
        set_status_leds(0, 0, catsniffer.mode);
    }
    else if (strcmp(cmd, "exit") == 0) {
        change_mode(PASSTHROUGH);
        response = "PASSTHROUGH\r\n";
        set_status_leds(0, 0, 0);
    }
    else if (strcmp(cmd, "band1") == 0) {
        change_band(GIG);
        response = "2.4GHz Band\r\n";
        set_status_leds(0, 0, 0);
    }
    else if (strcmp(cmd, "band2") == 0) {
        change_band(SUBGIG_1);
        response = "SUB-GHz Band\r\n";
        set_status_leds(0, 0, 0);
    }
    else if (strcmp(cmd, "band3") == 0) {
        change_band(SUBGIG_2);
        response = "LoRa Band\r\n";
        set_status_leds(0, 0, 0);
    }
    else if (strncmp(cmd, "TEST", 4) == 0) {
        process_lora_command(cmd);
        return; 
    }
    else if (strncmp(cmd, "TX ", 3) == 0) {
        process_lora_command(cmd);
        return; 
    }
    else if (strncmp(cmd, "TXTEST", 6) == 0) {
        process_lora_command(cmd); 
        return;
    }
    else if (strcmp(cmd, "reboot") == 0) {
        shell_reply("Entering USB bootloader...\r\n");
        k_msleep(100); // Let the message be sent
        reset_usb_boot(0, 0); // Enter USB bootloader (UF2 mode)
        // This function never returns
    }
    else if (strcmp(cmd, "help") == 0) {
        response = "Commands: help, boot, exit, band1/2/3, TEST, TX <hex>, TXTEST, reboot\r\n";
    }
    else {
        response = "UNKNOWN\r\n";
    }
    
    if (response) {
        shell_reply(response);
    }
}

