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
            // Calculate safe available length to prevent buffer overflow
            ULONG safeLen = desc->Size > desc->SerialNumberOffset 
                            ? desc->Size - desc->SerialNumberOffset 
                            : 0;
            if (safeLen > 0) {
                PCHAR serial = (PCHAR)desc + desc->SerialNumberOffset;
                RtlStringCbCopyA(serial, min(safeLen, 128UL), g_SpoofData.DiskSerial);
            }
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
    // Save original and replace slot directly (not inline hook to avoid loop)
    g_OriginalDiskDispatch = driver->MajorFunction[IRP_MJ_DEVICE_CONTROL];
    KIRQL irql = KeRaiseIrqlToDpcLevel();
    ULONG_PTR cr0 = __readcr0(); 
    __writecr0(cr0 & ~0x10000UL);  // Disable WP
    driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = HookedDiskDeviceControl;
    __writecr0(cr0 | 0x10000UL);   // Re-enable WP
    KeLowerIrql(irql);
    g_DiskHook.Installed = TRUE;
    g_DiskHook.TargetAddress = &driver->MajorFunction[IRP_MJ_DEVICE_CONTROL];
    g_DiskHook.HookFunction = HookedDiskDeviceControl;
}

void CleanupDiskSpoofer() {
    if (g_DiskHook.Installed && g_DiskDriver) {
        KIRQL irql = KeRaiseIrqlToDpcLevel();
        ULONG_PTR cr0 = __readcr0(); 
        __writecr0(cr0 & ~0x10000UL);  // Disable WP
        g_DiskDriver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = g_OriginalDiskDispatch;
        __writecr0(cr0 | 0x10000UL);   // Re-enable WP
        KeLowerIrql(irql);
        g_DiskHook.Installed = FALSE;
    }
    if (g_DiskDriver) { 
        ObDereferenceObject(g_DiskDriver); 
        g_DiskDriver = NULL; 
    }
}
