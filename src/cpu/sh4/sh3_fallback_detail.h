// Sampled fallback observations only. No guest reads, clocks or heap allocation.
// Included after Sh3WorkCount is defined by sh3_drc_work_profile.h.
#ifndef FBNEO_SH3_FALLBACK_DETAIL_H
#define FBNEO_SH3_FALLBACK_DETAIL_H
#include <stdio.h>

enum Sh3FallbackOrigin {
 SH3_FB_UNKNOWN, SH3_FB_DISABLED, SH3_FB_DELAY, SH3_FB_IRQ,
 SH3_FB_GATE, SH3_FB_FETCH, SH3_FB_NO_ENTRY, SH3_FB_BUDGET,
 SH3_FB_PARTIAL, SH3_FB_ORIGINS
};
enum Sh3FallbackAccess {
 SH3_FB_NOT_60_READ, SH3_FB_INTERNAL, SH3_FB_UNALIGNED,
 SH3_FB_MAPPED, SH3_FB_MIRROR, SH3_FB_WATCHED, SH3_FB_HANDLER,
 SH3_FB_ACCESS_UNKNOWN, SH3_FB_ACCESSES
};
static unsigned sh3_fallback_read_size(unsigned opcode)
{
 if ((opcode & 0xff00u) != 0x6000u) return 0;
 unsigned lo = opcode & 15u;
 return (lo <= 2 || (lo >= 4 && lo <= 6)) ? (1u << (lo & 3u)) : 0;
}
static unsigned sh3_fallback_access(unsigned size, unsigned address,
 bool mapped, bool mirror, unsigned physical, unsigned watched)
{
 if (!size) return SH3_FB_NOT_60_READ;
 if (address >= 0xe0000000u) return SH3_FB_INTERNAL;
 if (address & (size - 1u)) return SH3_FB_UNALIGNED;
 if (mapped) return SH3_FB_MAPPED;
 if (mirror) return size == 4 && physical == watched ? SH3_FB_WATCHED : SH3_FB_MIRROR;
 return SH3_FB_HANDLER;
}
static const char* sh3_fallback_origin_name(unsigned n)
{
 static const char* names[] = {"unknown","disabled","delay","irq","gate","fetch","no_entry","budget","partial"};
 return n < SH3_FB_ORIGINS ? names[n] : "invalid";
}
static const char* sh3_fallback_access_name(unsigned n)
{
 static const char* names[] = {"not_60_read","internal","unaligned","mapped","mirror","watched","handler","unknown"};
 return n < SH3_FB_ACCESSES ? names[n] : "invalid";
}
struct Sh3FallbackSite {
 unsigned used, pc, opcode, origin, access, delay;
 unsigned first_address, last_address;
 Sh3WorkCount count, guest_cycles;
};
struct Sh3FallbackDetail {
 enum { SITES = 64, PROBES = 4 };
 unsigned last_origin, pending_origin, pending_opcode, pending_access, pending_site;
 Sh3WorkCount observed, dropped_sites, dropped_site_cycles;
 Sh3WorkCount origins[SH3_FB_ORIGINS], origin_cycles[SH3_FB_ORIGINS];
 Sh3WorkCount hot60[256], hot60_cycles[256];
 Sh3WorkCount accesses[SH3_FB_ACCESSES], access_cycles[SH3_FB_ACCESSES];
 Sh3WorkCount hot60_cross[SH3_FB_ORIGINS][SH3_FB_ACCESSES];
 Sh3FallbackSite sites[SITES];
 void begin(unsigned pc, unsigned opcode, unsigned address, unsigned access, bool delay) {
  unsigned origin = last_origin < SH3_FB_ORIGINS ? last_origin : (unsigned)SH3_FB_UNKNOWN;
  if (access >= SH3_FB_ACCESSES) access = SH3_FB_ACCESS_UNKNOWN;
  ++observed; ++origins[origin];
  pending_origin=origin; pending_opcode=opcode; pending_access=access; pending_site=SITES;
  if ((opcode & 0xff00u) == 0x6000u) {
   ++hot60[opcode & 255u]; ++accesses[access]; ++hot60_cross[origin][access];
  }
  // Four bounded probes, no replacement or estimated counts. Collisions are
  // reported, so displayed sites are NOT claimed to be global top hotspots.
  unsigned hash=((pc>>1)^(pc>>11)^opcode^(origin*17u)^(access*7u)^(delay?31u:0u)) & (SITES-1u);
  for (unsigned k=0;k<PROBES;++k) {
   unsigned at=(hash+k)&(SITES-1u); Sh3FallbackSite &s=sites[at];
   if (!s.used) {
    s.used=1; s.pc=pc; s.opcode=opcode; s.origin=origin; s.access=access; s.delay=delay?1u:0u;
    s.first_address=s.last_address=address;
   }
   if (s.pc==pc && s.opcode==opcode && s.origin==origin && s.access==access && s.delay==(delay?1u:0u)) {
    ++s.count; s.last_address=address; pending_site=at; return;
   }
  }
  ++dropped_sites;
 }
 void finish(unsigned guest_cycles) {
  origin_cycles[pending_origin]+=guest_cycles;
  if ((pending_opcode & 0xff00u) == 0x6000u) {
   hot60_cycles[pending_opcode & 255u]+=guest_cycles; access_cycles[pending_access]+=guest_cycles;
  }
  if (pending_site<SITES) sites[pending_site].guest_cycles+=guest_cycles;
  else dropped_site_cycles+=guest_cycles;
 }
};
typedef char Sh3FallbackStorageBound[(sizeof(Sh3FallbackDetail)<=12288)?1:-1];

