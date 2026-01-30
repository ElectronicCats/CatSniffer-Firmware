# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(catsniffer LANGUAGES C)

target_include_directories(app PRIVATE ${CMAKE_SOURCE_DIR}/include)

FILE(GLOB app_sources src/*.c)

# Add local usb init
target_sources_ifdef(CONFIG_USB_DEVICE_STACK_NEXT app PRIVATE
    src/USB/usbd_init.c
    src/shell_commands.c
)

target_sources(app PRIVATE ${app_sources})
