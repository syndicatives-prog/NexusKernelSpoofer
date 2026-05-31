#pragma once
#include "common.h"

typedef struct _VMX_CONTROLS {
    UINT64 VmcsRevisionId;
    UINT64 VmxonRegionPhys;
    UINT64 VmcsRegionPhys;
    UINT64 EptPml4Phys;
    PVOID  EptPml4Va;
    BOOLEAN HypervisorActive;
    PVOID  HostStackVa;
} VMX_CONTROLS;

extern VMX_CONTROLS g_Vmx;

VOID EptHidePage(UINT64 PhysAddr, BOOLEAN Hide);
VOID EptSetFakePage(UINT64 PhysAddr, PVOID FakePageVa);
PUINT64 EptSplitTo4Kb(UINT64 Pml4Phys, UINT64 GuestPhysAddr);
NTSTATUS InitHypervisor();
VOID CleanupHypervisor();
extern VOID VmxLaunch(UINT64 HostStackPtr, UINT64 GuestRip);
extern BOOLEAN HandleHvciExecuteViolation(UINT64 GuestPhysAddr, UINT64 GuestRip);

VOID SetMTF();
VOID ClearMTF();

// INVEPT wrapper definida en hypervisor_asm.asm
extern VOID InvEpt(UINT64 Type, void* Descriptor);
