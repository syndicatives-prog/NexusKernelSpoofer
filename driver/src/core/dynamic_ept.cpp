#include "dynamic_ept.h"
#include "common.h"
#include "hypervisor.h"

static KTIMER g_DynamicTimer;
static KDPC g_DynamicDpc;
static PVOID g_ReservedPages[8] = {0};
static ULONG g_ReservedIndex = 0;
static KSPIN_LOCK g_EptLock;

extern UINT64 g_HiddenPages[];
extern UCHAR* g_FakePages[];
extern ULONG g_PageCount;

static PVOID AllocContiguousPhysLocal(ULONG Size, UINT64* PhysAddr) {
    PHYSICAL_ADDRESS highest; highest.QuadPart = -1;
    PVOID buf = MmAllocateContiguousMemory(Size, highest);
    if (!buf) return NULL;
    if (PhysAddr) *PhysAddr = MmGetPhysicalAddress(buf).QuadPart;
    RtlZeroMemory(buf, Size);
    return buf;
}

static VOID RelocateOnePage(ULONG Index) {
    KIRQL oldIrql;
    KeAcquireSpinLock(&g_EptLock, &oldIrql);

    UINT64 oldPhys = g_HiddenPages[Index];
    UCHAR* oldFake = g_FakePages[Index];
    if (!oldPhys || !oldFake) {
        KeReleaseSpinLock(&g_EptLock, oldIrql);
        return;
    }
    if (g_ReservedIndex >= 8) {
        KeReleaseSpinLock(&g_EptLock, oldIrql);
        return;
    }
    PVOID newVa = g_ReservedPages[g_ReservedIndex];
    if (!newVa) {
        KeReleaseSpinLock(&g_EptLock, oldIrql);
        return;
    }
    UINT64 newPhys = MmGetPhysicalAddress(newVa).QuadPart;
    RtlCopyMemory(newVa, oldFake, 4096);

    PUINT64 pte = NULL;
    int retries = 0;
    while (!pte && retries < 10) {
        pte = EptSplitTo4Kb(g_Vmx.EptPml4Phys, oldPhys);
        retries++;
    }
    if (!pte) {
        KeReleaseSpinLock(&g_EptLock, oldIrql);
        return;
    }
    *pte = (newPhys & ~0xFFFULL) | (*pte & 0xFFF);
    UINT64 desc[2] = {g_Vmx.EptPml4Phys, 0};
    InvEpt(1, desc);

    MmFreeContiguousMemory(oldFake);
    g_HiddenPages[Index] = newPhys;
    g_FakePages[Index] = (UCHAR*)newVa;

    // Replenish the ring buffer slot with a new page
    PVOID newReserved = AllocContiguousPhysLocal(4096, NULL);
    if (newReserved) {
        g_ReservedPages[g_ReservedIndex] = newReserved;
    }
    g_ReservedIndex++;

    KeReleaseSpinLock(&g_EptLock, oldIrql);
}

static VOID DynamicEptDpc(PKDPC Dpc, PVOID DeferredContext, PVOID Arg1, PVOID Arg2) {
    g_ReservedIndex = 0;
    for (ULONG i = 0; i < g_PageCount; i++) if (g_HiddenPages[i]) RelocateOnePage(i);
    LARGE_INTEGER due; due.QuadPart = -10 * 1000 * 1000 * 10;
    KeSetTimer(&g_DynamicTimer, due, &g_DynamicDpc);
}

NTSTATUS InitDynamicEpt() {
    if (!g_Vmx.HypervisorActive) return STATUS_NOT_SUPPORTED;
    KeInitializeSpinLock(&g_EptLock);
    for (int i = 0; i < 8; i++) {
        g_ReservedPages[i] = AllocContiguousPhysLocal(4096, NULL);
        if (!g_ReservedPages[i]) return STATUS_INSUFFICIENT_RESOURCES;
    }
    KeInitializeTimer(&g_DynamicTimer);
    KeInitializeDpc(&g_DynamicDpc, DynamicEptDpc, NULL);
    LARGE_INTEGER due; due.QuadPart = -10 * 1000 * 1000 * 10;
    KeSetTimer(&g_DynamicTimer, due, &g_DynamicDpc);
    return STATUS_SUCCESS;
}

VOID CleanupDynamicEpt() {
    KeCancelTimer(&g_DynamicTimer);
    for (int i = 0; i < 8; i++) {
        if (g_ReservedPages[i]) MmFreeContiguousMemory(g_ReservedPages[i]); // Corregido
    }
}
