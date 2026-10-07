# Calendar weekday labels — 2026-10-08

The owner approved a weekday letter after each agenda card's date, for example
`Oct 8 R`. Both pages use `drawAgendaPage`, so all six cards receive the same
layout. Letters are M/T/W/R/F/S/U; R means Thursday and U means Sunday.
`Today` retains its label and gains the weekday. Multi-day ranges show their
start weekday. Date/time color and title placement are unchanged.

Follow-up: all-day and multi-day events now show only the date/range and
weekday on the header row. The `All day` label is removed, and multi-day timed
events also omit their start time. Single-day timed events retain right-aligned
HH:MM, subject to the existing collision check. This affects both agenda pages.

The weekday uses the same ISO date prefix already displayed by the card, not
a timezone conversion of the event timestamp. Invalid dates omit the letter.
A 6px gap and 12px weekday glyph consume 18px. The existing time collision
check includes that width for single-day timed events. Date ranges use the
header for their date and start weekday without a time label.

Verification:

- The host regression check failed against an empty implementation, then passed
  all weekday letters, leap dates, invalid dates, offset timestamps, and three
  time zones. Run it with a fresh temporary output directory:
  `test_dir=$(mktemp -d); c++ -std=c++11 -Wall -Wextra -Werror scripts/test_calendar_weekday.cpp -o "$test_dir/test" && "$test_dir/test"`.
- `pio run -e smalltv -e smalltv_sdpro` passed. Neither repository has a Makefile.
  The weekday regression check and both builds were rerun for the time-label
  follow-up and passed.
  Existing unrelated compiler warnings remain (color redefinitions, formatting
  bounds, signedness, and framework Python escape sequences).
- No device was contacted or flashed. Physical readability is not yet verified.
- For the initial weekday change, an independent read-only reviewer returned CLEAN for the renderer, helper,
  and test, after running the host check and examining both page call sites.
  This is a code review, not approval to flash the dirty validation images.

The validation images were built before commit and carry a dirty `+` stamp.
Do not deploy those artifacts. A later deployment needs a clean committed
rebuild, independent flash audit, push approval, and the usual preflash gate.
No release version was changed. The change remains local until push is approved.

Trap from the initial change: a full 12px separator crowded same-month ranges
when they still displayed time. The follow-up deliberately omits range times.
