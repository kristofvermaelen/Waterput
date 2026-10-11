#include <WiFiS3.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ============================================================
// WiFi configuration
// ============================================================

const char* wifiSsid = "";
const char* wifiPassword = "";

WiFiServer server(80);

// ============================================================
// LCD
// ============================================================

LiquidCrystal_I2C lcd(0x27, 16, 2);

// Some LCD modules use 0x3F instead of 0x27.

// ============================================================
// Pins
// ============================================================

const int sensorPin = A0;
const int lcdButtonPin = 2;

// ============================================================
// ADC / sensor configuration
// ============================================================

// Keep 10-bit ADC resolution for compatibility with
// your previous calibration values.
const int adcResolutionBits = 10;
const int adcMaximum = 1023;

// Measure the actual voltage between 5V and GND
// on the UNO R4 and adjust this value if needed.
const float arduinoReferenceVoltage = 5.00;

// Current loop measurement resistor.
const float measurementResistorOhms = 220.0;

// Average 100 measurements.
const int numberOfMeasurements = 100;

// Existing calibration values.
// Recalibration on the UNO R4 is recommended.
const int adcEmpty = 196;
const int adcFull = 820;

// Tank configuration.
const float tankHeightCm = 250.0;
const float tankCapacityLiters = 10000.0;

// ============================================================
// Timing
// ============================================================

// Sensor measurement every 5 seconds.
const unsigned long sensorMeasurementIntervalMs = 5000UL;

// Refresh normal LCD display every 60 seconds.
const unsigned long lcdRefreshIntervalMs = 60000UL;

// LCD stays on for 5 minutes after a button press.
const unsigned long lcdOnTimeMs =
  5UL * 60UL * 1000UL;

// Button must be held for 5 seconds
// to show WiFi information.
const unsigned long buttonLongPressMs = 5000UL;

// Show WiFi information for 20 seconds.
const unsigned long wifiInfoDurationMs = 20000UL;

// Switch between WiFi pages every 3 seconds.
const unsigned long wifiPageIntervalMs = 3000UL;

// Button debounce.
const unsigned long buttonDebounceMs = 40UL;

// ============================================================
// Current sensor values
// ============================================================

float currentAdcValue = 0.0;
float currentVoltage = 0.0;
float currentCurrentMa = 0.0;

float currentPercentage = 0.0;
float currentHeightCm = 0.0;
float currentHeightM = 0.0;
float currentLiters = 0.0;

bool sensorActive = false;

// ============================================================
// Timers
// ============================================================

unsigned long previousSensorMeasurementTime = 0;
unsigned long previousLcdRefreshTime = 0;

unsigned long lcdActivatedTime = 0;

unsigned long buttonPressedTime = 0;
unsigned long lastButtonChangeTime = 0;

unsigned long wifiInfoStartTime = 0;
unsigned long previousWifiPageTime = 0;

// ============================================================
// LCD state
// ============================================================

bool lcdBacklightOn = false;
bool showWifiInfo = false;
bool wifiInfoPage = false;

// ============================================================
// Button state
// ============================================================

bool lastRawButtonState = HIGH;
bool stableButtonState = HIGH;
bool buttonLongPressHandled = false;

// ============================================================
// Setup
// ============================================================

void setup() {
  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println(
    "Water Tank Monitor - UNO R4 WiFi"
  );
  Serial.println(
    "--------------------------------"
  );

  // ----------------------------------------------------------
  // ADC
  // ----------------------------------------------------------

  analogReadResolution(adcResolutionBits);

  // ----------------------------------------------------------
  // Button
  // ----------------------------------------------------------

  pinMode(lcdButtonPin, INPUT_PULLUP);

  // ----------------------------------------------------------
  // LCD
  // ----------------------------------------------------------

  lcd.init();
  lcd.clear();
  lcd.noBacklight();

  // ----------------------------------------------------------
  // First sensor measurement
  // ----------------------------------------------------------

  updateSensorMeasurement();

  // ----------------------------------------------------------
  // WiFi
  // ----------------------------------------------------------

  connectToWiFi();

  // ----------------------------------------------------------
  // HTTP server
  // ----------------------------------------------------------

  server.begin();

  Serial.println("HTTP server started.");
  Serial.println();
}

// ============================================================
// Main loop
// ============================================================

void loop() {
  maintainWiFiConnection();

  updateSensorAtInterval();

  handleLcdButton();

  updateLcdAtInterval();

  handleHttpClient();
}

// ============================================================
// WiFi
// ============================================================

void connectToWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(wifiSsid);

  while (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(wifiSsid, wifiPassword);

    unsigned long connectionStart =
      millis();

    while (
      WiFi.status() != WL_CONNECTED &&
      millis() - connectionStart < 15000UL
    ) {
      delay(500);
      Serial.print(".");
    }

    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println(
        "WiFi connection failed. Retrying..."
      );

      delay(3000);
    }
  }

  Serial.println("WiFi connected.");

  printNetworkInformation();
}

void maintainWiFiConnection() {
  static unsigned long
    previousReconnectAttempt = 0;

  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  if (
    millis() - previousReconnectAttempt <
    10000UL
  ) {
    return;
  }

  previousReconnectAttempt = millis();

  Serial.println(
    "WiFi disconnected. Reconnecting..."
  );

  WiFi.begin(wifiSsid, wifiPassword);
}

void printNetworkInformation() {
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  Serial.print("Signal strength: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");

  Serial.println();

  Serial.print("Open in browser: http://");
  Serial.println(WiFi.localIP());
}

// ============================================================
// Sensor
// ============================================================

float readAverageAdc() {
  // Discard first ADC reading.
  analogRead(sensorPin);

  delay(5);

  unsigned long total = 0;

  for (
    int i = 0;
    i < numberOfMeasurements;
    i++
  ) {
    total += analogRead(sensorPin);

    delay(2);
  }

  return
    (float)total /
    numberOfMeasurements;
}

void updateSensorMeasurement() {
  currentAdcValue = readAverageAdc();

  currentVoltage =
    currentAdcValue *
    arduinoReferenceVoltage /
    adcMaximum;

  currentCurrentMa =
    (
      currentVoltage /
      measurementResistorOhms
    ) *
    1000.0;

  // Consider sensor active above about 3 mA.
  sensorActive =
    currentCurrentMa >= 3.0;

  float calibrationRange =
    adcFull - adcEmpty;

  if (calibrationRange <= 0) {
    currentPercentage = 0.0;
    currentHeightCm = 0.0;
    currentHeightM = 0.0;
    currentLiters = 0.0;

    return;
  }

  currentPercentage =
    (
      (
        currentAdcValue -
        adcEmpty
      ) /
      calibrationRange
    ) *
    100.0;

  currentPercentage =
    constrain(
      currentPercentage,
      0.0,
      100.0
    );

  currentHeightCm =
    tankHeightCm *
    (
      currentPercentage /
      100.0
    );

  currentHeightM =
    currentHeightCm /
    100.0;

  currentLiters =
    tankCapacityLiters *
    (
      currentPercentage /
      100.0
    );

  printSensorMeasurement();
}

void updateSensorAtInterval() {
  unsigned long now = millis();

  if (
    now -
    previousSensorMeasurementTime >=
    sensorMeasurementIntervalMs
  ) {
    previousSensorMeasurementTime = now;

    updateSensorMeasurement();
  }
}

// ============================================================
// Serial output
// ============================================================

void printSensorMeasurement() {
  Serial.print("ADC: ");
  Serial.print(currentAdcValue, 1);

  Serial.print(" | Voltage: ");
  Serial.print(currentVoltage, 3);
  Serial.print(" V");

  Serial.print(" | Current: ");
  Serial.print(currentCurrentMa, 3);
  Serial.print(" mA");

  Serial.print(" | Water: ");
  Serial.print(currentPercentage, 1);
  Serial.print(" %");

  Serial.print(" | Height: ");
  Serial.print(currentHeightCm, 1);
  Serial.print(" cm");

  Serial.print(" | Volume: ");
  Serial.print(currentLiters, 0);
  Serial.println(" L");
}

// ============================================================
// Button
// ============================================================

