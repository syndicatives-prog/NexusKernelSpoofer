#include "common.h"
#include "hooks.h"
#include "anti_read.h"

typedef NTSTATUS (*NTREADVIRTUALMEMORY)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);
NTREADVIRTUALMEMORY g_OriginalNtReadVirtualMemory = NULL;
HOOK_INFO g_AntiReadHook = {0};

NTSTATUS HookedNtReadVirtualMemory(HANDLE ProcessHandle, PVOID BaseAddress, PVOID Buffer, SIZE_T BufferSize, PSIZE_T BytesRead) {
    if (g_SpoofData.Enabled && ProcessHandle != NtCurrentProcess()) {
        PVOID start = g_DeviceObject->DriverObject->DriverStart;
        ULONG size = g_DeviceObject->DriverObject->DriverSize;
        if (BaseAddress >= start && ((PUCHAR)BaseAddress + BufferSize) <= ((PUCHAR)start + size))
            return STATUS_ACCESS_DENIED;
    }
    return g_OriginalNtReadVirtualMemory(ProcessHandle, BaseAddress, Buffer, BufferSize, BytesRead);
}

void InitAntiRead() {
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, L"NtReadVirtualMemory");
    g_OriginalNtReadVirtualMemory = (NTREADVIRTUALMEMORY)MmGetSystemRoutineAddress(&name);
    if (g_OriginalNtReadVirtualMemory)
        InstallHookX64(g_OriginalNtReadVirtualMemory, HookedNtReadVirtualMemory, &g_AntiReadHook);
}

void CleanupAntiRead() {
    RemoveHookX64(&g_AntiReadHook);
}