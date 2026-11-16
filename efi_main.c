#include <efi.h>
#include <efilib.h>

/*
    Main function for the UEFI Bootloader, defined as efi_main to invoke the entry point specidief by the EFI executable.
    EFI_HANDLE is the ID of the UEFI executable, SystemTable points to EFI_SYSTEM_TABLE which is a struct containing a lot
    of useful firmware interfaces including i/o/error streams, boot services and runtime services.
    Function returns EPI_STATUS which defines multiple occasions (succuss and types of failures)
    calling convension is EFIAPI

    This code will be compiled using the following flags:
    -fno-stack-protector - disable stack canary
    -fpic - to generate position independent code
    -fshort-wchar - to represent each character as 2 bytes and not 4 in the L"" format
    -mno-red-zone - we cant assume that the firmware gives the same redzone behviour we expect
    -maccumulate-outgoing-args - changes how gcc reserves stack space for outgoing function arguments.
*/
EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable)
{
    // Initialize the UEFI libraries using the parameters
    InitializeLib(ImageHandle, SystemTable);

    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Hello From NYOS!\r\n");

    return EFI_SUCCESS;
}