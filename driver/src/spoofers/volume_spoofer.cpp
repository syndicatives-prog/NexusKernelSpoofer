#include "common.h"
#include "hooks.h"
#include "volume_spoofer.h"

typedef NTSTATUS (*NTQUERYVOLUMEINFORMATIONFILE)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FS_INFORMATION_CLASS);
static NTQUERYVOLUMEINFORMATIONFILE g_Original = NULL;
static HOOK_INFO g_Hook = { 0 };

static NTSTATUS Hooked(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock,
                       PVOID FileInformation, ULONG Length, FS_INFORMATION_CLASS FsInformationClass) {
    NTSTATUS status = g_Original(FileHandle, IoStatusBlock, FileInformation, Length, FsInformationClass);
    if (g_SpoofData.Enabled && NT_SUCCESS(status) && FsInformationClass == FileFsVolumeInformation) {
        PFILE_FS_VOLUME_INFORMATION info = (PFILE_FS_VOLUME_INFORMATION)FileInformation;
        if (info && g_SpoofData.VolumeSerial[0] != '\0') {
            ULONG serial = 0;
            RtlCharToInteger(g_SpoofData.VolumeSerial, 16, &serial);
            info->VolumeSerialNumber = serial;
        }
    }
    return status;
}

void InitVolumeSpoofer() {
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, L"NtQueryVolumeInformationFile");
    g_Original = (NTQUERYVOLUMEINFORMATIONFILE)MmGetSystemRoutineAddress(&name);
    if (g_Original)
        InstallHookX64(g_Original, Hooked, &g_Hook);
}

void CleanupVolumeSpoofer() {
    RemoveHookX64(&g_Hook);
}