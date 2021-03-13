#include <Arduino.h>
#include <vector>

struct SymbolData
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
  unsigned long long lastUpdate;
  bool isValid = true;
  String errorString;
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