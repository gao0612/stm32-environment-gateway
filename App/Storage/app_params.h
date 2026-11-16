#ifndef APP_PARAMS_H
#define APP_PARAMS_H

#include <stdint.h>

/* 调试配置只管理静态 IPv4 网络；DNS、DHCP 和采样参数以后再接入。 */
typedef struct
{
    uint8_t mac[6];                   // MAC硬件地址 6字节
    uint8_t ip[4];                    // IPv4地址 4字节
    uint8_t mask[4];                  // 子网掩码
    uint8_t gateway[4];               // 网关地址
    uint32_t network_probe_period_ms; // 网络监控任务的轮询周期，单位ms，你的networkTask的调度周期
} AppParams_t;

typedef enum
{
    APP_PARAMS_OK = 0,         // ✅全部合法，校验通过
    APP_PARAMS_ERR_NULL = 1,   // ❌入参指针为NULL
    APP_PARAMS_ERR_PERIOD = 2, // ❌network_probe_period_ms周期非法（比如0、过小）
    APP_PARAMS_ERR_MAC = 3,    // ❌MAC地址非法：全0 / 全0xFF
    APP_PARAMS_ERR_MASK = 4,   // ❌子网掩码非法：不是连续的1
    APP_PARAMS_ERR_IP = 5,     // ❌IP非法：全0 / 广播地址
    APP_PARAMS_ERR_GATEWAY = 6 // ❌网关校验失败（可选：网段校验）
} AppParamsStatus_t;

/* 纯校验：不修改候选参数，也不修改当前参数。 */
AppParamsStatus_t AppParams_Validate(const AppParams_t *candidate);

/* 仅在创建任务前调用。NULL 使用默认值；非法候选参数回退默认值。
 * 返回候选参数的校验结果。默认表自身非法时返回错误，由启动代码停止启动。
 * 当前阶段没有任务运行期间修改参数的接口。
 */
AppParamsStatus_t AppParams_Init(const AppParams_t *candidate);

/* Init 成功建立默认值/候选值后使用。返回 RAM 当前参数的只读视图。 */
const AppParams_t *AppParams_Get(void);

#endif
