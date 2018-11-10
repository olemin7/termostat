//https://github.com/google/googletest.git
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#define ARDUINOJSON_ENABLE_PROGMEM 1
#define  ARDUINOJSON_DEFAULT_NESTING_LIMIT 10
//libs
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <Wire.h>
#include <FS.h>
#include "DHTesp.h"
#include "unsorted.h"
#include <ESP8266OTA.h>
#include <Time.h>
#include <TimeLib.h>
#include <Timezone.h>
//user libs

#include "CConfigs.h"
#include "WebFaceWiFiConfig.h"
#include "CFrontendFS.h"
#include "frontend.h"
#include "cmd.h"
#include "CFilter.h"
#include "NTPtime.h"



#if 0
#define WIFI_SERVER
#else
#include "secret.h"
#endif
const auto RelayPin = D6;
const auto DHTPin = D4;
const auto TermistorPin = A0;

const char* update_path = "/firmware";
const char* DEVICE_NAME = "termostat";
const unsigned long SYNK_NTP_PERIOD = 24 * 60 * 60 * 1000; // one per day

class CSetClock: public ObserverWrite<time_t> {
public:
  virtual void writeValue(time_t value) {
    
    Serial.printf("set GMT %02u:%02u:%02u done\n", hour(value), minute(value),
        second(value));
    setTime(value);
  }
};
/***
 *
 */
class CClock: public Observer<time_t>, CSubjectPeriodic<time_t> {
  virtual uint32_t getTimeInMs() {
    return now();
  }
  void update(const Subject<time_t> &time) {
    setValue(time.getValue());
  }
public:

  bool readValue(time_t &time) override {
    if (timeNotSet == timeStatus())
      return false;
    time = now();
    return true;
  }

  CClock(uint32_t aperiod) :
      CSubjectPeriodic<time_t>(aperiod) {
  }
  ;
};

NTPtime ntpTime(SYNK_NTP_PERIOD);
CSetClock setClock;
CClock Clock1sec(1000);
Timezone myTZ((TimeChangeRule ) { "DST", Last, Sun, Mar, 3, +3 * 60 },
    (TimeChangeRule ) { "STD", Last, Sun, Oct, 4, +2 * 60 });

ESP8266WebServer server(80);
WebFaceWiFiConfig WiFiConfig(server);
DHTesp dht;
CConfigs Config(server);
ESP8266OTA otaUpdater;

void cli_info(int argc, char **argv) {
  Config.info();
  cmd_handler_list();
}

void cli_ifconfig(int argc, char **argv) {
  const auto mode = WiFi.getMode();

  Serial.print("WiFi mode=");
  Serial.println(mode);
  if (WIFI_STA == mode || WIFI_AP_STA == mode) {
    Serial.print("Connected  to ");
    Serial.println(wifi_ssid);
    Serial.print("Station IP address: ");
    Serial.println(WiFi.localIP());
  }
  if (WIFI_AP == mode || WIFI_AP_STA == mode) {
    IPAddress myIP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(myIP);
  }
}
void cli_format(int argc, char **argv) {
  bool result = SPIFFS.format();
  Serial.print("SPIFFS format: ");
  Serial.println(result);
}
void cli_relay(int argc, char **argv) {
  if (2 == argc) {
    switch (*argv[1]) {
      case '0':
        digitalWrite(RelayPin, 0);
        break;
      case '1':
        digitalWrite(RelayPin, 1);
        break;
    }
  }
}

void cli_termo(int argc, char **argv) {
  Serial.print("dht status:");
  Serial.println(dht.getStatusString());
  Serial.print("Temperature= ");
  Serial.println(dht.getTemperature());
  Serial.print("Humidity= ");
  Serial.println(dht.getHumidity());

  auto ADCvalue = analogRead(TermistorPin);
  Serial.print("Analoge ");
  Serial.println(ADCvalue);
  Serial.print("transformed  ");
  Serial.println(Config.termistor.convert(ADCvalue));
}

void cli_freset(int argc, char **argv) {
  Config.factoryReset();
}

void cli_time(int argc, char **argv) {
  Serial.print("timeStatus ");
  auto stat = timeStatus();
  Serial.println(stat);
  time_t value;
  if (Clock1sec.readValue(value)) {
      Serial.printf("set GMT %02u:%02u:%02u done\n", hour(value), minute(value),
          second(value));
  }
}

/***
 *
 */
void setup() {
  WiFi.persistent(false);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(RelayPin, OUTPUT);
  digitalWrite(RelayPin, 0); //off
  pinMode(DHTPin, INPUT);
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\nBOOTING ESP8266 ...");
  dht.setup(DHTPin, DHTesp::DHT22);
#ifdef WIFI_SERVER //cliend
	Serial.print("Configuring access point...");
	/* You can remove the password parameter if you want the AP to be open. */
  WiFi.softAP("test", "esp12345");
	WiFi.mode(WIFI_AP);
#else
  setup_wifi(wifi_ssid, wifi_password, DEVICE_NAME);
#endif
  MDNS.begin(DEVICE_NAME);
  cli_ifconfig(0, NULL);

	CFrontendFS::add(server, "/thtml1.html", ct_html,_frontend_thtml1_html_);
	CFrontendFS::add(server, "/term_main.js", ct_js,_frontend_term_main_js_);
	CFrontendFS::add(server, "/term_main.css", ct_css,_frontend_term_main_css_);
	CFrontendFS::add(server, "/", ct_html,_frontend_term_main_html_);
	CFrontendFS::add(server, "/WiFiConfigEntry.html", ct_html,_frontend_WiFiConfigEntry_html_);
//	CFrontendFS::add(server, "/favicon.ico.html", ct_html,_frontend_WiFiConfigEntry_html_);

	server.onNotFound([]{
			Serial.println("Error no handler");
			Serial.println(server.uri());
	});

  otaUpdater.setUpdaterUi("Title", "Banner", "Build : 0.01", "Branch : master", "Device info : ukn", "footer");
  otaUpdater.setup(&server, update_path, ota_username, ota_password);

  server.begin();
  Config.begin();

  MDNS.addService("http", "tcp", 80);
  Serial.printf("HTTPUpdateServer ready! Open http://%s.local%s in your browser and login with username '%s' and password '%s'\n", DEVICE_NAME, update_path, ota_username, ota_password);

  ntpTime.init();
  ntpTime.addListener(setClock);
  ntpTime.addListener(Clock1sec);

  cmdInit();
  cmdAdd("info", cli_info);
  cmdAdd("ifconfig", cli_ifconfig);
  cmdAdd("relay", cli_relay);
  cmdAdd("term", cli_termo);
  cmdAdd("freset", cli_freset);
  cmdAdd("format", cli_format);
  cmdAdd("time", cli_time);

  Serial.println("Started");

  cmd_display();
}

void loop() {
  server.handleClient();
  cmdPoll();
  CFilterLoop::loops();
  wifi_loop();
}
