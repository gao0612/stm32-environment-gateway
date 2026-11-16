#include "flash_store.h"
#include "w25q64.h"
#include "cmsis_os.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char memory[FLASH_BYTES];
static int writes,fail_write=-1;
osMutexId_t osMutexNew(void *a){(void)a;return (void*)1;}
int osMutexAcquire(osMutexId_t m,uint32_t n){(void)m;(void)n;return 0;}
int osMutexRelease(osMutexId_t m){(void)m;return 0;}
uint32_t osKernelGetTickCount(void){return 1234;}
void osDelay(uint32_t n){(void)n;}
void AppHealth_Report(int id){(void)id;}
int Flash_Init(void){return 0;}
int Flash_Read(uint32_t a,void *p,size_t n){assert(a+n<=FLASH_BYTES);memcpy(p,memory+a,n);return 0;}
int Flash_Erase(uint32_t a){assert(a%4096==0 && a+4096<=FLASH_BYTES);memset(memory+a,255,4096);return 0;}
int Flash_Write(uint32_t a,const void *p,size_t n){
    assert(a+n<=FLASH_BYTES);const unsigned char *b=p;
    int fail=(writes++==fail_write);size_t part=fail?n/2:n;
    for(size_t i=0;i<part;i++){assert((memory[a+i]&b[i])==b[i]);memory[a+i]&=b[i];}
    return fail?-1:0;
}
static GatewaySnapshot_t sample={.data={-123,650},.health={1},.publish_tick=42};
static void Reset(void){memset(memory,255,sizeof(memory));writes=0;fail_write=-1;assert(Store_Init()==0);}
int main(void){
    StoreRecord r;
    Reset();assert(Store_Pending()==0);assert(Store_SaveSample(&sample)==0);
    assert(Store_Peek(&r)==0 && r.temperature==-123 && r.humidity==650 && r.tick==42);
    uint32_t number=r.number,boot=r.boot;
    assert(Store_Ack(number+1)!=0);assert(Store_Pending()==1);
    assert(Store_Init()==0 && Store_Boot()!=boot && Store_Pending()==1);
    assert(Store_Peek(&r)==0 && r.boot==boot);
    assert(Store_Ack(number)==0);assert(Store_Init()==0 && Store_Pending()==0);
    puts("PASS persist, reboot, wrong ACK, confirmed deletion");
    Reset();assert(Store_SaveSample(&sample)==0);
    fail_write=writes+1;assert(Store_SaveSample(&sample)!=0);fail_write=-1;
    assert(Store_Init()==0 && Store_Pending()==1);
    assert(Store_SaveSample(&sample)==0 && Store_Pending()==2);
    puts("PASS interrupted commit and skip partially programmed record");
    Reset();assert(Store_SaveSample(&sample)==0);
    memory[4U*1024U*1024U+16]^=1;
    assert(Store_Init()==0 && Store_Pending()==0);
    puts("PASS CRC corruption rejected");
    Reset();for(unsigned i=0;i<65536;i++)assert(Store_SaveSample(&sample)==0);
    assert(Store_SaveSample(&sample)==-2 && Store_Pending()==65536);
    assert(Store_Init()==0 && Store_Pending()==65536);
    for(unsigned i=0;i<64;i++){assert(Store_Peek(&r)==0 && r.number==i+1);assert(Store_Ack(r.number)==0);}
    assert(Store_SaveSample(&sample)==0 && Store_Pending()==65473);
    assert(Store_Init()==0 && Store_Peek(&r)==0 && r.number==65);
    puts("PASS full cache protects unsent sector and reuses confirmed sector");
    Reset();for(unsigned i=0;i<65540;i++)assert(Store_Log("wrap")==0);
    assert(Store_LogRead(0,&r)==0 && r.number==65541);
    assert(Store_Init()==0 && Store_LogRead(1,&r)==0 && r.number==65541);
    puts("PASS log ring wrap and reboot newest lookup");
    Reset();
    assert(Store_LogLevel(3,"error event")==0);
    assert(Store_LogLevel(1,"info event")==0);
    assert(Store_LogReadLevel(0,3,&r)==0 && r.type==3 && !strcmp(r.text,"error event"));
    assert(Store_LogReadLevel(1,3,&r)==-2);
    sample.sequence=77;assert(Store_SaveSample(&sample)==0);
    assert(Store_Peek(&r)==0 && !strcmp(r.text,"77"));
    puts("PASS log filtering and persisted sample identity");
    return 0;
}
