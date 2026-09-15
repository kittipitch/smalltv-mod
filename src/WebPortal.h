// WebPortal.h — HTTP config UI, REST API, and OTA endpoint
#pragma once
#include <Arduino.h>
#include "Settings.h"

void webPortalBegin(Settings& settings);
void webPortalLoop();
bool webPortalRebootDue();   // main polls this and calls ESP.restart()

// millis() of the last accepted push from the daemon (0 = none since boot). Any
// such request proves the daemon is still reaching us, which is the strongest
// evidence of a working link the device has -- see OfflineScreen.cpp.
uint32_t webPortalLastPushMs();
