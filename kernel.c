#include <stddef.h>
#include <stdint.h>
#include "limine.h"

// Declare the Limine base revision we support.
LIMINE_BASE_REVISION(0);

// Request the physical memory map.
__attribute__((used, aligned(8)))
static volatile struct limine_memmap_request MemoryMapRequest =
{
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0,
    .response = NULL
};

// Request the Higher Half Direct Map offset.
__attribute__((used, aligned(8)))
static volatile struct limine_hhdm_request HHDMRequest =
{
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0,
    .response = NULL
};

// Request the kernel's physical and virtual base addresses.
__attribute__((used, aligned(8)))
static volatile struct limine_kernel_address_request KernelAddressRequest =
{
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0,
    .response = NULL
};

// Base Revision 0: NULL-terminated list of request pointers.
__attribute__((used, section(".limine_reqs"), aligned(8)))
static volatile void *LimineRequests[] =
{
    &MemoryMapRequest,
    &HHDMRequest,
    &KernelAddressRequest,
    NULL
};

void kernel_main(void)
{
    while (1)
    {
        __asm__ __volatile__("hlt");
    }
}