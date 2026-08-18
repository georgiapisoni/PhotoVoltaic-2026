#include <DS1307RTC.h>
#include <TimeLib.h>
#include <Wire.h>

// Edit these values immediately before compiling and uploading.
const uint16_t SET_YEAR = 2026;
const uint8_t SET_MONTH = 8;
const uint8_t SET_DAY = 18;
const uint8_t SET_HOUR = 12;
const uint8_t SET_MINUTE = 0;
const uint8_t SET_SECOND = 0;

void printTwoDigits(uint8_t value)
{
  if (value < 10) Serial.print('0');
  Serial.print(value);
}

void printRtcTime(const tmElements_t &time)
{
  printTwoDigits(time.Day);
  Serial.print('/');
  printTwoDigits(time.Month);
  Serial.print('/');
  Serial.print(tmYearToCalendar(time.Year));
  Serial.print(' ');
  printTwoDigits(time.Hour);
  Serial.print(':');
  printTwoDigits(time.Minute);
  Serial.print(':');
  printTwoDigits(time.Second);
  Serial.println();
}

void setup()
{
  Serial.begin(9600);
  Wire.begin();
  delay(1000);

  tmElements_t newTime;
  newTime.Year = CalendarYrToTm(SET_YEAR);
  newTime.Month = SET_MONTH;
  newTime.Day = SET_DAY;
  newTime.Hour = SET_HOUR;
  newTime.Minute = SET_MINUTE;
  newTime.Second = SET_SECOND;

  Serial.println(F("Writing date and time to DS1307 at 0x68..."));

  if (!RTC.write(newTime)) {
    Serial.println(F("ERROR: RTC write failed."));
    return;
  }

  delay(100);

  tmElements_t readBack;
  if (!RTC.read(readBack)) {
    Serial.println(F("ERROR: RTC readback failed."));
    return;
  }

  Serial.print(F("RTC set successfully: "));
  printRtcTime(readBack);
  Serial.println(F("Now upload the normal application from the main branch."));
}

void loop()
{
  // One-shot utility: do nothing after setting and verifying the RTC.
}
