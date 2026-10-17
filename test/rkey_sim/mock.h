#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <stdlib.h>
#define FEATURE_NET
extern uint64_t g_now_us;
static inline uint32_t micros(){ return (uint32_t)g_now_us; }
static inline unsigned long millis(){ return (unsigned long)(g_now_us/1000); }
extern uint8_t SREG, TIMSK1, TIFR1, TCCR1A, TCCR1B; extern uint16_t TCNT1, OCR1A;
#define cli() do{}while(0)
#define _BV(b) (1u<<(b))
#define OCIE1A 1
#define OCF1A 1
#define CS10 0
#define CS11 1
#define ISR(v) void v##_handler()
struct __FlashStringHelper;
#define F(s) ((const __FlashStringHelper*)(s))
struct String { std::string s; String(const char*c=""):s(c){} bool operator==(const char*c)const{return s==c;}
  void trim(){ while(!s.empty()&&isspace(s.back()))s.pop_back(); while(!s.empty()&&isspace(s[0]))s.erase(0,1);} long toInt()const{return atol(s.c_str());}
  unsigned length()const{return s.size();} char charAt(unsigned i)const{return s[i];} };
struct SerialT { template<class T> void print(T){} template<class T> void println(T){} } ;
extern SerialT Serial;
struct IPAddress { uint8_t a[4]; };
extern uint32_t g_lastStatusNonce;
struct EthernetUDP { int begin(uint16_t){return 1;} void stop(){} int parsePacket(){return 0;} int available(){return 0;} int read(){return 0;}
  int read(uint8_t*,int){return 0;} IPAddress remoteIP(){return IPAddress();} uint16_t remotePort(){return 1;}
  void beginPacket(IPAddress,uint16_t){} void write(const uint8_t*b,uint16_t n){ uint32_t nn; memcpy(&nn,b+20,4); g_lastStatusNonce=nn; } void endPacket(){} };
struct EEPROMT { template<class T> void get(int,T&t){ t=T(); } template<class T> void put(int,const T&){} };
extern EEPROMT EEPROM;
extern uint8_t g_port;
#define portOutputRegister(p) (&g_port)
#define digitalPinToPort(p) (p)
#define digitalPinToBitMask(p) ((uint8_t)((p)==34?0x08:0x04))
static inline int analogRead(int){ return rand()&1023; }
const int CW1=34, CW2=35, DC3V=0, DCIN=1;
extern uint8_t CivMode; extern bool CivValid; extern unsigned long CivFreq; extern bool EthLinkStatus;
extern uint8_t ptt_interlock_active; extern bool tunePttRequest, keyerPttRequest, PttOutputEnable, pttExternal, serialDebug;
extern uint8_t SequencerLevel, PttOut; extern int SEQUENCERlead, PAlead, PTTlead;
