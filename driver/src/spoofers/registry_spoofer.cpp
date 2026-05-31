#include "registry_spoofer.h"
#include "common.h"
#include "hooks.h"
typedef NTSTATUS (*NTQUERYVALUEKEY)(HANDLE, PUNICODE_STRING, KEY_VALUE_INFORMATION_CLASS, PVOID, ULONG, PULONG);
static NTQUERYVALUEKEY g_Original = NULL;
static NTSTATUS Hooked(HANDLE KeyHandle, PUNICODE_STRING ValueName,
                       KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
                       PVOID KeyValueInformation, ULONG Length, PULONG ResultLength) {
    NTSTATUS status = g_Original(KeyHandle, ValueName, KeyValueInformationClass, KeyValueInformation, Length, ResultLength);
    if (!g_SpoofData.Enabled || !NT_SUCCESS(status)) return status;
    if (KeyValueInformationClass == KeyValuePartialInformation || KeyValueInformationClass == KeyValueFullInformation) {
        PKEY_VALUE_PARTIAL_INFORMATION partial = (PKEY_VALUE_PARTIAL_INFORMATION)KeyValueInformation;
        if (partial->Type == REG_SZ && partial->DataLength >= 2 && ValueName && ValueName->Buffer) {
            PWSTR val = ValueName->Buffer;
            UNICODE_STRING uniVal;
            ANSI_STRING ansiVal;
            
            if (_wcsicmp(val, L"SystemProductName") == 0 && g_SpoofData.SystemProductName[0]) {
                RtlInitAnsiString(&ansiVal, g_SpoofData.SystemProductName);
                if (NT_SUCCESS(RtlAnsiStringToUnicodeString(&uniVal, &ansiVal, TRUE))) {
                    RtlStringCbCopyW((PWSTR)partial->Data, partial->DataLength, uniVal.Buffer);
                    RtlFreeUnicodeString(&uniVal);
                }
            }
            else if (_wcsicmp(val, L"SystemManufacturer") == 0 && g_SpoofData.SystemManufacturer[0]) {
                RtlInitAnsiString(&ansiVal, g_SpoofData.SystemManufacturer);
                if (NT_SUCCESS(RtlAnsiStringToUnicodeString(&uniVal, &ansiVal, TRUE))) {
                    RtlStringCbCopyW((PWSTR)partial->Data, partial->DataLength, uniVal.Buffer);
                    RtlFreeUnicodeString(&uniVal);
                }
            }
            else if (_wcsicmp(val, L"SystemSerialNumber") == 0 && g_SpoofData.SystemSerialNumber[0]) {
                RtlInitAnsiString(&ansiVal, g_SpoofData.SystemSerialNumber);
                if (NT_SUCCESS(RtlAnsiStringToUnicodeString(&uniVal, &ansiVal, TRUE))) {
                    RtlStringCbCopyW((PWSTR)partial->Data, partial->DataLength, uniVal.Buffer);
                    RtlFreeUnicodeString(&uniVal);
                }
            }
            else if (_wcsicmp(val, L"MachineGuid") == 0 && g_SpoofData.MachineGuid[0]) {
                RtlInitAnsiString(&ansiVal, g_SpoofData.MachineGuid);
                if (NT_SUCCESS(RtlAnsiStringToUnicodeString(&uniVal, &ansiVal, TRUE))) {
                    RtlStringCbCopyW((PWSTR)partial->Data, partial->DataLength, uniVal.Buffer);
                    RtlFreeUnicodeString(&uniVal);
                }
            }
            else if (_wcsicmp(val, L"HardwareProfileGuid") == 0 && g_SpoofData.HardwareProfileGuid[0]) {
                RtlInitAnsiString(&ansiVal, g_SpoofData.HardwareProfileGuid);
                if (NT_SUCCESS(RtlAnsiStringToUnicodeString(&uniVal, &ansiVal, TRUE))) {
                    RtlStringCbCopyW((PWSTR)partial->Data, partial->DataLength, uniVal.Buffer);
                    RtlFreeUnicodeString(&uniVal);
                }
            }
            else if (_wcsicmp(val, L"ProductId") == 0 && g_SpoofData.ProductId[0]) {
                RtlInitAnsiString(&ansiVal, g_SpoofData.ProductId);
                if (NT_SUCCESS(RtlAnsiStringToUnicodeString(&uniVal, &ansiVal, TRUE))) {
                    RtlStringCbCopyW((PWSTR)partial->Data, partial->DataLength, uniVal.Buffer);
                    RtlFreeUnicodeString(&uniVal);
                }
            }
        }
    }
    return status;
}
void InitRegistrySpoofer() {
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, L"NtQueryValueKey");
    g_Original = (NTQUERYVALUEKEY)MmGetSystemRoutineAddress(&name);
    if (g_Original) InstallHookX64(g_Original, Hooked, &g_RegHook);
}
void CleanupRegistrySpoofer() { RemoveHookX64(&g_RegHook); }
