/*
 Copyright (C) 2020 chester4444@wolke7.net
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266mDNS.h>
  #include <SoftwareSerial.h>
#elif defined(ESP32)
  #include <WiFi.h>
  #include <ESPmDNS.h>
#endif
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include "credentials.h"
#include "WaterMeter.h"
#include "hwconfig.h"
#include "mqttsensorconfig.h"

#define ESP_NAME "WaterMeter"

#define DEBUG 0

#if defined(ESP32)
  #define LED_BUILTIN 2
#endif


WaterMeter waterMeter;

WiFiClient espMqttClient;
PubSubClient mqttClient(espMqttClient);

char MyIp[16];
int cred = -1;

int getWifiToConnect(int numSsid)
{
  for (int i = 0; i < NUM_SSID_CREDENTIALS; i++)
  {
    //Serial.println(WiFi.SSID(i));
    
    for (int j = 0; j < numSsid; ++j)
    {
      /*Serial.print(j);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i).c_str());
      Serial.print(" = ");
      Serial.println(credentials[j][0]);*/
      if (strcmp(WiFi.SSID(j).c_str(), credentials[i][0]) == 0)
      {
        Serial.println("Credentials found for: ");
        Serial.println(credentials[i][0]);
        return i;
      }
    }
  }
  return -1;
}

// connect to wifi – returns true if successful or false if not
bool ConnectWifi(void)
{
  int i = 0;

  Serial.println("starting scan");
  // scan for nearby networks:
  int numSsid = WiFi.scanNetworks();

  Serial.print("scanning WIFI, found ");
  Serial.print(numSsid);
  Serial.println(" available access points:");

  if (numSsid == -1)
  {
    Serial.println("Couldn't get a wifi connection");
    return false;
  }
  
  for (int i = 0; i < numSsid; i++)
  {
    Serial.print(i+1);
    Serial.print(") ");
    Serial.println(WiFi.SSID(i));
  }

  // search for given credentials
  cred = getWifiToConnect(numSsid);
  if (cred == -1)
  {
    Serial.println("No Wifi!");
    return false;
  }

  // try to connect
  WiFi.begin(credentials[cred][0], credentials[cred][1]);
  Serial.println("");
  Serial.print("Connecting to WiFi ");
  Serial.println(credentials[cred][0]);

  i = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    digitalWrite(LED_BUILTIN, LOW);
    delay(300);
    Serial.print(".");
    digitalWrite(LED_BUILTIN, HIGH);
    delay(300);
    if (i++ > 30)
    {
      // giving up
      return false;
    }
  }
  return true;
}

void mqttDebug(const char* debug_str)
{
    String s=String(node_id) + "/0/debug";
    mqttClient.publish(s.c_str(), debug_str);
}

void mqttCallback(char* topic, byte* payload, unsigned int len)
{
  // create a local copies of topic and payload
  // PubSubClient overwrites it internally
  String t(topic);
  byte *p = new byte[len];
  memcpy(p, payload, len);
  
/*  Serial.print("MQTT-RECV: ");
  Serial.print(topic);
  Serial.print(" ");
  Serial.println((char)payload[0]); // FIXME LEN
*/
  if (strstr(topic, "/smarthomeNG/start"))
  {
    if (len == 4) // True
    {
      // maybe to something
    }
  }
  else if (strstr(topic, "/espmeter/reset"))
  {
    if (len == 4) // True
    {
      // maybe to something
      const char *topic = "/espmeter/reset/status";
      const char *msg = "False";
      mqttClient.publish(topic, msg);
      mqttClient.loop();
      delay(200);

      // reboot
      ESP.restart();
    }
  }
  // and of course, free it
  delete[] p;
}

bool mqttConnect()
{
  mqttClient.setServer(credentials[cred][2], mqtt_port);
  mqttClient.setCallback(mqttCallback);

  // connect client to retainable last will message
  String s=String(node_id) + "/0/online";
  return mqttClient.connect(ESP_NAME, mqtt_user, mqtt_pass, s.c_str(), 0, true, "False");
}

void mqttMyData(const char* debug_str)
{
    String s=String(node_id) + "/0/sensor/mydata";
    mqttClient.publish(s.c_str(), debug_str, true);
}

void mqttMyDataJson(const char* debug_str)
{
    String s=String(node_id) + "/0/sensor/mydatajson";
    mqttClient.publish(s.c_str(), debug_str, true);
}

void sendHADiscoveryConfig() //function that creates 4 sensors for autodiscovery in Home Assistant
{
  #include "hadiscovery.h"
  mqttClient.setBufferSize(10000); //increase mqttclient buffersize to publish long json payloads
  //Water Temp discovery sensor
  String s1=String(discovery_prefix) + "/sensor/" + String(node_id) + "_" + String(water_temp_config_topic) + "/config";
  mqttClient.publish(s1.c_str(), water_temp_json_config.c_str(), true);
   //Room Temp discovery sensor
  String s2=String(discovery_prefix) + "/sensor/" + String(node_id) + "_" + String(room_temp_config_topic) + "/config";
  mqttClient.publish(s2.c_str(), room_temp_json_config.c_str(), true);
   //Water Usage discovery sensor
  String s3=String(discovery_prefix) + "/sensor/" + String(node_id) + "_" + String(water_usage_config_topic) + "/config";
  mqttClient.publish(s3.c_str(), water_usage_json_config.c_str(), true);
   //Water Meter Month Start Value discovery sensor
  String s4=String(discovery_prefix) + "/sensor/" + String(node_id) + "_" + String(water_usage_month_start_config_topic) + "/config";
  mqttClient.publish(s4.c_str(), water_usage_month_start_json_config.c_str(), true);
}

