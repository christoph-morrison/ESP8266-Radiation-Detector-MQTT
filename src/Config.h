#pragma once

#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>

namespace Config {
    char mqtt_server[80] = "example.com";
    char mqtt_topic[256] = "esp/radiation-detector";
    char mqtt_port[5]    = "1883";

    char username[100] = "";
    char password[100] = "";

    void save() {
        DynamicJsonDocument json(512);
        json["mqtt_server"]  = mqtt_server;
        json["mqtt_port"]    = mqtt_port;
        json["mqtt_topic"]   = mqtt_topic;
        json["username"]     = username;
        json["password"]     = password;

        File configFile = LittleFS.open("/config.json", "w");

        if (!configFile) {
            return;
        }

        serializeJson(json, configFile);
        configFile.close();
    }

    void load() {
        if (LittleFS.begin()) {
            if (LittleFS.exists("/config.json")) {
                File configFile = LittleFS.open("/config.json", "r");

                if (configFile) {
                    const size_t size = configFile.size();
                    std::unique_ptr<char[]> buf(new char[size]);

                    configFile.readBytes(buf.get(), size);
                    DynamicJsonDocument json(512);

                    if (DeserializationError::Ok == deserializeJson(json, buf.get())) {
                        strcpy(mqtt_server, json["mqtt_server"]);
                        strcpy(mqtt_server, json["mqtt_port"]);
                        strcpy(mqtt_server, json["mqtt_topic"]);
                        strcpy(username, json["username"]);
                        strcpy(password, json["password"]);
                    }
                }
            }
        }
    }
}