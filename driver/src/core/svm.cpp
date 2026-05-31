#include "svm.h"
#include "hypervisor.h"

#define MSR_EFER 0xC0000080  // Definici?n local de MSR_EFER

static BOOLEAN g_AmdHypervisorActive = FALSE;
// ... (resto del c?digo igual que en el bug 16)
