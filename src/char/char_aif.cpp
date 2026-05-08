// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "char_aif.hpp"

#include <cstring>

#include <common/cbasetypes.hpp>
#include <common/showmsg.hpp>
#include <common/socket.hpp>
#include <common/utilities.hpp>

#include "char.hpp"

int32 ai_server_fd = -1;

/// Forward a payload from the ai-server fd to all connected map-servers.
static void chaif_forward_to_maps(const uint8* buf, size_t len){
	for (int32 i = 0; i < ARRAYLENGTH(map_server); i++) {
		int32 mfd = map_server[i].fd;
		if (mfd <= 0)
			continue;
		WFIFOHEAD(mfd, len);
		memcpy(WFIFOP(mfd, 0), buf, len);
		WFIFOSET(mfd, len);
	}
}

void chaif_forward_from_map(const uint8* buf, size_t len){
	if (ai_server_fd < 0 || session[ai_server_fd] == nullptr)
		return;
	WFIFOHEAD(ai_server_fd, len);
	memcpy(WFIFOP(ai_server_fd, 0), buf, len);
	WFIFOSET(ai_server_fd, len);
}

/// Read a packet length given its first WORD. Returns 0 if more data needed,
/// negative on error, or the total length consumed.
static int32 chaif_packet_len(int32 fd){
	uint16 cmd = RFIFOW(fd, 0);
	switch (cmd) {
		// AI -> map (forwarded)
		case 0x2b40: // AI_SHELL_SPAWN (variable)
		case 0x2b41: // AI_SHELL_DESPAWN
		case 0x2b42: // AI_SHELL_CMD
		case 0x2b43: // AI_PING
			if (RFIFOREST(fd) < 4) return 0;
			return RFIFOW(fd, 2); // length-prefixed at offset 2
		default:
			return -1;
	}
}

int32 chaif_parse(int32 fd){
	if (session[fd]->flag.eof) {
		chaif_on_disconnect(fd);
		do_close(fd);
		return 0;
	}

	while (RFIFOREST(fd) >= 4) {
		int32 plen = chaif_packet_len(fd);
		if (plen < 0) {
			ShowError("chaif_parse: unknown packet 0x%04x from ai-server, disconnecting.\n", RFIFOW(fd, 0));
			set_eof(fd);
			return 0;
		}
		if (plen == 0 || RFIFOREST(fd) < (size_t)plen)
			return 0; // need more bytes
		// Forward as-is to all map-servers.
		chaif_forward_to_maps(RFIFOP(fd, 0), plen);
		RFIFOSKIP(fd, plen);
	}
	RFIFOFLUSH(fd);
	return 0;
}

void chaif_on_disconnect(int32 fd){
	if (ai_server_fd == fd) {
		ai_server_fd = -1;
		ShowStatus("ai-server: peer disconnected.\n");
	}
}
