/*
 * Aquarium Water Level Monitor
 * Copyright (c) 2026 Matthew Byrne (mattmbyrne-cmyk)
 * Licensed under the MIT License.
 * See LICENSE file in the project root for full license information.
 */
/*
  ESP32 + A02YYUW Water Level Indicator

  SENSOR WIRING
  -------------
  A02YYUW VCC -> ESP32 5V/VIN
  A02YYUW GND -> ESP32 GND
  A02YYUW TX  -> ESP32 GPIO16
  A02YYUW RX  -> Not connected

  OUTPUT WIRING
  -------------
  Green LED 1  -> GPIO33 through 220-ohm resistor
  Green LED 2  -> GPIO32 through 220-ohm resistor
  Yellow LED 1 -> GPIO26 through 220-ohm resistor
  Yellow LED 2 -> GPIO25 through 220-ohm resistor
  Red LED 1    -> GPIO14 through 220-ohm resistor
  Red LED 2    -> GPIO27 through 220-ohm resistor

  Active buzzer positive -> GPIO23
  Active buzzer negative -> GND

  WATER LEVEL INDICATION
  ----------------------
  80-100% -> Both green LEDs
  60-79%  -> Green LED 2 + Yellow LED 1
  40-59%  -> Both yellow LEDs
  20-39%  -> Yellow LED 2 + Red LED 1
  0-19%   -> Both red LEDs + buzzer

  WI-FI CONFIGURATION
  -------------------
  ESP32 access point: Watersensor.local
  Configuration page: http://192.168.4.1

  HOME WI-FI ACCESS
  -----------------
  http://watersensor.local

  LIVE WEBPAGE UPDATE
  -------------------
  When connected to home Wi-Fi, sensor information is
  updated every 5 seconds without refreshing the page.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>

// =====================================================
// SENSOR UART
// =====================================================

HardwareSerial sensorSerial(2);

const uint8_t SENSOR_RX_PIN = 16;
const int8_t SENSOR_TX_PIN = -1;
const uint32_t SENSOR_BAUD = 9600;

// =====================================================
// OUTPUT PINS
// =====================================================

const uint8_t GREEN_LED_1 = 33;
const uint8_t GREEN_LED_2 = 32;
const uint8_t YELLOW_LED_1 = 26;
const uint8_t YELLOW_LED_2 = 25;
const uint8_t RED_LED_1 = 14;
const uint8_t RED_LED_2 = 27;
const uint8_t BUZZER_PIN = 23;

const uint8_t BUZZER_ON = HIGH;
const uint8_t BUZZER_OFF = LOW;

// =====================================================
// WI-FI, DNS AND WEB SERVER
// =====================================================

const char *ACCESS_POINT_SSID = "Watersensor.local";
const char *MDNS_HOSTNAME = "watersensor";

const byte DNS_PORT = 53;

WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

String homeWiFiSSID = "";
String homeWiFiPassword = "";

bool homeWiFiConnected = false;
bool mdnsStarted = false;
bool dnsServerStarted = false;

unsigned long lastWiFiReconnectAttempt = 0;

const unsigned long WIFI_RECONNECT_INTERVAL_MS = 30000;

// =====================================================
// WATER LEVEL CALIBRATION
// =====================================================

float fullDistanceMM = 40.0;
float emptyDistanceMM = 140.0;

// =====================================================
// WATER LEVEL THRESHOLDS
// =====================================================

const float FULL_THRESHOLD_PERCENT = 80.0;
const float ALMOST_FULL_THRESHOLD_PERCENT = 60.0;
const float MEDIUM_THRESHOLD_PERCENT = 40.0;
const float LOW_THRESHOLD_PERCENT = 20.0;

// =====================================================
// SENSOR TIMING
// =====================================================

const unsigned long SENSOR_WARNING_TIME_MS = 2000;
const unsigned long SERIAL_PRINT_TIME_MS = 500;

unsigned long lastValidReadingTime = 0;
unsigned long lastSerialPrintTime = 0;
unsigned long lastErrorPrintTime = 0;

// =====================================================
// CURRENT SENSOR VALUES
// =====================================================

uint16_t currentRawDistanceMM = 0;
uint16_t currentFilteredDistanceMM = 0;

float currentWaterLevelPercent = 0.0;

bool sensorReadingValid = false;

// =====================================================
// WATER LEVEL STATES
// =====================================================

enum LevelState
{
  LEVEL_EMPTY,
  LEVEL_GETTING_LOWER,
  LEVEL_MEDIUM,
  LEVEL_ALMOST_FULL,
  LEVEL_FULL
};

LevelState currentState = LEVEL_EMPTY;

// =====================================================
// TURN OFF ALL OUTPUTS
// =====================================================

void allOutputsOff()
{
  digitalWrite(GREEN_LED_1, LOW);
  digitalWrite(GREEN_LED_2, LOW);

  digitalWrite(YELLOW_LED_1, LOW);
  digitalWrite(YELLOW_LED_2, LOW);

  digitalWrite(RED_LED_1, LOW);
  digitalWrite(RED_LED_2, LOW);

  digitalWrite(BUZZER_PIN, BUZZER_OFF);
}

// =====================================================
// READ A02YYUW SENSOR
// =====================================================

bool readA02YYUW(uint16_t &distanceMM)
{
  static uint8_t frame[4];
  static uint8_t framePosition = 0;

  while (sensorSerial.available() > 0)
  {
    uint8_t incomingByte = sensorSerial.read();

    if (framePosition == 0)
    {
      if (incomingByte == 0xFF)
      {
        frame[0] = incomingByte;
        framePosition = 1;
      }

      continue;
    }

    frame[framePosition] = incomingByte;
    framePosition++;

    if (framePosition == 4)
    {
      framePosition = 0;

      uint8_t calculatedChecksum =
          (uint8_t)(frame[0] + frame[1] + frame[2]);

      if (calculatedChecksum != frame[3])
      {
        if (millis() - lastErrorPrintTime >= 1000)
        {
          lastErrorPrintTime = millis();

          Serial.print("Checksum error. Received: 0x");
          Serial.print(frame[3], HEX);
          Serial.print(" Expected: 0x");
          Serial.println(calculatedChecksum, HEX);
        }

        return false;
      }

      uint16_t measuredDistance =
          ((uint16_t)frame[1] << 8) | frame[2];

      if (measuredDistance < 30 ||
          measuredDistance > 4500)
      {
        if (millis() - lastErrorPrintTime >= 1000)
        {
          lastErrorPrintTime = millis();

          Serial.print("Out-of-range reading: ");
          Serial.print(measuredDistance);
          Serial.println(" mm");
        }

        return false;
      }

      distanceMM = measuredDistance;

      return true;
    }
  }

  return false;
}

// =====================================================
// MEDIAN FILTER
// =====================================================

const uint8_t SAMPLE_COUNT = 5;

uint16_t samples[SAMPLE_COUNT];

uint8_t samplePosition = 0;
uint8_t validSampleCount = 0;

uint16_t medianDistance(uint16_t newDistance)
{
  samples[samplePosition] = newDistance;

  samplePosition++;

  if (samplePosition >= SAMPLE_COUNT)
  {
    samplePosition = 0;
  }

  if (validSampleCount < SAMPLE_COUNT)
  {
    validSampleCount++;
  }

  uint16_t sortedSamples[SAMPLE_COUNT];

  for (uint8_t i = 0; i < validSampleCount; i++)
  {
    sortedSamples[i] = samples[i];
  }

  for (uint8_t i = 1; i < validSampleCount; i++)
  {
    uint16_t value = sortedSamples[i];
    int8_t position = i - 1;

    while (position >= 0 &&
           sortedSamples[position] > value)
    {
      sortedSamples[position + 1] =
          sortedSamples[position];

      position--;
    }

    sortedSamples[position + 1] = value;
  }

  return sortedSamples[validSampleCount / 2];
}

// =====================================================
// CALCULATE WATER LEVEL PERCENTAGE
// =====================================================

float calculateWaterLevelPercent(float distanceMM)
{
  float measurementRange =
      emptyDistanceMM - fullDistanceMM;

  if (measurementRange <= 0.0)
  {
    return 0.0;
  }

  float waterLevel =
      ((emptyDistanceMM - distanceMM) /
       measurementRange) * 100.0;

  return constrain(waterLevel, 0.0, 100.0);
}

// =====================================================
// DETERMINE WATER LEVEL STATE
// =====================================================

LevelState determineLevelState(float levelPercent)
{
  if (levelPercent >= FULL_THRESHOLD_PERCENT)
  {
    return LEVEL_FULL;
  }

  if (levelPercent >= ALMOST_FULL_THRESHOLD_PERCENT)
  {
    return LEVEL_ALMOST_FULL;
  }

  if (levelPercent >= MEDIUM_THRESHOLD_PERCENT)
  {
    return LEVEL_MEDIUM;
  }

  if (levelPercent >= LOW_THRESHOLD_PERCENT)
  {
    return LEVEL_GETTING_LOWER;
  }

  return LEVEL_EMPTY;
}

// =====================================================
// UPDATE LEDS AND BUZZER
// =====================================================

void updateOutputs(LevelState state)
{
  allOutputsOff();

  switch (state)
  {
    case LEVEL_FULL:

      digitalWrite(GREEN_LED_1, HIGH);
      digitalWrite(GREEN_LED_2, HIGH);

      break;

    case LEVEL_ALMOST_FULL:

      digitalWrite(GREEN_LED_1, HIGH);
      digitalWrite(YELLOW_LED_2, HIGH);

      break;

    case LEVEL_MEDIUM:

      digitalWrite(YELLOW_LED_1, HIGH);
      digitalWrite(YELLOW_LED_2, HIGH);

      break;

    case LEVEL_GETTING_LOWER:

      digitalWrite(YELLOW_LED_1, HIGH);
      digitalWrite(RED_LED_2, HIGH);

      break;

    case LEVEL_EMPTY:

      digitalWrite(RED_LED_1, HIGH);
      digitalWrite(RED_LED_2, HIGH);
      digitalWrite(BUZZER_PIN, BUZZER_ON);

      break;
  }
}

// =====================================================
// CONVERT STATE TO TEXT
// =====================================================

const char *stateToText(LevelState state)
{
  switch (state)
  {
    case LEVEL_FULL:
      return "FULL";

    case LEVEL_ALMOST_FULL:
      return "ALMOST FULL";

    case LEVEL_MEDIUM:
      return "MEDIUM";

    case LEVEL_GETTING_LOWER:
      return "GETTING LOWER";

    case LEVEL_EMPTY:
      return "EMPTY - BUZZER ON";
  }

  return "UNKNOWN";
}

// =====================================================
// WEBPAGE LEVEL COLOUR
// =====================================================

String levelColour()
{
  if (!sensorReadingValid)
  {
    return "#94a3b8";
  }

  switch (currentState)
  {
    case LEVEL_FULL:
      return "#16a34a";

    case LEVEL_ALMOST_FULL:
      return "#65a30d";

    case LEVEL_MEDIUM:
      return "#eab308";

    case LEVEL_GETTING_LOWER:
      return "#f97316";

    case LEVEL_EMPTY:
      return "#dc2626";
  }

  return "#0284c7";
}

// =====================================================
// LOAD SAVED SETTINGS
// =====================================================

void loadSettings()
{
  preferences.begin("water-sensor", true);

  homeWiFiSSID =
      preferences.getString("wifiSSID", "");

  homeWiFiPassword =
      preferences.getString("wifiPass", "");

  fullDistanceMM =
      preferences.getFloat("fullMM", 40.0);

  emptyDistanceMM =
      preferences.getFloat("emptyMM", 140.0);

  preferences.end();

  if (fullDistanceMM < 30.0 ||
      fullDistanceMM >= 4500.0 ||
      emptyDistanceMM <= fullDistanceMM ||
      emptyDistanceMM > 4500.0)
  {
    Serial.println(
        "Invalid saved calibration. Restoring defaults."
    );

    fullDistanceMM = 40.0;
    emptyDistanceMM = 140.0;
  }
}

// =====================================================
// SAVE SETTINGS
// =====================================================

void saveSettings()
{
  preferences.begin("water-sensor", false);

  preferences.putString(
      "wifiSSID",
      homeWiFiSSID
  );

  preferences.putString(
      "wifiPass",
      homeWiFiPassword
  );

  preferences.putFloat(
      "fullMM",
      fullDistanceMM
  );

  preferences.putFloat(
      "emptyMM",
      emptyDistanceMM
  );

  preferences.end();
}

// =====================================================
// ESCAPE TEXT FOR HTML
// =====================================================

String htmlEscape(String text)
{
  text.replace("&", "&amp;");
  text.replace("\"", "&quot;");
  text.replace("<", "&lt;");
  text.replace(">", "&gt;");
  text.replace("'", "&#39;");

  return text;
}

// =====================================================
// ESCAPE TEXT FOR JSON
// =====================================================

String jsonEscape(String text)
{
  text.replace("\\", "\\\\");
  text.replace("\"", "\\\"");
  text.replace("\n", "\\n");
  text.replace("\r", "");

  return text;
}

// =====================================================
// BUILD MAIN WEBPAGE
// =====================================================

String buildWebPage()
{
  String page;

  page.reserve(10000);

  page += "<!DOCTYPE html>";
  page += "<html lang='en'>";

  page += "<head>";

  page += "<meta charset='UTF-8'>";

  page +=
      "<meta name='viewport' "
      "content='width=device-width, initial-scale=1.0'>";

  page += "<title>Water Sensor</title>";

  page += "<style>";

  page +=
      "body{"
      "margin:0;"
      "padding:20px;"
      "font-family:Arial,sans-serif;"
      "background:#eef3f7;"
      "color:#1e293b;"
      "}";

  page +=
      ".container{"
      "max-width:620px;"
      "margin:auto;"
      "}";

  page +=
      ".card{"
      "background:white;"
      "padding:22px;"
      "margin-bottom:18px;"
      "border-radius:14px;"
      "box-shadow:0 4px 14px rgba(0,0,0,0.08);"
      "}";

  page +=
      "h1{"
      "margin-top:0;"
      "color:#0369a1;"
      "}";

  page +=
      "h2{"
      "margin-top:0;"
      "font-size:20px;"
      "}";

  page +=
      ".level{"
      "font-size:38px;"
      "font-weight:bold;"
      "margin:8px 0;"
      "}";

  page +=
      ".status{"
      "padding:12px;"
      "background:#f1f5f9;"
      "border-radius:8px;"
      "margin:10px 0;"
      "line-height:1.6;"
      "}";

  page +=
      ".bar-background{"
      "height:24px;"
      "background:#e2e8f0;"
      "border-radius:12px;"
      "overflow:hidden;"
      "margin:12px 0;"
      "}";

  page +=
      ".bar-level{"
      "height:100%;"
      "transition:width 0.5s,background 0.5s;"
      "}";

  page +=
      "label{"
      "display:block;"
      "font-weight:bold;"
      "margin-top:14px;"
      "margin-bottom:5px;"
      "}";

  page +=
      "input{"
      "width:100%;"
      "box-sizing:border-box;"
      "padding:11px;"
      "border:1px solid #cbd5e1;"
      "border-radius:8px;"
      "font-size:16px;"
      "}";

  page +=
      "button{"
      "width:100%;"
      "padding:13px;"
      "margin-top:20px;"
      "background:#0284c7;"
      "color:white;"
      "border:none;"
      "border-radius:8px;"
      "font-size:17px;"
      "font-weight:bold;"
      "cursor:pointer;"
      "}";

  page +=
      ".small{"
      "font-size:14px;"
      "color:#64748b;"
      "line-height:1.5;"
      "}";

  page +=
      ".warning{"
      "font-size:14px;"
      "color:#b45309;"
      "line-height:1.5;"
      "}";

  page +=
      ".live{"
      "font-size:13px;"
      "color:#16a34a;"
      "font-weight:bold;"
      "}";

  page += "</style>";

  page += "</head>";

  page += "<body>";

  page += "<div class='container'>";

  // ---------------------------------------------------
  // WATER LEVEL CARD
  // ---------------------------------------------------

  page += "<div class='card'>";

  page += "<h1>Water Sensor</h1>";

  /*
    JavaScript updates these elements without refreshing
    or replacing the configuration form.
  */

  page += "<div id='levelPercentage' class='level' style='color:";
  page += levelColour();
  page += ";'>";

  if (sensorReadingValid)
  {
    page += String(currentWaterLevelPercent, 1);
    page += "%";
  }
  else
  {
    page += "--.-%";
  }

  page += "</div>";

  page += "<div class='bar-background'>";

  page += "<div id='levelBar' class='bar-level' style='width:";

  if (sensorReadingValid)
  {
    page += String(currentWaterLevelPercent, 1);
  }
  else
  {
    page += "0";
  }

  page += "%;background:";
  page += levelColour();
  page += ";'></div>";

  page += "</div>";

  page += "<div class='status'>";

  page += "<strong>Status:</strong> ";
  page += "<span id='levelStatus'>";

  if (sensorReadingValid)
  {
    page += stateToText(currentState);
  }
  else
  {
    page += "Waiting for valid sensor data...";
  }

  page += "</span>";

  page += "<br><strong>Filtered distance:</strong> ";
  page += "<span id='filteredDistance'>";

  if (sensorReadingValid)
  {
    page += String(currentFilteredDistanceMM);
  }
  else
  {
    page += "--";
  }

  page += "</span> mm";

  page += "<br><strong>Raw distance:</strong> ";
  page += "<span id='rawDistance'>";

  if (sensorReadingValid)
  {
    page += String(currentRawDistanceMM);
  }
  else
  {
    page += "--";
  }

  page += "</span> mm";

  page += "</div>";

  if (WiFi.status() == WL_CONNECTED)
  {
    page +=
        "<p id='liveIndicator' class='live'>"
        "Live update active - updates every 5 seconds"
        "</p>";
  }
  else
  {
    page +=
        "<p id='liveIndicator' class='small'>"
        "Live updates begin after connection to home Wi-Fi."
        "</p>";
  }

  page += "<p class='small'>";

  page += "ESP32 hotspot: ";
  page += ACCESS_POINT_SSID;

  page += "<br>Hotspot address: ";
  page += "http://192.168.4.1";

  page += "<br>Home Wi-Fi address: ";
  page += "http://watersensor.local";

  page += "</p>";

  page += "</div>";

  // ---------------------------------------------------
  // WI-FI STATUS CARD
  // ---------------------------------------------------

  page += "<div class='card'>";

  page += "<h2>Wi-Fi Status</h2>";

  page += "<div id='wifiStatus' class='status'>";

  if (WiFi.status() == WL_CONNECTED)
  {
    page += "<strong>Connected to:</strong> ";
    page += htmlEscape(WiFi.SSID());

    page += "<br><strong>Home network IP:</strong> ";
    page += WiFi.localIP().toString();

    page += "<br><strong>Home network address:</strong> ";
    page += "http://watersensor.local";
  }
  else
  {
    page +=
        "Not connected to home Wi-Fi."
        "<br>Enter the home Wi-Fi details below.";
  }

  page += "</div>";

  page += "</div>";

  // ---------------------------------------------------
  // CONFIGURATION CARD
  // ---------------------------------------------------

  page += "<div class='card'>";

  page += "<h2>Configuration</h2>";

  page += "<form action='/save' method='POST'>";

  page += "<label for='ssid'>";
  page += "Home Wi-Fi SSID";
  page += "</label>";

  page +=
      "<input "
      "type='text' "
      "id='ssid' "
      "name='ssid' "
      "maxlength='32' "
      "value='";

  page += htmlEscape(homeWiFiSSID);

  page +=
      "' "
      "placeholder='Enter your home Wi-Fi name'>";

  page += "<label for='password'>";
  page += "Home Wi-Fi Password";
  page += "</label>";

  page +=
      "<input "
      "type='password' "
      "id='password' "
      "name='password' "
      "maxlength='64' "
      "placeholder='Leave blank to keep saved password'>";

  page += "<label for='fullDistance'>";
  page += "Full water distance in millimetres";
  page += "</label>";

  page +=
      "<input "
      "type='number' "
      "id='fullDistance' "
      "name='fullDistance' "
      "min='30' "
      "max='4499' "
      "step='0.1' "
      "value='";

  page += String(fullDistanceMM, 1);

  page += "' required>";

  page += "<label for='emptyDistance'>";
  page += "Empty water distance in millimetres";
  page += "</label>";

  page +=
      "<input "
      "type='number' "
      "id='emptyDistance' "
      "name='emptyDistance' "
      "min='31' "
      "max='4500' "
      "step='0.1' "
      "value='";

  page += String(emptyDistanceMM, 1);

  page += "' required>";

  page += "<p class='warning'>";

  page +=
      "The empty distance must be greater than the "
      "full distance. The ESP32 will restart after "
      "the settings are saved.";

  page += "</p>";

  page += "<button type='submit'>";
  page += "Save Settings";
  page += "</button>";

  page += "</form>";

  page += "</div>";

  /*
    Add live sensor updates only while the ESP32 is
    connected to home Wi-Fi.

    The script fetches /api/status every 5 seconds and
    changes only the sensor display elements. It does
    not reload the page or change the configuration form.
  */

  if (WiFi.status() == WL_CONNECTED)
  {
    page += R"rawliteral(
<script>
async function updateSensorData() {
  try {
    const response = await fetch(
      '/api/status?time=' + Date.now(),
      {
        method: 'GET',
        cache: 'no-store'
      }
    );

    if (!response.ok) {
      throw new Error('Status request failed');
    }

    const data = await response.json();

    const percentageElement =
      document.getElementById('levelPercentage');

    const barElement =
      document.getElementById('levelBar');

    const statusElement =
      document.getElementById('levelStatus');

    const filteredElement =
      document.getElementById('filteredDistance');

    const rawElement =
      document.getElementById('rawDistance');

    const wifiElement =
      document.getElementById('wifiStatus');

    const liveElement =
      document.getElementById('liveIndicator');

    if (data.sensorValid) {
      percentageElement.textContent =
        data.percentage.toFixed(1) + '%';

      percentageElement.style.color = data.colour;

      barElement.style.width =
        data.percentage.toFixed(1) + '%';

      barElement.style.background = data.colour;

      statusElement.textContent = data.status;

      filteredElement.textContent =
        data.filteredDistance;

      rawElement.textContent =
        data.rawDistance;
    } else {
      percentageElement.textContent = '--.-%';
      percentageElement.style.color = '#94a3b8';

      barElement.style.width = '0%';
      barElement.style.background = '#94a3b8';

      statusElement.textContent =
        'Waiting for valid sensor data...';

      filteredElement.textContent = '--';
      rawElement.textContent = '--';
    }

    if (data.wifiConnected) {
      wifiElement.innerHTML =
        '<strong>Connected to:</strong> ' +
        escapeHtml(data.ssid) +
        '<br><strong>Home network IP:</strong> ' +
        escapeHtml(data.ipAddress) +
        '<br><strong>Home network address:</strong> ' +
        'http://watersensor.local';
    } else {
      wifiElement.innerHTML =
        'Not connected to home Wi-Fi.';
    }

    liveElement.textContent =
      'Live update active - last updated ' +
      new Date().toLocaleTimeString();

    liveElement.style.color = '#16a34a';
  }
  catch (error) {
    const liveElement =
      document.getElementById('liveIndicator');

    liveElement.textContent =
      'Live update temporarily unavailable';

    liveElement.style.color = '#dc2626';
  }
}

function escapeHtml(text) {
  const element = document.createElement('div');
  element.textContent = text;
  return element.innerHTML;
}

setInterval(updateSensorData, 5000);

updateSensorData();
</script>
)rawliteral";
  }

  page += "</div>";
  page += "</body>";
  page += "</html>";

  return page;
}

