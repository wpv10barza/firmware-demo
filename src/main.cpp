#if defined(BOARD_PANEL_4848S040)

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <HTTPClient.h>
#include <Update.h>
#include <mbedtls/sha256.h>
#include <esp_ota_ops.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <driver/i2s.h>
#include <esp_system.h>

#include "app_config.h"
#include "command_buffer.h"
#include "command_text_viewport.h"
#include "virtual_keyboard.h"

namespace pins {
constexpr int backlight = 38;
constexpr int lcdCs = 39;
constexpr int lcdClock = 48;
constexpr int lcdMosi = 47;
constexpr int touchSda = 19;
constexpr int touchScl = 45;
constexpr int audioBclk = 1;
constexpr int audioLrclk = 2;
constexpr int audioData = 40;
}  // namespace pins

namespace {
constexpr uint8_t kTouchAddress = 0x5D;
constexpr uint16_t kTouchStatusRegister = 0x814E;
constexpr uint16_t kTouchPointRegister = 0x814F;
constexpr int kScreenWidth = 480;
constexpr int kScreenHeight = 480;

WebServer web(80);
Arduino_ESP32SPI* displayBus = nullptr;
Arduino_RGB_Display* display = nullptr;

enum class PanelState {
  Booting,
  Offline,
  Ready,
  Busy,
  Pending,
  Applied,
  Rejected,
  Error,
};

PanelState panelState = PanelState::Booting;
String panelDetail = "Iniciando";
String lastBackendMessage = "Sin verificar";
String lastCommandId;
String databricksAccessToken;
unsigned long databricksTokenAcquiredMs = 0;
unsigned long databricksTokenLifetimeMs = 0;
unsigned long lastWifiAttempt = 0;
unsigned long lastHealthCheck = 0;
unsigned long lastCommandPoll = 0;
unsigned long lastOtaCheck = 0;
String lastOtaMessage = "OTA sin verificar";
bool otaBootConfirmed = false;
bool backendAvailable = false;
bool displayReady = false;
bool audioReady = false;
bool mdnsReady = false;
bool wifiAnnounced = false;

bool touchDown = false;
constexpr size_t kCommandCapacity = 240;
CommandBuffer<kCommandCapacity> commandBuffer;
virtual_keyboard::KeyboardMode keyboardMode = virtual_keyboard::KeyboardMode::Alpha;
bool commandEditorOpen = false;

struct TouchSample {
  bool ready = false;
  bool touched = false;
  uint16_t rawX = 0;
  uint16_t rawY = 0;
  uint16_t x = 0;
  uint16_t y = 0;
};

uint16_t color565(uint8_t red, uint8_t green, uint8_t blue) {
  return display ? display->color565(red, green, blue) : 0;
}

const char* stateLabel(PanelState state) {
  switch (state) {
    case PanelState::Booting: return "INICIANDO";
    case PanelState::Offline: return "SIN CONEXION";
    case PanelState::Ready: return "DATABRICKS LISTO";
    case PanelState::Busy: return "PROCESANDO";
    case PanelState::Pending: return "PENDIENTE";
    case PanelState::Applied: return "APLICADO";
    case PanelState::Rejected: return "RECHAZADO";
    case PanelState::Error: return "ERROR";
  }
  return "3C";
}

uint16_t stateBackground(PanelState state) {
  switch (state) {
    case PanelState::Ready: return color565(5, 45, 27);
    case PanelState::Busy: return color565(8, 28, 58);
    case PanelState::Pending: return color565(68, 43, 2);
    case PanelState::Applied: return color565(2, 65, 28);
    case PanelState::Rejected: return color565(62, 29, 3);
    case PanelState::Error: return color565(65, 5, 9);
    case PanelState::Offline: return color565(18, 22, 30);
    case PanelState::Booting: return color565(10, 18, 38);
  }
  return 0;
}

void drawCentered(const String& text, int y, uint8_t size, uint16_t color) {
  if (!displayReady) return;
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  display->setTextSize(size);
  display->getTextBounds(text, 0, y, &x1, &y1, &width, &height);
  int x = (kScreenWidth - static_cast<int>(width)) / 2;
  if (x < 4) x = 4;
  display->setTextColor(color);
  display->setCursor(x, y);
  display->print(text);
}

void drawButton(int x, int y, int width, int height, const char* label, uint16_t fill) {
  if (!displayReady) return;
  display->fillRoundRect(x, y, width, height, 16, fill);
  display->drawRoundRect(x, y, width, height, 16, color565(185, 210, 230));
  display->setTextSize(2);
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t textWidth = 0;
  uint16_t textHeight = 0;
  display->getTextBounds(label, 0, 0, &x1, &y1, &textWidth, &textHeight);
  display->setTextColor(WHITE);
  display->setCursor(x + (width - textWidth) / 2, y + (height - textHeight) / 2);
  display->print(label);
}

void drawEditor() {
  if (!displayReady) return;
  display->fillScreen(color565(8, 18, 30));
  drawCentered("EDITAR ORDEN 3C", 10, 2, color565(170, 220, 255));
  display->drawRect(8, 42, 464, 72, color565(185, 210, 230));
  display->setTextSize(2);
  display->setTextColor(WHITE);

  uint16_t prefixWidths[kCommandCapacity + 1] = {};
  String full(commandBuffer.c_str());
  for (size_t i = 0; i < full.length() && i < kCommandCapacity; ++i) {
    int16_t x1 = 0, y1 = 0; uint16_t w = 0, h = 0;
    display->getTextBounds(full.substring(0, i + 1), 0, 0, &x1, &y1, &w, &h);
    prefixWidths[i + 1] = w;
  }
  const auto window = command_text_viewport::compute(
      prefixWidths, commandBuffer.length(), commandBuffer.cursor(), 450, 3);
  const String visible = full.substring(window.first, window.last);
  display->setCursor(15, 68);
  display->print(visible);
  const int cursorX = 15 + window.cursorX;
  display->drawFastVLine(cursorX, 57, 28, color565(80, 220, 160));

  virtual_keyboard::Key keys[50]{};
  const size_t count = virtual_keyboard::buildKeys(keyboardMode, keys, 50);
  for (size_t i = 0; i < count; ++i) {
    const auto& key = keys[i];
    const auto fill = key.definition.kind == virtual_keyboard::KeyKind::Enter
        ? color565(18, 105, 73) : color565(25, 45, 65);
    display->fillRoundRect(key.rect.left, key.rect.top, key.rect.right - key.rect.left,
                           key.rect.bottom - key.rect.top, 7, fill);
    display->drawRoundRect(key.rect.left, key.rect.top, key.rect.right - key.rect.left,
                           key.rect.bottom - key.rect.top, 7, color565(130, 160, 180));
    display->setTextSize(key.definition.label[0] && strlen(key.definition.label) > 2 ? 1 : 2);
    int16_t x1 = 0, y1 = 0; uint16_t w = 0, h = 0;
    display->getTextBounds(key.definition.label, 0, 0, &x1, &y1, &w, &h);
    display->setTextColor(WHITE);
    display->setCursor(key.rect.left + ((key.rect.right-key.rect.left)-w)/2,
                       key.rect.top + ((key.rect.bottom-key.rect.top)-h)/2);
    display->print(key.definition.label);
  }
  drawButton(8, 172, 100, 36, "CANCELAR", color565(80, 35, 35));
  drawButton(112, 172, 72, 36, "<", color565(42, 67, 90));
  drawButton(192, 172, 72, 36, "DEL", color565(105, 72, 40));
  drawButton(272, 172, 115, 36,
             keyboardMode == virtual_keyboard::KeyboardMode::Alpha ? "123" : "ABC",
             color565(45, 70, 100));
}

void drawPanel() {
  if (commandEditorOpen) { drawEditor(); return; }
  if (!displayReady) return;
  const uint16_t background = stateBackground(panelState);
  const uint16_t eye = panelState == PanelState::Offline ? color565(125, 135, 145) : WHITE;
  display->fillScreen(background);
  drawCentered("Interfaz Portátil", 18, 2, color565(170, 220, 255));

  if (panelState == PanelState::Error || panelState == PanelState::Rejected) {
    display->drawLine(112, 105, 172, 165, eye);
    display->drawLine(172, 105, 112, 165, eye);
    display->drawLine(308, 105, 368, 165, eye);
    display->drawLine(368, 105, 308, 165, eye);
  } else if (panelState == PanelState::Applied) {
    display->fillRoundRect(105, 102, 75, 76, 22, eye);
    display->fillRoundRect(300, 102, 75, 76, 22, eye);
    display->fillCircle(143, 141, 13, background);
    display->fillCircle(338, 141, 13, background);
    display->drawLine(205, 205, 225, 218, eye);
    display->drawLine(225, 218, 255, 218, eye);
    display->drawLine(255, 218, 275, 205, eye);
  } else {
    display->fillRoundRect(105, 102, 75, 76, 22, eye);
    display->fillRoundRect(300, 102, 75, 76, 22, eye);
    display->fillCircle(143, 141, 13, background);
    display->fillCircle(338, 141, 13, background);
  }

  drawCentered(stateLabel(panelState), 250, 2, WHITE);
  String detail = panelDetail;
  if (detail.length() > 52) detail = detail.substring(0, 49) + "...";
  drawCentered(detail, 286, 1, color565(210, 225, 235));
  if (WiFi.status() == WL_CONNECTED) {
    drawCentered("CLOUD HTTPS", 310, 1, color565(150, 205, 235));
  }
  drawCentered(String("FW ") + app_config::firmwareVersion, 332, 1, color565(150, 205, 235));

  drawButton(20, 370, 210, 82, "PROBAR CLOUD", color565(15, 82, 135));
  drawButton(250, 370, 210, 82, "ENVIAR 3C", color565(18, 105, 73));
}

void playTone(uint16_t frequency, uint16_t durationMs) {
  if (!audioReady || !app_config::panelAudioEnabled || frequency == 0) return;
  constexpr uint32_t sampleRate = 16000;
  constexpr size_t framesPerChunk = 128;
  int16_t samples[framesPerChunk * 2];
  const uint32_t totalFrames = sampleRate * durationMs / 1000;
  uint32_t frame = 0;
  while (frame < totalFrames) {
    size_t frames = totalFrames - frame;
    if (frames > framesPerChunk) frames = framesPerChunk;
    for (size_t index = 0; index < frames; ++index) {
      const uint32_t phase = ((frame + index) * frequency * 2U) / sampleRate;
      const int16_t sample = (phase & 1U) ? 2600 : -2600;
      samples[index * 2] = sample;
      samples[index * 2 + 1] = sample;
    }
    size_t written = 0;
    i2s_write(I2S_NUM_0, samples, frames * 2 * sizeof(int16_t), &written, portMAX_DELAY);
    frame += frames;
  }
  i2s_zero_dma_buffer(I2S_NUM_0);
}

void updatePanel(PanelState state, const String& detail, bool sound = false) {
  const bool changed = state != panelState;
  panelState = state;
  panelDetail = detail;
  drawPanel();
  Serial.printf("PANEL STATE -> %s | %s\n", stateLabel(panelState), panelDetail.c_str());
  if (!sound || !changed) return;
  if (state == PanelState::Applied || state == PanelState::Ready) playTone(880, 70);
  else if (state == PanelState::Pending || state == PanelState::Busy) playTone(620, 55);
  else if (state == PanelState::Rejected || state == PanelState::Error) playTone(220, 110);
}

String normalizedStatus(String status) {
  status.trim();
  status.toLowerCase();
  return status;
}

void setTransportError(const char* phase, int code, const String& detail, bool clearCommand) {
  backendAvailable = false;
  if (clearCommand) lastCommandId = "";
  const String message = String(phase) + " HTTP " + code;
  updatePanel(PanelState::Error, message, true);
  Serial.printf("[ERROR] transport phase=%s code=%d detail=%s\n", phase, code, detail.c_str());
}

void setProtocolError(const char* phase, const String& detail) {
  backendAvailable = false;
  lastCommandId = "";
  const String message = String(phase) + ": " + (detail.length() ? detail : "respuesta invalida");
  updatePanel(PanelState::Error, message, true);
  Serial.printf("[ERROR] protocol phase=%s detail=%s\n", phase, detail.c_str());
}

bool initializeAudio() {
  if (!app_config::panelAudioEnabled) {
    Serial.println("Audio deshabilitado: GPIO 1/2/40 reservados para relays.");
    return false;
  }
  i2s_config_t config = {};
  config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
  config.sample_rate = 16000;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = 4;
  config.dma_buf_len = 128;
  config.use_apll = false;
  config.tx_desc_auto_clear = true;
  config.fixed_mclk = 0;

  i2s_pin_config_t pinConfig = {};
  pinConfig.bck_io_num = pins::audioBclk;
  pinConfig.ws_io_num = pins::audioLrclk;
  pinConfig.data_out_num = pins::audioData;
  pinConfig.data_in_num = I2S_PIN_NO_CHANGE;
  if (i2s_driver_install(I2S_NUM_0, &config, 0, nullptr) != ESP_OK) return false;
  if (i2s_set_pin(I2S_NUM_0, &pinConfig) != ESP_OK) {
    i2s_driver_uninstall(I2S_NUM_0);
    return false;
  }
  i2s_zero_dma_buffer(I2S_NUM_0);
  return true;
}

#if !defined(PANEL_PRODUCTION_BUILD) || !PANEL_PRODUCTION_BUILD
void runDisplayDiagnostic() {
  if (!displayReady) return;
  Serial.println("DISPLAY DIAGNOSTIC: RED");
  display->fillScreen(color565(255, 0, 0));
  delay(400);
  Serial.println("DISPLAY DIAGNOSTIC: GREEN");
  display->fillScreen(color565(0, 255, 0));
  delay(400);
  Serial.println("DISPLAY DIAGNOSTIC: BLUE");
  display->fillScreen(color565(0, 0, 255));
  delay(400);
  Serial.println("DISPLAY DIAGNOSTIC: WHITE");
  display->fillScreen(color565(255, 255, 255));
  delay(400);
}
#endif

bool initializeDisplay() {
  Serial.println("DISPLAY: creating 9-bit SPI command bus");
  displayBus = new Arduino_ESP32SPI(
    GFX_NOT_DEFINED, pins::lcdCs, pins::lcdClock, pins::lcdMosi, GFX_NOT_DEFINED);
  Serial.println("DISPLAY: creating RGB panel 480x480");
  auto* rgbPanel = new Arduino_ESP32RGBPanel(
    18, 17, 16, 21,
    11, 12, 13, 14, 0,
    8, 20, 3, 46, 9, 10,
    4, 5, 6, 7, 15,
    1, 10, 8, 50,
    1, 10, 8, 20,
    0, 12000000, false, 0, 0, 0);
  // Match the Guition ESP32-4848S040 reference: ST7701 type9,
  // rotation 1, 12 MHz RGB PCLK, RGB565 big-endian disabled.
  Serial.println("DISPLAY: using Arduino-GFX ST7701 type9 init sequence");
  display = new Arduino_RGB_Display(
    kScreenWidth, kScreenHeight, rgbPanel, 1, true,
    displayBus, GFX_NOT_DEFINED,
    st7701_type9_init_operations, sizeof(st7701_type9_init_operations));
  Serial.println("DISPLAY: calling display->begin()");
  if (!display->begin()) {
    Serial.println("DISPLAY: display->begin() FAILED");
    return false;
  }
  Serial.println("DISPLAY: display->begin() OK");
  pinMode(pins::backlight, OUTPUT);
  analogWrite(pins::backlight, app_config::panelBrightness);
  Serial.printf("DISPLAY: backlight GPIO %d PWM=%u\n", pins::backlight, app_config::panelBrightness);
  display->displayOn();
  Serial.println("DISPLAY: displayOn() OK");
  displayReady = true;
#if !defined(PANEL_PRODUCTION_BUILD) || !PANEL_PRODUCTION_BUILD
  runDisplayDiagnostic();
#else
  Serial.println("DISPLAY: production build; startup RGB diagnostic disabled");
#endif
  drawPanel();
  Serial.println("DISPLAY: first UI frame drawn");
  return true;
}

bool i2cRead(uint16_t reg, uint8_t* data, size_t length) {
  Wire.beginTransmission(kTouchAddress);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(kTouchAddress, static_cast<uint8_t>(length)) != length) return false;
  for (size_t index = 0; index < length; ++index) data[index] = Wire.read();
  return true;
}

