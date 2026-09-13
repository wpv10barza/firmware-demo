# ESP32-S3-4848S040 Firmware + Backend Bridge

Firmware reproducible para el panel cuadrado **ESP32-S3-4848S040 (480×480)** conectado al backend 3C que se ejecuta en Ubuntu/WSL.

## Alcance verificable

- compila firmware para ESP32-S3 N16R8 con pantalla ST7701 y táctil GT911;
- consume el contrato HTTP `/api/device/v1/health`, `POST /api/device/v1/commands` y `GET /api/device/v1/commands/{command_id}`;
- muestra `pendiente`, `aplicado`, `rechazado` y `error` en la pantalla;
- conserva la confirmación humana antes de modificar Google Sheets;
- publica binarios y manifiesto SHA-256 desde GitHub Actions;
- despliega un sitio de estado documental mediante GitHub Pages.

GitHub Actions demuestra **compilación**, no carga ni validación física. La prueba física requiere el panel conectado por USB.

## Uso directo en Ubuntu/WSL

```bash
git clone https://github.com/wpv10barza/firmware-demo.git
cd firmware-demo
cp include/local_config.example.h include/local_config.h
nano include/local_config.h
python3 -m pip install platformio==6.1.18
pio run -e panel_4848s040
pio run -e panel_4848s040 -t upload
pio device monitor -b 115200
```

En `local_config.h`, use la **IPv4 LAN de Windows** para el backend. Un ESP32 físico no puede acceder a `127.0.0.1` de WSL.

> Seguridad: no versionar Wi-Fi, token, credenciales de Google ni identificadores privados. El ESP32 nunca escribe directamente en Sheets; solicita una vista previa al backend.
