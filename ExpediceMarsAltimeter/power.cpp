#include "power.h"
#include "config.h"
#include "state.h"
#include "ui.h"
#include "sensor.h"
#include "storage.h"
#include <Wire.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <driver/usb_serial_jtag.h>

// =====================================================================
//  Sprava napajeni
//
//  Kde se setri nejvic:
//   * ARMED i IDLE travi cip mezi vzorky v light sleepu (~100 uA misto
//     ~25 mA aktivniho behu). Budi ho timer nebo stisk tlacitka.
//   * WiFi AP se zapina jen na vyzadani a sam se po case vypne.
//   * VF prepinac anteny je napajeny jen kdyz jede WiFi.
//   * Deep sleep (~15 uA) slouzi jako "vypnuto" - probudi jej tlacitko.
//   * BMP580 bezi na 20 Hz s OSR x8/x1, mimo provoz jde do STANDBY.
// =====================================================================

bool wokeFromDeepSleep() {
  esp_sleep_wakeup_cause_t c = esp_sleep_get_wakeup_cause();
  return c != ESP_SLEEP_WAKEUP_UNDEFINED;
}

void rfSwitch(bool on) {
  pinMode(WIFI_ENABLE, OUTPUT);
  digitalWrite(WIFI_ENABLE, on ? LOW : HIGH);   // LOW = prepinac zapnuty
  if (on) {
    pinMode(WIFI_ANT_CONFIG, OUTPUT);
    digitalWrite(WIFI_ANT_CONFIG, LOW);         // LOW = vestavena antena
  }
}

void powerBegin() {
  setCpuFrequencyMhz(CPU_MHZ_RUN);
  rfSwitch(false);            // dokud nejede AP, VF prepinac nepotrebujeme
  btStop();                   // Bluetooth nikdy nepouzijeme
}

// ESP32-C6 ma USB primo v cipu (USB-Serial/JTAG). Ve spanku se USB odpoji,
// port ve Windows "zamrzne" a nahrani noveho kodu skonci chybou typu
// "pristup odepren". Proto kdyz je pripojeny pocitac, vubec nespime.
bool usbConnected() {
#if USB_NO_SLEEP
  return usb_serial_jtag_is_connected();
#else
  return false;
#endif
}

void lightSleepFor(uint32_t ms) {
  if (usbConnected()) { delay(ms < 10 ? ms : 10); return; }

  // Drzene tlacitko musime hlidat casteji, nez by nas probudil timer
  if (buttonIsDown()) { delay(ms < 10 ? ms : 10); return; }

  // Nespat pres okamzik, kdy ma LED prepnout stav - vzor by se rozjel
  uint32_t led = uiNextEventIn();
  if (led < ms) ms = led;
  if (ms < 12) { delay(ms); return; }

  Serial.flush();
  esp_sleep_enable_timer_wakeup((uint64_t)ms * 1000ULL);
  gpio_wakeup_enable((gpio_num_t)PIN_BUTTON,
                     BUTTON_ACTIVE_LOW ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  esp_light_sleep_start();
  gpio_wakeup_disable((gpio_num_t)PIN_BUTTON);
}

void enterDeepSleep() {
  uiSet(UI_CONFIRM);
  uint32_t t = millis();
  while (millis() - t < 400) { uiTick(); delay(5); }
  uiSet(UI_OFF);
  uiAllOff();

  if (logIsOpen()) logFinalize();
  sensorSleep();
  Wire.end();
  rfSwitch(false);

  // Kdyby uzivatel jeste drzel tlacitko, hned bychom se probudili
  t = millis();
  while (buttonIsDown() && (millis() - t) < 8000) delay(20);
  delay(80);

  // Pullup musi zustat aktivni i v deep sleepu, jinak pin plave.
  // (Presto doporucuji osadit externich 100 kOhm k 3V3.)
  rtc_gpio_pullup_en((gpio_num_t)PIN_BUTTON);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_BUTTON);

  esp_deep_sleep_enable_gpio_wakeup(BIT64(PIN_BUTTON),
                                    BUTTON_ACTIVE_LOW ? ESP_GPIO_WAKEUP_GPIO_LOW
                                                      : ESP_GPIO_WAKEUP_GPIO_HIGH);
  esp_deep_sleep_start();
}
