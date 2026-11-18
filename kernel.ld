ENTRY(kernel_main)

KERNEL_VMA = 0xffffffff80000000;
KERNEL_LMA = 0x200000;

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = KERNEL_VMA;

    .text ALIGN(0x1000) :
        AT(ADDR(.text) - KERNEL_VMA + KERNEL_LMA)
    {
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
        *(.eh_frame*)
    } :text

    .data ALIGN(0x1000) :
        AT(ADDR(.data) - KERNEL_VMA + KERNEL_LMA)
    {
        *(.data)
        *(.data.*)
    } :data

    .limine_reqs ALIGN(8) :
        AT(ADDR(.limine_reqs) - KERNEL_VMA + KERNEL_LMA)
    {
        KEEP(*(.limine_reqs))
    } :data

    .bss ALIGN(0x1000) :
        AT(ADDR(.bss) - KERNEL_VMA + KERNEL_LMA)
    {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    } :data

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
    }
}