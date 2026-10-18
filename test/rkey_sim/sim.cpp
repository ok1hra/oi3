#include "mock.h"
#include <vector>
#include <algorithm>
#include <cmath>
uint64_t g_now_us=1000000; uint8_t SREG,TIMSK1,TIFR1,TCCR1A,TCCR1B; uint16_t TCNT1,OCR1A; SerialT Serial; EEPROMT EEPROM; uint8_t g_port; uint32_t g_lastStatusNonce;
uint8_t CivMode=3; bool CivValid=true; unsigned long CivFreq=14025000; bool EthLinkStatus=true;
uint8_t ptt_interlock_active=0; bool tunePttRequest=false, keyerPttRequest=false, PttOutputEnable=true, pttExternal=false, serialDebug=false;
uint8_t SequencerLevel=0, PttOut=3; int SEQUENCERlead=0, PAlead=10, PTTlead=10;
#include "../../rkey.ino"

static const char* MORSE[128];
struct Edge{ double t; bool down; };   // true time (µs) at sender keyer
int main(int argc,char**argv){
  double ppm = argc>1? atof(argv[1]):5000;  double lat=argc>2?atof(argv[2]):30000, jit=argc>3?atof(argv[3]):40000, loss=argc>4?atof(argv[4]):0.10;
  int wpm=argc>5?atoi(argv[5]):30; unsigned seed=argc>6?atoi(argv[6]):1; srand(seed);
  MORSE['C']="-.-.";MORSE['Q']="--.-";MORSE['T']="-";MORSE['E']=".";MORSE['S']="...";MORSE['5']=".....";MORSE['0']="-----";MORSE['K']="-.-";MORSE['R']=".-.";MORSE['N']="-.";
  const char* txt="CQ CQ TEST 5NN K CQ TEST R";
  double dit=1200000.0/wpm; std::vector<Edge> E; double t=3e6;
  for(const char*p=txt;*p;p++){ if(*p==' '){t+=4*dit;continue;} for(const char*m=MORSE[(int)*p];*m;m++){ E.push_back({t,true}); t+= (*m=='.'?1:3)*dit; E.push_back({t,false}); t+=dit;} t+=2*dit; }
  double tEnd=t+3e6;
  RkeyEnable=true; RkeyKeySet=true; for(int i=0;i<16;i++)RkeyKey[i]=i+1; RkeyDelayMs=120;
  rkey_setup(); rk_nonce_rotate(); rkUdpUp=true;
  // sender clock: s(t)= t*(1+ppm*1e-6) + 12345678
  auto sclk=[&](double tt){ return (uint32_t)(uint64_t)(tt*(1+ppm*1e-6)+12345678.0); };
  struct Pkt{ double arrive; std::vector<uint8_t> b; };
  std::vector<Pkt> net; uint32_t seq=0; uint16_t sess=0xBEEF; uint32_t nonce=0; size_t nextE=0; double lastSend=-1e9, lastEdge=-1e9;
  std::vector<std::pair<double,bool>> out; bool prev=false; uint16_t prevTc=0;
  for(g_now_us=1000000; g_now_us< (uint64_t)tEnd; g_now_us+=25){
    double now=(double)g_now_us;
    // sender
    bool newEdge=false; while(nextE<E.size() && E[nextE].t<=now){ lastEdge=E[nextE].t; nextE++; newEdge=true; }
    double hb = (now-lastEdge<2e6)?20000:100000;
    if(newEdge || now-lastSend>=hb){
      lastSend=now; nonce=g_lastStatusNonce;  // learned from status (simplified: instant)
      std::vector<uint32_t> ev; int first=(int)nextE;
      while(first>0 && (int)nextE-first<RKEY_MAX_EV && now-E[first-1].t<500000) first--;
      for(int i=first;i<(int)nextE;i++){ uint32_t ts=sclk(E[i].t); ts=(ts&~1u)|(E[i].down?1:0); ev.push_back(ts);}
      RkeyKeyHdr h{RKEY_MAGIC,RKEY_VER,RKEY_T_KEY,0,sess,(uint8_t)wpm,(uint8_t)ev.size(),++seq,sclk(now),nonce,(uint16_t)first,0};
      std::vector<uint8_t> b(sizeof h+4*ev.size()+8); memcpy(b.data(),&h,sizeof h); if(!ev.empty()) memcpy(b.data()+sizeof h,ev.data(),4*ev.size());
      rkey_sign(RkeyKey,b.data(),sizeof h+4*ev.size());
      double bo=getenv("BO")?atof(getenv("BO")):0, bl=getenv("BL")?atof(getenv("BL")):0; bool black=(now>=bo&&now<bo+bl);
      if(!black && (rand()/(double)RAND_MAX)>=loss) net.push_back({now+lat+jit*(rand()/(double)RAND_MAX), b});
    }
    // deliver
    for(size_t i=0;i<net.size();){ if(net[i].arrive<=now){ rk_on_packet(net[i].b.data(),net[i].b.size(),(uint32_t)g_now_us); net.erase(net.begin()+i);} else i++; }
    if(g_now_us%1000==0){ rk_service();
      // sequencer model: up after 20 ms, down after 200 ms tail
      static double upReq=-1, dnReq=-1; bool w=rkey_ptt_wanted();
      if(w){ dnReq=-1; if(SequencerLevel==0){ if(upReq<0)upReq=now; if(now-upReq>=20000){SequencerLevel=PttOut;upReq=-1;} } }
      else { upReq=-1; if(SequencerLevel){ if(dnReq<0)dnReq=now; if(now-dnReq>=200000){SequencerLevel=0;dnReq=-1;} } }
      if(g_now_us%5000000==0) rk_nonce_rotate(), g_lastStatusNonce=rkNonce;
    }
    // timer1 emulation
    uint16_t tc=(uint16_t)(g_now_us/4); TCNT1=tc;
    if((TIMSK1&_BV(OCIE1A)) && (uint16_t)(OCR1A-prevTc-1) < (uint16_t)(tc-prevTc)) TIMER1_COMPA_vect_handler();
    prevTc=tc;
    bool cur=(g_port&0x0C)==0x0C; if(cur!=prev){ out.push_back({now,cur}); prev=cur; }
  }
  { double mx=0; for(size_t i=0;i+1<out.size();i++) if(out[i].second) mx=std::max(mx,out[i+1].first-out[i].first); printf("longest output key-down = %.1f ms, out edges=%zu, muted=%d\n",mx/1000,out.size(),rkMuted); if(getenv("BO")) { for(auto&o:out) if(o.first>atof(getenv("BO"))-200000 && o.first<atof(getenv("BO"))+atof(getenv("BL"))+1500000) printf("  out %.1f ms %s\n",o.first/1000,o.second?"DOWN":"up"); } }
  // compare
  printf("in edges=%zu out edges=%zu late=%u lost=%u D=%u ms\n",E.size(),out.size(),rkLateEv,rkLostEv,(unsigned)(rk_D_us()/1000));
  size_t n=std::min(E.size(),out.size()); double maxMarkErr=0,maxGapErr=0,lmin=1e18,lmax=0;
  for(size_t i=0;i<n;i++){ if(E[i].down!=out[i].second){printf("STATE MISMATCH at %zu\n",i);break;}
    double l=out[i].first-E[i].t; lmin=std::min(lmin,l); lmax=std::max(lmax,l);
    if(i+1<n){ double din=(E[i+1].t-E[i].t)*(1+ppm*1e-6), dout=out[i+1].first-out[i].first; double err=fabs(dout-din);
      if(E[i].down){ if(err>2000) printf("mark %zu in=%.0f out=%.0f t=%.0f\n",i,din,dout,E[i].t); maxMarkErr=std::max(maxMarkErr,err);} else maxGapErr=std::max(maxGapErr,err);} }
  printf("mark len max err = %.0f us (dit=%.0f us)\ngap  len max err = %.0f us\nlatency min/max  = %.1f / %.1f ms\n",maxMarkErr,dit,maxGapErr,lmin/1000,lmax/1000);
}
