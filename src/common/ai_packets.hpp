// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder
//
// AI inter-server wire formats. Shared between ai-server, char-server
// forwarder and map-server. All packets are length-prefixed:
//   cmd:W, len:W, payload...
// Length includes the cmd+len header itself.

#ifndef COMMON_AI_PACKETS_HPP
#define COMMON_AI_PACKETS_HPP

#include "cbasetypes.hpp"
#include "mmo.hpp" // NAME_LENGTH, MAP_NAME_LENGTH_EXT

#pragma pack(push, 1)

/// PACKET_AI_SHELL_SPAWN (0x2b40)
/// ai-server -> char-server -> map-server
struct PACKET_AI_SHELL_SPAWN_S {
	uint16 cmd;          // 0x2b40
	uint16 len;          // sizeof(*this)
	uint32 shell_id;     // virtual account_id (and char_id)
	char   name[NAME_LENGTH];
	uint16 class_;       // job
	uint8  sex;          // 0=female, 1=male
	uint16 hair;
	uint16 hair_color;
	uint16 cloth_color;
	uint16 head_top;
	uint16 head_mid;
	uint16 head_bottom;
	uint16 weapon;
	uint16 shield;
	uint16 robe;
	char   map_name[MAP_NAME_LENGTH_EXT];
	uint16 x;
	uint16 y;
	uint8  dir;          // facing 0..7
	uint8  behavior_id;  // ai-server-side enum (Phase 2)
};

/// PACKET_AI_SHELL_DESPAWN (0x2b41)
struct PACKET_AI_SHELL_DESPAWN_S {
	uint16 cmd;
	uint16 len;
	uint32 shell_id;
	uint8  reason;
};

/// PACKET_AI_SHELL_CMD (0x2b42) — variable-length, parsed by op
struct PACKET_AI_SHELL_CMD_HEADER {
	uint16 cmd;
	uint16 len;
	uint32 shell_id;
	uint8  op;
};

constexpr uint8 AI_CMD_WALK_TO     = 1;
constexpr uint8 AI_CMD_ATTACK      = 2;
constexpr uint8 AI_CMD_CAST        = 3;
constexpr uint8 AI_CMD_SAY         = 4;
constexpr uint8 AI_CMD_EMOTE       = 5;
constexpr uint8 AI_CMD_STOP_ATTACK = 6;
constexpr uint8 AI_CMD_SIT         = 7;
constexpr uint8 AI_CMD_STAND       = 8;

constexpr uint8 AI_CAST_KIND_ID     = 0;
constexpr uint8 AI_CAST_KIND_GROUND = 1;
constexpr uint8 AI_CAST_KIND_SELF   = 2;

struct PACKET_AI_CMD_WALK_TO_S {
	PACKET_AI_SHELL_CMD_HEADER hdr;
	uint16 x;
	uint16 y;
};

/// AI_CMD_ATTACK — start auto-attack on a target (mob or shell).
struct PACKET_AI_CMD_ATTACK_S {
	PACKET_AI_SHELL_CMD_HEADER hdr;
	uint32 target_id;
	uint8  continuous; // 1=keep attacking, 0=single hit
};

/// AI_CMD_STOP_ATTACK — stop the current attack chain.
struct PACKET_AI_CMD_STOP_ATTACK_S {
	PACKET_AI_SHELL_CMD_HEADER hdr;
};

/// AI_CMD_SAY — chat overhead the shell, broadcast to nearby clients.
struct PACKET_AI_CMD_SAY_S {
	PACKET_AI_SHELL_CMD_HEADER hdr;
	uint16 mes_len;
	char   mes[120];
};

/// AI_CMD_EMOTE — play an emoticon over the shell.
struct PACKET_AI_CMD_EMOTE_S {
	PACKET_AI_SHELL_CMD_HEADER hdr;
	uint8 emote_id;
	uint8 pad[3];
};

/// AI_CMD_CAST — cast a skill on a target id (BL) or on a ground cell (x,y).
/// kind: 0 = id-targeted, 1 = ground-targeted, 2 = self-targeted.
/// SkillId is sent as a string so the map-server can resolve via skill_db.
struct PACKET_AI_CMD_CAST_S {
	PACKET_AI_SHELL_CMD_HEADER hdr;
	char   skill_name[24];
	uint16 skill_lv;
	uint8  kind;        // 0=id, 1=ground, 2=self
	uint8  pad;
	uint32 target_id;   // when kind=0 or kind=2
	uint16 x, y;        // when kind=1
};

/// PACKET_AI_SHELL_SPAWNED (0x2b50) — map-server ack
struct PACKET_AI_SHELL_SPAWNED_S {
	uint16 cmd;
	uint16 len;
	uint32 shell_id;
	uint8  ok;
	uint8  err;
};

/// One row in PACKET_AI_SHELL_REPORT.enemies[]. mob_class kept for
/// behavior-side mob_db lookups; distance pre-computed on map side.
struct PACKET_AI_NEARBY_ENEMY {
	uint32 id;          // block_list id
	uint16 mob_class;   // mob_db id; 0 if not BL_MOB
	uint16 hp_pct;      // 0..100
	uint16 x, y;
	uint8  distance;    // chebyshev cells
};

constexpr uint8 AI_REPORT_MAX_ENEMIES = 8;

/// PACKET_AI_SHELL_EVENT (0x2b52) — async map → ai notification.
/// kind tells the payload shape: ATTACKED_BY supplies attacker_id+dmg,
/// DIED supplies killer_id, WHISPERED_BY/MENTIONED reserved for Phase 3.
constexpr uint8 AI_EVT_ATTACKED_BY     = 1;
constexpr uint8 AI_EVT_DIED            = 2;
constexpr uint8 AI_EVT_RESURRECTED     = 3;
constexpr uint8 AI_EVT_WHISPERED_BY    = 4;
constexpr uint8 AI_EVT_MENTIONED       = 5;

struct PACKET_AI_SHELL_EVENT_S {
	uint16 cmd;
	uint16 len;
	uint32 shell_id;
	uint8  kind;
	uint8  pad[3];
	uint32 actor_id;     // attacker / killer / whisperer
	uint32 dmg;          // ATTACKED_BY only
};

/// PACKET_AI_SHELL_REPORT (0x2b51) — periodic map → ai snapshot.
/// Fixed-size to keep parsing trivial; padded with zeroed enemies if fewer.
struct PACKET_AI_SHELL_REPORT_S {
	uint16 cmd;
	uint16 len;
	uint32 shell_id;
	uint16 mapindex;    // mapindex_id of current map (Phase 3.7 drift check)
	uint16 x, y;
	uint32 hp, max_hp;
	uint32 sp, max_sp;
	uint32 target_id;   // 0 = no target
	uint8  enemy_count; // 0..AI_REPORT_MAX_ENEMIES
	uint8  pad[3];
	PACKET_AI_NEARBY_ENEMY enemies[AI_REPORT_MAX_ENEMIES];
};

/// AI_CMD_WARP — pc_setpos shell back to a known map+cell. Used for drift
/// correction when a town shell wanders through a warp tile.
constexpr uint8 AI_CMD_WARP = 9;
struct PACKET_AI_CMD_WARP_S {
	PACKET_AI_SHELL_CMD_HEADER hdr;
	char   map_name[12]; // MAP_NAME_LENGTH (without _EXT) is enough
	uint16 x, y;
	uint8  pad[2];
};

#pragma pack(pop)

#endif /* COMMON_AI_PACKETS_HPP */
