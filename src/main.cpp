#include <Arduino.h>
#include <SPI.h>
#include <eth.h>
#include <MQTTClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include "pass.cpp"

const char* Hostname = "DUS-VANN-KAI1.1_V2"; 
const char* mqtt_to_device = "/vann2/toDevice";
const char* mqtt_broker = "mqtt.norseagroup.com";
const char* mqtt_user = SECRET_BROKER_USER_KAI11; 
const char* mqtt_pw = SECRET_BROKER_PASSWORD_KAI11;  
const char* devicename = SECRET_DEVICE_KAI62;
const char* Description = SECRET_DEVICE_KAI62_DESC;
const char* input_topic = "/vann2/fromDevice";   

const char* Version = "2.01";

// MODUINO
#include <SSD1306.h>
#define flowmeterPin 36 //Moduino 36 - IN1
#define buttonPin 32
#define resetPin 34     //Moduino 34 - User Button
#define OLED_ADDRESS 0x3c
#define I2C_SDA 12
#define I2C_SCL 13
#define DISPLAYTYPE GEOMETRY_128_64
#define baud 115200
String rotate = "Yes";

// TTGO
//#include <SSD1306.h>
//#define flowmeterPin 32   //02
//#define buttonPin 32      //32
//#define resetPin 34
//#define OLED_ADDRESS 0x3c
//#define I2C_SDA 14
//#define I2C_SCL 13
//#define DISPLAYTYPE GEOMETRY_128_32
//#define baud 115200
//#define displayorientation TEXT_ALIGN_RIGHT  
//String rotate = "No";

static bool eth_is_connected = false;
static bool mqttClient_is_started = false;
unsigned long lastStatus = 0;  

//Runtime variables
volatile double flow_rate;
int flow_max = 300;
unsigned int pulse_since_last_loop;
volatile unsigned long pulsecount = 0;
unsigned long flowLastReportTime;
unsigned long flowCurrentTime;
bool lastflowpin = 1;
bool flowpin;
bool started = false;
unsigned long starttime;
unsigned long lastflowtime;
unsigned long startpulse = 0;
volatile int callbackWD = 0;
int rssi = -100;

const double vol_pr_pulse = 0.001;   //m3
const char* VolumeUnit = "m3";
const char* FlowUnit = "m3/Hour";
//const double Factor = 1.0;                        // Second
//const double Factor = 60.0;                       // Minute
const double Factor = 60.0 * 60.0;                  // Hour

unsigned long boot_timestamp = 0;
String boot_time = "";

WiFiClientSecure TCP;                        
MQTTClient mqttClient(512);  

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "no.pool.ntp.org", 3600, 60000);
SSD1306 display(OLED_ADDRESS, I2C_SDA, I2C_SCL, DISPLAYTYPE);

TaskHandle_t Task1;
TaskHandle_t Task2;

const char* Status = "{\"Message\":\"up and running\"}";

void messageReceived(String &topic, String &payload) {
  Serial.println("incoming: " + topic + " - " + payload);
  StaticJsonDocument<2000> doc;
  deserializeJson(doc, payload);

  //Only process messages to this device
  if (doc["device"] == Hostname)
  {
    if (strcmp(doc["command"], "pulsecount") == 0)
    {
      pulsecount = doc["value"] ;
    }
    else if (strcmp(doc["command"], "flow_max") == 0)
    {
        flow_max = doc["value"];
    }
    else if (strcmp(doc["command"], "reboot") == 0)
    {
      ESP.restart();
    }
    else
    {
      //Send Error
    }
  }
}

void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case SYSTEM_EVENT_ETH_START:
      Serial.println("ETH Started");
      //set eth hostname here
      ETH.setHostname(Hostname);
      break;
    case SYSTEM_EVENT_ETH_CONNECTED:
      Serial.println("ETH Connected");
      break;
    case SYSTEM_EVENT_ETH_GOT_IP:
      Serial.print("ETH MAC: ");
      Serial.print(ETH.macAddress());
      Serial.print(", IPv4: ");
      Serial.print(ETH.localIP());
      if (ETH.fullDuplex()) {
        Serial.print(", FULL_DUPLEX");
      }
      Serial.print(", ");
      Serial.print(ETH.linkSpeed());
      Serial.println("Mbps");
      eth_is_connected = true;
      mqttClient_is_started = false;
      break;
    case SYSTEM_EVENT_ETH_DISCONNECTED:
      Serial.println("ETH Disconnected");
      eth_is_connected = false;
      break;
    case SYSTEM_EVENT_ETH_STOP:
      Serial.println("ETH Stopped");
      eth_is_connected = false;
      break;
    default:
      break;
  }

}

