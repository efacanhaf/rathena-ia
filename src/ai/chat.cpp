// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "chat.hpp"

#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include <ryml.hpp>

#include <common/malloc.hpp>
#include <common/random.hpp>
#include <common/showmsg.hpp>

namespace rathena::server_ai {

namespace {
std::unordered_map<std::string, std::vector<std::string>> g_categories;
}

bool chat_load(const char* yaml_path){
	g_categories.clear();

	FILE* f = fopen(yaml_path, "rb");
	if (!f) {
		ShowError("chat: cannot open %s\n", yaml_path);
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
		ShowError("chat: parse failed for %s: %s\n", yaml_path, e.what());
		aFree(buf);
		return false;
	}

	auto root = tree.rootref();
	auto body = root.find_child(c4::to_csubstr("Body"));
	if (body.valid() == false) {
		ShowError("chat: %s missing Body.\n", yaml_path);
		aFree(buf);
		return false;
	}

	int32 lines_total = 0;
	for (const auto& bnode : body) {
		auto cg = bnode.find_child(c4::to_csubstr("ChatGlobal"));
		if (cg.valid() == false) continue;
		auto cats = cg.find_child(c4::to_csubstr("Categories"));
		if (cats.valid() == false) continue;
		for (const auto& cat : cats) {
			if (!cat.has_key()) continue;
			auto k = cat.key();
			std::string name(k.str, k.len);
			std::vector<std::string>& bucket = g_categories[name];
			for (const auto& line : cat) {
				if (!line.has_val()) continue;
				auto sv = line.val();
				bucket.emplace_back(sv.str, sv.len);
				lines_total++;
			}
		}
	}
	aFree(buf);

	ShowStatus("chat: loaded %zu categories, %d lines.\n",
		g_categories.size(), lines_total);
	return true;
}

const std::string* chat_pick(const char* category){
	auto it = g_categories.find(category);
	if (it == g_categories.end() || it->second.empty()) return nullptr;
	const auto& v = it->second;
	return &v[rnd() % v.size()];
}

}
