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

// Hook de IRP_MJ_SYSTEM_CONTROL para SMBIOS
// NOTE: Solo procesamos IRPs completadas síncronamente para evitar use-after-free.
// Si el driver original retorna STATUS_PENDING, no modificamos el IRP.
static NTSTATUS HookedAcpiSystemControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    NTSTATUS status = g_OriginalSystemControl(DeviceObject, Irp);
    
    // Only process synchronously completed IRPs
    if (status == STATUS_PENDING) {
        // IRP will be completed asynchronously; we cannot access it further
        return status;
    }
    
    if (!g_SpoofData.Enabled || !NT_SUCCESS(status)) return status;

    ULONG outLen = (ULONG)Irp->IoStatus.Information;
    PVOID outBuf = Irp->AssociatedIrp.SystemBuffer;

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
    UNICODE_STRING name; 
    RtlInitUnicodeString(&name, L"\\Driver\\ACPI");
    PDRIVER_OBJECT driver;
    if (!NT_SUCCESS(ObReferenceObjectByName(&name, OBJ_CASE_INSENSITIVE, NULL, 0,
        *IoDriverObjectType, KernelMode, NULL, (PVOID*)&driver))) return;
    g_AcpiDriver = driver;
    // Save original and replace slot directly (not inline hook to avoid loop)
    g_OriginalSystemControl = driver->MajorFunction[IRP_MJ_SYSTEM_CONTROL];
    KIRQL irql = KeRaiseIrqlToDpcLevel();
    ULONG_PTR cr0 = __readcr0(); 
    __writecr0(cr0 & ~0x10000UL);  // Disable WP
    driver->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = HookedAcpiSystemControl;
    __writecr0(cr0 | 0x10000UL);   // Re-enable WP
    KeLowerIrql(irql);
    g_SmbiosHook.Installed = TRUE;
    g_SmbiosHook.TargetAddress = &driver->MajorFunction[IRP_MJ_SYSTEM_CONTROL];
    g_SmbiosHook.HookFunction = HookedAcpiSystemControl;
}

void CleanupSmbiosSpoofer() {
    if (g_SmbiosHook.Installed && g_AcpiDriver) {
        KIRQL irql = KeRaiseIrqlToDpcLevel();
        ULONG_PTR cr0 = __readcr0(); 
        __writecr0(cr0 & ~0x10000UL);  // Disable WP
        g_AcpiDriver->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = g_OriginalSystemControl;
        __writecr0(cr0 | 0x10000UL);   // Re-enable WP
        KeLowerIrql(irql);
        g_SmbiosHook.Installed = FALSE;
    }
    if (g_AcpiDriver) { 
        ObDereferenceObject(g_AcpiDriver); 
        g_AcpiDriver = NULL; 
    }
}