// =====================================================
// MAIN WEBPAGE HANDLER
// =====================================================

void handleRoot()
{
  server.sendHeader(
      "Cache-Control",
      "no-store, no-cache, must-revalidate"
  );

  server.send(
      200,
      "text/html",
      buildWebPage()
  );
}

// =====================================================
// LIVE STATUS API
// =====================================================

void handleStatusAPI()
{
  String json;

  json.reserve(500);

  json += "{";

  json += "\"sensorValid\":";

  if (sensorReadingValid)
  {
    json += "true";
  }
  else
  {
    json += "false";
  }

  json += ",\"percentage\":";
  json += String(currentWaterLevelPercent, 1);

  json += ",\"rawDistance\":";
  json += String(currentRawDistanceMM);

  json += ",\"filteredDistance\":";
  json += String(currentFilteredDistanceMM);

  json += ",\"status\":\"";
  json += jsonEscape(String(stateToText(currentState)));
  json += "\"";

  json += ",\"colour\":\"";
  json += levelColour();
  json += "\"";

  json += ",\"wifiConnected\":";

  if (WiFi.status() == WL_CONNECTED)
  {
    json += "true";
  }
  else
  {
    json += "false";
  }

  json += ",\"ssid\":\"";

  if (WiFi.status() == WL_CONNECTED)
  {
    json += jsonEscape(WiFi.SSID());
  }

  json += "\"";

  json += ",\"ipAddress\":\"";

  if (WiFi.status() == WL_CONNECTED)
  {
    json += WiFi.localIP().toString();
  }

  json += "\"";

  json += "}";

  server.sendHeader(
      "Cache-Control",
      "no-store, no-cache, must-revalidate"
  );

  server.send(
      200,
      "application/json",
      json
  );
}

