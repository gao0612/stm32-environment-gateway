#ifndef MOCK_SNAPSHOT_H
#define MOCK_SNAPSHOT_H
#include <stdint.h>
typedef struct {struct {int16_t temperature_x10;uint16_t humidity_x10;} data;struct {uint32_t state;} health;uint32_t publish_tick, sequence;} GatewaySnapshot_t;
#endif
