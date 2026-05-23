#pragma once
#include "common.h"
NTSTATUS InstallHookX64(PVOID Target, PVOID HookFunction, HOOK_INFO* Hook);
void RemoveHookX64(HOOK_INFO* Hook);
