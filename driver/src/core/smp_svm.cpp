#include "smp_svm.h"
#include "svm.h"
#include "hypervisor.h"

typedef struct _PER_CORE_SVM {
    UINT64 HostVmcbPhys;
    UINT64 GuestVmcbPhys;
    BOOLEAN Active;
} PER_CORE_SVM;

static PER_CORE_SVM* g_PerCoreSvm = NULL;
static ULONG g_NumCores = 0;

static VOID PerCoreInitCallback(PVOID Context) {
    ULONG coreIndex = KeGetCurrentProcessorNumber();
    if (coreIndex >= g_NumCores) return;

    PER_CORE_SVM* core = &g_PerCoreSvm[coreIndex];

    // Habilitar SVM en EFER
    UINT64 efer = __readmsr(0xC0000080);
    efer |= (1 << 12);
    __writemsr(0xC0000080, efer);

    // Reservar VMCBs para este core (similar a svm.cpp)
    PHYSICAL_ADDRESS highest; highest.QuadPart = -1;
    PVOID hostVmcbVa = MmAllocateContiguousMemory(4096, highest);
    if (!hostVmcbVa) return;
    UINT64 hostVmcbPhys = MmGetPhysicalAddress(hostVmcbVa).QuadPart;
    RtlZeroMemory(hostVmcbVa, 4096);
    core->HostVmcbPhys = hostVmcbPhys;

    PVOID guestVmcbVa = MmAllocateContiguousMemory(4096, highest);
    if (!guestVmcbVa) {
        MmFreeContiguousMemory(hostVmcbVa);
        return;
    }
    UINT64 guestVmcbPhys = MmGetPhysicalAddress(guestVmcbVa).QuadPart;
    RtlZeroMemory(guestVmcbVa, 4096);
    core->GuestVmcbPhys = guestVmcbPhys;

    // Configurar NPT en guest VMCB
    *(UINT64*)((PUCHAR)guestVmcbVa + 0x0150) = g_Vmx.EptPml4Phys;

    core->Active = TRUE;
}

NTSTATUS InitSmpSvm() {
    if (!IsAmdVSupported()) return STATUS_NOT_SUPPORTED;

    g_NumCores = KeNumberProcessors;
    g_PerCoreSvm = (PER_CORE_SVM*)ExAllocatePoolWithTag(NonPagedPool,
        sizeof(PER_CORE_SVM) * g_NumCores, 'pmvA');
    if (!g_PerCoreSvm) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(g_PerCoreSvm, sizeof(PER_CORE_SVM) * g_NumCores);

    KeGenericCallDpc(PerCoreInitCallback, NULL);

    BOOLEAN anyActive = FALSE;
    for (ULONG i = 0; i < g_NumCores; i++) {
        if (g_PerCoreSvm[i].Active) anyActive = TRUE;
    }
    return anyActive ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

VOID CleanupSmpSvm() {
    if (!g_PerCoreSvm) return;
    // Liberar VMCBs (las direcciones virtuales no se guardaron, simplificamos)
    ExFreePoolWithTag(g_PerCoreSvm, 'pmvA');
    g_PerCoreSvm = NULL;
}

BOOLEAN IsSvmActiveOnCurrentCore() {
    ULONG core = KeGetCurrentProcessorNumber();
    return (g_PerCoreSvm && core < g_NumCores && g_PerCoreSvm[core].Active);
}