// =====================================================
// CAPTIVE PORTAL HANDLER
// =====================================================

void handleCaptivePortal()
{
  server.send(
      200,
      "text/html",
      buildWebPage()
  );
}

// =====================================================
// DISPLAY ERROR PAGE
// =====================================================

void sendErrorPage(String heading, String message)
{
  String page;

  page += "<!DOCTYPE html>";
  page += "<html lang='en'>";
  page += "<head>";
  page += "<meta charset='UTF-8'>";

  page +=
      "<meta name='viewport' "
      "content='width=device-width,initial-scale=1.0'>";

  page += "<title>Error</title>";
  page += "</head>";

  page +=
      "<body style='font-family:Arial;padding:25px;'>";

  page += "<h2>";
  page += heading;
  page += "</h2>";

  page += "<p>";
  page += message;
  page += "</p>";

  page +=
      "<p>/Return to configuration</a></p>";

  page += "</body>";
  page += "</html>";

  server.send(
      400,
      "text/html",
      page
  );
}

// =====================================================
// SAVE WEBPAGE SETTINGS
// =====================================================

void handleSave()
{
  if (!server.hasArg("ssid") ||
      !server.hasArg("fullDistance") ||
      !server.hasArg("emptyDistance"))
  {
    sendErrorPage(
        "Missing settings",
        "One or more required settings were not received."
    );

    return;
  }

  String newSSID = server.arg("ssid");
  String newPassword = "";

  if (server.hasArg("password"))
  {
    newPassword = server.arg("password");
  }

  newSSID.trim();

  float newFullDistance =
      server.arg("fullDistance").toFloat();

  float newEmptyDistance =
      server.arg("emptyDistance").toFloat();

  if (newSSID.length() > 32)
  {
    sendErrorPage(
        "Invalid Wi-Fi SSID",
        "The Wi-Fi SSID cannot exceed 32 characters."
    );

    return;
  }

  if (newPassword.length() > 64)
  {
    sendErrorPage(
        "Invalid Wi-Fi password",
        "The Wi-Fi password cannot exceed 64 characters."
    );

    return;
  }

  if (newFullDistance < 30.0 ||
      newFullDistance >= 4500.0)
  {
    sendErrorPage(
        "Invalid full distance",
        "The full distance must be between "
        "30 and 4499 millimetres."
    );

    return;
  }

  if (newEmptyDistance <= newFullDistance ||
      newEmptyDistance > 4500.0)
  {
    sendErrorPage(
        "Invalid empty distance",
        "The empty distance must be greater than "
        "the full distance and no more than "
        "4500 millimetres."
    );

    return;
  }

  bool ssidChanged = false;

  if (newSSID != homeWiFiSSID)
  {
    ssidChanged = true;
  }

  homeWiFiSSID = newSSID;

  if (newPassword.length() > 0)
  {
    homeWiFiPassword = newPassword;
  }
  else if (ssidChanged)
  {
    homeWiFiPassword = "";
  }

  fullDistanceMM = newFullDistance;
  emptyDistanceMM = newEmptyDistance;

  saveSettings();

  String savedPage;

  savedPage += "<!DOCTYPE html>";
  savedPage += "<html lang='en'>";
  savedPage += "<head>";
  savedPage += "<meta charset='UTF-8'>";

  savedPage +=
      "<meta name='viewport' "
      "content='width=device-width,initial-scale=1.0'>";

  savedPage += "<title>Settings Saved</title>";
  savedPage += "</head>";

  savedPage +=
      "<body style='font-family:Arial;padding:25px;'>";

  savedPage += "<h2>Settings saved</h2>";

  savedPage +=
      "<p>The water sensor is restarting.</p>";

  savedPage +=
      "<p>After approximately 15 seconds, reconnect "
      "your phone or computer to your home Wi-Fi.</p>";

  savedPage +=
      "<p>Then open:</p>";

  savedPage +=
      "<p><strong>http://watersensor.local</strong></p>";

  savedPage +=
      "<p>If necessary, reconnect to the "
      "Watersensor.local hotspot and open:</p>";

  savedPage +=
      "<p><strong>http://192.168.4.1</strong></p>";

  savedPage += "</body>";
  savedPage += "</html>";

  server.send(
      200,
      "text/html",
      savedPage
  );

  delay(2000);

  ESP.restart();
}

