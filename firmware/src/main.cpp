

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <SD.h>
#include <vector>
#include <WiFi.h>
#include <HTTPClient.h>

#define ARDUINOJSON_USE_LONG_LONG 1
#include <ArduinoJson.h>

#define PIN_LCD_BACKLIGHT_PWM 21
#define PIN_SD_CHIP_SELECT 15

// Use hardware SPI
TFT_eSPI tft = TFT_eSPI();

const float peRatioNA = 0.0;

// WiFi

struct WifiCredentials
{
  String ssid;
  String password;
};

const char *parametersFilePath = "/parameters.json";
unsigned int symbolSelect = 0;

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

struct Parameters
{
  std::vector<SymbolData> symbolData;
  std::vector<WifiCredentials> wifiCredentials;
  String apiProvider;
  String apiKey;
  String timeZone;
  int apiMaxRequestsPerMinute;
  int nextSymbolDelay;
  int brightnessMax;
  int brightnessMin;
  int dimStartHour;
  int dimEndHour;
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

bool InitSDCard()
{
  int count = 0;

  Serial.println("SD Card: Attempting to mount SD card...");

  while (!SD.begin(PIN_SD_CHIP_SELECT))
  {
    if (++count > 5)
    {
      Serial.println("SD Card: Card Mount Failed.");
      return false;
    }
    delay(250);
  }

  Serial.println("SD Card: SD card mounted.");
  return true;
}

bool GetParametersFromSDCard()
{
  File file = SD.open(parametersFilePath);

  Serial.printf("SD Card: Attempting to fetch parameters from %s...\n", parametersFilePath);

  if (!file)
  {
    Serial.printf("SD Card: Failed to open file: %s\n", parametersFilePath);
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

  parameters.timeZone = doc["timeZone"].as<String>();
  parameters.apiProvider = doc["apiProvider"].as<String>();
  parameters.apiKey = doc["apiKey"].as<String>();
  parameters.apiMaxRequestsPerMinute = doc["apiMaxRequestsPerMinute"].as<int>();
  parameters.nextSymbolDelay = doc["nextSymbolDelay"].as<int>();
  parameters.brightnessMax = doc["brightnessMax"].as<int>();
  parameters.brightnessMin = doc["brightnessMin"].as<int>();
  parameters.dimStartHour = doc["dimStartHour"].as<int>();
  parameters.dimEndHour = doc["dimEndHour"].as<int>();

  // Conform parameters into acceptable ranges.

  if (parameters.nextSymbolDelay < 1)
  {
    parameters.nextSymbolDelay = 1;
  }

  file.close();
  return true;
}

float mapFloat(float x, float in_min, float in_max, float out_min, float out_max)
{
  const float dividend = out_max - out_min;
  const float divisor = in_max - in_min;
  const float delta = x - in_min;

  return (delta * dividend + (divisor / 2)) / divisor + out_min;
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
}

void UpdateIndicators()
{
  static Status previousStatus;
  if (previousStatus != status)
  {
    previousStatus = status;

    int y = 217;
    DisplayIndicator("SD", 10, y, status.sd ? TFT_GREEN : TFT_RED);
    DisplayIndicator("WIFI", 45, y, status.wifi ? TFT_GREEN : TFT_RED);
    DisplayIndicator("API", 110, y, status.api ? TFT_GREEN : TFT_RED);
    DisplayIndicator("L", 150, y, status.symbolLocked ? TFT_BLUE : TFT_BLACK);
    DisplayIndicator("R", 180, y, status.symbolLocked ? TFT_BLUE : TFT_BLACK);
    DisplayIndicator("12:23:12", 215, y, status.time ? TFT_GREEN : TFT_RED);
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

  // Symbol.
  tft.setTextSize(3);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  String spaced;
  if (symbolData.symbol.length() == 1)
    spaced = "  " + symbolData.symbol + "  ";
  else if (symbolData.symbol.length() == 2)
    spaced = " " + symbolData.symbol + "  ";
  else if (symbolData.symbol.length() == 3)
    spaced = " " + symbolData.symbol + " ";
  else if (symbolData.symbol.length() == 4)
    spaced = "" + symbolData.symbol + " ";
  else
    spaced = symbolData.symbol;
  tft.drawString(spaced, indent, 7);

  // Name.
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);

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

  // Price.
  tft.setTextSize(6);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  sprintf(buf, "  %4.2f  ", symbolData.currentPrice);
  int tw = tft.textWidth(String(buf));
  tft.drawString(buf, tft.height() / 2 - tw / 2, 50);

  // Change.
  tft.setTextSize(3);
  sprintf(buf, "%1.2f", symbolData.change);
  tft.drawString(buf, 40, 110);

  // Percent change.
  if (symbolData.changePercent < 10)
    sprintf(buf, "%1.2f%%", symbolData.changePercent);
  else if (symbolData.changePercent < 100)
    sprintf(buf, "%2.1f%%", symbolData.changePercent);
  else
    sprintf(buf, "%3.0f%%", symbolData.changePercent);
  tft.drawString(buf, 175, 110);

  // Extra data.
  tft.setTextSize(2);
  tft.setTextColor(TFT_BLUE, TFT_BLACK);
  //sprintf(buf, "Open: %3.2f", stockData.openPrice);
  //tft.drawString(buf, 10, 175);
  sprintf(buf, "%3.2f", symbolData.openPrice);
  tft.drawString("Open", 10, 160);
  tft.drawString(buf, 10, 182);

  tft.drawString("P/E", 120, 160);

  if (symbolData.peRatio == peRatioNA)
  {
    sprintf(buf, "   N/A   ");
  }
  else
  {
    sprintf(buf, "%3.2f", symbolData.peRatio);
  }
  tft.drawString(buf, 120, 182);

  sprintf(buf, "%3.2f", symbolData.lastUpdate);
  tft.drawString("Update", 220, 160);
  tft.drawString("12:01:35", 220, 182);

  // 52 week
  static int x52;
  int y = 143;
  tft.fillRect(x52, y, 5, 10, TFT_BLACK);
  x52 = mapFloat(symbolData.currentPrice, symbolData.week52Low, symbolData.week52High, 20, tft.height() - 20);
  tft.drawLine(20, y + 5, tft.height() - 20, y + 5, TFT_YELLOW);
  tft.fillRect(x52, y, 5, 10, TFT_YELLOW);
}

bool GetSymbolDataFromAPI(SymbolData *symbolData)
{
  String payload;
  String host = "https://cloud.iexapis.com/stable/stock/" + symbolData->symbol + "/quote?token=" + parameters.apiKey;

  Serial.print("API: Connecting to ");
  Serial.println(host);

  HTTPClient http;
  http.begin(host);
  int httpCode = http.GET();

  if (httpCode > 0)
  {
    Serial.print("WIFI: HTTP code: ");
    Serial.println(httpCode);
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

bool GetSymbolData(SymbolData *symbolData)
{

  status.requestInProgess = true;
  UpdateIndicators();

  Serial.printf("API: Requesting data for symbol: %s\n", symbolData->symbol.c_str());

  bool flag = GetSymbolDataFromAPI(symbolData);

  /*
  stockData->companyName = "First Magestic Silver Co.";
  stockData->openPrice = 16.365;
  stockData->currentPrice = 9.24 * symbolSelect;
  stockData->change = 12.34;
  stockData->changePercent = 12.34;
  stockData->peRatio = -68.08;
  stockData->week52High = 24.01;
  stockData->week52Low = 4.17;
  stockData->lastUpdate = 24135179;
  */
  status.requestInProgess = false;

  return flag;
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
        Serial.printf("WIFI: WiFi connected to %s\n", WiFi.localIP().toString().c_str());
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

void setup()
{

  Serial.begin(115200);

  tft.init();
  delay(50);
  tft.setRotation(3);
  delay(50);
  tft.fillScreen(TFT_BLACK);

  ledcSetup(0, 5000, 8);
  ledcAttachPin(PIN_LCD_BACKLIGHT_PWM, 0);
  ledcWrite(0, 255);

  if (!InitSDCard())
  {
    Error(ErrorIDs::SdFailed);
  }

  if (!GetParametersFromSDCard())
  {
    Error(ErrorIDs::ParametersFailed);
  }

  if (ConnectWifi())
  {
  }
  else
  {
    Serial.println("WiFi not connected.");
  }
}

void loop()
{
  static unsigned long startSymbolSelect = millis();
  static unsigned long startDataRequest = millis();

  CheckTouchScreen();

  UpdateIndicators();

  // Increment selected symbol.
  if (millis() - startSymbolSelect > parameters.nextSymbolDelay * 1000)
  {
    startSymbolSelect = millis();
    if (!status.symbolLocked)
    {
      if (++symbolSelect > parameters.symbolData.size() - 1)
      {
        symbolSelect = 0;
      }
    }

    // TEMP: figure out how to handle requests and track the timing.
    status.api = GetSymbolData(&parameters.symbolData[symbolSelect]);
  }

  // Update display with symbol data.
  static unsigned int previousSymbolSelect;
  if (previousSymbolSelect != symbolSelect)
  {
    previousSymbolSelect = symbolSelect;
    DisplayStockData(parameters.symbolData.at(symbolSelect));
  }

  // Update symbol data.
  if (millis() - startDataRequest > (60 / parameters.apiMaxRequestsPerMinute) * 1000)
  {
    startDataRequest = millis();

    //status.api = GetSymbolData(&parameters.symbolData[symbolSelect]);
  }
}
