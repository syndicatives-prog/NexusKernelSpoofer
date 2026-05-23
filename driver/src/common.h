#pragma once
#include <ntddk.h>
#include <ntstrsafe.h>
#include <intrin.h>

#define DEVICE_NAME         L"\\Device\\NexusSpoofer"
#define SYMLINK_NAME        L"\\DosDevices\\NexusSpoofer"

#define IOCTL_SPOOF_SET_SERIALS     CTL_CODE(0x8000, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SPOOF_ENABLE          CTL_CODE(0x8000, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SPOOF_DISABLE         CTL_CODE(0x8000, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MAP_DRIVER            CTL_CODE(0x8000, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _SPOOF_DATA {
    BOOLEAN Enabled;
    CHAR DiskSerial[128];
    CHAR VolumeSerial[128];
    CHAR SystemManufacturer[64];
    CHAR SystemProductName[64];
    CHAR SystemSerialNumber[64];
    CHAR BaseBoardSerial[64];
    CHAR SMBIOS_UUID[64];
    UCHAR MacAddress[6];
    CHAR MachineGuid[128];
    CHAR HardwareProfileGuid[128];
    CHAR ProductId[64];
} SPOOF_DATA, *PSPOOF_DATA;

extern SPOOF_DATA g_SpoofData;
extern PDEVICE_OBJECT g_DeviceObject;

typedef struct _HOOK_INFO {
    PVOID TargetAddress;
    PVOID HookFunction;
    UCHAR OriginalBytes[5];
    BOOLEAN Installed;
} HOOK_INFO;

extern HOOK_INFO g_DiskHook;
extern HOOK_INFO g_VolHook;
extern HOOK_INFO g_RegHook;
extern HOOK_INFO g_MacHook;
extern HOOK_INFO g_SmbiosHook;
extern HOOK_INFO g_GpuHook;
extern HOOK_INFO g_AntiReadHook;
extern HOOK_INFO g_ModuleHideHook;
extern HOOK_INFO* g_AllHooks[];
