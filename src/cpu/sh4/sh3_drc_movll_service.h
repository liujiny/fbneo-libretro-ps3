// Included after RL, MOVLL, IRQ handling and the PPC dispatcher are declared.
// Handles board-declared idle/device loads. No direct RAM/device shortcut.
#ifndef FBNEO_SH3_DRC_MOVLL_SERVICE_H
#define FBNEO_SH3_DRC_MOVLL_SERVICE_H
#if defined(_XBOX)
#include "salvia_cv1k_probe.h"
#endif
#ifndef SALVIA_CV1K_PROBE
#define SALVIA_CV1K_PROBE 0
#endif
template<bool Count> static bool sh3_drc_service_movll(unsigned opcode)
{
 if(!sh3_drc_enabled || (opcode&0xf00fu)!=0x6002u) {
  if(Count) ++sh3_drc_work.movll_service_rejects;
  return false;
 }
 const unsigned address=m_r[(opcode>>4)&15u], physical=address&AM;
 unsigned idle_ram=0, idle_pc=0, device=2;
 const unsigned handler_pc=(m_delay?m_pc:m_pc+2u)&AM;
 bool eligible=false;
 if(address<0xe0000000u && !(address&3u)) {
  if(physical==sh3_ppc_state.mirror_watch && sh3_idle_watch_ram && sh3_idle_watch_pc) {
   // Live idle metadata, including the PC observed AFTER delay-slot prologue.
   idle_ram=*sh3_idle_watch_ram; idle_pc=*sh3_idle_watch_pc;
   eligible=physical==idle_ram &&
    (physical&~(unsigned)SH3_PAGEM)==sh3_ppc_state.mirror_page &&
    (uintptr_t)sh3_ppc_state.read_mirror>=SH3_MAXHANDLER &&
    (uintptr_t)MemMapR[physical>>SH3_SHIFT]==sh3_ppc_state.mirror_handler &&
    (handler_pc==idle_pc || handler_pc==idle_pc+2u);
  } else {
   for(unsigned i=0;i<2;++i) {
    const Sh3DrcDeviceRead &read=sh3_device_reads[i];
    if(read.callback && physical==read.address &&
       (uintptr_t)MemMapR[physical>>SH3_SHIFT]==read.handler &&
       ReadLong[read.handler]==read.callback) {
     device=i; eligible=true; break;
    }
   }
  }
 }
 if(!eligible) {
  if(Count) ++sh3_drc_work.movll_service_rejects;
  return false;
 }
 int before=0;
#if SALVIA_CV1K_PROBE
  static unsigned probeTick=0;
  const bool probeNow=((++probeTick&63u)==0);
  unsigned long long probeStart=0;
  if(probeNow) probeStart=salvia_cv1k_tick();
#endif
 if(Count) {
  before=m_sh4_icount; ++sh3_drc_work.movll_services;
  if(device<2) ++sh3_drc_work.device_services[device];
  else sh3_drc_work.idle_candidates.observe(m_delay?m_delay:m_pc,handler_pc,
   opcode,address,idle_ram,idle_pc,true,m_delay!=0,SH3_FB_UNKNOWN);
 }
 // Exactly the original interpreter instruction prologue. In a delay slot
 // the branch target and PR are already committed by the compiled branch.
 if(m_delay) m_delay=0; else m_pc+=2;
 m_ppc=m_pc;
 MOVLL((UINT16)opcode); // unchanged EA update -> RL -> WaitState/ReadLong
 if(m_test_irq && !m_delay) sh4_check_pending_irq();
 EAT(1); // base instruction cost AFTER handler/IRQ, as in timerhack
#if SALVIA_CV1K_PROBE
  if(probeNow) {
   const unsigned long long probeEnd=salvia_cv1k_tick();
   if(probeStart && probeEnd) { salvia_cv1k_probe[8]+=probeEnd-probeStart; ++salvia_cv1k_probe[9]; }
  }
#endif
 if(Count) {
  const unsigned elapsed=(unsigned)(before-m_sh4_icount);
  sh3_drc_work.movll_service_cycles+=elapsed;
  if(device<2) sh3_drc_work.device_service_cycles[device]+=elapsed;
 }
 return true;
}
#endif
