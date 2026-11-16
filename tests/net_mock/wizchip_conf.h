#include <stdint.h>
typedef struct {uint8_t mac[6],ip[4],sn[4],gw[4];int dhcp;} wiz_NetInfo;
#define NETINFO_STATIC 1
int getVERSIONR(void);
int wizchip_init(uint8_t *a,uint8_t *b);
void wizchip_setnetinfo(wiz_NetInfo *p);
void wizchip_getnetinfo(wiz_NetInfo *p);
void setRTR(int n);
void setRCR(int n);
