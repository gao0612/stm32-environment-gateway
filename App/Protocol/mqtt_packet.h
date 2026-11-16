#ifndef MQTT_PACKET_H
#define MQTT_PACKET_H
#include <stdint.h>
#include <stddef.h>
/* 返回完整报文长度，空间不足返回0。 */
size_t Mqtt_Connect(uint8_t *out, size_t cap, const char *id, const char *user,
                    const char *password);
size_t Mqtt_Publish(uint8_t *out, size_t cap, const char *topic, const char *text,
                    uint16_t packet_id);
size_t Mqtt_Subscribe(uint8_t *out, size_t cap, const char *topic, uint16_t packet_id);
/* 返回1：完整；0：继续接收；-1：非法/超过容量。 */
int Mqtt_FrameSize(const uint8_t *data, size_t size, size_t cap, size_t *frame, size_t *header);
#endif
