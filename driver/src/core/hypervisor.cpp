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

// Wrapper temporal que registra el mapeo para luego liberarlo
static PVOID MapPhysicalTemp(UINT64 PhysAddr, ULONG Size, PVOID* MappingToUnmap) {
    PHYSICAL_ADDRESS pa; pa.QuadPart = PhysAddr;
    PVOID va = MmMapIoSpace(pa, Size, MmNonCached);
    *MappingToUnmap = va;
    return va;
}

static UINT64 AdjustCr4(UINT64 cr4) { return cr4 | (1ULL << 13); }

PUINT64 EptSplitTo4Kb(UINT64 Pml4Phys, UINT64 GuestPhysAddr) {
    GuestPhysAddr &= ~0xFFFULL;
    PVOID pml4Mapping = NULL;
    UINT64* pml4 = (UINT64*)MapPhysicalTemp(Pml4Phys, 4096, &pml4Mapping);
    if (!pml4) return NULL;
    int pml4Idx = (GuestPhysAddr >> 39) & 0x1FF;
    if (!(pml4[pml4Idx] & 1)) { MmUnmapIoSpace(pml4Mapping, 4096); return NULL; }

    UINT64 pdptPhys = pml4[pml4Idx] & ~0xFFFULL;
    PVOID pdptMapping = NULL;
    UINT64* pdpt = (UINT64*)MapPhysicalTemp(pdptPhys, 4096, &pdptMapping);
    if (!pdpt) { MmUnmapIoSpace(pml4Mapping, 4096); return NULL; }
    int pdptIdx = (GuestPhysAddr >> 30) & 0x1FF;
    if (!(pdpt[pdptIdx] & 1)) { MmUnmapIoSpace(pdptMapping, 4096); MmUnmapIoSpace(pml4Mapping, 4096); return NULL; }

    if (pdpt[pdptIdx] & 0x80) {
        UINT64 pdPhys; PVOID pdVa = AllocContiguousPhys(4096, &pdPhys);
        if (!pdVa) { MmUnmapIoSpace(pdptMapping, 4096); MmUnmapIoSpace(pml4Mapping, 4096); return NULL; }
        UINT64* pd = (UINT64*)pdVa;
        UINT64 base = pdpt[pdptIdx] & 0xFFFFC0000000ULL;
        for (int i = 0; i < 512; i++) pd[i] = (base + (i * 0x200000ULL)) | (pdpt[pdptIdx] & 0x7F) | 0x80;
        pdpt[pdptIdx] = pdPhys | 7;
        __invept(1, NULL);
        MmUnmapIoSpace(pdptMapping, 4096); MmUnmapIoSpace(pml4Mapping, 4096);
        return NULL; // El caller volver? a intentar despu?s del split
    }

    UINT64 pdPhys = pdpt[pdptIdx] & ~0xFFFULL;
    PVOID pdMapping = NULL;
    UINT64* pd = (UINT64*)MapPhysicalTemp(pdPhys, 4096, &pdMapping);
    if (!pd) { MmUnmapIoSpace(pdptMapping, 4096); MmUnmapIoSpace(pml4Mapping, 4096); return NULL; }
    int pdIdx = (GuestPhysAddr >> 21) & 0x1FF;
    if (!(pd[pdIdx] & 1)) { MmUnmapIoSpace(pdMapping, 4096); MmUnmapIoSpace(pdptMapping, 4096); MmUnmapIoSpace(pml4Mapping, 4096); return NULL; }

    if (pd[pdIdx] & 0x80) {
        UINT64 ptPhys; PVOID ptVa = AllocContiguousPhys(4096, &ptPhys);
        if (!ptVa) { MmUnmapIoSpace(pdMapping, 4096); MmUnmapIoSpace(pdptMapping, 4096); MmUnmapIoSpace(pml4Mapping, 4096); return NULL; }
        UINT64* pt = (UINT64*)ptVa;
        UINT64 base = pd[pdIdx] & 0xFFFFFFFFFFE00000ULL;
        for (int i = 0; i < 512; i++) pt[i] = (base + (i * 0x1000ULL)) | (pd[pdIdx] & 0x7F) & ~0x80;
        pd[pdIdx] = ptPhys | 7;
        __invept(1, NULL);
        MmUnmapIoSpace(pdMapping, 4096); MmUnmapIoSpace(pdptMapping, 4096); MmUnmapIoSpace(pml4Mapping, 4096);
        return NULL;
    }

    UINT64 ptPhys = pd[pdIdx] & ~0xFFFULL;
    PVOID ptMapping = NULL;
    UINT64* pt = (UINT64*)MapPhysicalTemp(ptPhys, 4096, &ptMapping);
    if (!pt) { MmUnmapIoSpace(pdMapping, 4096); MmUnmapIoSpace(pdptMapping, 4096); MmUnmapIoSpace(pml4Mapping, 4096); return NULL; }
    int ptIdx = (GuestPhysAddr >> 12) & 0x1FF;
    PUINT64 pte = &pt[ptIdx];

    // Liberar todos los mapeos excepto el de la PTE (el caller necesita que siga mapeado)
    MmUnmapIoSpace(pdMapping, 4096);
    MmUnmapIoSpace(pdptMapping, 4096);
    MmUnmapIoSpace(pml4Mapping, 4096);

    // El caller DEBE liberar ptMapping despu?s de usar la PTE
    // Devolvemos el mapeo para que lo libere externamente (guardar en variable global)
    extern PVOID g_LastPtMapping;
    g_LastPtMapping = ptMapping;
    return pte;
}

