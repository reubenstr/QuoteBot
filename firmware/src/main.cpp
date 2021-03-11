

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <vector>

#define PIN_LCD_BACKLIGHT_PWM 21
#define PIN_SD_CHIP_SELECT 15

// Use hardware SPI
TFT_eSPI tft = TFT_eSPI();

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
  unsigned long lastUpdate;
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

  bool operator!=(Status const &s)
  {
    return (wifi != s.wifi ||
            sd != s.sd ||
            api != s.api ||
            time != s.time ||
            symbolLocked != s.symbolLocked);
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
  int numSymbols = doc["symbols"].size();

  for (int i = 0; i < numSymbols; i++)
  {
    SymbolData sData;
    sData.symbol = doc["symbols"][i].as<String>();
    parameters.symbolData.push_back(sData);
  }

  int numWifi = doc["wifiCredentials"].size();

  for (int i = 0; i < numWifi; i++)
  {
    WifiCredentials wC;
    wC.ssid = doc["wifiCredentials"][i]["ssid"].as<String>();
    wC.password = doc["wifiCredentials"][i]["password"].as<String>();
    parameters.wifiCredentials.push_back(wC);
  }

  parameters.apiMaxRequestsPerMinute = doc["apiMaxRequestsPerMinute"].as<int>();

  parameters.nextSymbolDelay = doc["nextSymbolDelay"].as<int>();
  if (parameters.nextSymbolDelay < 1)
  {
    parameters.nextSymbolDelay = 1;
  }

  /*
  timeZone = doc["timeZone"].as<String>();

  int indicatorBrightnessParameter = doc["indicatorBrightness"].as<int>();
  int signBrightnessParameter = doc["signBrightness"].as<int>();


  if (indicatorBrightnessParameter > 9 && indicatorBrightnessParameter < 256)
  {
    indicatorBrightness = indicatorBrightnessParameter;
  }

  if (signBrightnessParameter > 25 && signBrightnessParameter < 256)
  {
    signBrightness = signBrightnessParameter;
  }
  */

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
    DisplayIndicator("L", 170, y, status.symbolLocked ? TFT_BLUE : TFT_BLACK);
    DisplayIndicator("12:23:12", 215, y, status.time ? TFT_GREEN : TFT_RED);
  }
}

void DisplayStockData(SymbolData stockData)
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
  if (stockData.symbol.length() == 1)
    spaced = "  " + stockData.symbol + "  ";
  else if (stockData.symbol.length() == 2)
    spaced = " " + stockData.symbol + "  ";
  else if (stockData.symbol.length() == 3)
    spaced = " " + stockData.symbol + " ";
  else if (stockData.symbol.length() == 4)
    spaced = "" + stockData.symbol + " ";
  else
    spaced = stockData.symbol;
  tft.drawString(spaced, indent, 7);

  // Name.
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);

  String name = stockData.companyName;
  if (stockData.companyName.length() > 15)
  {
    // name = stockData.companyName.substring(0, 9);
    tft.drawString(stockData.companyName.substring(0, 14), indent + 110, 12);
    tft.drawPixel(300, 25, TFT_WHITE);
    tft.drawPixel(303, 25, TFT_WHITE);
    tft.drawPixel(306, 25, TFT_WHITE);
  }
  else
  {
    tft.drawString(stockData.companyName, indent + 110, 12);
  }

  // Price.
  tft.setTextSize(6);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  sprintf(buf, "   %i   ", stockData.currentPrice);
  int tw = tft.textWidth(String(buf));
  tft.drawString(buf, tft.height() / 2 - tw / 2, 50);

  // Change.
  tft.setTextSize(3);
  sprintf(buf, "%1.2f", stockData.change);
  tft.drawString(buf, 40, 110);

  // Percent change.
  if (stockData.changePercent < 10)
    sprintf(buf, "%1.2f%%", stockData.changePercent);
  else if (stockData.changePercent < 100)
    sprintf(buf, "%2.1f%%", stockData.changePercent);
  else
    sprintf(buf, "%3.0f%%", stockData.changePercent);
  tft.drawString(buf, 175, 110);

  // Extra data.
  tft.setTextSize(2);
  tft.setTextColor(TFT_BLUE, TFT_BLACK);
  //sprintf(buf, "Open: %3.2f", stockData.openPrice);
  //tft.drawString(buf, 10, 175);
  sprintf(buf, "%3.2f", stockData.openPrice);
  tft.drawString("Open", 10, 160);
  tft.drawString(buf, 10, 182);

  sprintf(buf, "%3.2f", stockData.peRatio);
  tft.drawString("P/E", 120, 160);
  tft.drawString(buf, 120, 182);

  sprintf(buf, "%3.2f", stockData.lastUpdate);
  tft.drawString("Update", 220, 160);
  tft.drawString("12:01:35", 220, 182);

  // 52 week
  int y = 143;
  int x = mapFloat(stockData.currentPrice, stockData.week52Low, stockData.week52High, 20, tft.height() - 20);
  tft.drawLine(20, y + 5, tft.height() - 20, y + 5, TFT_YELLOW);
  tft.fillRect(x, y, 5, 10, TFT_YELLOW);
}

bool GetSymbolData(SymbolData *stockData)
{

  Serial.printf("API: Requesting data for symbol: %s\n", stockData->symbol.c_str());

  stockData->companyName = "First Magestic Silver Co.";
  stockData->openPrice = 16.365;
  stockData->currentPrice = 9.24 * symbolSelect;
  stockData->change = 12.34;
  stockData->changePercent = 12.34;
  stockData->peRatio = -68.08;
  stockData->week52High = 24.01;
  stockData->week52Low = 4.17;
  stockData->lastUpdate = 24135179;

  return true;
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
  }

  // Update symbol data.
  if (millis() - startDataRequest > (60 / parameters.apiMaxRequestsPerMinute) * 1000)
  {
    startDataRequest = millis();
 
    status.api = GetSymbolData(&parameters.symbolData[symbolSelect]);   
  }

  // Update display with symbol data.
  static unsigned int previousSymbolSelect;
  if (previousSymbolSelect != symbolSelect)
  {
    previousSymbolSelect = symbolSelect;
    DisplayStockData(parameters.symbolData.at(symbolSelect));
  }
}
