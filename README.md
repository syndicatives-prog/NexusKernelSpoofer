# NexusKernelSpoofer

**The most advanced open‑source kernel spoofer for Windows 11 (x64).**  
Bypasses EAC, BattlEye, Vanguard, and HVCI/VBS via hardware‑assisted virtualization.  
Supports **Intel VT‑x** and **AMD SVM**.

## Features

- **Hypervisor‑based spoofing (Intel VT‑x + AMD SVM)**  
  Hides physical hardware: SMBIOS, disk serials, GPU, MAC, TPM, RAM SPD.
- **CPUID / MSR / RDTSC spoofing** – Anti‑timing, hidden VMX/SVM presence.
- **Dynamic EPT/NPT rotation** – Pages relocated every 10 s to evade signatures.
- **HVCI / VBS bypass** – Shadow‑page execution keeps code invisible to VTL 1.
- **DKOM + module hiding** – Removes driver from `PsLoadedModuleList` and `SystemModuleInformation`.
- **Encrypted communication channel** – Shared memory + events, no IOCTL.
- **Manual mapper** – Full import resolution, relocations, `.pdata` support.
- **UEFI bootkit** – Pre‑OS persistence (optional, see `uefi/`).
- **Adaptive engine** – Auto‑detects anti‑cheat and adjusts profiles.
- **Fallback legacy hooks** – Works even without VT‑x/AMD‑V.

## Requirements

- Windows 11 22H2+ (x64)
- Intel VT‑x **or** AMD SVM enabled
- Secure Boot **disabled**
- Visual Studio 2022 + WDK 11
- [Zydis](https://github.com/zyantific/zydis) cloned into `libs/zydis`

## Building

1. Clone with submodules:
   ~~~
   git clone --recurse-submodules https://github.com/wasimodo947-pixel/NexusKernelSpoofer
   cd NexusKernelSpoofer
   ~~~
2. Build the driver:
   ~~~
   cmake -B build -G "Visual Studio 17 2022"
   cmake --build build --config Release
   ~~~
   Output: `build/Release/NexusKernelSpoofer.sys`

3. Build the user‑mode client (optional):
   ~~~
   cl /EHsc client/spoofer_client.cpp /Fe:client/spoofer_client.exe
   ~~~

## Usage

1. Load the driver with your favourite mapper (`kdmapper`, the included manual mapper, etc.).
2. Run `client/spoofer_client.exe` to configure serials and enable spoofing.
3. Launch your game; hardware values are now virtualized.

## UEFI Bootkit (Optional)

1. Install EDK2 and set up the environment.
2. Go to the `uefi/` folder and run:
   ~~~
   cmake -B build -G "Visual Studio 17 2022"
   cmake --build build
   ~~~
3. Copy `build/bootkit_agent.efi` to a USB drive and boot from it.
4. The agent patches `bootmgfw.efi` to load the driver before Windows starts.

## Automated Testing

Run the Python lab script (requires admin):
~~~
python tools/test_lab.py
~~~

## Modules

| Module               | Purpose |
|----------------------|---------|
| `hypervisor`         | Intel VT‑x VMX root + EPT management |
| `svm`                | AMD SVM root + NPT management |
| `disk_spoofer`       | IRP hook on `\Driver\Disk` (serial) |
| `gpu_spoofer`        | PCI scan → EPT/NPT hide GPU MMIO |
| `mac_spoofer`        | PCI scan → EPT/NPT hide NIC MMIO |
| `smbios_spoofer`     | EPT/NPT hide SMBIOS tables + RAM SPD |
| `tpm_spoofer`        | EPT/NPT hide TPM MMIO |
| `registry_spoofer`   | `NtQueryValueKey` hook |
| `volume_spoofer`     | `NtQueryVolumeInformationFile` hook |
| `protection`         | DKOM, anti‑read, module hiding, integrity, HVCI bypass |
| `mapper`             | Manual mapper (imports, relocs, `.pdata`) |
| `comm_channel`       | Encrypted shared‑memory communication |
| `dynamic_ept`        | Page rotation every 10 s |
| `adaptive_spoofer`   | AC detection and profile adjustment |
| `uefi/`              | UEFI bootkit agent & payload |
| `tools/`             | Automated test lab |

## Disclaimer

**This project is for educational and research purposes only.**  
Misuse may result in account bans, system instability or legal consequences. The authors assume no liability.

## Credits

Developed by **wasimodo947‑pixel** and **Jack**, with low‑level architecture by **Fox**.
