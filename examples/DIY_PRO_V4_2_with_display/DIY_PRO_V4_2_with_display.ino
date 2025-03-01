/*
Important: This code is only for the DIY PRO PCB Version 3.7 that has a push button mounted.

This is the code for the AirGradient DIY PRO Air Quality Sensor with an ESP8266 Microcontroller with the SGP40 TVOC module from AirGradient.

It is a high quality sensor showing PM2.5, CO2, Temperature and Humidity on a small display and can send data over Wifi.

Build Instructions: https://www.airgradient.com/open-airgradient/instructions/diy-pro-v37/

Kits (including a pre-soldered version) are available: https://www.airgradient.com/open-airgradient/kits/

The codes needs the following libraries installed:
“WifiManager by tzapu, tablatronix” tested with version 2.0.11-beta
“U8g2” by oliver tested with version 2.32.15
"Sensirion I2C SGP41" by Sensation Version 0.1.0
"Sensirion Gas Index Algorithm" by Sensation Version 3.2.1

Configuration:
Please set in the code below the configuration parameters.

If you have any questions please visit our forum at https://forum.airgradient.com/

If you are a school or university contact us for a free trial on the AirGradient platform.
https://www.airgradient.com/

CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License

*/


#include <AirGradient.h>
#include <WiFiManager.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>

#include <EEPROM.h>

//#include "SGP30.h"
#include <SensirionI2CSgp41.h>
#include <NOxGasIndexAlgorithm.h>
#include <VOCGasIndexAlgorithm.h>


#include <U8g2lib.h>

AirGradient ag = AirGradient();
SensirionI2CSgp41 sgp41;
VOCGasIndexAlgorithm voc_algorithm;
NOxGasIndexAlgorithm nox_algorithm;
// time in seconds needed for NOx conditioning
uint16_t conditioning_s = 10;

// for peristent saving and loading
int addr = 4;
byte value;

// Display bottom right
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// Replace above if you have display on top left
//U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R2, /* reset=*/ U8X8_PIN_NONE);


// CONFIGURATION START

//set to the endpoint you would like to use
String APIROOT = "http://hw.airgradient.com/";

// set to true to switch from Celcius to Fahrenheit
boolean inF = false;

// PM2.5 in US AQI (default ug/m3)
boolean inUSAQI = false;

// Display Position
boolean displayTop = true;

// set to true if you want to connect to wifi. You have 60 seconds to connect. Then it will go into an offline mode.
boolean connectWIFI=true;

// CONFIGURATION END


unsigned long currentMillis = 0;

const int oledInterval = 1000; // 5000
unsigned long previousOled = 0;

const int sendToServerInterval = 10000;
unsigned long previoussendToServer = 0;

const int tvocInterval = 1000; // 5000
unsigned long previousTVOC = 0;
int TVOC = 0;
int NOX = 0;

const int co2Interval = 1000; // 5000
unsigned long previousCo2 = 0;
int Co2 = 0;

const int pm25Interval = 1000; // 5000
unsigned long previousPm25 = 0;
int pm25 = 0;
int pm01 = 0;
int pm10 = 0;
int pm003_count = 0;

const int tempHumInterval = 5000;
unsigned long previousTempHum = 0;
float temp = 0;
int hum = 0;

int buttonConfig=4;
int lastState = LOW;
int currentState;
unsigned long pressedTime  = 0;
unsigned long releasedTime = 0;

float AQIindex = 0;

void setup() {
  Serial.begin(115200);
  u8g2.begin();
  //u8g2.setDisplayRotation(U8G2_R0);

  EEPROM.begin(512);
  delay(500);

  buttonConfig = String(EEPROM.read(addr)).toInt();
  setConfig();

   updateOLED2("Press Button", "Now for", "Config Menu");
    delay(2000);

  currentState = digitalRead(D7);
  if (currentState == HIGH)
  {
    updateOLED2("Entering", "Config Menu", "");
    delay(3000);
    lastState = LOW;
    inConf();
  }

  if (connectWIFI)
  {
     connectToWifi();
  }

  updateOLED2("Warming Up", "Serial Number:", String(ESP.getChipId(), HEX));
  sgp41.begin(Wire);
  ag.CO2_Init();
  ag.PMS_Init();
  ag.TMP_RH_Init(0x44);
}

