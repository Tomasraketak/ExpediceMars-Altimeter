// =====================================================================
//  ExpediceMars Altimeter
//  Vyskomer pro modely raket - Seeed XIAO ESP32-C6 + BMP580 (I2C)
//
//  Co to umi
//  ---------
//   * drzenim tlacitka 3 s (nebo pres web) se ulozi aktualni vyska jako
//     startovni - reference zustava i po odpojeni baterie (NVS)
//   * po ozbrojeni meri 10x za sekundu a drzi kruhovy buffer 15 vzorku
//   * prekroceni +10 m nad startem = start letu; od te chvile se kazdych
//     100 ms zapisuje vyska do flash (vcetne 5 vzorku pred startem z bufferu)
//   * konec zaznamu: po apogeu, pod 25 m a rychlost < 1 m/s aspon 1 s v kuse
//   * apogeum = aktualni vyska klesla o vic nez 10 m pod dosud nejvyssi
//   * vse prezije reset i vypadek napajeni: vzorky se splachuji do flash
//     kazdou sekundu, apogeum se zalohuje do NVS kazdych 250 ms a po
//     restartu zaznam plynule pokracuje ve stejnem souboru
//   * WiFi AP + web: prohlizeni letu, prejmenovani, graf, export CSV
//   * duraz na spotrebu: mezi vzorky light sleep, "vypnuto" = deep sleep
//
//  Ovladani tlacitkem
//  ------------------
//   kratky stisk   - probudi / vyblika posledni apogeum
//   dvojklik       - zapne / vypne WiFi s webem
//   drzet 3 s      - ulozi startovni vysku a ozbroji (po 3 s cvrnkne LED)
//   drzet 8 s      - vypne (deep sleep), probudi zase stisk tlacitka
//                    (od 3 s LED blika cim dal rychleji, v 8 s sviti)
//   web: Testovaci let - start hned bez prahu, konec nejdriv po 10 s
// =====================================================================

#include "config.h"
#include "state.h"
#include "sensor.h"
#include "storage.h"
#include "ui.h"
#include "power.h"
#include "webui.h"

AltimeterState st;

const char *modeName(Mode m) {
  switch (m) {
    case MODE_IDLE:    return "IDLE";
    case MODE_ARMED:   return "ARMED";
    case MODE_ASCENT:  return "ASCENT";
    case MODE_DESCENT: return "DESCENT";
    case MODE_LANDED:  return "LANDED";
  }
  return "?";
}

// --- kruhovy buffer predstartovnich vzorku (zadani: 15 zaznamu) ---
struct PreSample { uint32_t ms; float alt; };
static PreSample s_pre[PRELOG_SAMPLES];
static uint8_t   s_preN = 0, s_preHead = 0;

static uint32_t s_nextSample = 0;
static uint32_t s_slowSince = 0;   // od kdy je po apogeu pod LAND_ALT_M a pomalu (0 = neni)
static uint8_t  s_aboveCnt = 0;
static uint8_t  s_vbatDiv = 0;

static inline uint32_t flightTimeNow() {
  return st.flightTOfs + (millis() - st.flightT0);
}

static void preReset() { s_preN = 0; s_preHead = 0; }

static void prePush(uint32_t ms, float alt) {
  s_pre[s_preHead].ms = ms;
  s_pre[s_preHead].alt = alt;
  s_preHead = (s_preHead + 1) % PRELOG_SAMPLES;
  if (s_preN < PRELOG_SAMPLES) s_preN++;
}

// index nejstarsiho vzorku v bufferu
static inline uint8_t preOldest() {
  return (s_preN < PRELOG_SAMPLES) ? 0 : s_preHead;
}

// ---------------------------------------------------------------- rezimy

