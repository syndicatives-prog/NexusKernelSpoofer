#include "disk_spoofer.h"
#include "common.h"
#include "hooks.h"

static PDRIVER_OBJECT g_DiskDriver = NULL;
static PDRIVER_DISPATCH g_OriginalDiskDispatch = NULL;

NTSTATUS DiskCompletion(PDEVICE_OBJECT DevObj, PIRP Irp, PVOID Context) {
    UNREFERENCED_PARAMETER(DevObj);
    UNREFERENCED_PARAMETER(Context);
    if (NT_SUCCESS(Irp->IoStatus.Status)) {
        PSTORAGE_DEVICE_DESCRIPTOR desc = (PSTORAGE_DEVICE_DESCRIPTOR)Irp->AssociatedIrp.SystemBuffer;
        if (desc && desc->SerialNumberOffset && g_SpoofData.DiskSerial[0] != '\0') {
            PCHAR serial = (PCHAR)desc + desc->SerialNumberOffset;
            RtlStringCbCopyA(serial, 128, g_SpoofData.DiskSerial);
        }
    }
    if (Irp->PendingReturned) IoMarkIrpPending(Irp);
    return STATUS_SUCCESS;
}

NTSTATUS HookedDiskDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    if (!g_SpoofData.Enabled) return g_OriginalDiskDispatch(DeviceObject, Irp);
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    if (stack->MajorFunction == IRP_MJ_DEVICE_CONTROL &&
        stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_STORAGE_QUERY_PROPERTY) {
        PSTORAGE_PROPERTY_QUERY query = (PSTORAGE_PROPERTY_QUERY)Irp->AssociatedIrp.SystemBuffer;
        if (query && query->PropertyId == StorageDeviceProperty) {
            IoSetCompletionRoutine(Irp, DiskCompletion, NULL, TRUE, TRUE, TRUE);
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
