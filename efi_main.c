#include <efi.h>
#include <efilib.h>
#include <elf.h>

void uint64ToChar16(UINT64 integer, CHAR16* buffer);
void Fail(EFI_SYSTEM_TABLE* SystemTable, CHAR16* Message);
void Halt(void);
BOOLEAN IsElf(VOID* Buffer, UINT64 BufferSize);

/*
    Main function for the UEFI Bootloader, defined as efi_main to invoke the entry point specidief by the EFI executable.
    EFI_HANDLE is the ID of the UEFI executable, SystemTable points to EFI_SYSTEM_TABLE which is a struct containing a lot
    of useful firmware interfaces including i/o/error streams, boot services and runtime services.
    Function returns EFI_STATUS which defines multiple occasions (success and types of failures)
    calling convension is EFIAPI

    This code will be compiled using the following flags:
    -fno-stack-protector - disable stack canary
    -fpic - to generate position independent code
    -fshort-wchar - to represent the size of wchar_t as 2 bytes and not 4 in the L"" format
    -mno-red-zone - we cant assume that the firmware gives the same redzone behviour we expect
    -maccumulate-outgoing-args - changes how gcc reserves stack space for outgoing function arguments.
*/
EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable)
{
    // Initialize the UEFI libraries using the parameters
    InitializeLib(ImageHandle, SystemTable);

    // Clear the screen and display the bootloader banner.
    uefi_call_wrapper(SystemTable->ConOut->ClearScreen, 1, SystemTable->ConOut);
    uefi_call_wrapper(SystemTable->ConOut->OutputString, 2, SystemTable->ConOut, L"Hello From NYOS!\r\n");

    // Retrieve the loaded image protocol interface
    EFI_GUID LoadedImageProtocol = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_LOADED_IMAGE_PROTOCOL* loadedImage = NULL;

    EFI_STATUS Status = uefi_call_wrapper(SystemTable->BootServices->HandleProtocol, 3, ImageHandle, &LoadedImageProtocol, (VOID**)&loadedImage);
    if(EFI_ERROR(Status))
    {
        Fail(SystemTable, L"Failed to get loaded image protocol!\r\n");
    }
    // Now loadedImage points to the protocol interface

    CHAR16 imageSizeString[21];

    // Get the image size as an integer
    uint64ToChar16(loadedImage->ImageSize, imageSizeString);
    uefi_call_wrapper(SystemTable->ConOut->OutputString, 2, SystemTable->ConOut, L"Image size: ");
    uefi_call_wrapper(SystemTable->ConOut->OutputString, 2, SystemTable->ConOut, imageSizeString);

    // Get the fs interface
    EFI_GUID FileSystemProtocol = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* FileSystem = NULL;

    Status = uefi_call_wrapper(SystemTable->BootServices->HandleProtocol, 3, loadedImage->DeviceHandle, &FileSystemProtocol, (VOID**)&FileSystem);
    if(EFI_ERROR(Status))
    {
        Fail(SystemTable, L"Failed to get File System protocol!\r\n");
    }

    // Open the root directory
    EFI_FILE_HANDLE Root = NULL;
    Status = uefi_call_wrapper(FileSystem->OpenVolume, 2, FileSystem, &Root);
    if(EFI_ERROR(Status))
    {
        Fail(SystemTable, L"Failed to open Filesystem volume!\r\n");
    }
    
    uefi_call_wrapper(SystemTable->ConOut->OutputString, 2, SystemTable->ConOut, L"\nSuccessfully opened filesystem!\r\n");

    // Open kernel.elf
    EFI_FILE_HANDLE KernelFile = NULL;
    Status = uefi_call_wrapper(Root->Open, 5, Root, &KernelFile, L"kernel.elf", EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status))
    {
        Fail(SystemTable, L"Failed to open kernel.elf!\r\n");
    }
    uefi_call_wrapper(SystemTable->ConOut->OutputString, 2, SystemTable->ConOut, L"\nSuccessfully opened kernel.elf!\r\n");

    // Figure out the size of the file
    EFI_GUID FileInfoGuid = EFI_FILE_INFO_ID;

    // We dont know the size of kernel.elf
    UINTN FileInfoSize = 0;

    // Ask UEFI how large the information buffer needs to be
    // Passing NULL as the buffer makes UEFI report the required size
    Status = uefi_call_wrapper(KernelFile->GetInfo, 4, KernelFile, &FileInfoGuid, &FileInfoSize, NULL);
    if(Status != EFI_BUFFER_TOO_SMALL)
    {
        Fail(SystemTable, L"Failed to determine file info buffer!\r\n");
    }

    // FileInfoSize contains the size to allocate, we will use AllocatePool() from the boot services
    EFI_FILE_INFO* FileInfo = NULL;
    Status = uefi_call_wrapper(SystemTable->BootServices->AllocatePool, 3, EfiLoaderData, FileInfoSize, (VOID**)&FileInfo);
    if (EFI_ERROR(Status))
    {
        Fail(SystemTable, L"Failed to allocate file into buffer\r\n");
    }

    // Now we can get the info
    Status = uefi_call_wrapper(KernelFile->GetInfo, 4, KernelFile, &FileInfoGuid, &FileInfoSize, FileInfo);
    if (EFI_ERROR(Status))
    {
        Fail(SystemTable, L"Failed to retrieve file information!\r\n");
    }
    // Retrieve the size
    UINT64 KernelSize = FileInfo->FileSize;

    // Format it
    CHAR16 kernelSizeString[21];
    uint64ToChar16(KernelSize, kernelSizeString);

    uefi_call_wrapper(SystemTable->ConOut->OutputString, 2, SystemTable->ConOut, L"Kernel size: ");
    uefi_call_wrapper(SystemTable->ConOut->OutputString, 2, SystemTable->ConOut, kernelSizeString);
    uefi_call_wrapper(SystemTable->ConOut->OutputString, 2, SystemTable->ConOut, L" Bytes\r\n");


    // Free allocated memory
    uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, FileInfo);

    // Allocate enough memory to hold the entire kernel.elf file
    VOID* KernelBuffer = NULL;
    Status = uefi_call_wrapper(SystemTable->BootServices->AllocatePool, 3, EfiLoaderData, (UINTN)KernelSize, &KernelBuffer);
    if (EFI_ERROR(Status))
    {
        Fail(SystemTable, L"Failed to allocate kernel buffer!\r\n");
    }

    // Read the kernel.elf file into the allocated buffer
    UINTN ReadSize = (UINTN)KernelSize;
    Status = uefi_call_wrapper(KernelFile->Read, 3, KernelFile, &ReadSize, KernelBuffer);
    if (EFI_ERROR(Status) || ReadSize != KernelSize)
    {
        Fail(SystemTable, L"Failed to read complete kernel!\r\n");
    }

    // Verify that the loaded file has the ELF's signature
    if(!IsElf(KernelBuffer, KernelSize))
    {
        Fail(SystemTable, L"Invalid ELF file!\r\n");
    }
    uefi_call_wrapper(SystemTable->ConOut->OutputString, 2, SystemTable->ConOut, L"\nSuccessfully loaded kernel.elf!\r\n");

    // Ensure that the file is large enough to contain a elf header
    if(KernelSize < sizeof(Elf64_Ehdr))
    {
        Fail(SystemTable, L"ELF file is too small!\r\n");
    }

    Elf64_Ehdr* ElfHeader = (Elf64_Ehdr*)KernelBuffer;
    
    // Verify that it is a 64bit elf file
    if(ElfHeader->e_ident[EI_CLASS] != ELFCLASS64)
    {
        Fail(SystemTable, L"ELF file is not 64-bit!\r\n");
    }

    // Verify that the elf uses little endian encoding
    if(ElfHeader->e_ident[EI_DATA] != ELFDATA2LSB)
    {
        Fail(SystemTable, L"Unsupported ELF byte order!\r\n");
    }

    // Verify the target architecture
    if(ElfHeader->e_machine != EM_X86_64)
    {
        Fail(SystemTable, L"ELF architecture is not x86-64!\r\n");
    }

    // Verify that the elf is exec type
    if(ElfHeader->e_type != ET_EXEC)
    {
        Fail(SystemTable, L"ELF file is not an executable!\r\n");
    }
    CHAR16 EntryPointString[21];
    CHAR16 ProgramHeaderOffsetString[21];
    CHAR16 ProgramHeaderCountString[21];

    uint64ToChar16(ElfHeader->e_entry, EntryPointString);
    uint64ToChar16(ElfHeader->e_phoff, ProgramHeaderOffsetString);
    uint64ToChar16(ElfHeader->e_phnum, ProgramHeaderCountString);

    uefi_call_wrapper(
        SystemTable->ConOut->OutputString,
        2,
        SystemTable->ConOut,
        L"ELF header parsed successfully!\r\n"
    );

    uefi_call_wrapper(SystemTable->ConOut->OutputString, 2, SystemTable->ConOut, L"Entry point: ");
    uefi_call_wrapper(SystemTable->ConOut->OutputString, 2, SystemTable->ConOut, EntryPointString);

    uefi_call_wrapper(SystemTable->ConOut->OutputString, 2, SystemTable->ConOut, L"\r\nProgram header offset: ");
    uefi_call_wrapper(SystemTable->ConOut->OutputString, 2, SystemTable->ConOut, ProgramHeaderOffsetString);

    uefi_call_wrapper(SystemTable->ConOut->OutputString, 2, SystemTable->ConOut, L"\r\nProgram header count: ");
    uefi_call_wrapper(SystemTable->ConOut->OutputString, 2, SystemTable->ConOut, ProgramHeaderCountString);

    uefi_call_wrapper(SystemTable->ConOut->OutputString, 2, SystemTable->ConOut, L"\r\n");

    // waitttt
    while (1)
    {
        __asm__ __volatile__("hlt");
    }

    return EFI_SUCCESS;
}

