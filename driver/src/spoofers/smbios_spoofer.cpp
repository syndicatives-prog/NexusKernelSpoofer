#include "smbios_spoofer.h"
#include "common.h"
#include "hypervisor.h"
#include "hooks.h"

static PDRIVER_OBJECT g_AcpiDriver = NULL;
static PDRIVER_DISPATCH g_OriginalSystemControl = NULL;

VOID SpoofRamInFakePage(PUCHAR FakePage, ULONG PageSize) {
    for (ULONG i = 0; i < PageSize - 2; ) {
        if (FakePage[i] == 17 && FakePage[i+1] >= 0x1C) {
            *(USHORT*)(FakePage + i + 0x14) = 32768;
            *(USHORT*)(FakePage + i + 0x16) = 3200;
        }
        ULONG len = FakePage[i+1];
        PUCHAR strings = FakePage + i + len;
        while (strings[0] || strings[1]) strings++;
        strings += 2;
        i = strings - FakePage;
    }
}

// Hook de IRP_MJ_SYSTEM_CONTROL para SMBIOS (corregido: usar Parameters.WMI)
static NTSTATUS HookedAcpiSystemControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    NTSTATUS status = g_OriginalSystemControl(DeviceObject, Irp);
    if (!g_SpoofData.Enabled || !NT_SUCCESS(status)) return status;

    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    // IRP_MJ_SYSTEM_CONTROL usa Parameters.WMI, no DeviceIoControl
    PVOID outBuf = Irp->AssociatedIrp.SystemBuffer;
    ULONG outLen = stack->Parameters.WMI.BufferLength;  // Corregido

    if (outBuf && outLen >= 0x18 && g_SpoofData.SMBIOS_UUID[0] != '\0') {
        for (ULONG i = 0; i < outLen - 0x19; i++) {
            if (((PUCHAR)outBuf)[i] == 0x01 && ((PUCHAR)outBuf)[i+1] >= 0x08) {
                RtlCopyMemory((PUCHAR)outBuf + i + 8, g_SpoofData.SMBIOS_UUID, 16);
                break;
            }
        }
    }
    return status;
}

void InitSmbiosSpoofer() {
    UNICODE_STRING name; RtlInitUnicodeString(&name, L"\\Driver\\ACPI");
    PDRIVER_OBJECT driver;
    if (!NT_SUCCESS(ObReferenceObjectByName(&name, OBJ_CASE_INSENSITIVE, NULL, 0,
        *IoDriverObjectType, KernelMode, NULL, (PVOID*)&driver))) return;
    g_AcpiDriver = driver;
    g_OriginalSystemControl = driver->MajorFunction[IRP_MJ_SYSTEM_CONTROL];
    InstallHookX64(g_OriginalSystemControl, HookedAcpiSystemControl, &g_SmbiosHook);
}

void CleanupSmbiosSpoofer() {
    RemoveHookX64(&g_SmbiosHook);
    if (g_AcpiDriver) ObDereferenceObject(g_AcpiDriver);
}
