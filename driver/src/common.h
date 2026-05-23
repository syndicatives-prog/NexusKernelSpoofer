#pragma once
#include <ntddk.h>
#include <ntstrsafe.h>

#define DEVICE_NAME         L"\\Device\\NexusSpoofer"
#define SYMLINK_NAME        L"\\DosDevices\\NexusSpoofer"

#define IOCTL_SPOOF_SET_SERIALS     CTL_CODE(0x8000, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SPOOF_ENABLE          CTL_CODE(0x8000, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SPOOF_DISABLE         CTL_CODE(0x8000, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

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

// Global spoof data – defined in core/main.cpp
extern SPOOF_DATA g_SpoofData;
extern PDEVICE_OBJECT g_DeviceObject;