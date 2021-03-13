/*




*/

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <SD.h>
#include <vector>
#include <WiFi.h>
#include <HTTPClient.h>
#include "time.h"
#include <Adafruit_NeoPixel.h>
#include "utilities.h" // Local

#define ARDUINOJSON_USE_LONG_LONG 1
#include <ArduinoJson.h>

// Pins.
#define PIN_LCD_BACKLIGHT_PWM 21
#define PIN_SD_CHIP_SELECT 15
#define PIN_LED_NEOPIXEL_MATRIX 12

// GLCD.
TFT_eSPI tft = TFT_eSPI();

const float peRatioNA = 0.0;

// Time.
struct tm timeinfo;
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -5 * 60 * 60;
const int daylightOffset_sec = 3600;

// WiFi
struct WifiCredentials
{
  String ssid;
  String password;
};

const char *parametersFilePath = "/parameters.json";
unsigned int symbolSelect = 0;

// Neopixel Matrix.
Adafruit_NeoPixel matrix = Adafruit_NeoPixel(16, PIN_LED_NEOPIXEL_MATRIX, NEO_GRB + NEO_KHZ800);

struct SymbolData
{
  String symbol;
  String companyName;
  float openPrice;
  float currentPrice;
  float change;
  float changePercent;
  float peRatio;
  float week52High;
  float week52Low;
  unsigned long long lastUpdate;
  String errorString;
};

struct Api
{
  String provider;
  String key;
  int maxRequestsPerMinute;
};
struct Display
{
  int nextSymbolDelay;
  int brightnessMax;
  int brightnessMin;
  int dimStartHour;
  int dimEndHour;
};
struct Matrix
{
  String marketHoursPattern;
  String afterHoursPattern;
};

struct System
{
  String timeZone;
};

struct Parameters
{
  std::vector<SymbolData> symbolData;
  std::vector<WifiCredentials> wifiCredentials;
  Api api;
  Display display;
  Matrix matrix;
  System system;

} parameters;

enum class LabelsIds
{
  Wifi,
  SD,
  Api,
  Clock

};

struct Status
{
  bool wifi;
  bool sd;
  bool api;
  bool time;
  bool symbolLocked;
  bool requestInProgess;

  bool operator!=(Status const &s)
  {
    return (wifi != s.wifi ||
            sd != s.sd ||
            api != s.api ||
            time != s.time ||
            symbolLocked != s.symbolLocked ||
            requestInProgess != s.requestInProgess);
  }

} status;

enum class ErrorIDs
{
  SdFailed,
  ParametersFailed
};

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void Error(ErrorIDs errorId)
{
  tft.setTextSize(4);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.drawString("ERROR!", 30, 20);

  if (errorId == ErrorIDs::SdFailed)
  {
    tft.drawString("SD Card", 30, 90);
    tft.drawString("not found.", 30, 130);
  }

  if (errorId == ErrorIDs::ParametersFailed)
  {
    tft.drawString("SD Card", 30, 90);
    tft.drawString("parameters", 30, 130);
    tft.drawString("are invalid.", 30, 170);
  }

  while (1)
    ;
}

void UpdateNeoPixelMatrix()
{

  for (int i = 0; i < matrix.numPixels(); i++)
  {
    if (random(0, 2) == 0)

      matrix.setPixelColor(i, matrix.Color(255, 0, 0));
    else
      matrix.setPixelColor(i, matrix.Color(0, 255, 0));
  }

  matrix.show();
}

bool InitSDCard()
{
  int count = 0;

  Serial.println("SD: Attempting to mount SD card...");

  while (!SD.begin(PIN_SD_CHIP_SELECT))
  {
    if (++count > 5)
    {
      Serial.println("SD: Card Mount Failed.");
      return false;
    }
    delay(250);
  }

  Serial.println("SD: SD card mounted.");
  return true;
}

