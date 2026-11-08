#include "storage.h"
#include "state.h"
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>

static Preferences prefs;
static bool s_prefsOk = false;

// ---- otevreny soubor zaznamu ----
#define LOG_BUF_SAMPLES 32
static File          s_log;
static FlightSample  s_buf[LOG_BUF_SAMPLES];
static uint8_t       s_bufN = 0;
static uint32_t      s_lastFlush = 0;
static uint32_t      s_samples = 0;
static bool          s_logFull = false;   // doslo misto / chyba zapisu

// Projde vzorky a spocita statistiky. Pouziva se jak pro dopocet hlavicky
// nedokonceneho letu, tak pri navazani zaznamu po resetu.
struct ScanStats {
  float    maxAlt = -1e9f;
  uint32_t maxAltT = 0;
  float    maxSpeed = 0;
  uint32_t lastT = 0;
  uint32_t count = 0;
};

static void scanSamples(File &f, uint32_t cnt, ScanStats &out) {
  FlightSample buf[64];
  float prevAlt = 0;
  uint32_t prevT = 0;
  bool first = true;
  for (uint32_t done = 0; done < cnt;) {
    uint32_t chunk = min((uint32_t)64, cnt - done);
    int rd = f.read((uint8_t *)buf, chunk * sizeof(FlightSample));
    if (rd <= 0) break;
    uint32_t got = rd / sizeof(FlightSample);
    for (uint32_t i = 0; i < got; i++) {
      out.count++;
      if (isnan(buf[i].alt)) { first = true; continue; }   // znacka preruseni
      out.lastT = buf[i].t;
      if (buf[i].alt > out.maxAlt) { out.maxAlt = buf[i].alt; out.maxAltT = buf[i].t; }
      if (!first && buf[i].t > prevT) {
        float v = (buf[i].alt - prevAlt) * 1000.0f / (float)(buf[i].t - prevT);
        if (v > out.maxSpeed) out.maxSpeed = v;
      }
      prevAlt = buf[i].alt;
      prevT = buf[i].t;
      first = false;
    }
    done += got;
    if (got < chunk) break;
  }
  if (out.maxAlt < -1e8f) out.maxAlt = 0.0f;
}

static void flightPath(uint16_t id, char *buf, size_t n, const char *ext) {
  snprintf(buf, n, "/f%04u.%s", (unsigned)id, ext);
}

bool storageBegin() {
  // true = pri prvnim startu (nebo poskozenem FS) se souborovy system zalozi
  bool fsOk = LittleFS.begin(true);
  s_prefsOk = prefs.begin("emalt", false);
  return fsOk && s_prefsOk;
}

// =====================================================================
//  Konfigurace
// =====================================================================

void cfgLoad() {
  if (!s_prefsOk) return;
  st.padPa       = prefs.getFloat("padPa", 0.0f);
  st.padMsl      = prefs.getFloat("padMsl", DEF_PAD_MSL_M);
  st.launchAltM  = prefs.getFloat("launchM", DEF_LAUNCH_ALT_M);
  st.apogeeDropM = prefs.getFloat("apoDropM", DEF_APOGEE_DROP_M);
  st.armed       = prefs.getBool("armed", false);
  st.lastFlightId= prefs.getUShort("lastId", 0);
  st.lastApogee  = prefs.getFloat("lastApo", 0.0f);
  st.bootCount   = prefs.getULong("boots", 0) + 1;
  prefs.putULong("boots", st.bootCount);
}

void cfgSavePad(float pa) {
  if (!s_prefsOk) return;
  prefs.putFloat("padPa", pa);
}

void cfgSaveArmed(bool armed) {
  if (!s_prefsOk) return;
  prefs.putBool("armed", armed);
}

void cfgSaveTunables() {
  if (!s_prefsOk) return;
  prefs.putFloat("padMsl", st.padMsl);
  prefs.putFloat("launchM", st.launchAltM);
  prefs.putFloat("apoDropM", st.apogeeDropM);
}

void cfgSaveAp(const String &ssid, const String &pass) {
  if (!s_prefsOk) return;
  if (ssid.length() >= 1) prefs.putString("apSsid", ssid);
  if (pass.length() >= 8) prefs.putString("apPass", pass);
}

void cfgSaveLast(uint16_t id, float apogee) {
  if (!s_prefsOk) return;
  prefs.putUShort("lastId", id);
  prefs.putFloat("lastApo", apogee);
  prefs.putBool("lastNew", true);
}

