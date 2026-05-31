#include "common.h"
#include "hooks.h"
#include "hypervisor.h"
#include "svm.h"
#include "smp_vmx.h"
#include "smp_svm.h"
#include "../spoofers/disk_spoofer.h"
#include "../spoofers/volume_spoofer.h"
#include "../spoofers/registry_spoofer.h"
#include "../spoofers/mac_spoofer.h"
#include "../spoofers/smbios_spoofer.h"
#include "../spoofers/gpu_spoofer.h"
#include "../spoofers/tpm_spoofer.h"
#include "../protection/hiding.h"
#include "../protection/anti_read.h"
#include "../protection/module_hiding.h"
#include "../protection/integrity.h"
#include "../protection/hvci_bypass.h"
#include "../mapper/manual_mapper.h"
#include "comm_channel.h"
#include "dynamic_ept.h"
#include "adaptive_spoofer.h"

SPOOF_DATA g_SpoofData = { 0 };
PDEVICE_OBJECT g_DeviceObject = NULL;

HOOK_INFO g_DiskHook = {0};
HOOK_INFO g_VolHook = {0};
HOOK_INFO g_RegHook = {0};
HOOK_INFO g_MacHook = {0};
HOOK_INFO g_SmbiosHook = {0};
HOOK_INFO g_GpuHook = {0};
HOOK_INFO g_AntiReadHook = {0};
HOOK_INFO g_ModuleHideHook = {0};

HOOK_INFO* g_AllHooks[] = {
    &g_DiskHook, &g_VolHook, &g_RegHook, &g_MacHook,
    &g_SmbiosHook, &g_GpuHook, &g_AntiReadHook, &g_ModuleHideHook, NULL
};

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
    case IOCTL_MAP_DRIVER: {
        PVOID image = Irp->AssociatedIrp.SystemBuffer;
        ULONG imageSize = stack->Parameters.DeviceIoControl.InputBufferLength;
        PDRIVER_OBJECT mappedDrv = NULL;
        status = MapDriver(image, imageSize, &mappedDrv);
        break;
    }
    }
    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = info;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

void DriverUnload(PDRIVER_OBJECT DriverObject) {
    CleanupAdaptiveEngine();
    CleanupDynamicEpt();
    CleanupCommChannel();
    CleanupTpmSpoofer();
    CleanupHvciBypass();
    CleanupIntegrityCheck();
    CleanupAntiRead();
    CleanupModuleHiding();
    CleanupGpuSpoofer();
    CleanupMacSpoofer();
    CleanupSmbiosSpoofer();
    CleanupRegistrySpoofer();
    CleanupVolumeSpoofer();
    CleanupDiskSpoofer();
    CleanupSmpVmx();
    CleanupSmpSvm();
    if (IsAmdVSupported()) {
        CleanupAmdHypervisor();
    } else {
        CleanupHypervisor();
    }
    UNICODE_STRING symLink;
    RtlInitUnicodeString(&symLink, SYMLINK_NAME);
    IoDeleteSymbolicLink(&symLink);
    if (g_DeviceObject) IoDeleteDevice(g_DeviceObject);
    DbgPrint("NexusSpoofer unloaded.\n");
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    UNREFERENCED_PARAMETER(RegistryPath);
    NTSTATUS status;
    UNICODE_STRING devName, symLink;
    PDEVICE_OBJECT deviceObj = NULL;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = DriverObject->MajorFunction[IRP_MJ_CLOSE] =
        [](PDEVICE_OBJECT, PIRP Irp) { Irp->IoStatus.Status = STATUS_SUCCESS; IoCompleteRequest(Irp, IO_NO_INCREMENT); return STATUS_SUCCESS; };
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DeviceIoControl;
    DriverObject->DriverUnload = DriverUnload;
    RtlInitUnicodeString(&devName, DEVICE_NAME);
    RtlInitUnicodeString(&symLink, SYMLINK_NAME);
    status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, 0, FALSE, &deviceObj);
    if (!NT_SUCCESS(status)) return status;
    g_DeviceObject = deviceObj;
    status = IoCreateSymbolicLink(&symLink, &devName);
    if (!NT_SUCCESS(status)) { IoDeleteDevice(deviceObj); return status; }

    // Detectar y elegir el hypervisor adecuado
    BOOLEAN hypervisorOk = FALSE;
    if (IsAmdVSupported()) {
        if (NT_SUCCESS(InitAmdHypervisor())) {
            hypervisorOk = TRUE;
            InitSmpSvm();  // SMP para AMD
        }
    } else if (NT_SUCCESS(InitHypervisor())) {
        hypervisorOk = TRUE;
        VmxLaunch(0, 0);  // Parameters unused; ASM uses rsp/rip from VMCS
        InitSmpVmx();  // SMP para Intel
    }

    if (hypervisorOk) {
        InitDiskSpoofer();
        InitVolumeSpoofer();
        InitRegistrySpoofer();
        InitMacSpoofer();
        InitSmbiosSpoofer();
        InitGpuSpoofer();
        InitTpmSpoofer();
        InitHvciBypass();
        InitCommChannel();
        InitDynamicEpt();
        InitAdaptiveEngine();
        InitAntiRead();
        InitModuleHiding();
        HideDriver();
        InitIntegrityCheck();
    } else {
        // Fallback: hooks cl?sicos sin hypervisor
        InitDiskSpoofer();
        InitVolumeSpoofer();
        InitRegistrySpoofer();
        InitMacSpoofer();
        InitSmbiosSpoofer();
        InitGpuSpoofer();
        InitAntiRead();
        InitModuleHiding();
        HideDriver();
        InitIntegrityCheck();
    }
    DbgPrint("NexusKernelSpoofer loaded.\n");
    return STATUS_SUCCESS;
}
