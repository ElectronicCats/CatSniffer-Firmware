# Catsniffer RP2040 Zephyr projects
Folder for Zephyr projects to control the catsniffer

## Compiling

For compiling this project is currently using Zephyr 4.1 if you want to compile it make sure you follow [this guide](https://docs.zephyrproject.org/4.1.0/develop/getting_started/index.html)

Once installed correctly you sould load the virtual enviroment

```bash
source ~/zephyrproject/.venv/bin/activate    
```

As we are not compiling from the zephyr root project we better export a enviroment variable to be use on west

```bash
export ZEPHYR_BASE=$HOME/zephyrproject/zephyr
```
Once the enviroment variable is declared we can compile with west

```bash
west build -p always -b rpi_pico -- -DZEPHYR_BASE=ZEPHYR_BASE -DDTV_OVERLAY_FILE=boards/rpio_pico.overlay
```
You can always add to your .bashrc or .zshrc

```bash
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR=$HOME/zephyr-sdk-0.17.0
export ZEPHYR_BASE=$HOME/zephyrproject/zephyr
```

Since this project is out from the zephyr project

## Examples

### Catsniffer

Catsniffer main project, includes serial passthrough 

### Blink

Super simple project with DTS overlay to define all the GPIOs used by the catsniffer as UART, cJTAG, LEDs and Reset pins, does a blink over one LED
