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

    // 14-byte absolute indirect JMP: FF 25 00 00 00 00 <8-byte addr>
    UCHAR jmp14[14] = {
        0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,  // jmp [rip+0]
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 // address
    };
    *(UINT64*)(jmp14 + 6) = (UINT64)HookFunction;

    Hook->TargetAddress = Target;
    Hook->HookFunction  = HookFunction;
    RtlCopyMemory(Hook->OriginalBytes, Target, 14);

    KIRQL irql = DisableWP();
    RtlCopyMemory(Target, jmp14, 14);
    EnableWP(irql);

    Hook->Installed = TRUE;
    return STATUS_SUCCESS;
}

void RemoveHookX64(HOOK_INFO* Hook) {
    if (!Hook->Installed) return;
    KIRQL irql = DisableWP();
    RtlCopyMemory(Hook->TargetAddress, Hook->OriginalBytes, 14);
    EnableWP(irql);
    Hook->Installed = FALSE;
}
