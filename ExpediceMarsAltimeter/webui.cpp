#include "webui.h"
#include "config.h"
#include "state.h"
#include "storage.h"
#include "sensor.h"
#include "power.h"
#include "web_page.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>

WebRequests webReq;

static WebServer server(80);
static DNSServer dns;
static bool      s_active = false;
static uint32_t  s_lastReq = 0;
static String    s_ssid;

static void touch() { s_lastReq = millis(); noteActivity(); }

// ---------------------------------------------------------------- utils

static String jsonEscape(const String &in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if ((uint8_t)c < 0x20) { out += ' '; }
    else out += c;
  }
  return out;
}

// Jmeno letu: vyhodime uvozovky, zpetna lomitka a ridici znaky
static String sanitizeName(const String &in) {
  String out;
  for (size_t i = 0; i < in.length() && out.length() < 40; i++) {
    char c = in[i];
    if (c == '"' || c == '\\' || (uint8_t)c < 0x20) continue;
    out += c;
  }
  out.trim();
  return out;
}

static uint16_t argId() {
  return (uint16_t)server.arg("id").toInt();
}

// ---------------------------------------------------------------- API

static void handleRoot() {
  touch();
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

static void handleStatus() {
  touch();
  char buf[960];
  String ssid = jsonEscape(s_ssid);
  snprintf(buf, sizeof buf,
    "{\"fw\":\"%s\",\"fwDate\":\"%s\",\"mode\":%u,\"modeName\":\"%s\",\"armed\":%d,\"sensorOk\":%d,"
    "\"padSet\":%d,\"padPa\":%.1f,\"padMsl\":%.1f,\"alt\":%.2f,\"altMsl\":%.2f,"
    "\"press\":%.1f,\"temp\":%.2f,\"vspeed\":%.2f,\"vbat\":%.2f,"
    "\"flightId\":%u,\"maxAlt\":%.2f,\"apogeeDone\":%d,"
    "\"lastId\":%u,\"lastApogee\":%.2f,"
    "\"launchM\":%.1f,\"apoDrop\":%.1f,"
    "\"fsUsed\":%u,\"fsTotal\":%u,\"uptime\":%lu,\"epoch\":%lu,\"boots\":%lu,"
    "\"ssid\":\"%s\",\"clients\":%u}",
    FW_VERSION, fwBuildDate(), (unsigned)st.mode, modeName(st.mode), st.armed ? 1 : 0,
    st.sensorOk ? 1 : 0, st.padPa > 1000.0f ? 1 : 0, st.padPa, st.padMsl,
    st.altAgl, st.altAgl + st.padMsl, st.pressPa, st.tempC, st.vSpeed, st.vbat,
    (unsigned)st.flightId, st.maxAlt, st.apogeeDone ? 1 : 0,
    (unsigned)st.lastFlightId, st.lastApogee,
    st.launchAltM, st.apogeeDropM,
    (unsigned)fsUsedBytes(), (unsigned)fsTotalBytes(),
    (unsigned long)(millis() / 1000UL), (unsigned long)nowEpoch(),
    (unsigned long)st.bootCount, ssid.c_str(), (unsigned)WiFi.softAPgetStationNum());
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", buf);
}

static void handleFlights() {
  touch();
  uint16_t ids[96];
  uint16_t n = flightList(ids, 96);

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", "");
  server.sendContent("[");

  bool first = true;
  for (uint16_t i = 0; i < n; i++) {
    FlightHeader h;
    if (!flightInfo(ids[i], h)) continue;
    String nm = jsonEscape(flightName(ids[i]));
    char buf[400];
    snprintf(buf, sizeof buf,
      "%s{\"id\":%u,\"name\":\"%s\",\"epoch\":%lu,\"apogee\":%.2f,\"apogeeT\":%lu,"
      "\"dur\":%lu,\"maxSpeed\":%.2f,\"samples\":%lu,\"padMsl\":%.1f,"
      "\"done\":%d,\"recovered\":%d,\"test\":%d}",
      first ? "" : ",", (unsigned)h.id, nm.c_str(), (unsigned long)h.epoch,
      h.apogeeM, (unsigned long)h.apogeeT, (unsigned long)h.durationMs,
      h.maxSpeed, (unsigned long)h.samples, h.padMsl,
      (h.flags & FF_FINALIZED) ? 1 : 0, (h.flags & FF_RECOVERED) ? 1 : 0,
      (h.flags & FF_TEST) ? 1 : 0);
    server.sendContent(buf);
    first = false;
  }
  server.sendContent("]");
  server.sendContent("");
}

// Letovy profil. Kvuli velikosti se posila jako plocha rada [t0,a0,t1,a1,...]
// a pri velkem poctu vzorku se redukuje krokem (parametr max).
static void handleFlight() {
  touch();
  uint16_t id = argId();
  FlightHeader h;
  if (id == 0 || !flightInfo(id, h)) { server.send(404, "application/json", "{\"err\":\"nenalezeno\"}"); return; }

  uint32_t maxPts = server.hasArg("max") ? (uint32_t)server.arg("max").toInt() : 800;
  if (maxPts < 50) maxPts = 50;
  if (maxPts > 20000) maxPts = 20000;
  uint32_t step = h.samples > maxPts ? (h.samples + maxPts - 1) / maxPts : 1;

  char path[24];
  snprintf(path, sizeof path, "/f%04u.dat", (unsigned)id);
  File f = LittleFS.open(path, "r");
  if (!f) { server.send(404, "application/json", "{\"err\":\"nenalezeno\"}"); return; }
  f.seek(sizeof(FlightHeader), SeekSet);

  String nm = jsonEscape(flightName(id));
  char head[420];
  snprintf(head, sizeof head,
    "{\"id\":%u,\"name\":\"%s\",\"epoch\":%lu,\"apogee\":%.2f,\"apogeeT\":%lu,"
    "\"dur\":%lu,\"maxSpeed\":%.2f,\"samples\":%lu,\"step\":%lu,\"padMsl\":%.1f,"
    "\"padPa\":%.1f,\"done\":%d,\"recovered\":%d,\"p\":[",
    (unsigned)id, nm.c_str(), (unsigned long)h.epoch, h.apogeeM,
    (unsigned long)h.apogeeT, (unsigned long)h.durationMs, h.maxSpeed,
    (unsigned long)h.samples, (unsigned long)step, h.padMsl, h.padPa,
    (h.flags & FF_FINALIZED) ? 1 : 0, (h.flags & FF_RECOVERED) ? 1 : 0);

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", "");
  server.sendContent(head);

  FlightSample buf[64];
  String out;
  out.reserve(1500);
  uint32_t idx = 0;
  bool first = true;
  int rd;
  while ((rd = f.read((uint8_t *)buf, sizeof buf)) > 0) {
    uint32_t got = rd / sizeof(FlightSample);
    for (uint32_t i = 0; i < got; i++, idx++) {
      if (idx % step) continue;
      char item[40];
      if (isnan(buf[i].alt))
        snprintf(item, sizeof item, "%s%lu,null", first ? "" : ",", (unsigned long)buf[i].t);
      else
        snprintf(item, sizeof item, "%s%lu,%.2f", first ? "" : ",", (unsigned long)buf[i].t, buf[i].alt);
      out += item;
      first = false;
      if (out.length() > 1200) { server.sendContent(out); out = ""; }
    }
    if (got < 64) break;
  }
  f.close();
  out += "]}";
  server.sendContent(out);
  server.sendContent("");
}

static void handleCsv() {
  touch();
  uint16_t id = argId();
  FlightHeader h;
  if (id == 0 || !flightInfo(id, h)) { server.send(404, "text/plain", "nenalezeno"); return; }

  bool decComma = server.hasArg("dec") && server.arg("dec") == ",";
  // s desetinnou carkou musi byt oddelovac strednik, jinak by se sloupce slily
  char sep = (decComma || server.arg("sep") == ";") ? ';' : ',';

  char path[24];
  snprintf(path, sizeof path, "/f%04u.dat", (unsigned)id);
  File f = LittleFS.open(path, "r");
  if (!f) { server.send(404, "text/plain", "nenalezeno"); return; }
  f.seek(sizeof(FlightHeader), SeekSet);

  String nm = flightName(id);
  char disp[80];
  snprintf(disp, sizeof disp, "attachment; filename=\"let%04u.csv\"", (unsigned)id);
  server.sendHeader("Content-Disposition", disp);
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv; charset=utf-8", "");

  String out;
  out.reserve(1500);
  char line[120];
  snprintf(line, sizeof line, "# let %u%s%s\n# apogeum %.1f m AGL\n", (unsigned)id,
           nm.length() ? " - " : "", nm.c_str(), h.apogeeM);
  out += line;
  snprintf(line, sizeof line, "cas_s%cvyska_m%cvyska_msl_m\n", sep, sep);
  out += line;

  FlightSample buf[64];
  int rd;
  while ((rd = f.read((uint8_t *)buf, sizeof buf)) > 0) {
    uint32_t got = rd / sizeof(FlightSample);
    for (uint32_t i = 0; i < got; i++) {
      if (isnan(buf[i].alt)) { out += "# --- preruseni zaznamu (reset) ---\n"; continue; }
      snprintf(line, sizeof line, "%.3f%c%.2f%c%.2f\n",
               buf[i].t / 1000.0f, sep, buf[i].alt, sep, buf[i].alt + h.padMsl);
      out += line;
    }
    if (out.length() > 1200) {
      if (decComma) out.replace('.', ',');
      server.sendContent(out);
      out = "";
    }
    if (got < 64) break;
  }
  f.close();
  if (decComma) out.replace('.', ',');
  server.sendContent(out);
  server.sendContent("");
}

static void handleName() {
  touch();
  uint16_t id = argId();
  String name = sanitizeName(server.hasArg("plain") ? server.arg("plain") : server.arg("name"));
  bool ok = id && flightSetName(id, name);
  server.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":1}" : "{\"ok\":0}");
}

