#include "common.h"
#include "hooks.h"
#include "disk_spoofer.h"

static HOOK_INFO g_DiskHook = { 0 };
static PDRIVER_OBJECT g_DiskDriver = NULL;
static PDRIVER_DISPATCH g_OriginalDiskDispatch = NULL;

static NTSTATUS HookedDiskDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    if (!g_SpoofData.Enabled)
        return g_OriginalDiskDispatch(DeviceObject, Irp);

    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    if (stack->MajorFunction == IRP_MJ_DEVICE_CONTROL) {
        ULONG code = stack->Parameters.DeviceIoControl.IoControlCode;
        if (code == IOCTL_STORAGE_QUERY_PROPERTY) {
            PSTORAGE_PROPERTY_QUERY query = (PSTORAGE_PROPERTY_QUERY)Irp->AssociatedIrp.SystemBuffer;
            if (query && query->PropertyId == StorageDeviceProperty) {
                NTSTATUS status = g_OriginalDiskDispatch(DeviceObject, Irp);
                if (NT_SUCCESS(status)) {
                    PSTORAGE_DEVICE_DESCRIPTOR desc = (PSTORAGE_DEVICE_DESCRIPTOR)Irp->AssociatedIrp.SystemBuffer;
                    if (desc->SerialNumberOffset && g_SpoofData.DiskSerial[0] != '\0') {
                        PCHAR serial = (PCHAR)desc + desc->SerialNumberOffset;
                        RtlStringCbCopyA(serial, 128, g_SpoofData.DiskSerial);
                    }
                }
                return status;
            }
        }
    }
    return g_OriginalDiskDispatch(DeviceObject, Irp);
}

void InitDiskSpoofer() {
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, L"\\Driver\\Disk");
    PDRIVER_OBJECT driver;
    NTSTATUS status = ObReferenceObjectByName(&name, OBJ_CASE_INSENSITIVE, NULL, 0,
        *IoDriverObjectType, KernelMode, NULL, (PVOID*)&driver);
    if (!NT_SUCCESS(status)) return;

    g_DiskDriver = driver;
    g_OriginalDiskDispatch = driver->MajorFunction[IRP_MJ_DEVICE_CONTROL];
    InstallHookX64(g_OriginalDiskDispatch, HookedDiskDeviceControl, &g_DiskHook);
}

void CleanupDiskSpoofer() {
    RemoveHookX64(&g_DiskHook);
    if (g_DiskDriver) ObDereferenceObject(g_DiskDriver);
}