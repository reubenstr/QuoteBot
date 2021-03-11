#include "gfxItems.h"
#include <vector>
#include "Free_Fonts.h"
#include <TFT_eSPI.h>

GFXItems::GFXItems(TFT_eSPI *tft)
{
  this->tft = tft;
}

void GFXItems::Add(GFXItem gfxItem)
{
  gfxItems.push_back(gfxItem);
}

GFXItem &GFXItems::GetGfxItemById(int eId)
{
  for (auto &b : gfxItems)
  {
    if (b.id == eId)
    {
      return b;
    }
  }
  return gfxItems[0];
}

void GFXItems::DisplayElement(GFXItem gfxItem)
{
  uint32_t backgroundColor = gfxItem.fillColor;
  if (gfxItem.isPressed)
    backgroundColor = gfxItem.activeColor;

  tft->setFreeFont(gfxItem.gfxFont);
  tft->setTextSize(gfxItem.textSize);
  tft->setTextColor(gfxItem.textColor);

  String text(gfxItem.text);

  if (gfxItem.cornerSize > 0)
  {
    tft->fillRoundRect(gfxItem.x, gfxItem.y, gfxItem.w, gfxItem.h, gfxItem.cornerSize, gfxItem.borderColor);
  }

  tft->fillRoundRect(gfxItem.x + gfxItem.borderThickness, gfxItem.y + gfxItem.borderThickness, gfxItem.w - gfxItem.borderThickness * 2, gfxItem.h - gfxItem.borderThickness * 2, gfxItem.cornerSize, backgroundColor);

  int textX;
  if (gfxItem.justification == Justification::Left)
  {
    textX = gfxItem.x;
  }
  else if (gfxItem.justification == Justification::Center)
  {
    textX = gfxItem.x + gfxItem.w / 2 - tft->textWidth(gfxItem.text) / 2;
  }
  else if (gfxItem.justification == Justification::Right)
  {
    textX = gfxItem.x + gfxItem.w - tft->textWidth(text);
  }
  else
  {
    textX = gfxItem.x;
  }

  int textY;
  textY = gfxItem.y + gfxItem.h / 2 - tft->fontHeight() / 2;

  tft->drawString(text, textX, textY);
}

void GFXItems::DisplayGfxItem(int id)
{
  for (auto const &b : gfxItems)
  {
    if (b.id == id)
    {
      DisplayElement(b);
    }
  }
}

void GFXItems::DisplayGroup(int groupId)
{
  for (auto &gfxItem : gfxItems)
  {
    if (gfxItem.groupId == groupId)
    {
      DisplayElement(gfxItem);
    }
  }
}

bool GFXItems::IsItemInGroupPressed(int groupId, int *id)
{
  uint16_t x, y;
  bool buttonPressedFlag = false;

  if (tft->getTouch(&x, &y, 20))
  {
    for (auto &gfxItem : gfxItems)
    {
      if (gfxItem.groupId == groupId)
      {
        if (gfxItem.isPressable)
        {
          if (gfxItem.IsPointInBoundry(x, y))
          {
            buttonPressedFlag = true;
            *id = gfxItem.id;
            // Prevent button flashing when being held down.
            if (!gfxItem.isPressed)
            {
              gfxItem.isPressed = true;
              DisplayElement(gfxItem);
            }
          }
        }
      }
    }
  }
  else
  {
    // No touch detected, depress all buttons.

    for (auto &gfxItem : gfxItems)
    {
      if (gfxItem.groupId == groupId)
      {
        if (gfxItem.isPressed)
        {
          gfxItem.isPressed = false;
          DisplayElement(gfxItem);
        }
      }
    }
  }

  return buttonPressedFlag;
}
