#pragma once
#include "common.h"

typedef struct _HOOK_INFO {
    PVOID TargetAddress;
    PVOID HookFunction;
    UCHAR OriginalBytes[5];
    BOOLEAN Installed;
} HOOK_INFO;

NTSTATUS InstallHookX64(PVOID Target, PVOID HookFunction, HOOK_INFO* Hook);
void RemoveHookX64(HOOK_INFO* Hook);