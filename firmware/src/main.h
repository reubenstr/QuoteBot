#include <Arduino.h>
#include <vector>
#include "timeRange.h"

enum class DayIds
{
  Sunday,
  Monday,
  Tuesday,
  Wednesday,
  Thursday,
  Friday,
  Saturday
};

struct Time
{
  struct tm currentTimeInfo;
  time_t currentEpoch;
  const char *ntpServer = "pool.ntp.org";
  const long gmtOffset_sec = -5 * 60 * 60;
  const int daylightOffset_sec = 3600;
  String timeZone;
  TimeRange preMarketTimeRange;
  TimeRange marketTimeRange;
  TimeRange afterMarketTimeRange;
  TimeRange displayMaxBrightnessTimeRange;
  TimeRange matrixMaxBrightnessTimeRange;
};

struct System
{
  Time time;
  unsigned int symbolSelect = 0;
  unsigned long millisecondsBetweenApiCalls;
  const unsigned long wifiTimeoutUntilNewScan = 30000; // milliseconds.
};

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
  unsigned long long latestUpdate = 0; // EPOCH in seconds.
  unsigned long long lastApiCall = 0;  // EPOCH in seconds.
  bool isValid = true;
  String errorString = "";
};

enum class ApiMode
{
  Unknown,
  Demo,
  Sandbox,
  Live
};

static const char *const apiModeText[] = {"Unknown", "Demo", "Sandbox", "Live"};

struct Api
{
  ApiMode mode;
  String provider;
  String key;
  int maxRequestsPerDay;
  String sandboxKey;
  int sandboxMaxRequestsPerDay;
};
struct Display
{
  int nextSymbolDelay;
  int brightnessMax;
  int brightnessMin;
  int dimStartHour;
  int dimStartMin;
  int dimEndHour;
  int dimEndMin;
};
struct Matrix
{
  String holidayPattern;
  String weekendPattern;
  String preMarketPattern;
  String marketPattern;
  String afterMarketPattern;
  String closedPattern;
  int brightnessMax;
  int brightnessMin;

  int dimStartHour;
  int dimStartMin;
  int dimEndHour;
  int dimEndMin;
};

struct WifiCredentials
{
  String ssid;
  String password;
};

struct Market
{
  bool fetchPreMarketData;
  bool fetchMarketData;
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
  UnknownApi,
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

static const char *const marketStateDesciptionTop[] = {"Unknown", "Holiday", "Weekend", "Pre", "Open", "After", "Closed"};
static const char *const marketStateDesciptionBottom[] = {"", "", "", "Hours", "", "Hours", ""};
// static const char *const marketStateDesciptionLetter[] = {"U", "H", "W", "P", "M", "S", "C"};