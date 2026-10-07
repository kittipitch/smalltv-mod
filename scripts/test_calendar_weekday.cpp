// Run: c++ -std=c++11 scripts/test_calendar_weekday.cpp -o /tmp/calendar-weekday-test
//      /tmp/calendar-weekday-test
#include "../src/features/calendar/CalendarWeekday.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <initializer_list>

int main() {
  // Catches shifted weekday indices, confusing Thursday/Tuesday, date
  // normalization, and converting a timestamp instead of its displayed date.
  const struct { const char* date; char want; } cases[] = {
    {"2026-10-05", 'M'}, {"2026-10-06", 'T'}, {"2026-10-07", 'W'},
    {"2026-10-08", 'R'}, {"2026-10-09", 'F'}, {"2026-10-10", 'S'},
    {"2026-10-11", 'U'}, {"2026-10-08T00:30:00+14:00", 'R'},
    {"2026-10-08T23:30:00-12:00", 'R'}, {"2000-02-29", 'T'},
    {"2024-02-29", 'R'}, {"2025-12-31", 'W'}, {"2026-01-01", 'R'},
    {"2026-03-08", 'U'}, {"2026-11-01", 'U'},
    {"", '\0'}, {"2026-10", '\0'}, {"2026/10/08", '\0'},
    {"202x-10-08", '\0'}, {"2026-00-08", '\0'}, {"2026-13-08", '\0'},
    {"2026-10-00", '\0'}, {"2026-04-31", '\0'}, {"2026-02-29", '\0'},
    {nullptr, '\0'},
  };
  for (const char* zone : {"UTC0", "ICT-7", "EST5EDT,M3.2.0,M11.1.0"}) {
    setenv("TZ", zone, 1);
    tzset();
    for (const auto& c : cases) assert(calendarStartWeekday(c.date) == c.want);
  }
  puts("PASS: weekday letters, invalid dates, date boundaries, and time zones");
}
