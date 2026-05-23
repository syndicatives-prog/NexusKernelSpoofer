# NexusKernelSpoofer

**Suplantador de hardware de kernel avanzado basado en hipervisor para Windows 11**

Controlador de kernel ultraprofesional diseñado para la suplantación completa de hardware contra EAC y otros sistemas antitrampas.

## Características

- Suplantación completa de hardware (disco, SMBIOS, MAC, CPUID, GPU, RAM, placa base, HWID, TPM)
- Capa de hipervisor (VT-x / AMD-V) para máxima discreción.
- Motor polimórfico + aleatorización en tiempo de ejecución
- Optimizado contra Easy Anti-Cheat (EAC) y otros sistemas antitrampas a nivel de kernel.
- Soporte para Windows 11 24H2 / 25H2

## Estructura del Proyecto

- `driver/src/NexusKernelSpoofer.c`: Código principal del controlador con todos los módulos integrados (DriverEntry, hooks, spoofers, protección).
- `config/`: Configuración del spoofer.
- `documentos/`: Documentación adicional.
- `conductor/`: Archivos del conductor (driver).

## Advertencia

Uso solo para fines educativos y de investigación. No usar en juegos online ni para violar términos de servicio de plataformas de gaming.

**Este es un punto de partida sólido para investigación en seguridad de kernel y anti-cheat. Prueba siempre en entornos controlados (VMs).**