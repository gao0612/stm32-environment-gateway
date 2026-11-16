#include "test_config.h"
#include "network.h"
/* USART3任务及所属收发回调：循环DMA、RingBuffer、RS485方向控制。 */
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "app_tasks.h"
#include "usart.h"
#include "modbus_rtu.h"
#include "kth2r.h"
#include "device_health.h"
#include "app_snapshot.h"
#include "clock_us.h"
#include "task_health.h"

#define UART3_DMA_RX_BUFFER_SIZE 64U
#define UART3_RING_BUFFER_SIZE 256U

/*  bit0：有接收数据
    bit1：发送完成
    bit2：发生接收错误 */

#define THREAD_FLAG_UART3_RX (1UL << 0U)
#define THREAD_FLAG_UART3_TX_DONE (1UL << 1U)
#define THREAD_FLAG_UART3_ERROR (1UL << 2U)

/* 当前采集结果，以及少量有实际诊断价值的状态 */
typedef struct
{
    KTH2R_Data_t data;

    DeviceHealth_t health; /* 通信状态及成功时间 */
    uint8_t data_fresh;    /* 本次检查时数据是否新鲜 */

    uint32_t success_count;
    uint32_t timeout_count;
    uint32_t error_count;

    KTH2R_ParseStatus_t parse_status;
    uint8_t exception_code;

    uint32_t publish_error_count;
} KTH2R_Monitor_t;

static volatile KTH2R_Monitor_t kth2r_monitor;

static uint8_t uart3_dma_rx_buffer[UART3_DMA_RX_BUFFER_SIZE];

static uint8_t uart3_ring_buffer[UART3_RING_BUFFER_SIZE];

static volatile uint16_t uart3_ring_head = 0U;

static volatile uint16_t uart3_ring_tail = 0U;

static uint16_t uart3_dma_old_position = 0U;

static volatile uint32_t uart3_rx_overflow_count = 0U;

static osThreadId_t uartRxTaskHandle;

static const osThreadAttr_t uartRxTask_attributes = {.name = "uartRxTask",

                                                     .stack_size = 256U * 4U,

                                                     .priority =
                                                         (osPriority_t)osPriorityAboveNormal};

static volatile uint32_t uart3_dma_tc_count = 0U;

static volatile uint32_t rs485_tx_ok_count = 0U;

static volatile uint32_t rs485_tx_error_count = 0U;

typedef struct
{
    uint32_t last_error_code;    // 最近一次故障错误码（HAL_UART_ErrorCallback里拿到的error）
    uint32_t error_event_count;  // UART硬件错误事件总次数（奇偶错、帧错、溢出、噪声等）
    uint32_t restart_ok_count;   // 尝试重启UART+DMA，重启成功次数
    uint32_t restart_fail_count; // 尝试重启UART+DMA，重启失败次数
} Uart3RecoveryMonitor_t;

static volatile Uart3RecoveryMonitor_t uart3_recovery;

/* 初始为1：第一轮由恢复函数统一启动RX DMA */
static volatile uint8_t uart3_rx_recovery_requested = 1U;

/* 调试测试使用：置1后，在任务中主动停止一次接收 */
static volatile uint8_t uart3_test_stop_rx = 0U;

/* 单写入者ISR、单读取者任务；缓冲区容量256，可存255字节。 */
static void Uart3_RingPushFromISR(uint8_t data)
{
    uint16_t next_head;

    next_head = uart3_ring_head + 1U;

    if (next_head >= UART3_RING_BUFFER_SIZE)
    {
        next_head = 0U;
    }

    if (next_head == uart3_ring_tail)
    {
        uart3_rx_overflow_count++;
    }
    else
    {
        uart3_ring_buffer[uart3_ring_head] = data;
        uart3_ring_head = next_head;
    }
}

