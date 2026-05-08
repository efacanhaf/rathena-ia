// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "ai-server.hpp"

#include <cstdlib>
#include <cstring>

#include <common/cli.hpp>
#include <common/core.hpp>
#include <common/malloc.hpp>
#include <common/mapindex.hpp>
#include <common/showmsg.hpp>
#include <common/socket.hpp>
#include <common/strlib.hpp>
#include <common/timer.hpp>
#include <common/utilities.hpp>
#include <common/utils.hpp>
#include <config/core.hpp>

#include "aichrif.hpp"
#include "chat.hpp"
#include "map_difficulty.hpp"
#include "names.hpp"
#include "profile.hpp"
#include "shell_pool.hpp"
#include "skill_picker.hpp"
#include "spawner.hpp"

using namespace rathena;
using namespace rathena::server_core;
using namespace rathena::server_ai;

struct AI_Config ai_config{};

void display_helpscreen(bool do_exit){
	ShowInfo("Usage: %s\n", SERVER_NAME);
	if (do_exit)
		exit(EXIT_SUCCESS);
}

int32 parse_console(const char* buf){
	if (strcmpi(buf, "shutdown") == 0 || strcmpi(buf, "exit") == 0 || strcmpi(buf, "quit") == 0 || strcmpi(buf, "end") == 0) {
		global_core->signal_shutdown();
	} else if (strcmpi(buf, "status") == 0) {
		ShowInfo("ai-server status: char_fd=%d, aichrif_state=%d, shells_active=%u, total_allocs=%u\n",
			char_fd, aichrif_state, shell_pool_active_count(), shell_pool_total_allocs());
	} else if (strcmpi(buf, "stats") == 0) {
		const ai_stats& s = aichrif_stats();
		ShowInfo("ai-server stats:\n");
		ShowInfo("  spawned          = %u\n", s.spawned);
		ShowInfo("  spawn_acked_ok   = %u\n", s.spawn_acked_ok);
		ShowInfo("  spawn_acked_err  = %u\n", s.spawn_acked_err);
		ShowInfo("  attacks_sent     = %u\n", s.attacks_sent);
		ShowInfo("  casts_sent       = %u\n", s.casts_sent);
		ShowInfo("  chats_sent       = %u\n", s.chats_sent);
		ShowInfo("  warps_drift      = %u\n", s.warps_drift);
		ShowInfo("  attacked_by_evts = %u\n", s.attacked_by_events);
		ShowInfo("  died_events      = %u\n", s.died_events);
	} else if (strcmpi(buf, "alive") == 0) {
		ShowInfo("ai-server: I'm alive.\n");
	} else if (strncmpi(buf, "alloc ", 6) == 0) {
		int32 n = atoi(buf + 6);
		if (n <= 0 || n > 1000) n = 5;
		for (int32 i = 0; i < n; i++) {
			uint32 id = shell_pool_alloc();
			char name[NAME_LENGTH];
			names_generate(name);
			ShowInfo("  shell #%d: id=%u name=%s\n", i + 1, id, name);
		}
	} else if (strncmpi(buf, "hire ", 5) == 0) {
		// Phase 6 — manual mercenary hire from the console (Phase A test path,
		// before the in-game NPC ships in Phase C).
		// Usage: hire <owner_aid> <owner_cid> <map> <x> <y> [job=8] [tier=2] [dur_ms=600000]
		uint32 owner_aid = 0, owner_cid = 0;
		char mapn[24] = {0};
		uint32 x = 0, y = 0;
		uint32 job = 8, tier = 2, dur = 600000;
		int n = sscanf(buf + 5, "%u %u %23s %u %u %u %u %u",
			&owner_aid, &owner_cid, mapn, &x, &y, &job, &tier, &dur);
		if (n < 5) {
			ShowInfo("hire: usage: hire <owner_aid> <owner_cid> <map> <x> <y> [job=8] [tier=2] [dur_ms=600000]\n");
		} else {
			uint32 sid = aichrif_hire((uint16)job, (uint8)tier, owner_aid, owner_cid, mapn,
				(uint16)x, (uint16)y, dur);
			if (sid) ShowInfo("hired shell %u for char %u (aid %u, job=%u tier=%u %ums)\n",
				sid, owner_cid, owner_aid, job, tier, dur);
		}
	} else if (strcmpi(buf, "list") == 0) {
		aichrif_list_shells();
	} else if (strcmpi(buf, "reload") == 0) {
		// Phase 3.8: hot reload chat + skill rotations from yaml without
		// dropping connected shells.
		bool ok_chat = rathena::server_ai::chat_load("db/ai/population_chat.yml");
		bool ok_sk   = rathena::server_ai::skill_picker_load("db/ai/population_skill_db.yml");
		bool ok_pr   = rathena::server_ai::profile_load("db/ai/population_profile.yml");
		ShowInfo("ai-server: reload chat=%s skill_db=%s profile=%s\n",
			ok_chat ? "OK" : "FAIL", ok_sk ? "OK" : "FAIL", ok_pr ? "OK" : "FAIL");
	} else {
		ShowInfo("Console commands: status | stats | list | alive | alloc <N> | reload | hire <aid> <map> <x> <y> [job tier dur_ms] | shutdown\n");
	}
	return 0;
}

