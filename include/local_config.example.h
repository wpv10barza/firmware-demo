#pragma once

// Copy to include/local_config.h. This file is ignored by Git.
// The ESP32 only needs Internet access; it does NOT need the same LAN/Wi-Fi as a PC.
#define WIFI_SSID_VALUE "YOUR_INTERNET_WIFI_2_4_GHZ"
#define WIFI_PASSWORD_VALUE "YOUR_WIFI_PASSWORD"

// Databricks Apps production endpoint and workspace OAuth endpoint.
#define ASSISTANT_BASE_URL_VALUE "https://asistente-cloud-erp-7474651957738908.aws.databricksapps.com"
#define DATABRICKS_WORKSPACE_URL_VALUE "https://dbc-a1aca8aa-28bd.cloud.databricks.com"

// Databricks service principal OAuth M2M credentials.
// Never commit real values.
#define DATABRICKS_CLIENT_ID_VALUE "YOUR_DATABRICKS_SERVICE_PRINCIPAL_CLIENT_ID"
#define DATABRICKS_CLIENT_SECRET_VALUE "YOUR_DATABRICKS_SERVICE_PRINCIPAL_OAUTH_SECRET"

// Second, application-level device credential used by the 3C Device API.
#define ESP32_API_TOKEN_VALUE "YOUR_ESP32_DEVICE_TOKEN"

// Optional hardware identity override.
// #define DEVICE_ID_VALUE "panel-4848s040-3c-ota-01"

#define DEFAULT_3C_COMMAND_VALUE "Cambia la tarea J10 a mensual"
#define PANEL_AUDIO_ENABLED_VALUE 1
#define PANEL_BRIGHTNESS_VALUE 180

// Automatic OTA checks are enabled by default. Set to 0 to require manual check.
#define OTA_AUTO_UPDATE_VALUE 1
