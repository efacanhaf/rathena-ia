// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "shell_pool.hpp"

#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#include <common/showmsg.hpp>

namespace {

// Bitmap of free slots; 1 = free, 0 = allocated.
std::vector<uint64> g_bitmap;
uint32 g_next_hint = 0;
uint32 g_active = 0;
uint32 g_total = 0;

constexpr uint32 BITS_PER_WORD = 64;
constexpr uint32 WORDS = (SHELL_POOL_SIZE + BITS_PER_WORD - 1) / BITS_PER_WORD;

inline uint32 ctz64(uint64 v){
#if defined(_MSC_VER) && defined(_M_X64)
	unsigned long idx;
	_BitScanForward64(&idx, v);
	return (uint32)idx;
#elif defined(_MSC_VER)
	unsigned long idx;
	if (_BitScanForward(&idx, (uint32)v))
		return (uint32)idx;
	_BitScanForward(&idx, (uint32)(v >> 32));
	return (uint32)idx + 32;
#else
	return (uint32)__builtin_ctzll(v);
#endif
}

}

void do_init_shell_pool(void){
	g_bitmap.assign(WORDS, ~uint64(0));
	// Mask off the slack bits at the tail of the last word.
	uint32 tail = SHELL_POOL_SIZE % BITS_PER_WORD;
	if (tail) {
		g_bitmap.back() &= (uint64(1) << tail) - 1;
	}
	g_next_hint = 0;
	g_active = 0;
	g_total = 0;
	ShowStatus("shell_pool: %u slots ready (%u..%u).\n",
		SHELL_POOL_SIZE, SHELL_ID_MIN, SHELL_ID_MAX - 1);
}

void do_final_shell_pool(void){
	g_bitmap.clear();
	g_bitmap.shrink_to_fit();
	g_active = 0;
}

uint32 shell_pool_alloc(void){
	uint32 start = g_next_hint;
	for (uint32 i = 0; i < WORDS; i++) {
		uint32 w = (start + i) % WORDS;
		uint64 bits = g_bitmap[w];
		if (bits == 0)
			continue;
		uint32 bit = ctz64(bits);
		g_bitmap[w] = bits & ~(uint64(1) << bit);
		g_next_hint = w;
		uint32 slot = w * BITS_PER_WORD + bit;
		if (slot >= SHELL_POOL_SIZE) {
			// Should not happen — slack masked at init — but guard anyway.
			g_bitmap[w] |= (uint64(1) << bit);
			continue;
		}
		g_active++;
		g_total++;
		return SHELL_ID_MIN + slot;
	}
	return 0; // pool exhausted
}

void shell_pool_free(uint32 account_id){
	if (!shell_id_is_virtual(account_id))
		return;
	uint32 slot = account_id - SHELL_ID_MIN;
	uint32 w = slot / BITS_PER_WORD;
	uint32 bit = slot % BITS_PER_WORD;
	uint64 mask = uint64(1) << bit;
	if (g_bitmap[w] & mask)
		return; // already free; double-free guard
	g_bitmap[w] |= mask;
	if (g_active)
		g_active--;
}

uint32 shell_pool_active_count(void){ return g_active; }
uint32 shell_pool_total_allocs(void){ return g_total; }