static void applyLedForMode() {
  switch (st.mode) {
    case MODE_IDLE:    uiSet(webActive() ? UI_WIFI : UI_IDLE); break;
    case MODE_ARMED:   uiSet(UI_ARMED); break;
    case MODE_ASCENT:
    case MODE_DESCENT: uiSet(UI_FLIGHT); break;
    case MODE_LANDED:
      if (st.lastApogee >= 1.0f) uiBlinkNumber((int)lroundf(st.lastApogee));
      else uiSet(UI_IDLE);
      break;
  }
}

static void setMode(Mode m) {
  if (st.mode == m) return;
  st.mode = m;
  s_nextSample = millis();     // jina perioda -> necekat na dobehnuti te stare
  Serial.printf("[mode] %s\n", modeName(m));
  applyLedForMode();
}

// ---------------------------------------------------------------- let

static void resetFlightVars() {
  st.maxAlt = 0;
  st.maxAltT = 0;
  st.maxSpeed = 0;
  st.apogeeDone = false;
  st.recovered = false;
  st.testFlight = false;
  st.flightTOfs = 0;
  st.flightT = 0;
  s_aboveCnt = 0;
  preReset();
}

static void startFlight(bool test) {
  uint32_t now = millis();
  uint8_t o = preOldest();
  uint32_t t0 = (s_preN > 0) ? s_pre[o].ms : now;

  uint32_t epochT0 = 0;
  uint32_t e = nowEpoch();
  if (e) epochT0 = e - (now - t0) / 1000UL;

  st.flightT0 = t0;
  st.flightTOfs = 0;
  st.testFlight = test;
  st.flightId = logStart(epochT0, test);
  st.recovered = false;

  // Nejdriv predstartovni vzorky z kruhoveho bufferu (~0,5 s pred startem)
  for (uint8_t i = 0; i < s_preN; i++) {
    uint8_t idx = (o + i) % PRELOG_SAMPLES;
    logSample(s_pre[idx].ms - t0, s_pre[idx].alt);
    if (s_pre[idx].alt > st.maxAlt) { st.maxAlt = s_pre[idx].alt; st.maxAltT = s_pre[idx].ms - t0; }
  }
  logFlushIfDue(true);

  st.flightT = flightTimeNow();
  progressSave(true);
  s_slowSince = 0;

  if (test) Serial.printf("[let] START id=%u  TESTOVACI LET (bez prahu)\n", st.flightId);
  else      Serial.printf("[let] START id=%u  (prah %.1f m)\n", st.flightId, st.launchAltM);
  setMode(MODE_ASCENT);
}

static void endFlight(const char *why) {
  logFinalize();
  st.lastFlightId = st.flightId;
  st.lastApogee = st.maxAlt;
  cfgSaveLast(st.lastFlightId, st.lastApogee);
  progressClear();

  Serial.printf("[let] KONEC id=%u  apogeum %.1f m  v %.1f s  (%s)\n",
                st.flightId, st.maxAlt, st.maxAltT / 1000.0f, why);

  st.flightId = 0;
  st.testFlight = false;
  st.armed = false;
  cfgSaveArmed(false);
  st.landedAt = millis();
  noteActivity();
  st.mode = MODE_IDLE;          // aby setMode() urcite prekreslil LED
  setMode(MODE_LANDED);
}

