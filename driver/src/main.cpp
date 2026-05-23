#include <ntifs.h>
#include "../include/hypervisor.h"
#include "../include/spoofers.h"

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    UNREFERENCED_PARAMETER(RegistryPath);

    // Initialize hypervisor and all spoofers
    InitializeHypervisor();
    SpoofAllComponents();

    // Set unload routine for cleaner
    DriverObject->DriverUnload = DriverUnload;

    return STATUS_SUCCESS;
}

void DriverUnload(PDRIVER_OBJECT DriverObject) {
    PerformCleanUp();
}