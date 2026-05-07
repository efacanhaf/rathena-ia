// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef AI_CHRIF_HPP
#define AI_CHRIF_HPP

#include <common/cbasetypes.hpp>
#include <common/showmsg.hpp>

#include "ai-server.hpp"

extern int32 char_fd;
extern int32 aichrif_state;

// Phase 5 — debug-level gated log macro. Enables ShowDebug only when
// ai_config.debug_level >= `lvl`. Use sparingly:
//   AI_LOG(1, "spawn ack id=%u", id);
//   AI_LOG(2, "skill chosen: %s", name);
// Levels are documented in ai-server.hpp::AI_Config::debug_level.
#define AI_LOG(lvl, ...) do { \
	if (ai_config.debug_level >= (uint32)(lvl)) ShowDebug(__VA_ARGS__); \
} while (0)

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
	uint16 head_top;
	uint16 weapon;
	const char* map_name;
	uint16 x, y;
	uint8  dir;
	uint8  behavior_id;
	uint8  tier;     // 0=town, 1=field, 2=dungeon

	// Phase 4 — populated from population_profile.yml by the spawner.
	uint16 base_level;
	uint16 job_level;
	uint16 str_, agi_, vit_, int_, dex_, luk_;
	uint16 speed;
	uint32 equip[10]; // see ai_equip_slot
};
int32 aichrif_send_shell_spawn(int32 fd, const ai_shell_init& init);

/// Phase 5 — tell map-server to remove a shell entirely (HP/inventory/etc
/// gone). Used during scheduled respawn (followed by a fresh SPAWN with the
/// same shell_id) and during shutdown wipes.
int32 aichrif_send_despawn(int32 fd, uint32 shell_id, uint8 reason);

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

/// Console-list helper: dumps every live shell with cur map+coord+hp.
void aichrif_list_shells();

/// Phase 3.9 runtime counters (read by the console "stats" command).
struct ai_stats {
	uint32 spawned;
	uint32 spawn_acked_ok;
	uint32 spawn_acked_err;
	uint32 attacks_sent;
	uint32 casts_sent;
	uint32 chats_sent;
	uint32 warps_drift;
	uint32 attacked_by_events;
	uint32 died_events;
	uint32 respawns_scheduled;	// Phase 5 — DIED with respawn enabled
	uint32 respawns_sent;		// Phase 5 — despawn+spawn pair emitted
};
const ai_stats& aichrif_stats();

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
