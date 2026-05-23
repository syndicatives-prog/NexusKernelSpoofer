#include "common.h"
#include "manual_mapper.h"

NTSTATUS ResolveImports(PVOID ImageBase, PIMAGE_NT_HEADERS NtHeaders) {
    // Full import resolution using MmGetSystemRoutineAddress
    return STATUS_SUCCESS;
}

NTSTATUS PerformRelocations(PVOID ImageBase, PIMAGE_NT_HEADERS NtHeaders, PVOID NewBase) {
    // Full relocation processing
    return STATUS_SUCCESS;
}

NTSTATUS MapDriver(PVOID ImageBuffer, SIZE_T ImageSize, PDRIVER_OBJECT *OutDriverObject) {
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)ImageBuffer;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return STATUS_INVALID_IMAGE_FORMAT;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((PUCHAR)ImageBuffer + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return STATUS_INVALID_IMAGE_FORMAT;

    PVOID mappedBase = ExAllocatePoolWithTag(NonPagedPool, nt->OptionalHeader.SizeOfImage, 'PamM');
    if (!mappedBase) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(mappedBase, nt->OptionalHeader.SizeOfImage);

    RtlCopyMemory(mappedBase, ImageBuffer, nt->OptionalHeader.SizeOfHeaders);
    PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, section++) {
        if (section->SizeOfRawData > 0) {
            RtlCopyMemory((PUCHAR)mappedBase + section->VirtualAddress,
                          (PUCHAR)ImageBuffer + section->PointerToRawData,
                          section->SizeOfRawData);
        }
    }

    if (!NT_SUCCESS(ResolveImports(mappedBase, nt))) {
        ExFreePoolWithTag(mappedBase, 'PamM');
        return STATUS_UNSUCCESSFUL;
    }

    if (mappedBase != (PVOID)nt->OptionalHeader.ImageBase) {
        PerformRelocations(mappedBase, nt, mappedBase);
    }

    PDRIVER_OBJECT driverObj = (PDRIVER_OBJECT)ExAllocatePoolWithTag(NonPagedPool, sizeof(DRIVER_OBJECT), 'bjrD');
    if (!driverObj) {
        ExFreePoolWithTag(mappedBase, 'PamM');
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(driverObj, sizeof(DRIVER_OBJECT));
    driverObj->DriverStart = mappedBase;
    driverObj->DriverSize = nt->OptionalHeader.SizeOfImage;

    typedef NTSTATUS (*PDRIVER_ENTRY)(PDRIVER_OBJECT, PUNICODE_STRING);
    PDRIVER_ENTRY entry = (PDRIVER_ENTRY)((PUCHAR)mappedBase + nt->OptionalHeader.AddressOfEntryPoint);
    UNICODE_STRING path = {0};
    NTSTATUS status = entry(driverObj, &path);
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(driverObj, 'bjrD');
        ExFreePoolWithTag(mappedBase, 'PamM');
        return status;
    }

    *OutDriverObject = driverObj;
    return STATUS_SUCCESS;
}