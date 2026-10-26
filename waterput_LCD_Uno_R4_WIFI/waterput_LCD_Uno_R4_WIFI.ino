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

// Valid 4-20 mA loop range, with some margin.
// Below: cable break or no sensor power.
// Above: short circuit or sensor fault.
const float sensorMinValidMa = 3.0;
const float sensorMaxValidMa = 21.0;

// Value reported for water fields on sensor fault,
// so Loxone can raise an alarm instead of
// showing an empty tank.
const float sensorFaultValue = -1.0;

// Average 100 measurements.
const int numberOfMeasurements = 100;

// Discard the lowest and highest 10 samples of each burst
// to reject spikes (e.g. WiFi transmit noise).
const int numberOfTrimmedMeasurements = 10;

// Rolling average over the last 60 bursts
// (60 x 5 s = 5 minutes), to smooth out
// water surface movement and noise.
const int smoothingWindowSize = 60;

float smoothingWindow[smoothingWindowSize];
int smoothingWindowIndex = 0;
int smoothingWindowCount = 0;

// Trend on the LCD: compare the level now with
// the level 10 minutes ago (one sample per minute).
const int trendHistorySize = 11;
const unsigned long trendSampleIntervalMs = 60000UL;

// Minimum change in percentage points
// before the LCD shows a rising or falling arrow.
const float trendThresholdPercent = 1.0;

float trendHistory[trendHistorySize];
int trendHistoryIndex = 0;
int trendHistoryCount = 0;

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

// Rolling-average ADC value (5 minutes).
float currentAdcValue = 0.0;

// Latest burst, without the rolling average.
float currentRawAdcValue = 0.0;

float currentVoltage = 0.0;
float currentCurrentMa = 0.0;

float currentPercentage = 0.0;
float currentHeightCm = 0.0;
float currentHeightM = 0.0;
float currentLiters = 0.0;

bool sensorActive = false;

// -1 = falling, 0 = steady, 1 = rising.
int currentTrend = 0;

// ============================================================
// Timers
// ============================================================

unsigned long previousSensorMeasurementTime = 0;
unsigned long previousTrendSampleTime = 0;
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

  createTrendCharacters();

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

  updateTrendAtInterval();

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

  // Try once. If WiFi is unavailable, continue
  // so the LCD and button still work locally.
  // maintainWiFiConnection() keeps retrying.
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
      "WiFi connection failed. Continuing offline, will retry in background."
    );

    return;
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

int compareInts(
  const void* a,
  const void* b
) {
  return
    *(const int*)a -
    *(const int*)b;
}

float readTrimmedAverageAdc() {
  static int samples[numberOfMeasurements];

  // Discard first ADC reading.
  analogRead(sensorPin);

  delay(5);

  for (
    int i = 0;
    i < numberOfMeasurements;
    i++
  ) {
    samples[i] = analogRead(sensorPin);

    delay(2);
  }

  // Sort, then average without the lowest
  // and highest samples.
  qsort(
    samples,
    numberOfMeasurements,
    sizeof(int),
    compareInts
  );

  unsigned long total = 0;

  for (
    int i = numberOfTrimmedMeasurements;
    i < numberOfMeasurements - numberOfTrimmedMeasurements;
    i++
  ) {
    total += samples[i];
  }

  return
    (float)total /
    (
      numberOfMeasurements -
      2 * numberOfTrimmedMeasurements
    );
}

float addToSmoothingWindow(float value) {
  smoothingWindow[smoothingWindowIndex] = value;

  smoothingWindowIndex =
    (smoothingWindowIndex + 1) %
    smoothingWindowSize;

  if (smoothingWindowCount < smoothingWindowSize) {
    smoothingWindowCount++;
  }

  float total = 0.0;

  for (
    int i = 0;
    i < smoothingWindowCount;
    i++
  ) {
    total += smoothingWindow[i];
  }

  return total / smoothingWindowCount;
}

void resetSmoothingWindow() {
  smoothingWindowIndex = 0;
  smoothingWindowCount = 0;
}

float adcToMilliamps(float adc) {
  float voltage =
    adc *
    arduinoReferenceVoltage /
    adcMaximum;

  return
    (
      voltage /
      measurementResistorOhms
    ) *
    1000.0;
}

