#include "dynamic_ept.h"
#include "common.h"
#include "hypervisor.h"
static KTIMER g_DynamicTimer;
static KDPC g_DynamicDpc;

extern UINT64 g_HiddenPages[];
extern UCHAR* g_FakePages[];
extern ULONG g_PageCount;

static VOID RelocateOnePage(ULONG Index) {
    UINT64 oldPhys = g_HiddenPages[Index];
    UCHAR* oldFake = g_FakePages[Index];
    if (!oldPhys || !oldFake) return;
    UINT64 newPhys;
    PVOID newVa = AllocContiguousPhys(4096, &newPhys);
    if (!newVa) return;
    RtlCopyMemory(newVa, oldFake, 4096);
    PUINT64 pte = NULL;
    while (!pte) pte = EptSplitTo4Kb(g_Vmx.EptPml4Phys, oldPhys);
    *pte = (newPhys & ~0xFFFULL) | (*pte & 0xFFF);
    __invept(1, NULL);
    ExFreePoolWithTag(oldFake, 'ekaF');
    g_HiddenPages[Index] = newPhys;
    g_FakePages[Index] = (UCHAR*)newVa;
}

static VOID DynamicEptDpc(PKDPC Dpc, PVOID DeferredContext, PVOID Arg1, PVOID Arg2) {
    for (ULONG i = 0; i < g_PageCount; i++) if (g_HiddenPages[i]) RelocateOnePage(i);
    LARGE_INTEGER due; due.QuadPart = -10 * 1000 * 1000 * 10;
    KeSetTimer(&g_DynamicTimer, due, &g_DynamicDpc);
}

NTSTATUS InitDynamicEpt() {
    if (!g_Vmx.HypervisorActive) return STATUS_NOT_SUPPORTED;
    KeInitializeTimer(&g_DynamicTimer);
    KeInitializeDpc(&g_DynamicDpc, DynamicEptDpc, NULL);
    LARGE_INTEGER due; due.QuadPart = -10 * 1000 * 1000 * 10;
    KeSetTimer(&g_DynamicTimer, due, &g_DynamicDpc);
    return STATUS_SUCCESS;
}

VOID CleanupDynamicEpt() { KeCancelTimer(&g_DynamicTimer); }
