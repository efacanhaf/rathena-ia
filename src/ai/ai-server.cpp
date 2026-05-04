// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "ai-server.hpp"

#include <cstdlib>
#include <cstring>

#include <common/cli.hpp>
#include <common/core.hpp>
#include <common/malloc.hpp>
#include <common/showmsg.hpp>
#include <common/socket.hpp>
#include <common/strlib.hpp>
#include <common/timer.hpp>
#include <common/utilities.hpp>
#include <common/utils.hpp>
#include <config/core.hpp>

#include "aichrif.hpp"

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
		ShowInfo("ai-server status: char_fd=%d, aichrif_state=%d\n", char_fd, aichrif_state);
	} else if (strcmpi(buf, "alive") == 0) {
		ShowInfo("ai-server: I'm alive.\n");
	} else {
		ShowInfo("Console commands: status | alive | shutdown\n");
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
