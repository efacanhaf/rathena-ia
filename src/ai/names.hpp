// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder
//
// Procedural name generator for AI shells.
// Phase 1.2: hardcoded syllable pools (subset of royrdev's
// population_names.yml). Phase 1.5 will replace these with a YAML loader.

#ifndef AI_NAMES_HPP
#define AI_NAMES_HPP

#include <common/cbasetypes.hpp>
#include <common/mmo.hpp> // NAME_LENGTH

void do_init_names(void);

/// Fill `out_name` (NAME_LENGTH bytes, NUL-terminated) with a fresh syllable
/// composition, reroll up to 16 times if it hits the blocklist.
void names_generate(char* out_name);

#endif /* AI_NAMES_HPP */
