#ifndef INTERNAL_FLASH_H
#define INTERNAL_FLASH_H

#include "stm32f4xx_hal.h"

#define APP_FLASH_SLOT_A 0U
#define APP_FLASH_SLOT_B 1U

#define APP_FLASH_SLOT_COUNT 2U

#define INTERNAL_FLASH_WORD_COUNT 12U

HAL_StatusTypeDef InternalFlash_Read(uint8_t slot, uint32_t words[INTERNAL_FLASH_WORD_COUNT]);

HAL_StatusTypeDef InternalFlash_Save(uint8_t slot, const uint32_t words[INTERNAL_FLASH_WORD_COUNT]);

#endif
