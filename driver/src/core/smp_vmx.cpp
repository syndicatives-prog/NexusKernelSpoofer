#include "smp_vmx.h"
#include "hypervisor.h"

typedef struct _PER_CORE_VMX {
    UINT64 VmxonPhys;
    UINT64 VmcsPhys;
    PVOID  VmxonVa;
    PVOID  VmcsVa;
    BOOLEAN Active;
} PER_CORE_VMX;

static PER_CORE_VMX* g_PerCoreVmx = NULL;
static ULONG g_NumCores = 0;

static VOID PerCoreInitCallback(PVOID Context) {
    ULONG coreIndex = KeGetCurrentProcessorNumber();
    if (coreIndex >= g_NumCores) return;

    PER_CORE_VMX* core = &g_PerCoreVmx[coreIndex];

    UINT64 cr4 = __readcr4();
    __writecr4(cr4 | (1ULL << 13));

    PHYSICAL_ADDRESS highest; highest.QuadPart = -1;
    PVOID vmxonVa = MmAllocateContiguousMemory(4096, highest);
    if (!vmxonVa) return;
    UINT64 vmxonPhys = MmGetPhysicalAddress(vmxonVa).QuadPart;
    *(UINT64*)vmxonVa = g_Vmx.VmcsRevisionId;
    core->VmxonPhys = vmxonPhys;
    core->VmxonVa   = vmxonVa;

    if (__vmx_on(&core->VmxonPhys)) {
        MmFreeContiguousMemory(vmxonVa);
        return;
    }

    PVOID vmcsVa = MmAllocateContiguousMemory(4096, highest);
    if (!vmcsVa) {
        __vmx_off();
        MmFreeContiguousMemory(vmxonVa);
        return;
    }
    UINT64 vmcsPhys = MmGetPhysicalAddress(vmcsVa).QuadPart;
    *(UINT64*)vmcsVa = g_Vmx.VmcsRevisionId;
    core->VmcsPhys = vmcsPhys;
    core->VmcsVa   = vmcsVa;

    if (__vmx_vmclear(&core->VmcsPhys) || __vmx_vmptrld(&core->VmcsPhys)) {
        __vmx_off();
        MmFreeContiguousMemory(vmcsVa);
        MmFreeContiguousMemory(vmxonVa);
        return;
    }

    core->Active = TRUE;
}

NTSTATUS InitSmpVmx() {
    if (!g_Vmx.HypervisorActive) return STATUS_NOT_SUPPORTED;
    g_NumCores = KeNumberProcessors;
    g_PerCoreVmx = (PER_CORE_VMX*)ExAllocatePoolWithTag(NonPagedPool,
        sizeof(PER_CORE_VMX) * g_NumCores, 'pmvS');
    if (!g_PerCoreVmx) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(g_PerCoreVmx, sizeof(PER_CORE_VMX) * g_NumCores);

    KeGenericCallDpc(PerCoreInitCallback, NULL);

    BOOLEAN anyActive = FALSE;
    for (ULONG i = 0; i < g_NumCores; i++) {
        if (g_PerCoreVmx[i].Active) anyActive = TRUE;
    }
    return anyActive ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

VOID CleanupSmpVmx() {
    if (!g_PerCoreVmx) return;
    for (ULONG i = 0; i < g_NumCores; i++) {
        if (g_PerCoreVmx[i].Active) {
            __vmx_off();
        }
        if (g_PerCoreVmx[i].VmcsVa)  MmFreeContiguousMemory(g_PerCoreVmx[i].VmcsVa);
        if (g_PerCoreVmx[i].VmxonVa) MmFreeContiguousMemory(g_PerCoreVmx[i].VmxonVa);
    }
    ExFreePoolWithTag(g_PerCoreVmx, 'pmvS');
    g_PerCoreVmx = NULL;
}

BOOLEAN IsVmxActiveOnCurrentCore() {
    ULONG core = KeGetCurrentProcessorNumber();
    return (g_PerCoreVmx && core < g_NumCores && g_PerCoreVmx[core].Active);
}
