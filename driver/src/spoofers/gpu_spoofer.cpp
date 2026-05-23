#include "gpu_spoofer.h"
#include "common.h"
#include "hypervisor.h"
UINT64 g_GpuPhys = 0;
UCHAR g_FakeGpuConfigPage[4096] = {0};

static ULONG ReadPciConfig(ULONG Bus, ULONG Slot, ULONG Func, ULONG Offset, ULONG Size) {
    PCI_SLOT_NUMBER slotNumber; slotNumber.u.AsULONG = 0;
    slotNumber.u.bits.DeviceNumber = Slot; slotNumber.u.bits.FunctionNumber = Func;
    ULONG value = 0;
    HalGetBusDataByOffset(PCIConfiguration, Bus, slotNumber.u.AsULONG, &value, Offset, Size);
    return value;
}

static UINT64 FindGpuMmioBase() {
    for (ULONG bus = 0; bus < 256; bus++)
        for (ULONG slot = 0; slot < 32; slot++)
            for (ULONG func = 0; func < 8; func++) {
                ULONG vendor = ReadPciConfig(bus, slot, func, 0, 2);
                if (vendor == 0xFFFF) continue;
                if (ReadPciConfig(bus, slot, func, 0x0B, 1) == 0x03) {
                    ULONG bar0Lo = ReadPciConfig(bus, slot, func, 0x10, 4);
                    ULONG bar0Hi = ReadPciConfig(bus, slot, func, 0x14, 4);
                    UINT64 bar0 = ((UINT64)bar0Hi << 32) | (bar0Lo & 0xFFFFFFF0);
                    if (bar0 && (bar0Lo & 1) == 0) return bar0;
                }
            }
    return 0;
}

void InitGpuSpoofer() {
    g_GpuPhys = FindGpuMmioBase();
    if (g_GpuPhys) {
        PVOID mapped = MmMapIoSpace(PHYSICAL_ADDRESS{g_GpuPhys}, 4096, MmNonCached);
        if (mapped) {
            RtlCopyMemory(g_FakeGpuConfigPage, mapped, 4096);
            *(ULONG*)g_FakeGpuConfigPage = 0x10DE;
            *(ULONG*)(g_FakeGpuConfigPage+4) = 0x1B06;
            EptSetFakePage(g_GpuPhys, g_FakeGpuConfigPage);
            EptHidePage(g_GpuPhys, TRUE);
            MmUnmapIoSpace(mapped, 4096);
        }
    }
}

void CleanupGpuSpoofer() { if (g_GpuPhys) EptHidePage(g_GpuPhys, FALSE); }
