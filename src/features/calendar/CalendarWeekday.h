#pragma once
#include <string.h>
#include <time.h>

// Return the weekday of the displayed ISO date, or no label for invalid input.
inline char calendarStartWeekday(const char* iso) {
  if (!iso || strlen(iso) < 10) return '\0';
  for (int i = 0; i < 10; ++i) {
    if (i == 4 || i == 7) { if (iso[i] != '-') return '\0'; }
    else if (iso[i] < '0' || iso[i] > '9') return '\0';
  }
  const int year = (iso[0]-'0')*1000 + (iso[1]-'0')*100 + (iso[2]-'0')*10 + iso[3]-'0';
  const int month = (iso[5]-'0')*10 + iso[6]-'0';
  const int day = (iso[8]-'0')*10 + iso[9]-'0';
  if (year < 1 || month < 1 || month > 12 || day < 1 || day > 31) return '\0';
  struct tm date = {};
  date.tm_year = year - 1900;
  date.tm_mon = month - 1;
  date.tm_mday = day;
  date.tm_hour = 12;  // Avoid ordinary DST transitions at midnight.
  date.tm_isdst = -1;
  // Use the same date prefix as the card, without converting timestamp offsets
  // or needing a synchronized device clock. Reject normalized invalid dates.
  if (mktime(&date) == (time_t)-1 || date.tm_year != year - 1900 ||
      date.tm_mon != month - 1 || date.tm_mday != day) return '\0';
  return "UMTWRFS"[date.tm_wday];
}
