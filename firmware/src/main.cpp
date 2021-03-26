/*
  QuoteBot
  Reuben Strangelove
  Spring 2021

  Fetch latest stock quotes and display the data on a graphical display.

  MCU:
    ESP32 (ESP32 DevKitV1)
  
  TFT:
    2.4" 320*240 touch TFT ILI9488 (Brand example: HiLetGo) with SD card slot.
  
  NeoPixels:
    4x4 WS2812b LED matrix.

  Supported API(s):
    https://iexcloud.io  

  TODO: 
    Check for market holiday.      
    Apply timezone offset to local time.   
    Add another API.

  History:

  VERSION   AUTHOR      DATE        NOTES
  =============================================================================
  0.0.0     ReubenStr   2021/13/3   Development phase.
  0.1.0     ReubenStr   2021/20/3   Pre-release, major functionality complete.

*/

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <SD.h>
#include <vector>
#include <list>

#include <WiFi.h>
#include <HTTPClient.h>
#include "time.h"
#include <Adafruit_NeoPixel.h>
#include "utilities.h"       // Local.
#include "tftMethods.h"      // Local.
#include "main.h"            // Local.
#include "neoPixelMethods.h" // Local.
#include "timeRange.h"       // Local.

#define ARDUINOJSON_USE_LONG_LONG 1
#include <ArduinoJson.h>

// TFT parameters are contained in platformio.ini
#define PIN_LCD_BACKLIGHT_PWM 21
#define PIN_SD_CHIP_SELECT 15
#define PIN_LED_NEOPIXEL_MATRIX 27

#define PWM_CHANNEL_LCD_BACKLIGHT 0

//
TFT_eSPI tft = TFT_eSPI();
Adafruit_NeoPixel matrix = Adafruit_NeoPixel(16, PIN_LED_NEOPIXEL_MATRIX, NEO_GRB + NEO_KHZ800);

// Time.
System sys;
Parameters parameters;
Status status;
MarketState marketState;

const char *parametersFilePath = "/parameters.json";
const float peRatioNA = 0.0;
bool isMarketHoliday = false;

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void Error(ErrorIDs errorId)
{
  const int yLine1 = 20;
  const int yLine2 = 90;
  const int yLine3 = 130;
  const int yLine4 = 170;

  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(4);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.drawString("ERROR!", 30, yLine1);

  if (errorId == ErrorIDs::SdFailed)
  {
    tft.drawString("SD Card", 30, yLine2);
    tft.drawString("not found.", 30, yLine3);
  }
  else if (errorId == ErrorIDs::ParametersFailed)
  {
    tft.drawString("SD Card", 30, yLine2);
    tft.drawString("parameters", 30, yLine3);
    tft.drawString("are invalid.", 30, yLine4);
  }
  else if (errorId == ErrorIDs::UnknownApi)
  {
    tft.drawString("Uknown", 30, yLine2);
    tft.drawString("API provider.", 30, yLine3);
  }
  else if (errorId == ErrorIDs::InvalidApiKey)
  {
    tft.drawString("Invalid", 30, yLine2);
    tft.drawString("API key.", 30, yLine3);
  }

  while (1)
    ;
}

auto sortByAbsFloat = [](float i, float j) {
  return abs(i) < abs(j);
};

