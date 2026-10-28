/* S32K344 UDS Flash Driver - ITCM resident image (Mem_43_INFLS / C40). */
ENTRY(main)

MEMORY
{
    ITCM (rwx) : ORIGIN = 0x00000000, LENGTH = 0x00010000
}

SECTIONS
{
    .drv_header :
    {
        KEEP(*(.drv_header))
    } > ITCM

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.mcal_text)
        *(.acmcu_code_rom)
        __acmem_43_infls_code_rom_start = .;
        *(.acmem_43_infls_code_rom)
        __acmem_43_infls_code_rom_end = .;
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        *(.mcal_const)
        *(.mcal_const*)
        . = ALIGN(4);
    } > ITCM

    .acmem_43_infls_code_ram :
    {
        . = ALIGN(4);
        __acmem_43_infls_code_ram_start = .;
        *(.acmem_43_infls_code_ram)
        __acmem_43_infls_code_ram_end = .;
        . = ALIGN(4);
    } > ITCM

    .data :
    {
        . = ALIGN(4);
        *(.data)
        *(.data*)
        *(.mcal_data)
        *(.mcal_data_no_cacheable)
        *(.mcal_shared_data)
        . = ALIGN(4);
    } > ITCM

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss)
        *(.bss*)
        *(.mcal_bss)
        *(.mcal_bss*)
        *(.mcal_bss_no_cacheable)
        *(.mcal_shared_bss)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > ITCM

    Mem_43_INFLS_ACEraseRomStart = __acmem_43_infls_code_rom_start;
    Mem_43_INFLS_ACEraseRomEnd   = __acmem_43_infls_code_rom_end;
    Mem_43_INFLS_ACEraseSize     = (__acmem_43_infls_code_rom_end - __acmem_43_infls_code_rom_start) / 4;
    Mem_43_INFLS_ACWriteRomStart = __acmem_43_infls_code_rom_start;
    Mem_43_INFLS_ACWriteRomEnd   = __acmem_43_infls_code_rom_end;
    Mem_43_INFLS_ACWriteSize     = (__acmem_43_infls_code_rom_end - __acmem_43_infls_code_rom_start) / 4;
    _MEM_43_INFLS_ERASE_FUNC_ADDRESS_ = __acmem_43_infls_code_ram_start;
    _MEM_43_INFLS_WRITE_FUNC_ADDRESS_ = __acmem_43_infls_code_ram_start;

    /DISCARD/ :
    {
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.comment)
    }

    /* The bootloader downloads the image into the first 32 KB of ITCM and
     * checks the entry against this limit. */
    ASSERT(__bss_end <= 0x00008000, "S32K344 flash driver exceeds 32 KB ITCM")
}
