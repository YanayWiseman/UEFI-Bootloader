#include <efi.h>
#include <efilib.h>
#include <elf.h>

void uint64ToChar16(UINT64 integer, CHAR16* buffer);
void Fail(EFI_SYSTEM_TABLE* SystemTable, CHAR16* Message);
void Halt(void);
BOOLEAN IsElf(VOID* Buffer, UINT64 BufferSize);

#define PAGE_PRESENT  0x001
#define PAGE_WRITABLE 0x002
#define PAGE_LARGE    0x080

#define MAX_IDENTITY_ADDRESS 0x100000000ULL

__attribute__((noreturn))
void JumpToKernel(UINT64 PageTableAddress, UINT64 StackTop, UINT64 EntryPoint);
EFI_STATUS ExitBootServicesAndGetMap(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable);

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

    uefi_call_wrapper(SystemTable->ConOut->OutputString, 2, SystemTable->ConOut, L"ELF header parsed successfully!\r\n");

    uefi_call_wrapper(SystemTable->ConOut->OutputString, 2, SystemTable->ConOut, L"Entry point: ");
    uefi_call_wrapper(SystemTable->ConOut->OutputString, 2, SystemTable->ConOut, EntryPointString);

    uefi_call_wrapper(SystemTable->ConOut->OutputString, 2, SystemTable->ConOut, L"\r\nProgram header offset: ");
    uefi_call_wrapper(SystemTable->ConOut->OutputString, 2, SystemTable->ConOut, ProgramHeaderOffsetString);

    uefi_call_wrapper(SystemTable->ConOut->OutputString, 2, SystemTable->ConOut, L"\r\nProgram header count: ");
    uefi_call_wrapper(SystemTable->ConOut->OutputString, 2, SystemTable->ConOut, ProgramHeaderCountString);

    uefi_call_wrapper(SystemTable->ConOut->OutputString, 2, SystemTable->ConOut, L"\r\n");

    // Verify that the program header entries have the expected size.
    if(ElfHeader->e_phentsize != sizeof(Elf64_Phdr))
    {
        Fail(SystemTable, L"Invalid program header size\r\n");
    }

    // Ensure that program headers exist
    if(ElfHeader->e_phnum == 0)
    {
        Fail(SystemTable, L"Couldn't find ELF header's number of segments!\r\n");
    }

    // Ensure that the program header table fits inside the ELF file
    // Division avoids integer overflow in the size calculation.
    if (ElfHeader->e_phoff > KernelSize || ElfHeader->e_phnum > (KernelSize - ElfHeader->e_phoff) / ElfHeader->e_phentsize)
    {
        Fail(SystemTable, L"Program header table exceeds ELF file bounds!\r\n");
    }

    // Calc the adress of the program header table inside the ELF buffer
    Elf64_Phdr* ProgramHeaders = (Elf64_Phdr*)((UINT8*)KernelBuffer + ElfHeader->e_phoff);

    // Find the complete memory range required by all PT_LOAD segments.
    UINT64 MinAddress = ~((UINT64)0);
    UINT64 MaxAddress = 0;

    // Iterate over all of the program hedaers
    for(UINT16 i = 0; i < ElfHeader->e_phnum; i++)
    {
        // Only load specific type of segments
        if(ProgramHeaders->p_type == PT_LOAD && ProgramHeaders->p_memsz > 0)
        {
            // Ensure the segment's memory size is large enough.
            if (ProgramHeaders->p_memsz < ProgramHeaders->p_filesz)
            {
                Fail(SystemTable, L"Segment memory size is smaller than file size!\r\n");
            }

            // Ensure the segment's file data is inside KernelBuffer.
            if (ProgramHeaders->p_offset > KernelSize || ProgramHeaders->p_filesz > KernelSize - ProgramHeaders->p_offset)
            {
                Fail(SystemTable, L"Segment exceeds ELF file bounds!\r\n");
            }

            // Ensure the virtual adSdress range doesn't overflow.
            if (ProgramHeaders->p_memsz > ~((UINT64)0) - ProgramHeaders->p_paddr)
            {
                Fail(SystemTable, L"Physical segment address overflow!\r\n");
            }

            UINT64 SegmentStart = ProgramHeaders->p_paddr;
            UINT64 SegmentEnd = SegmentStart + ProgramHeaders->p_memsz;

            // Align the segment boundaries to 4 KiB pages.
            UINT64 PageStart = SegmentStart & ~((UINT64)EFI_PAGE_SIZE - 1);

            if (SegmentEnd > ~((UINT64)0) - (EFI_PAGE_SIZE - 1))
            {
                Fail(SystemTable, L"Segment alignment overflow!\r\n");
            }

            UINT64 PageEnd = (SegmentEnd + EFI_PAGE_SIZE - 1) & ~((UINT64)EFI_PAGE_SIZE - 1);

            if (PageStart < MinAddress)
            {
                MinAddress = PageStart;
            }

            if (PageEnd > MaxAddress)
            {
                MaxAddress = PageEnd;
            }
        }

        // Go to the header
        ProgramHeaders++;
    }

    // Ensure we found at least one loadable segment
    if (MinAddress == ~((UINT64)0) || MaxAddress <= MinAddress)
    {
        Fail(SystemTable, L"No valid loadable segments found!\r\n");
    }

    // Calculate the required allocation size.
    UINT64 AllocationSize = MaxAddress - MinAddress;

    // Calculate the number of 4 KiB pages required.
    UINTN Pages = (UINTN)(AllocationSize / EFI_PAGE_SIZE);

    // Request memory at the exact physical address.
    EFI_PHYSICAL_ADDRESS AllocationAddress = MinAddress;

    Status = uefi_call_wrapper(SystemTable->BootServices->AllocatePages, 4, AllocateAddress, EfiLoaderData, Pages, &AllocationAddress);
    if (EFI_ERROR(Status))
    {
        Fail(SystemTable, L"Failed to allocate kernel memory!\r\n");
    }
    
    // Clear the entire allocated region
    // This also takes care of the zero initialized parts of segments
    SetMem((VOID*)(UINTN)MinAddress, (UINTN)AllocationSize, 0);

    // Reset the program header pointer
    ProgramHeaders = (Elf64_Phdr*)((UINT8*)KernelBuffer + ElfHeader->e_phoff);

    // Iterate over all program headers again
    for (UINT16 i = 0; i < ElfHeader->e_phnum; i++)
    {
        if (ProgramHeaders->p_type == PT_LOAD)
        {
            // Copy the segment's file contents to its destination.
            CopyMem((VOID*)(UINTN)ProgramHeaders->p_paddr, (UINT8*)KernelBuffer + ProgramHeaders->p_offset, (UINTN)ProgramHeaders->p_filesz);
        }
        ProgramHeaders++;
    }

    uefi_call_wrapper(SystemTable->ConOut->OutputString, 2, SystemTable->ConOut, L"Kernel segments loaded successfully!\r\n");

    // The kernel is linked at a low physical address
    // This first implementation identity maps only the first 4 GB
    if (MaxAddress > MAX_IDENTITY_ADDRESS || ElfHeader->e_entry >= MAX_IDENTITY_ADDRESS)
    {
        Fail(SystemTable, L"Kernel is outside the supported 4 GB range!\r\n");
    }

    // Allocate six contiguous pages for our page tables:
    // 1 PML4, 1 PDPT, and 4 page directories
    EFI_PHYSICAL_ADDRESS PageTablesAddress = 0;

    Status = uefi_call_wrapper(SystemTable->BootServices->AllocatePages, 4, AllocateAnyPages, EfiLoaderData, 6, &PageTablesAddress);

    if (EFI_ERROR(Status) || PageTablesAddress + 6 * EFI_PAGE_SIZE > MAX_IDENTITY_ADDRESS)
    {
        Fail(SystemTable, L"Failed to allocate page tables below 4 GB!\r\n");
    }

    SetMem((VOID*)(UINTN)PageTablesAddress, 6 * EFI_PAGE_SIZE, 0);

    UINT64* PML4 = (UINT64*)(UINTN)PageTablesAddress;
    UINT64* PDPT = (UINT64*)(UINTN)(PageTablesAddress + EFI_PAGE_SIZE);

    // Connect PML4 entry 0 to the PDPT
    PML4[0] = PageTablesAddress + EFI_PAGE_SIZE | PAGE_PRESENT | PAGE_WRITABLE;

    // Connect four PDPT entries to four page directories
    // Each page directory maps 1 GiB using 2 MB pages
    for (UINT64 i = 0; i < 4; i++)
    {
        UINT64 PageDirectoryAddress = PageTablesAddress + (2 + i) * EFI_PAGE_SIZE;

        PDPT[i] = PageDirectoryAddress | PAGE_PRESENT | PAGE_WRITABLE;

        UINT64* PageDirectory = (UINT64*)(UINTN)PageDirectoryAddress;

        for (UINT64 j = 0; j < 512; j++)
        {
            UINT64 PhysicalAddress = (i * 512 + j) * 0x200000;
            PageDirectory[j] = PhysicalAddress | PAGE_PRESENT | PAGE_WRITABLE | PAGE_LARGE;
        }
    }

    uefi_call_wrapper(SystemTable->ConOut->OutputString, 2, SystemTable->ConOut, L"Identity page tables created!\r\n");

    // Allocate a separate 64 KB kernel stack
    EFI_PHYSICAL_ADDRESS StackAddress = 0;

    Status = uefi_call_wrapper(SystemTable->BootServices->AllocatePages, 4, AllocateAnyPages, EfiLoaderData, 16, &StackAddress);

    if (EFI_ERROR(Status) || StackAddress + 16 * EFI_PAGE_SIZE > MAX_IDENTITY_ADDRESS)
    {
        Fail(SystemTable, L"Failed to allocate kernel stack below 4 GiB!\r\n");
    }

    UINT64 StackTop = StackAddress + 16 * EFI_PAGE_SIZE;  // The stack grows downwards

    uefi_call_wrapper(SystemTable->ConOut->OutputString, 2, SystemTable->ConOut, L"Kernel stack allocated!\r\n");

    uefi_call_wrapper(SystemTable->ConOut->OutputString, 2, SystemTable->ConOut, L"Exiting UEFI boot services...\r\n");

    // This must be our last UEFI boot services operation
    // UEFI's memory allocator, console text output (ConOut), file system protocols, and watchdog timers are destroyed
    // Calling SystemTable->ConOut->OutputString after this line will crash the machine
    Status = ExitBootServicesAndGetMap(ImageHandle, SystemTable);
    if (EFI_ERROR(Status))
    {
        // Boot services may still be available if ExitBootServices failed
        Fail(SystemTable, L"ExitBootServices failed!\r\n");
    }

    // No UEFI calls after this point
    // Switch page tables and stack, then jump to kernel_main
    JumpToKernel(PageTablesAddress, StackTop, ElfHeader->e_entry);

    return EFI_SUCCESS;
}

