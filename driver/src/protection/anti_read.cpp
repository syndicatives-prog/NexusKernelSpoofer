#include "anti_read.h"
#include "common.h"
#include "hooks.h"
typedef NTSTATUS (*NTREADVIRTUALMEMORY)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);
static NTREADVIRTUALMEMORY g_Original = NULL;
static NTSTATUS Hooked(HANDLE ProcessHandle, PVOID BaseAddress, PVOID Buffer, SIZE_T BufferSize, PSIZE_T BytesRead) {
    if (g_SpoofData.Enabled && ProcessHandle != NtCurrentProcess()) {
        PVOID start = g_DeviceObject->DriverObject->DriverStart;
        ULONG size = g_DeviceObject->DriverObject->DriverSize;
        if (BaseAddress >= start && ((PUCHAR)BaseAddress + BufferSize) <= ((PUCHAR)start + size))
            return STATUS_ACCESS_DENIED;
    }
    return g_Original(ProcessHandle, BaseAddress, Buffer, BufferSize, BytesRead);
}
void InitAntiRead() {
    UNICODE_STRING name; RtlInitUnicodeString(&name, L"NtReadVirtualMemory");
    g_Original = (NTREADVIRTUALMEMORY)MmGetSystemRoutineAddress(&name);
    if (g_Original) InstallHookX64(g_Original, Hooked, &g_AntiReadHook);
}
void CleanupAntiRead() { RemoveHookX64(&g_AntiReadHook); }
