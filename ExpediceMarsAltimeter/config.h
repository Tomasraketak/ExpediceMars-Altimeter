#pragma once
// =====================================================================
//  ExpediceMars Altimeter - konfigurace
//  Deska:  Seeed Studio XIAO ESP32-C6
//  Senzor: BMP580 (I2C, knihovna Tomasraketak/BMP580-library)
//  Vstup:  1 tlacitko proti GND
//  Zdroj:  1S LiPo primo na BAT pinech XIAO
// =====================================================================

// ---------------------- PINY ----------------------

// Tlacitko: D1 = GPIO1 (podle schematu: SW1 spina D1 na GND, R1 10k pull-up na 3V3).
// GPIO0..GPIO7 jsou na ESP32-C6 v LP (RTC) domene, takze dokazou probudit
// cip i z deep sleepu - pin tlacitka proto musi zustat v tomto rozsahu.
#ifndef PIN_BUTTON
#define PIN_BUTTON 1
#endif
#define BUTTON_ACTIVE_LOW 1   // tlacitko spina na GND, externi 10k + vnitrni pull-up

// Uzivatelska LED na XIAO ESP32-C6 je GPIO15 a sviti pri LOW.
#ifndef PIN_LED
#ifdef LED_BUILTIN
#define PIN_LED LED_BUILTIN
#else
#define PIN_LED 15
#endif
#endif
#define LED_ACTIVE_LOW 1

// Volitelny bzucak (aktivni piezo na 3V3). -1 = neni osazen.
// Bzuci soubezne s LED, takze raketu najdes i v trave.
#define PIN_BUZZER -1

// Volitelne mereni napeti baterie pres odporovy delic na ADC pin.
// XIAO ESP32-C6 NEMA interni mereni baterie (ani kdyz je clanek na BAT
// padech) - Seeed wiki doporucuje externi delic 1:2 na A0. Postup:
// 2x 200-220k: BAT+ -> R -> A0 (D0/GPIO0) -> R -> GND, a pak PIN_VBAT 0.
// Bez osazeneho delice nech -1 (plovouci pin by ukazoval nesmysly).
// Pozor: GPIO3 a GPIO14 ridi na XIAO C6 VF prepinac anteny, ty nepouzivej.
#define PIN_VBAT -1
#define VBAT_DIVIDER 2.0f   // (R1+R2)/R2 pro pouzity delic

// I2C - na XIAO ESP32-C6 je SDA = D4 (GPIO22), SCL = D5 (GPIO23).
#define PIN_SDA SDA
#define PIN_SCL SCL
#define I2C_CLOCK_HZ 400000UL

// ---------------------- LETOVE PRAHY ----------------------
// (tyto tri hodnoty jdou prepsat i z webu, ukladaji se do NVS)

#define DEF_LAUNCH_ALT_M   10.0f   // vyska nad startem, od ktere to je "let"
#define DEF_APOGEE_DROP_M  10.0f   // pokles pod dosud nejvyssi vysku = apogeum
#define DEF_PAD_MSL_M       0.0f   // nadmorska vyska startovisti (jen pro zobrazeni)

// ---------------------- CASOVANI ----------------------

#define SAMPLE_PERIOD_MS      100   // 10 Hz behem ARMED i letu (zadani)
#define IDLE_PERIOD_MS       1000   // 1 Hz kdyz je vyskomer odzbrojeny
#define PRELOG_SAMPLES          5   // kruhovy buffer pred startem (0,5 s historie)

// Konec zaznamu (jen po apogeu): vyska je pod LAND_ALT_M a zaroven se
// vyska meni (klesa i stoupa) pomaleji nez LAND_SPEED_MS, a to nepretrzite
// aspon LAND_STABLE_MS.
#define LAND_ALT_M          25.0f
#define LAND_SPEED_MS        1.0f
#define LAND_STABLE_MS       1000