// =====================================================
// UNKNOWN REQUEST HANDLER
// =====================================================

void handleNotFound()
{
  handleCaptivePortal();
}

// =====================================================
// START MDNS
// =====================================================

void startMDNS()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    return;
  }

  if (mdnsStarted)
  {
    return;
  }

  if (MDNS.begin(MDNS_HOSTNAME))
  {
    mdnsStarted = true;

    MDNS.addService(
        "http",
        "tcp",
        80
    );

    Serial.println("mDNS started successfully.");
    Serial.println("Open: http://watersensor.local");
  }
  else
  {
    Serial.println(
        "ERROR: Unable to start mDNS."
    );
  }
}

// =====================================================
// CONNECT TO HOME WI-FI
// =====================================================

void connectToHomeWiFi()
{
  if (homeWiFiSSID.length() == 0)
  {
    Serial.println(
        "No home Wi-Fi credentials have been saved."
    );

    return;
  }

  Serial.print("Connecting to home Wi-Fi: ");
  Serial.println(homeWiFiSSID);

  WiFi.begin(
      homeWiFiSSID.c_str(),
      homeWiFiPassword.c_str()
  );

  unsigned long connectionStart = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - connectionStart < 15000)
  {
    if (dnsServerStarted)
    {
      dnsServer.processNextRequest();
    }

    server.handleClient();

    delay(250);

    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED)
  {
    homeWiFiConnected = true;

    Serial.println("Connected to home Wi-Fi.");

    Serial.print("Home Wi-Fi SSID: ");
    Serial.println(WiFi.SSID());

    Serial.print("Home Wi-Fi IP address: ");
    Serial.println(WiFi.localIP());

    Serial.print("ESP32 hotspot IP address: ");
    Serial.println(WiFi.softAPIP());

    startMDNS();
  }
  else
  {
    homeWiFiConnected = false;

    Serial.println(
        "Could not connect to home Wi-Fi."
    );

    Serial.println(
        "Connect to Watersensor.local and open "
        "http://192.168.4.1"
    );
  }
}

