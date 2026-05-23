#pragma once
#include "common.h"

NTSTATUS InitSmpSvm();
VOID CleanupSmpSvm();
BOOLEAN IsSvmActiveOnCurrentCore();
