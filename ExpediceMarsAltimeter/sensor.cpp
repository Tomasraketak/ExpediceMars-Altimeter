#include "sensor.h"
#include "state.h"
#include "storage.h"
#include <Wire.h>
#include <BMP580.h>
#include "ui.h"

static BMP580 bmp;
static uint8_t s_badReads = 0;
static bool s_filtInit = false;
static uint32_t s_lastSampleMs = 0;

// Konfigurace senzoru: ODR 20 Hz je zamerne 2x rychlejsi nez nase vzorkovani
// (10 Hz). Knihovna BMP580 cachuje posledni mereni po dobu 1/ODR, takze pri
// stejne frekvenci by nam obcas vratila stary vzorek. 20 Hz = vzdy cerstvy.
static void applyConfig() {
  bmp.setI2CSpeed(I2C_CLOCK_HZ);
  // Tlak x8 (sum ~2 cm), teplota x1 - teplotu na vysku skoro nepotrebujeme
  bmp.setOversampling(BMP580_OSR_x8, BMP580_OSR_x1);
  bmp.setIIRFilter(BMP580_IIR_3, BMP580_IIR_1);  // lehky HW filtr, minimalni zpozdeni
  bmp.setODR(BMP580_ODR_20Hz);
  bmp.setPowerMode(BMP580_MODE_NORMAL);
}

bool sensorBegin() {
  Wire.begin(PIN_SDA, PIN_SCL, I2C_CLOCK_HZ);
  st.sensorOk = bmp.begin();
  if (st.sensorOk) {
    applyConfig();
    delay(60);
    s_badReads = 0;
  }
  return st.sensorOk;
}

void sensorSleep() {
  if (st.sensorOk) bmp.setPowerMode(BMP580_MODE_STANDBY);
}

float pressureToAgl(float pressPa) {
  if (st.padPa <= 1000.0f || pressPa <= 1000.0f) return 0.0f;
  float h = 44330.0f * (1.0f - powf(pressPa / st.padPa, 0.1902949f));
#if USE_TEMP_COMPENSATION
  // Barometricka formule pocita se standardni atmosferou 15 C. Pri jine
  // teplote vzduchu je skutecna vyska umerna absolutni teplote.
  h *= (st.tempC + 273.15f) / 288.15f;
#endif
  return h;
}

bool sensorRead() {
  if (!st.sensorOk) {
    // Zkousime senzor obcas vzkrisit (napr. po zaskoceni I2C sbernice vibracemi)
    static uint32_t lastTry = 0;
    if (millis() - lastTry > 2000) { lastTry = millis(); sensorBegin(); }
    return false;
  }

  float p = bmp.readPressure();
  float t = bmp.readTemperature();

  // Sanity check - mimo tento rozsah to neni platne mereni (30000 Pa ~ 9000 m)
  if (!(p > 20000.0f && p < 120000.0f) || isnan(p)) {
    if (++s_badReads >= 5) {
      // Tvrdy restart sbernice i senzoru
      Wire.end();
      delay(5);
      st.sensorOk = false;
      s_badReads = 0;
    }
    return false;
  }
  s_badReads = 0;

  st.pressPa = p;
  st.tempC = t;
  st.altRawAgl = pressureToAgl(p);

  uint32_t now = millis();
  if (!s_filtInit) {
    st.altAgl = st.altRawAgl;
    st.vSpeed = 0.0f;
    s_filtInit = true;
  } else {
    float prev = st.altAgl;
    st.altAgl += ALT_EMA_ALPHA * (st.altRawAgl - st.altAgl);
    uint32_t dt = now - s_lastSampleMs;
    if (dt > 0 && dt < 2000) {
      float v = (st.altAgl - prev) * 1000.0f / (float)dt;
      st.vSpeed += 0.5f * (v - st.vSpeed);   // vyhlazeni rychlosti
    }
  }
  s_lastSampleMs = now;
  return true;
}

// Zprumeruje nekolik vzorku tlaku a ulozi je jako referenci startovni plochy.
// Blokujici, trva zhruba PAD_CAL_SAMPLES * 100 ms.
bool sensorCalibratePad() {
  if (!st.sensorOk && !sensorBegin()) return false;

  double sum = 0;
  int n = 0;
  for (int i = 0; i < PAD_CAL_SAMPLES; i++) {
    float p = bmp.readPressure();
    if (p > 20000.0f && p < 120000.0f) { sum += p; n++; }
    for (int k = 0; k < 10; k++) { uiTick(); delay(10); }   // LED mezitim blika dal
  }
  if (n < PAD_CAL_SAMPLES / 2) return false;

  st.padPa = (float)(sum / n);
  st.tempC = bmp.readTemperature();
  cfgSavePad(st.padPa);

  // Novy referencni bod => vynulovat filtr i letove maximum
  s_filtInit = false;
  st.altAgl = 0.0f;
  st.altRawAgl = 0.0f;
  st.vSpeed = 0.0f;
  return true;
}

float sensorReadVbat() {
#if PIN_VBAT >= 0
  analogSetPinAttenuation(PIN_VBAT, ADC_11db);
  uint32_t mv = 0;
  for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(PIN_VBAT);
  return (mv / 8.0f) * VBAT_DIVIDER / 1000.0f;
#else
  return -1.0f;
#endif
}
