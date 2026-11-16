#include <assert.h>
#define configASSERT assert
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)

#include <stddef.h>
size_t xPortGetMinimumEverFreeHeapSize(void);
