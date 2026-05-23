#include "smbios_spoofer.h"
#include "common.h"
#include "hypervisor.h"

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

void InitSmbiosSpoofer() {
    UNICODE_STRING name; RtlInitUnicodeString(&name, L"\\Driver\\ACPI");
    PDRIVER_OBJECT driver;
    if (!NT_SUCCESS(ObReferenceObjectByName(&name, OBJ_CASE_INSENSITIVE, NULL, 0, *IoDriverObjectType, KernelMode, NULL, (PVOID*)&driver))) return;
    g_AcpiDriver = driver;
    g_OriginalSystemControl = driver->MajorFunction[IRP_MJ_SYSTEM_CONTROL];
}

void CleanupSmbiosSpoofer() {
    if (g_AcpiDriver) ObDereferenceObject(g_AcpiDriver);
}
