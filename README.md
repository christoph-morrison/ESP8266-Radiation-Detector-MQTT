# MQTT connectivity for CAJOE-style radiation detector boards

This repository contains an firmware for ESP8266 MCU with the capability to interact
with a CAJOE-style radiation detector board. It uses the `VIN` signal from the detection board
for counting pulses, converting them to µSievert/h and pushing the value to a MQTT broker.

It's delivered as a [Platform.io](https://platformio.org/) project. The WifiManager library is used for
Wi-Fi configuration. Also, ArduinoOTA is enabled, firmware updates are possible even with
an assembled device.

## Prerequisites

### BOM

1. CAJOE-style detector board, compatible boards are supported, as long as they emit an impulse
   for every detected ion. Supported tube types are J305, SBM-20 and STS-5.
2. An ESP8266 µC board with a 5VDC voltage regulator (e.g. a Wemos D1 Mini)
3. Connection cables (Dupont Cables)
4. I strongly recommend a junction box or similar for protection against the high tube voltage
   (400V) on the detector board.

### Software

Dependencies are noted in the [platformio.ini](platformio.ini) file and contain:

* PubSubClient
* ArduinoJson
* WiFiManager

### Tools

If your detector board and the ESP8266 board are already assembled, no tools are needed.

## Get started

1. Connect the radiation detector board to the ESP8266 board
2. Set some configuration
    * If you don't want to use the `D2` pin for input, configure `RAD_DATA_RECEIVER_PIN` to your chosen pin.
    * You must select the conversion factor `RAD_TUBE_FACTOR` depending on the detector tube
      used by the detection board. J305 is the default, but SBM-20 and STS-5 are noted down for your convenience.
    * The default logging period for counts per minute is 60000 microseconds, also a minute.
      If you want change that, modify `LOG_PERIOD`. Very small values (i.e. 15 seconds / 15000 microseconds) will
      result in not very reliable values, because especially background radiation is not evenly distributed. High values
      will result in better values, but are boring.
    * Even the WifiManager takes control of the Wi-Fi and MQTT connection settings (broker address, port, etc.),
      you must configure, for now, the MQTT topic prefix `mqttTopicPrefix`.
3. Configure your `upload_port` and `monitor_port` in [platformio.ini](platformio.ini).
4. Build and transfer
5. After rebooting the device, the ESP8266 will start in station mode, so look out for
   a new SSID like `ESP-esp8266-radiation-monitor-XXXXXX`, connect to it
   and visit [the wifi configuration page](http://192.168.4.1/).
6. Configure your Wi-Fi network, Wi-Fi credentials and MQTT broker address, reboot.
7. Enjoy.

## Documentation

### Configuration variables

| Name                    | Purpose                                                                                        | Default                   |
|-------------------------|------------------------------------------------------------------------------------------------|---------------------------|
| `RAD_DATA_RECEIVER_PIN` | ESP8266 receiver pin                                                                           | `D2`                      |
| `RAD_TUBE_FACTOR`       | Conversion factor from cp/m to µS/h, depending on the used tube                                | `0.00812037037037` (J305) |
| `LOG_PERIOD`            | Logging time in µSeconds. Impulses will be counted for `LOG_PERIOD` and then converted to cp/m | 60000                     |
| `mqttTopicPrefix`       | Prefix for the MQTT topic. Data will be send to `$mqttTopicPrefix/$deviceId`                   | `hab/devices/sensors/environment/radiation` |

### MQTT data structure

```text
$mqttTopicPrefix/$deviceId/
  +--- keep-alive = ping will be perodiocally pushed to show, that the system is still running and connected
  +--- connection = LWT from MQTT, will be online or offline
  +--- state = JSON formatted payload, i.e.
        {
           "radiation":{
              "microSievert_hour":0.121805549,
              "counts_in_period":15,
              "log_period_seconds":60,
              "counts_per_minute":15
           },
           "wifi":{
              "ssid":"YourSSID",
              "ip":"DeviceIP",
              "rssi": RSSI value in dB
           }
        }
```

## License

```text
ESP8266 based gateway for CAJOE-style Radiation detector boards to MQTT
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
```

See also the [LICENSE](LICENSE).