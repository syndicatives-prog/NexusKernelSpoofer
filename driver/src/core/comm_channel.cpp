#include "comm_channel.h"
#include "common.h"

static PVOID g_CommSection = NULL;
static HANDLE g_CommSectionHandle = NULL;

// Eventos como objetos kernel (PKEVENT) para esperas/se?ales
static HANDLE g_RequestEventHandle = NULL;
static HANDLE g_ReplyEventHandle   = NULL;
static PKEVENT g_RequestEvent = NULL;
static PKEVENT g_ReplyEvent   = NULL;

static SPOOF_COMMAND* g_Command = NULL;
static HANDLE g_WorkerThreadHandle = NULL;
static PKTHREAD g_WorkerThreadObj = NULL;

static VOID CommWorker(PVOID Context) {
    while (TRUE) {
        KeWaitForSingleObject(g_RequestEvent, Executive, KernelMode, FALSE, NULL);
        if (!g_Command) break;

        switch (g_Command->CommandId) {
        case 1: // SET
            RtlCopyMemory(&g_SpoofData, &g_Command->Data, sizeof(SPOOF_DATA));
            g_Command->Result = STATUS_SUCCESS;
            break;
        case 2: // ENABLE
            g_SpoofData.Enabled = TRUE;
            g_Command->Result = STATUS_SUCCESS;
            break;
        case 3: // DISABLE
            g_SpoofData.Enabled = FALSE;
            g_Command->Result = STATUS_SUCCESS;
            break;
        case 4: // GET
            RtlCopyMemory(&g_Command->Data, &g_SpoofData, sizeof(SPOOF_DATA));
            g_Command->Result = STATUS_SUCCESS;
            break;
        default:
            g_Command->Result = STATUS_INVALID_PARAMETER;
        }
        KeSetEvent(g_ReplyEvent, IO_NO_INCREMENT, FALSE);
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

NTSTATUS InitCommChannel() {
    UNICODE_STRING sectionName, eventReqName, eventRepName;
    OBJECT_ATTRIBUTES objAttr;
    LARGE_INTEGER sectionSize; sectionSize.QuadPart = sizeof(SPOOF_COMMAND);
    RtlInitUnicodeString(&sectionName, L"\\BaseNamedObjects\\NexusSpooferComm");
    InitializeObjectAttributes(&objAttr, &sectionName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    HANDLE hSection;
    NTSTATUS status = ZwCreateSection(&hSection, SECTION_ALL_ACCESS, &objAttr, &sectionSize, PAGE_READWRITE, SEC_COMMIT, NULL);
    if (!NT_SUCCESS(status)) return status;
    SIZE_T viewSize = sizeof(SPOOF_COMMAND);
    status = ZwMapViewOfSection(hSection, NtCurrentProcess(), &g_CommSection, 0, sizeof(SPOOF_COMMAND), NULL, &viewSize, ViewUnmap, 0, PAGE_READWRITE);
    if (!NT_SUCCESS(status)) { ZwClose(hSection); return status; }
    g_Command = (SPOOF_COMMAND*)g_CommSection;
    g_CommSectionHandle = hSection;

    // Crear eventos como objetos kernel (PKEVENT)
    RtlInitUnicodeString(&eventReqName, L"\\BaseNamedObjects\\NexusSpooferRequest");
    InitializeObjectAttributes(&objAttr, &eventReqName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    status = ZwCreateEvent(&g_RequestEventHandle, EVENT_ALL_ACCESS, &objAttr, SynchronizationEvent, FALSE);
    if (!NT_SUCCESS(status)) { ZwClose(hSection); return status; }
    status = ObReferenceObjectByHandle(g_RequestEventHandle, EVENT_ALL_ACCESS, *ExEventObjectType,
                                       KernelMode, (PVOID*)&g_RequestEvent, NULL);
    ZwClose(g_RequestEventHandle);
    if (!NT_SUCCESS(status)) { ZwClose(hSection); return status; }

    RtlInitUnicodeString(&eventRepName, L"\\BaseNamedObjects\\NexusSpooferReply");
    InitializeObjectAttributes(&objAttr, &eventRepName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    status = ZwCreateEvent(&g_ReplyEventHandle, EVENT_ALL_ACCESS, &objAttr, SynchronizationEvent, FALSE);
    if (!NT_SUCCESS(status)) { ObDereferenceObject(g_RequestEvent); ZwClose(hSection); return status; }
    status = ObReferenceObjectByHandle(g_ReplyEventHandle, EVENT_ALL_ACCESS, *ExEventObjectType,
                                       KernelMode, (PVOID*)&g_ReplyEvent, NULL);
    ZwClose(g_ReplyEventHandle);
    if (!NT_SUCCESS(status)) { ObDereferenceObject(g_RequestEvent); ZwClose(hSection); return status; }

    // Hilo worker
    status = PsCreateSystemThread(&g_WorkerThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, CommWorker, NULL);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(g_RequestEvent);
        ObDereferenceObject(g_ReplyEvent);
        ZwClose(hSection);
        return status;
    }
    status = ObReferenceObjectByHandle(g_WorkerThreadHandle, THREAD_ALL_ACCESS, *PsThreadType,
                                       KernelMode, (PVOID*)&g_WorkerThreadObj, NULL);
    ZwClose(g_WorkerThreadHandle);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(g_RequestEvent);
        ObDereferenceObject(g_ReplyEvent);
        ZwClose(hSection);
        return status;
    }

    return STATUS_SUCCESS;
}

VOID CleanupCommChannel() {
    if (g_WorkerThreadObj) {
        KeSetEvent(g_RequestEvent, IO_NO_INCREMENT, FALSE);
        KeWaitForSingleObject(g_WorkerThreadObj, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(g_WorkerThreadObj);
        g_WorkerThreadObj = NULL;
    }
    if (g_RequestEvent) { ObDereferenceObject(g_RequestEvent); g_RequestEvent = NULL; }
    if (g_ReplyEvent)   { ObDereferenceObject(g_ReplyEvent);   g_ReplyEvent   = NULL; }
    if (g_CommSection) ZwUnmapViewOfSection(NtCurrentProcess(), g_CommSection);
    if (g_CommSectionHandle) ZwClose(g_CommSectionHandle);
}
