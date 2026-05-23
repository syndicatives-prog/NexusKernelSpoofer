#include "mac_spoofer.h"
#include "common.h"
#include "hypervisor.h"
UINT64 g_MacPhys = 0;
UCHAR g_FakeMacPage[4096] = {0};

static ULONG ReadPciConfig(ULONG Bus, ULONG Slot, ULONG Func, ULONG Offset, ULONG Size) {
    PCI_SLOT_NUMBER slotNumber; slotNumber.u.AsULONG = 0;
    slotNumber.u.bits.DeviceNumber = Slot; slotNumber.u.bits.FunctionNumber = Func;
    ULONG value = 0;
    HalGetBusDataByOffset(PCIConfiguration, Bus, slotNumber.u.AsULONG, &value, Offset, Size);
    return value;
}

static UINT64 FindNicMmioBase() {
    for (ULONG bus = 0; bus < 256; bus++)
        for (ULONG slot = 0; slot < 32; slot++)
            for (ULONG func = 0; func < 8; func++) {
                ULONG vendor = ReadPciConfig(bus, slot, func, 0, 2);
                if (vendor == 0xFFFF) continue;
                if (ReadPciConfig(bus, slot, func, 0x0B, 1) == 0x02) {
                    ULONG bar0Lo = ReadPciConfig(bus, slot, func, 0x10, 4);
                    ULONG bar0Hi = ReadPciConfig(bus, slot, func, 0x14, 4);
                    UINT64 bar0 = ((UINT64)bar0Hi << 32) | (bar0Lo & 0xFFFFFFF0);
                    if (bar0 && (bar0Lo & 1) == 0) return bar0;
                }
            }
    return 0;
}

void InitMacSpoofer() {
    g_MacPhys = FindNicMmioBase();
    if (g_MacPhys) {
        PVOID mapped = MmMapIoSpace(PHYSICAL_ADDRESS{g_MacPhys}, 4096, MmNonCached);
        if (mapped) {
            RtlCopyMemory(g_FakeMacPage, mapped, 4096);
            RtlCopyMemory(g_FakeMacPage + 0x5400, g_SpoofData.MacAddress, 6);
            EptSetFakePage(g_MacPhys, g_FakeMacPage);
            EptHidePage(g_MacPhys, TRUE);
            MmUnmapIoSpace(mapped, 4096);
        }
    }
}

void CleanupMacSpoofer() { if (g_MacPhys) EptHidePage(g_MacPhys, FALSE); }
