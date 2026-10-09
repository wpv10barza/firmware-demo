# Firmware Demo — ESP32-S3-4848S040 OTA + Databricks

Firmware de demostración para el panel físico **ESP32-S3-4848S040 (480×480)** con actualización **OTA por HTTPS** coordinada por Databricks Apps.

## Arquitectura

```text
GitHub
  │
  ├─ compila firmware.bin
  ├─ calcula SHA-256 + tamaño + versión
  └─ genera latest.json
        │
        ▼
Unity Catalog Volume
  /stable/latest.json
  /stable/<version>/firmware.bin
        │
        ▼
Databricks App: asistente-cloud-erp
  GET /api/device/v1/firmware/latest
  GET /api/device/v1/firmware/<version>.bin
        │ HTTPS + OAuth M2M + X-3C-Device-Token
        ▼
ESP32-S3-4848S040
  consulta versión
  descarga firmware
  verifica tamaño + SHA-256
  escribe slot OTA inactivo
  cambia partición de arranque
  reinicia
  confirma boot saludable / rollback
```

El ESP32 solo necesita **salida a Internet**. No depende de WSL, Docker, IP privada, mDNS, loopback ni de estar conectado a la misma Wi-Fi que una PC.

## Por qué requiere un último flasheo USB

El firmware instalado actualmente en un equipo que todavía no tiene OTA necesita una última instalación física que incluya:

1. una tabla de particiones con `otadata`, `ota_0` y `ota_1`;
2. este firmware OTA bootstrap.

Después de ese bootstrap, las versiones siguientes pueden instalarse desde Internet.

## Seguridad OTA

La actualización se acepta solo si:

- la API de Databricks autoriza al dispositivo;
- la descarga responde por HTTPS;
- el manifiesto contiene versión, tamaño y SHA-256 válidos;
- el tamaño recibido coincide;
- el SHA-256 calculado por el ESP32 coincide exactamente antes de llamar `Update.end(true)`.

El access token OAuth de Databricks vive solo en RAM. Las credenciales privadas se guardan únicamente en `include/local_config.h`, que está excluido de Git.

## Particiones OTA

`partitions_ota_16mb.csv` reserva:

- `otadata`: 8 KiB;
- `ota_0`: 4 MiB;
- `ota_1`: 4 MiB;
- espacio restante para SPIFFS.

La actualización se escribe en el slot inactivo. El firmware llama a `esp_ota_mark_app_valid_cancel_rollback()` después de un arranque básico saludable; si una imagen nueva no logra iniciar y confirmar su estado, el mecanismo de rollback del bootloader puede volver a la imagen anterior cuando está habilitado por la plataforma.

## Configuración del dispositivo

Copie:

```bash
cp include/local_config.example.h include/local_config.h
```

Complete solamente los valores privados:

```cpp
#define WIFI_SSID_VALUE "..."
#define WIFI_PASSWORD_VALUE "..."
#define DATABRICKS_CLIENT_ID_VALUE "..."
#define DATABRICKS_CLIENT_SECRET_VALUE "..."
#define ESP32_API_TOKEN_VALUE "..."
```

Las URLs cloud ya están preparadas para `asistente-cloud-erp`.

## Prueba inicial de cambio a 6 meses

El firmware arranca con una orden de prueba preconfigurada para el registro indicado:

```text
Cambiar la frecuencia de la tarea 102497 del equipo 99336 a 6 meses.
```

La orden debe viajar ESP32 → Databricks → propuesta/revisión humana. No debe escribir Google Sheets directamente desde el panel.

## Compilar el bootstrap

```bash
python -m pip install platformio==6.2.0
pio run -e panel_4848s040
```

Para la primera instalación OTA-capable, flashee por USB el bootloader, tabla de particiones y firmware compilados. A partir de ahí, publique una versión superior, por ejemplo `1.0.1`.

## Versionado

El firmware tiene fallback `1.0.0`. GitHub Actions genera `include/firmware_version.generated.h` durante el build. Para una publicación OTA manual, indique una versión semántica como:

```text
1.0.1
1.1.0
2.0.0
```

El ESP32 solo instala una versión numéricamente superior a la que ya está ejecutando.

## Publicar en Databricks Volume

El workflow `firmware-ota.yml` puede publicar manualmente el artefacto a un Unity Catalog Volume usando GitHub OIDC.

Configure en GitHub Environment `prod`:

```text
DATABRICKS_HOST
DATABRICKS_CLIENT_ID
OTA_VOLUME_PATH=/Volumes/<catalog>/<schema>/<volume>
```

Luego ejecute **Actions → Firmware OTA → Run workflow**, coloque la nueva versión y active `publish_to_databricks`.

El workflow escribe primero:

```text
<volume>/stable/<version>/firmware.bin
<volume>/stable/<version>/manifest.json
```

y actualiza `<volume>/stable/latest.json` al final, para no anunciar una versión cuyo binario todavía no exista.

## Databricks App

La app `asistente-cloud-erp` debe tener un recurso **UC volume** con permiso mínimo de lectura y exponer su ruta al backend. El código compañero usa:

```text
OTA_VOLUME_PATH
OTA_CHANNEL=stable
```

El firmware consulta:

```http
GET /api/device/v1/firmware/latest
```

Respuesta esperada:

```json
{
  "version": "1.0.1",
  "sha256": "<64 hex>",
  "size": 1234567,
  "url": "/api/device/v1/firmware/1.0.1.bin"
}
```

## Evidencia

GitHub Actions verifica compilación, particiones y contrato OTA. Eso no sustituye la prueba física. El primer bootstrap debe comprobarse con el panel real conectado por USB; las actualizaciones posteriores sí pueden validarse por Internet.
