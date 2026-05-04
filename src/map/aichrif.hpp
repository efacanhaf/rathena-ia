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

constexpr uint16 PACKET_AI_SHELL_SPAWNED = 0x2b50;
constexpr uint16 PACKET_AI_SHELL_REPORT  = 0x2b51;
constexpr uint16 PACKET_AI_SHELL_EVENT   = 0x2b52;
constexpr uint16 PACKET_AI_PONG          = 0x2b53;

/// Try to consume one AI packet from `fd`. Returns:
///   1  — consumed exactly one packet, caller should `continue`
///   0  — opcode is in AI range but not enough bytes yet, caller should `return 0`
///  -1  — opcode is NOT an AI packet, caller should fall through to chrif logic
int32 aichrif_try_handle(int32 fd);

void do_init_map_aichrif(void);
void do_final_map_aichrif(void);

#endif /* MAP_AICHRIF_HPP */
