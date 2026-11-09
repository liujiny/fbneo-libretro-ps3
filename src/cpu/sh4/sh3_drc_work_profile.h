// Main emulation thread only. Integer workload counts, never per-opcode clocks.
// Only the explicit sampled specialization touches this storage in hot loops.
#ifndef FBNEO_SH3_DRC_WORK_PROFILE_H
#define FBNEO_SH3_DRC_WORK_PROFILE_H
#include <string.h>
#if defined(_MSC_VER)
typedef unsigned __int64 Sh3WorkCount;
#else
typedef unsigned long long Sh3WorkCount;
#endif
#include "sh3_fallback_detail.h"
#include "sh3_idle_candidate.h"
struct Sh3DrcWorkProfile {
 Sh3WorkCount frames, slices, cpu_off_slices, normal_mode_slices;
 Sh3WorkCount dispatch_calls, lookups, rebuilds, validation_spans, validation_words;
 // Why a compile() was requested. `conflict` is a tag/source-pointer miss (the
 // set could not hold this PC), `source` is a same-PC snapshot mismatch (the
 // guest rewrote or re-uploaded the opcodes), `map` is a read-map change only.
 // Written on the compile path, never from generated code or the hot loop.
 Sh3WorkCount rebuild_conflict, rebuild_source, rebuild_map;
 // Why the blocks that actually ran ended, counted per entry (work-sampled
 // builds). In order: the instruction window ran out, a PC-relative jump
 // (BRA/BSR), a register-indirect transfer, a delayed conditional, a
 // self-loop conditional, an unsupported opcode, and the delayed conditionals
 // split by whether their delay slot is one the emitter could continue
 // through (T-safe, guard-free) or not.
 Sh3WorkCount block_ends[8];
 // Code-arena pressure. `arena_recycles` counts full-arena resets caused by the
 // overflow guard; `arena_peak` is the highest word count reached since the
 // last report. Both are written on the compile path only, never from
 // generated code or the dispatched hot loop.
 Sh3WorkCount arena_recycles, arena_peak;
 // The arena is a ring of sectors: entering a sector whose space is handed out
 // again invalidates the block-table slots that held its previous code.
 // `arena_evictions` counts those reuses that actually dropped slots and
 // `arena_evicted_slots` how many records they cleared. Both are compile-path
 // only, like the counters above.
 Sh3WorkCount arena_evictions, arena_evicted_slots;
 // Guest stores the emitted blocks perform, weighted by how often each block
 // ran. Every generated guest store registers one code-write guard, so the
 // compile-time count is exact; the array that carries it per slot is a
 // diagnostic and only sampled builds read it in the dispatch loop.
 Sh3WorkCount block_stores;
 // Write-stamp cross-check, work-sampled entries only. `stamp_entries` counts
 // the validations the check ran on, `stamp_clean` those whose block's two
 // 64-byte lines had not been written since the dispatcher last cleared them
 // (exactly the validations a clean stamp pair can remove), and `stamp_dirty`
 // the rest. `stamp_missed` is the dangerous case: the stamps were clean while
 // the snapshot comparison said the bytes had changed, which would mean a
 // write path is not hooked. It must stay zero for the skip to be safe.
 Sh3WorkCount stamp_entries, stamp_clean, stamp_dirty, stamp_missed;
 // Block-link sizing probe, work-sampled entries only. `link_entries` is the
 // number of dispatcher entries observed, `link_sequential` those that directly
 // follow a block which ended at its own sequential completion, i.e. exactly
 // the population a successor fast path could enter without a lookup.
 // `lookup_sets` and `lookup_sets_touched` report how much of the set array the
 // workload reads (one 20-byte set per lookup): the footprint the fast path
 // would stop touching.
 Sh3WorkCount link_entries, link_sequential, lookup_sets, lookup_sets_touched;
 // Block-link gate probe (SALVIA_CV1K_LINK_PROBE >= 2), work-sampled entries
 // only. `link2_steps` are the entries that follow another entry inside the
 // same chain call -- the position a link can serve at all. `link2_rom_entries`
 // are those whose bytes live outside every writable mapping, so no store the
 // emulator executes can change them (`link2_ram_entries` the rest).
 // `link2_rec_*` model a link stored in the predecessor's block-table record,
 // `link2_set_*` one in a compact table indexed by hash(predecessor pc), with
 // collisions: `hit` is the prediction landing on the right pc, `page` that the
 // fetch page still matches, `ok` that the recorded code is still resident in
 // its arena sector. `link2_rom_ok`/`link2_set_rom_ok` are the entries that were
 // both immutable and fully predicted -- the population that needs no
 // store-side signal. `link2_stale` and `link2_rom_changed` are the dangerous
 // directions (checks passed while the snapshot says the bytes changed) and
 // must both stay zero.
 Sh3WorkCount link2_steps, link2_rom_entries, link2_ram_entries;
 Sh3WorkCount link2_rec_hit, link2_rec_page, link2_rec_ok, link2_rom_ok;
 Sh3WorkCount link2_set_hit, link2_set_page, link2_set_ok, link2_set_rom_ok;
 Sh3WorkCount link2_stale, link2_rom_changed;
 // Shadow-record probe: a one-way copy of a block's record, indexed by the same
 // set index and tagged with its pc, that the dispatcher could read instead of
 // the record itself. It carries the snapshot too, so the checks it serves are
 // the existing ones verbatim -- there is no store-side question to answer.
 // `link2_sh_hit` is the share of entries the shadow holds, `link2_sh_ok` those
 // whose bytes still match the shadow's snapshot (the share that would be
 // served), and `link2_sh_stale` the dangerous direction, which must be zero.
 Sh3WorkCount link2_sh_hit, link2_sh_ok, link2_sh_stale;
 // Footprint probes: distinct 64-byte lines touched by this window's entries in
 // the record array as it is today (84 bytes per record), in the record the
 // hot/snapshot split would leave behind (16 bytes of hot fields), and in the
 // guest source bytes. Reported as bytes at report time.
 Sh3WorkCount link2_hot_lines, link2_rec_lines, link2_src_lines;
 // Chain probe: entries that ran while no guest store had executed since the
 // chain call started (clean, i.e. linkable in principle) against the rest.
 Sh3WorkCount chain_clean_entries, chain_dirty_entries;
 Sh3WorkCount native_calls, native_cycles, interpreter_steps, interpreter_cycles;
 Sh3WorkCount movll_services, movll_service_cycles, movll_service_rejects;
 Sh3WorkCount device_services[2], device_service_cycles[2], device_fallbacks[2];
 Sh3WorkCount exit_gate, exit_fetch, exit_no_entry, exit_budget, exit_partial, exit_boundary;
 // Entries the shadow dispatch record answered, i.e. the entries that skipped
 // the set lookup and the record read because a copy of the record was already
 // in hand (docs/optimization/2026-10-10-cv1k-link-gate.md). Every other entry
 // took the record path and refreshed the copy.
 Sh3WorkCount shadow_hits;
 // How often the successor a copy predicted (its `next_pc`, which the prefetch
 // is aimed at) was not the one the block actually ran into. Diagnostic only:
 // a miss costs the fetch, never an answer.
 Sh3WorkCount shadow_pred_miss;
 // Requested snapshot lengths, not necessarily executed opcode counts.
 Sh3WorkCount snapshot_lengths[34];
 // High-byte opcode families actually sent to the interpreter in sampled frames.
 Sh3WorkCount interpreter_hi8[256];
#if defined(SH3_PPC_DRC_WORK_TEST)
 // Host differential builds only: exact 16-bit histogram of the opcodes the
 // interpreter fallback sites consume. Diagnostic width, never in the console
 // image, and the increment is compiled out of the console work-sample path.
 Sh3WorkCount interpreter_op16[65536];
#endif
 Sh3FallbackDetail fallback;
 Sh3IdleCandidates idle_candidates;
 void clear() { memset(this,0,sizeof(*this)); }
};
static Sh3DrcWorkProfile sh3_drc_work;
#endif
