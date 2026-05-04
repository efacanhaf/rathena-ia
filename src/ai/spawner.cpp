// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "spawner.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>

#include <ryml.hpp>

#include <common/malloc.hpp>
#include <common/showmsg.hpp>

namespace {

std::vector<spawn_target> g_targets;

/// Slice TotalPopulation across `maps` using floor + remainder so the sum
/// matches exactly. Each map gets floor(total/n); the first (total % n) maps
/// get +1.
void distribute(uint16 job, const std::vector<std::string>& maps, int32 total,
		spawn_category cat){
	if (maps.empty() || total <= 0) return;
	int32 n = (int32)maps.size();
	int32 base = total / n;
	int32 extra = total % n;
	for (int32 i = 0; i < n; i++) {
		int32 c = base + (i < extra ? 1 : 0);
		if (c <= 0) continue;
		g_targets.push_back({job, maps[i], (uint16)c, cat});
	}
}

void read_string_list(const ryml::NodeRef& parent, const char* key,
		std::vector<std::string>& out){
	if (parent.find_child(c4::to_csubstr(key)).valid() == false)
		return;
	for (const auto& m : parent[c4::to_csubstr(key)]) {
		if (!m.has_val()) continue;
		auto sv = m.val();
		out.emplace_back(sv.str, sv.len);
	}
}

int32 read_int(const ryml::NodeRef& parent, const char* key, int32 def){
	auto child = parent.find_child(c4::to_csubstr(key));
	if (child.valid() == false || !child.has_val()) return def;
	int32 v = def;
	child >> v;
	return v;
}

}

bool spawner_load(const char* yaml_path, int32 cap_total){
	g_targets.clear();

	FILE* f = fopen(yaml_path, "rb");
	if (!f) {
		ShowError("spawner: cannot open %s\n", yaml_path);
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
		ShowError("spawner: parse failed for %s: %s\n", yaml_path, e.what());
		aFree(buf);
		return false;
	}

	auto root = tree.rootref();
	auto body_idx = root.find_child(c4::to_csubstr("Body"));
	if (body_idx.valid() == false) {
		ShowError("spawner: %s missing Body.\n", yaml_path);
		aFree(buf);
		return false;
	}

	int32 jobs_seen = 0;
	for (const auto& job_node : body_idx) {
		uint16 job = (uint16)read_int(job_node, "Job", 0);

		std::vector<std::string> towns, fields, dungeons;
		read_string_list(job_node, "Towns", towns);
		read_string_list(job_node, "Fields", fields);
		read_string_list(job_node, "Dungeons", dungeons);

		// Phase 1.5c: only towns are loaded for the first rollout. Fields
		// and dungeons get enabled once we have walkable-cell sampling.
		distribute(job, towns, read_int(job_node, "TownsPopulation", 0), spawn_category::TOWN);
		(void)fields; (void)dungeons;
		jobs_seen++;
	}
	aFree(buf);

	int32 total = 0;
	for (auto& t : g_targets) total += t.count;

	if (cap_total > 0 && total > cap_total) {
		// Scale every target proportionally so we hit exactly cap_total.
		double k = (double)cap_total / (double)total;
		int32 new_total = 0;
		for (auto& t : g_targets) {
			t.count = (uint16)((double)t.count * k + 0.5);
			new_total += t.count;
		}
		ShowStatus("spawner: scaled population %d -> %d (cap=%d).\n", total, new_total, cap_total);
		total = new_total;
	}

	// Shuffle so a hard cap doesn't empty the pool on the first job/map only.
	std::mt19937 rng{ std::random_device{}() };
	std::shuffle(g_targets.begin(), g_targets.end(), rng);

	ShowStatus("spawner: loaded %d jobs, %zu (job,map) targets, %d total shells.\n",
		jobs_seen, g_targets.size(), total);
	return true;
}

const std::vector<spawn_target>& spawner_targets(void){
	return g_targets;
}
