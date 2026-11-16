#include "retry_delay.h"
uint32_t Retry_Next(uint32_t previous)
{
    if (previous < 1000U)
        return 1000U;
    if (previous >= 30000U)
        return 60000U;
    return previous * 2U;
}
int Buffer_NeedSave(unsigned count, int online, uint32_t age_ms)
{
    return count && (!online) && (count >= 4U || age_ms >= 2000U);
}
