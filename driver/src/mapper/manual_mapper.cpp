#include "manual_mapper.h"
#include "common.h"
#include <ntimage.h>

static PVOID GetModuleBaseByName(PCHAR ModuleName) {
    PSYSTEM_MODULE_INFORMATION_EX info = NULL;
    ULONG infoSize = 0;
    NTSTATUS status = ZwQuerySystemInformation(SystemModuleInformation, NULL, 0, &infoSize);
    if (status != STATUS_INFO_LENGTH_MISMATCH || infoSize == 0) return NULL;
    info = (PSYSTEM_MODULE_INFORMATION_EX)ExAllocatePoolWithTag(NonPagedPool, infoSize, 'mdlL');
    if (!info) return NULL;
    status = ZwQuerySystemInformation(SystemModuleInformation, info, infoSize, &infoSize);
    if (!NT_SUCCESS(status)) { ExFreePoolWithTag(info, 'mdlL'); return NULL; }
    PVOID base = NULL;
    for (ULONG i = 0; i < info->NumberOfModules; i++) {
        PCHAR path = (PCHAR)info->Modules[i].FullPathName + info->Modules[i].OffsetToFileName;
        if (_stricmp(path, ModuleName) == 0) { base = info->Modules[i].ImageBase; break; }
    }
    ExFreePoolWithTag(info, 'mdlL');
    return base;
}

static PVOID GetExportAddress(PVOID ModuleBase, PCHAR FunctionName) {
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)ModuleBase;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((PUCHAR)ModuleBase + dos->e_lfanew);
    ULONG exportRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    if (!exportRva) return NULL;
    PIMAGE_EXPORT_DIRECTORY exportDir = (PIMAGE_EXPORT_DIRECTORY)((PUCHAR)ModuleBase + exportRva);
    PULONG functions = (PULONG)((PUCHAR)ModuleBase + exportDir->AddressOfFunctions);
    PULONG names = (PULONG)((PUCHAR)ModuleBase + exportDir->AddressOfNames);
    PUSHORT ordinals = (PUSHORT)((PUCHAR)ModuleBase + exportDir->AddressOfNameOrdinals);
    for (ULONG i = 0; i < exportDir->NumberOfNames; i++) {
        PCHAR name = (PCHAR)((PUCHAR)ModuleBase + names[i]);
        if (strcmp(name, FunctionName) == 0) return (PVOID)((PUCHAR)ModuleBase + functions[ordinals[i]]);
    }
    return NULL;
}

