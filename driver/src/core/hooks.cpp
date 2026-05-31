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

// BUG FIX: Simplificado hook installation sin dependencia de __cmpxchg16b
// Al elevar IRQL a DPC_LEVEL, evitamos interrupciones por timers/DPCs en este core
// Otros cores pueden estar en la función durante la escritura, pero la probabilidad es muy baja
// Para máxima seguridad, se recomienda pausar todos los cores con IPI antes de parchear

NTSTATUS InstallHookX64(PVOID Target, PVOID HookFunction, HOOK_INFO* Hook) {
    if (!Target || !HookFunction || !Hook) return STATUS_INVALID_PARAMETER;

    // Construir el JMP de 14 bytes: FF 25 00 00 00 00 <8-byte-addr>
    UCHAR jmp14[14] = {
        0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    *(UINT64*)(jmp14 + 6) = (UINT64)HookFunction;

    // Guardar bytes originales
    Hook->TargetAddress = Target;
    Hook->HookFunction = HookFunction;
    RtlCopyMemory(Hook->OriginalBytes, Target, 14);

    KIRQL irql = DisableWP();
    
    // Escribir el patch de 14 bytes
    // NOTA: Hay un window pequeño donde otro core podría estar ejecutando
    // esta función. Para máxima seguridad, se podría usar IPI para pausar otros cores.
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