bool i2cWriteByte(uint16_t reg, uint8_t value) {
  Wire.beginTransmission(kTouchAddress);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg & 0xFF));
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

TouchSample readTouch() {
  TouchSample sample;
  uint8_t status = 0;
  if (!i2cRead(kTouchStatusRegister, &status, 1) || !(status & 0x80)) return sample;
  sample.ready = true;
  const uint8_t points = status & 0x0F;
  if (points > 0 && points <= 5) {
    uint8_t data[7] = {};
    if (i2cRead(kTouchPointRegister, data, sizeof(data))) {
      const uint16_t rawX = data[1] | (static_cast<uint16_t>(data[2]) << 8);
      const uint16_t rawY = data[3] | (static_cast<uint16_t>(data[4]) << 8);
      sample.rawX = rawX;
      sample.rawY = rawY;

      // The RGB display is mounted with Arduino-GFX rotation=1. GT911 reports
      // native panel coordinates, so the touch axes must follow the same
      // rotation before hit-testing the on-screen keyboard.
      int32_t mappedX = rawX;
      int32_t mappedY = rawY;
      if (app_config::touchSwapXy) {
        const int32_t temporary = mappedX;
        mappedX = mappedY;
        mappedY = temporary;
      }
      if (app_config::touchMirrorX) mappedX = (kScreenWidth - 1) - mappedX;
      if (app_config::touchMirrorY) mappedY = (kScreenHeight - 1) - mappedY;
      if (mappedX < 0) mappedX = 0;
      if (mappedY < 0) mappedY = 0;
      if (mappedX >= kScreenWidth) mappedX = kScreenWidth - 1;
      if (mappedY >= kScreenHeight) mappedY = kScreenHeight - 1;

      sample.x = static_cast<uint16_t>(mappedX);
      sample.y = static_cast<uint16_t>(mappedY);
      sample.touched = true;
    }
  }
  i2cWriteByte(kTouchStatusRegister, 0);
  return sample;
}

