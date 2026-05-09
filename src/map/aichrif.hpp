// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder
//
// map-server side handler for AI inter-server packets (DimensionsRO ia-server).
// Lives in its own translation unit so chrif.cpp only needs to delegate.

#ifndef MAP_AICHRIF_HPP
#define MAP_AICHRIF_HPP

#include <common/cbasetypes.hpp>

/// AI packet opcode range forwarded by char-server.
constexpr uint16 AI_PACKET_FROM_AI_FIRST = 0x2b40;
constexpr uint16 AI_PACKET_FROM_AI_LAST  = 0x2b4f;

constexpr uint16 PACKET_AI_SHELL_SPAWN   = 0x2b40;
constexpr uint16 PACKET_AI_SHELL_DESPAWN = 0x2b41;
constexpr uint16 PACKET_AI_SHELL_CMD     = 0x2b42;
constexpr uint16 PACKET_AI_PING          = 0x2b43;
constexpr uint16 PACKET_AI_HIRE_REQUEST    = 0x2b44;	// Phase 6 (map -> char -> ai)
constexpr uint16 PACKET_AI_DISMISS_REQUEST = 0x2b45;	// Phase 6 (map -> char -> ai)

constexpr uint16 PACKET_AI_SHELL_SPAWNED = 0x2b50;
constexpr uint16 PACKET_AI_SHELL_REPORT  = 0x2b51;
constexpr uint16 PACKET_AI_SHELL_EVENT   = 0x2b52;
constexpr uint16 PACKET_AI_PONG          = 0x2b53;
constexpr uint16 PACKET_AI_OWNER_BACK    = 0x2b54;	// Phase 6 (map -> char -> ai)

/// Try to consume one AI packet from `fd`. Returns:
///   1  — consumed exactly one packet, caller should `continue`
///   0  — opcode is in AI range but not enough bytes yet, caller should `return 0`
///  -1  — opcode is NOT an AI packet, caller should fall through to chrif logic
int32 aichrif_try_handle(int32 fd);

/// Returns true when `account_id` is in the ai-server's reserved range
/// (95M..100M). Used by callers that need to exclude shells from
/// player-only logic (party EXP share, etc.).
bool aishell_is_shell(uint32 account_id);

/// Phase 6 — send a HIRE_REQUEST upstream (map -> char -> ai). Returns 0 on
/// success, -1 if char-server isn't connected. base_level_override /
/// job_level_override = 0 means "use the profile's stat ramp"; non-zero
/// scales the merc to that exact level (used by the hire NPC to match
/// the party leader).
int32 aichrif_send_hire(uint32 owner_aid, uint32 owner_cid, uint16 job, uint8 tier,
		const char* map_name, uint16 x, uint16 y, uint32 duration_ms,
		uint16 base_level_override = 0, uint16 job_level_override = 0,
		uint8 role = 0 /* AI_HIRE_ROLE_SUPPORT */);

/// Phase 6 — manual dismiss. Tears down the merc shell of `owner_cid`
/// (char_id) without waiting for the contract timer. ai-server is the
/// source of truth for who-owns-what, so this just forwards the request.
int32 aichrif_send_dismiss(uint32 owner_cid);

void do_init_map_aichrif(void);
void do_final_map_aichrif(void);

#endif /* MAP_AICHRIF_HPP */
