# NexusKernelSpoofer

**The most advanced open-source kernel spoofer for Windows 11 (x64).**  
Bypasses EAC, BattlEye, Vanguard, and HVCI/VBS via hardware-assisted virtualization.

## Features

- **Hypervisor-based spoofing (VT-x + EPT)**  
  Hides physical hardware: SMBIOS, disk serials, GPU, MAC, TPM, RAM SPD.
- **CPUID / MSR / RDTSC spoofing** - Anti-timing, hidden VMX presence.
- **Dynamic EPT rotation** - Pages relocated every 10s to evade signatures.
- **HVCI / VBS bypass** - Shadow-page execution keeps code invisible to VTL 1.
- **DKOM + module hiding** - Removes driver from PsLoadedModuleList and SystemModuleInformation.
- **Encrypted communication channel** - Shared memory + events, no IOCTL.
- **Manual mapper** - Full import resolution, relocations, .pdata support.
- **UEFI bootkit** - Pre-OS persistence (optional, in uefi/).
- **Fallback legacy hooks** - Works even without VT-x.

## Requirements

- Windows 11 22H2+ (x64)
- Intel VT-x enabled (AMD-V is not yet supported)
- Secure Boot **disabled**
- Visual Studio 2022 + WDK 11
- [Zydis](https://github.com/zyantific/zydis) cloned into `libs/zydis`

## Building

~~~
git clone --recurse-submodules https://github.com/wasimodo947-pixel/NexusKernelSpoofer
cd NexusKernelSpoofer
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
~~~

Output: `build/Release/NexusKernelSpoofer.sys`

## Usage

1. Load the driver with your favourite mapper (kdmapper, manual mapper, etc.).
2. Run `spoofer_client.exe` to configure and enable spoofing.
3. Launch your game; hardware values are now virtualized.

## Modules

| Module | Purpose |
|--------|---------|
| `hypervisor` | VT-x VMX root + EPT management |
| `disk_spoofer` | IRP hook on `\Driver\Disk` (serial) |
| `gpu_spoofer` | PCI scan -> EPT hide GPU MMIO |
| `mac_spoofer` | PCI scan -> EPT hide NIC MMIO |
| `smbios_spoofer` | EPT hide SMBIOS tables + RAM SPD |
| `tpm_spoofer` | EPT hide TPM MMIO |
| `registry_spoofer` | `NtQueryValueKey` hook for registry keys |
| `volume_spoofer` | `NtQueryVolumeInformationFile` hook |
| `protection` | DKOM, anti-read, module hiding, integrity, HVCI bypass |
| `mapper` | Manual mapper (imports, relocs, `.pdata`) |
| `comm_channel` | Encrypted shared-memory communication |
| `dynamic_ept` | Page rotation every 10s |
| `adaptive_spoofer` | Timer-based environment detection |
| `uefi/` | UEFI bootkit agent & payload |

## Disclaimer

**This project is for educational and research purposes only.**  
Misuse can result in permanent hardware damage or legal consequences. The authors assume no liability.

## Credits

Developed by **wasimodo947-pixel** and **Jack**, with low-level architecture by **Fox**.
