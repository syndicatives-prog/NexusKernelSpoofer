#include "common.h"
#include "hooks.h"
#include "smbios_spoofer.h"

static PDRIVER_OBJECT g_AcpiDriver = NULL;
static PDRIVER_DISPATCH g_OriginalSystemControl = NULL;
static HOOK_INFO g_SmbiosHook = {0};

NTSTATUS HookedAcpiSystemControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    NTSTATUS status = g_OriginalSystemControl(DeviceObject, Irp);
    if (!g_SpoofData.Enabled || !NT_SUCCESS(status)) return status;

    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    PVOID outBuf = Irp->AssociatedIrp.SystemBuffer;
    ULONG outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;

    if (outBuf && outLen >= 0x18 && g_SpoofData.SMBIOS_UUID[0] != '\0') {
        PUCHAR buffer = (PUCHAR)outBuf;
        for (ULONG i = 0; i < outLen - 0x19; i++) {
            if (buffer[i] == 0x01 && buffer[i+1] >= 0x08) {
                RtlCopyMemory(buffer + i + 8, g_SpoofData.SMBIOS_UUID, 16);
                break;
            }
        }
    }
    return status;
}

void InitSmbiosSpoofer() {
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, L"\\Driver\\ACPI");
    PDRIVER_OBJECT driver;
    NTSTATUS status = ObReferenceObjectByName(&name, OBJ_CASE_INSENSITIVE, NULL, 0,
        *IoDriverObjectType, KernelMode, NULL, (PVOID*)&driver);
    if (!NT_SUCCESS(status)) return;

    g_AcpiDriver = driver;
    g_OriginalSystemControl = driver->MajorFunction[IRP_MJ_SYSTEM_CONTROL];
    InstallHookX64(g_OriginalSystemControl, HookedAcpiSystemControl, &g_SmbiosHook);
}

void CleanupSmbiosSpoofer() {
    RemoveHookX64(&g_SmbiosHook);
    if (g_AcpiDriver) ObDereferenceObject(g_AcpiDriver);
}