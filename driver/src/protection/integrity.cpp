#include "integrity.h"
#include "common.h"
#include "hooks.h"
static KTIMER g_Timer;
static KDPC g_Dpc;
extern HOOK_INFO* g_AllHooks[];
static void DpcRoutine(PKDPC Dpc, PVOID DeferredContext, PVOID Arg1, PVOID Arg2) {
    for (int i = 0; g_AllHooks[i] != NULL; i++) {
        HOOK_INFO* hook = g_AllHooks[i];
        if (hook->Installed && hook->HookFunction && RtlCompareMemory(hook->TargetAddress, hook->OriginalBytes, 5) == 5) {
            RemoveHookX64(hook);
            InstallHookX64(hook->TargetAddress, hook->HookFunction, hook);
        }
    }
    LARGE_INTEGER due; due.QuadPart = -30 * 1000 * 1000 * 10;
    KeSetTimer(&g_Timer, due, &g_Dpc);
}
void InitIntegrityCheck() {
    KeInitializeTimer(&g_Timer);
    KeInitializeDpc(&g_Dpc, DpcRoutine, NULL);
    LARGE_INTEGER due; due.QuadPart = -30 * 1000 * 1000 * 10;
    KeSetTimer(&g_Timer, due, &g_Dpc);
}
void CleanupIntegrityCheck() { KeCancelTimer(&g_Timer); }
