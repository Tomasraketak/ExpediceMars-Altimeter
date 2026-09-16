#include "ui.h"
#include "config.h"

// =====================================================================
//  LED / bzucak
//  Vzor je posloupnost delek, ktere se stridaji zapnuto/vypnuto.
//  Prehrava se v uiTick() bez jedineho delay().
// =====================================================================

#define PAT_MAX 96
static uint16_t  s_pat[PAT_MAX];
static uint8_t   s_patN = 0;
static uint8_t   s_idx = 0;
static uint32_t  s_stepStart = 0;
static bool      s_repeat = false;
static UiPattern s_current = UI_OFF;
static UiPattern s_after = UI_OFF;   // na co se prepnout po dobehnuti

static void ledWrite(bool on) {
#if LED_ACTIVE_LOW
  digitalWrite(PIN_LED, on ? LOW : HIGH);
#else
  digitalWrite(PIN_LED, on ? HIGH : LOW);
#endif
#if PIN_BUZZER >= 0
  digitalWrite(PIN_BUZZER, on ? HIGH : LOW);
#endif
}

void uiAllOff() { ledWrite(false); }

void uiBegin() {
  pinMode(PIN_LED, OUTPUT);
#if PIN_BUZZER >= 0
  pinMode(PIN_BUZZER, OUTPUT);
#endif
  ledWrite(false);
  uiSet(UI_OFF);
}

static void loadPattern(const uint16_t *d, uint8_t n, bool repeat) {
  s_patN = n > PAT_MAX ? PAT_MAX : n;
  memcpy(s_pat, d, s_patN * sizeof(uint16_t));
  s_repeat = repeat;
  s_idx = 0;
  s_stepStart = millis();
  ledWrite(s_patN > 0);
}

void uiSet(UiPattern p) {
  s_current = p;
  s_after = UI_OFF;
  switch (p) {
    case UI_OFF:     s_patN = 0; ledWrite(false); return;
    case UI_IDLE:    { static const uint16_t d[] = {25, 4000};            loadPattern(d, 2, true); } return;
    case UI_ARMED:   { static const uint16_t d[] = {30, 120, 30, 2800};   loadPattern(d, 4, true); } return;
    case UI_FLIGHT:  { static const uint16_t d[] = {60, 60};              loadPattern(d, 2, true); } return;
    case UI_CAL:     { static const uint16_t d[] = {50, 50};              loadPattern(d, 2, true); } return;
    case UI_WIFI:    { static const uint16_t d[] = {20, 180, 20, 1500};   loadPattern(d, 4, true); } return;
    case UI_CONFIRM: { static const uint16_t d[] = {60, 60, 60, 60, 400}; loadPattern(d, 5, false); } return;
    case UI_ERROR:   { static const uint16_t d[] = {500, 150, 120, 150, 500}; loadPattern(d, 5, false); } return;
    case UI_BLIP1:   { static const uint16_t d[] = {70, 200};             loadPattern(d, 2, false); } return;
    case UI_BLIP2:   { static const uint16_t d[] = {70, 90, 70, 200};     loadPattern(d, 4, false); } return;
    default:         s_patN = 0; ledWrite(false); return;
  }
}

UiPattern uiCurrent() { return s_current; }

// Vyblika cislo po cislicich: kazda cislice = odpovidajici pocet bliknuti,
// nula = jedno dlouhe bliknuti. Mezi cislicemi delsi pauza.
// Napr. 137 m = blik / pauza / 3x blik / pauza / 7x blik.
void uiBlinkNumber(int value) {
  if (value < 0) value = 0;
  if (value > 9999) value = 9999;

  char num[8];
  snprintf(num, sizeof num, "%d", value);

  uint16_t d[PAT_MAX];
  uint8_t n = 0;
  d[n++] = 1;      // zacneme "zhasnuto" (prvni prvek je vzdy ON, 1 ms = nic)
  d[n++] = 900;

  for (const char *c = num; *c && n < PAT_MAX - 4; c++) {
    int digit = *c - '0';
    if (digit == 0) {
      d[n++] = 700;  // nula = dlouhe bliknuti
      d[n++] = 250;
    } else {
      for (int i = 0; i < digit && n < PAT_MAX - 3; i++) {
        d[n++] = 120;
        d[n++] = 250;
      }
    }
    if (n < PAT_MAX - 1) d[n - 1] = 1100;   // delsi pauza mezi cislicemi
  }
  if (n < PAT_MAX - 1) { d[n++] = 1; d[n++] = 2500; }  // pauza pred opakovanim

  s_current = UI_DIGITS;
  loadPattern(d, n, true);
  s_after = UI_OFF;
}