bool cloudEndpointConfigured() {
  return strlen(app_config::assistantBaseUrl) > 0;
}

bool databricksAppEndpoint() {
  if (!cloudEndpointConfigured()) return false;
  String base = app_config::assistantBaseUrl;
  base.toLowerCase();
  return base.indexOf(".databricksapps.com") >= 0;
}

String backendBaseUrl() {
  String base = app_config::assistantBaseUrl;
  base.trim();
  while (base.endsWith("/")) base.remove(base.length() - 1);
  return base;
}

String endpoint(const String& path) {
  const String base = backendBaseUrl();
  return base.length() ? base + path : String();
}

String jsonEscape(const String& input) {
  String output;
  output.reserve(input.length() + 16);
  for (size_t index = 0; index < input.length(); ++index) {
    const char value = input[index];
    if (value == '\\' || value == '"') { output += '\\'; output += value; }
    else if (value == '\n') output += "\\n";
    else if (static_cast<uint8_t>(value) >= 0x20) output += value;
  }
  return output;
}

unsigned long jsonUnsignedLongValue(
    const String& json, const char* key, unsigned long fallback) {
  const String token = String("\"") + key + "\"";
  int position = json.indexOf(token);
  if (position < 0) return fallback;
  position = json.indexOf(':', position + token.length());
  if (position < 0) return fallback;
  position++;
  while (position < static_cast<int>(json.length()) && isspace(json[position])) position++;
  String digits;
  while (position < static_cast<int>(json.length()) && isdigit(json[position])) digits += json[position++];
  return digits.length() ? static_cast<unsigned long>(digits.toInt()) : fallback;
}