// Variable para liberar el ?ltimo mapeo de PT despu?s de usar la PTE
PVOID g_LastPtMapping = NULL;

// Wrapper de EptHidePage que libera el mapeo de la PTE
VOID EptHidePage(UINT64 PhysAddr, BOOLEAN Hide) {
    PhysAddr &= ~0xFFFULL;
    PUINT64 pte = NULL;
    int retries = 0;
    while (!pte && retries < 10) {
        pte = EptSplitTo4Kb(g_Vmx.EptPml4Phys, PhysAddr);
        retries++;
    }
    if (!pte) return;
    if (Hide) *pte &= ~1ULL; else *pte |= 1ULL;
    __invept(1, NULL);
    if (g_LastPtMapping) { MmUnmapIoSpace(g_LastPtMapping, 4096); g_LastPtMapping = NULL; }
}

VOID EptSetFakePage(UINT64 PhysAddr, PVOID FakePageVa) {
    for (ULONG i = 0; i < g_PageCount; i++) {
        if (g_HiddenPages[i] == PhysAddr) { g_FakePages[i] = (UCHAR*)FakePageVa; return; }
    }
    if (g_PageCount < 8) {
        g_HiddenPages[g_PageCount] = PhysAddr;
        g_FakePages[g_PageCount] = (UCHAR*)FakePageVa;
        g_PageCount++;
    }
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
    if (g_LastPtMapping) { MmUnmapIoSpace(g_LastPtMapping, 4096); g_LastPtMapping = NULL; }
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
    case 16: case 17:
    {
        UINT64 tsc = __rdtsc() - 500;
        Regs->rax = (UINT32)tsc; Regs->rdx = (UINT32)(tsc >> 32);
        SkipInstruction(GuestRip);
        break;
    }
    case 31:
    {
        UINT32 msrId = (UINT32)Regs->rcx;
        UINT64 val = (msrId == 0x3A || msrId == 0xE7 || msrId == 0xE8) ? 0 : __readmsr(msrId);
        Regs->rax = (UINT32)val; Regs->rdx = (UINT32)(val >> 32);
        SkipInstruction(GuestRip);
        break;
    }
    case 47:
        for (ULONG i = 0; i < g_PageCount; i++)
            if (g_HiddenPages[i]) EptHidePage(g_HiddenPages[i], TRUE);
        ClearMTF();
        break;
    case 48:
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
    if (g_Vmx.HostStackVa) { MmFreeContiguousMemory(g_Vmx.HostStackVa); g_Vmx.HostStackVa = NULL; }
    if (g_Vmx.EptPml4Va) { MmFreeContiguousMemory(g_Vmx.EptPml4Va); g_Vmx.EptPml4Va = NULL; }
    // pdptVa deber?a guardarse en g_Vmx tambi?n; por simplicidad lo omitimos (se libera en DriverUnload)
}
