// Shared C++ dispatch control. Included after the SH3 PPC cache/compiler.
// This does not link native blocks or bypass validation of mutable guest code.
/* Sampling profiler and per-block bookkeeping. Off by default: a release build
 * must not pay a timebase read every 64 blocks. Build with
 * -DSH3_PROFILE_SAMPLE=1 to get the hot-page histogram and the instrumented
 * counters back. */
#ifndef SH3_PROFILE_SAMPLE
#define SH3_PROFILE_SAMPLE 0
#endif
#if defined(__PS3__)
/* Timebase accounting: how much of the dispatch cost is the generated code and
 * how much is the dispatcher around it. */
extern unsigned long long sh3_dbg_blk_ticks, sh3_dbg_disp_ticks;
/* Sparse sampling profiler: one timestamp every 64 dispatched blocks. The sample
 * interval (~30 us) is far above the console's timebase resolution, so the deltas
 * are trustworthy where back-to-back reads are not. Each delta is attributed to
 * the page of the pc at the start of the interval. */
extern unsigned long long sh3_prof_total_ticks;
extern unsigned long long sh3_prof_page_ticks[1024];
extern unsigned sh3_prof_samples, sh3_prof_interp_samples;
extern unsigned long long sh3_prof_prev_tick;
extern unsigned long long sh3_dbg_svc_ticks, sh3_dbg_tail_ticks;
extern unsigned long long sh3_dbg_tail_start;
extern unsigned sh3_dbg_svc_calls;
extern unsigned sh3_dbg_exit_hist[17];
extern unsigned sh3_dbg_ex_fetch, sh3_dbg_ex_budget, sh3_dbg_interp;
extern unsigned sh3_dbg_timed_calls;
static inline unsigned long long sh3_tb_now(void)
{
 unsigned long long v;
 __asm__ __volatile__("mftb %0" : "=r"(v));
 return v;
}
#endif

#ifndef FBNEO_SH3_DRC_DISPATCH_H
#define FBNEO_SH3_DRC_DISPATCH_H
#include "sh3_drc_source_check.h"
#include "sh3_drc_lookup.h"
#include "sh3_drc_work_profile.h"
#if defined(_XBOX)
#include "salvia_cv1k_probe.h"
#endif
#ifndef SALVIA_CV1K_PROBE
#define SALVIA_CV1K_PROBE 0
#endif

#if SALVIA_CV1K_STAMP_PROBE
// Write-stamp helpers (docs/optimization/2026-10-09-cv1k-write-stamps.md).
// A clean stamp pair is only meaningful while the registered window is the one
// the generated code stamps against, which is exactly the condition the
// emitter uses to decide whether to stamp a store at all. Anything else -- no
// window, a stale base pointer, a block whose bytes are not in the window --
// reports "not clean" and keeps the ordinary comparison.
static bool sh3_stamp_in_contract()
{
 return Sh3Ppc::code_stamp && Sh3Ppc::ram_window.base &&
   sh3_ppc_state.ram_base==Sh3Ppc::ram_window.base;
}
// The two 64-byte guest lines these bytes occupy, by their offset inside the
// window backing. A block is at most 33 halfwords, so a block can never span
// more than two of them, and an aligned store can never cross one.
static bool sh3_stamp_lines(const UINT16 *source, unsigned words,
 unsigned &first, unsigned &last)
{
 if(!words || !sh3_stamp_in_contract()) return false;
 const UINT32 off=(UINT32)((uintptr_t)(const UINT8*)source-(uintptr_t)Sh3Ppc::ram_window.base);
 const UINT32 end=off+(UINT32)words*2-1;
 if(off>Sh3Ppc::ram_window.mask || end>Sh3Ppc::ram_window.mask) return false;
 first=off>>Sh3Ppc::STAMP_SHIFT;
 last=end>>Sh3Ppc::STAMP_SHIFT;
 return true;
}
static bool sh3_stamp_clean(const UINT16 *source, unsigned words)
{
 unsigned first=0,last=0;
 if(!sh3_stamp_lines(source,words,first,last)) return false;
 return Sh3Ppc::code_stamp[first]==0 && (first==last || Sh3Ppc::code_stamp[last]==0);
}
// Called when a validation has just proved these bytes still match the block's
// snapshot: from here the lines are clean again until a store touches them.
static void sh3_stamp_clear(const UINT16 *source, unsigned words)
{
 unsigned first=0,last=0;
 if(!sh3_stamp_lines(source,words,first,last)) return;
 Sh3Ppc::code_stamp[first]=0;
 if(last!=first) Sh3Ppc::code_stamp[last]=0;
}
#endif