bool cfgLastUnread() {
  return s_prefsOk && prefs.getBool("lastNew", false);
}

void cfgAckLast() {
  if (!s_prefsOk || !prefs.getBool("lastNew", false)) return;
  prefs.putBool("lastNew", false);
}

bool cfgWifiOn() {
  return s_prefsOk ? prefs.getBool("wifiOn", DEF_WIFI_ON) : (bool)DEF_WIFI_ON;
}

void cfgSaveWifi(bool on) {
  if (!s_prefsOk) return;
  if (prefs.getBool("wifiOn", DEF_WIFI_ON) != on) prefs.putBool("wifiOn", on);
}

String cfgApSsid() {
  String s = s_prefsOk ? prefs.getString("apSsid", "") : String();
  if (s.length()) return s;
  if (sizeof(AP_SSID) > 1) return String(AP_SSID);   // nazev z config.h
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[32];
  snprintf(buf, sizeof buf, "ExpediceMars-%02X%02X", mac[4], mac[5]);
  return String(buf);
}

String cfgApPass() {
  String s = s_prefsOk ? prefs.getString("apPass", "") : String();
  return s.length() >= 8 ? s : String(DEF_AP_PASS);
}

// =====================================================================
//  Prubezna zaloha letu do NVS
//  Diky ni prezije apogeum i reset tesne po nem - v souboru muze chybet
//  posledni nesplachnuta sekunda, tohle je vzdy nejvys 250 ms stare.
// =====================================================================

// Cely stav jde do NVS jako jeden blob - jeden zapis misto sedmi je
// podstatne rychlejsi (za letu na tom zalezi) i setrnejsi k flash.
struct __attribute__((packed)) Progress {
  uint16_t id;
  uint8_t  apogeeDone;
  uint8_t  test;       // testovaci let
  float    maxAlt;
  uint32_t maxAltT;
  uint32_t t;
  float    maxSpeed;
  float    padPa;
};
static_assert(sizeof(Progress) == 24, "Progress musi mit 24 B");

static uint32_t s_lastProgress = 0;
static float    s_lastSavedMax = -1e9f;
static bool     s_lastSavedApo = false;

void progressSave(bool force) {
  if (!s_prefsOk || st.flightId == 0) return;
  uint32_t now = millis();
  bool changed = (st.maxAlt - s_lastSavedMax) >= PROGRESS_SAVE_DELTA_M ||
                 st.apogeeDone != s_lastSavedApo;
  if (!force && (!changed || (now - s_lastProgress) < PROGRESS_SAVE_MS)) return;

  Progress p;
  p.id = st.flightId;
  p.apogeeDone = st.apogeeDone ? 1 : 0;
  p.test = st.testFlight ? 1 : 0;
  p.maxAlt = st.maxAlt;
  p.maxAltT = st.maxAltT;
  p.t = st.flightT;
  p.maxSpeed = st.maxSpeed;
  p.padPa = st.padPa;
  prefs.putBytes("cur", &p, sizeof p);

  s_lastProgress = now;
  s_lastSavedMax = st.maxAlt;
  s_lastSavedApo = st.apogeeDone;
}

void progressClear() {
  if (!s_prefsOk) return;
  Progress p = {};
  prefs.putBytes("cur", &p, sizeof p);
  s_lastSavedMax = -1e9f;
  s_lastSavedApo = false;
}

static bool progressRead(Progress &p) {
  if (!s_prefsOk) return false;
  return prefs.getBytes("cur", &p, sizeof p) == sizeof p;
}

uint16_t progressFlightId() {
  Progress p;
  return progressRead(p) ? p.id : 0;
}

void progressRestore() {
  Progress p;
  if (!progressRead(p)) return;
  st.flightId   = p.id;
  st.maxAlt     = p.maxAlt;
  st.maxAltT    = p.maxAltT;
  st.flightT    = p.t;
  st.apogeeDone = p.apogeeDone != 0;
  st.testFlight = p.test != 0;
  st.maxSpeed   = p.maxSpeed;
  if (p.padPa > 1000.0f) st.padPa = p.padPa;  // referenci bereme tu, se kterou let zacal
  s_lastSavedMax = st.maxAlt;
  s_lastSavedApo = st.apogeeDone;
}

// =====================================================================
//  Zapis letu
// =====================================================================

bool logIsOpen() { return (bool)s_log; }
uint32_t logSampleCount() { return s_samples; }

