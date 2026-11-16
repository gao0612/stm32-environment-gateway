#ifndef APP_PARAMS_STORE_H
#define APP_PARAMS_STORE_H

#include "app_params.h"

typedef enum
{
    APP_STORE_OK = 0,

    APP_STORE_ERR_ARGUMENT = 1,

    APP_STORE_ERR_FLASH = 2,

    APP_STORE_ERR_EMPTY = 3,

    APP_STORE_ERR_FORMAT = 4,

    APP_STORE_ERR_VERSION = 5,

    APP_STORE_ERR_CRC = 6,

    APP_STORE_ERR_PARAMS = 7,

    APP_STORE_ERR_INCOMPLETE = 8,

    APP_STORE_ERR_SEQUENCE = 9,

    APP_STORE_NOT_RUN = 255

} AppStoreStatus_t;

AppStoreStatus_t AppParamsStore_Load(AppParams_t *params);

AppStoreStatus_t AppParamsStore_Save(const AppParams_t *params);

#endif
