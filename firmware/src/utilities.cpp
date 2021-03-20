#include <Arduino.h>

float mapFloat(float x, float in_min, float in_max, float out_min, float out_max)
{
  const float dividend = out_max - out_min;
  const float divisor = in_max - in_min;
  const float delta = x - in_min;

  return (delta * dividend + (divisor / 2)) / divisor + out_min;
}

// Convert matrix pixel order to start at the upper left and end at the lower right.
int rotateMatrix(unsigned int i)
{
  //const int conversion[16] = {15, 8, 7, 0, 14, 9, 6, 1, 13, 10, 5, 2, 12, 11, 4, 3};
  const int conversion[16] = {3, 4, 11, 12, 2, 5, 10, 13, 1, 6, 9, 14, 0, 7, 8, 15};
  if (i >= 16)
  {
    return 0;
  }

  return conversion[i];
}