#include <Arduino.h>
#include <vector>

struct SymbolData
{
  String symbol = "";
  String companyName = "";
  float openPrice = 0;
  float currentPrice = 0;
  float change = 0;
  float changePercent = 0;
  float peRatio = 0;
  float week52High = 0;
  float week52Low = 0;
  unsigned long long lastUpdate = 0;
  bool isValid = true;
  // bool initialized = false;
  String errorString = "";
};

struct Api
{
  String provider;
  String key;
  int maxRequestsPerMinute;
};
struct Display
{
  int nextSymbolDelay;
  int brightnessMax;
  int brightnessMin;
  int dimStartHour;
  int dimEndHour;
};
struct Matrix
{
  String marketHoursPattern;
  String afterHoursPattern;
    int brightnessMax;
  int brightnessMin;
  int dimStartHour;
  int dimEndHour;
};

struct System
{
  String timeZone;
};

struct WifiCredentials
{
  String ssid;
  String password;
};

struct Market
{
    bool fetchPreMarketData;
    bool fetchAfterMarketData;
};

struct Parameters
{
  std::vector<SymbolData> symbolData;
  std::vector<WifiCredentials> wifiCredentials;
  Api api;
  Market market;
  Display display;
  Matrix matrix;
  System system;

};

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
  bool requestInProgess;

  bool operator!=(Status const &s)
  {
    return (wifi != s.wifi ||
            sd != s.sd ||
            api != s.api ||
            time != s.time ||
            symbolLocked != s.symbolLocked ||
            requestInProgess != s.requestInProgess);
  }

};

enum class ErrorIDs
{
  SdFailed,
  ParametersFailed,
  InvalidApiKey
};

enum class MarketState
{
    Unknown,
    Holiday,
    Weekend,
    PreHours,
    MarketHours,
    AfterHours,
    Closed    
};


static const char * const MarketStateDesciption[]= {"Unknown", "Holiday", "Weekend", "PreHours", "MarketHours", "AfterHours", "Closed"};


// Matrix colors (NeoPixels)
const int Off = 0x00000000;
const int Red = 0x00FF0000;
const int Green = 0x0000FF00;
const int Blue = 0x000000FF;