bool GetParametersFromSDCard()
{
  File file = SD.open(parametersFilePath);

  Serial.printf("SD: Attempting to fetch parameters from %s...\n", parametersFilePath);

  if (!file)
  {
    Serial.printf("SD: Failed to open file: %s\n", parametersFilePath);
    file.close();
    return false;
  }

  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, file.readString());

  if (error)
  {
    Serial.print(F("DeserializeJson() failed: "));
    Serial.println(error.c_str());
    return false;
  }

  // TODO: check for valid parameters.json file.
  // Check if expected parameters exist, if not alert user, and provide default file example.

  for (int i = 0; i < doc["symbols"].size(); i++)
  {
    SymbolData sData;
    sData.symbol = doc["symbols"][i].as<String>();
    parameters.symbolData.push_back(sData);
  }

  for (int i = 0; i < doc["wifiCredentials"].size(); i++)
  {
    WifiCredentials wC;
    wC.ssid = doc["wifiCredentials"][i]["ssid"].as<String>();
    wC.password = doc["wifiCredentials"][i]["password"].as<String>();
    parameters.wifiCredentials.push_back(wC);
  }

  parameters.api.provider = doc["api"]["apiProvider"].as<String>();
  parameters.api.key = doc["api"]["apiKey"].as<String>();
  parameters.api.maxRequestsPerMinute = doc["api"]["apiMaxRequestsPerMinute"].as<int>();
  parameters.display.nextSymbolDelay = doc["display"]["nextSymbolDelay"].as<int>();
  parameters.display.brightnessMax = doc["display"]["brightnessMax"].as<int>();
  parameters.display.brightnessMin = doc["display"]["brightnessMin"].as<int>();
  parameters.display.dimStartHour = doc["display"]["dimStartHour"].as<int>();
  parameters.display.dimEndHour = doc["display"]["dimEndHour"].as<int>();
  parameters.matrix.marketHoursPattern = doc["matrix"]["marketHoursPattern"].as<int>();
  parameters.matrix.afterHoursPattern = doc["matrix"]["afterHoursPattern"].as<int>();
  parameters.system.timeZone = doc["system"]["timeZone"].as<String>();

  // Conform parameters into acceptable ranges.

  if (parameters.display.nextSymbolDelay < 1)
  {
    parameters.display.nextSymbolDelay = 1;
  }

  file.close();
  return true;
}

// Solution provided by : https://github.com/Bodmer/TFT_eSPI/issues/6
void charBounds(char c, int16_t *x, int16_t *y,
                int16_t *minx, int16_t *miny, int16_t *maxx, int16_t *maxy)
{
  if (c == '\n')
  {                         // Newline?
    *x = 0;                 // Reset x to zero,
    *y += tft.textsize * 8; // advance y one line
                            // min/max x/y unchaged -- that waits for next 'normal' character
  }
  else if (c != '\r')
  { // Normal char; ignore carriage returns
    if (/*wrap*/ false && ((*x + tft.textsize * 6) > tft.width()))
    {                         // Off right?
      *x = 0;                 // Reset x to zero,
      *y += tft.textsize * 8; // advance y one line
    }
    int x2 = *x + tft.textsize * 6 - 1, // Lower-right pixel of char
        y2 = *y + tft.textsize * 8 - 1;
    if (x2 > *maxx)
      *maxx = x2; // Track max x, y
    if (y2 > *maxy)
      *maxy = y2;
    if (*x < *minx)
      *minx = *x; // Track min x, y
    if (*y < *miny)
      *miny = *y;
    *x += tft.textsize * 6; // Advance x one char
  }
}

// Solution provided by : https://github.com/Bodmer/TFT_eSPI/issues/6
void getTextBounds(const char *str, int16_t x, int16_t y,
                   int16_t *x1, int16_t *y1, uint16_t *w, uint16_t *h)
{
  uint8_t c; // Current character

  *x1 = x;
  *y1 = y;
  *w = *h = 0;

  int16_t minx = tft.width(), miny = tft.width(), maxx = -1, maxy = -1;

  while ((c = *str++))
    charBounds(c, &x, &y, &minx, &miny, &maxx, &maxy);

  if (maxx >= minx)
  {
    *x1 = minx;
    *w = maxx - minx + 1;
  }
  if (maxy >= miny)
  {
    *y1 = miny;
    *h = maxy - miny + 1;
  }
}