/*
    Function stops the processor from executing further instructions
    Uses HLT to halt the CPU, then loops in case execution resumes
    Input: no input values
    Output: No return values
*/
void Halt(void)
{
    while (1)
    {
        __asm__ __volatile__("hlt");
    }
}


/*
    Function displays an error message and stop execution
    Input: SystemTable is used to access the console and is Message the error message to display
    Output: No return values
*/

void Fail(EFI_SYSTEM_TABLE* SystemTable, CHAR16* Message)
{
    uefi_call_wrapper(SystemTable->ConOut->OutputString, 2, SystemTable->ConOut, Message);
    Halt();
}

/*
    Function check whether a memory buffer begins with the ELF magic signature
    Input: The buffer is the memory containing the file and BufferSize is the size of the buffer in bytes
    Output: TRUE if the ELF signature is valid, otherwise FALSE.
*/

BOOLEAN IsElf(VOID* Buffer, UINT64 BufferSize)
{
    // Ensure that the buffer is large enough to contain the signature.
    if (BufferSize < 4)
    {
        return FALSE;
    }

    UINT8* Bytes = (UINT8*)Buffer;

    return (Bytes[0] == 0x7F && Bytes[1] == 'E'  && Bytes[2] == 'L'  && Bytes[3] == 'F');
}

/*
    Helper function to convert UINT64 to CHAR16
    input: the UINT64 integer
    output: the CHAR16 buffer
*/

void uint64ToChar16(UINT64 integer, CHAR16* buffer)
{
    CHAR16 temp[20];
    int i = 0;

    if (integer == 0)
    {
        buffer[0] = L'0';
        buffer[1] = L'\0';
        return;
    }

    while(integer > 0)
    {
        temp[i++] = L'0' + integer % 10;
        integer /= 10;
    }

    // Reverse the reversed string
    int j = 0;
    while(i > 0)
    {
        buffer[j++] = temp[--i];
    }
    buffer[j] = L'\0';
}