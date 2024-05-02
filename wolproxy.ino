#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <EEPROM.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "c_types.h"
#include "user_interface.h"


#define VER "1.0.2"
#define EPSIG "wolp"
#define MYNAME "wol-proxy"
#define LED_PIN 2

#define DEBUG 0


// prototypes
boolean wifiAp();
boolean connectWifi();

const byte wolheader[6]={0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

char *ssid;
char *password;
char *myname;
char *epsig;
char *portin;
char *portout;
char *bcastip;

boolean wifiConnected;
boolean apAvail;
boolean epset;
boolean oldRead;
boolean cfgMode;

int portnum;

IPAddress myIP(0,0,0,0);
IPAddress myBroadcastIP(255,255,255,255);

//MDNSResponder mdns;
ESP8266WebServer cserver(80);
WiFiUDP udp;

ESP8266HTTPUpdateServer httpUpdater;

os_timer_t ledBlinkTimer;
os_timer_t ledTimer;


void setup_ota() {

  ArduinoOTA.setPort(8266);

  ArduinoOTA.setHostname(myname);

  // Authentication
  ArduinoOTA.setPassword((const char *)"4038");

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
}

void rdEeprom() {
  int i=0;
  char eebuf[131];
  EEPROM.begin(512);
  for(i=0; i<130; i++) {
    eebuf[i] = EEPROM.read(i);
  }
  EEPROM.end();

  memcpy(epsig, eebuf, 5);
  memcpy(myname, eebuf+5, 50);
  memcpy(ssid, eebuf+55, 35);
  memcpy(password, eebuf+90, 35);
  memcpy(portin, eebuf+125, 5);
  


    
  if(!(memcmp(epsig, EPSIG, 4))) {
    epset=true;
  } else {
    strcpy(myname, MYNAME);
    
    strcpy(portin, "00009");
   
    strcpy(ssid, "rma-lan21");
    strcpy(password, "53734543586303101");
  }
}


void wrEeprom() {
  int i=0;
  char eebuf[131];
  memcpy(eebuf, EPSIG, 5);
  memcpy(eebuf+5, myname, 50);
  memcpy(eebuf+55, ssid, 35);
  memcpy(eebuf+90, password, 35);
  memcpy(eebuf+125, portin, 5);
  
  
  EEPROM.begin(512);
  for(i=0; i<130; i++) {
    EEPROM.write(i, eebuf[i]);
  }
  EEPROM.commit();
  delay(200);
  EEPROM.end();
}

void ledBlinkTimerCB(void *pArg) {
  digitalWrite(LED_PIN, !digitalRead(LED_PIN));
}

void blinkLED(int speed) {
  if (speed > 0) {
    os_timer_arm(&ledBlinkTimer, speed * 100, true);
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  } else if (speed < 0) {
    os_timer_disarm(&ledBlinkTimer);
    digitalWrite(LED_PIN, HIGH); //high is off
  } else {
    os_timer_disarm(&ledBlinkTimer);
    digitalWrite(LED_PIN, LOW); //low is on
  }
}

void ledTimerCB(void *pArg) {
   os_timer_disarm(&ledTimer);
   blinkLED(0);
}

void blinkLedTime(int speed, int time) {
   if ((speed > 0) & (time > 0)) {
      os_timer_arm(&ledTimer, time * 100, false);
      blinkLED(speed);
   }
}


void handleSetup() {
  unsigned char i;
  String content;

  boolean willRestart = false;
  if (cserver.hasArg("myname")){
    cserver.arg("myname").toCharArray(myname, 51);
  }
  if (cserver.hasArg("ssid")){
    cserver.arg("ssid").toCharArray(ssid, 33);
  }
  if (cserver.hasArg("password")){
    cserver.arg("password").toCharArray(password, 33);
  }
  if (cserver.hasArg("portin")){
    cserver.arg("portin").toCharArray(portin, 6);
    portnum=strtoul(portin, NULL, 10);
    if (portnum < 1) {
       portnum=9;
       portin="00009";
    }
  }
  
  if (cserver.hasArg("action")){
    if((cserver.arg("action").toInt()) & 0x1) {
      wrEeprom();
    }
    if(((cserver.arg("action").toInt()) >>1) & 0x1) {
      willRestart = true;
    }
  }


  content = "<html>\n \
  <head>\n \
    <title>" + String(myname) + " Configuration</title>\n \
    <meta name='viewport' content='height=480; width=320; initial-scale=1.0; maximum-scale=1.0; user-scalable=0;'/>\n \
    <style>\n \
      body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\n \
    </style>\n \
  </head>\n \
  <body>\n \
  <br>\n \
  <center>\n \
  <form action='/config' method='POST'>\n \
  <table border=0 cellspacing=0 cellpadding=0><tr><td colspan=2 align=center>\n \
  <table border=1 cellspacing=2 cellpadding=2>\n \
    <tr><td colspan=2 align=center><br><b><u>System</u></b></td></tr>\n \
    <tr><td>mDNS-name:</td><td><input type='text' name='myname' placeholder='system name' value='" + String(myname) + "' maxlength='50'></td></tr>\n \
    <tr><td colspan=2 align=center><br><b><u>WiFi</u></b></td></tr>\n \
    <tr><td>SSID:</td><td><input type='text' name='ssid' placeholder='ssid' value='" + String(ssid) + "' maxlength='32'></td></tr>\n \
    <tr><td>password:</td><td><input type='password' name='password' placeholder='password' value='" + String(password) + "' maxlength='32'></td></tr>\n \
    <tr><td colspan=2 align=center><br><b><u>Management</u></b></td></tr>\n \
    <tr><td>\n \
      <input type='radio' id='sv' name='action' value='1' checked>\n \
      <label for='sv'> save data only&nbsp;&nbsp;</label><br>\n \
      <input type='radio' id='rb' name='action' value='2'>\n \
      <label for='rb'> restart only&nbsp;&nbsp;</label><br>\n \
      <input type='radio' id='sr' name='action' value='3'>\n \
      <label for='sr'> save data and restart&nbsp;&nbsp;</label>\n \
    </td><td align=center>\n \
      <input type='submit' value='submit' name='submit' style='background-color:#5555ff;color:#ffffff'>\n \
    </td></tr>\n \
  </table>\n \
  </td></tr><tr>\n \
  <td align=left><font size=-3><i>Wake On Lan Proxy V" + VER + "</i></font></td>\n \
  <td align=right><font size=-3><i>(C) RMD, 2023</i></font></td></tr>\n \
  </table>\n \
  </form>\n \
  </center>\n \
  </body>\n \
  </html>";

  cserver.send (200, "text/html", content);
  if (willRestart) {
    delay(1500); 
    ESP.restart();
  }
}


void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += cserver.uri();
  message += "\nMethod: ";
  message += ( cserver.method() == HTTP_GET ) ? "GET" : "POST";
  message += "\nArguments: ";
  message += cserver.args();
  message += "\n";
  for ( uint8_t i = 0; i < cserver.args(); i++ ) {
    message += " " + cserver.argName ( i ) + ": " + cserver.arg ( i ) + "\n";
  }
  cserver.send ( 404, "text/plain", message );
}



