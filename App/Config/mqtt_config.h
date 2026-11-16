#ifndef MQTT_CONFIG_H
#define MQTT_CONFIG_H
#if defined(__has_include)
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#endif
/* 改为你的 MQTT 服务器 IPv4 地址；本例不使用 DNS/TLS。 */
#ifndef MQTT_SERVER_IP
#define MQTT_SERVER_IP {192, 168, 50, 1}
#endif
#ifndef MQTT_SERVER_PORT
#define MQTT_SERVER_PORT 1883U
#endif
#ifndef MQTT_CLIENT_ID
#define MQTT_CLIENT_ID "f407-gateway-01"
#endif
#ifndef MQTT_USER
#define MQTT_USER ""
#endif
#ifndef MQTT_PASSWORD
#define MQTT_PASSWORD ""
#endif
#ifndef MQTT_TOPIC
#define MQTT_TOPIC "gateway/sensor"
#endif
#ifndef MQTT_COMMAND_TOPIC
#define MQTT_COMMAND_TOPIC "gateway/cmd"
#endif
#ifndef SCREEN_SLEEP_MS
#define SCREEN_SLEEP_MS 30000U
#endif
#endif
