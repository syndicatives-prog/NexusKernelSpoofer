#include "common.h"
#include "hooks.h"

// Forward declarations of init functions
void InitDiskSpoofer();
void InitVolumeSpoofer();
void InitRegistrySpoofer();
void InitMacSpoofer();
void InitSmbiosSpoofer();
void InitProtection();
void CleanupDiskSpoofer();
void CleanupVolumeSpoofer();
void CleanupRegistrySpoofer();
void CleanupMacSpoofer();
void CleanupSmbiosSpoofer();
void CleanupProtection();

SPOOF_DATA g_SpoofData = { 0 };
PDEVICE_OBJECT g_DeviceObject = NULL;

// ---- IOCTL dispatch ----
NTSTATUS DeviceIoControl(PDEVICE_OBJECT, PIRP Irp) {
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG_PTR info = 0;

    switch (stack->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_SPOOF_SET_SERIALS:
        if (stack->Parameters.DeviceIoControl.InputBufferLength >= sizeof(SPOOF_DATA)) {
            RtlCopyMemory(&g_SpoofData, Irp->AssociatedIrp.SystemBuffer, sizeof(SPOOF_DATA));
            status = STATUS_SUCCESS;
        } else status = STATUS_BUFFER_TOO_SMALL;
        break;
    case IOCTL_SPOOF_ENABLE:
        g_SpoofData.Enabled = TRUE;
        status = STATUS_SUCCESS;
        break;
    case IOCTL_SPOOF_DISABLE:
        g_SpoofData.Enabled = FALSE;
        status = STATUS_SUCCESS;
        break;
    }
    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = info;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

// ---- Unload ----
void DriverUnload(PDRIVER_OBJECT DriverObject) {
    CleanupProtection();
    CleanupDiskSpoofer();
    CleanupVolumeSpoofer();
    CleanupRegistrySpoofer();
    CleanupMacSpoofer();
    CleanupSmbiosSpoofer();

    UNICODE_STRING symLink;
    RtlInitUnicodeString(&symLink, SYMLINK_NAME);
    IoDeleteSymbolicLink(&symLink);
    if (g_DeviceObject)
        IoDeleteDevice(g_DeviceObject);

    DbgPrint("NexusSpoofer unloaded.\n");
}

// ---- Entry ----
extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    UNREFERENCED_PARAMETER(RegistryPath);
    NTSTATUS status;
    UNICODE_STRING devName, symLink;
    PDEVICE_OBJECT deviceObj = NULL;

    DriverObject->MajorFunction[IRP_MJ_CREATE] =
    DriverObject->MajorFunction[IRP_MJ_CLOSE] =
        [](PDEVICE_OBJECT, PIRP Irp) {
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_SUCCESS;
        };
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DeviceIoControl;
    DriverObject->DriverUnload = DriverUnload;

    RtlInitUnicodeString(&devName, DEVICE_NAME);
    RtlInitUnicodeString(&symLink, SYMLINK_NAME);

    status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, 0, FALSE, &deviceObj);
    if (!NT_SUCCESS(status)) return status;
    g_DeviceObject = deviceObj;

    status = IoCreateSymbolicLink(&symLink, &devName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(deviceObj);
        return status;
    }

    // Initialize every component
    InitDiskSpoofer();
    InitVolumeSpoofer();
    InitRegistrySpoofer();
    InitMacSpoofer();
    InitSmbiosSpoofer();
    InitProtection();

    DbgPrint("NexusKernelSpoofer loaded.\n");
    return STATUS_SUCCESS;
}