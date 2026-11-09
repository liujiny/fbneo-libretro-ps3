// Sampled metadata observations, separate from the crowded general site table.
#ifndef FBNEO_SH3_IDLE_CANDIDATE_H
#define FBNEO_SH3_IDLE_CANDIDATE_H
#if defined(_MSC_VER)
#define SH3_IDLE_COUNT "I64u"
#else
#define SH3_IDLE_COUNT "llu"
#endif
struct Sh3IdleSite {
 unsigned used, pc, handler_pc, opcode, address, idle_ram, idle_pc, delay, origin;
 Sh3WorkCount hits, matching;
};
struct Sh3IdleCandidates {
 enum { SITES=16 };
 Sh3WorkCount watched_movll, matching, unregistered, untracked;
 Sh3IdleSite sites[SITES];
 void observe(unsigned pc,unsigned handler_pc,unsigned opcode,unsigned address,
              unsigned idle_ram,unsigned idle_pc,bool registered,bool delay,unsigned origin) {
  ++watched_movll;
  bool matches=registered && (address&0x1fffffffu)==idle_ram &&
   (handler_pc==idle_pc || handler_pc==idle_pc+2u);
  matching+=matches?1u:0u;
  unregistered+=registered?0u:1u;
  for(unsigned i=0;i<SITES;++i) {
   Sh3IdleSite &s=sites[i];
   if(!s.used) {
    s.used=1; s.pc=pc; s.handler_pc=handler_pc; s.opcode=opcode; s.address=address;
    s.idle_ram=idle_ram; s.idle_pc=idle_pc; s.delay=delay?1u:0u; s.origin=origin;
   }
   if(s.pc==pc && s.handler_pc==handler_pc && s.opcode==opcode && s.address==address &&
      s.idle_ram==idle_ram && s.idle_pc==idle_pc && s.delay==(delay?1u:0u) && s.origin==origin) {
    ++s.hits; s.matching+=matches?1u:0u; return;
   }
  }
  ++untracked;
 }
};
static void sh3_idle_candidate_report(const Sh3IdleCandidates &p,void (*emit)(const char*))
{
 if(!emit)return;
 char text[768];
 sprintf(text,"movll_idle_candidates watched=%" SH3_IDLE_COUNT " idle_condition_match=%" SH3_IDLE_COUNT " unregistered=%" SH3_IDLE_COUNT " site_untracked=%" SH3_IDLE_COUNT " sampled_counts_only=1 handler_pc=post_delay_clear slots=16",
  p.watched_movll,p.matching,p.unregistered,p.untracked); emit(text);
 for(unsigned i=0;i<Sh3IdleCandidates::SITES;++i) {
  const Sh3IdleSite &s=p.sites[i]; if(!s.used)continue;
  sprintf(text,"movll_idle_site pc=%08X handler_pc=%08X opcode=%04X address=%08X idle_ram=%08X idle_pc=%08X delay=%u reason=%s hits=%" SH3_IDLE_COUNT " idle_condition_match=%" SH3_IDLE_COUNT,
   s.pc,s.handler_pc,s.opcode,s.address,s.idle_ram,s.idle_pc,s.delay,
   sh3_fallback_origin_name(s.origin),s.hits,s.matching); emit(text);
 }
}
#undef SH3_IDLE_COUNT
#endif
