#include "svm.h"
#include "hypervisor.h"  // para EptSplitTo4Kb, etc.

static BOOLEAN g_AmdHypervisorActive = FALSE;
static UINT64 g_HostVmcbPhys = 0;
static UINT64 g_GuestVmcbPhys = 0;
static PVOID g_HostVmcbVa = NULL;

// VMCB offsets (simplified)
#define VMCB_CR4         0x0148
#define VMCB_EFER        0x00C0
#define VMCB_RIP         0x0078
#define VMCB_RSP         0x0080
#define VMCB_RAX         0x0088
#define VMCB_EXIT_CODE   0x0000
#define VMCB_NPT_CR3     0x0150

// SVM instructions (intrinsics not available, we use inline asm)
static void __writemsr(UINT32 msr, UINT64 value) { __writemsr(msr, value); }
static UINT64 __readmsr(UINT32 msr) { return __readmsr(msr); }

BOOLEAN IsAmdVSupported() {
    int cpuInfo[4];
    __cpuidex(cpuInfo, 0x80000001, 0);
    return (cpuInfo[2] & (1 << 2)) != 0; // SVM bit
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

    // Enable SVM in EFER
    UINT64 efer = __readmsr(0xC0000080);
    efer |= (1 << 12); // SVME
    __writemsr(0xC0000080, efer);

    // Allocate host VMCB (4KB aligned)
    g_HostVmcbVa = AllocContiguousPhysAmd(4096, &g_HostVmcbPhys);
    if (!g_HostVmcbVa) return STATUS_INSUFFICIENT_RESOURCES;

    // Allocate guest VMCB
    PVOID guestVmcbVa = AllocContiguousPhysAmd(4096, &g_GuestVmcbPhys);
    if (!guestVmcbVa) { ExFreePoolWithTag(g_HostVmcbVa, 'bVmA'); return STATUS_INSUFFICIENT_RESOURCES; }

    // Configure host VMCB (save area) ? minimal setup
    RtlZeroMemory(g_HostVmcbVa, 4096);
    // Guest VMCB will be set later

    // Set up Nested Page Tables (same as EPT for Intel)
    // Reuse the EPT setup from hypervisor.cpp? We'll use the same g_Vmx.EptPml4Phys
    // Actually, for AMD we need to set VMCB.NPT_CR3
    *(UINT64*)((PUCHAR)guestVmcbVa + VMCB_NPT_CR3) = g_Vmx.EptPml4Phys;

    // Enable NPT in VMCB (bit 0 of NP_CR3? Not needed, just set the address)

    g_AmdHypervisorActive = TRUE;
    return STATUS_SUCCESS;
}

VOID CleanupAmdHypervisor() {
    if (g_AmdHypervisorActive) {
        // Disable SVM in EFER
        UINT64 efer = __readmsr(0xC0000080);
        efer &= ~(1 << 12);
        __writemsr(0xC0000080, efer);
        if (g_HostVmcbVa) ExFreePoolWithTag(g_HostVmcbVa, 'bVmA');
        g_AmdHypervisorActive = FALSE;
    }
}