void updateSensorMeasurement() {
  currentRawAdcValue = readTrimmedAverageAdc();

  // Fault check on the latest burst, so a cable
  // break shows at once and not after 5 minutes.
  float rawCurrentMa =
    adcToMilliamps(currentRawAdcValue);

  sensorActive =
    rawCurrentMa >= sensorMinValidMa &&
    rawCurrentMa <= sensorMaxValidMa;

  if (sensorActive) {
    currentAdcValue =
      addToSmoothingWindow(currentRawAdcValue);
  } else {
    // Do not mix fault values into the average;
    // start a new average when the sensor is back.
    resetSmoothingWindow();

    currentAdcValue = currentRawAdcValue;
  }

  currentVoltage =
    currentAdcValue *
    arduinoReferenceVoltage /
    adcMaximum;

  currentCurrentMa =
    adcToMilliamps(currentAdcValue);

  if (!sensorActive) {
    currentPercentage = sensorFaultValue;
    currentHeightCm = sensorFaultValue;
    currentHeightM = sensorFaultValue;
    currentLiters = sensorFaultValue;

    printSensorMeasurement();

    return;
  }

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
// Trend
// ============================================================

void updateTrendAtInterval() {
  unsigned long now = millis();

  if (
    previousTrendSampleTime != 0 &&
    now -
    previousTrendSampleTime <
    trendSampleIntervalMs
  ) {
    return;
  }

  previousTrendSampleTime = now;

  if (!sensorActive) {
    trendHistoryIndex = 0;
    trendHistoryCount = 0;
    currentTrend = 0;

    return;
  }

  trendHistory[trendHistoryIndex] = currentPercentage;

  trendHistoryIndex =
    (trendHistoryIndex + 1) %
    trendHistorySize;

  if (trendHistoryCount < trendHistorySize) {
    trendHistoryCount++;
  }

  // Not enough history yet (first 10 minutes).
  if (trendHistoryCount < trendHistorySize) {
    currentTrend = 0;

    return;
  }

  // After the increment, the index points
  // at the oldest sample (10 minutes ago).
  float change =
    currentPercentage -
    trendHistory[trendHistoryIndex];

  if (change >= trendThresholdPercent) {
    currentTrend = 1;
  } else if (change <= -trendThresholdPercent) {
    currentTrend = -1;
  } else {
    currentTrend = 0;
  }
}

// ============================================================
// Serial output
// ============================================================

void printSensorMeasurement() {
  Serial.print("ADC: ");
  Serial.print(currentAdcValue, 1);

  Serial.print(" (burst ");
  Serial.print(currentRawAdcValue, 1);
  Serial.print(")");

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
// LCD trend characters
// ============================================================

// Custom LCD characters 0, 1 and 2:
// arrow up, arrow down and a steady dash.
byte trendUpChar[8] = {
  0b00100,
  0b01110,
  0b10101,
  0b00100,
  0b00100,
  0b00100,
  0b00100,
  0b00000
};

byte trendDownChar[8] = {
  0b00100,
  0b00100,
  0b00100,
  0b00100,
  0b10101,
  0b01110,
  0b00100,
  0b00000
};

byte trendSteadyChar[8] = {
  0b00000,
  0b00000,
  0b00000,
  0b11111,
  0b00000,
  0b00000,
  0b00000,
  0b00000
};

void createTrendCharacters() {
  lcd.createChar(0, trendUpChar);
  lcd.createChar(1, trendDownChar);
  lcd.createChar(2, trendSteadyChar);
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
  // Sensor fault
  // Example:
  // SENSOR ERROR
  // Loop: 0.12mA
  // ----------------------------------------------------------

  if (!sensorActive) {
    lcd.setCursor(0, 0);
    lcd.print("SENSOR ERROR");

    lcd.setCursor(0, 1);
    lcd.print("Loop: ");
    lcd.print(currentCurrentMa, 2);
    lcd.print("mA");

    return;
  }

  // ----------------------------------------------------------
  // Line 1
  // Example:
  // 13% 1298L      ^
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

  // Trend arrow in the last column:
  // rising, falling or steady over 10 minutes.
  lcd.setCursor(15, 0);

  if (currentTrend > 0) {
    lcd.write(byte(0));
  } else if (currentTrend < 0) {
    lcd.write(byte(1));
  } else {
    lcd.write(byte(2));
  }

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
    "    \"version\": \"2.3-r4wifi\","
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
    "    \"adc_raw\": "
  );

  client.print(
    currentRawAdcValue,
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

  client.print(
    currentLiters,
    0
  );

  client.println(",");

  client.print(
    "    \"trend\": "
  );

  client.println(
    currentTrend > 0 ?
    "\"rising\"" :
    currentTrend < 0 ?
    "\"falling\"" :
    "\"steady\""
  );

  client.println("  }");

  client.println("}");
}
