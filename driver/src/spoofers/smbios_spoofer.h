#pragma once
void InitSmbiosSpoofer();
void CleanupSmbiosSpoofer();
VOID SpoofRamInFakePage(PUCHAR FakePage, ULONG PageSize);
