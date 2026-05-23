#include "common.h"
#include "hooks.h"
#include "mac_spoofer.h"

// NDIS function signature
typedef VOID (*NDISOIDREQUEST)(PVOID NdisBindingHandle, PVOID OidRequest);
static NDISOIDREQUEST g_OriginalNdisOidRequest = NULL;
static HOOK_INFO g_MacHook = {0};

#define OID_802_3_PERMANENT_ADDRESS 0x01010101
#define OID_802_3_CURRENT_ADDRESS   0x01010102

typedef struct _NDIS_OID_REQUEST {
    UCHAR RequestType;
    union {
        struct {
            ULONG Oid;
            PVOID InformationBuffer;
            ULONG InformationBufferLength;
            ULONG BytesWritten;
            ULONG BytesNeeded;
        } QUERY_INFORMATION;
    } DATA;
} NDIS_OID_REQUEST;

static VOID HookedNdisOidRequest(PVOID BindingHandle, PVOID Request) {
    if (g_SpoofData.Enabled) {
        NDIS_OID_REQUEST* req = (NDIS_OID_REQUEST*)Request;
        if (req && req->RequestType == 1) {
            ULONG oid = req->DATA.QUERY_INFORMATION.Oid;
            if (oid == OID_802_3_PERMANENT_ADDRESS || oid == OID_802_3_CURRENT_ADDRESS) {
                g_OriginalNdisOidRequest(BindingHandle, Request);
                RtlCopyMemory(req->DATA.QUERY_INFORMATION.InformationBuffer, g_SpoofData.MacAddress, 6);
                return;
            }
        }
    }
    g_OriginalNdisOidRequest(BindingHandle, Request);
}

static PVOID FindNdisOidRequest() {
    UNICODE_STRING ndisName;
    RtlInitUnicodeString(&ndisName, L"\\Driver\\NDIS");
    PDRIVER_OBJECT ndisDriver = NULL;
    ObReferenceObjectByName(&ndisName, OBJ_CASE_INSENSITIVE, NULL, 0, *IoDriverObjectType, KernelMode, NULL, (PVOID*)&ndisDriver);
    if (!ndisDriver) return NULL;

    PUCHAR moduleBase = (PUCHAR)ndisDriver->DriverStart;
    ULONG moduleSize = ndisDriver->DriverSize;
    ObDereferenceObject(ndisDriver);

    UCHAR pattern[] = "\x48\x89\x5C\x24\x08\x48\x89\x6C\x24\x10\x48\x89\x74\x24\x18\x57";
    for (SIZE_T i = 0; i < moduleSize - sizeof(pattern); i++) {
        if (RtlCompareMemory(moduleBase + i, pattern, sizeof(pattern)-1) == sizeof(pattern)-1) {
            return moduleBase + i;
        }
    }
    return NULL;
}

void InitMacSpoofer() {
    PVOID pNdisOidRequest = FindNdisOidRequest();
    if (!pNdisOidRequest) {
        DbgPrint("NdisOidRequest not found\n");
        return;
    }
    g_OriginalNdisOidRequest = (NDISOIDREQUEST)pNdisOidRequest;
    InstallHookX64(pNdisOidRequest, HookedNdisOidRequest, &g_MacHook);
}

void CleanupMacSpoofer() {
    RemoveHookX64(&g_MacHook);
}