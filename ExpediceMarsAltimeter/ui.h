#pragma once
#include <Arduino.h>

// LED (a volitelne bzucak) - neblokujici prehravac vzoru
enum UiPattern : uint8_t {
  UI_OFF = 0,
  UI_IDLE,       // odzbrojeno: kratke bliknuti co 4 s
  UI_ARMED,      // ozbrojeno: dve kratka bliknuti co 3 s
  UI_FLIGHT,     // za letu: rychle blikani
  UI_CAL,        // probiha kalibrace startovni vysky
  UI_CONFIRM,    // potvrzeni akce
  UI_ERROR,      // chyba
  UI_BLIP1,      // jedno cvrnknuti (drz dal = ARM)
  UI_BLIP2,      // dve cvrnknuti (drz dal = vypnuti)
  UI_WIFI,       // zapnute AP
  UI_DIGITS      // vyblikane apogeum
};

void uiBegin();
void uiTick();
void uiSet(UiPattern p);
UiPattern uiCurrent();
void uiBlinkNumber(int value);   // vyblika cislo (apogeum v metrech) po cislicich
void uiAllOff();
uint32_t uiNextEventIn();   // za kolik ms se LED prepne (kvuli light sleepu)

// Tlacitko
enum BtnEvent : uint8_t {
  BTN_NONE = 0,
  BTN_SHORT,    // kratky stisk
  BTN_DOUBLE,   // dvojklik -> WiFi
  BTN_ARM,      // drzeni 3 s -> ulozit startovni vysku + ARM
  BTN_OFF       // drzeni 8 s -> vypnout
};

void     buttonBegin();
BtnEvent buttonPoll();
bool     buttonIsDown();
