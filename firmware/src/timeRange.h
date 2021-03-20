class TimeRange
{
public:
    TimeRange()
    {
        startHour = 0;
        startMinute = 0;
        endHour = 0;
        endMinute = 0;
    }

    TimeRange(int sh,int sm,int eh, int em)
    {
        startHour = sh;
        startMinute = sm;
        endHour = eh;
        endMinute = em;
    }

    unsigned int startHour;
    unsigned int startMinute;
    unsigned int endHour;
    unsigned int endMinute;

    // Parse time range from string formated as "01:30-23:59".
    //inline bool getTimeRangeFromString(String string, int *hourStart, int *minStart, int *hourEnd, int *minEnd)
    inline bool SetTimeRangeFromString(String string)
    {
        if (string.length() != 11)
        {
            return false;
        }

        char buf[16];
        string.toCharArray(buf, string.length() + 1);
        if (sscanf(buf, "%u:%u-%u:%u", &startHour, &startMinute, &endHour, &endMinute) != 4)
        {
            return false;
        }
        return true;
    }

    // Check if time is between the stored times.
    inline bool isTimeBetweenRange(int testHour, int testMin)
    {
        return hourMinToSeconds(testHour, testMin) > hourMinToSeconds(startHour, startMinute) &&
               hourMinToSeconds(testHour, testMin) < hourMinToSeconds(endHour, endMinute);
    }

    inline unsigned long GetTotalSeconds()
    {
        return hourMinToSeconds(endHour, endMinute) - hourMinToSeconds(startHour, startMinute);
    }

private:
    // Return total seconds from 00:00 to provided hours and minutes;
    inline unsigned long hourMinToSeconds(int hour, int minute)
    {
        return hour * 60 * 60 + minute * 60;
    }
};