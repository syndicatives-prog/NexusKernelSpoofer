#include "adaptive_spoofer.h"
#include "common.h"
#include "hypervisor.h"
static KTIMER g_AdaptiveTimer;
static KDPC g_AdaptiveDpc;

static VOID AdaptiveDpc(PKDPC Dpc, PVOID DeferredContext, PVOID Arg1, PVOID Arg2) {
    LARGE_INTEGER due; due.QuadPart = -60 * 1000 * 1000 * 10;
    KeSetTimer(&g_AdaptiveTimer, due, &g_AdaptiveDpc);
}

NTSTATUS InitAdaptiveEngine() {
    if (!g_Vmx.HypervisorActive) return STATUS_NOT_SUPPORTED;
    KeInitializeTimer(&g_AdaptiveTimer);
    KeInitializeDpc(&g_AdaptiveDpc, AdaptiveDpc, NULL);
    LARGE_INTEGER due; due.QuadPart = -60 * 1000 * 1000 * 10;
    KeSetTimer(&g_AdaptiveTimer, due, &g_AdaptiveDpc);
    return STATUS_SUCCESS;
}

VOID CleanupAdaptiveEngine() { KeCancelTimer(&g_AdaptiveTimer); }