String GetUptime(){
  unsigned long uptimeseconds = (timeClient.getEpochTime() - boot_timestamp)  ;
  long days=0;
  long hours=0;
  long mins=0;
  mins=uptimeseconds/60.0; //convert seconds to minutes
  hours=mins/60.0; //convert minutes to hours
  days=hours/24.0; //convert hours to days
  uptimeseconds=uptimeseconds-(mins*60.0); //subtract the coverted seconds to minutes in order to display 59 secs max
  mins=mins-(hours*60.0); //subtract the coverted minutes to hours in order to display 59 minutes max
  hours=hours-(days*24.0); //subtract the coverted hours to days in order to display 23 hours max
  return String(days) + " Days " + String(hours) + ":" + String(mins) + ":" + String(uptimeseconds);
}

void StartMqttClient(){
  Serial.println("Starting MQTT Client");
    mqttClient.begin(mqtt_broker, 8883, TCP);           //   config MQTT Server, use port 8883 for secure connection
    mqttClient.onMessage(messageReceived);
    mqttClient.connect(Hostname, mqtt_user, mqtt_pw);
    mqttClient.subscribe(mqtt_to_device);
    timeClient.begin();
    timeClient.update();
    mqttClient_is_started = true;

    if(boot_timestamp == 0){
      boot_timestamp = timeClient.getEpochTime();
      boot_time = timeClient.getFormattedTime();
    }
}

void sensorloop( void * parameter )
{
  do{

    //detect flow pult
    flowpin = digitalRead(flowmeterPin);
    if(flowpin != lastflowpin)
    {
      if(flowpin)
      {
        pulse_since_last_loop++;
        pulsecount = pulsecount + 1;  
      }      
      lastflowpin = flowpin;
    }

    vTaskDelay(1);

  }while(true);

}

void flowloop( void * parameter )
{
  do{

    flowCurrentTime = millis();
    if(flowCurrentTime >= (flowLastReportTime + 5000))
    {
      flowLastReportTime = flowCurrentTime;
      flow_rate = (vol_pr_pulse * Factor * pulse_since_last_loop / 5.0);
      pulse_since_last_loop = 0.0;

      callbackWD++;
      if(callbackWD > 10)
      {
         ESP.restart();
      }
     
    }
    vTaskDelay(1);

  }while(true);

}

static void DeviceTwinCallback(const unsigned char *payLoad, int size)
{
  char *temp = (char *)malloc(size + 1);
  if (temp == NULL)
  {
    return;
  }
  memcpy(temp, payLoad, size);
  temp[size] = '\0';
  StaticJsonDocument<2000> doc;
  deserializeJson(doc, payLoad);
  long twinPulsecount = (unsigned long)doc["reported"]["pulsecount"];      
  if (twinPulsecount > pulsecount)
  {
    pulsecount = twinPulsecount;
  }  
  int twinFlowMax = (int)doc["reported"]["flow_max"];      
  if ((twinFlowMax > 0) & (twinFlowMax != flow_max))
  {
    flow_max = twinFlowMax;
  }
  free(temp);

}

void setup() {
  Serial.begin(115200);

  
    xTaskCreatePinnedToCore(
      sensorloop,
      "sensorloop",
      6000,
      NULL,
      1,
      &Task1,
      0);

  xTaskCreatePinnedToCore(
      flowloop,
      "flowloop",
      6000,
      NULL,
      1,
      &Task2,
      0);

 

  //Display
  display.init();
  if(rotate == "Yes")
  {
    display.flipScreenVertically();
  }
  
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 0, "Booting....");
  display.drawString(0,10, "Waiting for ethernet");
  display.display();

  WiFi.onEvent(WiFiEvent);
  ETH.begin(0, -1, 33, 18, ETH_PHY_LAN8720, ETH_CLOCK_GPIO0_IN);
}

void loop()
{
  if(eth_is_connected & !mqttClient_is_started){
    StartMqttClient();
  }

  //Only when connected
  if (eth_is_connected) { 

    //Send report
    if (millis() - lastStatus > 10000) {                            // Start send status every 10 sec (just as an example)
      Serial.println(Status);
      mqttClient.publish(input_topic, Status);                      //      send status to broker
      mqttClient.loop();                                            //      give control to MQTT to send message to broker
      lastStatus = millis();                                        //      remember time of last sent status message
      timeClient.update();
      Serial.println(GetUptime());
      Serial.println(timeClient.getFormattedTime());
    }
    mqttClient.loop();                                              // internal household function for MQTT
  }

  //Update display
  display.clear();
  display.drawString(0, 0, String(flow_rate * 1.0,3) + " " + FlowUnit);
  display.drawString(0, 10, String(pulsecount * vol_pr_pulse, 3 ) + " " + VolumeUnit);     
  display.drawString(0,20, "Uptime: " + GetUptime());

  //Moduino supports four lines, and requires rotation of display
  if(rotate == "Yes")
  {
    display.drawString(0, 30, Description);
    display.drawString(0, 40, "Build Version : " + String(Version));
    display.drawString(0, 50, devicename);
  }       
  display.display(); 

  delay(100);
}
