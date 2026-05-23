#include "svm.h"
#include "hypervisor.h"

static BOOLEAN g_AmdHypervisorActive = FALSE;
static UINT64 g_HostVmcbPhys = 0;
static UINT64 g_GuestVmcbPhys = 0;
static PVOID g_GuestVmcbVa = NULL;

// VMCB offsets (simplificados)
#define VMCB_CR4         0x0148
#define VMCB_EFER        0x00C0
#define VMCB_RIP         0x0078
#define VMCB_RSP         0x0080
#define VMCB_RAX         0x0088
#define VMCB_EXIT_CODE   0x0000
#define VMCB_NPT_CR3     0x0150
#define VMCB_EXIT_INFO1  0x0048
#define VMCB_EXIT_INFO2  0x0050

extern UINT64 g_SmbiosPhysAddr, g_GpuPhys, g_MacPhys, g_TpmPhysBase;
extern UCHAR g_FakeSmbiosPage[4096], g_FakeGpuConfigPage[4096], g_FakeMacPage[4096], g_FakeTpmPage[4096];
extern UCHAR* g_FakePages[8];
extern UINT64 g_HiddenPages[8];
extern ULONG g_PageCount;

BOOLEAN IsAmdVSupported() {
    int cpuInfo[4];
    __cpuidex(cpuInfo, 0x80000001, 0);
    return (cpuInfo[2] & (1 << 2)) != 0; // SVM bit
}

static PVOID AllocContiguousPhysAmd(ULONG Size, UINT64* PhysAddr) {
    PHYSICAL_ADDRESS highest; highest.QuadPart = -1;
    PVOID buf = MmAllocateContiguousMemory(Size, highest);
    if (!buf) return NULL;
    if (PhysAddr) *PhysAddr = MmGetPhysicalAddress(buf).QuadPart;
    RtlZeroMemory(buf, Size);
    return buf;
}

// Manejador de #VMEXIT para AMD
static VOID AmdVmexitHandler() {
    UINT64 exitCode = *(UINT64*)((PUCHAR)g_GuestVmcbVa + VMCB_EXIT_CODE);
    UINT64 guestRip = *(UINT64*)((PUCHAR)g_GuestVmcbVa + VMCB_RIP);
    UINT64 exitInfo1 = *(UINT64*)((PUCHAR)g_GuestVmcbVa + VMCB_EXIT_INFO1);

    switch (exitCode) {
    case 0x0048: // CPUID
    {
        UINT64 rax = *(UINT64*)((PUCHAR)g_GuestVmcbVa + VMCB_RAX);
        UINT64 rcx = *(UINT64*)((PUCHAR)g_GuestVmcbVa + 0x0090);
        UINT64 rbx, rdx;
        // Llamar a la misma l?gica que en Intel (copiamos el c?digo)
        if (rax == 0) {
            rbx = 0x756e6547; rdx = 0x49656e69; rcx = 0x6c65746e; rax = 0x16;
        } else if (rax == 1) {
            rax = 0x000906E0; rbx = 0x01000800; rcx = 0x7FFAFBBF; rdx = 0xBFEBFBFF;
        } else {
            rax = rbx = rcx = rdx = 0;
        }
        *(UINT64*)((PUCHAR)g_GuestVmcbVa + VMCB_RAX) = rax;
        *(UINT64*)((PUCHAR)g_GuestVmcbVa + 0x0088) = rbx;
        *(UINT64*)((PUCHAR)g_GuestVmcbVa + 0x0090) = rcx;
        *(UINT64*)((PUCHAR)g_GuestVmcbVa + 0x0098) = rdx;
        break;
    }
    case 0x007B: // NPF (EPT violation)
    {
        UINT64 guestPhysAddr = exitInfo1 & ~0xFFFULL;
        for (ULONG i = 0; i < g_PageCount; i++) {
            if (g_HiddenPages[i] && guestPhysAddr >= g_HiddenPages[i] && guestPhysAddr < g_HiddenPages[i] + 0x1000) {
                // Emular lectura
                PUINT64 pte = NULL;
                int retries = 0;
                while (!pte && retries < 10) {
                    pte = EptSplitTo4Kb(g_Vmx.EptPml4Phys, g_HiddenPages[i]);
                    retries++;
                }
                if (pte) {
                    UINT64 fakePhys = MmGetPhysicalAddress(g_FakePages[i]).QuadPart;
                    *pte = (fakePhys & ~0xFFFULL) | 7;
                    __invept(1, NULL); // AMD usa __invlpg? En realidad INVLPGA, pero simplificamos
                    // Programar restauraci?n con MTF (no implementado en AMD, necesitar?amos IRET)
                }
                break;
            }
        }
        break;
    }
    // Otros casos (MSR, RDTSC, etc.) se pueden a?adir aqu?
    }
}

// Inicializaci?n AMD
NTSTATUS InitAmdHypervisor() {
    if (!IsAmdVSupported()) return STATUS_HV_FEATURE_UNAVAILABLE;

    UINT64 efer = __readmsr(0xC0000080);
    efer |= (1 << 12);
    __writemsr(0xC0000080, efer);

    g_HostVmcbVa = AllocContiguousPhysAmd(4096, &g_HostVmcbPhys);
    if (!g_HostVmcbVa) return STATUS_INSUFFICIENT_RESOURCES;

    g_GuestVmcbVa = AllocContiguousPhysAmd(4096, &g_GuestVmcbPhys);
    if (!g_GuestVmcbVa) { ExFreePoolWithTag(g_HostVmcbVa, 'bVmA'); return STATUS_INSUFFICIENT_RESOURCES; }

    // Configurar guest VMCB con NPT
    RtlZeroMemory(g_GuestVmcbVa, 4096);
    *(UINT64*)((PUCHAR)g_GuestVmcbVa + VMCB_NPT_CR3) = g_Vmx.EptPml4Phys;

    g_AmdHypervisorActive = TRUE;
    return STATUS_SUCCESS;
}

VOID CleanupAmdHypervisor() {
    if (g_AmdHypervisorActive) {
        UINT64 efer = __readmsr(0xC0000080);
        efer &= ~(1 << 12);
        __writemsr(0xC0000080, efer);
        if (g_HostVmcbVa) ExFreePoolWithTag(g_HostVmcbVa, 'bVmA');
        if (g_GuestVmcbVa) ExFreePoolWithTag(g_GuestVmcbVa, 'bVmA');
        g_AmdHypervisorActive = FALSE;
    }
}
