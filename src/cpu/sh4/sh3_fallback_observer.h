// Included after the SH3 maps/state are declared. Called only by Count=true.
#ifndef FBNEO_SH3_FALLBACK_OBSERVER_H
#define FBNEO_SH3_FALLBACK_OBSERVER_H
static void sh3_work_fallback_observe(unsigned opcode, unsigned pc, bool delay)
{
 const bool movll=(opcode&0xf00fu)==0x6002u;
 unsigned size=movll?4u:sh3_fallback_read_size(opcode), address=0, access=SH3_FB_NOT_60_READ;
 if(size) {
  address=m_r[(opcode>>4)&15u];
  unsigned physical=address&AM, watched=0;
  bool mapped=false, mirror=false;
  if(address<0xe0000000u && !(address&(size-1u))) {
   // Inspect existing mapping metadata only. Never call a memory handler or
   // reread an opcode/data word just for diagnostics.
   const UINT8 *page=MemMapR[physical>>SH3_SHIFT];
   mapped=(uintptr_t)page>=SH3_MAXHANDLER;
#ifdef SH3_PPC_DRC
   mirror=!mapped && (uintptr_t)sh3_ppc_state.read_mirror>=SH3_MAXHANDLER &&
    (uintptr_t)page==sh3_ppc_state.mirror_handler &&
    (physical&~(unsigned)SH3_PAGEM)==sh3_ppc_state.mirror_page;
   watched=sh3_ppc_state.mirror_watch;
#endif
  }
  access=sh3_fallback_access(size,address,mapped,mirror,physical,watched);
#ifdef SH3_PPC_DRC
  if(movll) for(unsigned i=0;i<2;++i)
   if(sh3_device_reads[i].callback && address<0xe0000000u && physical==sh3_device_reads[i].address)
    ++sh3_drc_work.device_fallbacks[i];
  if(movll && access==SH3_FB_WATCHED) {
   // The interpreter clears delay BEFORE RL. In a delay slot the handler
   // sees the committed branch target, not the slot instruction address.
   unsigned handler_pc=(delay?m_pc:m_pc+2u)&AM;
   bool registered=sh3_idle_watch_ram && sh3_idle_watch_pc;
   unsigned idle_ram=registered?*sh3_idle_watch_ram:0;
   unsigned idle_pc=registered?*sh3_idle_watch_pc:0;
   sh3_drc_work.idle_candidates.observe(pc,handler_pc,opcode,address,
    idle_ram,idle_pc,registered,delay,sh3_drc_work.fallback.last_origin);
  }
#endif
 }
 sh3_drc_work.fallback.begin(pc,opcode,address,access,delay);
}
#endif