static void ai_set_defaults(void){
	ai_config.ai_ip = "127.0.0.1";
	ai_config.ai_port = 7280;
	ai_config.char_server_ip = "127.0.0.1";
	ai_config.char_server_port = 6121;
	safestrncpy(ai_config.ai_userid, "s1", NAME_LENGTH);
	safestrncpy(ai_config.ai_passwd, "p1", NAME_LENGTH);
	safestrncpy(ai_config.aiconf_name, AI_CONF_NAME, sizeof(ai_config.aiconf_name));
	safestrncpy(ai_config.msgconf_name, AI_MSG_CONF_NAME, sizeof(ai_config.msgconf_name));
	ai_config.respawn_delay_ms = 30000;	// 30s default — long enough to feel
										// "real death", short enough that the
										// population stays visibly active.
	ai_config.debug_level = 0;			// silent — flip to 1+ in conf to peek.
}

bool ai_config_read(const char* cfgName, bool normal){
	char line[1024], w1[32], w2[1024];
	FILE* fp = fopen(cfgName, "r");
	if (fp == nullptr) {
		if (normal) {
			ShowError("ai-server: configuration file (%s) not found.\n", cfgName);
			return false;
		}
		return true;
	}
	while (fgets(line, sizeof(line), fp)) {
		if (line[0] == '/' && line[1] == '/')
			continue;
		if (sscanf(line, "%31[^:]: %1023[^\r\n]", w1, w2) < 2)
			continue;
		if (strcmpi(w1, "ai_ip") == 0) {
			ai_config.ai_ip = w2;
		} else if (strcmpi(w1, "ai_port") == 0) {
			ai_config.ai_port = (uint16)atoi(w2);
		} else if (strcmpi(w1, "char_server_ip") == 0) {
			ai_config.char_server_ip = w2;
		} else if (strcmpi(w1, "char_server_port") == 0) {
			ai_config.char_server_port = (uint16)atoi(w2);
		} else if (strcmpi(w1, "ai_userid") == 0) {
			safestrncpy(ai_config.ai_userid, w2, NAME_LENGTH);
		} else if (strcmpi(w1, "ai_passwd") == 0) {
			safestrncpy(ai_config.ai_passwd, w2, NAME_LENGTH);
		} else if (strcmpi(w1, "respawn_delay_ms") == 0) {
			ai_config.respawn_delay_ms = (uint32)strtoul(w2, nullptr, 10);
		} else if (strcmpi(w1, "debug_level") == 0) {
			ai_config.debug_level = (uint32)strtoul(w2, nullptr, 10);
		} else if (strcmpi(w1, "import") == 0) {
			ai_config_read(w2, false);
		}
	}
	fclose(fp);
	return true;
}

bool AIServer::initialize(int32 argc, char* argv[]){
	safestrncpy(console_log_filepath, "./log/ai-msg_log.log", sizeof(console_log_filepath));

	ai_set_defaults();
	cli_get_options(argc, argv);

	if (!ai_config_read(ai_config.aiconf_name, true))
		return false;

	set_defaultparse(aichrif_parse);

	// Bind a local listener on ai_port so do_sockets() always has at least
	// one fd to select() on. Phase 2/3 will reuse it for admin/debug.
	int32 listen_fd = make_listen_bind(host2ip(ai_config.ai_ip.c_str()), ai_config.ai_port);
	if (listen_fd == -1) {
		ShowFatalError("ai-server: failed to bind %s:%u\n", ai_config.ai_ip.c_str(), ai_config.ai_port);
		return false;
	}

	// Phase 6 — needed by aichrif_follow_timer to resolve mapindex IDs
	// from REPORT into names for the WARP packet (cross-map merc follow).
	mapindex_init();

	do_init_shell_pool();
	do_init_names();

	// Phase 1.5c: load population_spawn.yml. No cap — map-server rejects
	// shells whose target map isn't loaded, so the effective population
	// matches what this cluster knows about.
	if (!spawner_load("db/ai/population_spawn.yml", 0)) {
		ShowWarning("ai-server: spawner not loaded; running with empty population.\n");
	}

	// Phase 2.4: per-job skill rotations + condition gates.
	if (!rathena::server_ai::skill_picker_load("db/ai/population_skill_db.yml")) {
		ShowWarning("ai-server: skill_picker not loaded; shells will only auto-attack.\n");
	}

	// Phase 4: per-job, per-tier stat/gear profiles.
	if (!rathena::server_ai::profile_load("db/ai/population_profile.yml")) {
		ShowWarning("ai-server: profile not loaded; shells fall back to hardcoded floor.\n");
	}

	// Phase 5 — per-map difficulty (avg mob level). Spawner uses this to pick
	// the profile tier that matches the map; falls back to mid tier when the
	// file is missing.
	if (!rathena::server_ai::map_difficulty_load("db/ai/map_difficulty.yml")) {
		ShowWarning("ai-server: map_difficulty not loaded; tier defaults to mid for all maps.\n");
	}

	// Phase 3.4: ambient chat lines.
	if (!rathena::server_ai::chat_load("db/ai/population_chat.yml")) {
		ShowWarning("ai-server: chat lines not loaded; shells will be silent.\n");
	}

	do_init_aichrif();

	ShowStatus("ai-server is " CL_GREEN "ready" CL_RESET " (listening %s:%u, target char-server %s:%u).\n",
		ai_config.ai_ip.c_str(), ai_config.ai_port,
		ai_config.char_server_ip.c_str(), ai_config.char_server_port);
	return true;
}

void AIServer::handle_main(t_tick next){
	do_sockets(next);
}

void AIServer::finalize(){
	do_final_aichrif();
	do_final_shell_pool();
	mapindex_final();
	ShowStatus("ai-server: finalized.\n");
}

void AIServer::handle_crash(){
	ShowFatalError("ai-server: crash handler invoked.\n");
}

void AIServer::handle_shutdown(){
	ShowStatus("ai-server: shutting down...\n");
	flush_fifos();
}

int32 main(int32 argc, char* argv[]){
	return main_core<AIServer>(argc, argv);
}
