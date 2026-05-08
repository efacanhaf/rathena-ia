// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder
//
// char-server <-> ai-server peer interface (DimensionsRO ia-server).
// Forwards AI inter-server packets between the ai-server and the connected
// map-servers. Lives in its own translation unit so existing char_mapif.cpp
// stays focused on the map-server protocol.

#ifndef CHAR_AIF_HPP
#define CHAR_AIF_HPP

#include <common/cbasetypes.hpp>

extern int32 ai_server_fd;	///< current ai-server fd, -1 if no peer connected

/// Parser installed on the ai-server fd after handshake (0x2b30/0x2b31).
int32 chaif_parse(int32 fd);

/// Forward a packet from a map-server fd to the ai-server. Caller passes the
/// raw payload start (i.e. RFIFOP(map_fd, 0)) and its length. No-op if no peer.
void chaif_forward_from_map(const uint8* buf, size_t len);

void chaif_on_disconnect(int32 fd);

#endif /* CHAR_AIF_HPP */