static void flightSample() {
  uint32_t ft = flightTimeNow();
  float alt = st.altAgl;

  logSample(ft, alt);
  st.flightT = ft;

  // Po detekci apogea uz se maximum nemeni. Fyzicky by nemelo, a hlavne to
  // chrani apogeum pred nesmyslem po obnove z resetu - kdyz se baterka pri
  // dopadu vypoji a znovu se zapoji az doma (jina nadm. vyska / pocasi),
  // "vyska" muze vyjit vyssi nez skutecne apogeum.
  if (!st.apogeeDone) {
    if (alt > st.maxAlt) { st.maxAlt = alt; st.maxAltT = ft; }
    if (st.vSpeed > st.maxSpeed) st.maxSpeed = st.vSpeed;
  }

  // Apogeum: vyska je o vic nez apogeeDropM pod dosud nejvyssi namerenou
  if (!st.apogeeDone && (st.maxAlt - alt) > st.apogeeDropM) {
    st.apogeeDone = true;
    Serial.printf("[let] APOGEUM %.1f m v %.2f s\n", st.maxAlt, st.maxAltT / 1000.0f);
    progressSave(true);
    setMode(MODE_DESCENT);
  }

  // Konec zaznamu: jen po apogeu, vyska pod LAND_ALT_M a zaroven se meni
  // (klesa nebo stoupa) pomaleji nez LAND_SPEED_MS - nepretrzite aspon
  // LAND_STABLE_MS. Jakmile podminka byt jen jednou neplati, pocita se znovu.
  // Testovaci let: prvnich TEST_LAND_BLOCK_MS je konec zablokovany, pak
  // se misto apogea nevyzaduje nic.
  bool landArmed = st.testFlight ? (ft >= TEST_LAND_BLOCK_MS) : st.apogeeDone;
  if (landArmed && alt < LAND_ALT_M && fabsf(st.vSpeed) < LAND_SPEED_MS) {
    uint32_t now = millis();
    if (s_slowSince == 0) s_slowSince = now ? now : 1;
    else if ((now - s_slowSince) >= LAND_STABLE_MS && ft >= MIN_FLIGHT_MS) {
      endFlight("pristani");
      return;
    }
  } else {
    s_slowSince = 0;
  }

  if (ft >= MAX_FLIGHT_MS) { endFlight("casovy limit"); return; }

  logFlushIfDue(false);
  progressSave(false);
}

// Navazani na let preruseny resetem nebo vypadkem napajeni
static void recoverFlight(uint16_t id) {
  progressRestore();
  ResumeInfo ri;
  if (!logResume(id, &ri)) {
    // Soubor chybi nebo je poskozeny (napr. dosla flash). Apogeum ale mame
    // v NVS, takze let NEZAHAZUJEME - pokracujeme bez zapisu profilu
    // (logSample() bez otevreneho souboru nic nedela) a pri pristani se
    // apogeum ulozi jako posledni let.
    Serial.printf("[obnova] soubor letu %u chybi, pokracuji jen s apogeem z NVS\n", id);
    ri = ResumeInfo();
  }
  // Ze souboru si vezmeme to, co v nem uz je - muze byt novejsi nez zaloha
  // v NVS (v souboru muze byt vzorek, ktery se do NVS nestihl promitnout).
  // Kdyz uz ale bylo apogeum, plati NVS: pri detekci apogea se zapisuje
  // vzdy (force) a od te chvile se maximum nemeni.
  if (!st.apogeeDone) {
    if (ri.maxAlt > st.maxAlt) { st.maxAlt = ri.maxAlt; st.maxAltT = ri.maxAltT; }
    if (ri.maxSpeed > st.maxSpeed) st.maxSpeed = ri.maxSpeed;
  }

  // Navazujeme za pozdejsi z obou znamych bodu, aby cas v zaznamu nikdy
  // neskocil zpatky. +300 ms je odhad mezery, kterou zabral samotny restart.
  uint32_t base = (ri.lastT > st.flightT) ? ri.lastT : st.flightT;
  if (st.flightT < base) st.flightT = base;
  st.flightTOfs = base + 300;
  st.flightT0 = millis();
  st.recovered = true;
  logMarker(st.flightTOfs - 150);      // znacka preruseni v grafu
  s_slowSince = 0;

  Serial.printf("[obnova] pokracuji v letu %u, dosud max %.1f m, apogeum %s\n",
                id, st.maxAlt, st.apogeeDone ? "uz bylo" : "jeste ne");
  st.mode = MODE_IDLE;
  setMode(st.apogeeDone ? MODE_DESCENT : MODE_ASCENT);
}

// ---------------------------------------------------------------- akce