static NTSTATUS ResolveImports(PVOID ImageBase, PIMAGE_NT_HEADERS NtHeaders) {
    ULONG importRva = NtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!importRva) return STATUS_SUCCESS;
    PIMAGE_IMPORT_DESCRIPTOR importDesc = (PIMAGE_IMPORT_DESCRIPTOR)((PUCHAR)ImageBase + importRva);
    while (importDesc->Name) {
        PCHAR dllName = (PCHAR)ImageBase + importDesc->Name;
        PVOID moduleBase = GetModuleBaseByName(dllName);
        if (!moduleBase) return STATUS_NOT_FOUND;
        PIMAGE_THUNK_DATA thunk = (PIMAGE_THUNK_DATA)((PUCHAR)ImageBase + importDesc->FirstThunk);
        PIMAGE_THUNK_DATA origThunk = (PIMAGE_THUNK_DATA)((PUCHAR)ImageBase + (importDesc->OriginalFirstThunk ? importDesc->OriginalFirstThunk : importDesc->FirstThunk));
        while (origThunk->u1.AddressOfData) {
            if (origThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG) return STATUS_NOT_IMPLEMENTED;
            PIMAGE_IMPORT_BY_NAME importByName = (PIMAGE_IMPORT_BY_NAME)((PUCHAR)ImageBase + origThunk->u1.AddressOfData);
            PVOID funcAddr = GetExportAddress(moduleBase, (PCHAR)importByName->Name);
            if (!funcAddr) return STATUS_PROCEDURE_NOT_FOUND;
            thunk->u1.Function = (ULONG_PTR)funcAddr;
            thunk++; origThunk++;
        }
        importDesc++;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS PerformRelocations(PVOID ImageBase, PIMAGE_NT_HEADERS NtHeaders, PVOID NewBase) {
    ULONG_PTR delta = (ULONG_PTR)NewBase - NtHeaders->OptionalHeader.ImageBase;
    if (delta == 0) return STATUS_SUCCESS;
    ULONG relocRva = NtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress;
    if (!relocRva) return STATUS_SUCCESS;
    PIMAGE_BASE_RELOCATION reloc = (PIMAGE_BASE_RELOCATION)((PUCHAR)ImageBase + relocRva);
    while (reloc->VirtualAddress) {
        PWORD entries = (PWORD)(reloc + 1);
        ULONG count = (reloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
        for (ULONG i = 0; i < count; i++) {
            if (entries[i] >> 12 == IMAGE_REL_BASED_DIR64) {
                ULONG_PTR* patchAddr = (ULONG_PTR*)((PUCHAR)ImageBase + reloc->VirtualAddress + (entries[i] & 0xFFF));
                *patchAddr += delta;
            }
        }
        reloc = (PIMAGE_BASE_RELOCATION)((PUCHAR)reloc + reloc->SizeOfBlock);
    }
    return STATUS_SUCCESS;
}

NTSTATUS MapDriver(PVOID ImageBuffer, SIZE_T ImageSize, PDRIVER_OBJECT *OutDriverObject) {
    if (!ImageBuffer || ImageSize < sizeof(IMAGE_DOS_HEADER)) return STATUS_INVALID_PARAMETER;
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)ImageBuffer;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return STATUS_INVALID_IMAGE_FORMAT;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((PUCHAR)ImageBuffer + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return STATUS_INVALID_IMAGE_FORMAT;
    if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) return STATUS_IMAGE_MACHINE_TYPE_MISMATCH;
    SIZE_T imageSize = nt->OptionalHeader.SizeOfImage;
    PVOID mappedBase = ExAllocatePoolWithTag(NonPagedPool, imageSize, 'paMn');
    if (!mappedBase) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(mappedBase, imageSize);
    RtlCopyMemory(mappedBase, ImageBuffer, nt->OptionalHeader.SizeOfHeaders);
    PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, section++)
        if (section->SizeOfRawData) RtlCopyMemory((PUCHAR)mappedBase + section->VirtualAddress, (PUCHAR)ImageBuffer + section->PointerToRawData, section->SizeOfRawData);
    if (!NT_SUCCESS(ResolveImports(mappedBase, nt))) { ExFreePoolWithTag(mappedBase, 'paMn'); return STATUS_UNSUCCESSFUL; }
    PerformRelocations(mappedBase, nt, mappedBase);
    typedef NTSTATUS (*PDRIVER_ENTRY)(PDRIVER_OBJECT, PUNICODE_STRING);
    PDRIVER_ENTRY entry = (PDRIVER_ENTRY)((PUCHAR)mappedBase + nt->OptionalHeader.AddressOfEntryPoint);
    PDRIVER_OBJECT driverObj = (PDRIVER_OBJECT)ExAllocatePoolWithTag(NonPagedPool, sizeof(DRIVER_OBJECT), 'bjrD');
    if (!driverObj) { ExFreePoolWithTag(mappedBase, 'paMn'); return STATUS_INSUFFICIENT_RESOURCES; }
    RtlZeroMemory(driverObj, sizeof(DRIVER_OBJECT));
    driverObj->DriverStart = mappedBase;
    driverObj->DriverSize = (ULONG)imageSize;
    UNICODE_STRING path = {0};
    NTSTATUS status = entry(driverObj, &path);
    if (!NT_SUCCESS(status)) { ExFreePoolWithTag(driverObj, 'bjrD'); ExFreePoolWithTag(mappedBase, 'paMn'); return status; }
    *OutDriverObject = driverObj;
    return STATUS_SUCCESS;
}

VOID UnmapDriver(PDRIVER_OBJECT DriverObject) {
    if (DriverObject) { ExFreePoolWithTag(DriverObject->DriverStart, 'paMn'); ExFreePoolWithTag(DriverObject, 'bjrD'); }
}
