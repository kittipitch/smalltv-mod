# Calendar weekday labels — 2026-10-08

## Standalone agenda rotation follow-up

Selecting `Next event` now alternates populated pages 1 and 2 at the same
`carouselSec` dwell, independently of the carousel's page-2 checkbox. Every
page transition wakes the selected renderer, including fallback after events
shrink to three or fewer. Leaving/reentering agenda, saving settings, and
offline recovery restart the dwell. Empty page 2 is skipped.

Page 2 is removed from both static and dynamically rebuilt mode dropdowns.
The carousel retains its nested page-2 toggle and existing ordering. Legacy
standalone `agenda2` settings map to rotating `agenda` on load/save.

Verification: `python3 scripts/test_agenda_rotation.py` passes equal dwell,
redraws, shrinking/growing event data, mode reentry, millis rollover, and
dropdown behavior. Both ESP8266 variants built successfully. Native pixel
previews from `python3 scripts/preview_agenda.py` reuse the actual renderer,
font and drawing routines; both pages were inspected for weekday placement,
all-day/range label omission, and timed-event spacing. Ego-browser exercised
the actual web UI with local fixture config: standalone agenda shows the dwell
input with no checklist; carousel shows the nested page-2 checkbox; neither
dropdown includes `agenda2`. Browser screenshot capture timed out, but DOM
interaction and snapshots passed; card PNG inspection completed separately.

An independent preflash audit against actual live baseline `059a04b` found no
source blocker. Deployment remains conditional on a clean pushed rebuild,
correct board/variant, matching source, passing preflash gate, and live status
verification. User authorized documented pushes and flash after visual tests;
target selection is pending. Firmware version remains `1.0.0-kitt27`.

## Earlier header changes

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
