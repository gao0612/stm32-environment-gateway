#include "test_config.h"
#include "app_watchdog.h"
#include "task_health.h"

#include "FreeRTOS.h"

#include "task.h"

#include "cmsis_os.h"

#define HEALTH_REQUIRED_MASK ((1UL << APP_HEALTH_COUNT) - 1UL)

typedef struct
{
    uint32_t last_tick;

    uint32_t report_count;

    uint8_t seen;

} HealthState_t;

typedef struct
{
    uint32_t check_count;

    uint32_t report_count[APP_HEALTH_COUNT];

    uint32_t age_ticks[APP_HEALTH_COUNT];

    uint32_t limit_ticks[APP_HEALTH_COUNT];

    uint32_t seen_mask;

    uint32_t missing_mask;

    uint32_t stale_mask;

    uint32_t fault_mask;

    uint8_t grace_active;

    uint8_t healthy;

} HealthDiag_t;

static volatile HealthState_t health_state[APP_HEALTH_COUNT];

static volatile HealthDiag_t health_diag;

static uint32_t limit_ticks[APP_HEALTH_COUNT];

static uint8_t health_initialized;

static uint8_t startup_complete;

static osThreadId_t healthTaskHandle;

/* 临时实验开关：屏蔽报告，不暂停业务任务 */
static volatile uint32_t health_test_suppress_mask = 0U;

static const osThreadAttr_t healthTask_attributes = {.name = "healthTask",

                                                     .stack_size = 256U * 4U,

                                                     .priority = (osPriority_t)osPriorityNormal};

void AppHealth_Init(uint32_t network_probe_period_ms)
{
    configASSERT(network_probe_period_ms >= 250U);

    configASSERT(network_probe_period_ms <= 60000U);

    limit_ticks[APP_HEALTH_UART] = pdMS_TO_TICKS(5000U);

    limit_ticks[APP_HEALTH_GATEWAY] = pdMS_TO_TICKS(2000U);

    limit_ticks[APP_HEALTH_DISPLAY] = pdMS_TO_TICKS(3000U);

    limit_ticks[APP_HEALTH_NETWORK] = pdMS_TO_TICKS(network_probe_period_ms + 5000U);

    for (uint32_t i = 0U; i < APP_HEALTH_COUNT; i++)
    {
        health_state[i].last_tick = 0U;

        health_state[i].report_count = 0U;

        health_state[i].seen = 0U;

        health_diag.report_count[i] = 0U;

        health_diag.age_ticks[i] = 0U;

        health_diag.limit_ticks[i] = limit_ticks[i];
    }

    health_diag.check_count = 0U;

    health_diag.seen_mask = 0U;

    health_diag.missing_mask = HEALTH_REQUIRED_MASK;

    health_diag.stale_mask = 0U;

    health_diag.fault_mask = 0U;

    health_diag.grace_active = 1U;

    health_diag.healthy = 0U;

    health_test_suppress_mask = 0U;

    startup_complete = 0U;

    health_initialized = 1U;
}

void AppHealth_Report(AppHealthTaskId_t id)
{
    if ((uint32_t)id >= APP_HEALTH_COUNT)
    {
        return;
    }

    configASSERT(health_initialized != 0U);

    taskENTER_CRITICAL();

    if ((APP_ENABLE_FAULT_TESTS == 0U) || (health_test_suppress_mask & (1UL << (uint32_t)id)) == 0U)
    {
        health_state[id].last_tick = osKernelGetTickCount();

        health_state[id].report_count++;

        health_state[id].seen = 1U;
    }

    taskEXIT_CRITICAL();
}

static void Health_Check(uint32_t start_tick)
{
    HealthState_t snapshot[APP_HEALTH_COUNT];

    uint32_t now;

    uint32_t seen_mask = 0U;

    uint32_t stale_mask = 0U;

    taskENTER_CRITICAL();

    now = osKernelGetTickCount();

    for (uint32_t i = 0U; i < APP_HEALTH_COUNT; i++)
    {
        snapshot[i].last_tick = health_state[i].last_tick;

        snapshot[i].report_count = health_state[i].report_count;

        snapshot[i].seen = health_state[i].seen;
    }

    taskEXIT_CRITICAL();

    for (uint32_t i = 0U; i < APP_HEALTH_COUNT; i++)
    {
        uint32_t age = 0U;

        health_diag.report_count[i] = snapshot[i].report_count;

        if (snapshot[i].seen != 0U)
        {
            seen_mask |= 1UL << i;

            age = (uint32_t)(now - snapshot[i].last_tick);

            if (age > limit_ticks[i])
            {
                stale_mask |= 1UL << i;
            }
        }

        health_diag.age_ticks[i] = age;
    }

    if ((startup_complete == 0U) && ((uint32_t)(now - start_tick) >= pdMS_TO_TICKS(10000U)))
    {
        startup_complete = 1U;
    }

    health_diag.seen_mask = seen_mask;

    health_diag.missing_mask = HEALTH_REQUIRED_MASK & ~seen_mask;

    health_diag.stale_mask = stale_mask;

    health_diag.grace_active = (uint8_t)(startup_complete == 0U);

    health_diag.fault_mask =
        (startup_complete != 0U) ? (health_diag.missing_mask | stale_mask) : 0U;

    health_diag.healthy = (uint8_t)((startup_complete != 0U) && (health_diag.fault_mask == 0U));

    health_diag.check_count++;
}

static void StartHealthTask(void *argument)
{
    uint32_t start_tick = osKernelGetTickCount();

    (void)argument;

    uint32_t fault_latched = 0U;
    for (;;)
    {
        Health_Check(start_tick);
        fault_latched |= health_diag.fault_mask;
        /* Feed only while all monitored tasks are making progress.
           An unplugged sensor/network is handled locally and still reports. */
        if (fault_latched == 0U)
            AppWatchdog_Feed();

        osDelay(pdMS_TO_TICKS(250U));
    }
}

void AppHealthTask_Create(void)
{
    configASSERT(health_initialized != 0U);

    healthTaskHandle = osThreadNew(StartHealthTask, NULL, &healthTask_attributes);

    configASSERT(healthTaskHandle != NULL);
}