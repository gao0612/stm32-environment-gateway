#include "mqtt_packet.h"
#include <string.h>
static int String(uint8_t *out, size_t cap, size_t *index, const char *s)
{
    size_t n = strlen(s);
    if (n > 65535U || *index > cap || n + 2U > cap - *index)
        return -1;
    out[(*index)++] = (uint8_t)(n >> 8);
    out[(*index)++] = (uint8_t)n;
    memcpy(out + *index, s, n);
    *index += n;
    return 0;
}
static size_t Finish(uint8_t *out, size_t cap, size_t end, uint8_t type)
{
    size_t remain = end - 5, index = 1;
    uint32_t n = (uint32_t)remain;
    do
    {
        uint8_t b = (uint8_t)(n % 128U);
        n /= 128U;
        if (n)
            b |= 128;
        out[index++] = b;
    } while (n);
    if (index + remain > cap)
        return 0;
    memmove(out + index, out + 5, remain);
    out[0] = type;
    return index + remain;
}
size_t Mqtt_Connect(uint8_t *out, size_t cap, const char *id, const char *user,
                    const char *password)
{
    if (!out || !id || !user || !password || cap < 15)
        return 0;
    size_t index = 5;
    const uint8_t fixed[] = {0, 4, 'M', 'Q', 'T', 'T', 4, 2, 0, 30};
    memcpy(out + index, fixed, sizeof(fixed));
    index += sizeof(fixed);
    if (*password && !*user)
        return 0;
    if (*user)
        out[12] |= 0x80;
    if (*password)
        out[12] |= 0x40;
    if (String(out, cap, &index, id) || (*user && String(out, cap, &index, user)) ||
        (*password && String(out, cap, &index, password)))
        return 0;
    return Finish(out, cap, index, 0x10);
}
size_t Mqtt_Publish(uint8_t *out, size_t cap, const char *topic, const char *text, uint16_t id)
{
    if (!out || !topic || !*topic || !text || !id || cap < 5)
        return 0;
    size_t index = 5, n = strlen(text);
    if (String(out, cap, &index, topic) || index + 2U > cap || n > cap - index - 2U)
        return 0;
    out[index++] = (uint8_t)(id >> 8);
    out[index++] = (uint8_t)id;
    memcpy(out + index, text, n);
    index += n;
    return Finish(out, cap, index, 0x32); /* QoS1：收到PUBACK后才删除缓存。 */
}
size_t Mqtt_Subscribe(uint8_t *out, size_t cap, const char *topic, uint16_t id)
{
    if (!out || !topic || !*topic || !id || cap < 7)
        return 0;
    size_t index = 5;
    out[index++] = (uint8_t)(id >> 8);
    out[index++] = (uint8_t)id;
    if (String(out, cap, &index, topic) || index >= cap)
        return 0;
    out[index++] = 0;
    return Finish(out, cap, index, 0x82);
}
int Mqtt_FrameSize(const uint8_t *data, size_t size, size_t cap, size_t *frame, size_t *header)
{
    if (!data || !frame || !header)
        return -1;
    uint32_t length = 0, multiplier = 1;
    for (size_t i = 1; i <= 4; i++)
    {
        if (i >= size)
            return 0;
        length += (data[i] & 127U) * multiplier;
        if (!(data[i] & 128U))
        {
            *header = i + 1;
            *frame = *header + length;
            if (*frame > cap)
                return -1;
            return size >= *frame ? 1 : 0;
        }
        multiplier *= 128U;
    }
    return -1;
}
