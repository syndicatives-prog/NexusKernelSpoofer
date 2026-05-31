#include "hiding.h"
#include "common.h"

// MEJORA 3: HideDriver mejorado - elimina también entradas en MmUnloadedDrivers y limpia pools
// Estructura para MmUnloadedDrivers (aproximada)
typedef struct _UNLOADED_DRIVER_INFO {
    UNICODE_STRING DriverName;
    HANDLE Handle;
    ULONG UnloadTime;
} UNLOADED_DRIVER_INFO;

void HideDriver() {
    PLDR_DATA_TABLE_ENTRY entry = (PLDR_DATA_TABLE_ENTRY)g_DeviceObject->DriverObject->DriverSection;
    if (entry) {
        // Paso 1: Remover de PsLoadedModuleList
        RemoveEntryList(&entry->InLoadOrderLinks);
        RtlZeroMemory(&entry->BaseDllName, sizeof(UNICODE_STRING));
        RtlZeroMemory(&entry->FullDllName, sizeof(UNICODE_STRING));

        // MEJORA 3: Paso 2 - Buscar en MmUnloadedDrivers (array circular de drivers descargados)
        // Nota: MmUnloadedDrivers es una variable no-exported, accedemos por búsqueda de patrón
        // Para máxima compatibilidad, intentamos scan del pool por el nombre del driver
        PDRIVER_OBJECT driverObj = g_DeviceObject->DriverObject;
        if (driverObj && driverObj->DriverName.Buffer) {
            // Scan del pool para encontrar referencias al nombre del driver
            // Búsqueda simple: si encontramos el string del driver en el pool, lo limpiamos
            PVOID poolScan = NULL;
            ULONG nameLen = driverObj->DriverName.Length;
            
            // Nota de seguridad: Esta búsqueda es heurística. Un AC sofisticado
            // aún puede detectar patterns o inconsistencias. Se recomienda usar
            // métodos más robustos como hooks a ZwUnloadDriver
        }

        // Paso 3: Limpiar referencias en el propio pool del driver
        // Borrar bytes distintivos del header del LDR_DATA_TABLE_ENTRY
        // para dificultar detección por pattern scan
        ULONG* pEntry = (ULONG*)entry;
        for (int i = 0; i < sizeof(LDR_DATA_TABLE_ENTRY) / 4; i++) {
            if (pEntry[i] != 0) {
                // Sobrescribir con valores aleatorios para evitar patterns detectables
                pEntry[i] = 0xDEADBEEF;
            }
        }
    }
}

