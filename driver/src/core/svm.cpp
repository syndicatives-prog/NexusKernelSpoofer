#include "svm.h"
#include "hypervisor.h"

static BOOLEAN g_AmdHypervisorActive = FALSE;
static UINT64 g_HostVmcbPhys = 0;
static UINT64 g_GuestVmcbPhys = 0;
static PVOID g_HostVmcbVa = NULL;
static PVOID g_GuestVmcbVa = NULL;
static UINT64 g_AmdNptPml4Phys = 0;

BOOLEAN IsAmdVSupported() {
    int cpuInfo[4];
    __cpuidex(cpuInfo, 0x80000001, 0);
    return (cpuInfo[2] & (1 << 2)) != 0;
}

static PVOID AllocContiguousPhysAmd(ULONG Size, UINT64* PhysAddr) {
    PHYSICAL_ADDRESS highest; highest.QuadPart = -1;
    PVOID buf = MmAllocateContiguousMemory(Size, highest);
    if (!buf) return NULL;
    if (PhysAddr) *PhysAddr = MmGetPhysicalAddress(buf).QuadPart;
    RtlZeroMemory(buf, Size);
    return buf;
}

NTSTATUS InitAmdHypervisor() {
    if (!IsAmdVSupported()) return STATUS_HV_FEATURE_UNAVAILABLE;

    UINT64 efer = __readmsr(0xC0000080);
    efer |= (1 << 12);
    __writemsr(0xC0000080, efer);

    g_HostVmcbVa = AllocContiguousPhysAmd(4096, &g_HostVmcbPhys);
    if (!g_HostVmcbVa) return STATUS_INSUFFICIENT_RESOURCES;

    g_GuestVmcbVa = AllocContiguousPhysAmd(4096, &g_GuestVmcbPhys);
    if (!g_GuestVmcbVa) { MmFreeContiguousMemory(g_HostVmcbVa); g_HostVmcbVa = NULL; return STATUS_INSUFFICIENT_RESOURCES; }

    // Crear NPT independiente de Intel (no usar g_Vmx.EptPml4Phys)
    PVOID pml4Va = AllocContiguousPhysAmd(4096, &g_AmdNptPml4Phys);
    if (!pml4Va) { MmFreeContiguousMemory(g_GuestVmcbVa); MmFreeContiguousMemory(g_HostVmcbVa); g_GuestVmcbVa = g_HostVmcbVa = NULL; return STATUS_INSUFFICIENT_RESOURCES; }
    RtlZeroMemory(pml4Va, 4096);
    UINT64 pdptPhys;
    PVOID pdptVa = AllocContiguousPhysAmd(4096, &pdptPhys);
    if (!pdptVa) { MmFreeContiguousMemory(pml4Va); MmFreeContiguousMemory(g_GuestVmcbVa); MmFreeContiguousMemory(g_HostVmcbVa); g_GuestVmcbVa = g_HostVmcbVa = NULL; return STATUS_INSUFFICIENT_RESOURCES; }
    RtlZeroMemory(pdptVa, 4096);
    ((PUINT64)pml4Va)[0] = pdptPhys | 7;
    PUINT64 pdpt = (PUINT64)pdptVa;
    for (int i = 0; i < 512; i++) pdpt[i] = (i * 0x40000000ULL) | 0x87;

    // Configurar VMCB guest con NPT
    RtlZeroMemory(g_GuestVmcbVa, 4096);
    *(UINT64*)((PUCHAR)g_GuestVmcbVa + 0x0150) = g_AmdNptPml4Phys;

    g_AmdHypervisorActive = TRUE;
    return STATUS_SUCCESS;
}

VOID CleanupAmdHypervisor() {
    if (g_AmdHypervisorActive) {
        UINT64 efer = __readmsr(0xC0000080);
        efer &= ~(1 << 12);
        __writemsr(0xC0000080, efer);
    }
    if (g_HostVmcbVa)  { MmFreeContiguousMemory(g_HostVmcbVa);  g_HostVmcbVa  = NULL; }
    if (g_GuestVmcbVa) { MmFreeContiguousMemory(g_GuestVmcbVa); g_GuestVmcbVa = NULL; }
    // liberar NPT pml4/pdpt
    g_AmdHypervisorActive = FALSE;
}
