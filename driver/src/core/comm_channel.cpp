#include "comm_channel.h"
#include "common.h"

// MEJORA 2: Eliminada shared section NexusSpooferComm (detectable por enumeradores de handles)
// Los comandos ahora se pasan EXCLUSIVAMENTE vía IOCTL en main.cpp
// Esta función existe solo por compatibilidad pero ya no crea named objects

static VOID CommWorker(PVOID Context) {
    // Worker thread ya no es necesario sin named objects
    // La comunicación ocurre sincronamente en DeviceIoControl handler
    PsTerminateSystemThread(STATUS_SUCCESS);
}

NTSTATUS InitCommChannel() {
    // MEJORA 2: La comunicación ahora es vía IOCTL solamente
    // No se crean named objects (\BaseNamedObjects\NexusSpooferComm, etc.)
    // El driver recibe comandos directamente en DeviceIoControl sin buffer compartido
    
    // Crear un worker thread dummy solo para compatibilidad con cleanup
    HANDLE hWorker;
    NTSTATUS status = PsCreateSystemThread(&hWorker, THREAD_ALL_ACCESS, NULL, NULL, NULL, CommWorker, NULL);
    if (!NT_SUCCESS(status)) {
        DbgPrint("Warning: CommWorker thread creation failed (non-critical)\n");
        return STATUS_SUCCESS;  // No es crítico
    }
    ZwClose(hWorker);
    
    return STATUS_SUCCESS;
}

VOID CleanupCommChannel() {
    // Nada que limpiar: los IOCTLs no crean recursos persistentes
    // Si había worker thread, ya fue cerrado en InitCommChannel
}

