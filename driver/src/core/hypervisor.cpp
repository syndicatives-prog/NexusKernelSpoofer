#include "hypervisor.h"
#include "vmcs_init.h"
#include "../spoofers/smbios_spoofer.h"
#include <Zydis/Zydis.h>
#include <ntddk.h>

VMX_CONTROLS g_Vmx = {0};
static UCHAR* g_FakePages[8] = {0};
static UINT64 g_HiddenPages[8] = {0};
static ULONG g_PageCount = 0;

UINT64 g_SmbiosPhysAddr = 0;
UCHAR g_FakeSmbiosPage[4096] = {0};
extern UINT64 g_GpuPhys, g_MacPhys, g_TpmPhysBase;
extern UCHAR g_FakeGpuConfigPage[4096], g_FakeMacPage[4096], g_FakeTpmPage[4096];

static PVOID AllocContiguousPhys(ULONG Size, UINT64* PhysAddr) {
    PHYSICAL_ADDRESS highest; highest.QuadPart = -1;
    PVOID buf = MmAllocateContiguousMemory(Size, highest);
    if (!buf) return NULL;
    if (PhysAddr) *PhysAddr = MmGetPhysicalAddress(buf).QuadPart;
    RtlZeroMemory(buf, Size);
    return buf;
}

static PVOID MapPhysicalTemp(UINT64 PhysAddr, ULONG Size, PVOID* MappingToUnmap) {
    PHYSICAL_ADDRESS pa; pa.QuadPart = PhysAddr;
    PVOID va = MmMapIoSpace(pa, Size, MmNonCached);
    *MappingToUnmap = va;
    return va;
}

static UINT64 AdjustCr4(UINT64 cr4) { return cr4 | (1ULL << 13); }

// SkipInstruction: read instruction length from VMCS and advance RIP
static void SkipInstruction(UINT64 GuestRip) {
    UINT64 instrLen = 0;
    __vmx_vmread(0x440C, &instrLen); // VM_EXIT_INSTRUCTION_LEN
    __vmx_vmwrite(0x681E, GuestRip + instrLen); // VMCS_GUEST_RIP
}

// DecodeInstructionLength: fallback if needed outside vmexit (normally use VMCS)
static ULONG DecodeInstructionLength(UINT64 Rip) {
    // Without real Zydis integration, prefer VMCS field when available
    UNREFERENCED_PARAMETER(Rip);
    return 0;
}

// SetMTF / ClearMTF: bit 27 of Proc-Based VM-Exec Controls
VOID SetMTF() {
    UINT64 procCtrl = 0;
    __vmx_vmread(0x4002, &procCtrl);
    __vmx_vmwrite(0x4002, procCtrl | (1ULL << 27));
}

VOID ClearMTF() {
    UINT64 procCtrl = 0;
    __vmx_vmread(0x4002, &procCtrl);
    __vmx_vmwrite(0x4002, procCtrl & ~(1ULL << 27));
}

// EptHidePage: remove R/W/X permissions from PTE for this physical page
VOID EptHidePage(UINT64 PhysAddr, BOOLEAN Hide) {
    PVOID dummy = NULL;
    PUINT64 pte = EptSplitTo4Kb(g_Vmx.EptPml4Phys, PhysAddr, &dummy);
    if (!pte) return;
    if (Hide)
        *pte &= ~7ULL;  // Remove R+W+X
    else
        *pte |= 7ULL;   // Restore R+W+X
    UINT64 desc[2] = { g_Vmx.EptPml4Phys, 0 };
    InvEpt(1, desc);
    if (dummy) MmUnmapIoSpace(dummy, 4096);
}

// EptSetFakePage: redirect PTE entry to a different physical page (fake)
VOID EptSetFakePage(UINT64 PhysAddr, PVOID FakePageVa) {
    PVOID dummy = NULL;
    PUINT64 pte = EptSplitTo4Kb(g_Vmx.EptPml4Phys, PhysAddr, &dummy);
    if (!pte) return;
    UINT64 fakePhys = MmGetPhysicalAddress(FakePageVa).QuadPart & ~0xFFFULL;
    *pte = fakePhys | 7ULL; // R+W+X present
    UINT64 desc[2] = { g_Vmx.EptPml4Phys, 0 };
    InvEpt(1, desc);
    if (dummy) MmUnmapIoSpace(dummy, 4096);
}

