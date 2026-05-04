// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef AI_CHAT_HPP
#define AI_CHAT_HPP

#include <string>

#include <common/cbasetypes.hpp>

namespace rathena::server_ai {

bool chat_load(const char* yaml_path);

/// Pick a random line from a category. Returns nullptr if category empty
/// or unknown. The pointer stays valid for the process lifetime.
const std::string* chat_pick(const char* category);

}

#endif /* AI_CHAT_HPP */
