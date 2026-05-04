// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef AI_CHRIF_HPP
#define AI_CHRIF_HPP

#include <common/cbasetypes.hpp>

extern int32 char_fd;
extern int32 aichrif_state;

void do_init_aichrif(void);
void do_final_aichrif(void);

/// Tries to (re)connect to char-server. Called periodically.
int32 aichrif_check_connect(void);

/// Parser for char-server fd.
int32 aichrif_parse(int32 fd);

/// Sends the AI peer handshake (0x2b30) once the TCP connection is up.
int32 aichrif_send_hello(int32 fd);

/// Sends a smoke-test ping that should round-trip back as a pong from map-server.
int32 aichrif_send_ping(int32 fd);

/// Send a shell spawn request to map-server (via char-server). Returns 0 on
/// success. Phase 1.3 stub: caller passes in fully-formed identity; Phase 1.5
/// will read identity from population_engine.yml.
struct ai_shell_init {
	uint32 shell_id;
	const char* name;
	uint16 class_;
	uint8  sex;
	uint16 hair;
	uint16 hair_color;
	const char* map_name;
	uint16 x, y;
	uint8  dir;
	uint8  behavior_id;
};
int32 aichrif_send_shell_spawn(int32 fd, const ai_shell_init& init);

/// Tell the map-server to walk a shell to (x, y).
int32 aichrif_send_walk_to(int32 fd, uint32 shell_id, uint16 x, uint16 y);

/// Tell the map-server to start auto-attacking a target.
int32 aichrif_send_attack(int32 fd, uint32 shell_id, uint32 target_id, bool continuous);

/// Tell the map-server to cast a skill. kind: 0=id, 1=ground, 2=self.
int32 aichrif_send_cast(int32 fd, uint32 shell_id, const char* skill_name,
		uint16 skill_lv, uint8 kind, uint32 target_id, uint16 x, uint16 y);

/// Make a shell broadcast a chat line over its head (AREA).
int32 aichrif_send_say(int32 fd, uint32 shell_id, const char* msg);

/// Sit / stand / emote (Phase 3.5).
int32 aichrif_send_sit(int32 fd, uint32 shell_id);
int32 aichrif_send_stand(int32 fd, uint32 shell_id);
int32 aichrif_send_emote(int32 fd, uint32 shell_id, uint8 emote_id);

/// Drift correction warp (Phase 3.7).
int32 aichrif_send_warp(int32 fd, uint32 shell_id, const char* map_name, uint16 x, uint16 y);

/// Inter-server packet opcodes (kept here so ai-server doesn't depend on map/).
constexpr uint16 PACKET_AI_HELLO         = 0x2b30;
constexpr uint16 PACKET_AI_ACK           = 0x2b31;
constexpr uint16 PACKET_AI_SHELL_SPAWN   = 0x2b40;
constexpr uint16 PACKET_AI_SHELL_DESPAWN = 0x2b41;
constexpr uint16 PACKET_AI_SHELL_CMD     = 0x2b42;
constexpr uint16 PACKET_AI_PING          = 0x2b43;
constexpr uint16 PACKET_AI_SHELL_SPAWNED = 0x2b50;
constexpr uint16 PACKET_AI_SHELL_REPORT  = 0x2b51;
constexpr uint16 PACKET_AI_SHELL_EVENT   = 0x2b52;
constexpr uint16 PACKET_AI_PONG          = 0x2b53;

#endif /* AI_CHRIF_HPP */