static uint8_t Uart3_RingPop(uint8_t *data)
{
    uint16_t next_tail;

    if (uart3_ring_tail == uart3_ring_head)
    {
        return 0U;
    }

    *data = uart3_ring_buffer[uart3_ring_tail];

    next_tail = uart3_ring_tail + 1U;

    if (next_tail >= UART3_RING_BUFFER_SIZE)
    {
        next_tail = 0U;
    }

    uart3_ring_tail = next_tail;

    return 1U;
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    uint16_t index;
    uint16_t new_position;

    /* 工程以后可能有多个UART，必须判断事件来自USART3 */
    if ((huart->Instance != USART3) || (uart3_rx_recovery_requested != 0U))
    {
        return;
    }

    if (HAL_UARTEx_GetRxEventType(huart) == HAL_UART_RXEVENT_TC)
    {
        uart3_dma_tc_count++;
    }

    new_position = Size;

    if (new_position > UART3_DMA_RX_BUFFER_SIZE)
    {
        return;
    }

    if (new_position > uart3_dma_old_position)
    {

        for (index = uart3_dma_old_position; index < new_position; index++)
        {
            Uart3_RingPushFromISR(uart3_dma_rx_buffer[index]);
        }
    }
    else if (new_position < uart3_dma_old_position)
    {

        for (index = uart3_dma_old_position; index < UART3_DMA_RX_BUFFER_SIZE; index++)
        {
            Uart3_RingPushFromISR(uart3_dma_rx_buffer[index]);
        }

        for (index = 0U; index < new_position; index++)
        {
            Uart3_RingPushFromISR(uart3_dma_rx_buffer[index]);
        }
    }
    else
    {
        /* 位置没有变化，没有新数据 */
    }

    uart3_dma_old_position = new_position;

    (void)osThreadFlagsSet(uartRxTaskHandle, THREAD_FLAG_UART3_RX);
}

/* 只允许UART任务调用：先清旧完成位，启动IT发送，阻塞等待，失败则中止TX。 */
static HAL_StatusTypeDef RS485_Send(const uint8_t *data, uint16_t length, uint32_t timeout_ticks)
{
    HAL_StatusTypeDef status;
    uint32_t flags;

    /* 这个函数需要阻塞等待，不能从中断调用 */
    if (__get_IPSR() != 0U)
    {
        return HAL_ERROR;
    }

    /* USART3由UART任务统一管理 */
    if ((osThreadGetId() != uartRxTaskHandle) || (data == NULL) || (length == 0U) ||
        (timeout_ticks == 0U))
    {
        return HAL_ERROR;
    }

    /*
     * 清除上次可能残留的TX完成位，只清bit1。
     *
     * 当前CMSIS封装的清标志过程包含读改写，
     * 使用短临界区避免与RX中断置位发生竞争。
     * 这里没有延时、等待或硬件传输。
     */
    taskENTER_CRITICAL();
    (void)osThreadFlagsClear(THREAD_FLAG_UART3_TX_DONE);
    taskEXIT_CRITICAL();

    /* EN高：切到发送 */
    HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_SET);

    /*
     * 这里只启动发送。
     * HAL_OK表示启动成功，不代表线路已经发完。
     */
    status = HAL_UART_Transmit_IT(&huart3, /* USART3 */
                                  data,    /* 待发送数据 */
                                  length); /* 字节数 */

    if (status != HAL_OK)
    {
        /* 启动失败也要释放总线 */
        HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET);

        rs485_tx_error_count++;
        return status;
    }

    /*
     * 等待bit1。
     * 等待期间任务阻塞，CPU可以运行其他就绪任务。
     */
    flags = osThreadFlagsWait(THREAD_FLAG_UART3_TX_DONE, /* 只等待发送完成 */
                              osFlagsWaitAny,            /* 匹配后自动清除此位 */
                              timeout_ticks);            /* 最大等待Tick数 */

    if (((flags & osFlagsError) == 0U) && ((flags & THREAD_FLAG_UART3_TX_DONE) != 0U))
    {
        /* 完成回调已经把EN拉低 */
        rs485_tx_ok_count++;
        return HAL_OK;
    }

    /*
     * 等待失败或超时。
     *
     * 必须先停止后续发送中断，
     * 才能允许调用者重用发送数组。
     *
     * 这里只中止TX，不中止正在运行的RX DMA。
     */
    status = HAL_UART_AbortTransmit(&huart3);

    HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET);

    rs485_tx_error_count++;

    if (status != HAL_OK)
    {
        /*
         * 无法确认发送已停止，就不能继续重用缓冲区。
         * 当前IT发送路径正常应能完成中止。
         */
        Error_Handler();
    }

    if (flags == osFlagsErrorTimeout)
    {
        return HAL_TIMEOUT;
    }

    return HAL_ERROR;
}

