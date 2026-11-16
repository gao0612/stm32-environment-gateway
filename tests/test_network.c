#define NETWORK_HOST_TEST
#include "../App/Tasks/network_task.c"
#include <assert.h>
static uint32_t tick;
static int saved_acks, next_commands;
static uint8_t last_tx[512], incoming[512];
static size_t last_tx_size, input_size, input_at;
static StoreRecord disk;
static int have_disk;
uint32_t HAL_GetTick(void){return tick;}
void osDelay(uint32_t n){tick+=n;}
void *osMessageQueueNew(uint32_t a,uint32_t b,void *c){(void)a;(void)b;(void)c;return (void*)1;}
int osMessageQueuePut(void *a,const void *b,uint8_t c,uint32_t d){(void)a;(void)b;(void)c;(void)d;return 0;}
int close(int sn){(void)sn;return 0;}
int32_t send(int sn,uint8_t *data,uint16_t n){(void)sn;memcpy(last_tx,data,n);last_tx_size=n;return n;}
int32_t recv(int sn,uint8_t *data,uint16_t n){(void)sn;if(!n)return 0;data[0]=incoming[input_at++];return 1;}
uint16_t getSn_RX_RSR(int n){(void)n;return (uint16_t)(input_size-input_at);}
int W5500_PortGetStatus(void){return 0;}
int Store_Log(const char *s){(void)s;return 0;}
int Store_LogLevel(uint8_t l,const char *s){(void)l;(void)s;return 0;}
int Store_Peek(StoreRecord *r){if(!have_disk)return -2;*r=disk;return 0;}
int Store_Ack(uint32_t n){assert(n==disk.number);saved_acks++;have_disk=0;return 0;}
uint32_t Store_Boot(void){return 5;}
osStatus_t AppDisplay_RequestNextPage(void){next_commands++;return 0;}
/* InitChip is discarded by the linker in this host-only executable. */
int main(void){
    state=NET_CONNACK;
    uint8_t auth[]={0x20,2,0,4};
    assert(ProcessPacket(auth,4,2)==-1 && network_monitor.reason==NET_REASON_AUTH);
    uint8_t ok[]={0x20,2,0,0};
    assert(ProcessPacket(ok,4,2)==0 && state==NET_SUBACK && last_tx_size && last_tx[0]==0x82);
    uint8_t subscribed[]={0x90,3,0,1,0};
    assert(ProcessPacket(subscribed,5,2)==0 && state==NET_ONLINE);
    ram_head=0;ram_count=1;ram[0].sequence=77;ram[0].publish_tick=123;
    assert(UploadOne()==0 && waiting_live && ram_count==1 && last_tx[0]==0x32);
    uint8_t ack[]={0x40,2,(uint8_t)(waiting_id>>8),(uint8_t)waiting_id};
    uint8_t wrong[]={0x40,2,0,0};
    assert(ProcessPacket(wrong,4,2)==0 && ram_count==1);
    assert(ProcessPacket(ack,4,2)==0 && ram_count==0 && !waiting_id && saved_acks==0);
    puts("PASS actual network auth, handshake and live RAM PUBACK lifetime");
    have_disk=1;memset(&disk,0,sizeof(disk));disk.number=88;disk.boot=3;strcpy(disk.text,"66");
    ram_count=1;assert(UploadOne()==0 && !waiting_live && waiting_record==88);
    ack[2]=(uint8_t)(waiting_id>>8);ack[3]=(uint8_t)waiting_id;
    memcpy(incoming,ack,4);incoming[4]=0xD0;incoming[5]=0;input_size=6;input_at=0;
    for(unsigned i=0;i<6;i++)assert(ReceivePackets()==0);
    assert(saved_acks==1 && ram_count==1 && !rx_size);
    puts("PASS disk-first upload, fragmented PUBACK plus coalesced PINGRESP");
    assert(UploadOne()==0 && waiting_live);
    retry_ms=0;Retry();assert(ram_count==1 && !waiting_id && retry_ms==1000);
    Retry();assert(retry_ms==2000);
    puts("PASS disconnect keeps unconfirmed RAM data and grows retry delay");
    return 0;
}