void handleLcdButton() {
  bool rawButtonState =
    digitalRead(lcdButtonPin);

  unsigned long now = millis();

  // ----------------------------------------------------------
  // Detect raw button state change
  // ----------------------------------------------------------

  if (
    rawButtonState !=
    lastRawButtonState
  ) {
    lastButtonChangeTime = now;

    lastRawButtonState =
      rawButtonState;
  }

  // ----------------------------------------------------------
  // Debounce
  // ----------------------------------------------------------

  if (
    now -
    lastButtonChangeTime >=
    buttonDebounceMs
  ) {
    if (
      stableButtonState !=
      rawButtonState
    ) {
      stableButtonState =
        rawButtonState;

      // ------------------------------------------------------
      // Button pressed
      // ------------------------------------------------------

      if (
        stableButtonState ==
        LOW
      ) {
        buttonPressedTime = now;

        buttonLongPressHandled =
          false;

        // Short press immediately
        // activates the LCD.
        turnLcdOn();
      }

      // ------------------------------------------------------
      // Button released
      // ------------------------------------------------------

      if (
        stableButtonState ==
        HIGH
      ) {
        buttonPressedTime = 0;

        buttonLongPressHandled =
          false;
      }
    }
  }

  // ----------------------------------------------------------
  // Long press
  // ----------------------------------------------------------

  if (
    stableButtonState == LOW &&
    !buttonLongPressHandled &&
    buttonPressedTime > 0 &&
    now -
    buttonPressedTime >=
    buttonLongPressMs
  ) {
    buttonLongPressHandled = true;

    showWifiInformation();
  }

  // ----------------------------------------------------------
  // Update WiFi diagnostic display
  // ----------------------------------------------------------

  if (showWifiInfo) {
    updateWifiInformationDisplay();

    if (
      now -
      wifiInfoStartTime >=
      wifiInfoDurationMs
    ) {
      showWifiInfo = false;

      updateLcdDisplay();

      previousLcdRefreshTime =
        now;
    }
  }

  // ----------------------------------------------------------
  // Turn LCD off after 5 minutes
  // ----------------------------------------------------------

  if (
    lcdBacklightOn &&
    now -
    lcdActivatedTime >=
    lcdOnTimeMs
  ) {
    turnLcdOff();
  }
}

// ============================================================
// LCD on / off
// ============================================================

void turnLcdOn() {
  lcdBacklightOn = true;

  lcdActivatedTime = millis();

  lcd.backlight();

  // Do not overwrite WiFi screen
  // when already showing WiFi info.
  if (!showWifiInfo) {
    updateLcdDisplay();
  }

  previousLcdRefreshTime =
    millis();

  Serial.println(
    "LCD ON for 5 minutes."
  );
}

void turnLcdOff() {
  lcd.noBacklight();

  lcdBacklightOn = false;

  showWifiInfo = false;

  Serial.println("LCD OFF.");
}

// ============================================================
// WiFi information mode
// ============================================================

void showWifiInformation() {
  lcd.backlight();

  lcdBacklightOn = true;

  // Restart 5-minute LCD timer.
  lcdActivatedTime = millis();

  showWifiInfo = true;

  wifiInfoStartTime = millis();

  previousWifiPageTime = 0;

  wifiInfoPage = false;

  Serial.println(
    "Showing WiFi information."
  );

  updateWifiInformationDisplay();
}

void updateWifiInformationDisplay() {
  unsigned long now = millis();

  if (
    previousWifiPageTime != 0 &&
    now -
    previousWifiPageTime <
    wifiPageIntervalMs
  ) {
    return;
  }

  previousWifiPageTime = now;

  wifiInfoPage =
    !wifiInfoPage;

  lcd.clear();

  // ----------------------------------------------------------
  // WiFi connected
  // ----------------------------------------------------------

  if (
    WiFi.status() ==
    WL_CONNECTED
  ) {
    if (wifiInfoPage) {
      // Page 1:
      // Connection status + SSID

      lcd.setCursor(0, 0);
      lcd.print("WiFi: CONNECTED");

      lcd.setCursor(0, 1);
      lcd.print("SSID:");

      String ssid =
        WiFi.SSID();

      // 16-char LCD:
      // "SSID:" uses 5 chars.
      if (ssid.length() > 11) {
        ssid =
          ssid.substring(0, 11);
      }

      lcd.print(ssid);
    } else {
      // Page 2:
      // IP address

      lcd.setCursor(0, 0);
      lcd.print("IP address:");

      lcd.setCursor(0, 1);
      lcd.print(
        WiFi.localIP()
      );
    }
  }

  // ----------------------------------------------------------
  // WiFi disconnected
  // ----------------------------------------------------------

  else {
    if (wifiInfoPage) {
      lcd.setCursor(0, 0);
      lcd.print("WiFi: OFFLINE");

      lcd.setCursor(0, 1);
      lcd.print("SSID:");

      String ssid =
        wifiSsid;

      if (ssid.length() > 11) {
        ssid =
          ssid.substring(0, 11);
      }

      lcd.print(ssid);
    } else {
      lcd.setCursor(0, 0);
      lcd.print("No connection");

      lcd.setCursor(0, 1);
      lcd.print("No IP address");
    }
  }
}

// ============================================================
// Normal LCD display
// ============================================================

void updateLcdAtInterval() {
  if (!lcdBacklightOn) {
    return;
  }

  // Do not overwrite WiFi information.
  if (showWifiInfo) {
    return;
  }

  unsigned long now = millis();

  if (
    now -
    previousLcdRefreshTime >=
    lcdRefreshIntervalMs
  ) {
    previousLcdRefreshTime = now;

    updateLcdDisplay();
  }
}

