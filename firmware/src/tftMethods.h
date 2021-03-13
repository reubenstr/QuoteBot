
#include "FS.h"
#include <TFT_eSPI.h>

#define CALIBRATION_FILE "/TouchCalData"

void CheckTouchCalibration(TFT_eSPI *tft, bool forceCalibrationFlag);