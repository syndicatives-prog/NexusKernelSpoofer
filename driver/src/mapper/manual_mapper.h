#pragma once
NTSTATUS MapDriver(PVOID ImageBuffer, SIZE_T ImageSize, PDRIVER_OBJECT *OutDriverObject);
VOID UnmapDriver(PDRIVER_OBJECT DriverObject);