#if defined(_MSC_VER)
#define SH3_FB_COUNT "I64u"
#else
#define SH3_FB_COUNT "llu"
#endif
// Pause-only formatting. Counts/guest cycles describe sampled instructions,
// not host time. The watched-address class does not prove the idle-PC check fired.
static void sh3_fallback_report(const Sh3FallbackDetail &p, void (*emit)(const char*))
{
 if (!emit) return;
 char text[768];
 sprintf(text,"fallback_detail observed=%" SH3_FB_COUNT " site_untracked=%" SH3_FB_COUNT " site_untracked_guest_cycles=%" SH3_FB_COUNT " slots=64 probes=4 access=pre_interpreter_metadata not_host_time=1 cycles_include_irq_and_base=1",p.observed,p.dropped_sites,p.dropped_site_cycles); emit(text);
 for(unsigned i=0;i<SH3_FB_ORIGINS;++i) {
  sprintf(text,"fallback_origin reason=%s count=%" SH3_FB_COUNT " guest_cycles=%" SH3_FB_COUNT,sh3_fallback_origin_name(i),p.origins[i],p.origin_cycles[i]); emit(text);
 }
 for(unsigned i=0;i<SH3_FB_ACCESSES;++i) {
  sprintf(text,"fallback_60_access class=%s count=%" SH3_FB_COUNT " guest_cycles=%" SH3_FB_COUNT " partial=%" SH3_FB_COUNT " no_entry=%" SH3_FB_COUNT " budget=%" SH3_FB_COUNT,sh3_fallback_access_name(i),p.accesses[i],p.access_cycles[i],p.hot60_cross[SH3_FB_PARTIAL][i],p.hot60_cross[SH3_FB_NO_ENTRY][i],p.hot60_cross[SH3_FB_BUDGET][i]); emit(text);
 }
 bool selected[256]={false};
 for(unsigned rank=0;rank<16;++rank) {
  unsigned best=256;
  for(unsigned i=0;i<256;++i)if(!selected[i] && p.hot60[i] && (best==256 || p.hot60[i]>p.hot60[best]))best=i;
  if(best==256)break; selected[best]=true;
  sprintf(text,"fallback_60_opcode rank=%u opcode=%04X count=%" SH3_FB_COUNT " guest_cycles=%" SH3_FB_COUNT,rank+1,0x6000u|best,p.hot60[best],p.hot60_cycles[best]); emit(text);
 }
 bool picked[Sh3FallbackDetail::SITES]={false};
 for(unsigned rank=0;rank<12;++rank) {
  unsigned best=Sh3FallbackDetail::SITES;
  for(unsigned i=0;i<Sh3FallbackDetail::SITES;++i)if(!picked[i] && p.sites[i].used && (best==Sh3FallbackDetail::SITES || p.sites[i].count>p.sites[best].count))best=i;
  if(best==Sh3FallbackDetail::SITES)break; picked[best]=true;
  const Sh3FallbackSite &s=p.sites[best];
  sprintf(text,"fallback_site rank_among_admitted=%u pc=%08X opcode=%04X reason=%s class=%s delay=%u first_address=%08X last_address=%08X count=%" SH3_FB_COUNT " guest_cycles=%" SH3_FB_COUNT,rank+1,s.pc,s.opcode,sh3_fallback_origin_name(s.origin),sh3_fallback_access_name(s.access),s.delay,s.first_address,s.last_address,s.count,s.guest_cycles); emit(text);
 }
}
#undef SH3_FB_COUNT
#endif
