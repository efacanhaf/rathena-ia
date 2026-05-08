// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef AI_SKILL_PICKER_HPP
#define AI_SKILL_PICKER_HPP

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <common/cbasetypes.hpp>
#include <common/timer.hpp> // t_tick

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
	// Phase 6 — mercenary support gates
	OWNER_HP_BELOW,
	OWNER_HP_ABOVE,
	OWNER_SP_BELOW,
	OWNER_STATUS,
	NOT_OWNER_STATUS,
};

/// 0=any, 1=has_target (combat), 2=no_target (roam).
enum class skill_state : uint8_t { ANY = 0, HAS_TARGET = 1, NO_TARGET = 2 };

/// 0=current target, 1=self, 2=ally (nearest BL_PC in range), 3=owner
/// (mercenary), 4=party (mercenary — iterates owner's party roster).
enum class skill_target : uint8_t {
	TARGET = 0, SELF = 1, ALLY = 2, OWNER = 3, PARTY = 4
};

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
	// Phase 6 — per-skill cooldown (ms). 0 = use the support tick's
	// default 3s. Override in YAML for buffs whose SC doesn't get
	// reliably reflected in REPORT (e.g. Offertorium) so we don't
	// re-cast every tick.
	uint32 cooldown_ms = 0;
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
	// Phase 5 — bitmasks of AI_ST_* (defined in common/ai_packets.hpp).
	uint32 self_statuses = 0;
	uint32 target_statuses = 0;
	// Phase 5 — target classification, populated from the picked
	// enemies[] row before evaluating ENEMY_RACE/ELEMENT/IS_BOSS.
	uint8 target_race = 0;       // e_race
	uint8 target_element = 0;    // low nibble = e_element type
	uint8 target_element_lv = 0; // 1..4
	bool  target_is_boss = false;
	bool  target_is_mvp = false;
	// Phase 6 — owner snapshot (mercenary mode). owner_present=false when
	// the shell has no owner OR the owner is offline/zoning; conditions
	// gated on owner state should fail in that case.
	bool   owner_present = false;
	uint16 owner_hp_pct = 0;
	uint16 owner_sp_pct = 0;
	uint32 owner_statuses = 0;
};

/// Phase 6 — re-evaluate a skill's condition gate against an arbitrary
/// shell_ctx. Used by the support tick when iterating party members for
/// Target=PARTY: build a "member as if they were the owner" ctx and
/// call this to decide whether the skill applies to that member.
bool skill_picker_cond_passes(const skill_entry& e, const shell_ctx& ctx);

/// Pick the next skill from the rotation that passes its condition gate
/// and a per-rate roll. cursor is mutated (round-robin start). Returns
/// nullptr if nothing matches.
///
/// Phase 6 — `cooldowns` is an optional per-skill block list (name →
/// expiry-tick). Entries with `cooldowns->at(name) > now` are skipped
/// even when their condition matches; this lets the support tick avoid
/// double-casting buffs while waiting for the SC bit to show up in the
/// next REPORT.
const skill_entry* skill_picker_choose(const skill_rotation& rot,
		const shell_ctx& ctx, size_t* cursor,
		const std::unordered_map<std::string, t_tick>* cooldowns = nullptr,
		t_tick now = 0);

/// Parses a condition name (e.g. "hp_below") to its enum. Returns ALWAYS for
/// the empty/missing case and SC_NONE-equivalent unknown for typos (logged).
skill_cond skill_picker_parse_cond(const std::string& s);

/// Phase 5 — caster jobs that should never fall back to a basic melee
/// attack when the picker can't find a skill (out of SP, on cooldown,
/// no condition match). A wizard whacking with a staff looks broken.
bool skill_picker_is_caster(uint16 job);

}

#endif /* AI_SKILL_PICKER_HPP */