// Keep the native loop in the caller: interpreter fallback should not pay a
// second dispatcher stack frame. This does not inline/link generated PPC blocks.
#if SALVIA_CV1K_LINK_PROBE >= 2
// Gate probe for a block link (docs/optimization/2026-10-10-cv1k-link-gate.md).
// Diagnostics: every counter below is only incremented, nothing here decides
// what the dispatcher does.
//
// `immutable`: this entry's fetch page has no write mapping, so the only writer
// left is the interpreter's store path, which the board routes to a region
// handler. CV1000 maps its program ROM that way and that handler drops writes,
// so guest code living in it cannot be changed by any store the emulator
// executes -- the population a link can enter without asking a store-side
// question at all. The snapshot comparison is the audit: if the bytes of an
// immutable entry ever differ from the record, this test is wrong.
static bool sh3_src_immutable(const UINT16 *source,UINT32 phys)
{
 if(MemMapW[phys>>SH3_SHIFT]!=NULL) return false;
 if(!Sh3Ppc::ram_window.base) return true;
 const UINT32 off=(UINT32)((uintptr_t)(const UINT8*)source-(uintptr_t)Sh3Ppc::ram_window.base);
 return off>Sh3Ppc::ram_window.mask;
}
// A link table entry that predicts the successor of one block. `sector` is the
// arena sector the target's code was compiled into and `slot` the block-table
// record that named it, so a later pass can ask whether that code is still
// resident -- what a sector-generation counter would answer in the real link.
static bool sh3_link_alive(const Sh3Ppc::LinkProbe &p)
{
 return Sh3Ppc::slot_sector && p.slot<Sh3Ppc::TABLE_SIZE &&
   Sh3Ppc::slot_sector[p.slot]!=0xFF && (Sh3Ppc::slot_sector[p.slot]&0x1Fu)==p.sector;
}
#endif
#if SALVIA_CV1K_SHADOW
// Fetch the copy that will answer the successor of this entry. The successor is
// the last one this pc was seen to run into, which the dispatcher recorded on
// the previous pass, so the fetch that would otherwise stall the next entry is
// issued a whole block execution early -- the only lead time long enough to
// cover a miss to main memory. A mispredicted pc fetches a line nobody reads;
// a prefetch decides nothing, so there is no way for it to be wrong.
static inline void sh3_shadow_touch(const UINT32 pc)
{
#if SALVIA_CV1K_SHADOW_TOUCH
 const unsigned index=((pc>>1)^(pc>>11)^(pc>>21))&(Sh3Ppc::CACHE_SETS-1);
 const char *line=(const char*)Sh3Ppc::shadow+((size_t)index*sizeof(Sh3Ppc::Shadow));
#if defined(_XBOX) || (defined(__GNUC__) && (defined(__powerpc__) || defined(__ppc__)))
 Sh3Ppc::sh3_touch_line(line);
#else
 // The host models no cache, so it only has to prove the address arithmetic
 // stays inside the table it is derived from.
 if((const char*)line<(const char*)Sh3Ppc::shadow ||
    (const char*)line>=(const char*)Sh3Ppc::shadow+Sh3Ppc::CACHE_SETS*(int)sizeof(Sh3Ppc::Shadow)) abort();
#endif
#else
 (void)pc;
#endif
}
#endif
#if defined(_MSC_VER) && defined(_XBOX)
#define SH3_DISPATCH_INLINE __forceinline
#elif defined(__GNUC__)
#define SH3_DISPATCH_INLINE inline __attribute__((always_inline))
#else
#define SH3_DISPATCH_INLINE inline
#endif