static void writeHeader(File &f, const FlightHeader &h) {
  f.seek(0, SeekSet);
  f.write((const uint8_t *)&h, sizeof h);
}

uint16_t logStart(uint32_t epochAtT0, bool test) {
  if (!s_prefsOk) return 0;
  if (s_log) s_log.close();

  uint16_t id = prefs.getUShort("nextId", 1);
  if (id == 0) id = 1;

  // Pokud je na FS uz malo mista, radeji do zapisu nelezeme - apogeum se
  // stejne ulozi do NVS a stary let nechceme prepsat.
  if (fsTotalBytes() > 0 && (fsTotalBytes() - fsUsedBytes()) < 32768) {
    s_logFull = true;
  } else {
    s_logFull = false;
  }

  char path[24];
  flightPath(id, path, sizeof path, "dat");
  s_log = LittleFS.open(path, "w+");
  if (!s_log) { s_logFull = true; return id; }

  FlightHeader h = {};
  h.magic = FLIGHT_MAGIC;
  h.version = FLIGHT_VER;
  h.flags = test ? FF_TEST : 0;
  h.id = id;
  h.epoch = epochAtT0;
  h.padPa = st.padPa;
  h.padMsl = st.padMsl;
  writeHeader(s_log, h);
  s_log.flush();

  prefs.putUShort("nextId", (uint16_t)(id + 1));
  s_bufN = 0;
  s_samples = 0;
  s_lastFlush = millis();
  return id;
}

bool logResume(uint16_t id, ResumeInfo *info) {
  if (s_log) s_log.close();
  char path[24];
  flightPath(id, path, sizeof path, "dat");
  if (!LittleFS.exists(path)) return false;

  s_log = LittleFS.open(path, "r+");
  if (!s_log) return false;

  FlightHeader h = {};
  if (s_log.read((uint8_t *)&h, sizeof h) != (int)sizeof h || h.magic != FLIGHT_MAGIC) {
    s_log.close();
    return false;
  }
  h.flags = (h.flags | FF_RECOVERED) & ~FF_FINALIZED;
  writeHeader(s_log, h);

  size_t sz = s_log.size();
  if (sz < sizeof(FlightHeader)) sz = sizeof(FlightHeader);
  // Zarovnat na cely vzorek (kdyby reset trefil rozepsany zapis)
  size_t data = ((sz - sizeof(FlightHeader)) / sizeof(FlightSample)) * sizeof(FlightSample);
  s_samples = data / sizeof(FlightSample);

  // Projdeme, co uz je v souboru. Zaloha v NVS muze byt starsi nez soubor
  // (po apogeu se maximum nemeni, takze se do NVS prestane zapisovat), takze
  // az kombinace obou dvou da presny vrchol i spojitou casovou osu.
  if (info) {
    ScanStats sc;
    s_log.seek(sizeof(FlightHeader), SeekSet);
    scanSamples(s_log, s_samples, sc);
    info->lastT = sc.lastT;
    info->maxAlt = sc.maxAlt;
    info->maxAltT = sc.maxAltT;
    info->maxSpeed = sc.maxSpeed;
    info->samples = s_samples;
  }
  s_log.seek(sizeof(FlightHeader) + data, SeekSet);

  s_bufN = 0;
  s_logFull = false;
  s_lastFlush = millis();
  return true;
}

void logSample(uint32_t t, float alt) {
  if (!s_log || s_logFull) return;
  s_buf[s_bufN].t = t;
  s_buf[s_bufN].alt = alt;
  s_bufN++;
  s_samples++;
  if (s_bufN >= LOG_BUF_SAMPLES) logFlushIfDue(true);
}

void logMarker(uint32_t t) { logSample(t, NAN); }

void logFlushIfDue(bool force) {
  if (!s_log || s_bufN == 0) return;
  if (!force && (millis() - s_lastFlush) < LOG_FLUSH_MS) return;

  size_t want = (size_t)s_bufN * sizeof(FlightSample);
  size_t got = s_log.write((const uint8_t *)s_buf, want);
  s_log.flush();          // fsync -> data jsou fyzicky ve flash
  if (got != want) s_logFull = true;
  s_bufN = 0;
  s_lastFlush = millis();
}