/*
 * 当前9600、8N1、一问一答链路的额外观察期。
 * 这是项目策略，不是把协议的t3.5重新定义成6ms。
 */
#define UART3_RX_END_GUARD_US 6000U

static HAL_StatusTypeDef Uart3_ConfirmResponseEnd(uint32_t start_tick, /* 整次响应等待的开始Tick */
                                                  uint32_t timeout_ticks) /* 整次响应等待的总预算 */
{
    uint32_t guard_start_us = AppTime_NowUs();
    uint32_t guard_elapsed_us;
    uint32_t elapsed_ticks;
    uint32_t dma_position;
    uint32_t copied_position;
    uint32_t flags;
    uint8_t invalid_rx;

    for (;;)
    {
        /* 帧尾确认仍然占用原来的接收超时预算 */
        elapsed_ticks = osKernelGetTickCount() - start_tick;

        if (elapsed_ticks >= timeout_ticks)
        {
            return HAL_TIMEOUT;
        }

        /*
         * 短暂保护：
         * 避免接收回调在比较过程中修改软件指针。
         * DMA硬件本身仍然运行。
         */
        taskENTER_CRITICAL();

        dma_position = UART3_DMA_RX_BUFFER_SIZE - __HAL_DMA_GET_COUNTER(huart3.hdmarx);

        copied_position = uart3_dma_old_position;

        /* 循环数组的末尾64与下一圈开头0表示相同边界 */
        if (dma_position == UART3_DMA_RX_BUFFER_SIZE)
        {
            dma_position = 0U;
        }

        if (copied_position == UART3_DMA_RX_BUFFER_SIZE)
        {
            copied_position = 0U;
        }

        invalid_rx = (uint8_t)((uart3_rx_recovery_requested != 0U) ||

                               (huart3.RxState != HAL_UART_STATE_BUSY_RX) ||

                               ((huart3.Instance->CR3 & USART_CR3_DMAR) == 0U) ||

                               ((huart3.hdmarx->Instance->CR & DMA_SxCR_EN) == 0U) ||

                               (uart3_ring_head != uart3_ring_tail) ||

                               (dma_position != copied_position));

        guard_elapsed_us = (uint32_t)(AppTime_NowUs() - guard_start_us);

        taskEXIT_CRITICAL();

        if (invalid_rx != 0U)
        {
            return HAL_ERROR;
        }

        if (guard_elapsed_us >= UART3_RX_END_GUARD_US)
        {
            return HAL_OK;
        }

        /*
         * 最多阻塞1个RTOS Tick，再检查一次DMA。
         * 如果期间出现RX或ERROR通知，也可以提前醒来。
         */
        flags =
            osThreadFlagsWait(THREAD_FLAG_UART3_RX | THREAD_FLAG_UART3_ERROR, osFlagsWaitAny, 1U);

        if ((flags & osFlagsError) != 0U)
        {
            if (flags != osFlagsErrorTimeout)
            {
                return HAL_ERROR;
            }

            /*
             * 这里只是本次1个Tick的等待到期，
             * 不代表整次响应等待超时。
             */
        }
        else if ((flags & THREAD_FLAG_UART3_ERROR) != 0U)
        {
            return HAL_ERROR;
        }
    }
}

