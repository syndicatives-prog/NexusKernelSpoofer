#include "mac_spoofer.h"
#include "common.h"
#include "hypervisor.h"

UINT64 g_MacPhys = 0;

// La p?gina falsa ahora cubre la p?gina de 4 KB que contiene la MAC
static UCHAR g_FakeMacPage[4096] = {0};

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

// Offset conocido de la MAC dentro del espacio MMIO (depende de la NIC)
// Para Intel E1000/E1000e, la MAC est? en los primeros 256 bytes, no en 0x5400.
// Usamos 0x0000 como offset gen?rico (muchas NIC la tienen en los primeros bytes del BAR)
#define MAC_OFFSET_IN_BAR  0x0000

void InitMacSpoofer() {
    UINT64 bar0 = FindNicMmioBase();
    if (!bar0) return;

    // La MAC est? en la p?gina de 4 KB base (offset 0) ? ajusta seg?n NIC
    UINT64 macPagePhys = bar0;  // la primera p?gina del MMIO
    g_MacPhys = macPagePhys;

    PVOID mapped = MmMapIoSpace(PHYSICAL_ADDRESS{macPagePhys}, 4096, MmNonCached);
    if (mapped) {
        RtlCopyMemory(g_FakeMacPage, mapped, 4096);
        // Copiar MAC falsa al offset correcto dentro de la p?gina
        RtlCopyMemory(g_FakeMacPage + MAC_OFFSET_IN_BAR, g_SpoofData.MacAddress, 6);

        EptSetFakePage(macPagePhys, g_FakeMacPage);
        EptHidePage(macPagePhys, TRUE);

        MmUnmapIoSpace(mapped, 4096);
    }
}

void CleanupMacSpoofer() {
    if (g_MacPhys) EptHidePage(g_MacPhys, FALSE);
}
