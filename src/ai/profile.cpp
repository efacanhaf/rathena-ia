// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "profile.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include <ryml.hpp>

#include <common/malloc.hpp>
#include <common/random.hpp>
#include <common/showmsg.hpp>

namespace rathena::server_ai {

namespace {

/// Phase 5 storage variant: stats are still single values, but each
/// equip slot can hold a pool of nameids. profile_get() rolls one per
/// call so visually-distinct shells of the same job spawn from the same
/// (job,tier) entry. Pool of size 1 = behaves exactly like the old
/// scalar profile.
struct ai_profile_pools {
	uint16 base_level = 0, job_level = 0;
	uint16 str = 0, agi = 0, vit = 0, int_ = 0, dex = 0, luk = 0;
	uint16 speed = 0;
	std::vector<uint32> equip[AI_EQ_COUNT];
};

// Hardcoded floor — used when both per-job and Defaults entries are missing.
// Stats only; equipment defaults to empty so status_calc_pc treats the shell
// as a barehanded character of the floor level/stats.
const ai_profile k_floor[3] = {
	// T0
	{ 30, 25, 25, 20, 20, 15, 25, 10, 150, {0,0,0,0,0,0,0,0,0,0} },
	// T1
	{ 60, 45, 50, 40, 40, 30, 50, 20, 150, {0,0,0,0,0,0,0,0,0,0} },
	// T2
	{ 90, 60, 80, 65, 65, 50, 80, 35, 150, {0,0,0,0,0,0,0,0,0,0} },
};

std::array<ai_profile_pools, 3> g_defaults_pools;
std::unordered_map<uint64, ai_profile_pools> g_table; // key = (job << 8) | tier

/// Materialize a pool entry into the wire-format ai_profile by rolling
/// each pool slot (or copying scalar slots) and overlaying stats.
ai_profile pools_to_profile(const ai_profile_pools& src){
	ai_profile out{};
	out.base_level = src.base_level;
	out.job_level  = src.job_level;
	out.str = src.str; out.agi = src.agi; out.vit = src.vit;
	out.int_ = src.int_; out.dex = src.dex; out.luk = src.luk;
	out.speed = src.speed;
	for (int i = 0; i < AI_EQ_COUNT; i++) {
		const auto& pool = src.equip[i];
		if (pool.empty()) {
			out.equip[i] = 0;
		} else if (pool.size() == 1) {
			out.equip[i] = pool[0];
		} else {
			out.equip[i] = pool[rnd() % pool.size()];
		}
	}
	return out;
}

/// Initialize defaults_pools from the hardcoded floors (single-element pools).
void seed_defaults(){
	for (int t = 0; t < 3; t++) {
		ai_profile_pools& dp = g_defaults_pools[t];
		dp.base_level = k_floor[t].base_level;
		dp.job_level  = k_floor[t].job_level;
		dp.str = k_floor[t].str; dp.agi = k_floor[t].agi;
		dp.vit = k_floor[t].vit; dp.int_ = k_floor[t].int_;
		dp.dex = k_floor[t].dex; dp.luk = k_floor[t].luk;
		dp.speed = k_floor[t].speed;
		for (int i = 0; i < AI_EQ_COUNT; i++) dp.equip[i].clear();
	}
}

uint64 key(uint16 job, uint8 tier){ return ((uint64)job << 8) | tier; }

template<typename T>
void read_field(const ryml::NodeRef& n, const char* k, T& dst){
	auto c = n.find_child(c4::to_csubstr(k));
	if (c.valid() == false || !c.has_val()) return;
	int64 v = 0;
	c >> v;
	dst = (T)v;
}

/// Read an Equip slot entry that's either:
///   Weapon: 1158                    # scalar -> pool of size 1
///   Weapon: [1158, 1162, 1170]      # sequence -> pool of size N
/// Anything else is ignored.
void read_slot_pool(const ryml::NodeRef& c, std::vector<uint32>& pool){
	if (c.valid() == false) return;
	if (c.has_val()) {
		int64 v = 0;
		c >> v;
		if (v > 0) {
			pool.clear();
			pool.push_back((uint32)v);
		}
		return;
	}
	if (c.is_seq()) {
		std::vector<uint32> tmp;
		for (const auto& it : c) {
			if (!it.has_val()) continue;
			int64 v = 0;
			it >> v;
			if (v > 0) tmp.push_back((uint32)v);
		}
		if (!tmp.empty()) pool = std::move(tmp);
	}
}

void read_equip(const ryml::NodeRef& parent, ai_profile_pools& p){
	auto eq = parent.find_child(c4::to_csubstr("Equip"));
	if (eq.valid() == false || !eq.is_map()) return;
	struct slot_name { const char* name; ai_equip_slot slot; };
	static const slot_name names[] = {
		{ "HandR",   AI_EQ_HAND_R   },
		{ "Weapon",  AI_EQ_HAND_R   }, // alias
		{ "HeadTop", AI_EQ_HEAD_TOP },
		{ "HeadMid", AI_EQ_HEAD_MID },
		{ "HeadLow", AI_EQ_HEAD_LOW },
		{ "Armor",   AI_EQ_ARMOR    },
		{ "HandL",   AI_EQ_HAND_L   },
		{ "Shield",  AI_EQ_HAND_L   }, // alias
		{ "Garment", AI_EQ_GARMENT  },
		{ "Shoes",   AI_EQ_SHOES    },
		{ "AccR",    AI_EQ_ACC_R    },
		{ "AccL",    AI_EQ_ACC_L    },
	};
	for (const auto& n : names) {
		auto c = eq.find_child(c4::to_csubstr(n.name));
		read_slot_pool(c, p.equip[n.slot]);
	}
}

void read_tier(const ryml::NodeRef& n, ai_profile_pools& p){
	read_field(n, "BaseLevel", p.base_level);
	read_field(n, "JobLevel",  p.job_level);
	read_field(n, "Str", p.str);
	read_field(n, "Agi", p.agi);
	read_field(n, "Vit", p.vit);
	read_field(n, "Int", p.int_);
	read_field(n, "Dex", p.dex);
	read_field(n, "Luk", p.luk);
	read_field(n, "Speed", p.speed);
	read_equip(n, p);
}

}

bool profile_load(const char* yaml_path){
	seed_defaults();
	g_table.clear();

	FILE* f = fopen(yaml_path, "rb");
	if (!f) {
		ShowError("profile: cannot open %s\n", yaml_path);
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
		ShowError("profile: parse failed for %s: %s\n", yaml_path, e.what());
		aFree(buf);
		return false;
	}

	auto root = tree.rootref();

	auto def = root.find_child(c4::to_csubstr("Defaults"));
	if (def.valid()) {
		const char* names[3] = { "T0", "T1", "T2" };
		for (int32 t = 0; t < 3; t++) {
			auto tn = def.find_child(c4::to_csubstr(names[t]));
			if (tn.valid()) read_tier(tn, g_defaults_pools[t]);
		}
	}

	auto body = root.find_child(c4::to_csubstr("Body"));
	int32 job_count = 0, tier_count = 0;
	if (body.valid()) {
		for (const auto& jn : body) {
			auto jc = jn.find_child(c4::to_csubstr("Job"));
			if (jc.valid() == false || !jc.has_val()) continue;
			int32 jv = -1; jc >> jv;
			if (jv < 0) continue;
			uint16 job = (uint16)jv;
			job_count++;
			const char* names[3] = { "T0", "T1", "T2" };
			for (int32 t = 0; t < 3; t++) {
				auto tn = jn.find_child(c4::to_csubstr(names[t]));
				if (tn.valid() == false) continue;
				ai_profile_pools p = g_defaults_pools[t]; // overlay
				read_tier(tn, p);
				g_table[key(job, (uint8)t)] = std::move(p);
				tier_count++;
			}
		}
	}
	aFree(buf);

	ShowStatus("profile: loaded %d jobs, %d (job,tier) entries.\n", job_count, tier_count);
	return true;
}

ai_profile profile_get(uint16 job, uint8 tier){
	if (tier > 2) tier = 2;
	// Exact (job, tier) match wins.
	auto it = g_table.find(key(job, tier));
	if (it != g_table.end()) return pools_to_profile(it->second);
	// Fall back to another tier of the same job before Defaults — keeps
	// the equip slots class-appropriate (Knight T0 uses Knight T1 gear
	// instead of class-agnostic Defaults that skip the spawn for missing
	// weapon).
	for (uint8 alt = 0; alt < 3; alt++) {
		if (alt == tier) continue;
		auto a = g_table.find(key(job, alt));
		if (a != g_table.end()) return pools_to_profile(a->second);
	}
	return pools_to_profile(g_defaults_pools[tier]);
}

}
