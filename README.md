# UEFI Bootloader
 
A x86-64 UEFI bootloader written in C with gnu-efi. It loads a 64 bit ELF kernel from the boot disk, sets up paging, and jumps into it. Made to learn how the boot process works between firmware and kernel.
 
## What it does
 
1. Prints a banner and some info about the loaded image
2. Opens the boot filesystem and reads kernel.elf into memory
3. Validates the ELF header (64 bit, little endian, x86-64, ET_EXEC) and checks that the program headers and segments fit inside the file
4. Works out the memory range needed by all PT_LOAD segments and allocates it at the exact physical address the kernel was linked for
5. Zeroes that region and copies each segment in
6. Builds identity mapped page tables for the first 4 GB (2 MB pages)
7. Allocates a 64 KB stack for the kernel
8. Gets the final memory map and exits boot services (retries if the map key changes)
9. Switches CR3 and the stack, then jumps to the kernel entry point
If anything fails along the way, it prints an error and halts.
 
## Files
 
| File | Description |
|------|-------------|
| efi_main.c | The bootloader |
| kernel.c | Minimal test kernel, just halts in a loop |
| kernel.ld | Linker script, places the kernel at 0x200000 |
| esp/EFI/BOOT/BOOTX64.EFI | Prebuilt bootloader in the default UEFI boot path |
| OVMF_VARS_4M.fd | Firmware variable store for QEMU |
 
## Requirements
 
- GCC, binutils
- gnu-efi
- QEMU
- OVMF (UEFI firmware for QEMU)
On Debian/Ubuntu:
 

sudo apt install build-essential gnu-efi qemu-system-x86 ovmf

 
## Build & Run the bootloader
 
Bootloader (the flags are the ones listed in the comment above efi_main):

gcc \   
    -I/usr/include/efi \
    -I/usr/include/efi/x86_64 \
    -fpic \                         
    -ffreestanding \
    -fno-stack-protector \
    -fshort-wchar \             
    -mno-red-zone \
    -maccumulate-outgoing-args \
    -c efi_main.c \
    -o efi_main.o    

ld \     
    -nostdlib \
    -znocombreloc \
    -T /usr/lib/elf_x86_64_efi.lds \
    -shared \   
    -Bsymbolic \ 
    /usr/lib/crt0-efi-x86_64.o \
    efi_main.o \
    -L/usr/lib \ 
    -lefi \    
    -lgnuefi \       
    -o bootloader.so

objcopy \
    -j .text \ 
    -j .sdata \    
    -j .data \                      
    -j .rodata \
    -j .dynamic \
    -j .dynsym \                
    -j '.rel*' \
    -j '.rela*' \
    -j .reloc \
    -I elf64-x86-64 \
    -O efi-app-x86_64 \
    bootloader.so BOOTX64.EFI

Make sure that there are no spaces after the \ character when compiling and linking.

cp BOOTX64.EFI esp/EFI/BOOT/BOOTX64.EFI && qemu-system-x86_64 -machine q35 -m 512M -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd -drive if=pflash,format=raw,file=OVMF_VARS_4M.fd -drive format=raw,file=fat:rw:esp  

 
## Build the example kernel:
 

gcc \
    -c kernel.c \
    -o kernel.o \
    -ffreestanding \
    -fno-stack-protector \
    -fno-pic \
    -fno-pie \
    -fno-asynchronous-unwind-tables \
    -fno-unwind-tables \
    -mno-red-zone \
    -m64

ld \
    -nostdlib \
    -static \
    --build-id=none \
    -z max-page-size=0x1000 \
    -T kernel.ld \
    -o esp/kernel.elf \
    kernel.o

 
Library and linker script paths can differ between distros, so adjust them if the link step fails.
 
 
The bootloader prints its progress, and the last line should be Exiting UEFI boot services.... After that the test kernel just halts, so the screen stays still. That means it worked.
 
## Notes
 
- The kernel has to be linked at a physical address below 4 GB, since that's all that gets mapped
- kernel.elf is expected in the root of the same volume the bootloader was loaded from
- Only PT_LOAD segments are loaded, no relocations