// EmulateEptRead: on EPT read violation, copy fake page to guest
static void EmulateEptRead(UINT64 PhysAddr, PVOID FakePage) {
    PHYSICAL_ADDRESS pa;
    pa.QuadPart = PhysAddr;
    PVOID mapped = MmMapIoSpace(pa, 4096, MmNonCached);
    if (mapped) {
        RtlCopyMemory(mapped, FakePage, 4096);
        MmUnmapIoSpace(mapped, 4096);
    }
}

// EptSplitTo4Kb: traverse or construct EPT hierarchy down to 4 KB PTE level
PUINT64 EptSplitTo4Kb(UINT64 Pml4Phys, UINT64 GuestPhysAddr, PVOID* OutPtMapping) {
    if (OutPtMapping) *OutPtMapping = NULL;

    ULONG pml4Idx = (GuestPhysAddr >> 39) & 0x1FF;
    ULONG pdptIdx = (GuestPhysAddr >> 30) & 0x1FF;
    ULONG pdIdx   = (GuestPhysAddr >> 21) & 0x1FF;
    ULONG ptIdx   = (GuestPhysAddr >> 12) & 0x1FF;

    PHYSICAL_ADDRESS pa;
    pa.QuadPart = Pml4Phys;
    PUINT64 pml4 = (PUINT64)MmMapIoSpace(pa, 4096, MmNonCached);
    if (!pml4) return NULL;

    if (!(pml4[pml4Idx] & 1)) {
        PHYSICAL_ADDRESS high;
        high.QuadPart = -1;
        PVOID pdptVa = MmAllocateContiguousMemory(4096, high);
        if (!pdptVa) { MmUnmapIoSpace(pml4, 4096); return NULL; }
        RtlZeroMemory(pdptVa, 4096);
        UINT64 pdptPhys = MmGetPhysicalAddress(pdptVa).QuadPart;
        pml4[pml4Idx] = pdptPhys | 7;
    }
    UINT64 pdptPhys = pml4[pml4Idx] & ~0xFFFULL;
    MmUnmapIoSpace(pml4, 4096);

    pa.QuadPart = pdptPhys;
    PUINT64 pdpt = (PUINT64)MmMapIoSpace(pa, 4096, MmNonCached);
    if (!pdpt) return NULL;

    if (pdpt[pdptIdx] & (1ULL << 7)) {
        UINT64 base1gb = pdpt[pdptIdx] & ~((1ULL << 30) - 1);
        PHYSICAL_ADDRESS high;
        high.QuadPart = -1;
        PVOID pdVa = MmAllocateContiguousMemory(4096, high);
        if (!pdVa) { MmUnmapIoSpace(pdpt, 4096); return NULL; }
        RtlZeroMemory(pdVa, 4096);
        PUINT64 pd2 = (PUINT64)pdVa;
        for (int i = 0; i < 512; i++)
            pd2[i] = (base1gb + i * 0x200000ULL) | 0x87;
        UINT64 pdPhys2 = MmGetPhysicalAddress(pdVa).QuadPart;
        pdpt[pdptIdx] = pdPhys2 | 7;
    }
    if (!(pdpt[pdptIdx] & 1)) {
        PHYSICAL_ADDRESS high;
        high.QuadPart = -1;
        PVOID pdVa = MmAllocateContiguousMemory(4096, high);
        if (!pdVa) { MmUnmapIoSpace(pdpt, 4096); return NULL; }
        RtlZeroMemory(pdVa, 4096);
        UINT64 pdPhys2 = MmGetPhysicalAddress(pdVa).QuadPart;
        pdpt[pdptIdx] = pdPhys2 | 7;
    }
    UINT64 pdPhys = pdpt[pdptIdx] & ~0xFFFULL;
    MmUnmapIoSpace(pdpt, 4096);

    pa.QuadPart = pdPhys;
    PUINT64 pd = (PUINT64)MmMapIoSpace(pa, 4096, MmNonCached);
    if (!pd) return NULL;

    if (pd[pdIdx] & (1ULL << 7)) {
        UINT64 base2mb = pd[pdIdx] & ~((1ULL << 21) - 1);
        PHYSICAL_ADDRESS high;
        high.QuadPart = -1;
        PVOID ptVa2 = MmAllocateContiguousMemory(4096, high);
        if (!ptVa2) { MmUnmapIoSpace(pd, 4096); return NULL; }
        RtlZeroMemory(ptVa2, 4096);
        PUINT64 pt2 = (PUINT64)ptVa2;
        for (int i = 0; i < 512; i++)
            pt2[i] = (base2mb + i * 0x1000ULL) | 7;
        UINT64 ptPhys2 = MmGetPhysicalAddress(ptVa2).QuadPart;
        pd[pdIdx] = ptPhys2 | 7;
    }
    if (!(pd[pdIdx] & 1)) {
        PHYSICAL_ADDRESS high;
        high.QuadPart = -1;
        PVOID ptVa2 = MmAllocateContiguousMemory(4096, high);
        if (!ptVa2) { MmUnmapIoSpace(pd, 4096); return NULL; }
        RtlZeroMemory(ptVa2, 4096);
        UINT64 ptPhys2 = MmGetPhysicalAddress(ptVa2).QuadPart;
        pd[pdIdx] = ptPhys2 | 7;
    }
    UINT64 ptPhys = pd[pdIdx] & ~0xFFFULL;
    MmUnmapIoSpace(pd, 4096);

    pa.QuadPart = ptPhys;
    PUINT64 pt = (PUINT64)MmMapIoSpace(pa, 4096, MmNonCached);
    if (!pt) return NULL;
    if (OutPtMapping) *OutPtMapping = pt;
    return &pt[ptIdx];
}