String jsonStringValue(const String& json, const char* key) {
  const String token = String("\"") + key + "\"";
  int position = json.indexOf(token);
  if (position < 0) return "";
  position = json.indexOf(':', position + token.length());
  if (position < 0) return "";
  position++;
  while (position < static_cast<int>(json.length()) && isspace(json[position])) position++;
  if (position >= static_cast<int>(json.length()) || json[position] != '"') return "";
  position++;
  String value;
  while (position < static_cast<int>(json.length())) {
    const char current = json[position++];
    if (current == '"') break;
    if (current == '\\' && position < static_cast<int>(json.length())) value += json[position++];
    else value += current;
  }
  return value;
}

void clearDatabricksAccessToken() {
  databricksAccessToken = "";
  databricksTokenAcquiredMs = 0;
  databricksTokenLifetimeMs = 0;
}

bool databricksOAuthConfigured() {
  return strlen(app_config::databricksWorkspaceUrl) > 0 &&
         strlen(app_config::databricksClientId) > 0 &&
         strlen(app_config::databricksClientSecret) > 0;
}

bool ensureDatabricksAccessToken() {
  if (!databricksAppEndpoint()) return true;
  if (!databricksOAuthConfigured()) {
    lastBackendMessage = "Databricks OAuth M2M no configurado";
    return false;
  }
  if (databricksAccessToken.length() &&
      millis() - databricksTokenAcquiredMs < databricksTokenLifetimeMs) return true;

  String workspace = app_config::databricksWorkspaceUrl;
  workspace.trim();
  while (workspace.endsWith("/")) workspace.remove(workspace.length() - 1);
  const String tokenUrl = workspace + "/oidc/v1/token";

  HTTPClient authHttp;
  authHttp.setTimeout(app_config::httpTimeoutMs);
  if (!authHttp.begin(tokenUrl)) {
    lastBackendMessage = "No se pudo abrir OAuth Databricks";
    return false;
  }
  authHttp.setAuthorization(app_config::databricksClientId, app_config::databricksClientSecret);
  authHttp.addHeader("Content-Type", "application/x-www-form-urlencoded");
  const String form = String("grant_type=client_credentials&scope=") + app_config::databricksOauthScope;
  const int code = authHttp.POST(form);
  const String body = code > 0 ? authHttp.getString() : authHttp.errorToString(code);
  authHttp.end();

  if (code != 200) {
    clearDatabricksAccessToken();
    lastBackendMessage = String("OAuth Databricks HTTP ") + code;
    Serial.printf("[ERROR] OAuth Databricks HTTP=%d (respuesta omitida)\n", code);
    return false;
  }

  const String token = jsonStringValue(body, "access_token");
  const unsigned long expiresSeconds = jsonUnsignedLongValue(body, "expires_in", 3600UL);
  if (!token.length()) {
    clearDatabricksAccessToken();
    lastBackendMessage = "OAuth Databricks sin access_token";
    return false;
  }

  databricksAccessToken = token;
  databricksTokenAcquiredMs = millis();
  const unsigned long rawLifetimeMs = expiresSeconds * 1000UL;
  databricksTokenLifetimeMs =
      rawLifetimeMs > app_config::oauthRefreshSkewMs
          ? rawLifetimeMs - app_config::oauthRefreshSkewMs
          : rawLifetimeMs / 2UL;
  lastBackendMessage = "OAuth Databricks renovado";
  Serial.printf("DATABRICKS: OAuth M2M listo; expires_in=%lu s\n", expiresSeconds);
  return true;
}

void addRequestAuth(HTTPClient& http) {
  if (databricksAccessToken.length()) {
    http.addHeader("Authorization", String("Bearer ") + databricksAccessToken);
  }
  if (strlen(app_config::apiToken)) {
    http.addHeader("X-3C-Device-Token", app_config::apiToken);
  }
}


int compareSemanticVersion(const String& leftRaw, const String& rightRaw) {
  String left = leftRaw;
  String right = rightRaw;
  left.trim();
  right.trim();
  if (left.startsWith("v") || left.startsWith("V")) left.remove(0, 1);
  if (right.startsWith("v") || right.startsWith("V")) right.remove(0, 1);

  for (int part = 0; part < 3; ++part) {
    const int leftDot = left.indexOf('.');
    const int rightDot = right.indexOf('.');
    String leftPart = leftDot >= 0 ? left.substring(0, leftDot) : left;
    String rightPart = rightDot >= 0 ? right.substring(0, rightDot) : right;
    const int leftDash = leftPart.indexOf('-');
    const int rightDash = rightPart.indexOf('-');
    if (leftDash >= 0) leftPart = leftPart.substring(0, leftDash);
    if (rightDash >= 0) rightPart = rightPart.substring(0, rightDash);
    const long leftValue = leftPart.toInt();
    const long rightValue = rightPart.toInt();
    if (leftValue < rightValue) return -1;
    if (leftValue > rightValue) return 1;
    left = leftDot >= 0 ? left.substring(leftDot + 1) : "";
    right = rightDot >= 0 ? right.substring(rightDot + 1) : "";
  }
  return 0;
}

