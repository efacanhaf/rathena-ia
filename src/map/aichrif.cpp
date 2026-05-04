// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "aichrif.hpp"

#include <common/cbasetypes.hpp>
#include <common/showmsg.hpp>
#include <common/socket.hpp>

#include "chrif.hpp"

/// Reply to ai-server with PACKET_AI_PONG. Length-prefixed (8 bytes).
static void aichrif_send_pong(int32 fd, uint32 token){
	WFIFOHEAD(fd, 8);
	WFIFOW(fd, 0) = PACKET_AI_PONG;
	WFIFOW(fd, 2) = 8;
	WFIFOL(fd, 4) = token;
	WFIFOSET(fd, 8);
}

/// Handle PACKET_AI_PING { len:W=8, token:L }. Phase 1.1 smoke test only.
static int32 aichrif_handle_ping(int32 fd){
	uint32 token = RFIFOL(fd, 4);
	ShowInfo("ai-server: ping received (token=%u). Sending pong.\n", token);
	aichrif_send_pong(fd, token);
	return 1;
}

/// Phase 1.1 placeholder: log + ack. Real spawn comes in Phase 1.2.
static int32 aichrif_handle_spawn(int32 fd){
	uint32 shell_id = RFIFOL(fd, 4);
	ShowInfo("ai-server: SHELL_SPAWN received (shell_id=%u). [stub]\n", shell_id);
	return 1;
}

static int32 aichrif_handle_despawn(int32 fd){
	uint32 shell_id = RFIFOL(fd, 4);
	ShowInfo("ai-server: SHELL_DESPAWN received (shell_id=%u). [stub]\n", shell_id);
	return 1;
}

static int32 aichrif_handle_cmd(int32 fd){
	uint32 shell_id = RFIFOL(fd, 4);
	ShowInfo("ai-server: SHELL_CMD received (shell_id=%u). [stub]\n", shell_id);
	return 1;
}

int32 aichrif_try_handle(int32 fd){
	if (RFIFOREST(fd) < 2)
		return 0;
	uint16 cmd = RFIFOW(fd, 0);
	if (cmd < AI_PACKET_FROM_AI_FIRST || cmd > AI_PACKET_FROM_AI_LAST)
		return -1;

	if (RFIFOREST(fd) < 4)
		return 0;
	int32 plen = RFIFOW(fd, 2);
	if (plen < 4)
		return -1; // malformed; let chrif handle the disconnect
	if (RFIFOREST(fd) < (size_t)plen)
		return 0;

	int32 ok = -1;
	switch (cmd) {
		case PACKET_AI_PING:          ok = aichrif_handle_ping(fd); break;
		case PACKET_AI_SHELL_SPAWN:   ok = aichrif_handle_spawn(fd); break;
		case PACKET_AI_SHELL_DESPAWN: ok = aichrif_handle_despawn(fd); break;
		case PACKET_AI_SHELL_CMD:     ok = aichrif_handle_cmd(fd); break;
		default:
			ShowWarning("ai-server: unhandled AI packet 0x%04x (len=%d).\n", cmd, plen);
			ok = 1; // skip and continue
			break;
	}

	RFIFOSKIP(fd, plen);
	return ok;
}

void do_init_map_aichrif(void){
	// nothing yet — Phase 1.2 will allocate the shell registry here.
}

void do_final_map_aichrif(void){
	// nothing yet
}
