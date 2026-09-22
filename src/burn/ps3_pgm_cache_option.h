#ifndef FBNEO_PS3_PGM_CACHE_OPTION_H
#define FBNEO_PS3_PGM_CACHE_OPTION_H

#if defined(__PS3__) && defined(__LIBRETRO__)
/* Requested policy only. Read by cache builders during game initialization;
 * changing it must never invalidate an active game's cache pointers. */
extern bool ps3_pgm_hdd_cache_requested;
#endif

#endif