static HAL_StatusTypeDef Uart3_WaitKTH2RResponse(uint8_t slave, uint8_t *response,
                                                 uint16_t capacity, uint16_t *length,
                                                 uint32_t timeout_ticks)
{
    uint32_t start_tick;
    uint32_t elapsed;
    uint32_t flags;
    uint8_t byte;
    KTH2R_RxResult_t rx_result; /* 保存本次喂入字节后的组帧结果 */

    if ((response == NULL) || (length == NULL) || (capacity < 9U) || (timeout_ticks == 0U))
    {
        return HAL_ERROR;
    }

    *length = 0U;
    start_tick = osKernelGetTickCount();

    for (;;)
    {
        if (uart3_rx_recovery_requested != 0U)
        {
            return HAL_ERROR;
        }

        if ((osKernelGetTickCount() - start_tick) >= timeout_ticks)
        {
            return HAL_TIMEOUT;
        }

        /* 先处理环形缓冲区中已有的字节 */
        while (Uart3_RingPop(&byte) != 0U)
        {
            if (uart3_rx_recovery_requested != 0U)
            {
                return HAL_ERROR; /* 接收通道异常，本次结束 */
            }

            /* 一直使用本次最初的start_tick，不能被新字节续期 */
            if ((osKernelGetTickCount() - start_tick) >= timeout_ticks)
            {
                return HAL_TIMEOUT;
            }

            rx_result = KTH2R_CollectByte(slave,    /* 本次期望的从站地址 */
                                          byte,     /* 刚从RingBuffer取出的字节 */
                                          response, /* 保存候选响应 */
                                          capacity, /* response数组容量 */
                                          length);  /* 保留并更新组帧进度 */

            if (rx_result == KTH2R_RX_READY)
            {
                return Uart3_ConfirmResponseEnd(start_tick,     /* 沿用整次接收的开始时间 */
                                                timeout_ticks); /* 沿用原来的接收总预算 */
            }

            if (rx_result == KTH2R_RX_ARGUMENT_ERROR)
            {
                return HAL_ERROR;
            }

            /* MORE：继续取已有字节；取完后再阻塞等待RX通知 */
        }

        elapsed = osKernelGetTickCount() - start_tick;

        if (elapsed >= timeout_ticks)
        {
            return HAL_TIMEOUT;
        }

        flags = osThreadFlagsWait(THREAD_FLAG_UART3_RX | THREAD_FLAG_UART3_ERROR,
                                  /* 有字节或发生错误，都可以唤醒 */

                                  osFlagsWaitAny,
                                  /* 任意一个事件出现就返回 */

                                  timeout_ticks - elapsed);
        /* 保留原来的整次接收超时预算 */

        if ((flags & osFlagsError) != 0U)
        {
            if (flags == osFlagsErrorTimeout)
            {
                return HAL_TIMEOUT;
            }

            return HAL_ERROR;
        }

        if ((flags & THREAD_FLAG_UART3_ERROR) != 0U)
        {
            return HAL_ERROR;
        }
    }
}

