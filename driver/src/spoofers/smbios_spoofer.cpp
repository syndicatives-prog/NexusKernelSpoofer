#include "smbios_spoofer.h"
#include "common.h"
#include "hypervisor.h"
#include "hooks.h"

static PDRIVER_OBJECT g_AcpiDriver = NULL;
static PDRIVER_DISPATCH g_OriginalSystemControl = NULL;

// MEJORA 5: Validación correcta de SMBIOS con verificación de header _SM_
// Estructura del SMBIOS Entry Point (SMBIOS 2.1+)
typedef struct _SMBIOS_ENTRY_POINT {
    UCHAR Anchor[4];           // "_SM_"
    UCHAR Checksum;            // Checksum of entry point
    UCHAR Length;              // Length of entry point
    UCHAR MajorVersion;
    UCHAR MinorVersion;
    USHORT MaxStructureSize;
    UCHAR EntryPointRevision;
    UCHAR FormattedArea[5];
    UCHAR IntermediateAnchor[5]; // "_DMI_"
    UCHAR IntermedateChecksum;
    USHORT StructureTableLength;
    UINT32 StructureTableAddress;
    USHORT NumberOfStructures;
} SMBIOS_ENTRY_POINT;

// Calculat checksum para validación SMBIOS
static BOOLEAN ValidateSmbiosChecksum(PUCHAR Buffer, ULONG Length) {
    UCHAR sum = 0;
    for (ULONG i = 0; i < Length; i++) {
        sum += Buffer[i];
    }
    return (sum == 0);  // La suma debe ser 0
}

// Validar y procesar estructura SMBIOS type 17 (Memory Device)
static VOID ProcessSmbiosMemoryDevice(PUCHAR StructStart, ULONG MaxOffset) {
    if (!StructStart || MaxOffset < 2) return;
    
    UCHAR type = StructStart[0];
    UCHAR length = StructStart[1];
    
    // Type 17 = Memory Device
    if (type != 17) return;
    
    // Validar que length sea razonable (típicamente >= 0x1C para SMBIOS 2.1+)
    if (length < 0x1C || length > 127) return;
    
    // Campos de memoria: offset 0x14 y 0x16 (tamaño en MB y velocidad MHz)
    if (length > 0x17) {
        *(USHORT*)(StructStart + 0x14) = 32768;  // Size in MB
        *(USHORT*)(StructStart + 0x16) = 3200;   // Speed in MHz
    }
}

VOID SpoofRamInFakePage(PUCHAR FakePage, ULONG PageSize) {
    if (!FakePage || PageSize < 32) return;

    // MEJORA 5: Validar header SMBIOS (_SM_ anchor)
    // Estructura SMBIOS Entry Point típicamente al inicio
    SMBIOS_ENTRY_POINT* ep = (SMBIOS_ENTRY_POINT*)FakePage;
    
    BOOLEAN isSmbiosValid = FALSE;
    
    // Verificar _SM_ anchor (0x5F, 0x53, 0x4D, 0x5F = "_SM_")
    if (PageSize >= sizeof(SMBIOS_ENTRY_POINT) &&
        ep->Anchor[0] == 0x5F && ep->Anchor[1] == 0x53 &&
        ep->Anchor[2] == 0x4D && ep->Anchor[3] == 0x5F) {
        
        // Validar versión
        if (ep->MajorVersion >= 2 && ep->MajorVersion <= 3) {
            // Validar checksum (recomendado pero no crítico)
            if (ValidateSmbiosChecksum(FakePage, ep->Length)) {
                isSmbiosValid = TRUE;
            } else {
                // Incluso si checksum falla, proceder con validación de estructura
                isSmbiosValid = TRUE;  // Más permisivo para compatibilidad
            }
        }
    }

    if (!isSmbiosValid) {
        // Si no es un header SMBIOS válido, salir sin modificar
        DbgPrint("SpoofRamInFakePage: Invalid SMBIOS header\n");
        return;
    }

    // Procesar estructuras SMBIOS (comienzan después del entry point)
    ULONG startOffset = (ep->Length + 1) & ~1;  // Alineado a 2 bytes
    
    for (ULONG i = startOffset; i < PageSize - 2; ) {
        // Validar que no estamos fuera de límites
        if (i + 2 > PageSize) break;
        
        UCHAR structType = FakePage[i];
        UCHAR structLen = FakePage[i + 1];
        
        // Type 127 = End of Structures
        if (structType == 127) break;
        
        // Validar que structLen es razonable
        if (structLen < 4 || structLen > 127) break;
        
        // Procesar type 17 (Memory Device)
        ProcessSmbiosMemoryDevice(FakePage + i, PageSize - i);
        
        // Saltar a siguiente estructura (estructura + strings terminados en doble null)
        ULONG nextPos = i + structLen;
        
        // Buscar strings terminados en doble null
        if (nextPos + 1 < PageSize) {
            while (nextPos + 1 < PageSize) {
                if (FakePage[nextPos] == 0 && FakePage[nextPos + 1] == 0) {
                    nextPos += 2;
                    break;
                }
                nextPos++;
            }
        }
        
        if (nextPos >= PageSize) break;
        i = nextPos;
    }
}