// =====================================================
// START WI-FI AND WEB SERVER
// =====================================================

void startWiFi()
{
  WiFi.mode(WIFI_AP_STA);

  WiFi.setHostname(MDNS_HOSTNAME);

  bool accessPointStarted =
      WiFi.softAP(ACCESS_POINT_SSID);

  Serial.println();

  if (accessPointStarted)
  {
    Serial.println("Wi-Fi access point started.");

    Serial.print("Access point name: ");
    Serial.println(ACCESS_POINT_SSID);

    Serial.print("Configuration IP: ");
    Serial.println(WiFi.softAPIP());

    dnsServerStarted = dnsServer.start(
        DNS_PORT,
        "*",
        WiFi.softAPIP()
    );

    if (dnsServerStarted)
    {
      Serial.println(
          "Captive-portal DNS server started."
      );
    }
    else
    {
      Serial.println(
          "ERROR: Captive-portal DNS failed to start."
      );
    }
  }
  else
  {
    Serial.println(
        "ERROR: Wi-Fi access point failed to start."
    );
  }

  server.on(
      "/",
      HTTP_GET,
      handleRoot
  );

  server.on(
      "/save",
      HTTP_POST,
      handleSave
  );

  /*
    Endpoint used by JavaScript to retrieve the latest
    sensor values without refreshing the entire webpage.
  */

  server.on(
      "/api/status",
      HTTP_GET,
      handleStatusAPI
  );

  // Android captive-portal checks.
  server.on(
      "/generate_204",
      HTTP_GET,
      handleCaptivePortal
  );

  server.on(
      "/gen_204",
      HTTP_GET,
      handleCaptivePortal
  );

  // Apple captive-portal checks.
  server.on(
      "/hotspot-detect.html",
      HTTP_GET,
      handleCaptivePortal
  );

  server.on(
      "/library/test/success.html",
      HTTP_GET,
      handleCaptivePortal
  );

  // Windows captive-portal checks.
  server.on(
      "/connecttest.txt",
      HTTP_GET,
      handleCaptivePortal
  );

  server.on(
      "/ncsi.txt",
      HTTP_GET,
      handleCaptivePortal
  );

  server.on(
      "/fwlink",
      HTTP_GET,
      handleCaptivePortal
  );

  // Firefox captive-portal check.
  server.on(
      "/canonical.html",
      HTTP_GET,
      handleCaptivePortal
  );

  server.onNotFound(handleNotFound);

  server.begin();

  Serial.println("Web server started.");

  connectToHomeWiFi();
}

