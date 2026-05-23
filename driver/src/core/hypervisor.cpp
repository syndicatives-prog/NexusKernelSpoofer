#include "hypervisor.h"
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
    GuestPhysAddr &= ~0xFFFULL;
    UINT64* pml4 = (UINT64*)MapPhysical(Pml4Phys, 4096);
    if (!pml4) return NULL;
    int pml4Idx = (GuestPhysAddr >> 39) & 0x1FF;
    if (!(pml4[pml4Idx] & 1)) return NULL;
    UINT64 pdptPhys = pml4[pml4Idx] & ~0xFFFULL;
    UINT64* pdpt = (UINT64*)MapPhysical(pdptPhys, 4096);
    if (!pdpt) return NULL;
    int pdptIdx = (GuestPhysAddr >> 30) & 0x1FF;
    if (!(pdpt[pdptIdx] & 1)) return NULL;
    if (pdpt[pdptIdx] & 0x80) {
        UINT64 pdPhys; PVOID pdVa = AllocContiguousPhys(4096, &pdPhys);
        if (!pdVa) return NULL;
        UINT64* pd = (UINT64*)pdVa;
        UINT64 base = pdpt[pdptIdx] & 0xFFFFC0000000ULL;
        for (int i = 0; i < 512; i++) pd[i] = (base + (i * 0x200000ULL)) | (pdpt[pdptIdx] & 0x7F) | 0x80;
        pdpt[pdptIdx] = pdPhys | 7;
        __invept(1, NULL);
        return NULL;
    }
    UINT64 pdPhys = pdpt[pdptIdx] & ~0xFFFULL;
    UINT64* pd = (UINT64*)MapPhysical(pdPhys, 4096);
    if (!pd) return NULL;
    int pdIdx = (GuestPhysAddr >> 21) & 0x1FF;
    if (!(pd[pdIdx] & 1)) return NULL;
    if (pd[pdIdx] & 0x80) {
        UINT64 ptPhys; PVOID ptVa = AllocContiguousPhys(4096, &ptPhys);
        if (!ptVa) return NULL;
        UINT64* pt = (UINT64*)ptVa;
        UINT64 base = pd[pdIdx] & 0xFFFFFFFFFFE00000ULL;
        for (int i = 0; i < 512; i++) pt[i] = (base + (i * 0x1000ULL)) | (pd[pdIdx] & 0x7F) & ~0x80;
        pd[pdIdx] = ptPhys | 7;
        __invept(1, NULL);
        return NULL;
    }
    UINT64 ptPhys = pd[pdIdx] & ~0xFFFULL;
    UINT64* pt = (UINT64*)MapPhysical(ptPhys, 4096);
    if (!pt) return NULL;
    int ptIdx = (GuestPhysAddr >> 12) & 0x1FF;
    return &pt[ptIdx];
}

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

static void SetMTF() {
    UINT32 procBased;
    __vmx_vmread(0x00004002, &procBased);
    procBased |= 0x00040000;
    __vmx_vmwrite(0x00004002, procBased);
}
static void ClearMTF() {
    UINT32 procBased;
    __vmx_vmread(0x00004002, &procBased);
    procBased &= ~0x00040000;
    __vmx_vmwrite(0x00004002, procBased);
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
    __vmx_vmwrite(0x0000681E, GuestRip + len);
}

static void HandleCpuidExit() {
    UINT64 rax, rbx, rcx, rdx;
    __vmx_vmread(0x0000681C, &rax);
    __vmx_vmread(0x00006820, &rbx);
    __vmx_vmread(0x00006824, &rcx);
    __vmx_vmread(0x00006828, &rdx);
    UINT32 function = (UINT32)rax;
    if (function == 0) { rbx = 0x756e6547; rdx = 0x49656e69; rcx = 0x6c65746e; rax = 0x16; }
    else if (function == 1) { rax = 0x000906E0; rbx = 0x01000800; rcx = 0x7FFAFBBF; rdx = 0xBFEBFBFF; }
    else if (function == 0x80000000) { rax = 0x80000008; rbx = rcx = rdx = 0; }
    else if (function == 0x80000001) { rdx = 0x2C000000; rax = rbx = rcx = 0; }
    else if (function == 0x80000002) { rax = 0x6578654E; rbx = 0x6F6F7073; rcx = 0x72656666; rdx = 0x50432072; }
    else if (function == 0x80000003) { rax = 0x20405520; rbx = 0x30352E35; rcx = 0x007A4847; rdx = 0; }
    else if (function == 0x80000004) { rax = rbx = rcx = rdx = 0; }
    else { rax = rbx = rcx = rdx = 0; }
    __vmx_vmwrite(0x0000681C, rax);
    __vmx_vmwrite(0x00006820, rbx);
    __vmx_vmwrite(0x00006824, rcx);
    __vmx_vmwrite(0x00006828, rdx);
}

