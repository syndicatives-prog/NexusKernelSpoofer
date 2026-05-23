#pragma once
#include "common.h"
NTSTATUS InitHvciBypass();
VOID CleanupHvciBypass();
VOID HvciBypassHideDriver(PDRIVER_OBJECT DriverObject);
