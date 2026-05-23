#include <windows.h>
#include <stdio.h>

#define SPOOF_COMMAND_SET    1
#define SPOOF_COMMAND_ENABLE 2
#define SPOOF_COMMAND_DISABLE 3

typedef struct {
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
} SPOOF_DATA;

typedef struct {
    ULONG CommandId;
    SPOOF_DATA Data;
    NTSTATUS Result;
} SPOOF_COMMAND;

int main() {
    // Open shared memory
    HANDLE hSection = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, L"NexusSpooferComm");
    if (!hSection) {
        printf("Error: Driver not loaded. Run kdmapper first.\n");
        return 1;
    }
    SPOOF_COMMAND* cmd = (SPOOF_COMMAND*)MapViewOfFile(hSection, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SPOOF_COMMAND));
    if (!cmd) { CloseHandle(hSection); return 1; }

    // Open events
    HANDLE hReq = OpenEventW(EVENT_ALL_ACCESS, FALSE, L"NexusSpooferRequest");
    HANDLE hRep = OpenEventW(EVENT_ALL_ACCESS, FALSE, L"NexusSpooferReply");
    if (!hReq || !hRep) { UnmapViewOfFile(cmd); CloseHandle(hSection); return 1; }

    // Prepare spoof data
    SPOOF_DATA data = {0};
    data.Enabled = TRUE;
    strcpy_s(data.DiskSerial, "SAMSUNG_MZVLW256HEHP-000L7");
    strcpy_s(data.VolumeSerial, "1234-5678");
    strcpy_s(data.SystemManufacturer, "Dell Inc.");
    strcpy_s(data.SystemProductName, "XPS 15 9520");
    strcpy_s(data.SystemSerialNumber, "ABC123XYZ");
    UCHAR mac[6] = {0x00, 0x15, 0x5D, 0xAA, 0xBB, 0xCC};
    memcpy(data.MacAddress, mac, 6);

    // Send SET command
    cmd->CommandId = SPOOF_COMMAND_SET;
    memcpy(&cmd->Data, &data, sizeof(data));
    SetEvent(hReq);
    WaitForSingleObject(hRep, INFINITE);
    printf("SET result: 0x%X\n", cmd->Result);

    // Enable spoofing
    cmd->CommandId = SPOOF_COMMAND_ENABLE;
    SetEvent(hReq);
    WaitForSingleObject(hRep, INFINITE);
    printf("ENABLE result: 0x%X\n", cmd->Result);

    printf("Spoofing active. Press Enter to disable and exit.\n");
    getchar();

    // Disable and cleanup
    cmd->CommandId = SPOOF_COMMAND_DISABLE;
    SetEvent(hReq);
    WaitForSingleObject(hRep, INFINITE);

    UnmapViewOfFile(cmd);
    CloseHandle(hSection);
    CloseHandle(hReq);
    CloseHandle(hRep);
    return 0;
}