void ProcessMatrix()
{
  // Check brightness.
  static int previousBrightness = matrix.getBrightness();
  int brightness = sys.time.matrixMaxBrightnessTimeRange.isTimeBetweenRange(
                       sys.time.currentTimeInfo.tm_hour, sys.time.currentTimeInfo.tm_min)
                       ? parameters.matrix.brightnessMax
                       : parameters.matrix.brightnessMin;
  if (previousBrightness != brightness)
  {
    Serial.printf("DISPLAY: matrix brightness changed from %u to %u.\n", previousBrightness, brightness);
    previousBrightness = brightness;
    matrix.setBrightness(brightness);
  }

  // Update pattern.
  static unsigned long start = millis();
  int delay = 1000;
  if (millis() - start > delay)
  {
    start = millis();

    String pattern =
        marketState == MarketState::Holiday       ? parameters.matrix.holidayPattern
        : marketState == MarketState::Weekend     ? parameters.matrix.weekendPattern
        : marketState == MarketState::PreHours    ? parameters.matrix.preMarketPattern
        : marketState == MarketState::MarketHours ? parameters.matrix.marketPattern
        : marketState == MarketState::AfterHours  ? parameters.matrix.afterMarketPattern
        : marketState == MarketState::Closed      ? parameters.matrix.closedPattern
                                                  : "";

    if (pattern.equalsIgnoreCase("TOP16"))
    {
      // Order change price data by magnitude.
      std::vector<float> changes;
      for (auto &symbolData : parameters.symbolData)
      {
        changes.push_back(symbolData.change);
      }
      sort(changes.begin(), changes.end(), sortByAbsFloat);

      for (int i = 0; i < matrix.numPixels() && i < changes.size(); i++)
      {
        if (changes[i] > 0)
        {
          matrix.setPixelColor(rotateMatrix(i), NeoGreen);
        }
        else if (changes[i] < 0)
        {
          matrix.setPixelColor(rotateMatrix(i), NeoRed);
        }
        else
        {
          matrix.setPixelColor(rotateMatrix(i), NeoOff);
        }
      }
    }
    else if (pattern.equalsIgnoreCase("RANDOMREDGREEN"))
    {
      for (int i = 0; i < matrix.numPixels(); i++)
      {
        if (random(0, 2) == 0)
          matrix.setPixelColor(i, NeoRed);
        else
          matrix.setPixelColor(i, NeoGreen);
      }
    }
    else if (pattern.equalsIgnoreCase("RAINBOW"))
    {
      static byte wheelPos = 0;
      wheelPos++;
      for (int i = 0; i < matrix.numPixels(); i++)
      {
        matrix.setPixelColor(rotateMatrix(i), Wheel(wheelPos + i * (255 / matrix.numPixels())));
      }
    }

    matrix.show();
  }
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

  String apiMode = doc["api"]["mode"].as<String>();
  parameters.api.provider = doc["api"]["provider"].as<String>();
  parameters.api.key = doc["api"]["key"].as<String>();
  parameters.api.maxRequestsPerDay = doc["api"]["maxRequestsPerDay"].as<int>();
  parameters.api.sandboxKey = doc["api"]["sandboxKey"].as<String>();
  parameters.api.sandboxMaxRequestsPerDay = doc["api"]["sandboxMaxRequestsPerDay"].as<int>();

  parameters.market.fetchPreMarketData = doc["market"]["fetchPreMarketData"].as<bool>();
  parameters.market.fetchMarketData = doc["market"]["fetchMarketData"].as<bool>();
  parameters.market.fetchAfterMarketData = doc["market"]["fetchAfterMarketData"].as<bool>();

  parameters.display.nextSymbolDelay = doc["display"]["nextSymbolDelay"].as<int>();
  parameters.display.brightnessMax = doc["display"]["brightnessMax"].as<int>();
  parameters.display.brightnessMin = doc["display"]["brightnessMin"].as<int>();
  sys.time.displayMaxBrightnessTimeRange.SetTimeRangeFromString(doc["display"]["maxBrightnessHours"].as<String>());

  parameters.matrix.holidayPattern = doc["matrix"]["holidayPattern"].as<String>();
  parameters.matrix.weekendPattern = doc["matrix"]["weekendPattern"].as<String>();
  parameters.matrix.preMarketPattern = doc["matrix"]["preMarketPattern"].as<String>();
  parameters.matrix.marketPattern = doc["matrix"]["marketPattern"].as<String>();
  parameters.matrix.afterMarketPattern = doc["matrix"]["afterMarketPattern"].as<String>();
  parameters.matrix.closedPattern = doc["matrix"]["closedPattern"].as<String>();
  parameters.matrix.brightnessMax = doc["matrix"]["brightnessMax"].as<int>();
  parameters.matrix.brightnessMin = doc["matrix"]["brightnessMin"].as<int>();
  sys.time.matrixMaxBrightnessTimeRange.SetTimeRangeFromString(doc["matrix"]["maxBrightnessHours"].as<String>());

  sys.time.timeZone = doc["system"]["timeZone"].as<String>();

  // Conform parameters into acceptable ranges.

  parameters.api.mode =
      apiMode.equalsIgnoreCase("DEMO")      ? ApiMode::Demo
      : apiMode.equalsIgnoreCase("SANDBOX") ? ApiMode::Sandbox
      : apiMode.equalsIgnoreCase("LIVE")    ? ApiMode::Live
                                            : ApiMode::Unknown;

  if (parameters.display.nextSymbolDelay < 1)
  {
    parameters.display.nextSymbolDelay = 1;
  }

  file.close();
  return true;
}