extern "C" UINT64 VmexitHandler(UINT64 ExitReason, UINT64 GuestRip, PGUEST_REGS Regs) {
    switch (ExitReason) {
    case 10: // CPUID (exit reason 10 per Intel SDM)
    {
        UINT32 func = (UINT32)Regs->rax;
        if (func == 0) {
            Regs->rbx = 0x756e6547; Regs->rdx = 0x49656e69;
            Regs->rcx = 0x6c65746e; Regs->rax = 0x16;
        } else if (func == 1) {
            Regs->rax = 0x000906E0; Regs->rbx = 0x01000800;
            Regs->rcx = 0x7FFAFBBF; Regs->rdx = 0xBFEBFBFF;
        } else {
            int cpuInfo[4]; __cpuidex(cpuInfo, func, (int)Regs->rcx);
            Regs->rax = cpuInfo[0]; Regs->rbx = cpuInfo[1];
            Regs->rcx = cpuInfo[2]; Regs->rdx = cpuInfo[3];
        }
        SkipInstruction(GuestRip);
        break;
    }
    case 16: // RDTSC
    {
        UINT64 tsc = __rdtsc() - 500;
        Regs->rax = (UINT32)tsc; Regs->rdx = (UINT32)(tsc >> 32);
        SkipInstruction(GuestRip);
        break;
    }
    case 17: // VMCALL
        SkipInstruction(GuestRip);
        break;
    case 31: // RDMSR
    {
        UINT32 msrId = (UINT32)Regs->rcx;
        UINT64 val = (msrId == 0x3A || msrId == 0xE7 || msrId == 0xE8) ? 0 : __readmsr(msrId);
        Regs->rax = (UINT32)val; Regs->rdx = (UINT32)(val >> 32);
        SkipInstruction(GuestRip);
        break;
    }
    case 37: // Monitor Trap Flag
        for (ULONG i = 0; i < g_PageCount; i++)
            if (g_HiddenPages[i]) EptHidePage(g_HiddenPages[i], TRUE);
        ClearMTF();
        break;
    case 48: // EPT violation
    {
        UINT64 gpa, qual;
        __vmx_vmread(0x2400, &gpa);
        __vmx_vmread(0x6400, &qual);
        if ((qual & 0x10) && HandleHvciExecuteViolation(gpa, GuestRip)) break;
        gpa &= ~0xFFFULL;
        for (ULONG i = 0; i < g_PageCount; i++) {
            if (g_HiddenPages[i] == gpa) {
                BOOLEAN isRead = (qual & 1) != 0;
                if (isRead) { EmulateEptRead(gpa, g_FakePages[i]); SetMTF(); }
                else SkipInstruction(GuestRip);
                break;
            }
        }
        break;
    }
    default:
        SkipInstruction(GuestRip);
        break;
    }
    return 0;
}

