// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "profile.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

#include <ryml.hpp>

#include <common/malloc.hpp>
#include <common/showmsg.hpp>

namespace rathena::server_ai {

namespace {

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

std::array<ai_profile, 3> g_defaults = {{ k_floor[0], k_floor[1], k_floor[2] }};
std::unordered_map<uint64, ai_profile> g_table; // key = (job << 8) | tier

uint64 key(uint16 job, uint8 tier){ return ((uint64)job << 8) | tier; }

template<typename T>
void read_field(const ryml::NodeRef& n, const char* k, T& dst){
	auto c = n.find_child(c4::to_csubstr(k));
	if (c.valid() == false || !c.has_val()) return;
	int64 v = 0;
	c >> v;
	dst = (T)v;
}

void read_equip(const ryml::NodeRef& parent, ai_profile& p){
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
		if (c.valid() == false || !c.has_val()) continue;
		int64 v = 0;
		c >> v;
		if (v > 0) p.equip[n.slot] = (uint32)v;
	}
}

void read_tier(const ryml::NodeRef& n, ai_profile& p){
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
	g_defaults = {{ k_floor[0], k_floor[1], k_floor[2] }};
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
			if (tn.valid()) read_tier(tn, g_defaults[t]);
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
				ai_profile p = g_defaults[t]; // start from defaults, overlay overrides
				read_tier(tn, p);
				g_table[key(job, (uint8)t)] = p;
				tier_count++;
			}
		}
	}
	aFree(buf);

	ShowStatus("profile: loaded %d jobs, %d (job,tier) entries.\n", job_count, tier_count);
	return true;
}

const ai_profile& profile_get(uint16 job, uint8 tier){
	if (tier > 2) tier = 2;
	// Exact (job, tier) match wins.
	auto it = g_table.find(key(job, tier));
	if (it != g_table.end()) return it->second;
	// Fall back to another tier of the same job before Defaults — keeps the
	// equip slots class-appropriate (Knight T0 uses Knight T1 gear instead
	// of class-agnostic Defaults that skip the spawn for missing weapon).
	for (uint8 alt = 0; alt < 3; alt++) {
		if (alt == tier) continue;
		auto a = g_table.find(key(job, alt));
		if (a != g_table.end()) return a->second;
	}
	return g_defaults[tier];
}

}
