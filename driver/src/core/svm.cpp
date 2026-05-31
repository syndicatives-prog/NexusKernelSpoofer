#include "svm.h"
#include "hypervisor.h"
#include <intrin.h>

#define MSR_EFER          0xC0000080
#define MSR_VM_CR         0xC0010114
#define MSR_HSAVE_PA      0xC0010117
#define EFER_SVME         (1ULL << 12)
#define CPUID_SVM_LEAF    0x80000001
#define CPUID_SVM_ECX_BIT (1 << 2)

static BOOLEAN g_AmdHypervisorActive = FALSE;
static PVOID   g_HostSaveAreaVa = NULL;
static UINT64  g_HostSaveAreaPhys = 0;

BOOLEAN IsAmdVSupported() {
    int cpuInfo[4];
    __cpuid(cpuInfo, 0x80000001);
    if (!(cpuInfo[2] & CPUID_SVM_ECX_BIT)) return FALSE;
    // Check that SVM is not disabled by MSR_VM_CR
    UINT64 vmcr = __readmsr(MSR_VM_CR);
    if (vmcr & (1ULL << 4)) return FALSE; // SVMDIS bit
    return TRUE;
}

NTSTATUS InitAmdHypervisor() {
    if (!IsAmdVSupported()) return STATUS_HV_FEATURE_UNAVAILABLE;

    // Enable SVME in EFER
    UINT64 efer = __readmsr(MSR_EFER);
    efer |= EFER_SVME;
    __writemsr(MSR_EFER, efer);

    // Allocate host save area (4 KB, physically aligned)
    PHYSICAL_ADDRESS highest; 
    highest.QuadPart = -1;
    g_HostSaveAreaVa = MmAllocateContiguousMemory(4096, highest);
    if (!g_HostSaveAreaVa) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(g_HostSaveAreaVa, 4096);
    g_HostSaveAreaPhys = MmGetPhysicalAddress(g_HostSaveAreaVa).QuadPart;

    // Point MSR_HSAVE_PA to the host save area
    __writemsr(MSR_HSAVE_PA, g_HostSaveAreaPhys);

    g_AmdHypervisorActive = TRUE;
    return STATUS_SUCCESS;
}

VOID CleanupAmdHypervisor() {
    if (!g_AmdHypervisorActive) return;
    // Disable SVME
    UINT64 efer = __readmsr(MSR_EFER);
    efer &= ~EFER_SVME;
    __writemsr(MSR_EFER, efer);
    if (g_HostSaveAreaVa) {
        MmFreeContiguousMemory(g_HostSaveAreaVa);
        g_HostSaveAreaVa = NULL;
    }
    g_AmdHypervisorActive = FALSE;
}
