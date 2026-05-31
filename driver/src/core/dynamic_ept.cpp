#include "dynamic_ept.h"
#include "common.h"
#include "hypervisor.h"

static KTIMER g_DynamicTimer;
static KDPC g_DynamicDpc;
static PVOID g_ReservedPages[8] = {0};
static ULONG g_ReservedIndex = 0;
static KSPIN_LOCK g_EptLock;
// BUG FIX: Pre-allocated pages para evitar MmAllocateContiguousMemory en DISPATCH_LEVEL
static PVOID g_PreAllocatedPages[16] = {0};
static ULONG g_PreAllocCount = 0;

extern UINT64 g_HiddenPages[];
extern UCHAR* g_FakePages[];
extern ULONG g_PageCount;

// Obtener una página pre-asignada (safe en DISPATCH_LEVEL)
static PVOID GetPreAllocatedPage() {
    KIRQL oldIrql;
    KeAcquireSpinLock(&g_EptLock, &oldIrql);
    
    PVOID page = NULL;
    if (g_PreAllocCount > 0) {
        page = g_PreAllocatedPages[g_PreAllocCount - 1];
        g_PreAllocatedPages[g_PreAllocCount - 1] = NULL;
        g_PreAllocCount--;
    }
    
    KeReleaseSpinLock(&g_EptLock, oldIrql);
    return page;
}

// Devolver una página al pool pre-asignado (safe en DISPATCH_LEVEL)
static VOID ReturnPreAllocatedPage(PVOID page) {
    if (!page) return;
    
    KIRQL oldIrql;
    KeAcquireSpinLock(&g_EptLock, &oldIrql);
    
    if (g_PreAllocCount < 16) {
        g_PreAllocatedPages[g_PreAllocCount] = page;
        g_PreAllocCount++;
    } else {
        // Pool lleno, liberar
        MmFreeContiguousMemory(page);
    }
    
    KeReleaseSpinLock(&g_EptLock, oldIrql);
}

// Asignar página contígua (solo en PASSIVE_LEVEL)
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

    // Bounds check BEFORE accessing array
    if (g_ReservedIndex >= 8) {
        KeReleaseSpinLock(&g_EptLock, oldIrql);
        return;
    }

    UINT64 oldPhys = g_HiddenPages[Index];
    UCHAR* oldFake = g_FakePages[Index];
    if (!oldPhys || !oldFake) {
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
    PVOID ptMapping = NULL;
    int retries = 0;
    while (!pte && retries < 10) {
        // BUG FIX: Usar GetCurrentCoreEptPml4Phys en lugar de g_Vmx.EptPml4Phys
        pte = EptSplitTo4Kb(GetCurrentCoreEptPml4Phys(), oldPhys, &ptMapping);
        retries++;
    }
    if (!pte) {
        KeReleaseSpinLock(&g_EptLock, oldIrql);
        return;
    }
    *pte = (newPhys & ~0xFFFULL) | (*pte & 0xFFF);
    InvEptCurrentContext();
    if (ptMapping) MmUnmapIoSpace(ptMapping, 4096);

    MmFreeContiguousMemory(oldFake);
    g_HiddenPages[Index] = newPhys;
    g_FakePages[Index] = (UCHAR*)newVa;

    // Save the index before incrementing
    ULONG currentIndex = g_ReservedIndex;
    g_ReservedIndex++;

    // BUG FIX: Usar pre-allocated page del pool en lugar de allocar nueva
    PVOID newReserved = GetPreAllocatedPage();
    if (!newReserved) {
        // Pool vacío - intentar reflenar en PASSIVE_LEVEL después
        g_ReservedPages[currentIndex] = NULL;
    } else {
        g_ReservedPages[currentIndex] = newReserved;
    }

    KeReleaseSpinLock(&g_EptLock, oldIrql);
}


static VOID DynamicEptDpc(PKDPC Dpc, PVOID DeferredContext, PVOID Arg1, PVOID Arg2) {
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    
    // BUG FIX: Proteger acceso a g_ReservedIndex con spinlock
    // La DPC se ejecuta en un solo core a la vez, pero RelocateOnePage
    // también puede ser llamada desde EPT violations en otros cores
    KIRQL oldIrql;
    KeAcquireSpinLock(&g_EptLock, &oldIrql);
    
    // Reiniciar el índice de forma segura bajo spinlock
    g_ReservedIndex = 0;
    
    KeReleaseSpinLock(&g_EptLock, oldIrql);
    
    // Ahora procesar páginas (cada RelocateOnePage adquiere su propio spinlock)
    for (ULONG i = 0; i < g_PageCount; i++) {
        if (g_HiddenPages[i]) RelocateOnePage(i);
    }
    
    // Rescheduler el timer
    LARGE_INTEGER due; 
    due.QuadPart = -10 * 1000 * 1000 * 10;  // 10 segundos
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
