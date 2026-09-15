// RadarClient.h — fetches nearby aircraft (adsb.fi direct, or a LAN webhook).
#pragma once
#include <Arduino.h>
#include "Settings.h"
#include "RadarData.h"

void radarInit(const Settings& s);       // reset + poll ASAP
void radarService(const Settings& s);    // call often; self-times the polling
void radarForceRefresh();                // poll on the next service() call

// Radar's own lat/lon (Settings.radar.lat/lon), falling back to the weather
// location (Settings.calendar.lat/lon) when radar's own is unset -- see
// RadarClient.cpp for the reasoning. Exported so RadarMode's airport-geo and
// "no home set" prompt agree with what radarService() actually fetches.
float radarHomeLat(const Settings& s);
float radarHomeLon(const Settings& s);

uint8_t         radarCount();            // aircraft currently held (nearest first)
const Aircraft& aircraftAt(uint8_t i);
uint32_t        radarLastOkMs();         // millis() of last good fetch (0 = never)
bool            radarError();            // most recent fetch failed
