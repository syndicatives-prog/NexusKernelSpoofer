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

// Callback con firma correcta para KeGenericCallDpc
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
        __vmx_off();  // Ahora se ejecuta en el core correcto
        core->Active = FALSE;
    }
    if (core->VmcsVa)  { MmFreeContiguousMemory(core->VmcsVa);  core->VmcsVa  = NULL; }
    if (core->VmxonVa) { MmFreeContiguousMemory(core->VmxonVa); core->VmxonVa = NULL; }
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
    // Ejecutar __vmx_off en cada core que lo activ?
    KeGenericCallDpc(PerCoreCleanupCallback, NULL);
    ExFreePoolWithTag(g_PerCoreVmx, 'pmvS');
    g_PerCoreVmx = NULL;
}

BOOLEAN IsVmxActiveOnCurrentCore() {
    ULONG core = KeGetCurrentProcessorNumber();
    return (g_PerCoreVmx && core < g_NumCores && g_PerCoreVmx[core].Active);
}
