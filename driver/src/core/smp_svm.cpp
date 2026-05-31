#include "smp_svm.h"
#include "svm.h"
#include "hypervisor.h"

typedef struct _PER_CORE_SVM {
    UINT64 HostVmcbPhys;
    UINT64 GuestVmcbPhys;
    PVOID  HostVmcbVa;
    PVOID  GuestVmcbVa;
    BOOLEAN Active;
} PER_CORE_SVM;

static PER_CORE_SVM* g_PerCoreSvm = NULL;
static ULONG g_NumCores = 0;

// Firma corregida para KeGenericCallDpc
static VOID PerCoreInitCallback(PKDPC Dpc, PVOID Context, PVOID Arg1, PVOID Arg2) {
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);

    ULONG coreIndex = KeGetCurrentProcessorNumber();
    if (coreIndex >= g_NumCores) return;

    PER_CORE_SVM* core = &g_PerCoreSvm[coreIndex];

    UINT64 efer = __readmsr(0xC0000080);
    efer |= (1 << 12);
    __writemsr(0xC0000080, efer);

    PHYSICAL_ADDRESS highest; 
    highest.QuadPart = -1;
    PVOID hostVmcbVa = MmAllocateContiguousMemory(4096, highest);
    if (!hostVmcbVa) return;
    UINT64 hostVmcbPhys = MmGetPhysicalAddress(hostVmcbVa).QuadPart;
    RtlZeroMemory(hostVmcbVa, 4096);
    core->HostVmcbPhys = hostVmcbPhys;
    core->HostVmcbVa   = hostVmcbVa;

    PVOID guestVmcbVa = MmAllocateContiguousMemory(4096, highest);
    if (!guestVmcbVa) {
        MmFreeContiguousMemory(hostVmcbVa);
        return;
    }
    UINT64 guestVmcbPhys = MmGetPhysicalAddress(guestVmcbVa).QuadPart;
    RtlZeroMemory(guestVmcbVa, 4096);
    core->GuestVmcbPhys = guestVmcbPhys;
    core->GuestVmcbVa   = guestVmcbVa;

    // Build identity-mapped NPT for AMD (not Intel EPT)
    // Allocate NPT PML4 table
    PVOID nptPml4Va = MmAllocateContiguousMemory(4096, highest);
    if (!nptPml4Va) {
        MmFreeContiguousMemory(hostVmcbVa);
        MmFreeContiguousMemory(guestVmcbVa);
        return;
    }
    RtlZeroMemory(nptPml4Va, 4096);
    UINT64 nptPml4Phys = MmGetPhysicalAddress(nptPml4Va).QuadPart;
    
    // Allocate NPT PDPT table
    PVOID nptPdptVa = MmAllocateContiguousMemory(4096, highest);
    if (!nptPdptVa) {
        MmFreeContiguousMemory(nptPml4Va);
        MmFreeContiguousMemory(hostVmcbVa);
        MmFreeContiguousMemory(guestVmcbVa);
        return;
    }
    RtlZeroMemory(nptPdptVa, 4096);
    UINT64 nptPdptPhys = MmGetPhysicalAddress(nptPdptVa).QuadPart;
    
    // Link PML4 to PDPT (identity mapping for first 512 GB)
    ((PUINT64)nptPml4Va)[0] = nptPdptPhys | 7; // RWX present
    
    // Fill PDPT with 1 GB pages (identity map: GPA = HPA)
    PUINT64 pdpt = (PUINT64)nptPdptVa;
    for (int i = 0; i < 512; i++) {
        pdpt[i] = (i * 0x40000000ULL) | 0x87; // 1 GB page, RWX, present, large page
    }
    
    // Write N_CR3 (Nested CR3) in guest VMCB to point to NPT
    *(UINT64*)((PUCHAR)guestVmcbVa + 0x0150) = nptPml4Phys;
    
    // TODO: Store nptPml4Va and nptPdptVa in core structure for cleanup
    // For now, memory will leak. Better: add NPT pointers to PER_CORE_SVM struct
    
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
    for (ULONG i = 0; i < g_NumCores; i++) {
        if (g_PerCoreSvm[i].HostVmcbVa)  MmFreeContiguousMemory(g_PerCoreSvm[i].HostVmcbVa);
        if (g_PerCoreSvm[i].GuestVmcbVa) MmFreeContiguousMemory(g_PerCoreSvm[i].GuestVmcbVa);
    }
    ExFreePoolWithTag(g_PerCoreSvm, 'pmvA');
    g_PerCoreSvm = NULL;
}

BOOLEAN IsSvmActiveOnCurrentCore() {
    ULONG core = KeGetCurrentProcessorNumber();
    return (g_PerCoreSvm && core < g_NumCores && g_PerCoreSvm[core].Active);
}
