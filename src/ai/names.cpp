// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "names.hpp"

#include <array>
#include <cstdlib>
#include <cstring>

#include <common/random.hpp>
#include <common/showmsg.hpp>
#include <common/strlib.hpp>

namespace {

// Subset of royrdev's population_names.yml SyllablesStart/Mid/End.
// Pools are intentionally co-prime length-ish so collisions stay rare.
const std::array<const char*, 22> kStart = {
	"Ka","Ve","To","Mi","Lu","Ra","Se","No","Di","Fy",
	"Xe","Ja","Hi","Wu","Bl","Cr","Dr","Fr","Gl","Pr","St","Vy"
};
const std::array<const char*, 22> kMid = {
	"ra","ni","ko","ta","li","de","fe","ga","hu","im",
	"ja","ke","lo","mu","na","ov","pe","ri","so","tu","ul","vi"
};
const std::array<const char*, 22> kEnd = {
	"ko","mi","ya","ro","ne","an","el","is","os","ur",
	"ax","by","ce","da","en","fi","gu","he","ix","jo","ky","lo"
};

// Substrings that disqualify a generated name.
const std::array<const char*, 6> kBlocklist = {
	"gm","admin","mod","null","root","fuck"
};

bool blocked(const char* name){
	char lower[NAME_LENGTH];
	safestrncpy(lower, name, NAME_LENGTH);
	for (char* p = lower; *p; ++p)
		*p = (char)tolower((unsigned char)*p);
	for (const char* bad : kBlocklist) {
		if (strstr(lower, bad))
			return true;
	}
	return false;
}

}

void do_init_names(void){
	ShowStatus("names: syllable pools loaded (%zu*%zu*%zu = %zu combos).\n",
		kStart.size(), kMid.size(), kEnd.size(),
		kStart.size() * kMid.size() * kEnd.size());
}

void names_generate(char* out_name){
	for (int32 attempt = 0; attempt < 16; attempt++) {
		const char* a = kStart[rnd() % kStart.size()];
		const char* b = kMid[rnd() % kMid.size()];
		const char* c = kEnd[rnd() % kEnd.size()];
		safesnprintf(out_name, NAME_LENGTH, "%s%s%s", a, b, c);
		// Capitalise first character (already capital from kStart, but be safe).
		out_name[0] = (char)toupper((unsigned char)out_name[0]);
		if (!blocked(out_name))
			return;
	}
	// All attempts blocked — fall back to a deterministic stub.
	safesnprintf(out_name, NAME_LENGTH, "Shell%u", rnd() % 100000);
}
