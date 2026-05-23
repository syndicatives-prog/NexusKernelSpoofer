#pragma once
#include "common.h"

NTSTATUS InitSmpVmx();
VOID CleanupSmpVmx();
BOOLEAN IsVmxActiveOnCurrentCore();