void DisplayIndicator(String string, int x, int y, uint16_t color)
{
  tft.setTextSize(2);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_BLACK, color);
  int padding = tft.textWidth(string);
  tft.setTextPadding(padding + 6);
  tft.drawFastHLine(x - ((padding + 6) / 2), y - 1, padding + 6, color);
  tft.drawFastHLine(x - ((padding + 6) / 2), y - 2, padding + 6, color);
  tft.drawString(string.c_str(), x, y);
}

void ProcessIndicators(bool forceUpdate = false)
{
  char buf[12];
  int y = 217;
  static Status previousStatus;
  if (previousStatus != status || forceUpdate)
  {
    previousStatus = status;

    sprintf(buf, "%02u:%02u", sys.time.currentTimeInfo.tm_hour, sys.time.currentTimeInfo.tm_min);

    DisplayIndicator("SD", 25, y, status.sd ? TFT_GREEN : TFT_RED);
    DisplayIndicator("WIFI", 75, y, status.wifi ? TFT_GREEN : TFT_RED);
    DisplayIndicator("API", 130, y, status.api ? TFT_GREEN : TFT_RED);
    DisplayIndicator("L", 165, y, status.symbolLocked ? TFT_BLUE : 0x0001);
    DisplayIndicator("R", 190, y, status.requestInProgess ? TFT_BLUE : 0x0001);
    DisplayIndicator(String(buf), 275, y, status.time ? TFT_GREEN : TFT_RED);
  }
}

void DisplayLayout()
{
  // Frame.
  tft.drawRect(0, 0, tft.width(), tft.height(), TFT_WHITE);
  tft.drawFastHLine(0, 35, tft.width(), TFT_WHITE);
  tft.drawFastHLine(0, 205, tft.width(), TFT_WHITE);
  tft.drawFastVLine(100, 0, 35, TFT_WHITE);
}

void DisplayBlank()
{
  tft.fillRect(101, 2, tft.height() - 102, 32, TFT_BLACK);        // Name area.
  tft.fillRect(1, 36, tft.height() - 2, 205 - 36 - 1, TFT_BLACK); // Center area
}

