/*
 *  ======== cc13x2x7_cc26x2x7_nortos.lds ========
 *  Linker command file for CC13X2X7/CC26X2X7 devices using GCC.
 */

MEMORY
{
    FLASH (RX)      : ORIGIN = 0x00000000, LENGTH = 0x000B0000
    /*
     * Customer Configuration Area and Bootloader Backdoor configuration
     * in flash, 4 KiB
     */
    FLASH_CCFG (RX) : ORIGIN = 0x50000000, LENGTH = 0x00000800
    SRAM (RWX)      : ORIGIN = 0x20000000, LENGTH = 0x00024000
    GPRAM (RWX)     : ORIGIN = 0x11000000, LENGTH = 0x00002000
}

REGION_ALIAS("REGION_TEXT", FLASH);
REGION_ALIAS("REGION_BSS", SRAM);
REGION_ALIAS("REGION_DATA", SRAM);
REGION_ALIAS("REGION_STACK", SRAM);
REGION_ALIAS("REGION_HEAP", SRAM);
REGION_ALIAS("REGION_ARM_EXIDX", FLASH);
REGION_ALIAS("REGION_ARM_EXTAB", FLASH);

SECTIONS
{
    .text :
    {
        CREATE_OBJECT_SYMBOLS
        *(.text)
        *(.text.*)
        . = ALIGN(0x4);
        KEEP (*(.ctors))
        . = ALIGN(0x4);
        KEEP (*(.dtors))
        . = ALIGN(0x4);
        __init_array_start = .;
        KEEP (*(.init_array*))
        __init_array_end = .;
        *(.init)
        *(.fini*)
    } > REGION_TEXT AT> REGION_TEXT

    .ARM.exidx :
    {
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > REGION_ARM_EXIDX AT> REGION_ARM_EXIDX

    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > REGION_ARM_EXTAB AT> REGION_ARM_EXTAB

    .rodata :
    {
        *(.rodata)
        *(.rodata.*)
    } > REGION_TEXT AT> REGION_TEXT

    .data :
    {
        __data_load__ = LOADADDR (.data);
        __data_start__ = .;
        *(.data)
        *(.data.*)
        *(.dma*)
        __data_end__ = .;
    } > REGION_DATA AT> REGION_TEXT

    .bss :
    {
        __bss_start__ = .;
        *(.shbss)
        *(.bss)
        *(.bss.*)
        *(COMMON)
        . = ALIGN (4);
        __bss_end__ = .;
    } > REGION_BSS AT> REGION_BSS

    .heap :
    {
        __heap_start__ = .;
        end = __heap_start__;
        _end = end;
        __end = end;
        KEEP(*(.heap))
        __heap_end__ = .;
    } > REGION_HEAP AT> REGION_HEAP

    .stack (NOLOAD) :
    {
        . = ALIGN(8);
        __stack_end__ = .;
        *(.stack)
        . = ALIGN(4);
        __stack_start__ = .;
    } > REGION_STACK AT> REGION_STACK

    /*
     * The CCFG section contains the Customer Configuration Area and the
     * Bootloader Backdoor configuration.
     * It is placed at the end of the flash URL.
     */
    .ccfg :
    {
        KEEP (*(.ccfg))
    } > FLASH_CCFG AT> FLASH_CCFG
    
    _ldata = __data_load__;
    _data = __data_start__;
    _edata = __data_end__;
    _bss = __bss_start__;
    _ebss = __bss_end__;
    
    UDMACC26XX_dmaUart0RxControlTableEntry_is_placed = 1;
    UDMACC26XX_dmaUart0TxControlTableEntry_is_placed = 1;
}

ENTRY(ResetISR)