static HAL_StatusTypeDef Uart3_EnsureRx(void)
{
    HAL_StatusTypeDef status;

    /*
     * 没有恢复请求，且HAL状态、UART DMA请求、
     * DMA Stream使能都正常，就直接返回。
     */
    if ((uart3_rx_recovery_requested == 0U) && (huart3.RxState == HAL_UART_STATE_BUSY_RX) &&
        ((huart3.Instance->CR3 & USART_CR3_DMAR) != 0U) &&
        ((huart3.hdmarx->Instance->CR & DMA_SxCR_EN) != 0U))
    {
        return HAL_OK;
    }

    uart3_rx_recovery_requested = 1U;

    /*
     * 发送尚未结束，或HAL正在异步中止DMA时，
     * 本次先不操作，保留恢复请求。
     */
    if ((huart3.gState != HAL_UART_STATE_READY) || (huart3.hdmarx->State == HAL_DMA_STATE_ABORT))
    {
        return HAL_BUSY;
    }

    /*
     * 暂停本模块的两个中断入口，
     * 防止清理位置时回调同时修改位置。
     * 没有关闭SysTick、TIM6等其他中断。
     */
    HAL_NVIC_DisableIRQ(USART3_IRQn);
    HAL_NVIC_DisableIRQ(DMA1_Stream1_IRQn);

    status = HAL_UART_AbortReceive(&huart3); /* 同步停止USART3接收，不中止TX */

    if (status == HAL_OK)
    {
        /*
         * F4上通过先读SR、再读DR清理相关接收标志。
         * 此时旧接收已停止，残留字节允许丢弃。
         */
        __HAL_UART_CLEAR_OREFLAG(&huart3);

        uart3_ring_head = 0U;
        uart3_ring_tail = 0U;
        uart3_dma_old_position = 0U;

        /* 清除旧的中断挂起状态，必须放在重新启动之前 */
        HAL_NVIC_ClearPendingIRQ(USART3_IRQn);
        HAL_NVIC_ClearPendingIRQ(DMA1_Stream1_IRQn);

        /* 两个相关中断已暂停，不会同时向这些标志位置位 */
        (void)osThreadFlagsClear(THREAD_FLAG_UART3_RX | THREAD_FLAG_UART3_ERROR);

        /*
         * 先清恢复请求，再启动、开放中断。
         * 如果开放中断后又发生错误，回调可以重新置1。
         */
        uart3_rx_recovery_requested = 0U;

        status = HAL_UARTEx_ReceiveToIdle_DMA(&huart3,                   /* USART3 */
                                              uart3_dma_rx_buffer,       /* 接收数组 */
                                              UART3_DMA_RX_BUFFER_SIZE); /* 数组容量 */
    }

    if (status != HAL_OK)
    {
        /* 本次失败，下轮还要继续尝试 */
        uart3_rx_recovery_requested = 1U;
    }

    /* 无论成功还是失败，都恢复两个中断入口 */
    HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);
    HAL_NVIC_EnableIRQ(USART3_IRQn);

    if (status == HAL_OK)
    {
        uart3_recovery.restart_ok_count++;
        Network_Log("uart rx restarted");
    }
    else
    {
        uart3_recovery.restart_fail_count++;
        Network_LogLevel(LOG_ERROR, "uart restart failed");
    }

    return status;
}

