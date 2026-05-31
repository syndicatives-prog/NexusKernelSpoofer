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

static VOID AllocContiguousPhysLocal(ULONG Size, UINT64* PhysAddr, PVOID* VaOut) {
    PHYSICAL_ADDRESS highest; highest.QuadPart = -1;
    PVOID buf = MmAllocateContiguousMemory(Size, highest);
    if (buf) {
        if (PhysAddr) *PhysAddr = MmGetPhysicalAddress(buf).QuadPart;
        RtlZeroMemory(buf, Size);
    }
    if (VaOut) *VaOut = buf;
}

// Callback con firma correcta para KeGenericCallDpc - crea EPT per-core
static VOID PerCoreInitCallback(PKDPC Dpc, PVOID Context, PVOID Arg1, PVOID Arg2) {
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);

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

    // MEJORA 1: Crear EPT por-core
    if (g_Vmx.PerCoreEpt && coreIndex < g_Vmx.NumCores) {
        PER_CORE_EPT* eptData = &g_Vmx.PerCoreEpt[coreIndex];
        
        // Allocate PML4 for this core
        UINT64 eptPml4Phys;
        PVOID eptPml4Va;
        AllocContiguousPhysLocal(4096, &eptPml4Phys, &eptPml4Va);
        if (!eptPml4Va) {
            __vmx_off();
            MmFreeContiguousMemory(vmcsVa);
            MmFreeContiguousMemory(vmxonVa);
            return;
        }

        // Allocate PDPT for this core (init with 1GB pages)
        UINT64 pdptPhys;
        PVOID pdptVa;
        AllocContiguousPhysLocal(4096, &pdptPhys, &pdptVa);
        if (!pdptVa) {
            MmFreeContiguousMemory(eptPml4Va);
            __vmx_off();
            MmFreeContiguousMemory(vmcsVa);
            MmFreeContiguousMemory(vmxonVa);
            return;
        }

        // Link PML4 to PDPT
        ((PUINT64)eptPml4Va)[0] = pdptPhys | 7;
        PUINT64 pdpt = (PUINT64)pdptVa;
        for (int i = 0; i < 512; i++) pdpt[i] = (i * 0x40000000ULL) | 0x87;

        eptData->EptPml4Phys = eptPml4Phys;
        eptData->EptPml4Va = eptPml4Va;
        eptData->PdptVa = pdptVa;

        // Configure EPTP for this core
        UINT64 eptp = eptPml4Phys | (6ULL << 3) | 3;
        __vmx_vmwrite(VMCS_EPTP, eptp);
    }

    core->Active = TRUE;
}

// Callback de limpieza ejecutado en cada core
static VOID PerCoreCleanupCallback(PKDPC Dpc, PVOID Context, PVOID Arg1, PVOID Arg2) {
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);

    ULONG coreIndex = KeGetCurrentProcessorNumber();
    if (coreIndex >= g_NumCores) return;

    PER_CORE_VMX* core = &g_PerCoreVmx[coreIndex];
    if (core->Active) {
        __vmx_off();
        core->Active = FALSE;
    }
    if (core->VmcsVa)  { MmFreeContiguousMemory(core->VmcsVa);  core->VmcsVa  = NULL; }
    if (core->VmxonVa) { MmFreeContiguousMemory(core->VmxonVa); core->VmxonVa = NULL; }

    // Clean up per-core EPT
    if (g_Vmx.PerCoreEpt && coreIndex < g_Vmx.NumCores) {
        PER_CORE_EPT* eptData = &g_Vmx.PerCoreEpt[coreIndex];
        if (eptData->EptPml4Va) MmFreeContiguousMemory(eptData->EptPml4Va);
        if (eptData->PdptVa) MmFreeContiguousMemory(eptData->PdptVa);
        eptData->EptPml4Phys = 0;
        eptData->EptPml4Va = NULL;
        eptData->PdptVa = NULL;
    }
}

NTSTATUS InitSmpVmx() {
    if (!g_Vmx.HypervisorActive) return STATUS_NOT_SUPPORTED;
    g_NumCores = KeNumberProcessors;
    g_Vmx.NumCores = g_NumCores;

    g_PerCoreVmx = (PER_CORE_VMX*)ExAllocatePoolWithTag(NonPagedPool,
        sizeof(PER_CORE_VMX) * g_NumCores, 'pmvS');
    if (!g_PerCoreVmx) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(g_PerCoreVmx, sizeof(PER_CORE_VMX) * g_NumCores);

    // Allocate per-core EPT array
    g_Vmx.PerCoreEpt = (PER_CORE_EPT*)ExAllocatePoolWithTag(NonPagedPool,
        sizeof(PER_CORE_EPT) * g_NumCores, 'tpES');
    if (!g_Vmx.PerCoreEpt) {
        ExFreePoolWithTag(g_PerCoreVmx, 'pmvS');
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(g_Vmx.PerCoreEpt, sizeof(PER_CORE_EPT) * g_NumCores);

    KeGenericCallDpc(PerCoreInitCallback, NULL);

    BOOLEAN anyActive = FALSE;
    for (ULONG i = 0; i < g_NumCores; i++) {
        if (g_PerCoreVmx[i].Active) anyActive = TRUE;
    }
    return anyActive ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

VOID CleanupSmpVmx() {
    if (!g_PerCoreVmx) return;
    KeGenericCallDpc(PerCoreCleanupCallback, NULL);
    ExFreePoolWithTag(g_PerCoreVmx, 'pmvS');
    g_PerCoreVmx = NULL;

    if (g_Vmx.PerCoreEpt) {
        ExFreePoolWithTag(g_Vmx.PerCoreEpt, 'tpES');
        g_Vmx.PerCoreEpt = NULL;
    }
}

BOOLEAN IsVmxActiveOnCurrentCore() {
    ULONG core = KeGetCurrentProcessorNumber();
    return (g_PerCoreVmx && core < g_NumCores && g_PerCoreVmx[core].Active);
}

UINT64 GetSmpCoreEptPml4Phys() {
    ULONG core = KeGetCurrentProcessorNumber();
    if (g_Vmx.PerCoreEpt && core < g_Vmx.NumCores) {
        return g_Vmx.PerCoreEpt[core].EptPml4Phys;
    }
    return g_Vmx.EptPml4Phys;
}

