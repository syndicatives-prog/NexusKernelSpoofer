#include "tpm_spoofer.h"
#include "common.h"
#include "hypervisor.h"
UINT64 g_TpmPhysBase = 0;
UCHAR g_FakeTpmPage[4096] = {0};

void InitTpmSpoofer() {
    g_TpmPhysBase = 0xFED40000;
    PVOID mapped = MmMapIoSpace(PHYSICAL_ADDRESS{g_TpmPhysBase}, 4096, MmNonCached);
    if (mapped) {
        RtlZeroMemory(g_FakeTpmPage, 4096);
        EptSetFakePage(g_TpmPhysBase, g_FakeTpmPage);
        EptHidePage(g_TpmPhysBase, TRUE);
        MmUnmapIoSpace(mapped, 4096);
    }
}

void CleanupTpmSpoofer() { if (g_TpmPhysBase) EptHidePage(g_TpmPhysBase, FALSE); }
