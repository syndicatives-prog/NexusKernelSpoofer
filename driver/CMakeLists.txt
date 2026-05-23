cmake_minimum_required(VERSION 3.20)
project(NexusSpoofer)

set(CMAKE_CXX_STANDARD 20)

add_library(NexusSpoofer MODULE
  src/main.cpp
  src/core/hypervisor.cpp
  src/core/polymorphic_engine.cpp
  src/spoofers/disk_spoofer.cpp
  src/spoofers/smbios_spoofer.cpp
  src/spoofers/mac_spoofer.cpp
  src/spoofers/cpuid_spoofer.cpp
  src/spoofers/gpu_spoofer.cpp
  src/spoofers/ram_spoofer.cpp
  src/spoofers/motherboard_spoofer.cpp
  src/spoofers/hwid_spoofer.cpp
  src/utils/anti_detection.cpp
  src/utils/cleaner.cpp
  src/utils/logger.cpp
)

target_include_directories(NexusSpoofer PRIVATE include)

target_link_libraries(NexusSpoofer PRIVATE ntifs wdmguid)