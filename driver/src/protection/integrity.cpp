#include "integrity.h"
#include "common.h"
#include "hooks.h"
static KTIMER g_Timer;
static KDPC g_Dpc;
extern HOOK_INFO* g_AllHooks[];
static void DpcRoutine(PKDPC Dpc, PVOID DeferredContext, PVOID Arg1, PVOID Arg2) {
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    
    for (int i = 0; g_AllHooks[i] != NULL; i++) {
        HOOK_INFO* hook = g_AllHooks[i];
        if (hook->Installed && hook->HookFunction && hook->TargetAddress) {
            // Build expected JMP instruction
            UCHAR expectedJmp[14] = {
                0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
            };
            *(UINT64*)(expectedJmp + 6) = (UINT64)hook->HookFunction;
            
            // Compare current bytes with expected JMP
            ULONG matchExpected = (ULONG)RtlCompareMemory(hook->TargetAddress, expectedJmp, 14);
            
            if (matchExpected != 14) {
                // Hook not installed or overwritten, reinstall
                hook->Installed = FALSE;
                InstallHookX64(hook->TargetAddress, hook->HookFunction, hook);
            }
        }
    }
    LARGE_INTEGER due; 
    due.QuadPart = -30 * 1000 * 1000 * 10;
    KeSetTimer(&g_Timer, due, &g_Dpc);
}
void InitIntegrityCheck() {
    KeInitializeTimer(&g_Timer);
    KeInitializeDpc(&g_Dpc, DpcRoutine, NULL);
    LARGE_INTEGER due; due.QuadPart = -30 * 1000 * 1000 * 10;
    KeSetTimer(&g_Timer, due, &g_Dpc);
}
void CleanupIntegrityCheck() { KeCancelTimer(&g_Timer); }
