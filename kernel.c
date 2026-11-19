#include <stdint.h>

void kernel_main(void)
{
    // The UEFI loader should have disabled interrupts before jumping here.
    while (1)
    {
        __asm__ __volatile__("hlt");
    }
}