void updateLcdDisplay() {
  if (!lcdBacklightOn) {
    return;
  }

  lcd.clear();

  // ----------------------------------------------------------
  // Line 1
  // Example:
  // 13% 1298L
  // ----------------------------------------------------------

  lcd.setCursor(0, 0);

  lcd.print(
    currentPercentage,
    0
  );

  lcd.print("% ");

  lcd.print(
    currentLiters,
    0
  );

  lcd.print("L");

  // ----------------------------------------------------------
  // Line 2
  // Example:
  // Height: 33cm
  // ----------------------------------------------------------

  lcd.setCursor(0, 1);

  lcd.print("Height: ");

  lcd.print(
    currentHeightCm,
    0
  );

  lcd.print("cm");
}

// ============================================================
// HTTP server
// ============================================================

void handleHttpClient() {
  WiFiClient client =
    server.available();

  if (!client) {
    return;
  }

  unsigned long connectionStart =
    millis();

  bool emptyLineReceived = false;

  while (
    client.connected() &&
    millis() -
    connectionStart <
    1000UL
  ) {
    if (client.available()) {
      char c = client.read();

      if (
        c == '\n' &&
        emptyLineReceived
      ) {
        sendJsonResponse(client);

        break;
      }

      if (c == '\n') {
        emptyLineReceived = true;
      } else if (c != '\r') {
        emptyLineReceived = false;
      }
    }
  }

  delay(1);

  client.stop();
}

// ============================================================
// JSON response
// ============================================================

void sendJsonResponse(
  WiFiClient& client
) {
  client.println(
    "HTTP/1.1 200 OK"
  );

  client.println(
    "Content-Type: application/json"
  );

  client.println(
    "Connection: close"
  );

  client.println(
    "Cache-Control: no-cache"
  );

  client.println();

  client.println("{");

  // ----------------------------------------------------------
  // Device
  // ----------------------------------------------------------

  client.println(
    "  \"device\": {"
  );

  client.println(
    "    \"name\": \"water_tank\","
  );

  client.println(
    "    \"version\": \"2.1-r4wifi\","
  );

  client.print(
    "    \"uptime_seconds\": "
  );

  client.println(
    millis() / 1000UL
  );

  client.println("  },");

  // ----------------------------------------------------------
  // Calibration
  // ----------------------------------------------------------

  client.println(
    "  \"calibration\": {"
  );

  client.print(
    "    \"adc_empty\": "
  );

  client.print(adcEmpty);

  client.println(",");

  client.print(
    "    \"adc_full\": "
  );

  client.print(adcFull);

  client.println(",");

  client.print(
    "    \"valid\": "
  );

  client.println(
    adcFull > adcEmpty ?
    "true" :
    "false"
  );

  client.println("  },");

  // ----------------------------------------------------------
  // Sensor
  // ----------------------------------------------------------

  client.println(
    "  \"sensor\": {"
  );

  client.print(
    "    \"adc\": "
  );

  client.print(
    currentAdcValue,
    1
  );

  client.println(",");

  client.print(
    "    \"voltage\": "
  );

  client.print(
    currentVoltage,
    3
  );

  client.println(",");

  client.print(
    "    \"current_mA\": "
  );

  client.print(
    currentCurrentMa,
    3
  );

  client.println(",");

  client.print(
    "    \"active\": "
  );

  client.println(
    sensorActive ?
    "true" :
    "false"
  );

  client.println("  },");

  // ----------------------------------------------------------
  // WiFi
  // ----------------------------------------------------------

  client.println(
    "  \"wifi\": {"
  );

  client.print(
    "    \"connected\": "
  );

  client.println(
    WiFi.status() ==
    WL_CONNECTED ?
    "true," :
    "false,"
  );

  client.print(
    "    \"rssi\": "
  );

  client.println(
    WiFi.status() ==
    WL_CONNECTED ?
    WiFi.RSSI() :
    0
  );

  client.println("  },");

  // ----------------------------------------------------------
  // Water
  // ----------------------------------------------------------

  client.println(
    "  \"water\": {"
  );

  client.print(
    "    \"percentage\": "
  );

  client.print(
    currentPercentage,
    0
  );

  client.println(",");

  client.print(
    "    \"height_cm\": "
  );

  client.print(
    currentHeightCm,
    0
  );

  client.println(",");

  client.print(
    "    \"height_m\": "
  );

  client.print(
    currentHeightM,
    3
  );

  client.println(",");

  client.print(
    "    \"liters\": "
  );

  client.println(
    currentLiters,
    0
  );

  client.println("  }");

  client.println("}");
}