// Testovaci let (tlacitko ve webu): zacne se zaznamenavat hned bez prahu
// startu, prvnich TEST_LAND_BLOCK_MS je podminka konce zablokovana a pak
// se nevyzaduje apogeum (na stole by nikdy nenastalo) - konec tedy prijde
// po 10 s, jakmile je vyska < LAND_ALT_M a zmena < LAND_SPEED_MS po 1 s.
#define TEST_LAND_BLOCK_MS  10000
#define MIN_FLIGHT_MS        3000   // driv let neukoncime
#define MAX_FLIGHT_MS      600000   // 10 min = tvrdy strop, pak zapis uzavrem

// Jak casto se za letu zalohuje apogeum + stav do NVS (odolnost proti resetu)
// Ulozi se kazdy narust maxima o >= 0.1 m, nejvys jednou za 250 ms, takze
// skutecny vrchol je v NVS nejpozdeji ~350 ms po tom, co nastal.
#define PROGRESS_SAVE_MS      250
#define PROGRESS_SAVE_DELTA_M 0.1f
// Jak casto se buffer vzorku splachne z RAM do flash
#define LOG_FLUSH_MS         1000

// Kalibrace startovni vysky: kolik vzorku se zprumeruje
#define PAD_CAL_SAMPLES        20

// ---------------------- SPRAVA NAPAJENI ----------------------

#define CPU_MHZ_RUN            80   // bezny beh i WiFi (160 MHz neni k nicemu potreba)
#define IDLE_TO_SLEEP_MS   180000UL // 3 min necinnosti v IDLE -> deep sleep
#define LANDED_AWAKE_MS    300000UL // po pristani 5 min blika apogeum, pak IDLE
#define AP_TIMEOUT_MS           0UL // 0 = AP se sam nevypina (vypne ho jen uzivatel)
#define DEF_WIFI_ON             1   // WiFi AP je po zapnuti defaultne zapnute
#define ARMED_TIMEOUT_MS        0UL // 0 = v ARMED nikdy sam neusne
// 1 = kdyz je pripojeny USB k pocitaci, nepouziva se light ani automaticky
// deep sleep (jinak se USB port odpojuje a nejde nahrat novy kod)
#define USB_NO_SLEEP            1

// ---------------------- TLACITKO ----------------------

#define BTN_DEBOUNCE_MS        25
#define BTN_SHORT_MAX_MS     1000   // kratky stisk
#define BTN_ARM_MS           3000   // 3 s = ulozit startovni vysku + ARM
#define BTN_OFF_MS           8000   // 8 s = vypnout (deep sleep)
#define BTN_DOUBLE_GAP_MS     500   // okno pro druhy klik (dvojklik = WiFi)
// Blikani pri drzeni k vypnuti (mezi 3 s a 8 s): perioda se zkracuje
// z BTN_OFF_BLINK_SLOW_MS na BTN_OFF_BLINK_FAST_MS, v 8 s LED trvale sviti.
#define BTN_OFF_BLINK_SLOW_MS 500
#define BTN_OFF_BLINK_FAST_MS  60

// ---------------------- FILTRACE ----------------------
// Lehky EMA filtr na vysku. 1.0 = zadna filtrace, nizsi = hladsi ale pomalejsi.
#define ALT_EMA_ALPHA        0.40f
// Prepocet tlaku na vysku podle aktualni teploty (jinak standardni atmosfera 15 C)
#define USE_TEMP_COMPENSATION 0

// ---------------------- WIFI AP ----------------------
#define DEF_AP_PASS "raketa1234"   // min. 8 znaku, da se zmenit ve webu
#define AP_HOSTNAME "vyskomer"     // http://vyskomer.local

#define FW_VERSION "1.1.0"
// Datum verze = kdy byl firmware prelozen (doplni prekladac)
#define FW_BUILD_DATE __DATE__
#define FW_BUILD_TIME __TIME__
