#include <Arduino.h>
#include <SPI.h>
#include <eth.h>
#include <MQTTClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include "pass.cpp"

String devicename = SECRET_DEVICE_KAI62;
const char* mqtt_user = SECRET_BROKER_USER_KAI11; 
const char* mqtt_pw = SECRET_BROKER_PASSWORD_KAI11;  
const char* Description = SECRET_DEVICE_KAI62_DESC;


const char* Hostname = devicename.c_str();
const char* mqtt_to_device = "/vann2/toDevice/DUS-VANN-KAI6.2" ;
const char* mqtt_broker = "mqtt.norseagroup.com";
const char* input_topic = "/vann2/fromDevice";   
const char* start_topic = "/vann/devicestart";   
const char* hartbeat_topic = "/vann/hartbeat";   
const char* Version = "2.02";


// MODUINO
#include <SSD1306.h>
#define flowmeterPin 36 //Moduino 36 - IN1
#define buttonPin 34
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
volatile int pulsecount = 0;
unsigned long flowLastReportTime;
unsigned long flowCurrentTime;
int start_min_flow = 5;
int stopp_no_flow_minutes = 2;

bool lastflowpinstatus = 1;
bool flowpinstatus;
bool lastbuttonpinstatus = 1;
bool buttonpinstatus;

bool started = false;
unsigned long starttime;
unsigned long lastflowtime;
unsigned long startpulse = 0;
volatile int callbackWD = 120;
int rssi = -100;
bool do_startup_report = true;

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
TaskHandle_t Task3;

const char* Status = "{\"Message\":\"up and running\"}";

void messageReceived(String &topic, String &payload) {
  Serial.println("incoming: " + topic + " - " + payload);
  StaticJsonDocument<3000> doc;
  deserializeJson(doc, payload);
  if(topic == hartbeat_topic)
  {
      callbackWD=payload.toInt();
  }
  //Only process messages to this device
  else if (doc["device"] == Hostname)
  {
    if (strcmp(doc["command"], "pulsecount") == 0)
    {
      pulsecount = doc["value"].as<int>();
    }
    else if (strcmp(doc["command"], "pulsecount_first") == 0)
    {
      pulsecount = pulsecount + doc["value"].as<int>();
    }
    else if (strcmp(doc["command"], "start_min_flow") == 0)
    {
      start_min_flow = doc["value"].as<int>();
    }
    else if (strcmp(doc["command"], "stopp_no_flow_minutes") == 0)
    {
      stopp_no_flow_minutes = doc["value"].as<int>();
    }
    else if (strcmp(doc["command"], "flow_max") == 0)
    {
        flow_max = doc["value"].as<int>();
    }
    else if (strcmp(doc["command"], "reboot") == 0)
    {
      ESP.restart();
    }
    else
    {
      Serial.println("Unknown command : " + payload);
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
    mqttClient.subscribe(hartbeat_topic);
    Serial.println(mqtt_to_device);
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
    flowpinstatus = digitalRead(flowmeterPin);
    if(flowpinstatus != lastflowpinstatus)
    {
      if(flowpinstatus)
      {
        pulse_since_last_loop++;
        pulsecount++;  
      }      
      lastflowpinstatus = flowpinstatus;
    }

    vTaskDelay(1);

  }while(true);

}

void buttonloop( void * parameter )
{
  do{

    //detect flow pult
    buttonpinstatus = digitalRead(buttonPin);
    if(buttonpinstatus != lastbuttonpinstatus)
    {
      if(buttonpinstatus)
      {
        pulse_since_last_loop++;
        pulsecount++;    
      }      
      lastbuttonpinstatus = buttonpinstatus;
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

      //Watchdog. Must recieve new callback value from node-red to avoid reboot
      callbackWD--;
      if(callbackWD <= 0)
      {
         ESP.restart();
      }
     
    }
    vTaskDelay(1);

  }while(true);

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

   xTaskCreatePinnedToCore(
      buttonloop,
      "buttonloop",
      6000,
      NULL,
      1,
      &Task3,
      0);

  pinMode(16, OUTPUT); 
  digitalWrite(16, 1);
  pinMode(flowmeterPin, INPUT_PULLUP);
  pinMode(buttonPin, INPUT_PULLUP);

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
  } else if(eth_is_connected & mqttClient_is_started & do_startup_report)
  {
    //Create start document payload and send. Only once pr reboot.
    String payload;
    StaticJsonDocument<300> startdoc;
    startdoc["hostname"] = devicename;
    startdoc["ip"] = ETH.localIP().toString();
    startdoc["start"] = timeClient.getEpochTime();
    startdoc["version"] = Version;
    serializeJson(startdoc, payload);
    Serial.println(payload);
    mqttClient.publish(start_topic, payload);
    mqttClient.loop(); 
    do_startup_report = false;
  }

  //Only when connected
  if (eth_is_connected) { 

    //Send report
    if (millis() - lastStatus > 10000) {                            // Start send status every 10 sec (just as an example)
      
      
      String payload;
      StaticJsonDocument<300> report;
      report["hostname"] = devicename;
      report["type"] = "flow";
      report["flow"] = flow_rate;
      report["pulsecount"] = pulsecount;
      report["volume"] = pulsecount * vol_pr_pulse;
      report["started"] = started ? 1 : 0;
      
      serializeJson(report, payload);
      Serial.println(payload);
      mqttClient.publish(input_topic, payload);                      //      send status to broker
      mqttClient.loop();                                            //      give control to MQTT to send message to broker
      lastStatus = millis();                                        //      remember time of last sent status message
      timeClient.update();

    }
    mqttClient.loop();                                              // internal household function for MQTT
  }

  //Update display
  display.clear();
  display.drawString(0, 0, String(flow_rate * 1.0,3) + " " + FlowUnit + "   (" + callbackWD + (")"));
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
  
  //Detect start / stopp operation
  if(flow_rate > start_min_flow)
    {
      //update timestamp for last flow more  than min
      lastflowtime = timeClient.getEpochTime();

      //start the operation if not already started
      if(!started)
      {
        Serial.println("Started");
        started = true;
        starttime = timeClient.getEpochTime();
        startpulse = pulsecount;
      }
      
            
    }else
    {
      //flow smaller than min
      if(started)
      {
        if(timeClient.getEpochTime() >= (lastflowtime + (stopp_no_flow_minutes)))
        {
          started= false;
          
          //Send stopp message
          String payload;
          StaticJsonDocument<300> stopdoc;
          
          stopdoc["hostname"] = devicename;
          stopdoc["type"] = "stopp";
          stopdoc["starttime"] = starttime;
          stopdoc["stopptime"] = timeClient.getEpochTime();
          stopdoc["duration"] = timeClient.getEpochTime() - starttime;
          stopdoc["startvolume"] = startpulse * vol_pr_pulse;
          stopdoc["stoppvolume"] = pulsecount * vol_pr_pulse;
          stopdoc["volume"] = (pulsecount - startpulse) * vol_pr_pulse ;

          serializeJson(stopdoc, payload);
          Serial.println(payload);
          mqttClient.publish(input_topic, payload); 

        }
      }
    }
  delay(100);
}
