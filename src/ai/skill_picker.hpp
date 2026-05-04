// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef AI_SKILL_PICKER_HPP
#define AI_SKILL_PICKER_HPP

#include <cstdint>
#include <string>
#include <vector>

#include <common/cbasetypes.hpp>

namespace rathena::server_ai {

/// Per-skill condition gate. CondValue is either a number (HP% etc.) or a
/// status_change name (parsed lazy at apply-time, the parser keeps the raw
/// string so we don't drag status enums into the ai-server).
enum class skill_cond : uint8_t {
	ALWAYS = 0,
	HP_BELOW,
	SP_BELOW,
	HP_ABOVE,
	ENEMY_HP_BELOW,
	ENEMY_HP_ABOVE,
	ENEMY_STATUS,
	NOT_ENEMY_STATUS,
	SELF_STATUS,
	NOT_SELF_STATUS,
	DISTANCE_BELOW,
	DISTANCE_ABOVE,
	ENEMY_COUNT_NEARBY,
	MAP_ZONE,
	MELEE_ATTACKED,
	RANGE_ATTACKED,
	ALLY_HP_BELOW,
	ALLY_STATUS,
	NOT_ALLY_STATUS,
	HAS_SPHERE,
	ENEMY_HIDDEN,
	ENEMY_CASTING,
	ENEMY_CASTING_GROUND,
	CELL_HAS_SKILL_UNIT,
	SELF_TARGETED,
	ENEMY_ELEMENT,
	ENEMY_RACE,
	ALLY_COUNT_NEARBY,
	ENEMY_IS_BOSS,
	SP_ABOVE,
};

/// 0=any, 1=has_target (combat), 2=no_target (roam).
enum class skill_state : uint8_t { ANY = 0, HAS_TARGET = 1, NO_TARGET = 2 };

/// 0=current target, 1=self, 2=ally (nearest BL_PC in range).
enum class skill_target : uint8_t { TARGET = 0, SELF = 1, ALLY = 2 };

struct skill_entry {
	uint16 skill_id = 0;       // numeric SkillId; resolved at parse time
	std::string skill_name;    // raw name kept for log/diagnostics
	uint8 level = 1;
	uint16 rate = 10000;       // per-10000
	skill_state state = skill_state::ANY;
	skill_target target = skill_target::TARGET;
	skill_cond condition = skill_cond::ALWAYS;
	int32 cond_value_num = 0;
	std::string cond_value_str; // raw SC name when CondValue is a status string
};

struct skill_rotation {
	uint16 job = 0;
	std::vector<skill_entry> skills;
};

/// Loads population_skill_db.yml. Returns false on any parse/IO error.
bool skill_picker_load(const char* yaml_path);

/// Looks up the skill rotation for a given JobId. Returns nullptr if the
/// job has no entry (caller falls back to plain melee).
const skill_rotation* skill_picker_get(uint16 job);

/// Snapshot of the shell context the picker needs to evaluate conditions.
/// Filled from the latest REPORT for that shell. All fields zero-default.
struct shell_ctx {
	uint32 hp = 0, max_hp = 0;
	uint32 sp = 0, max_sp = 0;
	bool has_target = false;
	uint16 target_hp_pct = 0;
	uint8  target_distance = 0;
	uint8  enemy_count_nearby = 0;
	uint8  ally_count_nearby = 0;
	uint8  map_zone = 0; // 1=town,2=field,3=dungeon (Phase 3)
};

/// Pick the next skill from the rotation that passes its condition gate
/// and a per-rate roll. cursor is mutated (round-robin start). Returns
/// nullptr if nothing matches.
const skill_entry* skill_picker_choose(const skill_rotation& rot,
		const shell_ctx& ctx, size_t* cursor);

/// Parses a condition name (e.g. "hp_below") to its enum. Returns ALWAYS for
/// the empty/missing case and SC_NONE-equivalent unknown for typos (logged).
skill_cond skill_picker_parse_cond(const std::string& s);

}

#endif /* AI_SKILL_PICKER_HPP */
