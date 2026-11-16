#include <stdint.h>
int close(int n);
int32_t send(int sn,uint8_t *data,uint16_t n);
int32_t recv(int sn,uint8_t *data,uint16_t n);
uint16_t getSn_RX_RSR(int n);