// =====================================================
// MAINTAIN HOME WI-FI CONNECTION
// =====================================================

void maintainWiFiConnection()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    if (!homeWiFiConnected)
    {
      homeWiFiConnected = true;

      Serial.println("Home Wi-Fi connected.");

      Serial.print("Home Wi-Fi IP address: ");
      Serial.println(WiFi.localIP());

      startMDNS();
    }

    return;
  }

  if (homeWiFiConnected)
  {
    Serial.println("Home Wi-Fi connection lost.");
  }

  homeWiFiConnected = false;

  if (homeWiFiSSID.length() == 0)
  {
    return;
  }

  if (millis() - lastWiFiReconnectAttempt >=
      WIFI_RECONNECT_INTERVAL_MS)
  {
    lastWiFiReconnectAttempt = millis();

    Serial.println(
        "Attempting to reconnect to home Wi-Fi..."
    );

    WiFi.disconnect();

    WiFi.begin(
        homeWiFiSSID.c_str(),
        homeWiFiPassword.c_str()
    );
  }
}

// =====================================================
// STARTUP OUTPUT TEST
// =====================================================

void startupTest()
{
  allOutputsOff();

  Serial.println("Testing full-level LEDs...");

  digitalWrite(GREEN_LED_1, HIGH);
  digitalWrite(GREEN_LED_2, HIGH);

  delay(700);

  allOutputsOff();

  Serial.println("Testing almost-full LEDs...");

  digitalWrite(GREEN_LED_2, HIGH);
  digitalWrite(YELLOW_LED_1, HIGH);

  delay(700);

  allOutputsOff();

  Serial.println("Testing medium-level LEDs...");

  digitalWrite(YELLOW_LED_1, HIGH);
  digitalWrite(YELLOW_LED_2, HIGH);

  delay(700);

  allOutputsOff();

  Serial.println("Testing getting-lower LEDs...");

  digitalWrite(YELLOW_LED_2, HIGH);
  digitalWrite(RED_LED_1, HIGH);

  delay(700);

  allOutputsOff();

  Serial.println("Testing empty LEDs and buzzer...");

  digitalWrite(RED_LED_1, HIGH);
  digitalWrite(RED_LED_2, HIGH);
  digitalWrite(BUZZER_PIN, BUZZER_ON);

  delay(500);

  allOutputsOff();

  Serial.println("Startup test complete.");
}