BOOLEAN HandleHvciExecuteViolation(UINT64 GuestPhysAddr, UINT64 GuestRip) {
    return FALSE;
}

extern "C" UINT64 VmexitHandler(UINT64 ExitReason, UINT64 GuestRip) {
    UINT64 guestPhysAddr, exitQual;
    switch (ExitReason) {
    case 16: // RDTSC
    case 17: // RDTSCP
    {
        UINT64 tsc = __rdtsc() - 1000;
        __vmx_vmwrite(0x0000681C, (UINT32)tsc);
        __vmx_vmwrite(0x00006820, (UINT32)(tsc >> 32));
        break;
    }
    case 18: // CPUID
        HandleCpuidExit();
        break;
    case 31: // MSR read
    {
        UINT64 msrId; __vmx_vmread(0x0000681C, &msrId);
        if (msrId == 0x480 || msrId == 0x3A || msrId == 0xE7 || msrId == 0xE8) {
            __vmx_vmwrite(0x0000681C, 0); __vmx_vmwrite(0x00006820, 0);
        }
        break;
    }
    case 47: // Monitor Trap Flag
        for (ULONG i = 0; i < g_PageCount; i++) if (g_HiddenPages[i]) EptHidePage(g_HiddenPages[i], TRUE);
        ClearMTF();
        break;
    case 48: // EPT violation
        __vmx_vmread(0x00002400, &guestPhysAddr);
        __vmx_vmread(0x00006400, &exitQual);
        BOOLEAN isRead = (exitQual & 1) == 0;
        if ((exitQual & 0x10) && HandleHvciExecuteViolation(guestPhysAddr, GuestRip)) break;
        for (ULONG i = 0; i < g_PageCount; i++) {
            if (g_HiddenPages[i] && guestPhysAddr >= g_HiddenPages[i] && guestPhysAddr < g_HiddenPages[i] + 0x1000) {
                if (isRead) { EmulateEptRead(g_HiddenPages[i], g_FakePages[i]); SetMTF(); }
                else SkipInstruction(GuestRip);
                break;
            }
        }
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
    for (int i = 0; i < 4; i++) pdpt[i] = (i * 0x40000000ULL) | 0x87;
    UINT64 eptp = g_Vmx.EptPml4Phys | (6 << 3) | 3;
    __vmx_vmwrite(0x0000201A, eptp);
    // Host state
    __vmx_vmwrite(0x00006C14, (UINT64)AllocContiguousPhys(8192, NULL) + 8192);
    __vmx_vmwrite(0x00006C00, __readcr0());
    __vmx_vmwrite(0x00006C02, __readcr4());
    __vmx_vmwrite(0x00006800, __readcr0());
    __vmx_vmwrite(0x00006802, __readcr4());
    // HOST_RIP
    extern ULONG_PTR VmxExitEntry;
    __vmx_vmwrite(0x00006C16, (ULONG_PTR)&VmxExitEntry);
    // HOST_CR3
    __vmx_vmwrite(0x00006C06, __readcr3());
    // HOST_GDTR_BASE, HOST_IDTR_BASE
    GDTR gdtr; IDTR idtr;
    _sgdt(&gdtr); __sidt(&idtr);
    __vmx_vmwrite(0x00006C0A, gdtr.Base);
    __vmx_vmwrite(0x00006C0C, idtr.Base);
    // HOST_CS, DS, SS
    __vmx_vmwrite(0x00000C02, __readcs());
    __vmx_vmwrite(0x00000C04, 0); // DS
    __vmx_vmwrite(0x00000C06, 0); // SS

    UINT32 primaryCtrl; __vmx_vmread(0x00004002, &primaryCtrl);
    primaryCtrl |= 0x80000000; __vmx_vmwrite(0x00004002, primaryCtrl);
    UINT32 secondaryCtrl; __vmx_vmread(0x0000401E, &secondaryCtrl);
    secondaryCtrl |= 0x00000002; __vmx_vmwrite(0x0000401E, secondaryCtrl);
    g_Vmx.HypervisorActive = TRUE;
    return STATUS_SUCCESS;
}

VOID CleanupHypervisor() { if (g_Vmx.HypervisorActive) { __vmx_off(); g_Vmx.HypervisorActive = FALSE; } }
