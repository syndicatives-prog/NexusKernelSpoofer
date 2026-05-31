#pragma once
#include "common.h"

// Ahora el hook guarda 14 bytes originales en vez de 5
typedef struct _HOOK_INFO {
    PVOID TargetAddress;
    PVOID HookFunction;
    UCHAR OriginalBytes[14];
    BOOLEAN Installed;
} HOOK_INFO;

NTSTATUS InstallHookX64(PVOID Target, PVOID HookFunction, HOOK_INFO* Hook);
void RemoveHookX64(HOOK_INFO* Hook);