// Hook de IRP_MJ_SYSTEM_CONTROL para SMBIOS
// NOTE: Solo procesamos IRPs completadas síncronamente para evitar use-after-free.
// Si el driver original retorna STATUS_PENDING, no modificamos el IRP.
static NTSTATUS HookedAcpiSystemControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    NTSTATUS status = g_OriginalSystemControl(DeviceObject, Irp);
    
    // Only process synchronously completed IRPs
    if (status == STATUS_PENDING) {
        // IRP will be completed asynchronously; we cannot access it further
        return status;
    }
    
    if (!g_SpoofData.Enabled || !NT_SUCCESS(status)) return status;

    ULONG outLen = (ULONG)Irp->IoStatus.Information;
    PVOID outBuf = Irp->AssociatedIrp.SystemBuffer;

    if (outBuf && outLen >= 0x18 && g_SpoofData.SMBIOS_UUID[0] != '\0') {
        for (ULONG i = 0; i < outLen - 0x19; i++) {
            if (((PUCHAR)outBuf)[i] == 0x01 && ((PUCHAR)outBuf)[i+1] >= 0x08) {
                RtlCopyMemory((PUCHAR)outBuf + i + 8, g_SpoofData.SMBIOS_UUID, 16);
                break;
            }
        }
    }
    return status;
}

void InitSmbiosSpoofer() {
    UNICODE_STRING name; 
    RtlInitUnicodeString(&name, L"\\Driver\\ACPI");
    PDRIVER_OBJECT driver;
    if (!NT_SUCCESS(ObReferenceObjectByName(&name, OBJ_CASE_INSENSITIVE, NULL, 0,
        *IoDriverObjectType, KernelMode, NULL, (PVOID*)&driver))) return;
    g_AcpiDriver = driver;
    // Save original and replace slot directly (not inline hook to avoid loop)
    g_OriginalSystemControl = driver->MajorFunction[IRP_MJ_SYSTEM_CONTROL];
    KIRQL irql = KeRaiseIrqlToDpcLevel();
    ULONG_PTR cr0 = __readcr0(); 
    __writecr0(cr0 & ~0x10000UL);  // Disable WP
    driver->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = HookedAcpiSystemControl;
    __writecr0(cr0 | 0x10000UL);   // Re-enable WP
    KeLowerIrql(irql);
    g_SmbiosHook.Installed = TRUE;
    g_SmbiosHook.TargetAddress = &driver->MajorFunction[IRP_MJ_SYSTEM_CONTROL];
    g_SmbiosHook.HookFunction = HookedAcpiSystemControl;
}

void CleanupSmbiosSpoofer() {
    if (g_SmbiosHook.Installed && g_AcpiDriver) {
        KIRQL irql = KeRaiseIrqlToDpcLevel();
        ULONG_PTR cr0 = __readcr0(); 
        __writecr0(cr0 & ~0x10000UL);  // Disable WP
        g_AcpiDriver->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = g_OriginalSystemControl;
        __writecr0(cr0 | 0x10000UL);   // Re-enable WP
        KeLowerIrql(irql);
        g_SmbiosHook.Installed = FALSE;
    }
    if (g_AcpiDriver) { 
        ObDereferenceObject(g_AcpiDriver); 
        g_AcpiDriver = NULL; 
    }
}
