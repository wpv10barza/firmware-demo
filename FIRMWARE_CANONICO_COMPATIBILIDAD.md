# Compatibilidad del firmware ESP32-S3-4848S040 (21/oct/2026)

Esta es una línea histórica **independiente**. No se sustituyen sus fuentes
de firmware ni sus backends: el firmware oficial de la arquitectura
Databricks-only usa diferentes dependencias, endpoints y esquema OTA.
La referencia nueva es:

- Firmware canónico: https://github.com/wpv10barza/erp-mantto-esp32/tree/main
- Versión verificada al revisar: commit `b8ebce37d0a2a6a0eed4243fe6b6caddf650543f`.
- Integración de voz: https://github.com/wpv10barza/voice-embeding-in-databricks-for-esp32
- Backend cloud protegido: https://github.com/wpv10barza/asistente-de-databricks--erp/pull/8
- Backend local WSL sin Databricks: https://github.com/wpv10barza/asistente-3c

En PowerShell, desde este repositorio histórico:

```powershell
.\ACTUALIZAR_FIRMWARE_CANONICO_3C.ps1
# Solo si tiene PlatformIO y necesita una compilación aislada:
.\ACTUALIZAR_FIRMWARE_CANONICO_3C.ps1 -Build
```

El comando clona o actualiza mediante Git fast-forward el **firmware nuevo**
en `$HOME\erp-mantto-esp32-deploy`, sin sobrescribir código antiguo.
Conserva en el historial el SHA exacto verificado al ejecutar. El script
no habilita Google Sheets ni cambia Databricks. Ningún repo antiguo
queda automáticamente convertido al backend cloud.

El flasheo, **únicamente si la placa física es un S3-4848S040**, sigue
el flujo de `erp-mantto-esp32-deploy/scripts/Flash-ESP32-3C.ps1`;
se necesita detectar el puerto USB COM y conservar `local_config.h`
privado. No usar `erase`, no pasar los binarios a placas Waveshare o C3.
GitHub Actions no equivale a un flash físico.

Para implementar escritura en Sheets desde el backend local Express
de forma independiente, use la versión corregida de `asistente-3c`;
requiere autorización humana y lectura posterior de las celdas. No
intercambie el protocolo de comandos local con Databricks sin adaptar
explícitamente ambos extremos y ejecutar pruebas E2E.

Ningún cambio a la hoja se afirma sin aprobación y readback observado.

**Perfil de este repositorio:** Independent OTA bootstrap prototype for 4848S040 + Databricks; do not replace OTA behavior or manifests.
