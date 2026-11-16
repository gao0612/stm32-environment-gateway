#include "app_params.h"

/* 唯一的默认值表。修改后仍会经过 AppParams_Validate 检查。 */
static const AppParams_t default_params = {.mac = {0x02U, 0x00U, 0x00U, 0x00U, 0x04U, 0x07U},
                                           .ip = {192U, 168U, 50U, 20U},
                                           .mask = {255U, 255U, 255U, 0U},
                                           .gateway = {0U, 0U, 0U, 0U},
                                           .network_probe_period_ms = 1000U};

/* 启动阶段写入，任务运行后只读。const 只读接口不能代替并发同步。 */
static AppParams_t current_params;

static uint32_t Params_Ipv4ToU32(const uint8_t address[4])
{
    return ((uint32_t)address[0] << 24U) | ((uint32_t)address[1] << 16U) |
           ((uint32_t)address[2] << 8U) | (uint32_t)address[3];
}

static uint8_t Params_IsHost(uint32_t address, uint32_t host_mask)
{
    uint32_t first = address >> 24U;
    uint32_t host = address & host_mask;

    /* 本实验支持常规 IPv4 单播主机地址，排除 0/8、127/8 和组播/保留段。
     * 主机位全0或全1也不作为本机/网关地址。
     */
    return (uint8_t)((first >= 1U) && (first <= 223U) && (first != 127U) && (host != 0U) &&
                     (host != host_mask));
}

AppParamsStatus_t AppParams_Validate(const AppParams_t *candidate)
{
    uint8_t mac_nonzero = 0U;
    uint32_t mask;
    uint32_t host_mask;
    uint32_t ip;
    uint32_t gateway;

    if (candidate == 0)
    {
        return APP_PARAMS_ERR_NULL;
    }

    /* 250~60000ms 是本实验的巡检周期策略，不是 W5500 的硬件限制。 */
    if ((candidate->network_probe_period_ms < 250U) ||
        (candidate->network_probe_period_ms > 60000U))
    {
        return APP_PARAMS_ERR_PERIOD;
    }

    for (uint32_t i = 0U; i < 6U; i++)
    {
        mac_nonzero |= candidate->mac[i];
    }
    if ((mac_nonzero == 0U) || ((candidate->mac[0] & 0x01U) != 0U))
    {
        return APP_PARAMS_ERR_MAC;
    }

    mask = Params_Ipv4ToU32(candidate->mask);
    host_mask = ~mask;
    /* 掩码必须是连续的高位1、低位0，调试支持 /1~/30。
     * 取反后应为连续的低位1，形式为 2^n-1。
     * /0、/31、/32 超出本实验支持范围，并非说它们在所有网络中非法。
     */
    if ((mask == 0U) || (host_mask < 3U) || ((host_mask & (host_mask + 1U)) != 0U))
    {
        return APP_PARAMS_ERR_MASK;
    }

    ip = Params_Ipv4ToU32(candidate->ip);
    if (Params_IsHost(ip, host_mask) == 0U)
    {
        return APP_PARAMS_ERR_IP;
    }

    gateway = Params_Ipv4ToU32(candidate->gateway);
    /* 直连实验允许未设置网关。设置了则必须为同子网的另一台主机。 */
    if ((gateway != 0U) && ((Params_IsHost(gateway, host_mask) == 0U) ||
                            ((gateway & mask) != (ip & mask)) || (gateway == ip)))
    {
        return APP_PARAMS_ERR_GATEWAY;
    }

    return APP_PARAMS_OK;
}

AppParamsStatus_t AppParams_Init(const AppParams_t *candidate)
{
    AppParams_t next = default_params;
    AppParamsStatus_t status = AppParams_Validate(&default_params);

    if (status != APP_PARAMS_OK)
    {
        return status;
    }

    if (candidate != 0)
    {
        status = AppParams_Validate(candidate);
        if (status == APP_PARAMS_OK)
        {
            next = *candidate;
        }
    }

    /* 先准备完整副本再赋值。只有启动阶段会调用，不与任务并发。 */
    current_params = next;
    return status;
}

const AppParams_t *AppParams_Get(void)
{
    return &current_params;
}
