#pragma once
#include <Arduino.h>

// Pozadavky z webu, ktere vyrizuje hlavni smycka (aby HTTP odpoved
// neblokovala na nekolik sekund kalibrace).
struct WebRequests {
  bool zero = false;      // nastavit startovni vysku + ozbrojit
  bool arm = false;
  bool disarm = false;
  bool sleep = false;
  bool test = false;      // kalibrace + testovaci let hned
};
extern WebRequests webReq;

void     webBegin();      // zapne AP a HTTP server
void     webStop();
bool     webActive();
void     webTick();       // obsluha klientu (vola se ze smycky)
uint32_t webIdleMs();     // jak dlouho uz nikdo nic nechtel
uint8_t  webClients();
String   webSsid();
