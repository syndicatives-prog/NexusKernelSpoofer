#pragma once
#pragma pack(1)

#include <ntddk.h>
#include <ntstrsafe.h>
#include <intrin.h>

#define DEVICE_NAME         L"\\Device\\NexusSpoofer"
#define SYMLINK_NAME        L"\\DosDevices\\NexusSpoofer"

#define IOCTL_SPOOF_SET_SERIALS     CTL_CODE(0x8000, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SPOOF_ENABLE          CTL_CODE(0x8000, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SPOOF_DISABLE         CTL_CODE(0x8000, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MAP_DRIVER            CTL_CODE(0x8000, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define SPOOF_COMMAND_SET    1
#define SPOOF_COMMAND_ENABLE 2
#define SPOOF_COMMAND_DISABLE 3
#define SPOOF_COMMAND_GET    4

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

typedef struct _SPOOF_COMMAND {
    ULONG    CommandId;
    SPOOF_DATA Data;
    NTSTATUS Result;
} SPOOF_COMMAND, *PSPOOF_COMMAND;

// System Module Information structures (used by module_hiding.cpp and adaptive_spoofer.cpp)
typedef struct _SYSTEM_MODULE_ENTRY {
    PVOID  Section;
    PVOID  MappedBase;
    PVOID  ImageBase;
    ULONG  ImageSize;
    ULONG  Flags;
    USHORT LoadOrderIndex;
    USHORT InitOrderIndex;
    USHORT LoadCount;
    USHORT OffsetToFileName;
    UCHAR  FullPathName[256];
} SYSTEM_MODULE_ENTRY, *PSYSTEM_MODULE_ENTRY;

typedef struct _SYSTEM_MODULE_INFORMATION {
    ULONG ModulesCount;
    SYSTEM_MODULE_ENTRY Modules[1];
} SYSTEM_MODULE_INFORMATION, *PSYSTEM_MODULE_INFORMATION;

#pragma pack()

extern SPOOF_DATA g_SpoofData;
extern PDEVICE_OBJECT g_DeviceObject;

// HOOK_INFO is defined in hooks.h ? only forward?declared here
struct _HOOK_INFO;
typedef struct _HOOK_INFO HOOK_INFO;

extern HOOK_INFO g_DiskHook;
extern HOOK_INFO g_VolHook;
extern HOOK_INFO g_RegHook;
extern HOOK_INFO g_MacHook;
extern HOOK_INFO g_SmbiosHook;
extern HOOK_INFO g_GpuHook;
extern HOOK_INFO g_AntiReadHook;
extern HOOK_INFO g_ModuleHideHook;
extern HOOK_INFO* g_AllHooks[];
