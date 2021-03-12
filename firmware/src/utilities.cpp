#include <Arduino.h>


String AddEvenSpaces(String string, int numChars)
{
  bool toggle = true;
  while (string.length() < numChars)
  {
    toggle = !toggle;
    if (toggle)
    {
      string = " " + string;
    }
    else
    {
      string = string + " ";
    }
  }
  return string;
}

float mapFloat(float x, float in_min, float in_max, float out_min, float out_max)
{
  const float dividend = out_max - out_min;
  const float divisor = in_max - in_min;
  const float delta = x - in_min;

  return (delta * dividend + (divisor / 2)) / divisor + out_min;
}