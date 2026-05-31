#include "vmcs_init.h"
#include "hypervisor.h"
#include <intrin.h>

static UINT32 SegAccessRights(UINT16 Selector, UINT64 GdtBase) {
    if (Selector == 0) return 0x10000; // unusable
    PSEGMENT_DESCRIPTOR d = (PSEGMENT_DESCRIPTOR)(GdtBase + (Selector & ~7u));
    UINT32 ar = (UINT32)d->AccessByte | ((UINT32)(d->FlagsLimitHigh & 0xF0u) << 8);
    if (!(ar & 0x80)) ar |= 0x10000; // if present bit not set, mark unusable
    return ar;
}

static UINT64 SegBase(UINT16 Selector, UINT64 GdtBase) {
    if (Selector == 0) return 0;
    PSEGMENT_DESCRIPTOR d = (PSEGMENT_DESCRIPTOR)(GdtBase + (Selector & ~7u));
    UINT64 base = (UINT64)d->BaseLow | ((UINT64)d->BaseMid << 16) | ((UINT64)d->BaseHigh << 24);
    if (!(d->AccessByte & 0x10u)) {
        PSYSTEM_SEGMENT_DESCRIPTOR sd = (PSYSTEM_SEGMENT_DESCRIPTOR)d;
        base |= ((UINT64)sd->BaseUpper << 32);
    }
    return base;
}

static UINT32 SegLimit(UINT16 Selector, UINT64 GdtBase) {
    if (Selector == 0) return 0;
    PSEGMENT_DESCRIPTOR d = (PSEGMENT_DESCRIPTOR)(GdtBase + (Selector & ~7u));
    UINT32 limit = (UINT32)d->LimitLow | ((UINT32)(d->FlagsLimitHigh & 0x0Fu) << 16);
    if (d->FlagsLimitHigh & 0x80u) limit = (limit << 12) | 0xFFFu;
    return limit;
}

static UINT32 AdjustControl(UINT32 Desired, UINT32 CapMsr) {
    UINT64 cap = __readmsr(CapMsr);
    UINT32 must1 = (UINT32)(cap);
    UINT32 may1  = (UINT32)(cap >> 32);
    return (Desired | must1) & may1;
}

