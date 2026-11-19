ENTRY(kernel_main)

KERNEL_ADDRESS = 0x200000;

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = KERNEL_ADDRESS;

    .text ALIGN(0x1000) :
    {
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
    } :text

    .data ALIGN(0x1000) :
    {
        *(.data)
        *(.data.*)
    } :data

    .bss ALIGN(0x1000) :
    {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    } :data

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}