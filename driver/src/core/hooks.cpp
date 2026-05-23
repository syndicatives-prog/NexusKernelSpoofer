#include "hooks.h"
static KIRQL DisableWP() {
    KIRQL irql = KeRaiseIrqlToDpcLevel();
    ULONG_PTR cr0 = __readcr0();
    __writecr0(cr0 & ~0x10000);
    return irql;
}
static void EnableWP(KIRQL irql) {
    ULONG_PTR cr0 = __readcr0();
    __writecr0(cr0 | 0x10000);
    KeLowerIrql(irql);
}
NTSTATUS InstallHookX64(PVOID Target, PVOID HookFunction, HOOK_INFO* Hook) {
    if (!Target || !HookFunction || !Hook) return STATUS_INVALID_PARAMETER;
    Hook->TargetAddress = Target;
    Hook->HookFunction = HookFunction;
    INT64 delta = (PUCHAR)HookFunction - ((PUCHAR)Target + 5);
    if (delta < INT_MIN || delta > INT_MAX) return STATUS_NOT_IMPLEMENTED;
    UCHAR jmp[5] = { 0xE9 };
    *(INT32*)(jmp + 1) = (INT32)delta;
    RtlCopyMemory(Hook->OriginalBytes, Target, 5);
    KIRQL irql = DisableWP();
    RtlCopyMemory(Target, jmp, 5);
    EnableWP(irql);
    Hook->Installed = TRUE;
    return STATUS_SUCCESS;
}
void RemoveHookX64(HOOK_INFO* Hook) {
    if (!Hook->Installed) return;
    KIRQL irql = DisableWP();
    RtlCopyMemory(Hook->TargetAddress, Hook->OriginalBytes, 5);
    EnableWP(irql);
    Hook->Installed = FALSE;
}