void uiTick() {
  if (s_patN == 0) return;
  uint32_t now = millis();
  if ((now - s_stepStart) < s_pat[s_idx]) return;

  s_stepStart = now;
  s_idx++;
  if (s_idx >= s_patN) {
    if (s_repeat) {
      s_idx = 0;
    } else {
      s_patN = 0;
      ledWrite(false);
      UiPattern nxt = s_after;
      s_current = UI_OFF;
      if (nxt != UI_OFF) uiSet(nxt);
      return;
    }
  }
  ledWrite((s_idx % 2) == 0);
}

// =====================================================================
//  Tlacitko - debounce + rozliseni kratky / dvojklik / 3 s / 8 s
// =====================================================================

static bool     s_down = false;
static bool     s_rawLast = false;
static uint32_t s_changeAt = 0;
static uint32_t s_downAt = 0;
static bool     s_blip1 = false, s_blip2 = false;
static bool     s_pendShort = false;
static uint32_t s_pendAt = 0;

static inline bool rawPressed() {
#if BUTTON_ACTIVE_LOW
  return digitalRead(PIN_BUTTON) == LOW;
#else
  return digitalRead(PIN_BUTTON) == HIGH;
#endif
}

void buttonBegin() {
#if BUTTON_ACTIVE_LOW
  pinMode(PIN_BUTTON, INPUT_PULLUP);
#else
  pinMode(PIN_BUTTON, INPUT_PULLDOWN);
#endif
  s_down = s_rawLast = rawPressed();
  s_changeAt = millis();
}

bool buttonIsDown() { return s_down; }

BtnEvent buttonPoll() {
  uint32_t now = millis();
  bool raw = rawPressed();

  if (raw != s_rawLast) {
    s_rawLast = raw;
    s_changeAt = now;
  } else if (raw != s_down && (now - s_changeAt) >= BTN_DEBOUNCE_MS) {
    s_down = raw;
    if (s_down) {
      // stisk
      s_downAt = now;
      s_blip1 = s_blip2 = false;
    } else {
      // pusteni - rozhodujeme podle delky drzeni
      uint32_t held = now - s_downAt;
      if (held >= BTN_OFF_MS) return BTN_OFF;
      if (held >= BTN_ARM_MS) return BTN_ARM;
      if (held <= BTN_SHORT_MAX_MS) {
        if (s_pendShort && (now - s_pendAt) <= BTN_DOUBLE_GAP_MS) {
          s_pendShort = false;
          return BTN_DOUBLE;
        }
        s_pendShort = true;
        s_pendAt = now;
      }
      // 1..3 s = zrusene drzeni, nic nedelame
    }
  }

  // Zpetna vazba behem drzeni: 3 s = "pust a ozbroji se", 8 s = "pust a vypne se"
  if (s_down) {
    uint32_t held = now - s_downAt;
    if (!s_blip1 && held >= BTN_ARM_MS) { s_blip1 = true; uiSet(UI_BLIP1); }
    if (!s_blip2 && held >= BTN_OFF_MS) { s_blip2 = true; uiSet(UI_BLIP2); }
  }

  // Kratky stisk potvrdime az kdyz uz nemuze prijit druhy klik
  if (s_pendShort && (now - s_pendAt) > BTN_DOUBLE_GAP_MS) {
    s_pendShort = false;
    return BTN_SHORT;
  }
  return BTN_NONE;
}

// Kolik ms zbyva do dalsi zmeny stavu LED. Rizeni napajeni podle toho
// zkrati light sleep, aby vzor blikani zustal presny.
uint32_t uiNextEventIn() {
  if (s_patN == 0) return 0xFFFFFFFFUL;
  uint32_t el = millis() - s_stepStart;
  return (el >= s_pat[s_idx]) ? 0 : (s_pat[s_idx] - el);
}