void loop() {
  currentMillis = millis();
  updateTVOC();
  updateCo2();
  updatePm25();
  updateTempHum();
  updateAQIindex();
  drawScreen();
  sendToServer();
}

void inConf(){
  setConfig();
  currentState = digitalRead(D7);

  if(lastState == LOW && currentState == HIGH) {
    pressedTime = millis();
  }

  else if(lastState == HIGH && currentState == LOW) {
    releasedTime = millis();
    long pressDuration = releasedTime - pressedTime;
    if( pressDuration < 1000 ) {
      buttonConfig=buttonConfig+1;
      if (buttonConfig>7) buttonConfig=0;
    }
  }

  if (lastState == HIGH && currentState == HIGH){
     long passedDuration = millis() - pressedTime;
      if( passedDuration > 4000 ) {
        // to do
//        if (buttonConfig==4) {
//          updateOLED2("Saved", "Release", "Button Now");
//          delay(1000);
//          updateOLED2("Starting", "CO2", "Calibration");
//          delay(1000);
//          Co2Calibration();
//       } else {
          updateOLED2("Saved", "Release", "Button Now");
          delay(1000);
          updateOLED2("Rebooting", "in", "5 seconds");
          delay(5000);
          EEPROM.write(addr, char(buttonConfig));
          EEPROM.commit();
          delay(1000);
          ESP.restart();
 //       }
    }

  }
  lastState = currentState;
//  delay(100);
//  inConf();
}


void setConfig() {
  if (buttonConfig == 0) {
    updateOLED2("Temp. in C", "PM in ug/m3", "Display Top");
      u8g2.setDisplayRotation(U8G2_R2);
      inF = false;
      inUSAQI = false;
  } else if (buttonConfig == 1) {
    updateOLED2("Temp. in C", "PM in US AQI", "Display Top");
      u8g2.setDisplayRotation(U8G2_R2);
      inF = false;
      inUSAQI = true;
  } else if (buttonConfig == 2) {
    updateOLED2("Temp. in F", "PM in ug/m3", "Display Top");
      u8g2.setDisplayRotation(U8G2_R2);
      inF = true;
      inUSAQI = false;
  } else if (buttonConfig == 3) {
    updateOLED2("Temp. in F", "PM in US AQI", "Display Top");
      u8g2.setDisplayRotation(U8G2_R2);
       inF = true;
      inUSAQI = true;
  } else if (buttonConfig == 4) {
    updateOLED2("Temp. in C", "PM in ug/m3", "Display Bottom");
      u8g2.setDisplayRotation(U8G2_R0);
      inF = false;
      inUSAQI = false;
  }
    if (buttonConfig == 5) {
    updateOLED2("Temp. in C", "PM in US AQI", "Display Bottom");
      u8g2.setDisplayRotation(U8G2_R0);
      inF = false;
      inUSAQI = true;
  } else if (buttonConfig == 6) {
    updateOLED2("Temp. in F", "PM in ug/m3", "Display Bottom");
    u8g2.setDisplayRotation(U8G2_R0);
      inF = true;
      inUSAQI = false;
  } else  if (buttonConfig == 7) {
    updateOLED2("Temp. in F", "PM in US AQI", "Display Bottom");
      u8g2.setDisplayRotation(U8G2_R0);
       inF = true;
      inUSAQI = true;
  }



  // to do
  // if (buttonConfig == 8) {
  //  updateOLED2("CO2", "Manual", "Calibration");
  // }
}

