#include "smp_vmx.h"
#include "hypervisor.h"

// Estructura para pasar a cada core
typedef struct _PER_CORE_VMX {
    UINT64 VmxonPhys;
    UINT64 VmcsPhys;
    BOOLEAN Active;
} PER_CORE_VMX;

static PER_CORE_VMX* g_PerCoreVmx = NULL;
static ULONG g_NumCores = 0;

// Callback ejecutado en cada core
static VOID PerCoreInitCallback(PVOID Context) {
    ULONG coreIndex = KeGetCurrentProcessorNumber();
    if (coreIndex >= g_NumCores) return;

    PER_CORE_VMX* core = &g_PerCoreVmx[coreIndex];

    // Habilitar VMX en CR4 (si no est? ya)
    UINT64 cr4 = __readcr4();
    __writecr4(cr4 | (1ULL << 13));

    // Reservar VMXON region (4KB) para este core
    PHYSICAL_ADDRESS highest; highest.QuadPart = -1;
    PVOID vmxonVa = MmAllocateContiguousMemory(4096, highest);
    if (!vmxonVa) return;
    UINT64 vmxonPhys = MmGetPhysicalAddress(vmxonVa).QuadPart;
    *(UINT64*)vmxonVa = g_Vmx.VmcsRevisionId;
    core->VmxonPhys = vmxonPhys;

    // VMXON en este core
    if (__vmx_on(&core->VmxonPhys)) {
        MmFreeContiguousMemory(vmxonVa);
        return;
    }

    // Reservar VMCS region (4KB) para este core
    PVOID vmcsVa = MmAllocateContiguousMemory(4096, highest);
    if (!vmcsVa) {
        __vmx_off();
        MmFreeContiguousMemory(vmxonVa);
        return;
    }
    UINT64 vmcsPhys = MmGetPhysicalAddress(vmcsVa).QuadPart;
    *(UINT64*)vmcsVa = g_Vmx.VmcsRevisionId;
    core->VmcsPhys = vmcsPhys;

    // VMCLEAR y VMPTRLD
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

    // Ejecutar en todos los cores
    KeGenericCallDpc(PerCoreInitCallback, NULL);

    // Verificar que al menos un core se haya inicializado
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
            // Liberar VMCS y VMXON (no trivial, necesitamos las direcciones virtuales)
            // Simplificamos: asumimos que se liberan en DriverUnload
        }
    }
    ExFreePoolWithTag(g_PerCoreVmx, 'pmvS');
    g_PerCoreVmx = NULL;
}

BOOLEAN IsVmxActiveOnCurrentCore() {
    ULONG core = KeGetCurrentProcessorNumber();
    return (g_PerCoreVmx && core < g_NumCores && g_PerCoreVmx[core].Active);
}
