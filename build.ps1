# NexusKernelSpoofer Build Script
# Ejecutar desde la ra?z del proyecto

$ErrorActionPreference = "Stop"

Write-Host "[1/4] Clonando Zydis..." -ForegroundColor Cyan
if (-not (Test-Path libs/zydis)) {
    git clone https://github.com/zyantific/zydis.git libs/zydis
} else {
    Write-Host "Zydis ya existe, omitiendo descarga."
}

Write-Host "[2/4] Configurando CMake..." -ForegroundColor Cyan
cmake -B build -G "Visual Studio 17 2022"

Write-Host "[3/4] Compilando driver..." -ForegroundColor Cyan
cmake --build build --config Release

Write-Host "[4/4] Compilando cliente user-mode..." -ForegroundColor Cyan
if (Test-Path client/spoofer_client.cpp) {
    cl /EHsc client/spoofer_client.cpp /Fe:client/spoofer_client.exe
} else {
    Write-Host "Cliente no encontrado, omitiendo."
}

Write-Host "`nBuild completado." -ForegroundColor Green
Write-Host "Driver: build/Release/NexusKernelSpoofer.sys"
Write-Host "Cliente: client/spoofer_client.exe"
