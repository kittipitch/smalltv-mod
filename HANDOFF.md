# Calendar weekday labels — 2026-10-08

The owner approved a weekday letter after each agenda card's date, for example
`Oct 8 R`. Both pages use `drawAgendaPage`, so all six cards receive the same
layout. Letters are M/T/W/R/F/S/U; R means Thursday and U means Sunday.
`Today` retains its label and gains the weekday. Multi-day ranges show their
start weekday. Date/time color and title placement are unchanged.

The weekday uses the same ISO date prefix already displayed by the card, not
a timezone conversion of the event timestamp. Invalid dates omit the letter.
A 6px gap and 12px weekday glyph consume 18px. The existing time collision
check includes that width: even a same-month two-digit range plus HH:MM fits
in 198 of the 200 available pixels. Long cross-month ranges still omit time.

Verification:

- The host regression check failed against an empty implementation, then passed
  all weekday letters, leap dates, invalid dates, offset timestamps, and three
  time zones. Run it with a fresh temporary output directory:
  `test_dir=$(mktemp -d); c++ -std=c++11 -Wall -Wextra -Werror scripts/test_calendar_weekday.cpp -o "$test_dir/test" && "$test_dir/test"`.
- `pio run -e smalltv -e smalltv_sdpro` passed. Neither repository has a Makefile.
  Validation binary sizes: Ultra 680,000 bytes; SD PRO 673,408 bytes.
  Existing unrelated compiler warnings remain (color redefinitions, formatting
  bounds, signedness, and framework Python escape sequences).
- No device was contacted or flashed. Physical readability is not yet verified.
- An independent read-only reviewer returned CLEAN for the renderer, helper,
  and test, after running the host check and examining both page call sites.
  This is a code review, not approval to flash the dirty validation images.

The validation images were built before commit and carry a dirty `+` stamp.
Do not deploy those artifacts. A later deployment needs a clean committed
rebuild, independent flash audit, push approval, and the usual preflash gate.
No release version was changed. The change remains local until push is approved.

Trap: a full 12px separator would hide the time on a same-month range;
the 6px separator preserves it without shrinking the font.
