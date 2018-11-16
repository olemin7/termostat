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
#include "CFilter.h"
#include "NTPtime.h"
#include "CMQTT.h"
#include "CRelayPID.h"

//#define DEBUG

#if 1
#define WIFI_SERVER
#include "secret.h_ex"
#else
#include "secret.h"
#endif
const auto RelayPin = D6;
const auto DHTPin = D4;
const auto TermistorPin = A0;

const char* update_path = "/firmware";
const char* DEVICE_NAME = "termostat";
const unsigned long SYNK_NTP_PERIOD = 24 * 60 * 60 * 1000; // one per day
#ifndef DEBUG
const long MQTT_REFRESH_PERIOD = 15 * 60 * 1000;
#else
const long MQTT_REFRESH_PERIOD=5*1000;
#endif
const auto SENSOR_REFRESH_PERIOD = 5 * 1000;

Timezone myTZ((TimeChangeRule ) { "DST", Last, Sun, Mar, 3, +3 * 60 },
    (TimeChangeRule ) { "STD", Last, Sun, Oct, 4, +2 * 60 });

time_t get_local_time() {
  return myTZ.toLocal(now());
}
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

ESP8266WebServer server(80);
WebFaceWiFiConfig WiFiConfig(server);
DHTesp dht;
CConfigs Config(server);
ESP8266OTA otaUpdater;
CMQTT mqtt;
CRelayPID RelayPID(RelayPin);


#include "cli_cmd_list.h"


/***
 *
 */
void setup() {
  WiFi.persistent(false);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(DHTPin, INPUT);
  RelayPID.setup();
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
  mqtt.setup(mqtt_server, mqtt_port);
  mqtt.setClientID(DEVICE_NAME);
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

  cli_cmd_list_setup();
  Serial.println("Started");
  cmd_display();
}

void mqtt_loop() {
  if (WL_CONNECTED != WiFi.status()) {
    return;
  }

  const long now = millis();
  mqtt.loop();
  static long nextMsgMQTT = 0;
  if (now < nextMsgMQTT) {
    return;
  }

  nextMsgMQTT = now + MQTT_REFRESH_PERIOD;

  String topic;

  topic = "channels/" + String(termostat_channelID) + "/publish/"
      + termostat_Write_API_Key;
  String data;

  data = "field1=" + String(Config.status_.air_term_, 1);
  data += "&field2=" + String(Config.status_.air_humm_, 1);
  data += "&field3=" + String(Config.status_.floor_term_, 1);
  data += "&field4=" + String(Config.status_.desired_temperature_, 1);
  data += "&field6=" + String(Config.status_.heater_on_);
  data += "&field7=" + String(Config.status_.adc_);
  Config.status_.adc_++;
#ifndef WIFI_SERVER
  mqtt.publish(topic, data);
#endif
  Serial.print("topic= ");
  Serial.print(topic);
  Serial.print(" [");
  Serial.print(data);
  Serial.println("]");
}
void sensor_loop() {
  const long now = millis();
  static long nextSensor = 0;
  if (now < nextSensor) {
    return;
  }
  nextSensor = now + SENSOR_REFRESH_PERIOD;

  const auto air_term = dht.getTemperature();
  if (!isnan(air_term)) {
    Config.status_.air_term_ = air_term;
  }
  const auto air_humm = dht.getHumidity();
  if (!isnan(air_humm)) {
    Config.status_.air_humm_ = air_humm;
  }
  const auto ADCvalue = analogRead(TermistorPin);
  Config.status_.floor_term_ = Config.termistor_.convert(ADCvalue);

  RelayPID.setInput(Config.status_.floor_term_);
}

void loop_scheduler() {
  const long now = millis();
  static long next_loop_scheduler = 0;
  if (next_loop_scheduler >= now) {
    return;
  }
  next_loop_scheduler = now + 15 * 1000;
  const auto temperature = Config.mainConfig_.getDesiredTemperature();
  Config.status_.desired_temperature_ = temperature;
  RelayPID.setTarget(temperature);
}

void loop() {
  loop_scheduler();
  server.handleClient();
  cmdPoll();
  CFilterLoop::loops();
  wifi_loop();
  mqtt_loop();
  sensor_loop();
  RelayPID.loop();
}