void logFinalize() {
  if (!s_log) return;
  logFlushIfDue(true);

  FlightHeader h = {};
  s_log.seek(0, SeekSet);
  if (s_log.read((uint8_t *)&h, sizeof h) == (int)sizeof h && h.magic == FLIGHT_MAGIC) {
    h.flags |= FF_FINALIZED;
    h.apogeeM = st.maxAlt;
    h.apogeeT = st.maxAltT;
    h.durationMs = st.flightT;
    h.maxSpeed = st.maxSpeed;
    h.samples = s_samples;
    if (h.epoch == 0) {
      uint32_t e = nowEpoch();
      if (e) h.epoch = e - st.flightT / 1000UL;   // dopocitat zpetne
    }
    writeHeader(s_log, h);
  }
  s_log.flush();
  s_log.close();
  s_bufN = 0;
}

// =====================================================================
//  Cteni pro web
// =====================================================================

size_t fsTotalBytes() { return LittleFS.totalBytes(); }
size_t fsUsedBytes()  { return LittleFS.usedBytes(); }

uint16_t flightList(uint16_t *ids, uint16_t maxN) {
  uint16_t n = 0;
  File root = LittleFS.open("/");
  if (!root) return 0;
  File f;
  while ((f = root.openNextFile())) {
    // Pozor: sscanf("f%4u.dat") vraci 1 i pro "f0001.nam" - priponu overujeme rucne
    const char *nm = f.name();
    if (nm && nm[0] == '/') nm++;
    if (nm && strlen(nm) == 9 && nm[0] == 'f' && strcmp(nm + 5, ".dat") == 0 &&
        isdigit((int)nm[1]) && isdigit((int)nm[2]) && isdigit((int)nm[3]) && isdigit((int)nm[4])) {
      unsigned id = (unsigned)strtoul(nm + 1, nullptr, 10);
      if (id > 0 && n < maxN) ids[n++] = (uint16_t)id;
    }
    f.close();
  }
  root.close();
  // serazeni sestupne (nejnovejsi nahore) - letu jsou desitky, bubble staci
  for (uint16_t i = 0; i + 1 < n; i++)
    for (uint16_t j = 0; j + 1 < n - i; j++)
      if (ids[j] < ids[j + 1]) { uint16_t t = ids[j]; ids[j] = ids[j + 1]; ids[j + 1] = t; }
  return n;
}

bool flightInfo(uint16_t id, FlightHeader &h) {
  char path[24];
  flightPath(id, path, sizeof path, "dat");
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  if (f.read((uint8_t *)&h, sizeof h) != (int)sizeof h || h.magic != FLIGHT_MAGIC) {
    f.close();
    return false;
  }
  size_t sz = f.size();
  uint32_t cnt = sz > sizeof(FlightHeader)
                   ? (sz - sizeof(FlightHeader)) / sizeof(FlightSample) : 0;

  if (h.flags & FF_FINALIZED) {
    h.samples = cnt;
    f.close();
    return true;
  }

  // Nedokonceny let (reset behem letu / vybita baterka) - statistiku
  // dopocitame primo ze zapsanych vzorku.
  ScanStats sc;
  f.seek(sizeof(FlightHeader), SeekSet);
  scanSamples(f, cnt, sc);
  f.close();

  h.apogeeM = sc.maxAlt;
  h.apogeeT = sc.maxAltT;
  h.durationMs = sc.lastT;
  h.maxSpeed = sc.maxSpeed;
  h.samples = cnt;
  return true;
}

String flightName(uint16_t id) {
  char path[24];
  flightPath(id, path, sizeof path, "nam");
  File f = LittleFS.open(path, "r");
  if (!f) return String();
  String s = f.readStringUntil('\n');
  f.close();
  s.trim();
  return s;
}

bool flightSetName(uint16_t id, const String &name) {
  char path[24];
  flightPath(id, path, sizeof path, "nam");
  if (name.length() == 0) { LittleFS.remove(path); return true; }
  File f = LittleFS.open(path, "w");
  if (!f) return false;
  f.print(name.substring(0, 40));
  f.print('\n');
  f.close();
  return true;
}

bool flightDelete(uint16_t id) {
  if (id == st.flightId && s_log) return false;   // probihajici let nemazeme
  char path[24];
  flightPath(id, path, sizeof path, "dat");
  bool ok = LittleFS.remove(path);
  flightPath(id, path, sizeof path, "nam");
  LittleFS.remove(path);
  return ok;
}

uint16_t flightDeleteAll() {
  uint16_t ids[128];
  uint16_t n = flightList(ids, 128);
  uint16_t done = 0;
  for (uint16_t i = 0; i < n; i++)
    if (flightDelete(ids[i])) done++;
  return done;
}