void updateTVOC()
{
 uint16_t error;
    char errorMessage[256];
    uint16_t defaultRh = 0x8000;
    uint16_t defaultT = 0x6666;
    uint16_t srawVoc = 0;
    uint16_t srawNox = 0;
    uint16_t defaultCompenstaionRh = 0x8000;  // in ticks as defined by SGP41
    uint16_t defaultCompenstaionT = 0x6666;   // in ticks as defined by SGP41
    uint16_t compensationRh = 0;              // in ticks as defined by SGP41
    uint16_t compensationT = 0;               // in ticks as defined by SGP41

    delay(1000);

    compensationT = static_cast<uint16_t>((temp + 45) * 65535 / 175);
    compensationRh = static_cast<uint16_t>(hum * 65535 / 100);

    if (conditioning_s > 0) {
        error = sgp41.executeConditioning(compensationRh, compensationT, srawVoc);
        conditioning_s--;
    } else {
        error = sgp41.measureRawSignals(compensationRh, compensationT, srawVoc, srawNox);
    }

    if (currentMillis - previousTVOC >= tvocInterval) {
      previousTVOC += tvocInterval;
      TVOC = voc_algorithm.process(srawVoc);
      NOX = nox_algorithm.process(srawNox);
//      Serial.println(String(TVOC));
//      Serial.println(String(NOX)); 
    }
}

void updateCo2()
{
    if (currentMillis - previousCo2 >= co2Interval) {
      previousCo2 += co2Interval;
      Co2 = ag.getCO2_Raw();
//      Serial.println(String(Co2));
    }
}

void updatePm25()
{
    if (currentMillis - previousPm25 >= pm25Interval) {
      previousPm25 += pm25Interval;
      pm25 = ag.getPM2_Raw();
      pm01 = ag.getPM1_Raw();
      pm10 = ag.getPM10_Raw();
      pm003_count = ag.getPM0_3Count();
//      Serial.println(String(pm25));
    }
}

void updateTempHum()
{
    if (currentMillis - previousTempHum >= tempHumInterval) {
      previousTempHum += tempHumInterval;
      TMP_RH result = ag.periodicFetchData();
      temp = result.t;
      hum = result.rh;
//      Serial.println(String(temp));
    }
}

void updateAQIindex()
{
  int pm25_index = 0;
  int NOX_index = 0;
  int TVOC_index = 0;
  int Co2_index = 0;
  
  if (pm25 <= 10) {
    pm25_index = 0;
  } else if (pm25 <= 15) {
    pm25_index = 1;
  } else if (pm25 <= 25) {
    pm25_index = 2;
  } else if (pm25 <= 35) {
    pm25_index = 3;
  } else if (pm25 <= 55) {
    pm25_index = 4;
  } else if (pm25 > 55) {
    pm25_index = 5;
  }
  
  if (NOX <= 50) {
    NOX_index = 0;
  } else if (NOX <= 100) {
    NOX_index = 1;
  } else if (NOX <= 150) {
    NOX_index = 2;
  } else if (NOX <= 200) {
    NOX_index = 3;
  } else if (NOX <= 300) {
    NOX_index = 4;
  } else if (NOX > 300) {
    NOX_index = 5;
  }
  
  if (TVOC <= 50) {
    TVOC_index = 0;
  } else if (TVOC <= 100) {
    TVOC_index = 1;
  } else if (TVOC <= 150) {
    TVOC_index = 2;
  } else if (TVOC <= 200) {
    TVOC_index = 3;
  } else if (TVOC <= 500) {
    TVOC_index = 4;
  } else if (TVOC > 500) {
    TVOC_index = 5;
  }
  
  if (Co2 <= 400) {
    Co2_index = 0;
  } else if (Co2 <= 700) {
    Co2_index = 1;
  } else if (Co2 <= 1000) {
    Co2_index = 2;
  } else if (Co2 <= 1500) {
    Co2_index = 3;
  } else if (Co2 <= 2000) {
    Co2_index = 4;
  } else if (Co2 > 2000) {
    Co2_index = 5;
  }

//  Serial.println("");
//  Serial.println(String(pm25));
//  Serial.println(String(pm25_index));
//  Serial.println("");
//  Serial.println(String(NOX));
//  Serial.println(String(NOX_index));
//  Serial.println("");
//  Serial.println(String(TVOC));
//  Serial.println(String(TVOC_index));
//  Serial.println("");
//  Serial.println(String(Co2));
//  Serial.println(String(Co2_index));

  AQIindex = ((pm25_index + NOX_index + TVOC_index + Co2_index) / 4.0);

//  Serial.println("");
//  Serial.println(String(AQIindex));
}

