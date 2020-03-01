#include <WiFiClientSecure.h>                // needed for the WiFi communication
#include <ESPmDNS.h>                         // for FOTA Suport
#include <WiFiUdp.h>                         //    ditto
#include <ArduinoOTA.h>                      //    ditto
#include <MQTTClient.h>                      // MQTT Client from Joël Gaehwiler https://github.com/256dpi/arduino-mqtt   keepalive manually to 15s

const char* Hostname = "DUS-VANN-KAI1.1_V2";            // change according your setup : it is used in OTA and as MQTT identifier
String WiFi_SSID = "NSG IOT";               // change according your setup : SSID and password for the WiFi network
String WiFi_PW = "AviDzHybObl4jXhGUIgh";                   //    "
const char* OTA_PW = "otapw";                // change according your setup : password for 'over the air sw update'
String mqtt_broker = "mqtt.norseagroup.com";         // change according your setup : IP Adress or FQDN of your MQTT broker
String mqtt_user = "MQTT-001";               // change according your setup : username and password for authenticated broker access
String mqtt_pw = "4FkejDt59sqmJZW1Dc2B";                   //    "
String input_topic = "/vann2/fromDevice";        // change according your setup : MQTT topic for messages from device to broker
unsigned long waitCount = 0;                 // counter
uint8_t conn_stat = 0;                       // Connection status for WiFi and MQTT:
                                             //
                                             // status |   WiFi   |    MQTT
                                             // -------+----------+------------
                                             //      0 |   down   |    down
                                             //      1 | starting |    down
                                             //      2 |    up    |    down
                                             //      3 |    up    |  starting
                                             //      4 |    up    | finalising
                                             //      5 |    up    |     up

unsigned long lastStatus = 0;                // counter in example code for conn_stat == 5
unsigned long lastTask = 0;                  // counter in example code for conn_stat <> 5

const char* Version = "{\"Version\":\"low_prio_wifi_v2\"}";
const char* Status = "{\"Message\":\"up and running\"}";

WiFiClientSecure TCP;                        // TCP client object, uses SSL/TLS
MQTTClient mqttClient(512);                  // MQTT client object with a buffer size of 512 (depends on your message size)

void messageReceived(String &topic, String &payload) {

  Serial.println("incoming: " + topic + " - " + payload);
  // Note: Do not use the client in the callback to publish, subscribe or
  // unsubscribe as it may cause deadlocks when other things arrive while
  // sending and receiving acknowledgments. Instead, change a global variable,
  // or push to a queue and handle it in the loop after calling `client.loop()`.
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);                                            // config WiFi as client
  MDNS.begin(Hostname);                                           // start MDNS, needed for OTA
  ArduinoOTA.setHostname(Hostname);                               // initialize and start OTA
  ArduinoOTA.setPassword(OTA_PW);                                 //       set OTA password
  ArduinoOTA.onError([](ota_error_t error) {ESP.restart();});     //       restart in case of an error during OTA
  ArduinoOTA.begin();                                             //       at this point OTA is set up
}


void loop() {                                                     // with current code runs roughly 400 times per second
// start of non-blocking connection setup section
  if ((WiFi.status() != WL_CONNECTED) && (conn_stat != 1)) { conn_stat = 0; }
  if ((WiFi.status() == WL_CONNECTED) && !mqttClient.connected() && (conn_stat != 3))  { conn_stat = 2; }
  if ((WiFi.status() == WL_CONNECTED) && mqttClient.connected() && (conn_stat != 5)) { conn_stat = 4;}
  switch (conn_stat) {
    case 0:                                                       // MQTT and WiFi down: start WiFi
      Serial.println("MQTT and WiFi down: start WiFi");
      WiFi.begin(WiFi_SSID.c_str(), WiFi_PW.c_str());
      conn_stat = 1;
      break;
    case 1:                                                       // WiFi starting, do nothing here
      Serial.println("WiFi starting, wait : "+ String(waitCount));
      waitCount++;
      break;
    case 2:                                                       // WiFi up, MQTT down: start MQTT
      Serial.println("WiFi up, MQTT down: start MQTT");
      mqttClient.begin(mqtt_broker.c_str(), 8883, TCP);           //   config MQTT Server, use port 8883 for secure connection
      mqttClient.onMessage(messageReceived);
      mqttClient.connect(Hostname, mqtt_user.c_str(), mqtt_pw.c_str());
      mqttClient.subscribe("/vann2/toDevice");
      conn_stat = 3;
      waitCount = 0;
      break;
    case 3:                                                       // WiFi up, MQTT starting, do nothing here
      Serial.println("WiFi up, MQTT starting, wait : "+ String(waitCount));
      waitCount++;
      break;
    case 4:                                                       // WiFi up, MQTT up: finish MQTT configuration
      Serial.println("WiFi up, MQTT up: finish MQTT configuration");
      //mqttClient.subscribe(output_topic);
      mqttClient.publish(input_topic, Version);
      conn_stat = 5;                    
      break;
  }
// end of non-blocking connection setup section

// start section with tasks where WiFi/MQTT is required
  if (conn_stat == 5) {
    if (millis() - lastStatus > 10000) {                            // Start send status every 10 sec (just as an example)
      Serial.println(Status);
      mqttClient.publish(input_topic, Status);                      //      send status to broker
      mqttClient.loop();                                            //      give control to MQTT to send message to broker
      lastStatus = millis();                                        //      remember time of last sent status message
    }
    ArduinoOTA.handle();                                            // internal household function for OTA
    mqttClient.loop();                                              // internal household function for MQTT
  } 
// end of section for tasks where WiFi/MQTT are required

// start section for tasks which should run regardless of WiFi/MQTT
  if (millis() - lastTask > 1000) {                                 // Print message every second (just as an example)
    Serial.println("print this every second");
    lastTask = millis();
  }
  delay(100);
// end of section for tasks which should run regardless of WiFi/MQTT
}