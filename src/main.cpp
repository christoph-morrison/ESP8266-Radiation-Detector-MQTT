/*
    ESP8266 based gateway for CANJOE-style Radiation detector boards to MQTT
    Copyright (C) 2022, Christoph 'knurd' Morrison <code@christoph-morrison.de>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <Config.h>

// ********************************************************************************************************************  User settings

//  Pin, VIN from the detector board ist connected to
const u_int RAD_DATA_RECEIVER_PIN = D2;

// Detector tube conversion factor, chose one:
const float RAD_TUBE_FACTOR = 0.00812037037037;        // J305  
// const float  RAD_TUBE_FACTOR = 0.0057;                  // SMB-20   
// const float  RAD_TUBE_FACTOR = 0.0060;                  // STS-5

// Logging period in milliseconds, recommended value 15000-60000.
const u_int LOG_PERIOD = 60000;     

// MQTT topic prefix
String mqttTopicPrefix = "hab/devices/sensors/environment/radiation";

// ********************************************************************************************************************  Device settings

// Firmware prefix name of the device
#define FIRMWARE_PREFIX "esp8266-radiation-monitor"

char identifier[30];
bool shouldSaveConfig   = false;
bool initDone           = false;

volatile unsigned long counts;      // variable for GM Tube events
volatile bool sig;                  // flag that at least one impulse occured interrupt
unsigned long cpm;                  // variable for CPM
unsigned int multiplier;            // variable for calculation CPM in this sketch
unsigned long previousMillis;       // variable for time measurement

// ********************************************************************************************************************  MQTT Settings

// Via MQTT transferred value if online / offline
#define AVAILABILITY_ONLINE "online"
#define AVAILABILITY_OFFLINE "offline"

// Reconnect interval 
const uint16_t mqttConnectionInterval = 60000 * 1;      // 1 minute = 60 seconds = 60000 milliseconds

// Keep alive interval
const uint16_t keepAlivePublishInterval = 60000 * 1;

// Publish network infos every networkPublishInterval microseconds
const uint32_t networkPublishInterval = 60000 * 30;

// MQTT predefined values
uint8_t  mqttRetryCounter = 0;
uint32_t lastMqttConnectionAttempt = 0;
uint32_t networkPreviousMillis = 0;
uint32_t keepAlivePreviousMillis = 0;
char MQTT_TOPIC_AVAILABILITY[128];
char MQTT_TOPIC_STATE[128];
char MQTT_TOPIC_KEEP_ALIVE[128];
char MQTT_TOPIC_COMMAND[128];

// ********************************************************************************************************************  Wifi

WiFiManager  wifiManager;
WiFiClient   wifiClient;
PubSubClient mqttClient;

WiFiManagerParameter custom_mqtt_server("server", "mqtt server", Config::mqtt_server, sizeof(Config::mqtt_server));
WiFiManagerParameter custom_mqtt_port("port", "MQTT server port", Config::mqtt_port, sizeof(Config::mqtt_port));
WiFiManagerParameter custom_mqtt_topic("topic", "MQTT base topic", Config::mqtt_topic, sizeof(Config::mqtt_topic));
WiFiManagerParameter custom_mqtt_user("user", "MQTT username", Config::username, sizeof(Config::username));
WiFiManagerParameter custom_mqtt_pass("pass", "MQTT password", Config::password, sizeof(Config::password));

// ********************************************************************************************************************  Functions

void saveConfigCallback() {
    shouldSaveConfig = true;
}

void setupOTA() {
    ArduinoOTA.onStart([]() { Serial.println("Start"); });
    ArduinoOTA.onEnd([]() { Serial.println("\nEnd"); });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) {
            Serial.println("Auth Failed");
        } else if (error == OTA_BEGIN_ERROR) {
            Serial.println("Begin Failed");
        } else if (error == OTA_CONNECT_ERROR) {
            Serial.println("Connect Failed");
        } else if (error == OTA_RECEIVE_ERROR) {
            Serial.println("Receive Failed");
        } else if (error == OTA_END_ERROR) {
            Serial.println("End Failed");
        }
    });

    ArduinoOTA.setHostname(identifier);

    // This is less of a security measure and more a accidential flash prevention
    ArduinoOTA.setPassword(identifier);
    ArduinoOTA.begin();
}

void setupWifi() {
    wifiManager.setDebugOutput(false);
    wifiManager.setSaveConfigCallback(saveConfigCallback);

    wifiManager.addParameter(&custom_mqtt_server);
    wifiManager.addParameter(&custom_mqtt_port);
    wifiManager.addParameter(&custom_mqtt_user);
    wifiManager.addParameter(&custom_mqtt_pass);
    wifiManager.addParameter(&custom_mqtt_topic);

    WiFi.hostname(identifier);
    wifiManager.autoConnect(identifier);
    mqttClient.setClient(wifiClient);

    strcpy(Config::mqtt_server, custom_mqtt_server.getValue());
    strcpy(Config::username, custom_mqtt_user.getValue());
    strcpy(Config::password, custom_mqtt_pass.getValue());
    strcpy(Config::mqtt_port, custom_mqtt_port.getValue());
    strcpy(Config::mqtt_topic, custom_mqtt_topic.getValue());

    if (shouldSaveConfig) {
        Config::save();
    } else {
        // For some reason, the read values get overwritten in this function
        // To combat this, we just reload the config
        // This is most likely a logic error which could be fixed otherwise
        Config::load();
    }
}

void resetWifiSettingsAndReboot() {
    wifiManager.resetSettings();
    delay(3000);
    ESP.restart();
}

void mqttReconnect() {
    for (uint8_t attempt = 0; attempt < 3; ++attempt) {
        if (mqttClient.connect(identifier, Config::username, Config::password, MQTT_TOPIC_AVAILABILITY, 1, true, AVAILABILITY_OFFLINE)) {
            mqttClient.publish(MQTT_TOPIC_AVAILABILITY, AVAILABILITY_ONLINE, true);
            
            // Make sure to subscribe after polling the status so that we never execute commands with the default data
            mqttClient.subscribe(MQTT_TOPIC_COMMAND);
            break;
        }
        delay(5000);
    }
}

bool isMqttConnected() {
    return mqttClient.connected();
}

void publishKeepAlive() {
    char keepAliveMsg[5] = "ping";
    Serial.printf("Send keep alive message: %s\n", keepAliveMsg);

    mqttClient.publish(&MQTT_TOPIC_KEEP_ALIVE[0], keepAliveMsg, true);
}

void publishNetworkState() {
    DynamicJsonDocument wifiJson(192);
    DynamicJsonDocument stateJson(1024);
    char payload[256];

    wifiJson["ssid"]    = WiFi.SSID();
    wifiJson["ip"]      = WiFi.localIP().toString();
    wifiJson["rssi"]    = WiFi.RSSI();

    stateJson["wifi"]   = wifiJson.as<JsonObject>();

    serializeJson(stateJson, payload);
    mqttClient.publish(&MQTT_TOPIC_STATE[0], &payload[0], true);
}

void mqttCallback(char* topic, uint8_t* payload, unsigned int length) { 

    Serial.print("Received message [");
    Serial.print(topic);
    Serial.print("] ");

    char msg[length+1];
    
    for (u_int i = 0; i < length; i++) {
        Serial.print((char)payload[i]);
        msg[i] = (char)payload[i];
    }
    
    Serial.println();
 
    msg[length] = '\0';
    Serial.println(msg);
}

void IRAM_ATTR count_geiger_impulse(){
  counts++;
  sig = true;
  Serial.println("Tick!");
}

void  setupGeiger() {
    sig = false;
    counts = 0;
    cpm = 0;

    // calculating multiplier: if LOG_PERIOD is not a minute, calculate a multiplier for generating cpm
    // i.e. LOG_PERIOD is 30 seconds, multiply the counted impulses with 2 to get a minute value
    multiplier = 60000 / LOG_PERIOD;     
    pinMode(RAD_DATA_RECEIVER_PIN, INPUT); 
    attachInterrupt(digitalPinToInterrupt(RAD_DATA_RECEIVER_PIN), count_geiger_impulse, RISING);
}

void publishGeigerData() {
    unsigned long currentMillis = millis();
    
    if(currentMillis - previousMillis > LOG_PERIOD){
        previousMillis = currentMillis;
        cpm = counts * multiplier;
        
        float microSievert = RAD_TUBE_FACTOR * cpm;

        Serial.printf("%lu cp/m, %.2f µ/Sh\n", cpm, microSievert);

        DynamicJsonDocument wifiJson(192);
        DynamicJsonDocument valueJson(192);
        DynamicJsonDocument stateJson(1024);
        char payload[256];

        wifiJson["ssid"]    = WiFi.SSID();
        wifiJson["ip"]      = WiFi.localIP().toString();
        wifiJson["rssi"]    = WiFi.RSSI();

        valueJson["microSievert_hour"]      = microSievert;
        valueJson["counts_in_period"]       = counts;
        valueJson["log_period_seconds"]     = LOG_PERIOD / 1000;
        valueJson["counts_per_minute"]      = cpm;

        stateJson["radiation"]  = valueJson.as<JsonObject>();
        stateJson["wifi"]       = wifiJson.as<JsonObject>();

        serializeJson(stateJson, payload);
        mqttClient.publish(&MQTT_TOPIC_STATE[0], &payload[0], true);

        counts = 0;
    }
}

void setup() {
    Serial.begin(115200);

    Serial.println("\n");
    Serial.printf("Hello from %s-%X\n", FIRMWARE_PREFIX, ESP.getChipId());
    Serial.printf("Core Version: %s\n", ESP.getCoreVersion().c_str());
    Serial.printf("Boot Version: %u\n", ESP.getBootVersion());
    Serial.printf("Boot Mode: %u\n", ESP.getBootMode());
    Serial.printf("CPU Frequency: %u MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("Reset reason: %s\n", ESP.getResetReason().c_str());

    delay(3000);

    snprintf(identifier, sizeof(identifier), "%s-%X", FIRMWARE_PREFIX, ESP.getChipId());
    snprintf(MQTT_TOPIC_AVAILABILITY, 127,   "%s/%X/connection",  Config::mqtt_topic, ESP.getChipId());
    snprintf(MQTT_TOPIC_STATE, 127,          "%s/%X/state",       mqttTopicPrefix.c_str(), ESP.getChipId());
    snprintf(MQTT_TOPIC_COMMAND, 127,        "%s/%X/command",     mqttTopicPrefix.c_str(), ESP.getChipId());
    snprintf(MQTT_TOPIC_KEEP_ALIVE, 127,     "%s/%X/keep-alive",  mqttTopicPrefix.c_str(), ESP.getChipId());

    WiFi.hostname(identifier);

    Config::load();

    setupWifi();
    setupOTA();

    mqttClient.setServer(Config::mqtt_server, atoi(Config::mqtt_port));
    mqttClient.setKeepAlive(10);
    mqttClient.setBufferSize(2048);
    mqttClient.setCallback(mqttCallback);

    Serial.printf("Hostname: %s\n", identifier);
    Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.println("-- Current GPIO Configuration --");
    Serial.printf("Radation Data Receiver PIN: %d\n", RAD_DATA_RECEIVER_PIN);

    mqttReconnect();

    setupGeiger();
}

void loop() {
    ArduinoOTA.handle();
    mqttClient.loop();

    if (initDone == false) {
        Serial.print("Process initial config informations: ");
        
        publishNetworkState();
        Serial.print("network, ");
        
        publishKeepAlive();
        Serial.print("keep-alive, ");
        
        Serial.println("done");
        initDone = true;
    }
    const uint32_t currentMillis = millis();

    publishGeigerData();

    if (currentMillis - keepAlivePreviousMillis >= keepAlivePublishInterval) {
        keepAlivePreviousMillis = currentMillis;
        publishKeepAlive();
    }

    if (currentMillis - networkPreviousMillis >= networkPublishInterval) {
        networkPreviousMillis = currentMillis;
        publishNetworkState();
    }

    if (!mqttClient.connected() && currentMillis - lastMqttConnectionAttempt >= mqttConnectionInterval) {
        lastMqttConnectionAttempt = currentMillis;
        printf("Reconnect mqtt\n");
        mqttReconnect();
    }
}