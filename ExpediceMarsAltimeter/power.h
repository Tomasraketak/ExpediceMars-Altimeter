#pragma once
#include <Arduino.h>

void powerBegin();
void rfSwitch(bool on);          // napajeni VF prepinace antény na XIAO C6
void lightSleepFor(uint32_t ms); // uspi cip mezi vzorky (probudi timer nebo tlacitko)
void enterDeepSleep();           // "vypnuto" - probudi jen tlacitko
bool wokeFromDeepSleep();
