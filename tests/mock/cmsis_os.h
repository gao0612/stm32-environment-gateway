#ifndef MOCK_OS_H
#define MOCK_OS_H
#include <stdint.h>
typedef void *osMutexId_t;
#define osOK 0
osMutexId_t osMutexNew(void *a);
int osMutexAcquire(osMutexId_t m,uint32_t n);
int osMutexRelease(osMutexId_t m);
uint32_t osKernelGetTickCount(void);
void osDelay(uint32_t n);
#endif