// =====================================================
// SETUP
// =====================================================

void setup()
{
  Serial.begin(115200);

  delay(1000);

  pinMode(GREEN_LED_1, OUTPUT);
  pinMode(GREEN_LED_2, OUTPUT);

  pinMode(YELLOW_LED_1, OUTPUT);
  pinMode(YELLOW_LED_2, OUTPUT);

  pinMode(RED_LED_1, OUTPUT);
  pinMode(RED_LED_2, OUTPUT);

  pinMode(BUZZER_PIN, OUTPUT);

  allOutputsOff();

  loadSettings();

  sensorSerial.begin(
      SENSOR_BAUD,
      SERIAL_8N1,
      SENSOR_RX_PIN,
      SENSOR_TX_PIN
  );

  while (sensorSerial.available() > 0)
  {
    sensorSerial.read();
  }

  Serial.println();
  Serial.println("==================================");
  Serial.println(" A02YYUW WATER LEVEL INDICATOR");
  Serial.println("==================================");

  Serial.print("Full distance:  ");
  Serial.print(fullDistanceMM, 1);
  Serial.println(" mm");

  Serial.print("Empty distance: ");
  Serial.print(emptyDistanceMM, 1);
  Serial.println(" mm");

  startupTest();

  startWiFi();

  Serial.println();
  Serial.println("Waiting for A02YYUW data...");
  Serial.println("Serial Monitor speed: 115200 baud");

  lastValidReadingTime = millis();
}

