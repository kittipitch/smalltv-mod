# Calendar weekday labels — 2026-10-08

## Subtitle-yellow separator — implemented, deployment pending, 2026-10-15

The owner requests more space on both sides and a bolder separator, clarifying
the color as subtitle-style bright yellow rather than amber. Reuse C_YELLOW
from Gfx.h (0xFFE0/#FFFF00), increase fillCircle radius2->3 (solid7px circle),
move center x48->51 and date x56->62. The widest row now leaves six/seven
blank columns around the dot, two more each side than the deployed version.
Weekday x20, original font/header baseline, time x160, title positions and
6px month/day gaps are unchanged. Hidden/relative-day guard is unchanged.
No new font, asset, configuration, version, dependency, pin or partition.

The source-derived test first failed on the old date slot. It now asserts
the actual palette's exact yellow, literal37-pixel rounded7px mask, six/seven
blank side gaps, no old gray residue/frame, original full-size ink/baseline,
clean visibility states, spacing/trace/bounds and boundary-date behavior.
Native on/off previews, settings/UI, rotation and C++ weekday checks pass.
Renderer SHA256:
9e990a9ed64addd7ad6877463a76eaeee321e484828b32dad7e01df6a4c84ca4.
Both units still runf3323b1; new exact visual/source audits and clean published
per-board artifacts/live gates remain required before conditional deployment.
No physical TFT approval or OTA clearance follows from host rendering alone.

## Round middle dot — deployed, 2026-10-15

The owner selected a frameless weekday/date separator after independent
Astra/Fable comparisons, then required it to be round, bold and large enough.
Replace the frame with the existing fillCircle primitive, radius 2: solid
5x5 bounds centered at x48/top+15, muted C_DIM. Original full-size text and
header baseline top+8 are restored. Widest dot row leaves four/five blank
columns between weekday ink and date; date/time/title x positions, existing
12px/6px text spacing and all hidden/relative-day guards are unchanged.
No font, bitmap, setting, version, allocation, partition, pin or updater change.

The source-derived host harness reuses the installed fillCircle routine.
Its new contract failed against the old baseline before implementation and
now checks the exact 21-pixel filled-circle mask (not a square or undersized
dot), absence of every other C_DIM pixel/frame, clean hidden/relative states,
original header baseline and full-size14px weekday ink. This replaces frame-
padding assertions because the owner explicitly removed the frame; checks
were not disabled to pass. On/off pages, settings/UI, rotation and C++ weekday
tests pass. Renderer SHA256:
8dc435e14682c3372831b52a676ad9d7d126963e53a02a26bd2764c0d22e19c3.

The owner authorized flashing after final visual/safety checks. Both Astra and
Fable directly inspected the exact final native on/off PNGs and returned GO:
visible, round, solid/bold, centered, on-theme, no contact/clipping or residue;
no adjustment required. This is the final 5px round dot, not an earlier square
prototype. Independent Astra/Codex/zagent/gagent/zask source/platform audits
returned conditional per-unit GO/DONE with no blocker; graphics transactions,
bounded geometry and unchanged PROGMEM/buffers/partitions/pins were checked.

Both clean committed builds and published f3323b1 preflash gates passed:

| Environment | Upload bytes / OTA headroom | MD5 |
|---|---|---|
| smalltv | 680736 / 36064 | bcc90c104a091937b1e63890b4c971fd |
| smalltv_sdpro | 674208 / 42592 | cbb30447352c06a257adc4f1f2fca362 |

Copied MD5/SHA256 matched. Fresh mDNS identity, correct board/SHA/oom and
private configuration backup gates preceded normal own/update uploads, home
first then office only after home health passed. Both returned OK. Both now
report f3323b1/kitt27 with correct Ultra/SDPRO boards, increasing uptime, oom 0,
connected/fresh synchronized clocks, active daemons and exact config retention.
Usage pushes OK, six events fetched, both scoped fleet audits PASS. Office's
first reboot-window calendar push timed out; later polling recovered.
No source change followed the exact reviews/build. No release/version, daemon
code, custom font/asset, settings, filesystem or board-variant change.
Private journal retains complete native images/reports, artifact SHA256s and
configuration backup/transfer paths. Physical TFT appearance remains unverified.
This completion commit changes documentation only; deployed SHA stays f3323b1.
Do not reflash solely to match a later documentation-only HEAD.

## Interior padding revision — deployed, 2026-10-15

The owner reports that the weekday letters in the deployed28x28 frame are too
close to its inner left/right edges. A size1 text trial solved clearance but
Astra rejected its native glance readability. That trial is not shipped.
The revised candidate keeps original size-2 text and uses a constant 36×36
rounded square for every weekday, providing7px painted-ink clearance on both
sides. Header text shifts down 4px uniformly to center the glyphs and align
dates/times; horizontal coordinates and title positions remain unchanged.
Today/Tomorrow and weekday-off remain unboxed. No setting or asset is added.

The native preview now asserts at least7px inner side clearance, the fixed
outline edges/corners and size2 text throughout. The old deployed layout
failed the left-clearance assertion before implementation. Both candidate
on/off previews, settings/UI, rotation, weekday tests and Ultra validation
build pass. Renderer SHA256:
46b2e7efbcb614a4195c704286c4ffcf353c6886b49c3673ddb493146b09bfe9.
Astra directly compared native baseline/candidate/off PNGs and returned GO
for owner preview approval: readable, centered, useful padding; larger frames
are more prominent and sit closer to titles, but no clipping is visible.
The earlier small-font NO-GO is retained rather than hidden.

The owner subsequently authorized renewed Astra PNG review, then home-first
and office-second updates, and publication of the documentation. Both units
now run clean published a1b0c83/kitt27, with the correct Ultra/SDPRO images.
Astra directly inspected the exact retained on/off native PNGs: visual GO.
Independent Astra, Codex, zagent, gagent and zask source/platform reviews all
returned conditional GO/DONE with no blocker, including the full office delta
from its pre-outline baseline. Balanced GFX transactions, bounded geometry,
unchanged PROGMEM/assets/partitions/pins/settings/updater were checked.
Fresh on/off previews, settings/UI, rotation and C++ weekday tests passed;
both clean PlatformIO builds and published preflash gates passed:

| Environment | Upload bytes / OTA headroom | MD5 |
|---|---|---|
| smalltv | 681168 / 35632 | f7c36234a5aa3a0c24b1e9ea02f933c4 |
| smalltv_sdpro | 674656 / 42144 | 067f570a86949ad8dacd5c8fca6dae60 |

Transferred MD5/SHA256 matched each local binary. Fresh mDNS identity, board,
SHA/oom/connectivity and private configuration backups preceded both normal
own/update uploads; both returned OK. Home verification passed before office
upload. Postboot both report a1b0c83 with their correct boards, increasing
uptime, oom 0, connected/link ok, fresh synced clocks and exact JSON configuration
preservation. Both daemons active, usage pushes OK, six events fetched, and
unit-scoped published-firmware fleet audits PASS. Brief reboot-window request
failures recovered. No daemon code, new setting, release/version bump, filesystem
or board-variant change. Rejected size1 text was never shipped.
The private journal retains full independent reports, fixture PNGs, artifact
SHA256s, transfer paths and private backup paths. Physical TFT appearance is
still unverified. This completion commit is documentation-only; deployed SHA
remains a1b0c83. Do not reflash solely to match a documentation-only HEAD.

## Home trial — rounded weekday outline, 2026-10-15

The owner approved trying a thin, muted rounded square on the home unit first.
One drawRoundRect call in the shared agenda renderer outlines visible two-letter
weekdays: 28x28 pixels, radius 4, existing C_DIM, no fill. Painted glyphs remain
centered; date/time/title coordinates and 12px/6px spacing stay unchanged.
The existing showWeekday setting hides both label and outline. Today/Tomorrow
remain unboxed on both pages. No setting, version, bitmap, PROGMEM access,
allocation, partition, pin, updater, or daemon change is introduced.

The source-derived native preview now uses the installed GFX outline routines
and checks four outline edges and a rounded corner per card, including hidden
and relative-day cases. The enabled test failed against the old renderer and
passed after the drawing call. On/off previews, settings/UI, rotation, and
weekday checks pass; Ultra validation build passes (681200-byte upload image).
Independent codex, Fable, zagent, gagent and zask reviews completed GO/DONE under
/tmp/calendar-badge-review.hqLc71; no blocking source or platform finding.
Fable also verified its actual model identity and preview geometry. The outline
sits one pixel below the card top; this is an aesthetic trial, not a collision.
The pixel checks do not explicitly detect a filled badge, but the reviewed
outline primitive and inspected native pixels confirm no fill in this change.
Final renderer SHA256:
6781b9cfbb7a4e7e5f800f3dd9c77e1a56e78ded11f95b33a3845c764192274c.

Home-only deployment complete: f661 now runs clean published95fc6d1/kitt27.
The owner confirmed the previously described publication/flash sequence and
requested Astra before upload. gpt-6-astra directly inspected native baseline,
on/off pages and enlarged contact sheets: GO, centered/readable/no clipping,
modest grouping improvement with slight visual clutter. Physical TFT/runtime
safety was not inferred from the PNG verdict.
Final Ultra gate passed:681168B, headroom35632B,
MD5e24eb5abed40aab46b6c18f0b61f2234, matched on pi5. Reviewed renderer and
source-diff hashes remain unchanged. Fresh mDNS identity and private config
backup preceded normal own /update OTA through pi5. Upload returned OK.
Postboot SHA/esp8266 correct, uptime30->55->105, oom0, connected, fresh clock;
configuration exactly preserved (carousel15s, weekday on). Daemon resumed,
usage push OK and six calendar events fetched; initial reboot-window calendar
push timed out, followed by later polls without another reported failure.
Home-scoped fleet audit PASS: identified published95fc6d1.
Office/2c21 and daemon source remain unchanged. Physical screen approval is
pending. This follow-up changes documentation only; do not reflash solely to
match a later documentation HEAD. FW_VERSION remains1.0.0-kitt27.

## Fleet spacing refinement — 12px / 6px

Deployment complete: final clean published b2065d7/kitt27, home Ultra first,
office SD PRO through ubuntu_office second, then owner-requested known Ultra
2c21. Per-target independent audit and fresh mDNS/config/variant/digest gates
passed. Clean builds: Ultra680720B/MD5e91072b922b718b2424d80ff6233cad3;
SDPRO674192B/MD586cfe1c182d827b9c0652e97609de20d. Both below716800B.
All uploads returned OK; live expected SHA/boards and oom0 were verified.
Home/office showed increasing uptime. 2c21 initially reported uptime19 and
fresh clock, then stopped responding to ping/HTTP; continuing health is not
confirmed while offline. Owner subsequently confirmed deliberately disconnecting
2c21 after the successful flash; no recovery is needed or attempted.
Home/office daemons active with six events; 2c21 has no newly set-up
daemon. Office/2c21 configs add default-on weekday only; home mode observation
changed agenda to carousel, owner confirmation pending. Physical screen
confirmation remains open. This follow-up changes documentation only; deployed
source and public FW_VERSION1.0.0-kitt27 remain unchanged.

Owner requested home first, then office SD PRO through ubuntu_office SSH,
and narrowed month/day spacing from 8px to 6px before either update started.
Weekday/month remains 12px. Only the shared renderer spacing constant and
corresponding native gap assertions change; controls and relative dates remain
unchanged. Both on/off previews, correct Ultra/SD PRO builds and a fresh
independent per-board flash audit are required before sequential fleet OTA.
Office starts from 059a04b; home starts from 84ef693. Configuration backups,
mDNS identity, clean published stamp/size/digest gates and live health checks
are required for each unit. No release/version change is requested.

On/off native previews, JSON/UI and rotation regressions pass. Independent
fleet audit reviewed the complete office source delta from059a04b and the
home spacing delta from84ef693, including platform/PROGMEM and SD PRO CS/OTA
hazards; conditional GO for home first, office after home verification.
Final renderer SHA256:
`02fa260a8c9dd6bd56d24c8b546d943da985e72bdd306ecba6aa9778bbd41dc6`.
The final clean published per-board rebuilds/gates remain mandatory before
uploads; earlier 8px images are not the requested source.

## Balanced date spacing

Owner requested a 12px weekday/month gap and subsequently specified an 8px
month/day gap. Agenda headers use 12px and 8px; ordinary size-2 glyph advances
remain 12px. The existing local date buffer is printed in space-separated
chunks, tracking the exact resulting end for the time collision guard. Cross-
month ranges narrow both month/day spaces. Both pages and weekday-off mode
share this path; relative Today/Tomorrow and right-aligned times are unchanged.
Preview assertions check both exact gaps rather than only the date origin.
Focused checks, build and independent preflash review precede another home OTA.
No global date helper, setting, version, or office unit is changed.

Final 12px/8px source-derived on/off previews, JSON/UI and rotation checks pass;
native card image inspected. Ultra validation build passes. Independent final
8px preflash review returned conditional GO without a firmware/platform finding.
Renderer SHA256 `84e39519524882d3dacb850b62d662368e0cfd78f9d2cb4d5eb1fd1fd7899587`.
Maximum legal 13-character cross-month range ends x204, within margin x220;
ordinary dates end at most x124, leaving 36px before time.

Home deployment complete: `84ef693` clean published Ultra image passed the
preflash gate (680,720 B; 36,080 B headroom; MD5
`c9ee3b91b1acd5059abc20a6354c7793`, matched on flash host). OTA returned OK;
status confirms SHA/esp8266, connected, fresh clock, oom0, uptime18 seconds.
All configuration is identical to backup, including weekday on and 15s dwell;
daemon resumed. The 12/10 candidate was never flashed. This post-deployment
documentation commit leaves the deployed source unchanged. Office untouched;
owner's physical spacing confirmation remains open.

## Weekday visibility checkbox

The owner approved one saved Show weekday choice for both agenda pages. It
defaults on and is stored as the boolean `calendar.showWeekday`. Existing
configs retain the approved header; partial updates preserve the saved value,
and non-boolean values are ignored. Disabling it removes only the absolute
date/range prefix, restoring date x20 while time stays x160. Today/Tomorrow
already omit the prefix and remain unchanged.

The web UI uses one checkbox node: below the dwell field in standalone Next
event, or below the nested page-2 toggle in the carousel's Next event row.
Other standalone modes hide it. The node is moved outside the carousel list
before rebuilding that list, preserving unsaved state through reorder and mode
switches. Load and Save use the same nested calendar setting; a missing calendar
feature omits that slice instead of resetting the preference.

Verification: `python3 scripts/test_agenda_weekday_setting.py` compiles the
actual CalendarSettings methods against installed ArduinoJson and checks
defaults, missing/invalid values, explicit booleans and nested round-trip. It
also runs actual UI functions for load, reorder, mode changes and Save while
checking node identity and feature absence. `python3 scripts/preview_agenda.py`
and `--no-weekday` pass both-page pixel/position/label checks; the off preview
was visually inspected. Its check fails when the renderer guard is removed in
a scratch-only negative control. Existing rotation regression passes.
`pio run -e smalltv` passes: RAM 46,860 B; flash 676,521 B. Existing unrelated
compiler warnings remain. Generated header and build outputs were backed up.

Parent browser QC passed actual control placement in both modes, geometry,
mode/reorder state preservation, one-node identity and checked-value collection.
Screenshot CDP capture timed out; semantic browser and layout checks passed.
Independent preflash audit returned conditional GO with no firmware defect,
after independently rerunning JSON/UI, rotation and both renderer previews.
Reviewed firmware delta from `4ceb637` SHA256:
`a3fffe7d380af02d78cdcf1af552924644b9b9c131d4f694b21e9126ae0020d5`.
Deployment complete: published `5198ba3` was clean-built and passed the Ultra
preflash gate (680,640 B; 36,160 B headroom; MD5
`fc86ab1caf1b9c560d931a551e9cbc40`). Home OTA returned OK. Live status confirmed
the expected SHA/esp8266, connected and fresh clock, uptime 26→74 seconds,
oom 0, resumed six-event daemon pushes, and the served checkbox UI. Live
partial config updates saved false and true without reboot; the preference
was restored to true. All previous configuration matched its backup after
excluding the one new default-on field. The 10px gap is retained.
Office was not updated; no firmware version or release changed. This follow-up
documentation commit does not change the deployed firmware source. Owner's
physical screen confirmation remains open. Dirty validation images remain
forbidden; only the clean published deployment image was used.

Trap: replacing carouselList.innerHTML destroys nested inputs. Moving the
single checkbox node out first avoids recreating it and losing an unsaved
choice; no duplicate IDs or synchronization callbacks are needed.

## Weekday/date gap follow-up

Owner approved Astra's spacing recommendation: widen the weekday/date gap
from 6px to 10px, without punctuation. Date moves from x50 to x54; time stays
x160. The longest valid range ends at x210, within the x220 card margin.
Today/Tomorrow and their times remain unchanged. Both pages share the change.
The source-derived preview position check requires x54. Native visual check,
independent flash audit and clean published Ultra build are required before
the authorized home-only update. No version bump or office update is requested.

Validation complete: both-page native preview and exact position/label/edge
checks passed, rotation regression passed, and Ultra validation build passed.
Independent preflash audit returned conditional GO with no source/platform
finding. Reviewed renderer SHA256:
`e0910a6a62a790a58bba95665737130dd817cf663e76369e5920da342d7dd308`.
The one-constant firmware change adds no PROGMEM, allocation or partition risk.
Final clean published build/stamp/size/digest and live post-OTA checks remain
required; physical confirmation follows the owner-authorized home update.

## Final left-grouped header (supersedes the layouts below)

Both independent visual readers, Fable and Astra, preferred the weekday before
absolute dates/ranges, hidden for Today/Tomorrow. The owner approved two-letter
codes: Mo Tu We Th Fr Sa Su. Full Tomorrow is retained; single-day times remain
right-aligned and all-day/multi-day events still omit time. Both agenda pages
share this renderer. The two-letter prefix plus 6px gap and longest 13-character
range use 186px of the 200px header width. Date colors and titles are unchanged.

Tomorrow uses the existing synchronized local clock and mktime calendar
normalization, including month/year/leap/DST boundaries. Without a valid clock,
the absolute date and weekday remain visible. Multi-day ranges always win over
relative labels. The existing weekday validation helper is reused unchanged.

Verification: native source-derived pixel previews and exact position/label
checks pass, along with year rollover, leap day, spring/fall DST and no-clock
renderer checks. Standalone page rotation regression passes. Ultra validation
build passes. Independent mandatory preflash review and clean published rebuild
are the final gates before the authorized home-only update; office is untouched.
FW_VERSION remains 1.0.0-kitt27. No release or version bump is requested.

Trap: time must not reserve the old rightmost weekday column after moving the
weekday left. Its original right margin is restored, keeping Tomorrow readable.

Independent final preflash audit returned conditional GO with no firmware
defect or ESP8266/PROGMEM/partition hazard. Renderer SHA256 is
`4bdf6198f9f36abdb34c018add366408794687aa029e025251d932ed20f9738e`;
the reviewed firmware delta against home `7916aed` is unchanged. The auditor
independently reran the preview, rotation, weekday and tomorrow-boundary checks.
Its minor preview-report count correction is incorporated here. Final clean
published rebuild/stamp/digest/live identity checks remain required before OTA.

## Centered weekday follow-up

The owner saw `Today R` on the home unit and requested more separation. The
weekday now occupies the horizontal center of a 240px header (glyph x=114,
12px advance); date remains left and time remains right. Long ranges that
extend into the center keep the weekday 6px after the date to avoid overlap.
The time collision check accounts for the weekday's actual new position.

The native preview's position assertion failed before implementation and
passed afterward. The resulting two-page PNG was visually inspected: Today,
single dates and their weekdays have clear spacing; multi-day ranges fit;
single-day HH:MM remains visible. Source-based rotation regression still passes.
This is a follow-up to home deployment `7916aed`; the same home-only rollout
authorization applies. Office 6a12 remains unchanged.
Ultra validation build passed, and independent follow-up audit found no source
blocker or new ESP8266/PROGMEM/partition hazard. Clean published rebuild and
preflash gate are required before the home update.

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
