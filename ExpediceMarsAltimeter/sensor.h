#pragma once
#include <Arduino.h>

bool  sensorBegin();                  // nastartuje I2C + BMP580
bool  sensorRead();                   // nacte tlak/teplotu, prepocte vysku, filtruje
float pressureToAgl(float pressPa);   // tlak -> vyska nad startovni plochou [m]
void  sensorSleep();                  // uspi senzor pred deep sleepem
bool  sensorCalibratePad();           // zprumeruje tlak a ulozi jako startovni vysku
float sensorReadVbat();               // napeti baterie, nebo -1 kdyz neni delic
