
#include "FS.h"
#include <TFT_eSPI.h>

#define CALIBRATION_FILE "/TouchCalData"

void touch_calibrate(TFT_eSPI *tft, bool forceCalibrationFlag);