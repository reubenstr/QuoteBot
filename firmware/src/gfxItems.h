#include <Arduino.h>
#include <TFT_eSPI.h>
#include <map>
#include <vector>

#ifndef GFXITEMS_H
#define GFXITEMS_H

// ids are not unique.

enum class Justification
{
    Left,
    Center,
    Right
};

struct GFXItem
{
    String text;
    int id;
    int groupId;
    bool isPressable = true;
    int x, y;
    int w = 0;
    int h = 0;

    uint32_t textColor;
    uint32_t fillColor;
    uint32_t activeColor;
    uint32_t borderColor;

    bool isPressed = false;
    int textSize = 1;

    Justification justification = Justification::Center;

    int borderThickness = 0;
    signed int minimumCharacters = -1;
    int padding = -1;
    int cornerSize = 0;

    uint8_t textFont = 0;
    const GFXfont *gfxFont = nullptr;

    GFXItem();

    // Candidate for buttons.
    GFXItem(int id, int groupId, String text, int x, int y, int w, int h,
            uint32_t textColor, uint32_t fillColor, uint32_t activeColor,
            uint32_t borderColor, const GFXfont *gfxFont = nullptr)
    {
        this->text = text;
        this->groupId = groupId;
        this->id = id;
        this->x = x;
        this->y = y;
        this->w = w;
        this->h = h;
        this->textColor = textColor;
        this->fillColor = fillColor;
        this->activeColor = activeColor;
        this->borderColor = borderColor;
        this->gfxFont = gfxFont;

        cornerSize = 5;
        textSize = 1;
        borderThickness = 1;
    }

    // Candidate for labels.
    GFXItem(int id, int groupId, String text, int textSize, int x, int y, int w, int h,
            uint32_t textColor, uint32_t fillColor,
            Justification justification, const GFXfont *gfxFont = nullptr)
    {
        this->id = id;
        this->groupId = groupId;
        this->text = text;
        this->textSize = textSize;
        this->x = x;
        this->y = y;
        this->w = w;
        this->h = h;
        this->textColor = textColor;
        this->fillColor = fillColor;
        this->justification = justification;
        this->gfxFont = gfxFont;
    }

    bool IsPointInBoundry(int xTest, int yTest)
    {
        if (xTest >= x && xTest <= (x + w))
        {
            if (yTest >= y && yTest <= (y + h))
            {
                return true;
            }
        }
        return false;
    }
};

class GFXItems
{
public:
    GFXItems(TFT_eSPI *tft);
    void Add(GFXItem gfxItem);

    void DisplayGfxItem(int id);
    void DisplayGroup(int groupId);
    bool IsItemInGroupPressed(int key, int *id);
    GFXItem &GetGfxItemById(int id);

private:
    TFT_eSPI *tft;
    std::vector<GFXItem> gfxItems;    

    void DisplayElement(GFXItem b);

    GFXItem &operator[](int i)
    {
        for (auto &b : gfxItems)
        {
            if (b.id == i)
            {
                return gfxItems[i];
            }
        }
        return gfxItems[i];
    }
};

#endif