NTSTATUS InitHypervisor() {
    int cpuInfo[4]; __cpuidex(cpuInfo, 1, 0);
    if (!(cpuInfo[2] & (1 << 5))) return STATUS_HV_FEATURE_UNAVAILABLE;
    g_Vmx.VmcsRevisionId = __readmsr(0x480);
    __writecr4(AdjustCr4(__readcr4()));
    UINT64 vmxonPhys; PVOID vmxonVa = AllocContiguousPhys(4096, &vmxonPhys);
    if (!vmxonVa) return STATUS_INSUFFICIENT_RESOURCES;
    *(UINT64*)vmxonVa = g_Vmx.VmcsRevisionId;
    g_Vmx.VmxonRegionPhys = vmxonPhys;
    if (__vmx_on(&vmxonPhys)) return STATUS_UNSUCCESSFUL;
    UINT64 vmcsPhys; PVOID vmcsVa = AllocContiguousPhys(4096, &vmcsPhys);
    if (!vmcsVa) { __vmx_off(); return STATUS_INSUFFICIENT_RESOURCES; }
    *(UINT64*)vmcsVa = g_Vmx.VmcsRevisionId;
    g_Vmx.VmcsRegionPhys = vmcsPhys;
    if (__vmx_vmclear(&vmcsPhys) || __vmx_vmptrld(&vmcsPhys)) { __vmx_off(); return STATUS_UNSUCCESSFUL; }

    PVOID pml4Va = AllocContiguousPhys(4096, &g_Vmx.EptPml4Phys);
    if (!pml4Va) { __vmx_off(); return STATUS_INSUFFICIENT_RESOURCES; }
    g_Vmx.EptPml4Va = pml4Va;
    RtlZeroMemory(pml4Va, 4096);
    UINT64 pdptPhys; PVOID pdptVa = AllocContiguousPhys(4096, &pdptPhys);
    if (!pdptVa) { __vmx_off(); return STATUS_INSUFFICIENT_RESOURCES; }
    g_Vmx.PdptVa = pdptVa;
    RtlZeroMemory(pdptVa, 4096);
    ((PUINT64)pml4Va)[0] = pdptPhys | 7;
    PUINT64 pdpt = (PUINT64)pdptVa;
    for (int i = 0; i < 512; i++) pdpt[i] = (i * 0x40000000ULL) | 0x87;
    UINT64 eptp = g_Vmx.EptPml4Phys | (6ULL << 3) | 3;
    __vmx_vmwrite(VMCS_EPTP, eptp);

    g_Vmx.HostStackVa = AllocContiguousPhys(0x6000, NULL);
    if (!g_Vmx.HostStackVa) { __vmx_off(); return STATUS_INSUFFICIENT_RESOURCES; }
    PVOID hostStackTop = (PUCHAR)g_Vmx.HostStackVa + 0x6000;

    extern ULONG_PTR VmxExitEntry;
    NTSTATUS st = InitVmcsGuestState(hostStackTop, (ULONG_PTR)&VmxExitEntry);
    if (!NT_SUCCESS(st)) { __vmx_off(); return st; }

    g_Vmx.HypervisorActive = TRUE;
    return STATUS_SUCCESS;
}

VOID CleanupHypervisor() {
    if (g_Vmx.HypervisorActive) { __vmx_off(); g_Vmx.HypervisorActive = FALSE; }
    if (g_Vmx.MsrBitmapVa) { MmFreeContiguousMemory(g_Vmx.MsrBitmapVa); g_Vmx.MsrBitmapVa = NULL; }
    if (g_Vmx.HostStackVa) { MmFreeContiguousMemory(g_Vmx.HostStackVa); g_Vmx.HostStackVa = NULL; }
    if (g_Vmx.EptPml4Va) { MmFreeContiguousMemory(g_Vmx.EptPml4Va); g_Vmx.EptPml4Va = NULL; }
    if (g_Vmx.PdptVa)    { MmFreeContiguousMemory(g_Vmx.PdptVa);    g_Vmx.PdptVa    = NULL; }
}
