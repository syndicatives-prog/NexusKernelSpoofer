cmake_minimum_required(VERSION 3.10)
project(UefiBootkit)

set(SOURCES
    bootkit_agent.c
    bootkit_payload.c
)

add_executable(bootkit_agent.efi ${SOURCES})
target_link_libraries(bootkit_agent.efi Uefi)
