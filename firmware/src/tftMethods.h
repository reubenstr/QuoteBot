/*
    tftMethods.h

*/

#include "FS.h"
#include <TFT_eSPI.h>
#include <SPIFFS.h>

#define CALIBRATION_FILE "/TouchCalData"

void CheckTouchCalibration(TFT_eSPI *tft, bool forceCalibrationFlag)
{
    uint16_t calData[5];
    uint8_t calDataOK = 0;

    if (SPIFFS.begin())
    {
        Serial.println("SPIFFS: Exists.");
    }
    else
    {
        Serial.println("SPIFFS: Formating file system.");
        SPIFFS.format();
        SPIFFS.begin();
    }

    // check if calibration file exists and size is correct
    if (SPIFFS.exists(CALIBRATION_FILE))
    {
        Serial.println("SPIFFS: Getting calibration file.");
        File f = SPIFFS.open(CALIBRATION_FILE, "r");
        if (f)
        {
            if (f.readBytes((char *)calData, 14) == 14)
            {
                calDataOK = 1;
            }
            f.close();
        }
    }
    else
    {
        Serial.println("SPIFFS: calibration files does not exist.");
    }

    if (calDataOK && !forceCalibrationFlag)
    {
        // calibration data valid
        Serial.println("TFT: calibration data valid.");
        tft->setTouch(calData);
    }
    else
    {
        Serial.println("TFT: calibration data invalid.");
        Serial.println("TFT: Start calibration.");

        // data not valid so recalibrate
        tft->fillScreen(TFT_BLACK);
        tft->setCursor(20, 0);
        tft->setTextFont(2);
        tft->setTextSize(1);
        tft->setTextColor(TFT_WHITE, TFT_BLACK);
        tft->println("Touch corners as indicated");
        tft->setTextFont(1);
        tft->println();
        tft->calibrateTouch(calData, TFT_MAGENTA, TFT_BLACK, 15);
        tft->setTextColor(TFT_GREEN, TFT_BLACK);
        tft->println("Calibration complete!");

        // store data
        File f = SPIFFS.open(CALIBRATION_FILE, "w");
        if (f)
        {
            f.write((const unsigned char *)calData, 14);
            f.close();
        }
        Serial.println("TFT: calibration complete.");
    }
}