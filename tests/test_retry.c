#include "retry_delay.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
int main(void) {
    uint32_t n=0, expected[]={1000,2000,4000,8000,16000,32000,60000,60000};
    for(unsigned i=0;i<8;i++){n=Retry_Next(n);assert(n==expected[i]);}
    assert(Retry_Next(UINT32_MAX)==60000);
    assert(!Buffer_NeedSave(0,0,9999));
    assert(!Buffer_NeedSave(1,1,9999));
    assert(!Buffer_NeedSave(3,0,1999));
    assert(Buffer_NeedSave(4,0,0));
    assert(Buffer_NeedSave(1,0,2000));
    puts("PASS retry cap and online/offline Flash flush policy");
}
