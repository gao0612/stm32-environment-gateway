#ifndef APP_TIME_H
#define APP_TIME_H

#include <stdint.h>

/* MX_TIM2_Init完成后调用一次，启动微秒计数器 */
void AppTime_Init(void);

/* 读取当前微秒计数值；不延时、不等待 */
uint32_t AppTime_NowUs(void);

#endif