void mqttSubscribe()
{
  String s;
  // publish online status
  s = String(node_id) + "/0/online";
  mqttClient.publish(s.c_str(), "True", true);
  Serial.print("MQTT-SEND: ");
  Serial.print(s);
  Serial.println(" True");
  
  // publish ip address
  s=String(node_id) + "/0/ipaddr";
  IPAddress MyIP = WiFi.localIP();
  snprintf(MyIp, 16, "%d.%d.%d.%d", MyIP[0], MyIP[1], MyIP[2], MyIP[3]);
  mqttClient.publish(s.c_str(), MyIp, true);
  Serial.print("MQTT-SEND: ");
  Serial.print(s);
  Serial.print(" ");
  Serial.println(MyIp);

  //create HA discovery sensors if enabled in mqttsensorconfig
  if (enable_HA_discovery_sensors == true) {
    sendHADiscoveryConfig();
    Serial.println("HA Discovery config sent");
  }

  // if smarthome.py restarts -> publish init values
  s = "/smarthomeNG/start";
  mqttClient.subscribe(s.c_str());

  // if True; meter data are published every 5 seconds
  // if False: meter data are published once a minute
  s = String(node_id) + "/0/liveData";
  mqttClient.subscribe(s.c_str());

  // if True -> perform an reset
  s = "espmeter/reset";
  mqttClient.subscribe(s.c_str());
}

void setupOTA()
{
    // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(ESP_NAME);

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
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

// receive encrypted packets -> send it via MQTT to decrypter
void waterMeterLoop()
{
  if (waterMeter.isFrameAvailable())
  {
    // publish meter info via MQTT

  }
}

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);

    Serial.begin(115200);

    waterMeter.begin();
    Serial.println("Setup done...");
}

enum ControlStateType
  { StateInit
  , StateNotConnected
  , StateWifiConnect
  , StateMqttConnect
  , StateConnected
  , StateOperating
  };
ControlStateType ControlState = StateInit;

void loop()
{
  switch (ControlState)
  {
    case StateInit:
      //Serial.println("StateInit:");
      WiFi.mode(WIFI_STA);

      ControlState = StateNotConnected;
      break;

    case StateNotConnected:
      //Serial.println("StateNotConnected:");

      ControlState = StateWifiConnect;
      break;
      
    case StateWifiConnect:
      //Serial.println("StateWifiConnect:");
      // station mode
      ConnectWifi();

      delay(500);
      
      if (WiFi.status() == WL_CONNECTED)
      {
        Serial.println("");
        Serial.print("Connected to ");
        Serial.println(credentials[cred][0]); // FIXME
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());

        setupOTA();
        
        ControlState = StateMqttConnect;
      }
      else
      {
        Serial.println("");
        Serial.println("Connection failed.");

        // try again
        ControlState = StateNotConnected;

        // reboot 
        ESP.restart();
      }
      break;

    case StateMqttConnect:
      Serial.println("StateMqttConnect:");
      digitalWrite(LED_BUILTIN, HIGH); // off

      if (WiFi.status() != WL_CONNECTED)
      {
        ControlState = StateNotConnected;
        break; // exit (hopefully) switch statement
      }
      
      Serial.print("try to connect to MQTT server ");
      Serial.println(credentials[cred][2]); // FIXME

      if (mqttConnect())
      {
        ControlState = StateConnected;
      }
      else
      {
        Serial.println("MQTT connect failed");

        delay(1000);
        // try again
      }
      ArduinoOTA.handle();
      
      break;

    case StateConnected:
      Serial.println("StateConnected:");

      if (!mqttClient.connected())
      {
        ControlState = StateMqttConnect;
        delay(1000);
      }
      else
      {
        // subscribe to given topics
        mqttSubscribe();
        ControlState = StateOperating;
        digitalWrite(LED_BUILTIN, LOW); // on
        Serial.println("StateOperating:");
        //mqttDebug("up and running");
      }
      ArduinoOTA.handle();
      
      break;
    
    case StateOperating:
      //Serial.println("StateOperating:");

      if (WiFi.status() != WL_CONNECTED)
      {
        ControlState = StateWifiConnect;
        break; // exit (hopefully switch statement)
      }

      if (!mqttClient.connected())
      {
        Serial.println("not connected to MQTT server");
        ControlState = StateMqttConnect;
      }

      // here we go
      waterMeterLoop();

      mqttClient.loop();

      ArduinoOTA.handle();

      break;

    default:
      Serial.println("Error: invalid ControlState");  
  }
}