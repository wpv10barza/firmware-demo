#pragma once

// Copy this file to include/local_config.h and edit only the values.
// include/local_config.h is ignored by Git and must never be committed.
#define WIFI_SSID_VALUE "YOUR_WIFI_2_4_GHZ"
#define WIFI_PASSWORD_VALUE "YOUR_WIFI_PASSWORD"
#define ASSISTANT_BASE_URL_VALUE "http://192.168.1.50:3000"
#define ESP32_API_TOKEN_VALUE "USE_THE_SAME_LONG_RANDOM_TOKEN_AS_WSL"
// Optional. If omitted, each PlatformIO target uses its own hardware ID.
// #define DEVICE_ID_VALUE "panel-4848s040-3c-01"
#define DEFAULT_3C_COMMAND_VALUE "Cambia la tarea J10 a mensual"

// ESP32-4848S040 audio version: 1. Relay version: 0 because GPIO 1/2/40
// are shared by the relay/audio population options.
#define PANEL_AUDIO_ENABLED_VALUE 1
#define PANEL_BRIGHTNESS_VALUE 180
