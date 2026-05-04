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