static void handleDelete() {
  touch();
  uint16_t id = argId();
  bool ok = id && flightDelete(id);
  server.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":1}" : "{\"ok\":0}");
}

static void handleDeleteAll() {
  touch();
  if (server.arg("confirm") != "1") { server.send(400, "application/json", "{\"ok\":0}"); return; }
  uint16_t n = flightDeleteAll();
  char buf[40];
  snprintf(buf, sizeof buf, "{\"ok\":1,\"deleted\":%u}", (unsigned)n);
  server.send(200, "application/json", buf);
}

static void handleZero()   { touch(); webReq.zero = true;   server.send(200, "application/json", "{\"ok\":1}"); }
static void handleWifiOff() { touch(); webReq.wifiOff = true; server.send(200, "application/json", "{\"ok\":1}"); }

// __DATE__ ("Nov  8 2026") + __TIME__ -> "2026-11-08 14:05"
static const char *fwBuildDate() {
  static char out[20] = {0};
  if (out[0]) return out;
  static const char mon[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  const char *d = FW_BUILD_DATE;
  int m = 0;
  for (int i = 0; i < 12; i++) if (strncmp(d, mon + i * 3, 3) == 0) { m = i + 1; break; }
  int day = atoi(d + 4), year = atoi(d + 7);
  snprintf(out, sizeof out, "%04d-%02d-%02d %.5s", year, m, day, FW_BUILD_TIME);
  return out;
}

static void handleTest()   { touch(); webReq.test = true;   server.send(200, "application/json", "{\"ok\":1}"); }
static void handleSleep()  { touch(); webReq.sleep = true;  server.send(200, "application/json", "{\"ok\":1}"); }

static void handleArm() {
  touch();
  if (server.arg("on") == "0") webReq.disarm = true;
  else webReq.arm = true;
  server.send(200, "application/json", "{\"ok\":1}");
}

static void handleSettings() {
  touch();
  if (server.hasArg("padMsl"))   st.padMsl = server.arg("padMsl").toFloat();
  if (server.hasArg("launchM"))  st.launchAltM = constrain(server.arg("launchM").toFloat(), 3.0f, 300.0f);
  if (server.hasArg("apoDrop"))  st.apogeeDropM = constrain(server.arg("apoDrop").toFloat(), 1.0f, 100.0f);
  cfgSaveTunables();
  if (server.hasArg("ssid") || server.hasArg("pass"))
    cfgSaveAp(server.arg("ssid"), server.arg("pass"));
  server.send(200, "application/json", "{\"ok\":1}");
}

// Prohlizec nam posle svuj cas - vyskomer nema RTC, takze diky tomu
// dostanou nove lety skutecne datum a cas.
static void handleTime() {
  touch();
  uint32_t e = (uint32_t)strtoul(server.arg("epoch").c_str(), nullptr, 10);
  if (e > 1600000000UL) st.epochBase = e - millis() / 1000UL;
  server.send(200, "application/json", "{\"ok\":1}");
}

static void handleNotFound() {
  // Captive portal - cokoliv presmerujeme na uvodni stranku
  String url = "http://" + WiFi.softAPIP().toString() + "/";
  server.sendHeader("Location", url, true);
  server.send(302, "text/plain", "");
}

// ---------------------------------------------------------------- start/stop

void webBegin() {
  if (s_active) { touch(); return; }

  s_ssid = cfgApSsid();
  String pass = cfgApPass();

  rfSwitch(true);
  delay(50);
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(s_ssid.c_str(), pass.c_str());
  WiFi.setTxPower(WIFI_POWER_11dBm);   // na par metru bohate staci, setri proud
  delay(100);

  IPAddress ip = WiFi.softAPIP();
  dns.setTTL(60);
  dns.start(53, "*", ip);
  MDNS.begin(AP_HOSTNAME);
  MDNS.addService("http", "tcp", 80);

  // Handlery se registruji jen jednou - WebServer je pri stop() nezahazuje,
  // takze pri opakovanem zapinani AP by se hromadily.
  static bool routesDone = false;
  if (!routesDone) {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/api/flights", HTTP_GET, handleFlights);
    server.on("/api/flight", HTTP_GET, handleFlight);
    server.on("/api/csv", HTTP_GET, handleCsv);
    server.on("/api/name", HTTP_POST, handleName);
    server.on("/api/delete", HTTP_POST, handleDelete);
    server.on("/api/deleteall", HTTP_POST, handleDeleteAll);
    server.on("/api/zero", HTTP_POST, handleZero);
    server.on("/api/arm", HTTP_POST, handleArm);
    server.on("/api/sleep", HTTP_POST, handleSleep);
    server.on("/api/test", HTTP_POST, handleTest);
    server.on("/api/wifioff", HTTP_POST, handleWifiOff);
    server.on("/api/settings", HTTP_POST, handleSettings);
    server.on("/api/time", HTTP_POST, handleTime);
    server.onNotFound(handleNotFound);
    routesDone = true;
  }
  server.begin();

  s_active = true;
  touch();
  Serial.printf("[web] AP \"%s\", heslo \"%s\", http://%s/\n",
                s_ssid.c_str(), pass.c_str(), ip.toString().c_str());
}

void webStop() {
  if (!s_active) return;
  server.stop();
  dns.stop();
  MDNS.end();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  rfSwitch(false);
  s_active = false;
  Serial.println("[web] AP vypnuto");
}

bool webActive() { return s_active; }

void webTick() {
  if (!s_active) return;
  dns.processNextRequest();
  server.handleClient();
}

uint32_t webIdleMs() { return millis() - s_lastReq; }
uint8_t webClients() { return s_active ? WiFi.softAPgetStationNum() : 0; }
String webSsid() { return s_ssid; }
