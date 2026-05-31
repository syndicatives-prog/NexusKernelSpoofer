#include "tpm_spoofer.h"
#include "common.h"
#include "hypervisor.h"
UINT64 g_TpmPhysBase = 0;
UCHAR g_FakeTpmPage[4096] = {0};

void InitTpmSpoofer() {
    g_TpmPhysBase = 0xFED40000;
    PHYSICAL_ADDRESS pa;
    pa.QuadPart = g_TpmPhysBase;
    PVOID mapped = MmMapIoSpace(pa, 4096, MmNonCached);
    if (mapped) {
        // Copy real TPM page content
        RtlCopyMemory(g_FakeTpmPage, mapped, 4096);
        // Patch vendor ID field (TPM FIFO interface, offset 0xF00)
        // Set vendor ID to unknown to avoid AC detection patterns
        if (4096 > 0xF04) {
            *(UINT32*)(g_FakeTpmPage + 0xF00) = 0x00000000;
        }
        EptSetFakePage(g_TpmPhysBase, g_FakeTpmPage);
        EptHidePage(g_TpmPhysBase, TRUE);
        MmUnmapIoSpace(mapped, 4096);
    }
}

void CleanupTpmSpoofer() { if (g_TpmPhysBase) EptHidePage(g_TpmPhysBase, FALSE); }