void DisplayIndicator(String string, int x, int y, uint16_t color)
{
  /*
  int16_t x1, y1;
  uint16_t w, h;
  const int offset = 2;
  tft.setTextSize(2);
  getTextBounds(string.c_str(), x, y, &x1, &y1, &w, &h);
  tft.fillRect(x - offset, y - offset, w + offset, h + offset, color);
  tft.setCursor(x, y);
  tft.setTextPadding(5);
  tft.setTextColor(TFT_BLACK, color);
  tft.print(string);
  */

  tft.setTextSize(2);
  tft.setTextDatum(CC_DATUM);
  tft.setTextColor(TFT_BLACK, color);
  int padding = tft.textWidth(string);
  tft.setTextPadding(padding + 10);
  tft.drawString(string.c_str(), x, y);
}

void UpdateIndicators(bool forceUpdate = false)
{
  static Status previousStatus;
  if (previousStatus != status || forceUpdate)
  {
    previousStatus = status;

    char buf[12];
    sprintf(buf, "%02u:%02u:%02u", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    //String timeString = String(timeinfo.tm_hour) + ":" + String(timeinfo.tm_min) + ":" + String(timeinfo.tm_sec);
    int y = 217;
    DisplayIndicator("SD", 40, y, status.sd ? TFT_GREEN : TFT_RED);
    DisplayIndicator("WIFI", 80, y, status.wifi ? TFT_GREEN : TFT_RED);
    DisplayIndicator("API", 150, y, status.api ? TFT_GREEN : TFT_RED);
    DisplayIndicator("L", 165, y, status.symbolLocked ? TFT_BLUE : TFT_BLACK);
    DisplayIndicator("R", 200, y, status.requestInProgess ? TFT_BLUE : TFT_BLACK);
    DisplayIndicator(String(buf), 270, y, status.time ? TFT_GREEN : TFT_RED);
  }
}

void DisplayStockData(SymbolData symbolData)
{
  char buf[32];
  const int indent = 10;

  tft.setTextFont(0);

  //tft.setFreeFont(&FreeMono9pt7b);

  // Frame.
  tft.drawRect(0, 0, tft.width(), tft.height(), TFT_WHITE);
  tft.drawFastHLine(0, 35, tft.width(), TFT_WHITE);
  tft.drawFastHLine(0, 205, tft.width(), TFT_WHITE);
  tft.drawFastVLine(105, 0, 35, TFT_WHITE);

  //////////////////////////////////////////////////////
  // Symbol.
  tft.setTextSize(3);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextPadding(tft.textWidth("12345"));
  tft.drawString(symbolData.symbol, 50, 7);

  // Name.
  tft.setTextSize(2);
  tft.setTextDatum(TL_DATUM);
  String name = symbolData.companyName;
  if (symbolData.companyName.length() > 15)
  {
    // name = stockData.companyName.substring(0, 9);
    tft.drawString(symbolData.companyName.substring(0, 14), indent + 110, 12);
    tft.drawPixel(300, 25, TFT_WHITE);
    tft.drawPixel(303, 25, TFT_WHITE);
    tft.drawPixel(306, 25, TFT_WHITE);
  }
  else
  {
    tft.drawString(symbolData.companyName, indent + 110, 12);
  }
  //////////////////////////////////////////////////////

  // Price.

  //////////////////////////////////////////////////////
  tft.setTextSize(6);
  tft.setTextDatum(TC_DATUM);
  if (symbolData.change < 0)
  {
    tft.setTextColor(TFT_RED, TFT_BLACK);
  }
  else if (symbolData.change > 0)
  {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
  }
  else
  {
    tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
  }
  tft.setTextPadding(tft.textWidth("12345.78"));

  sprintf(buf, "%4.2f", symbolData.currentPrice);
  //int tw = tft.textWidth(String(buf));

  tft.drawString(buf, tft.height() / 2, 50);
  //tft.fillRect(1, 50, (tft.height() - tw) / 2, 45, TFT_ORANGE);
  // tft.fillRect(tft.height() / 2 + tw / 2, 50, (tft.height() - tw) / 2 - 1, 45, TFT_ORANGE);

  // Change.
  tft.setTextSize(3);
  tft.setTextPadding(tft.textWidth("123.56"));
  sprintf(buf, "%1.2f", symbolData.change);
  tft.drawString(buf, 90, 110);

  // Percent change.
  /*
  if (symbolData.changePercent < 10)
    sprintf(buf, "%1.2f%%", symbolData.changePercent);
  else if (symbolData.changePercent < 100)
    sprintf(buf, "%2.1f%%", symbolData.changePercent);
  else
    sprintf(buf, "%3.0f%%", symbolData.changePercent);
    */
  tft.setTextPadding(tft.textWidth("-2345.67"));
  sprintf(buf, "%3.2f%%", symbolData.changePercent);
  tft.drawString(buf, tft.height() - 90, 110);
  //////////////////////////////////////////////////////

  // 52 week
  //////////////////////////////////////////////////////
  static int x52;
  int y = 143;
  tft.fillRect(x52, y, 5, 10, TFT_BLACK);
  x52 = mapFloat(symbolData.currentPrice, symbolData.week52Low, symbolData.week52High, 20, tft.height() - 20);
  tft.drawLine(20, y + 5, tft.height() - 20, y + 5, TFT_YELLOW);
  tft.fillRect(x52, y, 5, 10, TFT_YELLOW);
  //////////////////////////////////////////////////////

  // Extra data.
  //////////////////////////////////////////////////////

  tft.setTextSize(2);
  tft.setTextColor(TFT_BLUE, TFT_BLACK);
  tft.setTextPadding(0);
  tft.drawString("Update", 265, 160);
  tft.drawString("P/E", 50, 160);
  tft.drawString("Open", 150, 160);

  // PE.
  if (symbolData.peRatio == peRatioNA)
  {
    sprintf(buf, "N/A");
  }
  else
  {
    sprintf(buf, "%3.2f", symbolData.peRatio);
  }
  tft.setTextPadding(tft.textWidth("-123.56"));
  tft.drawString(buf, 50, 182);

  // Open.
  tft.setTextPadding(tft.textWidth("12345.78"));
  sprintf(buf, "%3.2f", symbolData.openPrice);
  tft.drawString(buf, 150, 182);

  // Update.
  tft.setTextPadding(0);
  time_t rawtime(symbolData.lastUpdate);
  sprintf(buf, "%02u:%02u:%02u", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
  tft.drawString(buf, 265, 182);
  //////////////////////////////////////////////////////
}

bool GetSymbolDataFromAPI(SymbolData *symbolData)
{
  String payload;
  //String host = "https://cloud.iexapis.com/stable/stock/" + symbolData->symbol + "/quote?token=" + parameters.apiKey;

  // TEMP SANDBOX
  String host = "https://sandbox.iexapis.com/stable/stock/" + symbolData->symbol + "/quote?token=Tpk_81853d40d7084179b6e722e84f44e148";

  Serial.print("API: Connecting to ");
  Serial.println(host);

  HTTPClient http;
  http.begin(host);
  int httpCode = http.GET();

  Serial.printf("WIFI: HTTP code: %i\n", httpCode);

  if (httpCode > 0)
  {
    payload = http.getString();
    Serial.println("API: [RESPONSE]");
    Serial.println(payload);
    http.end();

    if (httpCode != 200)
    {
      return false;
    }
  }
  else
  {
    Serial.print("WIFI: Connection failed, HTTP client code: ");
    Serial.println(httpCode);
    symbolData->errorString = String(httpCode);
    http.end();
    return false;
  }

  // Check for endpoint error messages.
  if (payload.equalsIgnoreCase("Unknown symbol"))
  {
    Serial.print("API: Error from endpoint: Unknown symbol");
    symbolData->errorString = "Unknown symbol";
    return false;
  }
  if (payload.equalsIgnoreCase("Forbidden"))
  {
    Serial.print("API: Error from endpoint: Forbidden");
    symbolData->errorString = "Forbidden";
    return false;
  }

  DynamicJsonDocument doc(2048);
  DeserializationError jsonError = deserializeJson(doc, payload);

  if (jsonError)
  {
    Serial.print(F("JSON: DeserializeJson() failed: "));
    Serial.println(jsonError.c_str());
    symbolData->errorString = "JSON: jsonError.c_str()";
    return false;
  }

  symbolData->companyName = doc["companyName"].as<String>();
  symbolData->openPrice = doc["previousClose"].as<float>();

  symbolData->change = doc["change"].as<float>();
  symbolData->changePercent = doc["changePercent"].as<float>();
  symbolData->week52High = doc["week52High"].as<float>();
  symbolData->week52High = doc["week52High"].as<float>();
  symbolData->week52Low = doc["week52Low"].as<float>();
  symbolData->lastUpdate = doc["iexLastUpdated"].as<long long>();

  // OTC stocks have different price locations. (?)
  if (doc["iexRealtimePrice"].is<float>())
  {
    symbolData->currentPrice = doc["iexRealtimePrice"].as<float>();
  }
  else
  {
    symbolData->currentPrice = doc["extendedPrice"].as<float>();
  }

  if (doc["peRatio"].is<float>())
  {
    symbolData->peRatio = doc["peRatio"].as<float>();
  }
  else
  {
    symbolData->peRatio = peRatioNA;
  }

  return true;
}

void GetSymbolData(void *)
{
  static unsigned long start = millis();

  while (1)
  {
    if (millis() - start > 2000)
    {
      start = millis();
      status.requestInProgess = true;

      // Get index symbol with oldest data.
      int selectedIndex = 0;
      for (int i = 0; i < parameters.symbolData.size(); i++)
      {
        if (parameters.symbolData[i].lastUpdate > parameters.symbolData[selectedIndex].lastUpdate)
        {
          selectedIndex = i;
        }
      }

      Serial.printf("API: Requesting data for symbol: %s\n", parameters.symbolData[selectedIndex].symbol.c_str());

      if (parameters.api.provider.equalsIgnoreCase("IEXCLOUD"))
      {
        status.api = GetSymbolDataFromAPI(&parameters.symbolData[selectedIndex]);
      }
      else
      {
        Serial.printf("API: Error, unknown API provider: %s\n", parameters.api.provider);
        status.api = false;
      }

      status.requestInProgess = false;
    }
  }

  vTaskDelete(NULL); // This will not occur, but kept in for awareness.
}

// Touch screen requires calibation, orientation may be inversed.
void CheckTouchScreen()
{
  //static takeTouchReadings = true;
  static unsigned long touchDebounceDelay = 250;
  static unsigned long touchDebounceMillis = millis();
  uint16_t x, y;

  if (millis() - touchDebounceMillis > touchDebounceDelay)
  {
    if (tft.getTouch(&x, &y, 40))
    {
      touchDebounceMillis = millis();

      if (x < tft.width() / 3)
      {
        symbolSelect++;
        if (symbolSelect > parameters.symbolData.size() - 1)
        {
          symbolSelect = 0;
        }
      }
      else if (x > (tft.width() / 3) * 2)
      {
        if (symbolSelect != 0)
        {
          symbolSelect--;
        }
        else
        {
          symbolSelect = parameters.symbolData.size() - 1;
        }
      }
      else
      {
        status.symbolLocked = !status.symbolLocked;
      }
    }
  }
}

bool ConnectWifi()
{
  int wifiCredentialsIndex = 0;

  while (1)
  {
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0, 0);
    tft.setTextSize(2);
    tft.setTextColor(TFT_GREEN);
    tft.printf("Connecting to WiFi\n");
    tft.printf("SSID: %s\n", parameters.wifiCredentials[wifiCredentialsIndex].ssid.c_str());
    tft.printf("Password: %s\n", parameters.wifiCredentials[wifiCredentialsIndex].password.c_str());

    Serial.printf("WIFI: Connecting to SSID: %s, with password: %s\n", parameters.wifiCredentials[wifiCredentialsIndex].ssid.c_str(), parameters.wifiCredentials[wifiCredentialsIndex].password.c_str());

    WiFi.begin(parameters.wifiCredentials[wifiCredentialsIndex].ssid.c_str(), parameters.wifiCredentials[wifiCredentialsIndex].password.c_str());

    int count = 0;
    while (count++ < 10)
    {
      delay(500);
      tft.print(".");
      Serial.print(".");

      if (WiFi.status() == WL_CONNECTED)
      {
        tft.fillScreen(TFT_BLACK);
        Serial.println("");
        Serial.printf("WIFI: WiFi connected to %s, device IP: %s\n", parameters.wifiCredentials[wifiCredentialsIndex].ssid.c_str(), WiFi.localIP().toString().c_str());
        return true;
      }
    }

    Serial.println("");

    wifiCredentialsIndex++;
    if (wifiCredentialsIndex > parameters.wifiCredentials.size() - 1)
    {
      wifiCredentialsIndex = 0;
    }
  }
}

bool GetTime()
{

  if (!getLocalTime(&timeinfo))
  {
    Serial.println("TIME: Failed to obtain time");
    return false;
  }

  UpdateIndicators(true);

  return true;
}

void setup()
{
  delay(500);
  Serial.begin(115200);
  Serial.println(F("\nQuoteBot starting up..."));

  tft.init();
  delay(50);
  tft.setRotation(3);
  delay(50);
  tft.fillScreen(TFT_BLACK);

  // LCD backlight PWM.
  ledcSetup(0, 5000, 8);
  ledcAttachPin(PIN_LCD_BACKLIGHT_PWM, 0);
  ledcWrite(0, 255);

  if (InitSDCard())
  {
    status.sd = true;
  }
  else
  {
    Error(ErrorIDs::SdFailed);
  }

  if (!GetParametersFromSDCard())
  {
    Error(ErrorIDs::ParametersFailed);
  }

  ConnectWifi();

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // TEMP TEST FILL
  for (auto &n : parameters.symbolData)
  {
    n.currentPrice = pow(10, random(1, 5));
  }

  xTaskCreate(
      GetSymbolData,   // Function that should be called
      "GetSymbolData", // Name of the task (for debugging)
      8192,            // Stack size (bytes)
      NULL,            // Parameter to pass
      1,               // Task priority
      NULL             // Task handle
  );
}

void loop()
{
  static unsigned long startSymbolSelect = millis();
  static unsigned long startDataRequest = millis();

  status.wifi = WiFi.status() == WL_CONNECTED;

  CheckTouchScreen();

  UpdateIndicators();

  // Increment selected symbol.
  if (millis() - startSymbolSelect > parameters.display.nextSymbolDelay * 1000)
  {
    startSymbolSelect = millis();
    if (!status.symbolLocked)
    {
      if (++symbolSelect > parameters.symbolData.size() - 1)
      {
        symbolSelect = 0;
      }
    }
  }

  // Update display with symbol data.
  static unsigned int previousSymbolSelect;
  if (previousSymbolSelect != symbolSelect)
  {
    previousSymbolSelect = symbolSelect;
    DisplayStockData(parameters.symbolData.at(symbolSelect));
  }

  // Fetch time.
  static unsigned long startGetTime = millis();
  if (millis() - startGetTime > 750)
  {
    startGetTime = millis();
    status.time = GetTime();
  }
}