template<bool Chained, bool Count> static SH3_DISPATCH_INLINE bool sh3_drc_dispatch_impl()
{
 using namespace Sh3Ppc;
 // Preserve allocate()'s failure precedence and cold initialization. On a
 // warm cache it only returns true, so avoid its out-of-line call here.
 if(Count) ++sh3_drc_work.dispatch_calls;
#if SALVIA_CV1K_CHAIN_PROBE
 // One chain starts here: nothing in it may be assumed valid yet.
 sh3_chain_dirty=0;
#endif
 if(m_sh4_icount<=0 || failed || (!blocks && !allocate())) {
  if(Count) { ++sh3_drc_work.exit_gate; sh3_drc_work.fallback.last_origin=SH3_FB_GATE; }
  return false;
 }
 // Generated entries are leaf functions: they cannot release the cache or
 // replace its allocation. Keep the dispatcher stack alive across entries.
#if SALVIA_CV1K_LINK_PROBE
 // Per chain call: the entry before the current one, and whether it ended at
 // its own sequential completion (the compiler records that reason in the top
 // three bits of the slot map). A successor fast path could only serve the
 // pair, so this counts exactly the population it could remove from the
 // lookup path. Diagnostics; the sampled specialization owns these.
 static unsigned link_prev_words=0;
 static UINT32 link_prev_pc=0;
 static bool link_prev_sequential=false;
 link_prev_sequential=false;
#endif
#if SALVIA_CV1K_LINK_PROBE >= 2
 // The predecessor of the entry this iteration is about to resolve. A link can
 // only be consulted for entries that follow another entry, so the state starts
 // invalid at every chain call: whatever ran before the loop returned to the
 // outer interpreter is not a predecessor this chain links from.
 unsigned lp_prev_slot=0, lp_prev_index=0;
 bool lp_prev_valid=false;
#endif
 do {
#if defined(__PS3__)
  const unsigned long long __tb_entry = sh3_tb_now();
#endif
#if SALVIA_CV1K_PROBE
  // Three spans per sampled entry: the pre-entry work (fetch, lookup,
  // validation, any recompile), the generated block call, and the post-entry
  // work. Compared with the whole drc_dispatch phase they show how much of the
  // dispatch path is instructions and how much is the unattributed remainder.
  static unsigned probeTick=0;
  const bool probeNow=((++probeTick&63u)==0);
  unsigned long long probeT0=0,probeStart=0,probeEnd=0;
  if(probeNow) probeT0=salvia_cv1k_tick();
#endif
  UINT32 pc=m_pc, phys=pc&AM;
  unsigned index=((pc>>1)^(pc>>11)^(pc>>21))&(CACHE_SETS-1);
  const UINT8 *page=MemMapF[phys>>SH3_SHIFT];
  if((uintptr_t)page<SH3_MAXHANDLER || (phys&1)) {
   ++sh3_dbg_ex_fetch;
   if(Count) { ++sh3_drc_work.exit_fetch; sh3_drc_work.fallback.last_origin=SH3_FB_FETCH; }
   return false;
  }
  const UINT16 *source=(const UINT16*)(page+(phys&SH3_PAGEM));
  // What the shared tail below needs, from whichever record answered: the
  // shadow copy if it can serve this entry, the table record otherwise.
  int (*entry)(Sh3PpcState*);
  int cycles;
  unsigned words, reason=0, stores=0;
#if SALVIA_CV1K_SHADOW
  // The shadow copy for this pc, if the dispatcher has one and it still
  // describes the bytes: its own tag and fetch page, a sector generation that
  // says the code it names is still resident, and the snapshot comparison --
  // the same validation the record path makes, not a weaker one.
  const Shadow *copy=NULL;
  if(shadow) {
   const Shadow &cand=shadow[index];
   if(cand.pc==pc && cand.source==source && sector_epoch[cand.sector]==cand.epoch &&
      (!cand.check_read_map || MemMapR[phys>>SH3_SHIFT]==page) &&
      sh3_drc_source_equal(cand.original,source,cand.words)) copy=&cand;
  }
  if(copy) {
   if(Count) ++sh3_drc_work.shadow_hits;
   entry=copy->entry; cycles=copy->cycles; words=copy->words;
   reason=copy->reason; stores=copy->stores;
  } else
#endif
  {
  if(Count) ++sh3_drc_work.lookups;
  Lookup &set=lookup[index];
  const unsigned way=sh3_drc_lookup4(set.tag,set.next,pc);
  Block &b=blocks[index*WAYS+way];
#if SALVIA_CV1K_LINK_PROBE >= 2
  const bool lp_immutable=Count && sh3_src_immutable(source,phys);
  bool lp_rec_ok=false, lp_set_ok=false;
   bool lp_sh_ok=false;
  if(Count) {
   const unsigned lp_slot=index*WAYS+way;
   if(lp_immutable) ++sh3_drc_work.link2_rom_entries;
   else ++sh3_drc_work.link2_ram_entries;
   // Footprint of the layouts under consideration: the record array as it is
   // (84 bytes), the hot half of a split record (16 bytes), and the guest bytes
   // the validation reads.
   lp_rec_line[((UINT32)lp_slot*84)>>6]=1;
   lp_hot_line[((UINT32)lp_slot*LP_HOT_BYTES)>>6]=1;
   // The guest bytes this entry's validation reads, keyed by their line inside
   // the registered window (the same space the write stamps use): the window
   // offset of the source pointer, which a record in the table always has.
   if(Sh3Ppc::ram_window.base) {
    const UINT32 lp_srcoff=(UINT32)((uintptr_t)source-(uintptr_t)Sh3Ppc::ram_window.base);
    if(lp_srcoff<=Sh3Ppc::ram_window.mask)
     lp_src_line[(lp_srcoff>>6)&(LP_SRC_LINES-1)]=1;
   }
   if(lp_prev_valid) {
    const uintptr_t lp_page=(uintptr_t)page;
    ++sh3_drc_work.link2_steps;
    const LinkProbe &rec=lp_rec[lp_prev_slot];
    const LinkProbe &setp=lp_set[lp_prev_index&LP_SET_MASK];
    if(rec.pc==pc) {
     ++sh3_drc_work.link2_rec_hit;
     if(rec.page==lp_page) {
      ++sh3_drc_work.link2_rec_page;
      if(sh3_link_alive(rec)) {
       lp_rec_ok=true;
       ++sh3_drc_work.link2_rec_ok;
       if(lp_immutable) ++sh3_drc_work.link2_rom_ok;
      }
     }
    }
    if(setp.pc==pc) {
     ++sh3_drc_work.link2_set_hit;
     if(setp.page==lp_page) {
      ++sh3_drc_work.link2_set_page;
      if(sh3_link_alive(setp)) {
       lp_set_ok=true;
       ++sh3_drc_work.link2_set_ok;
       if(lp_immutable) ++sh3_drc_work.link2_set_rom_ok;
      }
     }
    }
    // The shadow model: it is read at the set index of the pc being entered and
    // answers only if its own tag and page match and the code it names is still
    // resident. Everything else about it -- the snapshot, the entry, the cycle
    // cost -- is what the record held when it was last written.
    const LinkProbe &sh=lp_shadow[index&LP_SET_MASK];
    if(sh.pc==pc) {
     ++sh3_drc_work.link2_sh_hit;
     if(sh.page==lp_page && sh3_link_alive(sh)) lp_sh_ok=true;
    }
   }
  }
#endif
#if SALVIA_CV1K_LINK_PROBE
  if(Count && b.source==source && b.pc==pc) {
   ++sh3_drc_work.link_entries;
   lookup_touched[index]=1;
   if(link_prev_sequential && pc==link_prev_pc+2*(UINT32)link_prev_words)
    ++sh3_drc_work.link_sequential;
  }
  // The next iteration's predecessor. Both fields are read before compile()
  // can overwrite them on a miss.
  link_prev_pc=pc; link_prev_words=b.words;
  link_prev_sequential=(b.source==source && b.pc==pc) &&
   (slot_sector[index*WAYS+way]>>5)==END_WINDOW;
#endif
  // Recheck EVERY entry, including successors, aliases, DMA/cheat writes and
  // changed fetch/read mappings. No cached host entry bypasses these guards.
  bool rebuild;
  if(Count) {
   // Classify before compile() overwrites the record it is derived from.
   const bool tag_hit=(b.source==source && b.pc==pc);
   const bool same=tag_hit && sh3_drc_source_equal(b.original,source,b.words);
   const bool map_ok=(!b.check_read_map || MemMapR[phys>>SH3_SHIFT]==page);
   if(tag_hit) {
    ++sh3_drc_work.validation_spans;
    sh3_drc_work.validation_words+=b.words;
#if SALVIA_CV1K_LINK_PROBE >= 2
    // The shadow answers only if the bytes still match the snapshot it copied
    // along with everything else, so this is the share of entries a shadow
    // record could serve, and the counter next to it is the one hole that would
    // make it unsafe.
    if(lp_sh_ok) {
     if(same) ++sh3_drc_work.link2_sh_ok;
     else ++sh3_drc_work.link2_sh_stale;
    }
#endif
#if SALVIA_CV1K_STAMP_PROBE
    // Write-stamp cross-check. This specialization compares anyway, so it is
    // the audit of the skip below: `clean` is the share of validations a clean
    // stamp pair would have removed, and `missed` counts the entries where the
    // stamps said clean while the comparison said the bytes had changed --
    // a write path that is not hooked, and the one result that makes the skip
    // unsafe. It must stay zero.
    const bool stamps_clean=sh3_stamp_clean(source,b.words);
    ++sh3_drc_work.stamp_entries;
    if(stamps_clean) {
     ++sh3_drc_work.stamp_clean;
     if(!same) ++sh3_drc_work.stamp_missed;
    } else ++sh3_drc_work.stamp_dirty;
    // The ordinary validation credits the stamps, exactly as it will when the
    // hot path is allowed to rely on them.
    if(same) sh3_stamp_clear(source,b.words);
#endif
#if SALVIA_CV1K_CHAIN_PROBE
    // Could this entry have been linked? Only if no store has run since the
    // chain started, which is exactly what a chain-scoped link would test.
    if(sh3_chain_dirty) ++sh3_drc_work.chain_dirty_entries;
    else ++sh3_drc_work.chain_clean_entries;
#endif
   }
   if(!tag_hit) ++sh3_drc_work.rebuild_conflict;
   else if(!same) ++sh3_drc_work.rebuild_source;
   else if(!map_ok) ++sh3_drc_work.rebuild_map;
#if SALVIA_CV1K_LINK_PROBE >= 2
   // The two dangerous directions, on the same entries the counters above
   // classify: a link would have entered a translation whose bytes this entry's
   // comparison proves had changed, and the same for the immutable population.
   // Both must stay zero; anything else is a hole in the link's validity rule.
   if(!same) {
    if(lp_rec_ok || lp_set_ok) ++sh3_drc_work.link2_stale;
    if(lp_immutable) ++sh3_drc_work.link2_rom_changed;
   }
#endif
   rebuild=!tag_hit || !same || !map_ok;
  } else {
#if SALVIA_CV1K_STAMP_PROBE >= 2
   // Rely on the stamps: no store has written the lines this block's bytes
   // occupy since the dispatcher last proved them equal to its snapshot, so
   // the comparison cannot have changed its answer. Everything else -- the tag
   // match, the fetch page, the source pointer, the read-map test -- is
   // checked exactly as before, and a dirty pair still takes the comparison.
   const bool tag_hit=(b.source==source && b.pc==pc);
   const bool stamps_clean=tag_hit && sh3_stamp_clean(source,b.words);
   const bool same=tag_hit && (stamps_clean || sh3_drc_source_equal(b.original,source,b.words));
   if(same) sh3_stamp_clear(source,b.words);
   rebuild=!same || (b.check_read_map && MemMapR[phys>>SH3_SHIFT]!=page);
#else
   rebuild=(b.source!=source || b.pc!=pc || (b.check_read_map && MemMapR[phys>>SH3_SHIFT]!=page) ||
     !sh3_drc_source_equal(b.original,source,b.words));
#if defined(__PS3__)
   /* Why does this entry have to be rebuilt? A block cache that never hits is
    * indistinguishable from a slow emulator, so classify the miss. */
   if (b.source != source || b.pc != pc) ++sh3_dbg_rb_tag;
   else if (b.check_read_map && MemMapR[phys>>SH3_SHIFT] != page) ++sh3_dbg_rb_map;
   else if (!sh3_drc_source_equal(b.original, source, b.words)) ++sh3_dbg_rb_src;
   else ++sh3_dbg_rb_ok;
#endif
#endif
  }
  if(rebuild) {
   compile(b,pc,source);
   arena_note_slot(index*WAYS+way,b);
   if(Count) ++sh3_drc_work.rebuilds;
   set.tag[way]=pc;
  }
  entry=b.entry; cycles=b.cycles; words=b.words;
  reason=slot_sector?(unsigned)(slot_sector[index*WAYS+way]>>5):0u;
  stores=slot_stores?slot_stores[index*WAYS+way]:0u;
#if SALVIA_CV1K_SHADOW
  if(shadow && b.entry) {
   // The record now describes the bytes at this pc, so this is the moment to
   // take the copy the next entry to this pc will read instead of it.
   Shadow &dst=shadow[index];
   dst.pc=b.pc; dst.source=b.source; dst.entry=b.entry;
   dst.words=b.words; dst.cycles=b.cycles;
   dst.check_read_map=b.check_read_map?1:0;
   dst.sector=(UINT8)arena_sector_of(b.entry);
   dst.epoch=sector_epoch[dst.sector];
   dst.reason=(UINT8)reason; dst.stores=(UINT8)stores;
   memcpy(dst.original,b.original,(unsigned)b.words*2);
  }
#endif
  }
  if(!entry) {
   if(Count) { ++sh3_drc_work.exit_no_entry; sh3_drc_work.fallback.last_origin=SH3_FB_NO_ENTRY; }
   return false;
  }
  ++sh3_dbg_fallbacks;
  if(m_sh4_icount<cycles) {
   ++sh3_dbg_ex_budget;
   if(Count) { ++sh3_drc_work.exit_budget; sh3_drc_work.fallback.last_origin=SH3_FB_BUDGET; }
   return false;
  }
  // A partial/guarded block requests one interpreter step at its updated PC,
  // even when earlier blocks in this call completed. Do not return true here.
  int before=0;
  if(Count) {
   before=m_sh4_icount;
   ++sh3_drc_work.native_calls;
   ++sh3_drc_work.snapshot_lengths[words<=33?words:33];
   ++sh3_drc_work.block_ends[reason];
   // Guest stores this block will perform, weighted by its entry count. Only
   // the sampled dispatch specialization reads the diagnostic array.
   sh3_drc_work.block_stores+=stores;
  }
#if SALVIA_CV1K_PROBE
  if(probeNow) probeStart=salvia_cv1k_tick();
#endif
#if SALVIA_CV1K_LINK_PROBE >= 2
  if(Count) {
   // The successor of the block about to run is exactly the pc the next
   // iteration resolves, so this is where a link learns. A record with no
   // generated code has nothing to link into and records an impossible pc.
   const unsigned lp_slot=index*WAYS+way;
   const bool lp_ok=b.entry && slot_sector && slot_sector[lp_slot]!=0xFF;
   const UINT32 lp_pc=lp_ok?pc:0xFFFFFFFFu;
   const unsigned lp_sect=lp_ok?(unsigned)(slot_sector[lp_slot]&0x1Fu):0u;
   lp_rec[lp_prev_slot].pc=lp_pc;
   lp_rec[lp_prev_slot].page=(uintptr_t)page;
   lp_rec[lp_prev_slot].slot=lp_slot;
   lp_rec[lp_prev_slot].sector=lp_sect;
   lp_set[lp_prev_index&LP_SET_MASK].pc=lp_pc;
   lp_set[lp_prev_index&LP_SET_MASK].page=(uintptr_t)page;
   lp_set[lp_prev_index&LP_SET_MASK].slot=lp_slot;
   lp_set[lp_prev_index&LP_SET_MASK].sector=lp_sect;
   // The shadow is keyed by the pc it describes, so it is written at the entry
   // it belongs to, out of the record the dispatcher has just resolved.
   lp_shadow[index&LP_SET_MASK].pc=pc;
   lp_shadow[index&LP_SET_MASK].page=(uintptr_t)page;
   lp_shadow[index&LP_SET_MASK].slot=lp_slot;
   lp_shadow[index&LP_SET_MASK].sector=lp_sect;
   lp_prev_slot=lp_slot; lp_prev_index=index; lp_prev_valid=true;
  }
#endif
#if SALVIA_CV1K_SHADOW
  if(shadow) sh3_shadow_touch(shadow[index].next_pc);
#endif
#if defined(__PS3__)
  ++sh3_dbg_native_calls;
  ++sh3_dbg_dispatches;
  {
   const unsigned long long __tb1 = sh3_tb_now();
   sh3_dbg_disp_ticks += __tb1 - __tb_entry;
  }
  int completed;
  {
   const unsigned long long __tb2 = sh3_tb_now();
   completed = sh3_ppc_call(entry,&sh3_ppc_state);
   sh3_dbg_blk_ticks += sh3_tb_now() - __tb2;
  }
#else
  const int completed=entry(&sh3_ppc_state);
#endif
#if SALVIA_CV1K_SHADOW
  if(shadow) {
   // The pc the next entry resolves is this pc's observed successor, and the
   // line is already in hand from the read above.
   if(Count && shadow[index].next_pc!=(UINT32)m_pc) ++sh3_drc_work.shadow_pred_miss;
   shadow[index].next_pc=(UINT32)m_pc;
  }
#endif
#if SALVIA_CV1K_PROBE
  if(probeNow) probeEnd=salvia_cv1k_tick();
#endif
  if(Count) sh3_drc_work.native_cycles+=(unsigned)(before-m_sh4_icount);
#if defined(__PS3__)
  /* Catch the block that drives the cycle counter far below zero: that is what
   * creates the nExtraCycles debt and starves the following frames. */
#endif
  // A compiler-tagged idle/device MOV.L needs one real handler access, but
  // no opcode refetch or trip through the outer interpreter decoder.
  bool serviced=false;
#if defined(__PS3__)
  if((completed&3)==2) {
   const unsigned long long __s0 = sh3_tb_now();
   serviced=sh3_drc_service_movll<Count>((unsigned)completed>>2);
   sh3_dbg_svc_ticks += sh3_tb_now() - __s0;
   ++sh3_dbg_svc_calls;
  }
#else
  if((completed&3)==2) serviced=sh3_drc_service_movll<Count>((unsigned)completed>>2);
#endif
#if SALVIA_CV1K_PROBE
  if(probeNow) {
   const unsigned long long probeT3=salvia_cv1k_tick();
   if(probeT0 && probeStart) salvia_cv1k_probe[12]+=probeStart-probeT0;
   if(probeStart && probeEnd) { salvia_cv1k_probe[6]+=probeEnd-probeStart; ++salvia_cv1k_probe[7]; }
   if(probeEnd && probeT3) salvia_cv1k_probe[13]+=probeT3-probeEnd;
  }
#endif
#if defined(__PS3__) && SH3_PROFILE_SAMPLE
  if((++sh3_prof_samples & 63u) == 0) {
   const unsigned long long now = sh3_tb_now();
   if(sh3_prof_prev_tick) {
    const unsigned long long delta = now - sh3_prof_prev_tick;
    sh3_prof_total_ticks += delta;
    {
    unsigned bucket = 0;
    if ((m_pc & 0xfff000u) == 0x3c0000u) bucket = 1u + ((m_pc >> 4) & 0x3ffu);
    else if ((m_pc & 0xfff000u) == 0x1e3000u) bucket = 0;
    sh3_prof_page_ticks[bucket] += delta;
   }
   }
   sh3_prof_prev_tick = now;
  }
#endif
#if defined(__PS3__) && SH3_PROFILE_SAMPLE
  sh3_dbg_tail_start = sh3_tb_now();
  ++sh3_dbg_exit_hist[(unsigned)completed & 0x0f];
  if((unsigned)completed > 0x0f) ++sh3_dbg_exit_hist[0x10];
  sh3_dbg_tail_ticks += sh3_tb_now() - sh3_dbg_tail_start;
#endif
  if(!completed || ((completed&3)==2 && !serviced)) {
   if(Count) { ++sh3_drc_work.exit_partial; sh3_drc_work.fallback.last_origin=SH3_FB_PARTIAL; }
   return false;
  }
  // These are exactly the outer timerhack loop's eligibility checks. Return
  // to that loop for delay slots, IRQ handling, mode changes or exhausted time.
 } while(Chained && m_sh4_icount>0 && sh3_drc_enabled && !m_delay && !m_test_irq);
 if(Count) ++sh3_drc_work.exit_boundary;
 return true;
}
// Normal callers instantiate no workload bookkeeping and no runtime Count test.
template<bool Chained> static SH3_DISPATCH_INLINE bool sh3_drc_dispatch()
{
 // The CV1000 frame loop enters sh3_drc_dispatch_impl directly, so a probe here
 // would read zero. Probe the chained loop itself when that number is needed.
 return sh3_drc_dispatch_impl<Chained,false>();
}
#undef SH3_DISPATCH_INLINE
#endif
