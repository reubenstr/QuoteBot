/*
  QuoteBot
  Reuben Strangelove
  Spring 2021

  Fetch latest stock quotes and display the data on a graphical display.

  MCU:
    ESP32 (ESP32 DevKitV1)
  
  TFT:
    320*240 touch TFT ILI9488 (Brand: HiLetGo) with SD card slot.
  
  NeoPixels:
    4x4 WS2812b LED matrix.

 Phase: 
  Development.
 

  TODO:

  determine market hours for matrix... and other possible indicators?
  determine weekend hours for price color change to magenta.
  after hours API halt.

    
  // get market holidays
  https://cloud.iexapis.com/stable/ref-data/us/dates/holiday/next?token=pk_967cbde31f9247b38d646068f3fb6a30

 
  History:

  VERSION   AUTHOR      DATE        NOTES
  =============================================================================
  0.0.0     ReubenStr   2021/13/3   Development phase.


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
#include "utilities.h"  // Local.
#include "tftMethods.h" // Local.
#include "main.h"       // Local.

#define ARDUINOJSON_USE_LONG_LONG 1
#include <ArduinoJson.h>

// Pins.
#define PIN_LCD_BACKLIGHT_PWM 21
#define PIN_SD_CHIP_SELECT 15
#define PIN_LED_NEOPIXEL_MATRIX 27

// Demo mode.
// const bool demoMode = true;

// GLCD.
TFT_eSPI tft = TFT_eSPI();
Adafruit_NeoPixel matrix = Adafruit_NeoPixel(16, PIN_LED_NEOPIXEL_MATRIX, NEO_GRB + NEO_KHZ800);

// Time.
struct tm timeinfo;
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -5 * 60 * 60;
const int daylightOffset_sec = 3600;

const unsigned long wifiTimeoutUntilNewScan = 30000; // milliseconds.
const char *parametersFilePath = "/parameters.json";
unsigned int symbolSelect = 0;
const float peRatioNA = 0.0;
const String errorUnknownSymbol = "Uknown symbol";
Parameters parameters;
Status status;
MarketState marketState;
bool isMarketHoliday = false;

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
  else if (errorId == ErrorIDs::ParametersFailed)
  {
    tft.drawString("SD Card", 30, 90);
    tft.drawString("parameters", 30, 130);
    tft.drawString("are invalid.", 30, 170);
  }
  else if (errorId == ErrorIDs::InvalidApiKey)
  {
    tft.drawString("Invalid", 30, 90);
    tft.drawString("API key.", 30, 130);
  }

  while (1)
    ;
}

void UpdateNeoPixelMatrix()
{
  static unsigned long start = millis();
  int delay = 1000;
  if (millis() - start > delay)
  {
    start = millis();

    if (parameters.matrix.marketHoursPattern.equalsIgnoreCase("TOP16"))
    {
    }
    else if (parameters.matrix.marketHoursPattern.equalsIgnoreCase("REDGREENCHECKER"))
    {
    }
    else if (parameters.matrix.marketHoursPattern.equalsIgnoreCase("RAINBOW"))
    {
    }

    for (int i = 0; i < matrix.numPixels(); i++)
    {
      if (random(0, 2) == 0)
        matrix.setPixelColor(i, matrix.Color(255, 0, 0));
      else
        matrix.setPixelColor(i, matrix.Color(0, 255, 0));
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

  parameters.api.provider = doc["api"]["apiProvider"].as<String>();
  parameters.api.key = doc["api"]["apiKey"].as<String>();
  parameters.api.maxRequestsPerMinute = doc["api"]["apiMaxRequestsPerMinute"].as<int>();
  parameters.market.fetchPreMarketData = doc["market"]["fetchPreMarketData"].as<int>();
  parameters.market.fetchAfterMarketData = doc["market"]["fetchAfterMarketData"].as<int>();
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

void UpdateIndicators(bool forceUpdate = false)
{
  char buf[12];
  int y = 217;
  static Status previousStatus;
  if (previousStatus != status || forceUpdate)
  {
    previousStatus = status;

    sprintf(buf, "%02u:%02u:%02u", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    DisplayIndicator("SD", 25, y, status.sd ? TFT_GREEN : TFT_RED);
    DisplayIndicator("WIFI", 75, y, status.wifi ? TFT_GREEN : TFT_RED);
    DisplayIndicator("API", 130, y, status.api ? TFT_GREEN : TFT_RED);
    DisplayIndicator("L", 165, y, status.symbolLocked ? TFT_BLUE : 0x0001);
    DisplayIndicator("R", 190, y, status.requestInProgess ? TFT_BLUE : 0x0001);
    DisplayIndicator(String(buf), 260, y, status.time ? TFT_GREEN : TFT_RED);
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
    sprintf(buf, "%3.2f%%", symbolData.changePercent);
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
    //tft.drawString("Symbol", tft.height() / 2, 80);
  }
}

bool GetSymbolDataFromAPI(SymbolData *symbolData)
{
  String payload;
  //String host = "https://cloud.iexapis.com/stable/stock/" + symbolData->symbol + "/quote?token=" + parameters.apiKey;
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

  symbolData->companyName = doc["companyName"].as<String>();
  symbolData->openPrice = doc["previousClose"].as<float>();
  symbolData->change = doc["change"].as<float>();
  symbolData->changePercent = doc["changePercent"].as<float>();
  symbolData->week52High = doc["week52High"].as<float>();
  symbolData->week52High = doc["week52High"].as<float>();
  symbolData->week52Low = doc["week52Low"].as<float>();
  symbolData->lastUpdate = doc["latestUpdate"].as<long long>();

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

void GetMarketState(tm currentTime, MarketState *ms)
{
  if (isMarketHoliday) // Market holiday.
  {
    *ms = MarketState::Holiday;
  }
  else if (timeinfo.tm_wday == 0 || timeinfo.tm_wday == 0) // Sunday or Saturday.
  {
    *ms = MarketState::Weekend;
  }
  else
  {
    if (isTimeBetweenTimes(timeinfo.tm_hour, timeinfo.tm_min, 4, 0, 9, 29)) // Pre Market hours.
    {
      *ms = MarketState::PreHours;
    }
    else if (isTimeBetweenTimes(timeinfo.tm_hour, timeinfo.tm_min, 9, 30, 15, 59)) // Market hours.
    {
      *ms = MarketState::MarketHours;
    }
    else if (isTimeBetweenTimes(timeinfo.tm_hour, timeinfo.tm_min, 16, 0, 22, 0)) // After market hours.
    {
      *ms = MarketState::AfterHours;
    }
    else
    {
      *ms = MarketState::Closed;
    }
  }
}

// Executed as a RTOS task.
void GetSymbolData(void *)
{
  static unsigned long start = millis();

  while (1)
  {

    if (millis() - start > 2000) // TODO: change to API per min calc provided user param
    {
      start = millis();

      // Get index symbol with oldest data.
      int selectedIndex = 0;
      for (int i = 0; i < parameters.symbolData.size(); i++)
      {
        if (parameters.symbolData[i].isValid)
        {
          if (parameters.symbolData[i].lastUpdate < parameters.symbolData[selectedIndex].lastUpdate)
          {
            selectedIndex = i;
          }
        }
      }

      if ((marketState == MarketState::PreHours && parameters.market.fetchPreMarketData) ||
          (marketState == MarketState::MarketHours) ||
          (marketState == MarketState::AfterHours && parameters.market.fetchAfterMarketData) ||
          parameters.symbolData[selectedIndex].lastUpdate == 0)
      {

        Serial.printf("API: Requesting data for symbol: %s\n", parameters.symbolData[selectedIndex].symbol.c_str());
        status.requestInProgess = true;

        if (parameters.api.provider.equalsIgnoreCase("IEXCLOUD"))
        {
          status.api = GetSymbolDataFromAPI(&parameters.symbolData[selectedIndex]);
        }
        else
        {
          Serial.printf("API: Error, unknown API provider: %s\n", parameters.api.provider.c_str());
          status.api = false;
        }

        status.requestInProgess = false;
      }
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
    if (tft.getTouch(&x, &y, 64))
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
  char buf[128];
  int wifiCredentialsIndex = 0;

  while (1)
  {
    DisplayBlank();    
    tft.setTextSize(2);
    tft.setTextColor(TFT_GREEN);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("Connecting to WiFi\n", 10, 50);
    sprintf(buf, "SSID: %s\n", parameters.wifiCredentials[wifiCredentialsIndex].ssid.c_str());
    tft.drawString(buf, 10, 70);
    sprintf(buf, "Password: %s\n", parameters.wifiCredentials[wifiCredentialsIndex].password.c_str());
    tft.drawString(buf, 10, 90);  
    Serial.printf("\nWIFI: Connecting to SSID: %s, with password: %s\n", parameters.wifiCredentials[wifiCredentialsIndex].ssid.c_str(), parameters.wifiCredentials[wifiCredentialsIndex].password.c_str());

    WiFi.begin(parameters.wifiCredentials[wifiCredentialsIndex].ssid.c_str(), parameters.wifiCredentials[wifiCredentialsIndex].password.c_str());

    tft.setCursor(10, 110);
    int count = 0;
    while (count++ < 10)
    {
      delay(500);
      tft.print(".");
      Serial.print(".");

      if (WiFi.status() == WL_CONNECTED)
      {
        tft.drawString("Connected!\n", 10, 140);
        sprintf(buf, "IP: %s\n", WiFi.localIP().toString().c_str());
        tft.drawString(buf, 10, 160);       
        Serial.println("");
        Serial.printf("WIFI: WiFi connected to %s, device IP: %s\n", parameters.wifiCredentials[wifiCredentialsIndex].ssid.c_str(), WiFi.localIP().toString().c_str());
        delay(1000);
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

bool GetTime()
{
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("TIME: Failed to obtain time");
    return false;
  }

  return true;
}

void setup()
{
  delay(500);
  Serial.begin(115200);
  Serial.println(F("\nQuoteBot starting up..."));

  matrix.setBrightness(127);
  matrix.begin();
  matrix.show();

  // LCD backlight PWM.
  ledcSetup(0, 5000, 8);
  ledcAttachPin(PIN_LCD_BACKLIGHT_PWM, 0);
  ledcWrite(0, 255);

  tft.init();
  delay(50);
  tft.setRotation(1);
  delay(50);
  tft.fillScreen(TFT_BLACK);
  DisplayLayout();
  UpdateIndicators(true);

  CheckTouchCalibration(&tft, false);

  if (InitSDCard())
  {
    status.sd = true;
    UpdateIndicators();
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
  static unsigned int previousSymbolSelect;

  GetMarketState(timeinfo, &marketState);

  UpdateNeoPixelMatrix();

  CheckTouchScreen();

  UpdateIndicators();

  // Check for WiFi connection, attempt reconnect after timeout.
  status.wifi = (WiFi.status() == WL_CONNECTED);
  static unsigned long startStatus = millis();
  if (millis() - startStatus > wifiTimeoutUntilNewScan)
  {
    ConnectWifi();
  }
  if (status.wifi == true)
  {
    startStatus = millis();
  }

  // Increment selected symbol.
  static unsigned long startSymbolSelect = millis();
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

  // Update display after a API completes.
  static bool previousRequestInProgess;
  if (previousRequestInProgess == true && status.requestInProgess == false)
  {
    previousSymbolSelect = !previousSymbolSelect;
  }
  previousRequestInProgess = status.requestInProgess;

  // Update display with symbol data.
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
    UpdateIndicators(true);
  }
}
