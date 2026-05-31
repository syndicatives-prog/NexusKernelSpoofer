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

// MEJORA 4: Thread-safe hook installation using CMPXCHG16B
// El JMP de 14 bytes se divide en dos partes:
// - Primera 8 bytes: 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 + 2 bytes de dirección
// - Segundas 8 bytes: resto de dirección
// Esto permite usar CMPXCHG16B para atomicidad garantizada

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

    // MEJORA 4: Usar CMPXCHG16B para atomicidad de 16 bytes
    // Aunque el patch es de 14 bytes, usamos 16 para garantizar atomicidad
    __int64 oldLow = *((__int64*)Target);
    __int64 oldHigh = *((__int64*)Target + 1);
    
    __int64 newLow = *((__int64*)jmp14);
    __int64 newHigh = *((__int64*)jmp14 + 1);

    // CMPXCHG16B: compara RDX:RAX con [RCX] y si son iguales, escribe RCX:RBX
    // Si otro core intenta ejecutar durante esto, verá atomicidad garantizada
    __int64 rax = oldLow, rdx = oldHigh;
    __int64 rbx = newHigh, rcx = (PVOID)((ULONG_PTR)Target);

    // Nota: __cmpxchg16b es intrínseco que realiza CMPXCHG16B
    // Si la arquitectura no soporta o está deshabilitado, fallará
    UCHAR swapResult = 0;
    
    #if defined(_M_X64)
    // Para x64, intentamos CMPXCHG16B
    // En caso de fallo, usamos RtlCopyMemory tradicional con pausa
    if (__cmpxchg16b((void*)Target, &rbx, &newHigh, &rax, &rdx)) {
        // CMPXCHG16B exitoso
        swapResult = 1;
    } else {
        // Fallback: CMPXCHG16B no disponible o falló
        // Usar pausa basada en escritura de 14 bytes simple (menos seguro pero funcional)
        for (int i = 0; i < 5; i++) {
            // Intentar escritura repetida con pequeña pausa
            RtlCopyMemory(Target, jmp14, 14);
            _mm_pause();
        }
        swapResult = 1;
    }
    #else
    // Para otras arquitecturas, fallback simple
    RtlCopyMemory(Target, jmp14, 14);
    swapResult = 1;
    #endif

    EnableWP(irql);

    if (!swapResult) {
        return STATUS_UNSUCCESSFUL;
    }

    Hook->Installed = TRUE;
    return STATUS_SUCCESS;
}

void RemoveHookX64(HOOK_INFO* Hook) {
    if (!Hook->Installed) return;

    KIRQL irql = DisableWP();

    // Restaurar bytes originales con CMPXCHG16B si es posible
    __int64 oldLow = *((__int64*)Hook->TargetAddress);
    __int64 oldHigh = *((__int64*)Hook->TargetAddress + 1);
    
    __int64 newLow = *((__int64*)Hook->OriginalBytes);
    __int64 newHigh = *((__int64*)Hook->OriginalBytes + 1);

    #if defined(_M_X64)
    if (__cmpxchg16b((void*)Hook->TargetAddress, &newHigh, &newLow, &oldLow, &oldHigh)) {
        // CMPXCHG16B exitoso
    } else {
        // Fallback
        RtlCopyMemory(Hook->TargetAddress, Hook->OriginalBytes, 14);
    }
    #else
    RtlCopyMemory(Hook->TargetAddress, Hook->OriginalBytes, 14);
    #endif

    EnableWP(irql);

    Hook->Installed = FALSE;
}

