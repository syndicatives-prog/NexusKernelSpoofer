#pragma once
#include "common.h"

// Per-core EPT structure (one EPT per core)
typedef struct _PER_CORE_EPT {
    UINT64 EptPml4Phys;
    PVOID  EptPml4Va;
    PVOID  PdptVa;
} PER_CORE_EPT;

typedef struct _VMX_CONTROLS {
    UINT64 VmcsRevisionId;
    UINT64 VmxonRegionPhys;
    UINT64 VmcsRegionPhys;
    UINT64 EptPml4Phys;          // Mantenido por compatibilidad (apunta a shared EPT del BSP)
    PVOID  EptPml4Va;            // Mantenido por compatibilidad
    PVOID  PdptVa;               // Mantenido por compatibilidad
    PVOID  MsrBitmapVa;
    BOOLEAN HypervisorActive;
    PVOID  HostStackVa;
    ULONG  NumCores;
    PER_CORE_EPT* PerCoreEpt;    // Array de EPT por core
} VMX_CONTROLS;

extern VMX_CONTROLS g_Vmx;

// Obtener EPT del core actual
__forceinline UINT64 GetCurrentCoreEptPml4Phys() {
    if (g_Vmx.PerCoreEpt) {
        ULONG coreIdx = KeGetCurrentProcessorNumber();
        if (coreIdx < g_Vmx.NumCores) {
            return g_Vmx.PerCoreEpt[coreIdx].EptPml4Phys;
        }
    }
    return g_Vmx.EptPml4Phys;  // Fallback a shared
}

VOID EptHidePage(UINT64 PhysAddr, BOOLEAN Hide);
VOID EptSetFakePage(UINT64 PhysAddr, PVOID FakePageVa);
PUINT64 EptSplitTo4Kb(UINT64 Pml4Phys, UINT64 GuestPhysAddr, PVOID* OutPtMapping);
NTSTATUS InitHypervisor();
VOID CleanupHypervisor();
extern VOID VmxLaunch(UINT64 HostStackPtr, UINT64 GuestRip);
extern BOOLEAN HandleHvciExecuteViolation(UINT64 GuestPhysAddr, UINT64 GuestRip);

VOID SetMTF();
VOID ClearMTF();

extern VOID InvEpt(UINT64 Type, void* Descriptor);

// Invalidate all EPT contexts (Type 2 INVEPT - single-context invalidate for current EPT)
// This MUST be called from within a VM (VMCS loaded)
__forceinline VOID InvEptCurrentContext() {
    UINT64 desc[2] = {GetCurrentCoreEptPml4Phys(), 0};
    InvEpt(1, desc);  // Type 1: Single-context invalidate
}