bool isSha256Hex(const String& value) {
  if (value.length() != 64) return false;
  for (size_t index = 0; index < value.length(); ++index) {
    const char c = value[index];
    if (!isxdigit(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

String sha256Hex(const unsigned char digest[32]) {
  static const char kHex[] = "0123456789abcdef";
  String output;
  output.reserve(64);
  for (size_t index = 0; index < 32; ++index) {
    output += kHex[(digest[index] >> 4) & 0x0F];
    output += kHex[digest[index] & 0x0F];
  }
  return output;
}

String absoluteFirmwareUrl(const String& candidate) {
  String url = candidate;
  url.trim();
  if (url.startsWith("https://")) return url;
  if (!url.startsWith("/")) return "";
  return backendBaseUrl() + url;
}

void abortOta(const String& detail) {
  Update.abort();
  lastOtaMessage = detail;
  updatePanel(PanelState::Error, detail, true);
  Serial.printf("[ERROR] OTA %s\n", detail.c_str());
}

bool performOtaUpdate(
    const String& version,
    const String& downloadUrl,
    const String& expectedSha256,
    size_t expectedSize) {
  if (!isSha256Hex(expectedSha256)) {
    lastOtaMessage = "OTA: SHA-256 invalido";
    updatePanel(PanelState::Error, lastOtaMessage, true);
    return false;
  }
  if (!ensureDatabricksAccessToken()) {
    lastOtaMessage = "OTA OAuth no disponible";
    updatePanel(PanelState::Error, lastOtaMessage, true);
    return false;
  }

  const String url = absoluteFirmwareUrl(downloadUrl);
  if (!url.length()) {
    lastOtaMessage = "OTA URL invalida";
    updatePanel(PanelState::Error, lastOtaMessage, true);
    return false;
  }

  updatePanel(PanelState::Busy, String("OTA ") + version + " descargando", true);

  HTTPClient http;
  http.setTimeout(app_config::otaHttpTimeoutMs);
  if (!http.begin(url)) {
    lastOtaMessage = "OTA no pudo abrir HTTPS";
    updatePanel(PanelState::Error, lastOtaMessage, true);
    return false;
  }
  addRequestAuth(http);
  const int code = http.GET();
  if (code != 200) {
    lastOtaMessage = String("OTA descarga HTTP ") + code;
    http.end();
    if (code == 401 && databricksAppEndpoint()) clearDatabricksAccessToken();
    updatePanel(PanelState::Error, lastOtaMessage, true);
    return false;
  }

  const int contentLength = http.getSize();
  if (expectedSize > 0 && contentLength > 0 &&
      static_cast<size_t>(contentLength) != expectedSize) {
    http.end();
    lastOtaMessage = "OTA tamano no coincide";
    updatePanel(PanelState::Error, lastOtaMessage, true);
    return false;
  }

  const size_t updateSize =
      contentLength > 0 ? static_cast<size_t>(contentLength) : UPDATE_SIZE_UNKNOWN;
  if (!Update.begin(updateSize, U_FLASH)) {
    http.end();
    lastOtaMessage = "OTA sin particion disponible";
    updatePanel(PanelState::Error, lastOtaMessage, true);
    Serial.printf("[ERROR] Update.begin: %s\n", Update.errorString());
    return false;
  }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  if (mbedtls_sha256_starts_ret(&sha, 0) != 0) {
    mbedtls_sha256_free(&sha);
    http.end();
    abortOta("OTA SHA init fallo");
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buffer[4096];
  size_t total = 0;
  unsigned long lastProgress = millis();
  bool streamOk = true;

  while (http.connected() &&
         (contentLength < 0 || total < static_cast<size_t>(contentLength))) {
    const size_t available = stream->available();
    if (!available) {
      if (millis() - lastProgress > app_config::otaStreamTimeoutMs) {
        streamOk = false;
        break;
      }
      delay(5);
      continue;
    }

    const size_t chunk = available < sizeof(buffer) ? available : sizeof(buffer);
    const int read = stream->readBytes(buffer, chunk);
    if (read <= 0) {
      streamOk = false;
      break;
    }
    lastProgress = millis();

    if (mbedtls_sha256_update_ret(&sha, buffer, static_cast<size_t>(read)) != 0) {
      streamOk = false;
      break;
    }
    const size_t written = Update.write(buffer, static_cast<size_t>(read));
    if (written != static_cast<size_t>(read)) {
      streamOk = false;
      break;
    }
    total += written;
  }

  unsigned char digest[32] = {};
  const int shaResult = mbedtls_sha256_finish_ret(&sha, digest);
  mbedtls_sha256_free(&sha);
  http.end();

  if (!streamOk || shaResult != 0) {
    abortOta("OTA descarga incompleta");
    return false;
  }
  if (contentLength > 0 && total != static_cast<size_t>(contentLength)) {
    abortOta("OTA bytes incompletos");
    return false;
  }
  if (expectedSize > 0 && total != expectedSize) {
    abortOta("OTA tamano verificado fallo");
    return false;
  }

  String actualSha = sha256Hex(digest);
  String expectedSha = expectedSha256;
  expectedSha.toLowerCase();
  if (actualSha != expectedSha) {
    Serial.printf("[ERROR] OTA SHA mismatch expected=%s actual=%s\n",
                  expectedSha.c_str(), actualSha.c_str());
    abortOta("OTA SHA-256 no coincide");
    return false;
  }

  if (!Update.end(true)) {
    Serial.printf("[ERROR] Update.end: %s\n", Update.errorString());
    abortOta("OTA no pudo finalizar");
    return false;
  }

  lastOtaMessage = String("OTA ") + version + " verificada";
  updatePanel(PanelState::Busy, "OTA OK; reiniciando", true);
  Serial.printf("OTA SUCCESS version=%s bytes=%u sha256=%s\n",
                version.c_str(), static_cast<unsigned>(total), actualSha.c_str());
  delay(1200);
  ESP.restart();
  return true;
}

bool checkForOtaUpdate(bool manual) {
  if (WiFi.status() != WL_CONNECTED) {
    lastOtaMessage = "OTA sin Internet";
    if (manual) updatePanel(PanelState::Offline, lastOtaMessage, true);
    return false;
  }
  if (!ensureDatabricksAccessToken()) {
    lastOtaMessage = "OTA OAuth no disponible";
    if (manual) updatePanel(PanelState::Error, lastOtaMessage, true);
    return false;
  }

  HTTPClient http;
  http.setTimeout(app_config::httpTimeoutMs);
  const String url = endpoint("/api/device/v1/firmware/latest");
  if (!http.begin(url)) {
    lastOtaMessage = "OTA manifest no disponible";
    if (manual) updatePanel(PanelState::Error, lastOtaMessage, true);
    return false;
  }
  addRequestAuth(http);
  http.addHeader("X-Firmware-Version", app_config::firmwareVersion);
  const int code = http.GET();
  const String body = code > 0 ? http.getString() : http.errorToString(code);
  http.end();
  lastOtaCheck = millis();

  if (code == 401 && databricksAppEndpoint()) clearDatabricksAccessToken();
  if (code != 200) {
    lastOtaMessage = String("OTA manifest HTTP ") + code;
    if (manual) updatePanel(PanelState::Error, lastOtaMessage, true);
    Serial.printf("[WARN] OTA manifest HTTP=%d detail=%s\n", code, body.c_str());
    return false;
  }

  const String version = jsonStringValue(body, "version");
  const String sha256 = jsonStringValue(body, "sha256");
  const String path = jsonStringValue(body, "url");
  const size_t size = static_cast<size_t>(jsonUnsignedLongValue(body, "size", 0UL));
  if (!version.length() || !path.length() || !isSha256Hex(sha256)) {
    lastOtaMessage = "OTA manifest invalido";
    if (manual) updatePanel(PanelState::Error, lastOtaMessage, true);
    return false;
  }

  if (compareSemanticVersion(version, app_config::firmwareVersion) <= 0) {
    lastOtaMessage = String("FW ") + app_config::firmwareVersion + " al dia";
    if (manual) updatePanel(PanelState::Ready, lastOtaMessage, true);
    return false;
  }

  lastOtaMessage = String("OTA disponible ") + version;
  Serial.printf("OTA AVAILABLE current=%s latest=%s size=%u\n",
                app_config::firmwareVersion, version.c_str(), static_cast<unsigned>(size));
  if (!app_config::otaAutoUpdate && !manual) return false;
  return performOtaUpdate(version, path, sha256, size);
}

void confirmOtaBootIfHealthy() {
  if (otaBootConfirmed || millis() < app_config::otaBootConfirmDelayMs) return;
  otaBootConfirmed = true;

  const esp_partition_t* running = esp_ota_get_running_partition();
  if (!running) return;
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  const esp_err_t stateResult = esp_ota_get_state_partition(running, &state);
  if (stateResult != ESP_OK || state != ESP_OTA_IMG_PENDING_VERIFY) return;

  if (displayReady) {
    const esp_err_t validResult = esp_ota_mark_app_valid_cancel_rollback();
    Serial.printf("OTA BOOT CONFIRM result=%d\n", static_cast<int>(validResult));
  } else {
    Serial.println("[ERROR] OTA boot self-test failed; rolling back");
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
}

bool checkBackendHealthOnce() {
  if (!backendBaseUrl().length()) {
    backendAvailable = false;
    lastBackendMessage = "Sin endpoint configurado";
    updatePanel(PanelState::Error, "Endpoint no configurado", true);
    return false;
  }
  if (!ensureDatabricksAccessToken()) {
    backendAvailable = false;
    updatePanel(PanelState::Error, lastBackendMessage, true);
    return false;
  }

  updatePanel(PanelState::Busy,
    databricksAppEndpoint() ? "Verificando Databricks" : "Verificando endpoint 3C");
  HTTPClient http;
  http.setTimeout(app_config::httpTimeoutMs);
  const String url = endpoint("/api/device/v1/health");
  if (!http.begin(url)) {
    lastBackendMessage = "No se pudo abrir URL " + url;
    http.end();
    backendAvailable = false;
    updatePanel(PanelState::Error, lastBackendMessage, true);
    Serial.printf("[ERROR] health begin failed url=%s\n", url.c_str());
    return false;
  }

  addRequestAuth(http);
  const int code = http.GET();
  lastBackendMessage = code > 0 ? http.getString() : http.errorToString(code);
  http.end();
  if (code == 401 && databricksAppEndpoint()) clearDatabricksAccessToken();
  backendAvailable = code == 200;
  updatePanel(
    backendAvailable ? PanelState::Ready : PanelState::Error,
    backendAvailable
      ? (databricksAppEndpoint() ? "Databricks conectado" : "Endpoint 3C conectado")
      : String("Health HTTP ") + code,
    true);
  if (!backendAvailable) {
    Serial.printf("[ERROR] health HTTP=%d endpoint=%s detail=%s\n",
      code, backendBaseUrl().c_str(), lastBackendMessage.c_str());
  }
  Serial.printf("GET health -> %d %s endpoint=%s\n",
    code, lastBackendMessage.c_str(), backendBaseUrl().c_str());
  return backendAvailable;
}

bool checkBackendHealth() {
  if (WiFi.status() != WL_CONNECTED) {
    backendAvailable = false;
    updatePanel(PanelState::Offline, "Sin acceso a Internet", true);
    return false;
  }
  return checkBackendHealthOnce();
}

int send3CCommand(const String& rawCommand) {
  String command = rawCommand;
  command.trim();
  if (!command.length()) {
    updatePanel(PanelState::Error, "Comando vacio", true);
    return 400;
  }
  if (WiFi.status() != WL_CONNECTED) {
    updatePanel(PanelState::Offline, "Sin acceso a Internet", true);
    return 503;
  }

  updatePanel(PanelState::Busy, "Enviando a Databricks", true);
  if (!backendBaseUrl().length()) {
    updatePanel(PanelState::Error, "Endpoint no configurado", true);
    return 503;
  }
  if (!ensureDatabricksAccessToken()) {
    updatePanel(PanelState::Error, lastBackendMessage, true);
    return 503;
  }

  HTTPClient http;
  http.setTimeout(app_config::httpTimeoutMs);
  if (!http.begin(endpoint("/api/device/v1/commands"))) {
    updatePanel(PanelState::Error, "No se pudo abrir endpoint 3C", true);
    return 503;
  }
  http.addHeader("Content-Type", "application/json");
  addRequestAuth(http);

  char randomPart[9];
  snprintf(randomPart, sizeof(randomPart), "%08lx", static_cast<unsigned long>(esp_random()));
  const String requestId = String(app_config::deviceId) + "-" + randomPart + "-" + String(millis());
  const String body = "{\"device_id\":\"" + jsonEscape(app_config::deviceId) +
    "\",\"request_id\":\"" + jsonEscape(requestId) +
    "\",\"text\":\"" + jsonEscape(command) + "\"}";
  const int code = http.POST(body);
  lastBackendMessage = code > 0 ? http.getString() : http.errorToString(code);
  http.end();
  if (code == 401 && databricksAppEndpoint()) clearDatabricksAccessToken();

  if (code == 200 || code == 202) {
    backendAvailable = true;
    lastCommandId = jsonStringValue(lastBackendMessage, "command_id");
    if (!lastCommandId.length()) {
      setProtocolError("POST", "falta command_id");
      Serial.printf("POST 3C -> %d %s\n", code, lastBackendMessage.c_str());
      return code;
    }
    lastCommandPoll = millis();
    updatePanel(PanelState::Pending, "CONFIRMACIÓN REQUERIDA EN WEB", true);
  } else {
    setTransportError("POST", code, lastBackendMessage, true);
  }
  Serial.printf("POST 3C -> %d %s\n", code, lastBackendMessage.c_str());
  return code;
}

void pollCommandStatus() {
  if (!lastCommandId.length() || WiFi.status() != WL_CONNECTED) return;
  if (!backendBaseUrl().length()) return;
  if (!ensureDatabricksAccessToken()) {
    setProtocolError("OAUTH", lastBackendMessage);
    return;
  }
  HTTPClient http;
  http.setTimeout(app_config::httpTimeoutMs);
  if (!http.begin(endpoint("/api/device/v1/commands/" + lastCommandId))) {
    setTransportError("POLL", -1, "No se pudo abrir endpoint 3C", true);
    return;
  }
  addRequestAuth(http);
  const int code = http.GET();
  const String body = code > 0 ? http.getString() : http.errorToString(code);
  lastBackendMessage = body;
  http.end();
  if (code == 401 && databricksAppEndpoint()) clearDatabricksAccessToken();
  if (code != 200) {
    setTransportError("POLL", code, body, true);
    return;
  }

  String status = normalizedStatus(jsonStringValue(body, "status"));
  const String result = jsonStringValue(body, "result");
  Serial.printf("GET command status -> %d status=%s result=%s\n", code, status.c_str(), result.c_str());

  if (status == "applied") {
    updatePanel(PanelState::Applied, result.length() ? result : "Confirmado en backend 3C", true);
    lastCommandId = "";
  } else if (status == "rejected") {
    updatePanel(PanelState::Rejected, result.length() ? result : "Rechazado en backend 3C", true);
    lastCommandId = "";
  } else if (status == "error" || status == "failed" || status == "fallido") {
    setProtocolError("POLL", result.length() ? result : "Error reportado por backend 3C");
  } else if (status == "pending_confirmation" || status == "pending" || status == "pendiente") {
    backendAvailable = true;
    updatePanel(PanelState::Pending, "CONFIRMACIÓN REQUERIDA EN WEB");
  } else {
    setProtocolError("POLL", status.length() ? String("estado desconocido '") + status + "'" : "falta status");
  }
}

const char controlPage[] PROGMEM = R"HTML(
<!doctype html><html lang="es"><meta name="viewport" content="width=device-width,initial-scale=1">
<style>body{font-family:system-ui;max-width:680px;margin:auto;padding:24px;background:#eef3f7}section{background:white;padding:20px;border-radius:16px;box-shadow:0 5px 20px #0001}button,textarea{font:inherit}button{padding:13px 18px;border:0;border-radius:10px;background:#08784f;color:white}textarea{box-sizing:border-box;width:100%;min-height:120px;padding:12px;margin:8px 0 12px}.warn{color:#805500}</style>
<h1>Panel ESP32-4848S040 3C + OTA</h1><section><p class="warn">Databricks controla el backend y la versión OTA. Google Sheets cambia solo después de confirmación humana.</p><textarea id="text" placeholder="Cambia la tarea J10 a mensual"></textarea><button onclick="send3c()">Enviar al asistente</button><button onclick="health()">Probar Databricks</button><button onclick="ota()">Buscar OTA</button><pre id="result"></pre></section>
<script>async function send3c(){const b=new URLSearchParams({text:document.querySelector('#text').value});const r=await fetch('/api/3c',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b});result.textContent=r.status+' '+await r.text()}async function health(){const r=await fetch('/api/backend-health',{method:'POST'});result.textContent=r.status+' '+await r.text()}async function ota(){const r=await fetch('/api/ota-check',{method:'POST'});result.textContent=r.status+' '+await r.text()}</script></html>
)HTML";

void configureWebServer() {
  web.on("/", HTTP_GET, [] { web.send_P(200, "text/html; charset=utf-8", controlPage); });
  web.on("/health", HTTP_GET, [] {
    const String body = String("{\"ok\":true,\"board\":\"ESP32-4848S040\",\"wifi\":") +
      (WiFi.status() == WL_CONNECTED ? "true" : "false") +
      ",\"backend\":" + (backendAvailable ? "true" : "false") +
      ",\"pending\":" + (lastCommandId.length() ? "true" : "false") +
      ",\"transport\":\"internet\"" +
      ",\"firmware_version\":\"" + String(app_config::firmwareVersion) + "\"" +
      ",\"ota_status\":\"" + jsonEscape(lastOtaMessage) + "\"}";
    web.send(200, "application/json", body);
  });
  web.on("/api/backend-health", HTTP_POST, [] {
    web.send(checkBackendHealth() ? 200 : 502, "application/json", lastBackendMessage);
  });
  web.on("/api/3c", HTTP_POST, [] {
    app_config::commandBuffer = web.arg("text");
    const int code = send3CCommand(app_config::commandBuffer);
    web.send(code == 200 || code == 202 ? 202 : 502, "application/json", lastBackendMessage);
  });
  web.on("/api/ota-check", HTTP_POST, [] {
    const bool started = checkForOtaUpdate(true);
    if (!started) {
      web.send(200, "application/json",
        String("{\"version\":\"") + app_config::firmwareVersion +
        "\",\"status\":\"" + jsonEscape(lastOtaMessage) + "\"}");
    }
  });
  web.onNotFound([] { web.send(404, "application/json", "{\"error\":\"not found\"}"); });
  web.begin();
}

const char* wifiStatusLabel(wl_status_t status) {
  switch (status) {
    case WL_NO_SHIELD: return "NO_SHIELD";
    case WL_IDLE_STATUS: return "IDLE";
    case WL_NO_SSID_AVAIL: return "NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED: return "SCAN_COMPLETED";
    case WL_CONNECTED: return "CONNECTED";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
    default: return "UNKNOWN";
  }
}

void configureWifi() {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(app_config::deviceId);
}

void connectWifi() {
  if (!strlen(app_config::wifiSsid)) {
    updatePanel(PanelState::Offline, "Configure local_config.h");
    Serial.println("Configure include/local_config.h antes de usar Wi-Fi.");
    return;
  }
  configureWifi();
  Serial.printf("Wi-Fi: iniciando STA, credenciales presentes, status=%d (%s)\n",
    static_cast<int>(WiFi.status()), wifiStatusLabel(WiFi.status()));
  WiFi.begin(app_config::wifiSsid, app_config::wifiPassword);
  lastWifiAttempt = millis();
  updatePanel(PanelState::Busy, "Conectando Wi-Fi");
}

void handleTouch() {
  const TouchSample sample = readTouch();
  if (!sample.ready) return;
  if (sample.touched && !touchDown) {
    Serial.printf(
      "TOUCH raw=(%u,%u) mapped=(%u,%u) swap=%d mx=%d my=%d\n",
      sample.rawX, sample.rawY, sample.x, sample.y,
      app_config::touchSwapXy ? 1 : 0,
      app_config::touchMirrorX ? 1 : 0,
      app_config::touchMirrorY ? 1 : 0);
    if (commandEditorOpen) {
      if (sample.y >= 216) {
        virtual_keyboard::Key key{};
        if (virtual_keyboard::hitTest(keyboardMode, sample.x, sample.y, &key)) {
          Serial.printf(
            "TOUCH KEY label=%s mapped=(%u,%u)\n",
            key.definition.label, sample.x, sample.y);
          using virtual_keyboard::KeyKind;
          switch (key.definition.kind) {
            case KeyKind::Character:
              commandBuffer.insert(key.definition.label);
              break;
            case KeyKind::Backspace:
              commandBuffer.backspace();
              break;
            case KeyKind::Space:
              commandBuffer.insert(' ');
              break;
            case KeyKind::Enter:
              app_config::commandBuffer = commandBuffer.c_str();
              commandEditorOpen = false;
              drawPanel();
              send3CCommand(app_config::commandBuffer);
              touchDown = sample.touched;
              return;
            case KeyKind::ToggleAlphaNumeric:
              keyboardMode = keyboardMode == virtual_keyboard::KeyboardMode::Alpha
                  ? virtual_keyboard::KeyboardMode::NumericSymbols
                  : virtual_keyboard::KeyboardMode::Alpha;
              break;
          }
          drawEditor();
        }
      } else if (sample.y >= 160 && sample.y < 215) {
        if (sample.x < 110) {
          commandEditorOpen = false;
          drawPanel();
        } else if (sample.x < 190) {
          commandBuffer.moveLeft();
          drawEditor();
        } else if (sample.x < 270) {
          commandBuffer.deleteForward();
          drawEditor();
        } else if (sample.x < 395) {
          keyboardMode = keyboardMode == virtual_keyboard::KeyboardMode::Alpha
              ? virtual_keyboard::KeyboardMode::NumericSymbols
              : virtual_keyboard::KeyboardMode::Alpha;
          drawEditor();
        }
      } else if (sample.y >= 42 && sample.y < 114) {
        String text(commandBuffer.c_str());
        if (text.length()) {
          display->setTextSize(2);
          size_t best = 0;
          uint16_t bestDistance = UINT16_MAX;
          for (size_t i = 0; i <= text.length() && i <= kCommandCapacity; ++i) {
            int16_t x1=0,y1=0; uint16_t w=0,h=0;
            display->getTextBounds(text.substring(0, i), 0, 0, &x1, &y1, &w, &h);
            const uint16_t distance = static_cast<uint16_t>(
              abs(static_cast<int>(15 + w) - static_cast<int>(sample.x)));
            if (distance < bestDistance) { bestDistance = distance; best = i; }
          }
          commandBuffer.setCursor(best);
          drawEditor();
        }
      }
    } else if (sample.y >= 350) {
      if (sample.x < 240) {
        checkBackendHealth();
      } else {
        commandEditorOpen = true;
        keyboardMode = virtual_keyboard::KeyboardMode::Alpha;
        drawEditor();
      }
    }
  }
  touchDown = sample.touched;
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.printf("ESP32-4848S040 3C | PSRAM: %s | %u bytes\n",
    psramFound() ? "OK" : "NO", ESP.getPsramSize());

  displayReady = initializeDisplay();
  if (!displayReady) Serial.println("No se pudo inicializar la pantalla ST7701.");
  Wire.begin(pins::touchSda, pins::touchScl, 100000);
  audioReady = initializeAudio();
  commandBuffer.set(app_config::commandBuffer.c_str());
  updatePanel(PanelState::Booting, "Hardware inicializado; Databricks Cloud");
  playTone(520, 60);
  connectWifi();
  configureWebServer();
}

void loop() {
  web.handleClient();
  handleTouch();

  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiAnnounced) {
      wifiAnnounced = true;
      Serial.printf("Red con Internet lista; Wi-Fi RSSI=%d dBm\n", WiFi.RSSI());
      checkBackendHealth();
      checkForOtaUpdate(false);
    }
    if (lastCommandId.length() && millis() - lastCommandPoll >= app_config::commandPollMs) {
      lastCommandPoll = millis();
      pollCommandStatus();
    }
    if (!lastCommandId.length() && millis() - lastHealthCheck >= app_config::healthCheckMs) {
      lastHealthCheck = millis();
      checkBackendHealth();
    }
    if (millis() - lastOtaCheck >= app_config::otaCheckMs) {
      checkForOtaUpdate(false);
    }
  } else {
    wifiAnnounced = false;
    if (strlen(app_config::wifiSsid) && millis() - lastWifiAttempt >= app_config::wifiRetryMs) {
      lastWifiAttempt = millis();
      const wl_status_t status = WiFi.status();
      Serial.printf("Red sin acceso: status=%d (%s); reintentando enlace\n",
        static_cast<int>(status), wifiStatusLabel(status));
      WiFi.reconnect();
      updatePanel(PanelState::Busy, "Reconectando Internet");
    }
  }
  confirmOtaBootIfHealthy();
  delay(5);
}

#endif  // BOARD_PANEL_4848S040
