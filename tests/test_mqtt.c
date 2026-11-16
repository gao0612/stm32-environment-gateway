#include "mqtt_packet.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void){
    uint8_t b[512];size_t total=0,h=0;
    const uint8_t expected[]={0x10,15,0,4,'M','Q','T','T',4,2,0,30,0,3,'a','b','c'};
    size_t n=Mqtt_Connect(b,sizeof(b),"abc","","");
    assert(n==sizeof(expected) && memcmp(b,expected,n)==0);
    assert(Mqtt_Connect(b,14,"abc","","")==0);
    assert(Mqtt_Connect(b,sizeof(b),"abc","","password")==0);
    n=Mqtt_Connect(b,sizeof(b),"abc","u","p");assert(n && b[9]==0xC2);
    puts("PASS CONNECT wire bytes, auth flags and bounds");
    const uint8_t pub[]={0x32,8,0,1,'t',0x12,0x34,'a','b','c'};
    n=Mqtt_Publish(b,sizeof(b),"t","abc",0x1234);
    assert(n==sizeof(pub) && memcmp(b,pub,n)==0);
    assert(!Mqtt_Publish(b,10,"t","abc",1));
    assert(!Mqtt_Publish(b,sizeof(b),"t","abc",0));
    puts("PASS QoS1 PUBLISH id, payload and bounds");
    char text[201];memset(text,'x',200);text[200]=0;
    n=Mqtt_Publish(b,sizeof(b),"t",text,1);assert(n==208 && (b[1]&0x80));
    for(size_t i=0;i<n;i++)assert(Mqtt_FrameSize(b,i,sizeof(b),&total,&h)==0);
    assert(Mqtt_FrameSize(b,n,sizeof(b),&total,&h)==1 && total==n && h==3);
    uint8_t bad[]={0x30,0x80,0x80,0x80,0x80};
    assert(Mqtt_FrameSize(bad,5,512,&total,&h)==-1);
    assert(Mqtt_FrameSize(b,n,100,&total,&h)==-1);
    const uint8_t sub[]={0x82,6,0,1,0,1,'t',0};
    n=Mqtt_Subscribe(b,sizeof(b),"t",1);assert(n==8 && memcmp(b,sub,8)==0);
    puts("PASS SUBSCRIBE, variable remaining length, fragmented frames and oversized input");
}
