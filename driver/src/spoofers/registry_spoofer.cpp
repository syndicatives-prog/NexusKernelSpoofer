#include "common.h"
#include "hooks.h"
#include "registry_spoofer.h"

typedef NTSTATUS (*NTQUERYVALUEKEY)(HANDLE, PUNICODE_STRING, KEY_VALUE_INFORMATION_CLASS, PVOID, ULONG, PULONG);
static NTQUERYVALUEKEY g_Original = NULL;
static HOOK_INFO g_Hook = { 0 };

static NTSTATUS Hooked(HANDLE KeyHandle, PUNICODE_STRING ValueName,
                       KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
                       PVOID KeyValueInformation, ULONG Length, PULONG ResultLength) {
    NTSTATUS status = g_Original(KeyHandle, ValueName, KeyValueInformationClass,
                                 KeyValueInformation, Length, ResultLength);
    if (!g_SpoofData.Enabled || !NT_SUCCESS(status)) return status;

    if (KeyValueInformationClass == KeyValuePartialInformation ||
        KeyValueInformationClass == KeyValueFullInformation) {
        PKEY_VALUE_PARTIAL_INFORMATION partial = (PKEY_VALUE_PARTIAL_INFORMATION)KeyValueInformation;
        if (partial->Type == REG_SZ && partial->DataLength >= 2) {
            if (ValueName && ValueName->Buffer) {
                PWSTR val = ValueName->Buffer;
                if (_wcsicmp(val, L"SystemProductName") == 0)
                    RtlStringCbCopyW((PWSTR)partial->Data, partial->DataLength, L"FakeProduct");
                else if (_wcsicmp(val, L"SystemManufacturer") == 0)
                    RtlStringCbCopyW((PWSTR)partial->Data, partial->DataLength, L"FakeManufacturer");
                else if (_wcsicmp(val, L"SystemSerialNumber") == 0)
                    RtlStringCbCopyW((PWSTR)partial->Data, partial->DataLength, L"FakeSerial");
                else if (_wcsicmp(val, L"MachineGuid") == 0)
                    RtlStringCbCopyW((PWSTR)partial->Data, partial->DataLength, L"{Fake-Machine-GUID}");
                else if (_wcsicmp(val, L"HardwareProfileGuid") == 0)
                    RtlStringCbCopyW((PWSTR)partial->Data, partial->DataLength, L"{Fake-HW-Profile}");
                else if (_wcsicmp(val, L"ProductId") == 0)
                    RtlStringCbCopyW((PWSTR)partial->Data, partial->DataLength, L"FakeProductId");
            }
        }
    }
    return status;
}

void InitRegistrySpoofer() {
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, L"NtQueryValueKey");
    g_Original = (NTQUERYVALUEKEY)MmGetSystemRoutineAddress(&name);
    if (g_Original)
        InstallHookX64(g_Original, Hooked, &g_Hook);
}

void CleanupRegistrySpoofer() {
    RemoveHookX64(&g_Hook);
}