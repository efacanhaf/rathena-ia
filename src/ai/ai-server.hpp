// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef AI_SERVER_HPP
#define AI_SERVER_HPP

#include <string>

#include <common/cbasetypes.hpp>
#include <common/core.hpp>
#include <common/mmo.hpp>
#include <common/timer.hpp>
#include <config/core.hpp>

using rathena::server_core::Core;
using rathena::server_core::e_core_type;

namespace rathena::server_ai {
class AIServer : public Core{
	protected:
		bool initialize( int32 argc, char* argv[] ) override;
		void handle_main( t_tick next ) override;
		void finalize() override;
		void handle_crash() override;
		void handle_shutdown() override;

	public:
		AIServer() : Core( e_core_type::AI ){

		}
};
}

struct AI_Config {
	std::string ai_ip;					///< bind IP for ai-server
	uint16 ai_port;						///< local listen port (currently unused, reserved)
	std::string char_server_ip;			///< char-server we connect to (chrif-style)
	uint16 char_server_port;
	char ai_userid[NAME_LENGTH];		///< credentials presented to char-server
	char ai_passwd[NAME_LENGTH];

	char aiconf_name[256];				///< main conf path
	char msgconf_name[256];				///< msg_conf path
};

extern struct AI_Config ai_config;

#define AI_CONF_NAME "conf/ai_athena.conf"
#define AI_MSG_CONF_NAME "conf/msg_conf/ai_msg.conf"

bool ai_config_read(const char* cfgName, bool normal);

#endif /* AI_SERVER_HPP */
