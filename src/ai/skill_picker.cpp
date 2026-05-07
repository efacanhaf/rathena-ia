// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "skill_picker.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <ryml.hpp>

#include <common/ai_packets.hpp>
#include <common/malloc.hpp>
#include <common/random.hpp>
#include <common/showmsg.hpp>

namespace rathena::server_ai {

namespace {

std::unordered_map<uint16, skill_rotation> g_rotations;

/// Phase 5 — name → AI_ST_* bit lookup. Case-insensitive (parser
/// lowercases the YAML cond_value before lookup). MVP set; add bits to
/// ai_packets.hpp + new entries here when extending.
const std::unordered_map<std::string, uint32> g_status_bit = {
	{ "stone",      AI_ST_STONE },
	{ "freeze",     AI_ST_FREEZE },
	{ "stun",       AI_ST_STUN },
	{ "sleep",      AI_ST_SLEEP },
	{ "silence",    AI_ST_SILENCE },
	{ "curse",      AI_ST_CURSE },
	{ "confusion",  AI_ST_CONFUSION },
	{ "blind",      AI_ST_BLIND },
	{ "poison",     AI_ST_POISON },
	{ "bleeding",   AI_ST_BLEEDING },
	{ "hiding",     AI_ST_HIDING },
	{ "cloaking",   AI_ST_CLOAKING },
	{ "endure",     AI_ST_ENDURE },
	{ "provoke",    AI_ST_PROVOKE },
	{ "autoguard",  AI_ST_AUTOGUARD },
	{ "increase_agi", AI_ST_INC_AGI },
	{ "inc_agi",    AI_ST_INC_AGI },
};

/// Phase 5 — known passive skills. The map-server's CAST handler drops
/// these when shells try to cast them, but the ai-server still consumes
/// the 5s cast cooldown thinking it succeeded — so the shell stands idle
/// instead of attacking. We strip them at load time so the picker never
/// returns one. Only includes 1st/trans-1st/2-1/2-2 base skills that
/// shipped in population_skill_db.yml; extend when adding new rotations.
const std::unordered_set<std::string> g_passive_skills = {
	// Swordsman tree
	"SM_SWORD", "SM_TWOHAND", "SM_RECOVERY", "SM_HP",
	// Mage tree
	"MG_SRECOVERY",
	// Archer tree
	"AC_OWL",          // active in some clients but no-op for shells
	"AC_VULTURE",
	// Acolyte tree
	"AL_DP", "AL_DEMONBANE",
	// Merchant tree
	"MC_INCCARRY", "MC_DISCOUNT", "MC_OVERCHARGE", "MC_PUSHCART",
	"MC_VENDING",   // can't auto-vend
	// Thief tree
	"TF_HIDING",    // toggle, but useless in shell rotation
	"TF_MISS", "TF_DOUBLE", "TF_STEAL",
	// Knight
	"KN_RIDING", "KN_CAVALIERMASTERY", "KN_TWOHANDQUICKEN",
	// Crusader
	"CR_TRUST",
	// Hunter
	"HT_BEASTBANE", "HT_FALCON", "HT_STEELCROW",
	// Sage
	"SA_ADVANCEDBOOK", "SA_FREECAST", "SA_DRAGONOLOGY",
	// Wizard
	// (no notable passives in the shipped rotation)
	// Priest
	"PR_MACEMASTERY",
	// Blacksmith
	"BS_HILTBINDING", "BS_WEAPONRESEARCH", "BS_IRON",
	// Assassin
	"AS_CLOAKING", // toggle — shells shouldn't auto-cloak mid-fight
	"AS_RIGHT", "AS_LEFT",
	// Rogue
	"RG_TUNNELDRIVE", "RG_GANGSTER", "RG_COMPULSION", "RG_PLAGIARISM",
	// Alchemist
	"AM_TWILIGHT1", "AM_TWILIGHT2", "AM_TWILIGHT3",
	// Bard / Dancer
	"BA_MUSICALLESSON", "DC_DANCINGLESSON",
};
static bool is_passive(const std::string& name){
	return g_passive_skills.find(name) != g_passive_skills.end();
}

/// Pure caster jobs — never auto-attack. If they're out of SP / on cooldown /
/// no condition matched, they stand and wait (the engagement clears next
/// tick when the mob walks away or dies).
const std::unordered_set<uint16> g_caster_jobs = {
	2,    // Mage
	4003, // High Mage
	10,   // Wizard
	4010, // High Wizard
	16,   // Sage
	4017, // Professor
	8,    // Priest
	4014, // High Priest
	// 3rd classes
	4047, // Warlock
	4049, // Arch Bishop
	4053, // Sorcerer
};

static uint32 status_name_to_bit(const std::string& name){
	std::string lc = name;
	for (char& c : lc) c = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
	auto it = g_status_bit.find(lc);
	return (it == g_status_bit.end()) ? 0u : it->second;
}

const std::unordered_map<std::string, skill_cond> g_cond_table = {
	{ "always",               skill_cond::ALWAYS },
	{ "hp_below",             skill_cond::HP_BELOW },
	{ "sp_below",             skill_cond::SP_BELOW },
	{ "hp_above",             skill_cond::HP_ABOVE },
	{ "enemy_hp_below",       skill_cond::ENEMY_HP_BELOW },
	{ "enemy_hp_above",       skill_cond::ENEMY_HP_ABOVE },
	{ "enemy_status",         skill_cond::ENEMY_STATUS },
	{ "not_enemy_status",     skill_cond::NOT_ENEMY_STATUS },
	{ "self_status",          skill_cond::SELF_STATUS },
	{ "not_self_status",      skill_cond::NOT_SELF_STATUS },
	{ "distance_below",       skill_cond::DISTANCE_BELOW },
	{ "distance_above",       skill_cond::DISTANCE_ABOVE },
	{ "enemy_count_nearby",   skill_cond::ENEMY_COUNT_NEARBY },
	{ "map_zone",             skill_cond::MAP_ZONE },
	{ "melee_attacked",       skill_cond::MELEE_ATTACKED },
	{ "range_attacked",       skill_cond::RANGE_ATTACKED },
	{ "ally_hp_below",        skill_cond::ALLY_HP_BELOW },
	{ "ally_status",          skill_cond::ALLY_STATUS },
	{ "not_ally_status",      skill_cond::NOT_ALLY_STATUS },
	{ "has_sphere",           skill_cond::HAS_SPHERE },
	{ "enemy_hidden",         skill_cond::ENEMY_HIDDEN },
	{ "enemy_casting",        skill_cond::ENEMY_CASTING },
	{ "enemy_casting_ground", skill_cond::ENEMY_CASTING_GROUND },
	{ "cell_has_skill_unit",  skill_cond::CELL_HAS_SKILL_UNIT },
	{ "self_targeted",        skill_cond::SELF_TARGETED },
	{ "enemy_element",        skill_cond::ENEMY_ELEMENT },
	{ "enemy_race",           skill_cond::ENEMY_RACE },
	{ "ally_count_nearby",    skill_cond::ALLY_COUNT_NEARBY },
	{ "enemy_is_boss",        skill_cond::ENEMY_IS_BOSS },
	{ "sp_above",             skill_cond::SP_ABOVE },
};

std::string read_str(const ryml::NodeRef& node, const char* key,
		const std::string& def = ""){
	auto child = node.find_child(c4::to_csubstr(key));
	if (child.valid() == false || !child.has_val()) return def;
	auto sv = child.val();
	return std::string(sv.str, sv.len);
}

int32 read_int(const ryml::NodeRef& node, const char* key, int32 def){
	auto child = node.find_child(c4::to_csubstr(key));
	if (child.valid() == false || !child.has_val()) return def;
	int32 v = def;
	child >> v;
	return v;
}

bool starts_with_digit(const std::string& s){
	return !s.empty() && (s[0] >= '0' && s[0] <= '9');
}

}

skill_cond skill_picker_parse_cond(const std::string& s){
	if (s.empty()) return skill_cond::ALWAYS;
	auto it = g_cond_table.find(s);
	if (it == g_cond_table.end()) {
		ShowWarning("skill_picker: unknown condition '%s'.\n", s.c_str());
		return skill_cond::ALWAYS;
	}
	return it->second;
}

bool skill_picker_load(const char* yaml_path){
	g_rotations.clear();

	FILE* f = fopen(yaml_path, "rb");
	if (!f) {
		ShowError("skill_picker: cannot open %s\n", yaml_path);
		return false;
	}
	fseek(f, 0, SEEK_END);
	size_t size = ftell(f);
	rewind(f);
	char* buf = (char*)aMalloc(size + 1);
	size_t got = fread(buf, 1, size, f);
	buf[got] = '\0';
	fclose(f);

	ryml::Tree tree;
	try {
		tree = ryml::parse_in_arena(c4::to_csubstr(yaml_path), c4::to_csubstr(buf));
	} catch (const std::runtime_error& e) {
		ShowError("skill_picker: parse failed for %s: %s\n", yaml_path, e.what());
		aFree(buf);
		return false;
	}

	auto root = tree.rootref();
	auto body = root.find_child(c4::to_csubstr("Body"));
	if (body.valid() == false) {
		ShowError("skill_picker: %s missing Body.\n", yaml_path);
		aFree(buf);
		return false;
	}

	int32 jobs = 0, total_skills = 0, dropped_passives = 0;
	for (const auto& job_node : body) {
		uint16 job = (uint16)read_int(job_node, "JobId", 0);
		auto skills_node = job_node.find_child(c4::to_csubstr("Skills"));
		if (skills_node.valid() == false) continue;

		skill_rotation rot;
		rot.job = job;
		for (const auto& sk : skills_node) {
			skill_entry e;
			e.skill_name = read_str(sk, "SkillId");
			if (is_passive(e.skill_name)) {
				dropped_passives++;
				continue;	// map-server's CAST handler drops these; if the
							// picker returns one, the shell burns a 5s
							// cooldown standing idle.
			}
			if (starts_with_digit(e.skill_name)) {
				e.skill_id = (uint16)std::atoi(e.skill_name.c_str());
			}
			e.level = (uint8)read_int(sk, "Level", 1);
			e.rate  = (uint16)read_int(sk, "Rate", 10000);
			e.state  = (skill_state)read_int(sk, "State", 0);
			e.target = (skill_target)read_int(sk, "Target", 0);

			std::string cond = read_str(sk, "Condition", "");
			e.condition = skill_picker_parse_cond(cond);

			std::string cv = read_str(sk, "CondValue", "");
			if (!cv.empty()) {
				if (starts_with_digit(cv) || cv[0] == '-') {
					e.cond_value_num = std::atoi(cv.c_str());
				} else {
					e.cond_value_str = cv;
				}
			}
			rot.skills.push_back(std::move(e));
			total_skills++;
		}
		g_rotations[job] = std::move(rot);
		jobs++;
	}
	aFree(buf);

	ShowStatus("skill_picker: loaded %d job rotations, %d skill entries (%d passives stripped).\n",
		jobs, total_skills, dropped_passives);
	return true;
}

bool skill_picker_is_caster(uint16 job){
	return g_caster_jobs.find(job) != g_caster_jobs.end();
}

const skill_rotation* skill_picker_get(uint16 job){
	auto it = g_rotations.find(job);
	return it == g_rotations.end() ? nullptr : &it->second;
}

/// Evaluate one condition against the shell context. Returns true if the
/// gate passes. Status-name conditions (enemy_status/self_status/ally_status)
/// always pass for now — Phase 2 doesn't carry SC info in REPORT yet, so
/// they're treated as no-op satisfied; refine in Phase 3.
static bool cond_passes(const skill_entry& e, const shell_ctx& ctx){
	auto pct = [](uint32 cur, uint32 max) -> int32 {
		if (max == 0) return 0;
		return (int32)((uint64)cur * 100 / max);
	};
	switch (e.condition) {
		case skill_cond::ALWAYS:           return true;
		case skill_cond::HP_BELOW:         return pct(ctx.hp, ctx.max_hp) < e.cond_value_num;
		case skill_cond::HP_ABOVE:         return pct(ctx.hp, ctx.max_hp) > e.cond_value_num;
		case skill_cond::SP_BELOW:         return pct(ctx.sp, ctx.max_sp) < e.cond_value_num;
		case skill_cond::SP_ABOVE:         return pct(ctx.sp, ctx.max_sp) > e.cond_value_num;
		case skill_cond::ENEMY_HP_BELOW:   return ctx.has_target && ctx.target_hp_pct < e.cond_value_num;
		case skill_cond::ENEMY_HP_ABOVE:   return ctx.has_target && ctx.target_hp_pct > e.cond_value_num;
		case skill_cond::DISTANCE_BELOW:   return ctx.has_target && ctx.target_distance <= e.cond_value_num;
		case skill_cond::DISTANCE_ABOVE:   return ctx.has_target && ctx.target_distance >  e.cond_value_num;
		case skill_cond::ENEMY_COUNT_NEARBY: return ctx.enemy_count_nearby >= e.cond_value_num;
		case skill_cond::ALLY_COUNT_NEARBY: return ctx.ally_count_nearby  >= e.cond_value_num;
		case skill_cond::MAP_ZONE:         return ctx.map_zone == e.cond_value_num;
		// Phase 5 — wire status conditions against the AI_ST_* bitmask
		// from REPORT. Names that don't resolve (status not yet in the
		// MVP set) fall through with the safe default of "not asserted".
		case skill_cond::ENEMY_STATUS: {
			if (!ctx.has_target) return false;
			uint32 bit = status_name_to_bit(e.cond_value_str);
			if (bit == 0) return false; // unknown name → never matches
			return (ctx.target_statuses & bit) != 0;
		}
		case skill_cond::NOT_ENEMY_STATUS: {
			if (!ctx.has_target) return true; // no target → can't be statused
			uint32 bit = status_name_to_bit(e.cond_value_str);
			if (bit == 0) return true; // unknown name → assume "not present"
			return (ctx.target_statuses & bit) == 0;
		}
		case skill_cond::SELF_STATUS: {
			uint32 bit = status_name_to_bit(e.cond_value_str);
			if (bit == 0) return false;
			return (ctx.self_statuses & bit) != 0;
		}
		case skill_cond::NOT_SELF_STATUS: {
			uint32 bit = status_name_to_bit(e.cond_value_str);
			if (bit == 0) return true;
			return (ctx.self_statuses & bit) == 0;
		}
		// Phase 5 — target classification from REPORT.enemies[].
		case skill_cond::ENEMY_HIDDEN:
			return ctx.has_target &&
			       (ctx.target_statuses & (AI_ST_HIDING | AI_ST_CLOAKING)) != 0;
		case skill_cond::ENEMY_RACE:
			return ctx.has_target && (int32)ctx.target_race == e.cond_value_num;
		case skill_cond::ENEMY_ELEMENT:
			return ctx.has_target && (int32)ctx.target_element == e.cond_value_num;
		case skill_cond::ENEMY_IS_BOSS:
			// e.cond_value_num: 0 = miniboss-or-mvp, 1 = mvp-only,
			// anything else (default-when-omitted) treats as miniboss-or-mvp.
			if (!ctx.has_target) return false;
			if (e.cond_value_num == 1) return ctx.target_is_mvp;
			return ctx.target_is_boss;
		// Phase 3 stubs (ally tracking + caster-only state not in REPORT yet).
		case skill_cond::ALLY_STATUS:
		case skill_cond::NOT_ALLY_STATUS:
		case skill_cond::ALLY_HP_BELOW:
		case skill_cond::HAS_SPHERE:
		case skill_cond::ENEMY_CASTING:
		case skill_cond::ENEMY_CASTING_GROUND:
		case skill_cond::CELL_HAS_SKILL_UNIT:
		case skill_cond::SELF_TARGETED:
		case skill_cond::MELEE_ATTACKED:
		case skill_cond::RANGE_ATTACKED:
			return true;
	}
	return true;
}

const skill_entry* skill_picker_choose(const skill_rotation& rot,
		const shell_ctx& ctx, size_t* cursor){
	if (rot.skills.empty()) return nullptr;
	size_t n = rot.skills.size();
	size_t start = (cursor != nullptr) ? (*cursor % n) : 0;
	for (size_t i = 0; i < n; i++) {
		size_t idx = (start + i) % n;
		const skill_entry& e = rot.skills[idx];
		if (e.rate == 0) continue;
		// State filter: HAS_TARGET needs a target, NO_TARGET forbids one.
		if (e.state == skill_state::HAS_TARGET && !ctx.has_target) continue;
		if (e.state == skill_state::NO_TARGET  &&  ctx.has_target) continue;
		if (!cond_passes(e, ctx)) continue;
		// Per-10000 rate roll.
		if (e.rate < 10000 && (int32)(rnd() % 10000) >= e.rate) continue;
		if (cursor != nullptr) *cursor = (idx + 1) % n;
		return &e;
	}
	return nullptr;
}

}
