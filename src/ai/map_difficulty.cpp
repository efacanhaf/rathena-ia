// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "map_difficulty.hpp"

#include <cstdio>
#include <string>
#include <unordered_map>

#include <ryml.hpp>

#include <common/malloc.hpp>
#include <common/showmsg.hpp>

namespace rathena::server_ai {

namespace {

std::unordered_map<std::string, uint16> g_avg_level;

}

bool map_difficulty_load(const char* yaml_path){
	g_avg_level.clear();

	FILE* f = fopen(yaml_path, "rb");
	if (!f) {
		ShowWarning("map_difficulty: cannot open %s — every map will use mid tier.\n", yaml_path);
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
		ShowError("map_difficulty: parse failed: %s\n", e.what());
		aFree(buf);
		return false;
	}

	auto root = tree.rootref();
	auto body = root.find_child(c4::to_csubstr("Body"));
	if (!body.valid()) {
		ShowError("map_difficulty: %s missing Body.\n", yaml_path);
		aFree(buf);
		return false;
	}

	for (const auto& n : body) {
		auto mn = n.find_child(c4::to_csubstr("Map"));
		auto an = n.find_child(c4::to_csubstr("AvgLevel"));
		if (!mn.valid() || !mn.has_val() || !an.valid() || !an.has_val()) continue;
		auto sv = mn.val();
		std::string map(sv.str, sv.len);
		int32 avg = 0;
		an >> avg;
		if (avg <= 0) continue;
		g_avg_level[std::move(map)] = (uint16)avg;
	}
	aFree(buf);

	ShowStatus("map_difficulty: loaded %zu maps.\n", g_avg_level.size());
	return true;
}

uint16 map_difficulty_avg_level(const char* map_name){
	if (!map_name) return 0;
	auto it = g_avg_level.find(map_name);
	return it == g_avg_level.end() ? 0 : it->second;
}

uint8 map_difficulty_tier(const char* map_name){
	uint16 avg = map_difficulty_avg_level(map_name);
	if (avg == 0) return 1;       // unknown — assume mid
	if (avg < 25)  return 0;
	if (avg < 50)  return 1;
	return 2;
}

}