void updateOLED() {
   if (currentMillis - previousOled >= oledInterval) {
     previousOled += oledInterval;

    String ln3;
    String ln1;

    if (inUSAQI) {
      ln1 = "AQI:" + String(PM_TO_AQI_US(pm25)) +  " CO2:" + String(Co2);
    } else {
      ln1 = "PM:" + String(pm25) +  " CO2:" + String(Co2);
    }

     String ln2 = "TVOC:" + String(TVOC) + " NOX:" + String(NOX);

      if (inF) {
        ln3 = "F:" + String((temp* 9 / 5) + 32) + " H:" + String(hum)+"%";
        } else {
        ln3 = "C:" + String(temp) + " H:" + String(hum)+"%";
       }
     updateOLED2(ln1, ln2, ln3);
   }
}

void updateOLED2(String ln1, String ln2, String ln3) {
  char buf[9];
  u8g2.firstPage();
  u8g2.firstPage();
  do {
  u8g2.setFont(u8g2_font_t0_16_tf);
  u8g2.drawStr(1, 10, String(ln1).c_str());
  u8g2.drawStr(1, 30, String(ln2).c_str());
  u8g2.drawStr(1, 50, String(ln3).c_str());
    } while ( u8g2.nextPage() );
}


// Constants for the Arduino-built screens
// Width of the text characters
#define WIDTH_TEXT 6
// Width of the numbers
#define WIDTH_NUM 11
// Width of the last row
#define WIDTH_GARAGE 9
// Additional offset for the text to give it a bit more space
#define TEXT_OFFSET 2
// 3 rows - the start of each row
#define ROW1_Y 21
#define ROW2_Y 42
#define ROW3_Y 63

void drawScreen() {

  if (currentMillis - previousOled >= oledInterval) {
    previousOled += oledInterval;
    
    String s_aqi = String(pm25);
    String s_co2 = String(Co2);
    String s_tvoc = String(TVOC);
    String s_nox = String(NOX);
    String s_temp = String(temp, 1);
    String s_hum = String(hum) + "%";
    String s_aqi_index = "";
  
    u8g2.firstPage();
    do {
      int aqi_x = 60 - (WIDTH_TEXT * 3);
      int co2_x = 128 - (WIDTH_TEXT * 3);
      int tvoc_x = 4 + 64 - (WIDTH_TEXT * 4); // add a bit more offset
      int nox_x = 128 - (WIDTH_TEXT * 3);
      int t_x = 64 - (WIDTH_TEXT * 2); // add a bit more offset
      int h_x = 128 - (WIDTH_TEXT * 3);

      u8g2.setFont(u8g2_font_5x8_tf);
      for (int i = 0; i < round(AQIindex); i++) {
        s_aqi_index += '*';
      }
      u8g2.drawStr(1,5, s_aqi_index.c_str());

      u8g2.setFont(u8g2_font_t0_11_tf);
      u8g2.drawStr(aqi_x, ROW1_Y, "PM2");
      u8g2.drawStr(co2_x, ROW1_Y, "CO2");
  
      u8g2.drawStr(tvoc_x, ROW2_Y, "TVoC");
      u8g2.drawStr(nox_x, ROW2_Y, "NOX");
  
      u8g2.drawStr(t_x - 4, ROW3_Y - 8, "o");
      u8g2.drawStr(t_x, ROW3_Y, "C");
      u8g2.drawStr(h_x, ROW3_Y, "H2O");
  
      u8g2.setFont(u8g2_font_t0_22b_tr);
  
      u8g2.drawStr(aqi_x - TEXT_OFFSET - (WIDTH_NUM * s_aqi.length()), ROW1_Y,
                   s_aqi.c_str());
      u8g2.drawStr(co2_x - TEXT_OFFSET - (WIDTH_NUM * s_co2.length()), ROW1_Y,
                   s_co2.c_str());
      u8g2.drawStr(tvoc_x - TEXT_OFFSET - (WIDTH_NUM * s_tvoc.length()), ROW2_Y,
                   s_tvoc.c_str());
      u8g2.drawStr(nox_x - TEXT_OFFSET - (WIDTH_NUM * s_nox.length()), ROW2_Y,
                   s_nox.c_str());
      u8g2.drawStr(t_x - TEXT_OFFSET - 4 - (WIDTH_NUM * s_temp.length()), ROW3_Y,
                   s_temp.c_str());
      u8g2.drawStr(h_x - TEXT_OFFSET - (WIDTH_NUM * s_hum.length()), ROW3_Y,
                   s_hum.c_str());
  
    } while (u8g2.nextPage());
  }
}