void DisplayStockData(SymbolData symbolData)
{
  char buf[32];
  tft.setTextFont(0);

  // Symbol.
  tft.setTextSize(3);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextPadding(tft.textWidth("12345"));
  tft.drawString(symbolData.symbol, 52, 7);

  if (symbolData.isValid)
  {
    // Company name.
    tft.setTextSize(2);
    tft.setTextDatum(TL_DATUM);
    String name = symbolData.companyName;
    if (symbolData.companyName.length() > 16)
    {
      tft.drawString(symbolData.companyName.substring(0, 15), 115, 12);
      tft.drawPixel(297, 25, TFT_WHITE);
      tft.drawPixel(300, 25, TFT_WHITE);
      tft.drawPixel(303, 25, TFT_WHITE);
    }
    else
    {
      tft.setTextPadding(tft.textWidth("12345678901234567"));
      tft.drawString(symbolData.companyName, 115, 12);
    }
    //////////////////////////////////////////////////////

    // Price.
    //////////////////////////////////////////////////////
    tft.setTextSize(6);
    tft.setTextDatum(TC_DATUM);

    if (marketState == MarketState::Holiday || marketState == MarketState::Weekend)
    {
      tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
    }
    else if (symbolData.change < 0)
    {
      tft.setTextColor(TFT_RED, TFT_BLACK);
    }
    else if (symbolData.change > 0)
    {
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
    }
    tft.setTextPadding(tft.textWidth("12345.78"));

    sprintf(buf, "%4.2f", symbolData.currentPrice);
    tft.drawString(buf, tft.height() / 2, 55);

    // Change.
    tft.setTextSize(3);
    tft.setTextPadding(tft.textWidth("123.56"));
    sprintf(buf, "%1.2f", symbolData.change);
    tft.drawString(buf, 90, 113);

    tft.setTextPadding(tft.textWidth("-2345.67"));
    sprintf(buf, "%3.2f%%", symbolData.changePercent * 100);
    tft.drawString(buf, tft.height() - 90, 113);
    //////////////////////////////////////////////////////

    // 52 week
    //////////////////////////////////////////////////////
    static int x52 = 20;
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
    tft.drawString("Update", 260, 160);
    tft.drawString("P/E", 50, 160);

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

    // Market state.
    tft.setTextPadding(tft.textWidth("Weekend"));
    static MarketState previousMarketState = MarketState::Unknown;    
    if (previousMarketState != marketState)
    {
      previousMarketState = marketState;
      tft.fillRect(90, 158, 120, 44, TFT_BLACK);  
    }
    if (marketStateDesciptionBottom[int(marketState)][0] != 0)
    {
      tft.drawString(marketStateDesciptionTop[int(marketState)], 150, 160);
      tft.drawString(marketStateDesciptionBottom[int(marketState)], 150, 182);
    }
    else
    {
      tft.drawString(marketStateDesciptionTop[int(marketState)], 150, 171);
    }

    // Update.
    tft.setTextPadding(0);
    time_t rawtime(symbolData.latestUpdate);
    sprintf(buf, "%02u:%02u", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min);
    tft.drawString(buf, 260, 182);
    //////////////////////////////////////////////////////
  }
  else
  {
    // Error message.
    DisplayBlank();
    tft.setTextSize(3);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("Invalid Symbol", tft.height() / 2, 65);
  }
}

