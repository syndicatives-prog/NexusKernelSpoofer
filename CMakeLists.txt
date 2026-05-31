cmake_minimum_required(VERSION 3.10)
project(NexusKernelSpoofer)

set(SOURCES
    driver/src/core/main.cpp
    driver/src/core/hooks.cpp
    driver/src/core/hypervisor.cpp
    driver/src/core/hypervisor_asm.asm
    driver/src/core/vmcs_init.cpp
    driver/src/core/svm.cpp
    driver/src/core/smp_vmx.cpp
    driver/src/core/smp_svm.cpp
    driver/src/core/comm_channel.cpp
    driver/src/core/dynamic_ept.cpp
    driver/src/core/adaptive_spoofer.cpp

    driver/src/spoofers/disk_spoofer.cpp
    driver/src/spoofers/volume_spoofer.cpp
    driver/src/spoofers/registry_spoofer.cpp
    driver/src/spoofers/mac_spoofer.cpp
    driver/src/spoofers/smbios_spoofer.cpp
    driver/src/spoofers/gpu_spoofer.cpp
    driver/src/spoofers/tpm_spoofer.cpp

    driver/src/protection/hiding.cpp
    driver/src/protection/anti_read.cpp
    driver/src/protection/module_hiding.cpp
    driver/src/protection/integrity.cpp
    driver/src/protection/hvci_bypass.cpp

    driver/src/mapper/manual_mapper.cpp
)

add_library(zydis STATIC libs/zydis/src/Zydis.c)
target_include_directories(zydis PUBLIC libs/zydis/include)

add_driver(NexusKernelSpoofer ${SOURCES})
target_link_libraries(NexusKernelSpoofer ntoskrnl hal zydis)
target_include_directories(NexusKernelSpoofer PRIVATE
    driver/src/core
    driver/src/spoofers
    driver/src/protection
    driver/src/mapper
    libs/zydis/include
)
