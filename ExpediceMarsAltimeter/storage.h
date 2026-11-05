#pragma once
#include <Arduino.h>

// =====================================================================
//  Ulozeni dat
//  - Preferences (NVS) : konfigurace + prubezna zaloha letu
//  - LittleFS          : soubor na kazdy let  /fNNNN.dat (+ /fNNNN.nam)
//
//  Format souboru letu:  [FlightHeader 44 B][FlightSample 8 B] x N
//  Vzorek s alt == NAN je znacka preruseni zaznamu (reset behem letu).
// =====================================================================

#define FLIGHT_MAGIC 0x314D4145UL   // "EAM1"
#define FLIGHT_VER   1

#define FF_FINALIZED 0x01   // let byl radne uzavren (hlavicka je platna)
#define FF_RECOVERED 0x02   // zaznam pokracoval po resetu

struct __attribute__((packed)) FlightHeader {
  uint32_t magic;
  uint8_t  version;
  uint8_t  flags;
  uint16_t id;
  uint32_t epoch;       // unixovy cas okamziku t=0 (0 = neznamy)
  float    padPa;       // referencni tlak startovni plochy
  float    padMsl;      // nadm. vyska startovni plochy
  float    apogeeM;     // apogeum nad startem [m]
  uint32_t apogeeT;     // cas apogea od t=0 [ms]
  uint32_t durationMs;  // delka zaznamu [ms]
  float    maxSpeed;    // max. stoupava rychlost [m/s]
  uint32_t samples;     // pocet vzorku (dopocita se i ze souboru)
  uint32_t reserved;
};
static_assert(sizeof(FlightHeader) == 44, "FlightHeader musi mit 44 B");

struct __attribute__((packed)) FlightSample {
  uint32_t t;    // ms od zacatku letu
  float    alt;  // m nad startovni plochou (NAN = preruseni zaznamu)
};
static_assert(sizeof(FlightSample) == 8, "FlightSample musi mit 8 B");

bool storageBegin();

// ---------- konfigurace v NVS ----------
void   cfgLoad();
void   cfgSavePad(float pa);
void   cfgSaveArmed(bool armed);
void   cfgSaveTunables();                 // padMsl, launchAltM, apogeeDropM
void   cfgSaveAp(const String &ssid, const String &pass);
void   cfgSaveLast(uint16_t id, float apogee);   // + oznaci apogeum jako neprectene
bool   cfgLastUnread();                         // posledni apogeum jeste nikdo nevidel
void   cfgAckLast();                            // uzivatel apogeum videl
String cfgApSsid();
String cfgApPass();

// ---------- prubezna zaloha probihajiciho letu ----------
void     progressSave(bool force);        // apogeum + stav do NVS (rate-limited)
void     progressClear();
uint16_t progressFlightId();              // id letu prerusaneho resetem (0 = zadny)
void     progressRestore();               // natahne maxAlt/apogeeDone/... do st

// ---------- zapis letu ----------
uint16_t logStart(uint32_t epochAtT0);    // zalozi novy soubor, vrati id (0 = chyba)
// Co se da vycist z uz zapsane casti letu pri navazani po resetu
struct ResumeInfo {
  uint32_t lastT = 0;      // cas posledniho zapsaneho vzorku
  float    maxAlt = 0;     // nejvyssi zapsana vyska
  uint32_t maxAltT = 0;
  float    maxSpeed = 0;
  uint32_t samples = 0;
};
bool     logResume(uint16_t id, ResumeInfo *info);  // otevre soubor pro pokracovani
void     logSample(uint32_t t, float alt);
void     logMarker(uint32_t t);           // znacka preruseni (po resetu)
void     logFlushIfDue(bool force);
void     logFinalize();                   // dopise hlavicku a zavre soubor
bool     logIsOpen();
uint32_t logSampleCount();

// ---------- cteni pro web ----------
uint16_t flightList(uint16_t *ids, uint16_t maxN);       // serazeno od nejnovejsiho
bool     flightInfo(uint16_t id, FlightHeader &h);        // dopocita i nedokoncene
String   flightName(uint16_t id);
bool     flightSetName(uint16_t id, const String &name);
bool     flightDelete(uint16_t id);
uint16_t flightDeleteAll();
size_t   fsTotalBytes();
size_t   fsUsedBytes();