static void doZeroAndArm() {
  Serial.println("[akce] kalibrace startovni vysky");
  uiSet(UI_CAL);
  if (!sensorCalibratePad()) {
    Serial.println("[akce] kalibrace SELHALA");
    uiSet(UI_ERROR);
    return;
  }
  resetFlightVars();
  st.armed = true;
  cfgSaveArmed(true);
  cfgAckLast();
  Serial.printf("[akce] reference %.1f Pa, ozbrojeno\n", st.padPa);
  st.mode = MODE_IDLE;
  setMode(MODE_ARMED);
  uiSet(UI_CONFIRM);
  noteActivity();
}

// Testovaci let: kalibrace startovni vysky a hned start zaznamu (bez prahu).
static void doTestFlight() {
  Serial.println("[akce] testovaci let");
  uiSet(UI_CAL);
  if (!sensorCalibratePad()) {
    Serial.println("[akce] kalibrace SELHALA");
    uiSet(UI_ERROR);
    return;
  }
  resetFlightVars();
  st.armed = true;
  cfgSaveArmed(true);
  cfgAckLast();
  noteActivity();
  startFlight(true);
}

static void doDisarm() {
  st.armed = false;
  cfgSaveArmed(false);
  if (st.mode == MODE_ARMED) setMode(MODE_IDLE);
  uiSet(UI_CONFIRM);
  noteActivity();
}

static void toggleWeb() {
  if (webActive()) webStop();
  else webBegin();
  applyLedForMode();
  noteActivity();
}

static void handleButton(BtnEvent ev) {
  noteActivity();
  switch (ev) {
    case BTN_SHORT:
      if (st.lastApogee >= 1.0f && (st.mode == MODE_IDLE || st.mode == MODE_LANDED)) {
        uiBlinkNumber((int)lroundf(st.lastApogee));
        cfgAckLast();        // uzivatel apogeum videl
      } else
        uiSet(UI_CONFIRM);
      break;
    case BTN_DOUBLE:
      toggleWeb();
      break;
    case BTN_ARM:
      if (st.flightId) { uiSet(UI_ERROR); break; }   // behem letu nekalibrujeme
      doZeroAndArm();
      break;
    case BTN_OFF:
      if (st.flightId) { uiSet(UI_ERROR); break; }   // behem letu se nevypina
      if (webActive()) webStop();
      enterDeepSleep();
      break;
    default: break;
  }
}

static void handleWebRequests() {
  if (webReq.zero)   { webReq.zero = false;   if (!st.flightId) doZeroAndArm(); }
  if (webReq.arm)    { webReq.arm = false;
    if (st.padPa > 1000.0f && !st.flightId) {
      resetFlightVars();
      st.armed = true;
      cfgSaveArmed(true);
      cfgAckLast();
      st.mode = MODE_IDLE;
      setMode(MODE_ARMED);
    } else uiSet(UI_ERROR);
  }
  if (webReq.test)   { webReq.test = false;   if (!st.flightId) doTestFlight(); else uiSet(UI_ERROR); }
  if (webReq.disarm) { webReq.disarm = false; if (!st.flightId) doDisarm(); }
  if (webReq.sleep)  { webReq.sleep = false;
    if (st.flightId) uiSet(UI_ERROR);
    else { delay(400); webStop(); enterDeepSleep(); }
  }
}

// ---------------------------------------------------------------- smycka

static void doSample() {
  if (!sensorRead()) return;

  if (++s_vbatDiv >= 50) { s_vbatDiv = 0; st.vbat = sensorReadVbat(); }

  switch (st.mode) {
    case MODE_ARMED:
      prePush(millis(), st.altAgl);
      if (st.padPa > 1000.0f && st.altAgl >= st.launchAltM) {
        if (++s_aboveCnt >= 2) startFlight(false);   // dva vzorky po sobe = neni to zakmit
      } else {
        s_aboveCnt = 0;
      }
      break;

    case MODE_ASCENT:
    case MODE_DESCENT:
      flightSample();
      break;

    default:
      break;
  }
}

