#pragma once

#if __has_include("local_config.h")
#include "local_config.h"
#endif
#if __has_include("firmware_version.generated.h")
#include "firmware_version.generated.h"
#endif

#ifndef WIFI_SSID_VALUE
#define WIFI_SSID_VALUE ""
#endif
#ifndef WIFI_PASSWORD_VALUE
#define WIFI_PASSWORD_VALUE ""
#endif

// Production Databricks endpoints. URLs are not secrets.
// The firmware never discovers or stores a LAN backend IP.
#ifndef ASSISTANT_BASE_URL_VALUE
#define ASSISTANT_BASE_URL_VALUE "https://asistente-cloud-erp-7474651957738908.aws.databricksapps.com"
#endif
#ifndef DATABRICKS_WORKSPACE_URL_VALUE
#define DATABRICKS_WORKSPACE_URL_VALUE "https://dbc-a1aca8aa-28bd.cloud.databricks.com"
#endif
#ifndef DATABRICKS_CLIENT_ID_VALUE
#define DATABRICKS_CLIENT_ID_VALUE ""
#endif
#ifndef DATABRICKS_CLIENT_SECRET_VALUE
#define DATABRICKS_CLIENT_SECRET_VALUE ""
#endif
#ifndef DATABRICKS_OAUTH_SCOPE_VALUE
#define DATABRICKS_OAUTH_SCOPE_VALUE "all-apis"
#endif
#ifndef ESP32_API_TOKEN_VALUE
#define ESP32_API_TOKEN_VALUE ""
#endif

#ifndef DEVICE_ID_VALUE
#if defined(BOARD_PANEL_4848S040)
#define DEVICE_ID_VALUE "panel-4848s040-3c-01"
#else
#define DEVICE_ID_VALUE "esp-hi-3c-01"
#endif
#endif

#ifndef DEFAULT_3C_COMMAND_VALUE
#define DEFAULT_3C_COMMAND_VALUE "Cambia la tarea J10 a mensual"
#endif
#ifndef PANEL_AUDIO_ENABLED_VALUE
#define PANEL_AUDIO_ENABLED_VALUE 1
#endif
#ifndef PANEL_BRIGHTNESS_VALUE
#define PANEL_BRIGHTNESS_VALUE 180
#endif

#ifndef FIRMWARE_VERSION_VALUE
#define FIRMWARE_VERSION_VALUE "1.0.0"
#endif
#ifndef OTA_AUTO_UPDATE_VALUE
#define OTA_AUTO_UPDATE_VALUE 1
#endif

namespace app_config {
static constexpr char wifiSsid[] = WIFI_SSID_VALUE;
static constexpr char wifiPassword[] = WIFI_PASSWORD_VALUE;
static constexpr char assistantBaseUrl[] = ASSISTANT_BASE_URL_VALUE;
static constexpr char databricksWorkspaceUrl[] = DATABRICKS_WORKSPACE_URL_VALUE;
static constexpr char databricksClientId[] = DATABRICKS_CLIENT_ID_VALUE;
static constexpr char databricksClientSecret[] = DATABRICKS_CLIENT_SECRET_VALUE;
static constexpr char databricksOauthScope[] = DATABRICKS_OAUTH_SCOPE_VALUE;
static constexpr char apiToken[] = ESP32_API_TOKEN_VALUE;
static constexpr char deviceId[] = DEVICE_ID_VALUE;
static String commandBuffer = DEFAULT_3C_COMMAND_VALUE;
static String& defaultCommand = commandBuffer;
static constexpr bool panelAudioEnabled = PANEL_AUDIO_ENABLED_VALUE != 0;
static constexpr uint8_t panelBrightness = PANEL_BRIGHTNESS_VALUE;
static constexpr char firmwareVersion[] = FIRMWARE_VERSION_VALUE;
static constexpr bool otaAutoUpdate = OTA_AUTO_UPDATE_VALUE != 0;
static constexpr unsigned long wifiRetryMs = 10000UL;
static constexpr unsigned long healthCheckMs = 30000UL;
static constexpr unsigned long commandPollMs = 2500UL;
static constexpr unsigned long httpTimeoutMs = 12000UL;
static constexpr unsigned long oauthRefreshSkewMs = 300000UL;
static constexpr unsigned long otaCheckMs = 6UL * 60UL * 60UL * 1000UL;
static constexpr unsigned long otaHttpTimeoutMs = 30000UL;
static constexpr unsigned long otaStreamTimeoutMs = 30000UL;
static constexpr unsigned long otaBootConfirmDelayMs = 15000UL;
}
