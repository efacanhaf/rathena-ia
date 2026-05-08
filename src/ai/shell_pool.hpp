// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder
//
// Virtual account_id allocator for AI shells.
// Range [SHELL_ID_MIN, SHELL_ID_MAX) is reserved across the whole cluster
// and serves both as a free-list bitmap and as the implicit "is AI shell"
// flag (no need to add a bit to map_session_data).

#ifndef AI_SHELL_POOL_HPP
#define AI_SHELL_POOL_HPP

#include <cstdint>

#include <common/cbasetypes.hpp>

constexpr uint32 SHELL_ID_MIN = 95'000'000;
constexpr uint32 SHELL_ID_MAX = 100'000'000;
constexpr uint32 SHELL_POOL_SIZE = SHELL_ID_MAX - SHELL_ID_MIN; // 5_000_000 ids

inline bool shell_id_is_virtual(uint32 account_id){
	return account_id >= SHELL_ID_MIN && account_id < SHELL_ID_MAX;
}

void do_init_shell_pool(void);
void do_final_shell_pool(void);

/// Allocate a fresh virtual account_id, or 0 if pool exhausted.
uint32 shell_pool_alloc(void);

/// Return an id to the pool.
void shell_pool_free(uint32 account_id);

uint32 shell_pool_active_count(void);
uint32 shell_pool_total_allocs(void);

#endif /* AI_SHELL_POOL_HPP */