// =====================================================
// MAIN LOOP
// =====================================================

void loop()
{
  if (dnsServerStarted)
  {
    dnsServer.processNextRequest();
  }

  server.handleClient();

  maintainWiFiConnection();

  uint16_t rawDistanceMM;

  if (readA02YYUW(rawDistanceMM))
  {
    lastValidReadingTime = millis();

    sensorReadingValid = true;

    currentRawDistanceMM = rawDistanceMM;

    currentFilteredDistanceMM =
        medianDistance(rawDistanceMM);

    currentWaterLevelPercent =
        calculateWaterLevelPercent(
            currentFilteredDistanceMM
        );

    currentState =
        determineLevelState(
            currentWaterLevelPercent
        );

    updateOutputs(currentState);

    if (millis() - lastSerialPrintTime >=
        SERIAL_PRINT_TIME_MS)
    {
      lastSerialPrintTime = millis();

      Serial.print("Raw: ");
      Serial.print(currentRawDistanceMM);

      Serial.print(" mm | Filtered: ");
      Serial.print(currentFilteredDistanceMM);

      Serial.print(" mm | Level: ");
      Serial.print(currentWaterLevelPercent, 1);

      Serial.print("% | ");
      Serial.println(stateToText(currentState));
    }
  }

  if (millis() - lastValidReadingTime >=
      SENSOR_WARNING_TIME_MS)
  {
    sensorReadingValid = false;

    allOutputsOff();

    if (millis() - lastErrorPrintTime >= 1000)
    {
      lastErrorPrintTime = millis();

      Serial.println(
          "ERROR: No valid sensor data. "
          "Check VCC, GND, and sensor TX -> GPIO16."
      );
    }
  }

  delay(2);
} 