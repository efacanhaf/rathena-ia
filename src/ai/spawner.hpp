// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder
//
// Loads population_spawn.yml and produces concrete (job, map, count)
// spawn requests for the rest of the ai-server to consume.

#ifndef AI_SPAWNER_HPP
#define AI_SPAWNER_HPP

#include <string>
#include <vector>

#include <common/cbasetypes.hpp>

enum class spawn_category : uint8 {
	TOWN = 0,
	FIELD = 1,
	DUNGEON = 2,
};

struct spawn_target {
	uint16 job;
	std::string map_name;
	uint16 count;
	spawn_category category;
};

bool spawner_load(const char* yaml_path, int32 cap_total = 0);

const std::vector<spawn_target>& spawner_targets(void);

#endif /* AI_SPAWNER_HPP */