void setup() {
  Serial.begin(9600,SERIAL_8N1,SERIAL_TX_ONLY);
  pinMode(3, INPUT_PULLUP);
  pinMode(0, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  ssid=(char*)malloc(36);
  password=(char*)malloc(36);
  myname=(char*)malloc(51);
  epsig=(char*)malloc(5);
  portin=(char*)malloc(6);
  portout=(char*)malloc(6);
  bcastip=(char*)malloc(16);

  wifiConnected = false;
  apAvail = false;
  epset = false;

  delay(500);
  Serial.println("start...");
  wifi_set_sleep_type(NONE_SLEEP_T);
  
  os_timer_setfn(&ledBlinkTimer, ledBlinkTimerCB, NULL);
  os_timer_setfn(&ledTimer, ledTimerCB, NULL);
  blinkLED(2);
  
  rdEeprom();
  setup_ota();

  if (ssid[0] == 0) {
    apAvail = wifiAp();
  } else {
    wifiConnected = connectWifi();
    if(!(wifiConnected)) {
      apAvail = wifiAp();
    }
  }

  if(wifiConnected) {
    Serial.printf("connected to network %s\n", ssid);
    Serial.printf("my name: %s\n", myname);
    Serial.printf("my IP: %s\n", myIP.toString().c_str());
    Serial.printf("bc IP: %s\n", myBroadcastIP.toString().c_str());
    Serial.printf("listening to wol packets on port %d\n", 9);
    httpUpdater.setup(&cserver, "/update" );
    if (!MDNS.begin(myname, myIP)) {
       Serial.printf("mDNS not available\nplease use IP address for access\n");
    }
    cserver.on ( "/", handleSetup );
    cserver.on ( "/setup", handleSetup );
    cserver.on ( "/config", handleSetup );
    cserver.onNotFound ( handleNotFound );
    cserver.begin();
    MDNS.addService("http", "tcp", 80);
    udp.begin(9);
    blinkLED(0);
  } else if (apAvail) {
    Serial.printf("not connected to a network\nrunning in ap-mode\nplease configure me\n", ssid);
    Serial.printf("my name: %s\n", myname);
    Serial.printf("my IP: %s\n", myIP.toString().c_str());
    httpUpdater.setup(&cserver, "/update" );
    if (!MDNS.begin(myname, myIP)) {
       Serial.printf("mDNS not available\nplease use IP address for access\n");
    }
    cserver.on ( "/", handleSetup );
    cserver.on ( "/setup", handleSetup );
    cserver.on ( "/config", handleSetup );
    cserver.onNotFound ( handleNotFound );
    cserver.begin();
    MDNS.addService("http", "tcp", 80);
    blinkLED(5);
  } else {
    Serial.printf("oops - something went wrong\nwifi not available\nplease reset me\n");
    blinkLED(2);
  }

}


void handleUDP() {
  int i;
  char incomingPacket[256];

  int recvd = udp.parsePacket();
  if (recvd) {
     blinkLED(1);
     blinkLedTime(1,5);
     Serial.printf("Received %d bytes from %s, port %d\n", recvd, udp.remoteIP().toString().c_str(), udp.remotePort());
     int len = udp.read(incomingPacket, 255);
     if (udp.remoteIP() != myIP) {
        if ((recvd == 102) & (!(memcmp(incomingPacket, wolheader, 6)))) {
           blinkLedTime(3,20);
           udp.beginPacket(myBroadcastIP, 9);
           udp.write(incomingPacket, recvd);
           udp.endPacket();
           delay(100);
           udp.beginPacket(myBroadcastIP, 9);
           udp.write(incomingPacket, recvd);
           udp.endPacket();
           if (DEBUG) {
              Serial.printf("packet relayed to broadcast:\n");
              for (i=0; i<102; i++) {
                 Serial.printf("0x%02x ", incomingPacket[i]);
              }
              Serial.printf("\n\n");
           }
        } else {
           if ((len > 0) && (DEBUG)) {
              Serial.printf("read len: %d\n", len);
              incomingPacket[len] = '\0';
              Serial.printf("UDP packet contents: %s\n\n", incomingPacket);
           }
        }
     }
  } 
}





void loop() {
  if (wifiConnected || apAvail) {
     ArduinoOTA.handle();
     MDNS.update();
     cserver.handleClient();
     if(wifiConnected) {
        handleUDP();
     }
     delay(100);
  } else {
     delay(1000);
  }
}


boolean wifiAp() {
  byte mac[6]; 
  char ap_ssid[64];
  WiFi.macAddress(mac); 
  sprintf(ap_ssid, "wolp_start");
  boolean state = false;
  WiFi.mode(WIFI_AP);
     state = WiFi.softAP(ap_ssid, ap_ssid, 3, false);
  if (state) {
     myIP=WiFi.softAPIP();
     myBroadcastIP=WiFi.broadcastIP();
  } 
  return state;
}


boolean connectWifi() {
  boolean state = true;
  int i=0;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    if (i > 40){
      state = false;
      break;
    }
    i++;
  }
  
  if (state) {
    myIP=WiFi.localIP();
    myBroadcastIP=WiFi.broadcastIP();
  }
  return state;
}
