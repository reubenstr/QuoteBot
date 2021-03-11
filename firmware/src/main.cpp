// Animates white pixels to simulate flying through a star field

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>

#include <gfxItems.h>

#define LCD_BACKLIGHT_PWM 21

// Use hardware SPI
TFT_eSPI tft = TFT_eSPI();

GFXItems gfxItems(&tft);

const int groupId = 0;

enum class LabelsIds
{
  Wifi,
  SD,
  Api,
  Clock

};

void DisplayIndicator(String string, int x, int y, uint16_t color)
{
  int16_t x1, y1;
  uint16_t w, h;
  const int offset = 2;
  tft.setTextSize(2);
    tft.setTextColor(TFT_BLACK, color);
  //tft.setCursor(x, y);

  //getTextBounds(string.c_str(), x, y, &x1, &y1, &w, &h);
  tft.fillRect(x - offset, y - offset, tft.textWidth(string), h + offset, color);
  //tft.setTextPadding(5);
tft.drawString(string, x, y);
  //tft.print(string);
}

void DisplayStock()
{

  tft.setTextFont(0);
  //tft.setFreeFont(&FreeMono9pt7b);

  tft.drawRect(0, 0, tft.width(), tft.height(), TFT_WHITE);
  tft.drawFastHLine(0, 45, tft.width(), TFT_WHITE);
  tft.drawFastHLine(0, 200, tft.width(), TFT_WHITE);

  //tft.setTextFont(1);
  tft.setTextSize(3);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("AG", 10, 10);

  // tft.setTextSize(2);
  //tft.setTextColor(TFT_RED);
  //tft.drawString("CLOSED", 260, 5);

  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("First Majestic Silver", 70, 15);

  //tft.setTextFont(0);
  tft.setTextSize(6);
  tft.setTextColor(TFT_GREEN);
  tft.drawString("16.32", 25, 70);

  tft.setTextSize(3);
  tft.setTextColor(TFT_GREEN);
  tft.drawString("0.75%", 220, 70);

  //tft.setTextFont(1);
  tft.setTextSize(2);
  tft.setTextColor(TFT_BLUE);
  tft.drawString("Open:1711.25", 10, 175);
  tft.drawString("P/E:-453", 200, 175);

  // 52 week
  int x = 50;
  int y = 140;
  //tft.drawFastHLine(20, y + 10, tft.width() - 80, TFT_RED);
  tft.drawLine(20, y + 10, tft.height() - 20, y + 10, TFT_RED);

  tft.fillRect(x, y, 5, 20, TFT_RED);
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

  gfxItems.Add(GFXItem(int(LabelsIds::Wifi), groupId, "WiFi", 2, 10, y, 50, 20, TFT_BLACK, TFT_GREEN, Justification::Center));
  gfxItems.Add(GFXItem(int(LabelsIds::SD), groupId, "SD", 2, 70, y, 40, 20, TFT_BLACK, TFT_GREEN, Justification::Center));
  gfxItems.Add(GFXItem(int(LabelsIds::Api), groupId, "API", 2, 120, y, 50, 20, TFT_BLACK, TFT_GREEN, Justification::Center));
  gfxItems.Add(GFXItem(int(LabelsIds::Clock), groupId, "12:23:12", 2, 205, y, 100, 20, TFT_BLACK, TFT_GREEN, Justification::Center));

  //gfxItems.DisplayGroup(groupId);

  DisplayStock();

  int textStatusY = y;

  DisplayIndicator("SD", 10, textStatusY, 1 ? TFT_GREEN : TFT_RED);
  DisplayIndicator("WIFI", 55, textStatusY, 0 ? TFT_GREEN : TFT_RED);
  DisplayIndicator("API", 130, textStatusY, 0 ? TFT_GREEN : TFT_RED);
  DisplayIndicator("12:23:12", 205, textStatusY, 1 ? TFT_GREEN : TFT_RED);


  ledcSetup(0, 5000, 8);  
  ledcAttachPin(LCD_BACKLIGHT_PWM, 0);
  ledcWrite(0, 255);
}

void loop()
{
}
