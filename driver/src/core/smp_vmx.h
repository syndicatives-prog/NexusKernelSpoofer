#pragma once
#include "common.h"

NTSTATUS InitSmpVmx();
VOID CleanupSmpVmx();
BOOLEAN IsVmxActiveOnCurrentCore();

// Obtener EPT del core actual para SMP
UINT64 GetSmpCoreEptPml4Phys();