static void StartUartRxTask(void *argument)
{
    uint8_t request[8];
    uint8_t response[9];
    uint8_t discarded_byte;
    uint8_t exception_code;

    uint16_t request_length;
    uint16_t response_length;
    uint32_t cycle_tick;

    DeviceHealth_t health;

    uint8_t sample_ok;
    uint32_t now_tick;

    HAL_StatusTypeDef status;
    KTH2R_Data_t reading;
    KTH2R_ParseStatus_t parse_status;

    KTH2R_Data_t latest_data = {0};
    GatewaySnapshot_t snapshot = {0};

    (void)argument;

    DeviceHealth_Init(&health); /* 初始化本任务持有的状态结构体 */

    request_length = KTH2R_BuildReadRequest(1U,               /* 从站地址 */
                                            request,          /* 存放查询请求 */
                                            sizeof(request)); /* 数组容量 */

    if (request_length != 8U)
    {
        Error_Handler();
    }

    /* EN低：默认接收 */
    HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET);

    osDelay(pdMS_TO_TICKS(1000U));

    for (;;)
    {
        cycle_tick = osKernelGetTickCount();

        sample_ok =
            0U; /* 作用： 每轮必须重新得到一次有效响应，才能算成功，不能沿用上一轮的结果。 */

        /* 开始新查询前，丢弃软件缓冲区中的旧字节 */
        while (Uart3_RingPop(&discarded_byte) != 0U)
        {
        }

        /* 调试故障注入：仅在手动置1后执行一次 */
        if (APP_ENABLE_FAULT_TESTS && uart3_test_stop_rx != 0U)
        {
            uart3_test_stop_rx = 0U;

            if (HAL_UART_AbortReceive(&huart3) != HAL_OK)
            {
                uart3_rx_recovery_requested = 1U;
            }
        }

        /* 接收通道正常后，才允许发起本次查询 */
        status = Uart3_EnsureRx();

        if (status == HAL_OK)
        {
            status = RS485_Send(request,              /* 查询报文 */
                                request_length,       /* 报文长度 */
                                pdMS_TO_TICKS(100U)); /* 发送等待上限 */
        }

        if (status == HAL_OK)
        {
            status = Uart3_WaitKTH2RResponse(1U,                   /* 期望的从站地址 */
                                             response,             /* 存放响应 */
                                             sizeof(response),     /* 数组容量9字节 */
                                             &response_length,     /* 输出实际接收长度 */
                                             pdMS_TO_TICKS(300U)); /* 整次接收最多等待300ms */

            if (status == HAL_OK)
            {
                parse_status = KTH2R_ParseResponse(response,         /* 候选响应 */
                                                   response_length,  /* 实际长度 */
                                                   1U,               /* 期望的从站地址 */
                                                   &reading,         /* 输出温湿度 */
                                                   &exception_code); /* 输出异常码 */

                kth2r_monitor.parse_status = parse_status;
                kth2r_monitor.exception_code = exception_code;

                if (parse_status == KTH2R_PARSE_OK)
                {
                    kth2r_monitor.data.temperature_x10 = reading.temperature_x10;

                    kth2r_monitor.data.humidity_x10 = reading.humidity_x10;

                    latest_data = reading;
                    sample_ok = 1U;

                    kth2r_monitor.success_count++;
                }
                else
                {
                    kth2r_monitor.error_count++;
                }
            }
            else if (status == HAL_TIMEOUT)
            {
                kth2r_monitor.timeout_count++;
            }
            else
            {
                kth2r_monitor.error_count++;
            }
        }
        else
        {
            kth2r_monitor.error_count++;
        }

        now_tick = osKernelGetTickCount();

        DeviceHealth_Record(&health,   /* 更新哪个状态对象 */
                            sample_ok, /* 本次是否成功：1成功，0失败 */
                            now_tick); /* 本次结束时的RTOS Tick */

        /* 复制状态快照，供调试器观察 */
        kth2r_monitor.health = health;

        kth2r_monitor.data_fresh = DeviceHealth_IsFresh(&health,  /* 检查哪个设备的数据 */
                                                        now_tick, /* 当前时间 */
                                                        pdMS_TO_TICKS(2500U)); /* 有效期2.5秒 */

        snapshot.data = latest_data;
        snapshot.health = health;

        snapshot.sequence++;
        snapshot.publish_tick = osKernelGetTickCount();

        if (AppSnapshot_Publish(&snapshot) != osOK) /* 发布本次完整快照，不阻塞 */
        {
            kth2r_monitor.publish_error_count++;
        }

        AppHealth_Report(APP_HEALTH_UART);

        /* 以本次开始时间为基准，等待下一轮 */
        if (osDelayUntil(cycle_tick + pdMS_TO_TICKS(1000U)) != osOK)
        {
            /* 本次已经超期时，避免连续立即重发 */
            osDelay(pdMS_TO_TICKS(1000U));
        }
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART3)
    {
        return;
    }

    /*
     * 当前使用UART中断发送。
     * HAL确认UART TC后才调用这里，最后停止位已发完。
     *
     * 先释放总线，避免等待任务调度。
     */
    HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET);

    /* 再通知UART任务：这次发送已完成 */
    if (uartRxTaskHandle != NULL)
    {
        (void)osThreadFlagsSet(uartRxTaskHandle,         /* 通知哪个任务 */
                               THREAD_FLAG_UART3_TX_DONE /* 设置bit1 */
        );
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART3)
    {
        return;
    }

    /* 先保存现场，后续启动收发可能清除HAL内部错误码 */
    uart3_recovery.last_error_code = huart->ErrorCode;
    uart3_recovery.error_event_count++;

    uart3_rx_recovery_requested = 1U;

    if (uartRxTaskHandle != NULL)
    {
        (void)osThreadFlagsSet(uartRxTaskHandle,         /* 通知UART任务 */
                               THREAD_FLAG_UART3_ERROR); /* 设置错误事件位 */
    }
}

void AppUartTask_Create(void)
{
    /* 调度器启动前创建；句柄只由本模块保存。 */
    uartRxTaskHandle = osThreadNew(StartUartRxTask,         /* 任务入口 */
                                   NULL,                    /* 启动参数 */
                                   &uartRxTask_attributes); /* 名称、栈字节数、优先级 */
    configASSERT(uartRxTaskHandle != NULL);
}