static void autoTimeouts() {
  uint32_t now = millis();

  if (webActive() && webIdleMs() > AP_TIMEOUT_MS) {
    webStop();
    applyLedForMode();
  }
  if (st.mode == MODE_LANDED && (now - st.landedAt) > LANDED_AWAKE_MS) {
    setMode(MODE_IDLE);
  }
  if (st.mode == MODE_IDLE && !webActive() && !st.armed && !usbConnected() &&
      (now - st.lastActivity) > IDLE_TO_SLEEP_MS) {
    Serial.println("[power] necinnost -> deep sleep");
    enterDeepSleep();
  }
#if ARMED_TIMEOUT_MS > 0
  if (st.mode == MODE_ARMED && !usbConnected() && (now - st.lastActivity) > ARMED_TIMEOUT_MS) {
    enterDeepSleep();
  }
#endif
}

void setup() {
  Serial.begin(115200);
  powerBegin();
  uiBegin();
  buttonBegin();

  bool fromSleep = wokeFromDeepSleep();
  if (!fromSleep) delay(300);          // chvilka na pripojeni seriove konzole

  if (!storageBegin()) Serial.println("[init] CHYBA: LittleFS / NVS");
  cfgLoad();
  st.vbat = sensorReadVbat();

  if (!sensorBegin()) Serial.println("[init] CHYBA: BMP580 neodpovida");

  Serial.printf("\nExpediceMars vyskomer %s, %lu. zapnuti, probuzeni: %s\n",
                FW_VERSION, (unsigned long)st.bootCount, fromSleep ? "tlacitkem" : "reset/zapnuti");
  Serial.printf("[init] reference %.1f Pa, ozbrojeno: %s, flash %u/%u B\n",
                st.padPa, st.armed ? "ano" : "ne",
                (unsigned)fsUsedBytes(), (unsigned)fsTotalBytes());

  // Byl let prerusen resetem? Pak v nem plynule pokracujeme.
  uint16_t pending = progressFlightId();
  if (pending) {
    recoverFlight(pending);
  } else if (st.armed && st.padPa > 1000.0f) {
    resetFlightVars();
    setMode(MODE_ARMED);               // ozbrojeni prezije i reset na rampe
  } else if (st.lastApogee >= 1.0f && cfgLastUnread()) {
    // Let skoncil, ale apogeum jeste nikdo nevidel (reset / vypojena baterka
    // po pristani) -> rovnou ho vyblikat, jako by se prave pristalo.
    st.landedAt = millis();
    setMode(MODE_LANDED);
  } else {
    setMode(MODE_IDLE);
  }
  applyLedForMode();

  noteActivity();
  s_nextSample = millis();
}

void loop() {
  uiTick();

  BtnEvent ev = buttonPoll();
  if (ev != BTN_NONE) handleButton(ev);

  handleWebRequests();

  bool inFlight = (st.mode == MODE_ASCENT || st.mode == MODE_DESCENT);
  if (webActive() && !inFlight) webTick();   // behem letu web neobsluhujeme

  uint32_t period = (st.mode == MODE_IDLE || st.mode == MODE_LANDED)
                      ? IDLE_PERIOD_MS : SAMPLE_PERIOD_MS;

  if ((int32_t)(millis() - s_nextSample) >= 0) {
    s_nextSample += period;
    // kdyz nas neco na dlouho zdrzelo, neposilame se dohanet zamesskane
    if ((int32_t)(millis() - s_nextSample) > (int32_t)period)
      s_nextSample = millis() + period;
    doSample();
  }

  autoTimeouts();

  // Kdyz uz LED dohrala jednorazovy vzor, vratime vzor podle rezimu
  if (uiCurrent() == UI_OFF) applyLedForMode();

  int32_t wait = (int32_t)(s_nextSample - millis());
  if (wait > 0) {
    if (inFlight)         delay(wait > 4 ? 4 : wait);
    else if (webActive()) delay(1);
    else                  lightSleepFor((uint32_t)wait);
  }
}
