#pragma once
#include <Arduino.h>
#include "config.h"

// Rezimy vyskomeru
enum Mode : uint8_t {
  MODE_IDLE = 0,    // odzbrojeno, 1 Hz, po chvili usne do deep sleepu
  MODE_ARMED = 1,   // ceka na startu, 10 Hz, mezi vzorky light sleep
  MODE_ASCENT = 2,  // let - stoupani, zapisuje se do flash
  MODE_DESCENT = 3, // let - po apogeu, zapisuje se dal
  MODE_LANDED = 4   // po pristani, blika apogeum, drzi WiFi k dispozici
};

struct AltimeterState {
  Mode     mode = MODE_IDLE;
  bool     armed = false;
  bool     sensorOk = false;

  // --- konfigurace (v NVS) ---
  float    padPa = 0.0f;                    // referencni tlak startovni plochy [Pa]
  float    padMsl = DEF_PAD_MSL_M;          // nadm. vyska startovni plochy [m]
  float    launchAltM = DEF_LAUNCH_ALT_M;   // prah startu [m AGL]
  float    apogeeDropM = DEF_APOGEE_DROP_M; // pokles pro detekci apogea [m]

  // --- zive hodnoty ---
  float    pressPa = 0.0f;
  float    tempC = 0.0f;
  float    altAgl = 0.0f;      // filtrovana vyska nad startem [m]
  float    altRawAgl = 0.0f;   // nefiltrovana (jen pro diagnostiku)
  float    vSpeed = 0.0f;      // vertikalni rychlost [m/s]
  float    vbat = 0.0f;        // -1 = nemereno

  // --- probihajici let ---
  uint16_t flightId = 0;       // 0 = zadny let se nezapisuje
  uint32_t flightT0 = 0;       // millis() odpovidajici t=0 letu
  uint32_t flightTOfs = 0;     // posun casove osy po obnove z resetu [ms]
  uint32_t flightT = 0;        // posledni zapsany cas letu [ms]
  float    maxAlt = 0.0f;
  uint32_t maxAltT = 0;
  bool     apogeeDone = false;
  float    maxSpeed = 0.0f;
  bool     recovered = false;  // let byl obnoven po resetu

  // --- posledni ukonceny let ---
  uint16_t lastFlightId = 0;
  float    lastApogee = 0.0f;

  // --- provoz ---
  uint32_t lastActivity = 0;   // millis() posledni akce (tlacitko/web/let)
  uint32_t landedAt = 0;
  uint32_t bootCount = 0;
  uint32_t epochBase = 0;      // unix cas - millis()/1000 (0 = cas neznamy)
  uint8_t  resetReason = 0;
};

extern AltimeterState st;

// Unixovy cas, nebo 0 pokud ho prohlizec jeste nenastavil
static inline uint32_t nowEpoch() {
  return st.epochBase ? (st.epochBase + millis() / 1000UL) : 0UL;
}

// Cokoliv, co ma odlozit automaticke uspani
static inline void noteActivity() { st.lastActivity = millis(); }

const char *modeName(Mode m);
