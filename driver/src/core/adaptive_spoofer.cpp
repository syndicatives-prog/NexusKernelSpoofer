#include "adaptive_spoofer.h"
#include "common.h"
#include "hypervisor.h"

static KTIMER g_AdaptiveTimer;
static KDPC g_AdaptiveDpc;
static BOOLEAN g_AdaptiveActive = FALSE;

// Lista de m?dulos de AC conocidos (ampliable)
static const WCHAR* g_AcModules[] = {
    L"EasyAntiCheat.sys",
    L"BEService.sys",
    L"vgk.sys",
    L"FACEIT.sys",
    NULL
};

extern SPOOF_DATA g_SpoofData;
extern UINT64 g_DiskPhys, g_GpuPhys, g_MacPhys, g_SmbiosPhysAddr;
extern UCHAR g_FakeDiskSerialPage[4096], g_FakeGpuConfigPage[4096], g_FakeMacPage[4096], g_FakeSmbiosPage[4096];

static BOOLEAN IsAcModuleLoaded(const WCHAR* ModuleName) {
    // Buscar el m?dulo en la lista del sistema
    ULONG size = 0;
    ZwQuerySystemInformation(SystemModuleInformation, NULL, 0, &size);
    if (size == 0) return FALSE;
    PVOID buf = ExAllocatePoolWithTag(NonPagedPool, size, 'mdAc');
    if (!buf) return FALSE;
    NTSTATUS status = ZwQuerySystemInformation(SystemModuleInformation, buf, size, &size);
    if (!NT_SUCCESS(status)) { ExFreePoolWithTag(buf, 'mdAc'); return FALSE; }
    PSYSTEM_MODULE_INFORMATION modInfo = (PSYSTEM_MODULE_INFORMATION)buf;
    BOOLEAN found = FALSE;
    for (ULONG i = 0; i < modInfo->ModulesCount; i++) {
        PCHAR path = (PCHAR)modInfo->Modules[i].FullPathName + modInfo->Modules[i].OffsetToFileName;
        UNICODE_STRING uniPath;
        ANSI_STRING ansiPath;
        RtlInitAnsiString(&ansiPath, path);
        RtlAnsiStringToUnicodeString(&uniPath, &ansiPath, TRUE);
        if (wcsstr(uniPath.Buffer, ModuleName)) { found = TRUE; RtlFreeUnicodeString(&uniPath); break; }
        RtlFreeUnicodeString(&uniPath);
    }
    ExFreePoolWithTag(buf, 'mdAc');
    return found;
}

static VOID AdaptToEnvironment() {
    if (IsAcModuleLoaded(L"vgk.sys")) {
        // Vanguard: perfil ultra?limpio
        RtlStringCbCopyA(g_SpoofData.DiskSerial, sizeof(g_SpoofData.DiskSerial), "SAMSUNG_MZVLW256HEHP-000L7");
        RtlStringCbCopyA(g_SpoofData.SystemManufacturer, sizeof(g_SpoofData.SystemManufacturer), "Dell Inc.");
        // Actualizar p?ginas falsas (se podr?a hacer aqu? o esperar a la siguiente rotaci?n)
    } else if (IsAcModuleLoaded(L"BEService.sys")) {
        // BattlEye: MAC aleatoria con mejor variación
        g_SpoofData.MacAddress[0] = 0x00; 
        g_SpoofData.MacAddress[1] = 0x15; 
        g_SpoofData.MacAddress[2] = 0x5D;
        // Use high-resolution timer for better randomness instead of correlated rdtsc
        LARGE_INTEGER time;
        KeQuerySystemTime(&time);
        UINT32 seed = (UINT32)time.LowPart;
        for (int j = 3; j < 6; j++) {
            seed = seed * 1103515245 + 12345;  // Simple LCG for variety
            g_SpoofData.MacAddress[j] = (UCHAR)((seed >> 16) & 0xFF);
        }
    }
    // Si no hay AC, dejamos los valores por defecto
}

static VOID AdaptiveDpc(PKDPC Dpc, PVOID DeferredContext, PVOID Arg1, PVOID Arg2) {
    AdaptToEnvironment();
    LARGE_INTEGER due; due.QuadPart = -60 * 1000 * 1000 * 10;
    KeSetTimer(&g_AdaptiveTimer, due, &g_AdaptiveDpc);
}

NTSTATUS InitAdaptiveEngine() {
    if (!g_Vmx.HypervisorActive) return STATUS_NOT_SUPPORTED;
    KeInitializeTimer(&g_AdaptiveTimer);
    KeInitializeDpc(&g_AdaptiveDpc, AdaptiveDpc, NULL);
    LARGE_INTEGER due; due.QuadPart = -60 * 1000 * 1000 * 10;
    KeSetTimer(&g_AdaptiveTimer, due, &g_AdaptiveDpc);
    g_AdaptiveActive = TRUE;
    return STATUS_SUCCESS;
}

VOID CleanupAdaptiveEngine() {
    if (g_AdaptiveActive) { KeCancelTimer(&g_AdaptiveTimer); g_AdaptiveActive = FALSE; }
}