bool GetSymbolDataFromApiIEXCLOUD(SymbolData *symbolData)
{
  String payload;
  String host;

  // API documentation: https://iexcloud.io/docs/api/#quote
  if (parameters.api.mode == ApiMode::Live)
  {
    host = "https://cloud.iexapis.com/stable/stock/" + symbolData->symbol + "/quote?token=" + parameters.api.key;
  }
  else if (parameters.api.mode == ApiMode::Sandbox)
  {
    host = "https://sandbox.iexapis.com/stable/stock/" + symbolData->symbol + "/quote?token=" + parameters.api.sandboxKey;
  }
  else
  {
    return false;
  }

  symbolData->lastApiCall = sys.time.currentEpoch;

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
      // Check for endpoint error messages.
      if (payload.equalsIgnoreCase("Unknown symbol"))
      {
        Serial.println("API: Error from endpoint: Unknown symbol");
        symbolData->errorString = "Unknown symbol";
        symbolData->isValid = false;
        return false;
      }
      else if (payload.equalsIgnoreCase("Forbidden"))
      {
        Serial.println("API: Error from endpoint: Forbidden");
        symbolData->errorString = "Forbidden";
        return false;
      }
      else if (payload.equalsIgnoreCase("The API key provided is not valid."))
      {
        Serial.println("API: Error from endpoint: The API key provided is not valid.");
        symbolData->errorString = "The API key provided is not valid.";
        Error(ErrorIDs::InvalidApiKey);
        return false;
      }

      // TODO: report more error codes. https://iexcloud.io/docs/api/#error-codes
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

  DynamicJsonDocument doc(2048);
  DeserializationError jsonError = deserializeJson(doc, payload);

  if (jsonError)
  {
    Serial.print(F("JSON: DeserializeJson() failed: "));
    Serial.println(jsonError.c_str());
    symbolData->errorString = "JSON: jsonError.c_str()";
    return false;
  }

  symbolData->currentPrice = doc["latestPrice"].as<float>();
  symbolData->companyName = doc["companyName"].as<String>();
  symbolData->openPrice = doc["previousClose"].as<float>();
  symbolData->change = doc["change"].as<float>();
  symbolData->changePercent = doc["changePercent"].as<float>();
  symbolData->week52High = doc["week52High"].as<float>();
  symbolData->week52High = doc["week52High"].as<float>();
  symbolData->week52Low = doc["week52Low"].as<float>();
  symbolData->latestUpdate = doc["latestUpdate"].as<long long>() / 1000; // convert milliseconds to seconds

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

// Executed as a RTOS task.
void GetSymbolData(void *)
{
  static unsigned long start = millis();

  if (millis() - start > sys.millisecondsBetweenApiCalls)
  {
    start = millis();

    // Get index of symbol with oldest api call time.
    int selectedIndex = 0;
    for (int i = 0; i < parameters.symbolData.size(); i++)
    {
      if (parameters.symbolData[i].isValid)
      {
        if (parameters.symbolData[i].lastApiCall < parameters.symbolData[selectedIndex].lastApiCall)
        {
          selectedIndex = i;
        }
      }
    }

    if (parameters.api.mode == ApiMode::Live || parameters.api.mode == ApiMode::Sandbox)
    {
      if ((marketState == MarketState::PreHours && parameters.market.fetchPreMarketData) ||
          (marketState == MarketState::MarketHours) ||
          (marketState == MarketState::AfterHours && parameters.market.fetchAfterMarketData) ||
          parameters.symbolData[selectedIndex].lastApiCall == 0)
      {

        Serial.printf("API: Requesting data for symbol: %s\n", parameters.symbolData[selectedIndex].symbol.c_str());
        status.requestInProgess = true;

        if (parameters.api.provider.equalsIgnoreCase("IEXCLOUD"))
        {
          status.api = GetSymbolDataFromApiIEXCLOUD(&parameters.symbolData[selectedIndex]);
        }
        else
        {
          Serial.printf("API: Error, unknown API provider: %s\n", parameters.api.provider.c_str());
          Error(ErrorIDs::UnknownApi);
        }

        status.requestInProgess = false;
      }
    }
    else if (parameters.api.mode == ApiMode::Demo)
    {
      // TODO: generate random data.
    }
  }

  vTaskDelete(NULL);
}

// Touch screen requires calibation, orientation may be inversed.
void ProcessTouchScreen()
{
  //static takeTouchReadings = true;
  static unsigned long touchDebounceDelay = 250;
  static unsigned long touchDebounceMillis = millis();
  uint16_t x, y;

  if (millis() - touchDebounceMillis > touchDebounceDelay)
  {
    if (tft.getTouch(&x, &y, 64))
    {
      touchDebounceMillis = millis();

      if (x < tft.width() / 3)
      {
        sys.symbolSelect++;
        if (sys.symbolSelect > parameters.symbolData.size() - 1)
        {
          sys.symbolSelect = 0;
        }
      }
      else if (x > (tft.width() / 3) * 2)
      {
        if (sys.symbolSelect != 0)
        {
          sys.symbolSelect--;
        }
        else
        {
          sys.symbolSelect = parameters.symbolData.size() - 1;
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
  const int yLine1 = 50;
  const int yLine2 = 70;
  const int yLine3 = 90;
  const int yLine4 = 110;
  const int yLine5 = 140;
  const int yLine6 = 160;

  char buf[128];
  int wifiCredentialsIndex = 0;

  while (1)
  {
    DisplayBlank();
    tft.setTextSize(2);
    tft.setTextColor(TFT_GREEN);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("Connecting to WiFi", 10, yLine1);
    sprintf(buf, "SSID: %s", parameters.wifiCredentials[wifiCredentialsIndex].ssid.c_str());
    tft.drawString(buf, 10, yLine2);
    sprintf(buf, "PWD: %s", parameters.wifiCredentials[wifiCredentialsIndex].password.c_str());
    tft.drawString(buf, 10, yLine3);

    Serial.printf("\nWIFI: Connecting to SSID: %s, with password: %s\n", parameters.wifiCredentials[wifiCredentialsIndex].ssid.c_str(), parameters.wifiCredentials[wifiCredentialsIndex].password.c_str());

    WiFi.begin(parameters.wifiCredentials[wifiCredentialsIndex].ssid.c_str(), parameters.wifiCredentials[wifiCredentialsIndex].password.c_str());

    tft.setCursor(10, yLine4);
    int count = 0;
    while (count++ < 10)
    {
      delay(500);
      tft.print(".");
      Serial.print(".");

      if (WiFi.status() == WL_CONNECTED)
      {
        tft.drawString("Connected!", 10, yLine5);
        sprintf(buf, "IP: %s", WiFi.localIP().toString().c_str());
        tft.drawString(buf, 10, yLine6);
        Serial.println("");
        Serial.printf("WIFI: WiFi connected to %s, device IP: %s\n", parameters.wifiCredentials[wifiCredentialsIndex].ssid.c_str(), WiFi.localIP().toString().c_str());
        delay(2000);
        DisplayBlank();
        return true;
      }
    }

    wifiCredentialsIndex++;
    if (wifiCredentialsIndex > parameters.wifiCredentials.size() - 1)
    {
      wifiCredentialsIndex = 0;
    }
  }
}

void CalcMillisecondsBetweenApiFetches()
{
  unsigned long delay = 0;

  if (parameters.api.mode == ApiMode::Live)
  {
    float apiSeconds = 0;
    if (parameters.market.fetchPreMarketData)
      apiSeconds += sys.time.preMarketTimeRange.GetTotalSeconds();
    if (parameters.market.fetchMarketData)
      apiSeconds += sys.time.marketTimeRange.GetTotalSeconds();
    if (parameters.market.fetchAfterMarketData)
      apiSeconds += sys.time.afterMarketTimeRange.GetTotalSeconds();
    delay = (apiSeconds / parameters.api.maxRequestsPerDay) * 1000;
  }
  else if (parameters.api.mode == ApiMode::Sandbox)
  {
    float apiSeconds = 24 * 60 * 60;
    delay = (apiSeconds / parameters.api.sandboxMaxRequestsPerDay) * 1000;
  }
  else if (parameters.api.mode == ApiMode::Demo)
  {
    delay = 1000;
  }
  else
  {
    delay = 60000;
  }

  sys.millisecondsBetweenApiCalls = delay;
}

bool ProcessTime()
{
  static unsigned long startGetTime = millis();

  if (millis() - startGetTime > 250)
  {
    startGetTime = millis();

    if (!getLocalTime(&sys.time.currentTimeInfo))
    {
      Serial.println("TIME: Failed to obtain time");
      status.time = false;
      return false;
    }

    status.time = true;

    time(&sys.time.currentEpoch); // Fetch current time as epoch

    static int previousMinute = ~sys.time.currentTimeInfo.tm_min;
    if (previousMinute != sys.time.currentTimeInfo.tm_min)
    {
      previousMinute = sys.time.currentTimeInfo.tm_min;
      ProcessIndicators(true);
    }
  }

  return true;
}

void ProcessMarketState()
{
  marketState = isMarketHoliday                                                                                                        ? MarketState::Holiday
                : sys.time.currentTimeInfo.tm_wday == int(DayIds::Sunday) || sys.time.currentTimeInfo.tm_wday == int(DayIds::Saturday) ? MarketState::Weekend
                : sys.time.preMarketTimeRange.isTimeBetweenRange(sys.time.currentTimeInfo.tm_hour, sys.time.currentTimeInfo.tm_min)    ? MarketState::PreHours
                : sys.time.marketTimeRange.isTimeBetweenRange(sys.time.currentTimeInfo.tm_hour, sys.time.currentTimeInfo.tm_min)       ? MarketState::MarketHours
                : sys.time.afterMarketTimeRange.isTimeBetweenRange(sys.time.currentTimeInfo.tm_hour, sys.time.currentTimeInfo.tm_min)  ? MarketState::AfterHours
                                                                                                                                       : MarketState::Closed;
}

void ProcessDisplayBrightness()
{
  static int previousBrightness = ledcRead(0);
  int brightness = sys.time.displayMaxBrightnessTimeRange.isTimeBetweenRange(sys.time.currentTimeInfo.tm_hour, sys.time.currentTimeInfo.tm_min) ? parameters.display.brightnessMax : parameters.display.brightnessMin;

  if (previousBrightness != brightness)
  {
    Serial.printf("DISPLAY: display brightness changed from %u to %u.\n", previousBrightness, brightness);
    previousBrightness = brightness;
    ledcWrite(PWM_CHANNEL_LCD_BACKLIGHT, brightness);
  }
}

void ProcessWifiCheck()
{
  // Check for WiFi connection, attempt reconnect after timeout.
  status.wifi = (WiFi.status() == WL_CONNECTED);
  static unsigned long startStatus = millis();
  if (millis() - startStatus > sys.wifiTimeoutUntilNewScan)
  {
    ConnectWifi();
  }
  if (status.wifi == true)
  {
    startStatus = millis();
  }
}

// Start API data fetch.
void ProcessAPIFetch()
{
  static unsigned long startFetch = 0;
  if (millis() - startFetch > 10000)
  {
    startFetch = millis();

    xTaskCreate(
        GetSymbolData,   // Function that should be called
        "GetSymbolData", // Name of the task (for debugging)
        8192,            // Stack size (bytes)
        NULL,            // Parameter to pass
        1,               // Task priority
        NULL             // Task handle
    );
  }
}

// Increment selected symbol periodically.
void ProcessSymbolIncrement()
{
  static unsigned long startSymbolSelect = millis();
  if (millis() - startSymbolSelect > parameters.display.nextSymbolDelay * 1000)
  {
    startSymbolSelect = millis();
    if (!status.symbolLocked)
    {
      if (++sys.symbolSelect > parameters.symbolData.size() - 1)
      {
        sys.symbolSelect = 0;
      }
    }
  }
}

// Update display after a API completes.
void ProcessDisplayUpdate()
{
  static bool previousRequestInProgess;
  static unsigned int previousSymbolSelect;
  if (previousRequestInProgess == true && status.requestInProgess == false)
  {
    previousSymbolSelect = !previousSymbolSelect;
  }
  previousRequestInProgess = status.requestInProgess;

  // Update display with symbol data.
  if (previousSymbolSelect != sys.symbolSelect)
  {
    previousSymbolSelect = sys.symbolSelect;
    DisplayStockData(parameters.symbolData.at(sys.symbolSelect));
  }
}

void setup()
{
  delay(500);
  Serial.begin(115200);
  Serial.println(F("\nQuoteBot starting up..."));

  matrix.setBrightness(0);
  matrix.begin();
  matrix.show();

  // LCD backlight PWM.
  ledcSetup(PWM_CHANNEL_LCD_BACKLIGHT, 5000, 8);
  ledcAttachPin(PIN_LCD_BACKLIGHT_PWM, PWM_CHANNEL_LCD_BACKLIGHT);
  ledcWrite(PWM_CHANNEL_LCD_BACKLIGHT, 255);

  tft.init();
  delay(50);
  tft.setRotation(1);
  delay(50);
  tft.fillScreen(TFT_BLACK);
  DisplayLayout();
  ProcessIndicators(true);

  CheckTouchCalibration(&tft, false);

  if (InitSDCard())
  {
    status.sd = true;
    ProcessIndicators();
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

  configTime(sys.time.gmtOffset_sec, sys.time.daylightOffset_sec, sys.time.ntpServer);

  sys.time.preMarketTimeRange = TimeRange(4, 0, 9, 29);
  sys.time.marketTimeRange = TimeRange(9, 30, 15, 59);
  sys.time.afterMarketTimeRange = TimeRange(16, 0, 21, 59);

  CalcMillisecondsBetweenApiFetches();

  Serial.printf("API: mode: %s\n", apiModeText[int(parameters.api.mode)]);
  Serial.printf("API: max api (live) fetches per day: %u\n", parameters.api.maxRequestsPerDay);
  Serial.printf("API: milliseconds per request: %lu\n", sys.millisecondsBetweenApiCalls);

}

void loop()
{
  ProcessDisplayBrightness();

  ProcessMarketState();

  ProcessTime();

  ProcessMatrix();

  ProcessTouchScreen();

  ProcessWifiCheck();

  ProcessAPIFetch();

  ProcessIndicators();

  ProcessSymbolIncrement();

  ProcessDisplayUpdate();
}
