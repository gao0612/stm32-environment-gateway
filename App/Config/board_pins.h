#ifndef BOARD_PINS_H
#define BOARD_PINS_H
#include "main.h"
/* 引用CubeMX标签，不再复制一套数字引脚。 */
#define FLASH_CS_PORT FLASH_CS_GPIO_Port
#define FLASH_CS_PIN  FLASH_CS_Pin
#define SCREEN_CS_PORT LCD_CS_GPIO_Port
#define SCREEN_CS_PIN LCD_CS_Pin
#define NETWORK_CS_PORT W5500_CS_GPIO_Port
#define NETWORK_CS_PIN W5500_CS_Pin
#define RS485_DIR_PORT RS485_DE_GPIO_Port
#define RS485_DIR_PIN RS485_DE_Pin
#endif
