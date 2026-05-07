// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder
//
// Phase 4: per-job, per-tier shell profiles. Loaded from
// db/ai/population_profile.yml. The map-server's aishell_create reads these
// via the spawn packet and feeds them into status_calc_pc — same canonical
// path real players use after pc_authok. No HP/Atk/Def hardcoding.

#ifndef AI_PROFILE_HPP
#define AI_PROFILE_HPP

#include <common/cbasetypes.hpp>

namespace rathena::server_ai {

// Equipment slot order in ai_profile.equip[] and PACKET_AI_SHELL_SPAWN_S.equip[].
// Stable wire format — append-only.
enum ai_equip_slot : uint8 {
	AI_EQ_HAND_R   = 0, // weapon (2H weapons set HAND_L too via item_data->equip)
	AI_EQ_HEAD_TOP = 1,
	AI_EQ_HEAD_MID = 2,
	AI_EQ_HEAD_LOW = 3,
	AI_EQ_ARMOR    = 4,
	AI_EQ_HAND_L   = 5, // shield
	AI_EQ_GARMENT  = 6,
	AI_EQ_SHOES    = 7,
	AI_EQ_ACC_R    = 8,
	AI_EQ_ACC_L    = 9,
	AI_EQ_COUNT    = 10,
};

struct ai_profile {
	uint16 base_level;
	uint16 job_level;
	uint16 str, agi, vit, int_, dex, luk;
	uint16 speed;
	uint32 equip[AI_EQ_COUNT]; // item nameid per slot, 0 = empty
};

bool profile_load(const char* yaml_path);

/// Look up profile by (job, tier). tier is clamped to [0,2]. Falls back to
/// the Defaults entry if (job, tier) wasn't defined; if defaults are missing
/// too, returns a hardcoded floor.
///
/// Phase 5 — equipment slots that have a pool (YAML sequence) are rolled
/// per-call, so two consecutive calls for the same (job,tier) can return
/// different equip[] arrays. Stats and pool-less slots are deterministic.
/// Returned by value so a re-roll between calls can't invalidate a
/// previously-held reference.
ai_profile profile_get(uint16 job, uint8 tier);

/// Phase 5 — does (job, tier) have an exact entry in population_profile.yml
/// (no fallback)? Spawner uses this to skip spawning jobs whose profile
/// doesn't cover the map's difficulty tier — better no shell than a
/// lvl-35 High Mage in a lvl-80 field.
bool profile_has_exact(uint16 job, uint8 tier);

}

#endif /* AI_PROFILE_HPP */
