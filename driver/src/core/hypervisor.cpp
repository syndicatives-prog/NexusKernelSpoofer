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

static PVOID MapPhysical(UINT64 PhysAddr, ULONG Size) {
    PHYSICAL_ADDRESS pa; pa.QuadPart = PhysAddr;
    return MmMapIoSpace(pa, Size, MmNonCached);
}

static UINT64 AdjustCr4(UINT64 cr4) { return cr4 | (1ULL << 13); }

PUINT64 EptSplitTo4Kb(UINT64 Pml4Phys, UINT64 GuestPhysAddr) {
    // ... (sin cambios, pero aseg?rate de que use las mismas funciones de mapeo)
}

VOID EptHidePage(UINT64 PhysAddr, BOOLEAN Hide) {
    // ... (sin cambios)
}

VOID EptSetFakePage(UINT64 PhysAddr, PVOID FakePageVa) {
    // ... (sin cambios)
}

static ULONG DecodeInstructionLength(UINT64 Rip) {
    ZydisDecoder decoder;
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_ADDRESS_WIDTH_64);
    ZydisDecodedInstruction instruction;
    if (ZYAN_SUCCESS(ZydisDecoderDecodeBuffer(&decoder, (PVOID)Rip, 15, &instruction))) return instruction.length;
    return 3;
}

VOID SetMTF() {
    UINT32 procBased;
    __vmx_vmread(VMCS_PROC_BASED_CTRL, &procBased);
    procBased |= 0x00040000;
    __vmx_vmwrite(VMCS_PROC_BASED_CTRL, procBased);
}

VOID ClearMTF() {
    UINT32 procBased;
    __vmx_vmread(VMCS_PROC_BASED_CTRL, &procBased);
    procBased &= ~0x00040000;
    __vmx_vmwrite(VMCS_PROC_BASED_CTRL, procBased);
}

static void EmulateEptRead(UINT64 PhysAddr, PVOID FakePage) {
    PUINT64 pte = NULL;
    int retries = 0;
    while (!pte && retries < 10) {
        pte = EptSplitTo4Kb(g_Vmx.EptPml4Phys, PhysAddr);
        retries++;
    }
    if (!pte) return;
    UINT64 fakePhys = MmGetPhysicalAddress(FakePage).QuadPart;
    *pte = (fakePhys & ~0xFFFULL) | 7;
    __invept(1, NULL);
}
static void SkipInstruction(UINT64 GuestRip) {
    ULONG len = DecodeInstructionLength(GuestRip);
    __vmx_vmwrite(VMCS_GUEST_RIP, GuestRip + len);
}

extern "C" UINT64 VmexitHandler(UINT64 ExitReason, UINT64 GuestRip, PGUEST_REGS Regs) {
    switch (ExitReason) {
    case 10: // CPUID
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
    case 17: // RDTSCP
    {
        UINT64 tsc = __rdtsc() - 500;
        Regs->rax = (UINT32)tsc;
        Regs->rdx = (UINT32)(tsc >> 32);
        SkipInstruction(GuestRip);
        break;
    }
    case 31: // RDMSR
    {
        UINT32 msrId = (UINT32)Regs->rcx;
        UINT64 val = 0;
        if (msrId == 0x3A || msrId == 0xE7 || msrId == 0xE8) {
            val = 0;
        } else {
            val = __readmsr(msrId);
        }
        Regs->rax = (UINT32)val;
        Regs->rdx = (UINT32)(val >> 32);
        SkipInstruction(GuestRip);
        break;
    }
    case 47: // MTF
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
                else        { SkipInstruction(GuestRip); }
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

    // EPT: cubrir 512 GB
    PVOID pml4Va = AllocContiguousPhys(4096, &g_Vmx.EptPml4Phys);
    if (!pml4Va) { __vmx_off(); return STATUS_INSUFFICIENT_RESOURCES; }
    g_Vmx.EptPml4Va = pml4Va;
    RtlZeroMemory(pml4Va, 4096);
    UINT64 pdptPhys; PVOID pdptVa = AllocContiguousPhys(4096, &pdptPhys);
    if (!pdptVa) { __vmx_off(); return STATUS_INSUFFICIENT_RESOURCES; }
    RtlZeroMemory(pdptVa, 4096);
    ((PUINT64)pml4Va)[0] = pdptPhys | 7;
    PUINT64 pdpt = (PUINT64)pdptVa;
    for (int i = 0; i < 512; i++) pdpt[i] = (i * 0x40000000ULL) | 0x87;
    UINT64 eptp = g_Vmx.EptPml4Phys | (6ULL << 3) | 3;
    __vmx_vmwrite(VMCS_EPTP, eptp);

    // Host stack
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
    if (g_Vmx.HypervisorActive) {
        __vmx_off();
        g_Vmx.HypervisorActive = FALSE;
    }
    if (g_Vmx.HostStackVa) { MmFreeContiguousMemory(g_Vmx.HostStackVa); g_Vmx.HostStackVa = NULL; }
    // liberar EptPml4Va, pdptVa, etc.
}