NTSTATUS InitVmcsGuestState(PVOID HostStackTop, ULONG_PTR HostRip) {
    GDTR gdtr; IDTR idtr;
    _sgdt(&gdtr); __sidt(&idtr);
    UINT64 gdtBase = gdtr.Base;

    UINT16 cs   = __readcs();
    UINT16 ss   = __readss();
    UINT16 ds   = __readds();
    UINT16 es   = __reades();
    UINT16 fs   = __readfs();
    UINT16 gs   = __readgs();
    UINT16 tr   = __readtr();
    UINT16 ldtr = __readldtr();

    UINT64 cr0Fixed0 = __readmsr(0x486), cr0Fixed1 = __readmsr(0x487);
    UINT64 cr4Fixed0 = __readmsr(0x488), cr4Fixed1 = __readmsr(0x489);
    UINT64 cr0 = (__readcr0() | cr0Fixed0) & cr0Fixed1;
    UINT64 cr4 = (__readcr4() | cr4Fixed0) & cr4Fixed1;
    UINT64 cr3 = __readcr3();

    // Guest selectors
    __vmx_vmwrite(VMCS_GUEST_CS_SELECTOR,   cs);
    __vmx_vmwrite(VMCS_GUEST_SS_SELECTOR,   ss);
    __vmx_vmwrite(VMCS_GUEST_DS_SELECTOR,   ds);
    __vmx_vmwrite(VMCS_GUEST_ES_SELECTOR,   es);
    __vmx_vmwrite(VMCS_GUEST_FS_SELECTOR,   fs);
    __vmx_vmwrite(VMCS_GUEST_GS_SELECTOR,   gs);
    __vmx_vmwrite(VMCS_GUEST_TR_SELECTOR,   tr);
    __vmx_vmwrite(VMCS_GUEST_LDTR_SELECTOR, ldtr);

    // Guest bases
    __vmx_vmwrite(VMCS_GUEST_CS_BASE,   SegBase(cs,   gdtBase));
    __vmx_vmwrite(VMCS_GUEST_SS_BASE,   SegBase(ss,   gdtBase));
    __vmx_vmwrite(VMCS_GUEST_DS_BASE,   SegBase(ds,   gdtBase));
    __vmx_vmwrite(VMCS_GUEST_ES_BASE,   SegBase(es,   gdtBase));
    __vmx_vmwrite(VMCS_GUEST_FS_BASE,   __readmsr(MSR_FS_BASE));
    __vmx_vmwrite(VMCS_GUEST_GS_BASE,   __readmsr(MSR_GS_BASE));
    __vmx_vmwrite(VMCS_GUEST_TR_BASE,   SegBase(tr,   gdtBase));
    __vmx_vmwrite(VMCS_GUEST_LDTR_BASE, SegBase(ldtr, gdtBase));
    __vmx_vmwrite(VMCS_GUEST_GDTR_BASE, gdtBase);
    __vmx_vmwrite(VMCS_GUEST_IDTR_BASE, idtr.Base);

    // Guest limits
    __vmx_vmwrite(VMCS_GUEST_CS_LIMIT,   SegLimit(cs,   gdtBase));
    __vmx_vmwrite(VMCS_GUEST_SS_LIMIT,   SegLimit(ss,   gdtBase));
    __vmx_vmwrite(VMCS_GUEST_DS_LIMIT,   SegLimit(ds,   gdtBase));
    __vmx_vmwrite(VMCS_GUEST_ES_LIMIT,   SegLimit(es,   gdtBase));
    __vmx_vmwrite(VMCS_GUEST_FS_LIMIT,   SegLimit(fs,   gdtBase));
    __vmx_vmwrite(VMCS_GUEST_GS_LIMIT,   SegLimit(gs,   gdtBase));
    __vmx_vmwrite(VMCS_GUEST_TR_LIMIT,   SegLimit(tr,   gdtBase));
    __vmx_vmwrite(VMCS_GUEST_LDTR_LIMIT, SegLimit(ldtr, gdtBase));
    __vmx_vmwrite(VMCS_GUEST_GDTR_LIMIT, gdtr.Limit);
    __vmx_vmwrite(VMCS_GUEST_IDTR_LIMIT, idtr.Limit);

    // Guest access rights
    __vmx_vmwrite(VMCS_GUEST_CS_AR,   SegAccessRights(cs,   gdtBase));
    __vmx_vmwrite(VMCS_GUEST_SS_AR,   SegAccessRights(ss,   gdtBase));
    __vmx_vmwrite(VMCS_GUEST_DS_AR,   SegAccessRights(ds,   gdtBase));
    __vmx_vmwrite(VMCS_GUEST_ES_AR,   SegAccessRights(es,   gdtBase));
    __vmx_vmwrite(VMCS_GUEST_FS_AR,   SegAccessRights(fs,   gdtBase));
    __vmx_vmwrite(VMCS_GUEST_GS_AR,   SegAccessRights(gs,   gdtBase));
    __vmx_vmwrite(VMCS_GUEST_TR_AR,   SegAccessRights(tr,   gdtBase));
    __vmx_vmwrite(VMCS_GUEST_LDTR_AR, SegAccessRights(ldtr, gdtBase));

    // Guest control registers
    __vmx_vmwrite(VMCS_GUEST_CR0,  cr0);
    __vmx_vmwrite(VMCS_GUEST_CR3,  cr3);
    __vmx_vmwrite(VMCS_GUEST_CR4,  cr4);
    __vmx_vmwrite(VMCS_GUEST_DR7,  0x400);

    __vmx_vmwrite(VMCS_CR0_GUEST_HOST_MASK, cr0Fixed0);
    __vmx_vmwrite(VMCS_CR4_GUEST_HOST_MASK, cr4Fixed0);
    __vmx_vmwrite(VMCS_CR0_READ_SHADOW,     __readcr0());
    __vmx_vmwrite(VMCS_CR4_READ_SHADOW,     __readcr4());

    // Placeholders for RIP/RSP/RFLAGS ? ASM overrides
    __vmx_vmwrite(VMCS_GUEST_RSP,    0);
    __vmx_vmwrite(VMCS_GUEST_RIP,    0);
    __vmx_vmwrite(VMCS_GUEST_RFLAGS, 0x202);

    // Guest MSRs
    __vmx_vmwrite(VMCS_GUEST_IA32_DEBUGCTL, 0);
    __vmx_vmwrite(VMCS_GUEST_IA32_EFER,     __readmsr(MSR_IA32_EFER));
    __vmx_vmwrite(VMCS_GUEST_SYSENTER_CS,   __readmsr(MSR_SYSENTER_CS));
    __vmx_vmwrite(VMCS_GUEST_SYSENTER_ESP,  __readmsr(MSR_SYSENTER_ESP));
    __vmx_vmwrite(VMCS_GUEST_SYSENTER_EIP,  __readmsr(MSR_SYSENTER_EIP));

    // Guest non-register state
    __vmx_vmwrite(VMCS_GUEST_VMCS_LINK_PTR, ~0ULL);
    __vmx_vmwrite(VMCS_GUEST_INTERRUPTIBILITY, 0);
    __vmx_vmwrite(VMCS_GUEST_ACTIVITY_STATE,   0);
    __vmx_vmwrite(VMCS_GUEST_PENDING_DBG_EXC,  0);

    // Host selectors (strip RPL/TI)
    __vmx_vmwrite(VMCS_HOST_CS_SELECTOR, cs   & ~7u);
    __vmx_vmwrite(VMCS_HOST_SS_SELECTOR, ss   & ~7u);
    __vmx_vmwrite(VMCS_HOST_DS_SELECTOR, ds   & ~7u);
    __vmx_vmwrite(VMCS_HOST_ES_SELECTOR, es   & ~7u);
    __vmx_vmwrite(VMCS_HOST_FS_SELECTOR, fs   & ~7u);
    __vmx_vmwrite(VMCS_HOST_GS_SELECTOR, gs   & ~7u);
    __vmx_vmwrite(VMCS_HOST_TR_SELECTOR, tr   & ~7u);

    // Host control registers
    __vmx_vmwrite(VMCS_HOST_CR0, cr0);
    __vmx_vmwrite(VMCS_HOST_CR3, cr3);
    __vmx_vmwrite(VMCS_HOST_CR4, cr4);

    // Host segment bases
    __vmx_vmwrite(VMCS_HOST_FS_BASE,   __readmsr(MSR_FS_BASE));
    __vmx_vmwrite(VMCS_HOST_GS_BASE,   __readmsr(MSR_GS_BASE));
    __vmx_vmwrite(VMCS_HOST_TR_BASE,   SegBase(tr, gdtBase));
    __vmx_vmwrite(VMCS_HOST_GDTR_BASE, gdtBase);
    __vmx_vmwrite(VMCS_HOST_IDTR_BASE, idtr.Base);

    // Host MSRs
    __vmx_vmwrite(VMCS_HOST_SYSENTER_CS,  __readmsr(MSR_SYSENTER_CS));
    __vmx_vmwrite(VMCS_HOST_SYSENTER_ESP, __readmsr(MSR_SYSENTER_ESP));
    __vmx_vmwrite(VMCS_HOST_SYSENTER_EIP, __readmsr(MSR_SYSENTER_EIP));
    __vmx_vmwrite(VMCS_HOST_IA32_EFER,    __readmsr(MSR_IA32_EFER));

    // Host RSP / RIP
    __vmx_vmwrite(VMCS_HOST_RSP, (UINT64)HostStackTop);
    __vmx_vmwrite(VMCS_HOST_RIP, HostRip);

    // Control fields
    BOOLEAN useTrueCtls = (__readmsr(MSR_VMX_BASIC) >> 55) & 1;

    __vmx_vmwrite(VMCS_PIN_BASED_CTRL,
        AdjustControl(0, useTrueCtls ? MSR_VMX_TRUE_PINBASED : MSR_VMX_PINBASED));

    __vmx_vmwrite(VMCS_PROC_BASED_CTRL,
        AdjustControl(
            (1u << 31) |  // activate secondary controls
            (1u << 12),   // RDTSC exiting
            useTrueCtls ? MSR_VMX_TRUE_PROCBASED : MSR_VMX_PROCBASED));

    __vmx_vmwrite(VMCS_PROC_BASED_CTRL2,
        AdjustControl(
            (1u << 1)  |  // enable EPT
            (1u << 3)  |  // enable VPID
            (1u << 11),   // enable RDTSCP
            MSR_VMX_PROCBASED2));

    __vmx_vmwrite(VMCS_VMEXIT_CTRL,
        AdjustControl(
            (1u << 9)  |  // HOST_ADDR_SPACE_SIZE = 64bit
            (1u << 20) |  // load IA32_EFER on exit
            (1u << 21),   // save IA32_EFER on exit
            useTrueCtls ? MSR_VMX_TRUE_EXIT : MSR_VMX_EXIT_CTLS));

    __vmx_vmwrite(VMCS_VMENTRY_CTRL,
        AdjustControl(
            (1u << 9)  |  // IA32E_MODE_GUEST
            (1u << 15),   // load IA32_EFER on entry
            useTrueCtls ? MSR_VMX_TRUE_ENTRY : MSR_VMX_ENTRY_CTLS));

    __vmx_vmwrite(VMCS_EXCEPTION_BITMAP, 0);
    
    // Allocate and initialize MSR bitmap (required if "use MSR bitmap" bit is set)
    PHYSICAL_ADDRESS highest; highest.QuadPart = -1;
    PVOID msrBitmapVa = MmAllocateContiguousMemory(4096, highest);
    if (msrBitmapVa) {
        RtlZeroMemory(msrBitmapVa, 4096); // All zero = no MSR interception
        UINT64 msrBitmapPhys = MmGetPhysicalAddress(msrBitmapVa).QuadPart;
        __vmx_vmwrite(VMCS_MSR_BITMAP, msrBitmapPhys);
        // Save for cleanup
        extern VMX_CONTROLS g_Vmx;
        g_Vmx.MsrBitmapVa = msrBitmapVa;
    }
    
    __vmx_vmwrite(0x0000, 1); // VPID = 1

    return STATUS_SUCCESS;
}