void sendToServer() {
   if (currentMillis - previoussendToServer >= sendToServerInterval) {
     previoussendToServer += sendToServerInterval;
      String payload = "{\"wifi\":" + String(WiFi.RSSI())
      + (Co2 < 0 ? "" : ", \"rco2\":" + String(Co2))
      + (pm25 < 0 ? "" : ", \"pm02\":" + String(pm25))
      + (pm01 < 0 ? "" : ", \"pm01\":" + String(pm01))
      + (pm10 < 0 ? "" : ", \"pm10\":" + String(pm10))
      + (pm003_count < 0 ? "" : ", \"pm003_count\":" + String(pm003_count))
      + (TVOC < 0 ? "" : ", \"tvoc_index\":" + String(TVOC))
      + (NOX < 0 ? "" : ", \"nox_index\":" + String(NOX))
      + ", \"atmp\":" + String(temp)
      + (hum < 0 ? "" : ", \"rhum\":" + String(hum))
      + "}";

      if(WiFi.status()== WL_CONNECTED){
        Serial.println(payload);
        String POSTURL = APIROOT + "sensors/airgradient:" + String(ESP.getChipId(), HEX) + "/measures";
//        Serial.println(POSTURL);
        WiFiClient client;
        HTTPClient http;
        http.begin(client, POSTURL);
        http.addHeader("content-type", "application/json");
        int httpCode = http.POST(payload);
        String response = http.getString();
//        Serial.println(httpCode);
//        Serial.println(response);
        http.end();
      }
      else {
        Serial.println("WiFi Disconnected");
      }
   }
}

// Wifi Manager
 void connectToWifi() {
   WiFiManager wifiManager;
   //WiFi.disconnect(); //to delete previous saved hotspot
   String HOTSPOT = "AG-" + String(ESP.getChipId(), HEX);
   updateOLED2("90s to connect", "to Wifi Hotspot", HOTSPOT);
   wifiManager.setTimeout(90);

   if (!wifiManager.autoConnect((const char * ) HOTSPOT.c_str())) {
     updateOLED2("booting into", "offline mode", "");
     Serial.println("failed to connect and hit timeout");
     delay(6000);
   }

}

// Calculate PM2.5 US AQI
int PM_TO_AQI_US(int pm02) {
  if (pm02 <= 12.0) return ((50 - 0) / (12.0 - .0) * (pm02 - .0) + 0);
  else if (pm02 <= 35.4) return ((100 - 50) / (35.4 - 12.0) * (pm02 - 12.0) + 50);
  else if (pm02 <= 55.4) return ((150 - 100) / (55.4 - 35.4) * (pm02 - 35.4) + 100);
  else if (pm02 <= 150.4) return ((200 - 150) / (150.4 - 55.4) * (pm02 - 55.4) + 150);
  else if (pm02 <= 250.4) return ((300 - 200) / (250.4 - 150.4) * (pm02 - 150.4) + 200);
  else if (pm02 <= 350.4) return ((400 - 300) / (350.4 - 250.4) * (pm02 - 250.4) + 300);
  else if (pm02 <= 500.4) return ((500 - 400) / (500.4 - 350.4) * (pm02 - 350.4) + 400);
  else return 500;
};
