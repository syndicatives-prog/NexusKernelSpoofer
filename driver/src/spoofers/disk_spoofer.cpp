#include "disk_spoofer.h"
#include "common.h"
#include "hypervisor.h"
UINT64 g_DiskPhys = 0;
UCHAR g_FakeDiskSerialPage[4096] = {0};

void InitDiskSpoofer() {
    UNICODE_STRING name; RtlInitUnicodeString(&name, L"\\Driver\\Disk");
    PDRIVER_OBJECT driver;
    if (!NT_SUCCESS(ObReferenceObjectByName(&name, OBJ_CASE_INSENSITIVE, NULL, 0, *IoDriverObjectType, KernelMode, NULL, (PVOID*)&driver))) return;
    PDEVICE_OBJECT dev = driver->DeviceObject;
    if (dev) {
        g_DiskPhys = MmGetPhysicalAddress(dev->DeviceExtension).QuadPart & ~0xFFFULL;
        RtlCopyMemory(g_FakeDiskSerialPage, dev->DeviceExtension, 4096);
        RtlStringCbCopyA((PCHAR)g_FakeDiskSerialPage + 0x100, 4096-0x100, g_SpoofData.DiskSerial);
        EptSetFakePage(g_DiskPhys, g_FakeDiskSerialPage);
        EptHidePage(g_DiskPhys, TRUE);
    }
    ObDereferenceObject(driver);
}

void CleanupDiskSpoofer() { if (g_DiskPhys) EptHidePage(g_DiskPhys, FALSE); }
