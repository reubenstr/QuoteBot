

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <SD.h>
#include <ArduinoJson.h>

#include <gfxItems.h>

#include <vector>

#define PIN_LCD_BACKLIGHT_PWM 21
#define PIN_SD_CHIP_SELECT 15

// Use hardware SPI
TFT_eSPI tft = TFT_eSPI();

GFXItems gfxItems(&tft);

const int groupId = 0;

// WiFi

struct WifiCredentials
{
  String ssid;
  String password;
};

const char *parametersFilePath = "/parameters.json";
std::vector<WifiCredentials> wifiCredentials;

unsigned int symbolSelect = 0;
unsigned int maxSymbols = 10;

std::vector<String> symbols;

struct StockData
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
};

enum class LabelsIds
{
  Wifi,
  SD,
  Api,
  Clock

};

void Error()
{

  tft.setTextSize(4);
  tft.setTextColor(TFT_RED, TFT_WHITE);
  tft.drawString("ERROR!", 50, 128);
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
    symbols.push_back(doc["symbols"][i].as<String>());
  }

  int numWifi = doc["wifiCredentials"].size();

  for (int i = 0; i < numWifi; i++)
  {

    WifiCredentials wC;

    wC.ssid = doc["wifiCredentials"][i]["ssid"].as<String>();
    wC.password = doc["wifiCredentials"][i]["password"].as<String>();
    wifiCredentials.push_back(wC);

    Serial.println(wifiCredentials[i].ssid);
    Serial.println(wifiCredentials[i].password);
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

void DisplayStockData(StockData stockData)
{
  char buf[32];
  const int indent = 10;

  tft.setTextFont(0);
  //tft.setFreeFont(&FreeMono9pt7b);

  // Frame.
  tft.drawRect(0, 0, tft.width(), tft.height(), TFT_WHITE);
  tft.drawFastHLine(0, 40, tft.width(), TFT_WHITE);
  tft.drawFastHLine(0, 200, tft.width(), TFT_WHITE);
  tft.drawFastVLine(105, 0, 40, TFT_WHITE);

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
  tft.drawString(spaced, indent, 10);

  // Name.
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);

  String name = stockData.companyName;
  if (stockData.companyName.length() > 15)
  {
   // name = stockData.companyName.substring(0, 9);
     tft.drawString(stockData.companyName.substring(0, 14), indent + 110, 15);
       tft.drawPixel(300, 25, TFT_WHITE);
    tft.drawPixel(303, 25, TFT_WHITE);
      tft.drawPixel(306, 25, TFT_WHITE);
  }
  else
  {
  tft.drawString(stockData.companyName, indent + 110, 15);
  }









  // Price data.
  tft.setTextSize(6);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  //sprintf(buf, "%.2f", stockData.currentPrice);
  sprintf(buf, "%i", symbolSelect);
  int tw = tft.textWidth(String(buf));
  tft.drawString(buf, tft.height() / 2 - tw / 2, 60);
  tft.setTextSize(3);
  sprintf(buf, "%1.2f", stockData.change);
  tft.drawString(buf, 40, 120);

  if (stockData.changePercent < 10)
    sprintf(buf, "%1.2f%%", stockData.changePercent);
  else if (stockData.changePercent < 100)
    sprintf(buf, "%2.1f%%", stockData.changePercent);
  else
    sprintf(buf, "%3.0f%%", stockData.changePercent);

  tft.drawString(buf, 175, 120);

  // Extra data.
  tft.setTextSize(2);
  tft.setTextColor(TFT_BLUE, TFT_BLACK);
  sprintf(buf, "Open: %3.2f", stockData.openPrice);
  tft.drawString(buf, 10, 175);
  sprintf(buf, "P/E: %3.2f", stockData.peRatio);
  tft.drawString(buf, 185, 175);

  // 52 week
  int y = 155;
  int x = mapFloat(stockData.currentPrice, stockData.week52Low, stockData.week52High, 20, tft.height() - 20);
  tft.drawLine(20, y + 5, tft.height() - 20, y + 5, TFT_RED);
  tft.fillRect(x, y, 5, 10, TFT_RED);
}

bool GetStockData(String symbol, StockData *stockData)
{

  stockData->symbol = symbol;
  stockData->companyName = "First Magestic Silver Co.";
  stockData->openPrice = 16.365;
  stockData->currentPrice = 16.45;
  stockData->change = 12.34;
  stockData->changePercent = 12.34;
  stockData->peRatio = -68.08;
  stockData->week52High = 24.01;
  stockData->week52Low = 4.17;

  return true;
}

void CheckTouchScreen()
{

  //static takeTouchReadings = true;
  static unsigned long touchDebounceDelay = 250;
  static unsigned long touchDebounceMillis = millis();
  uint16_t x, y;

  if (millis() - touchDebounceMillis > touchDebounceDelay)
  {
    if (tft.getTouch(&x, &y, 20))
    {
      touchDebounceMillis = millis();

      symbolSelect++;
      if (symbolSelect > symbols.size() - 1)
      {
        symbolSelect = 0;
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

  int y = 210;

  //gfxItems.Add(GFXItem(int(LabelsIds::Wifi), groupId, "WiFi", 2, 10, y, 50, 20, TFT_BLACK, TFT_GREEN, Justification::Center));
  //gfxItems.Add(GFXItem(int(LabelsIds::SD), groupId, "SD", 2, 70, y, 40, 20, TFT_BLACK, TFT_GREEN, Justification::Center));
  //gfxItems.Add(GFXItem(int(LabelsIds::Api), groupId, "API", 2, 120, y, 50, 20, TFT_BLACK, TFT_GREEN, Justification::Center));
  //gfxItems.Add(GFXItem(int(LabelsIds::Clock), groupId, "12:23:12", 2, 205, y, 100, 20, TFT_BLACK, TFT_GREEN, Justification::Center));

  //gfxItems.DisplayGroup(groupId);

  int textStatusY = y;

  DisplayIndicator("SD", 10, textStatusY, 1 ? TFT_GREEN : TFT_RED);
  DisplayIndicator("WIFI", 55, textStatusY, 0 ? TFT_GREEN : TFT_RED);
  DisplayIndicator("API", 130, textStatusY, 0 ? TFT_GREEN : TFT_RED);
  DisplayIndicator("12:23:12", 205, textStatusY, 1 ? TFT_GREEN : TFT_RED);

  ledcSetup(0, 5000, 8);
  ledcAttachPin(PIN_LCD_BACKLIGHT_PWM, 0);
  ledcWrite(0, 255);

  InitSDCard();

  GetParametersFromSDCard();
}

void loop()
{

  CheckTouchScreen();

  StockData stockData;
  GetStockData(symbols.at(symbolSelect), &stockData);
  DisplayStockData(stockData);
}
