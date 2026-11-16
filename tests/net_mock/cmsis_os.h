#ifndef NET_MOCK_OS
#define NET_MOCK_OS
#include <stdint.h>
#include <stddef.h>
typedef void *osMessageQueueId_t;
typedef int osStatus_t;
#define osOK 0
#define osErrorParameter -1
osMessageQueueId_t osMessageQueueNew(uint32_t n,uint32_t size,void *attr);
int osMessageQueuePut(void *q,const void *data,uint8_t priority,uint32_t timeout);
void osDelay(uint32_t n);
#endif
