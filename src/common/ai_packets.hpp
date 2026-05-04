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

/// PACKET_AI_SHELL_SPAWNED (0x2b50) — map-server ack
struct PACKET_AI_SHELL_SPAWNED_S {
	uint16 cmd;
	uint16 len;
	uint32 shell_id;
	uint8  ok;
	uint8  err;
};

#pragma pack(pop)

#endif /* COMMON_AI_PACKETS_HPP */
