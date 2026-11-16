#ifndef RETRY_DELAY_H
#define RETRY_DELAY_H
#include <stdint.h>
uint32_t Retry_Next(uint32_t previous);
int Buffer_NeedSave(unsigned count, int online, uint32_t age_ms);
#endif
