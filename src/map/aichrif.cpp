// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "aichrif.hpp"

#include <cstring>

#include <common/ai_packets.hpp>
#include <common/cbasetypes.hpp>
#include <common/showmsg.hpp>
#include <common/socket.hpp>

#include "chrif.hpp"

extern int32 char_fd; ///< inter-server fd to char-server (defined in chrif.cpp)

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

/// Send a SHELL_SPAWNED ack back to ai-server (via char-server forwarder).
static void aichrif_send_spawned_ack(uint32 shell_id, bool ok, uint8 err){
	if (char_fd < 0 || session[char_fd] == nullptr)
		return;
	PACKET_AI_SHELL_SPAWNED_S p{};
	p.cmd = PACKET_AI_SHELL_SPAWNED;
	p.len = sizeof(p);
	p.shell_id = shell_id;
	p.ok = ok ? 1 : 0;
	p.err = err;
	WFIFOHEAD(char_fd, sizeof(p));
	memcpy(WFIFOP(char_fd, 0), &p, sizeof(p));
	WFIFOSET(char_fd, sizeof(p));
}

/// Phase 1.3: parse the wire format and ack. Phase 1.4 will allocate the
/// map_session_data and call clif_spawn / unit_walktoxy.
static int32 aichrif_handle_spawn(int32 fd){
	if (RFIFOREST(fd) < sizeof(PACKET_AI_SHELL_SPAWN_S))
		return 0;
	const PACKET_AI_SHELL_SPAWN_S* p = (const PACKET_AI_SHELL_SPAWN_S*)RFIFOP(fd, 0);
	ShowInfo("ai-server: SHELL_SPAWN id=%u name='%s' class=%u @ %s(%u,%u) dir=%u behavior=%u [stub: would create map_session_data]\n",
		p->shell_id, p->name, p->class_, p->map_name, p->x, p->y, p->dir, p->behavior_id);
	aichrif_send_spawned_ack(p->shell_id, true, 0);
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