/*
    Function gets the final UEFI memory map and exit boot services.
    The map is retrieved immediately before ExitBootServices because allocations and other boot service calls can change its map key.
    input: The image handle and system table (efi_main params)
    Output: EFI_STATUS representing succuss of the function
*/

EFI_STATUS ExitBootServicesAndGetMap(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable)
{
    UINTN MapSize = 0;
    UINTN MapKey = 0;
    UINTN DescriptorSize = 0;
    UINT32 DescriptorVersion = 0;

    // Since we don't know how many memory regions are there, we intentionally fail the following funciton with EFI_BUFFER_TOO_SMALL
    // Then it writes the actual values into MapSize, MapKey, DescriptorSize and DescriptorVersion
    EFI_STATUS Status = uefi_call_wrapper(SystemTable->BootServices->GetMemoryMap, 5, &MapSize, NULL, &MapKey, &DescriptorSize, &DescriptorVersion);
    if (Status != EFI_BUFFER_TOO_SMALL || DescriptorSize == 0)
    {
        return EFI_LOAD_ERROR;
    }

    // Leave room for additional descriptors
    UINTN MapCapacity = MapSize + DescriptorSize * 32;

    EFI_MEMORY_DESCRIPTOR* MemoryMap = NULL;

    Status = uefi_call_wrapper(SystemTable->BootServices->AllocatePool, 3, EfiLoaderData, MapCapacity, (VOID**)&MemoryMap);
    if (EFI_ERROR(Status))
    {
        return Status;
    }

    // Retry if the memory map changes before ExitBootServices
    for (UINTN Attempt = 0; Attempt < 5; Attempt++)
    {
        // ensure UEFI always knows the full capacity of the buffer
        MapSize = MapCapacity;

        Status = uefi_call_wrapper(SystemTable->BootServices->GetMemoryMap, 5, &MapSize, MemoryMap, &MapKey, &DescriptorSize, &DescriptorVersion);
        if (EFI_ERROR(Status))
        {
            return Status;
        }

        Status = uefi_call_wrapper(SystemTable->BootServices->ExitBootServices, 2, ImageHandle, MapKey);
        if (!EFI_ERROR(Status))
        {
            return EFI_SUCCESS;
        }

        if (Status != EFI_INVALID_PARAMETER)
        {
            return Status;
        }
    }

    return EFI_ABORTED;
}


/*
    Function to witch to our own page tables and stack, then jump to the kernel.
    This function will not jump back to where it was called from.
    The page tables identity map the first 4 GB, so the kernel's physical address is also its virtual address.
    Input: The page tables address, the stack top and the entry point.
    Output: no return values
*/

__attribute__((noreturn))
void JumpToKernel(UINT64 PageTableAddress, UINT64 StackTop, UINT64 EntryPoint)
{
    __asm__ __volatile__
    (
        "cli\n"
        "mov %0, %%cr3\n"  // Set the control register to out PML4 physical address
        "mov %1, %%rsp\n"  // Set the stack pointer
        "andq $-16, %%rsp\n"  // Align rsp down to a 16 bit boundary
        "subq $8, %%rsp\n"  // Fake the stack alignment of a call instruction so instructions in kernel_main don't trigger a General Protection Fault
        "xorq %%rbp, %%rbp\n"
        "cld\n"  // DF must be 0 before entering any C function
        "jmp *%2\n"  // jump to EntryPoint
        :
        : "r"(PageTableAddress), "r"(StackTop), "r"(EntryPoint)  
        : "memory"
    );

    __builtin_unreachable();  // Trigger UB if executable reaches this
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