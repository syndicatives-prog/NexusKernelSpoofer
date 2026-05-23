#pragma once
#include "common.h"

BOOLEAN IsAmdVSupported();
NTSTATUS InitAmdHypervisor();
VOID CleanupAmdHypervisor();
