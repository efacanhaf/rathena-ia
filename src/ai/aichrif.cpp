// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "aichrif.hpp"

#include <common/cbasetypes.hpp>
#include <common/showmsg.hpp>
#include <common/socket.hpp>
#include <common/strlib.hpp>
#include <common/timer.hpp>

#include "ai-server.hpp"

int32 char_fd = -1;

/// 0 = not connected
/// 1 = TCP up, 0x2b30 sent, awaiting 0x2b31
/// 2 = handshake OK
int32 aichrif_state = 0;

static t_tick aichrif_last_attempt = 0;

int32 aichrif_send_ping(int32 fd){
	static uint32 token = 0;
	token++;
	WFIFOHEAD(fd, 8);
	WFIFOW(fd, 0) = PACKET_AI_PING;
	WFIFOW(fd, 2) = 8;
	WFIFOL(fd, 4) = token;
	WFIFOSET(fd, 8);
	return 0;
}

int32 aichrif_send_hello(int32 fd){
	WFIFOHEAD(fd, 60);
	WFIFOW(fd, 0) = PACKET_AI_HELLO;
	memcpy(WFIFOP(fd, 2), ai_config.ai_userid, NAME_LENGTH);
	memcpy(WFIFOP(fd, 26), ai_config.ai_passwd, NAME_LENGTH);
	WFIFOL(fd, 50) = 0;
	WFIFOL(fd, 54) = 0;
	WFIFOW(fd, 58) = 0;
	WFIFOSET(fd, 60);
	return 0;
}

/// 0x2b31 <errCode>.B
static int32 aichrif_parse_connectack(int32 fd){
	if (RFIFOREST(fd) < 3)
		return 0;
	uint8 err = RFIFOB(fd, 2);
	RFIFOSKIP(fd, 3);
	if (err != 0) {
		ShowFatalError("ai-server: char-server rejected handshake (err=%u). Check ai_userid/ai_passwd.\n", err);
		set_eof(fd);
		return 0;
	}
	aichrif_state = 2;
	ShowStatus("ai-server: handshake OK with char-server (fd=%d).\n", fd);
	return 1;
}

/// 0x2b53 PONG { len:W=8, token:L }
static int32 aichrif_parse_pong(int32 fd){
	if (RFIFOREST(fd) < 4) return 0;
	int32 plen = RFIFOW(fd, 2);
	if (plen < 8 || RFIFOREST(fd) < (size_t)plen) return 0;
	uint32 token = RFIFOL(fd, 4);
	ShowInfo("ai-server: pong from map-server (token=%u). Routing OK.\n", token);
	RFIFOSKIP(fd, plen);
	return 1;
}

int32 aichrif_parse(int32 fd){
	if (session[fd]->flag.eof) {
		do_close(fd);
		char_fd = -1;
		aichrif_state = 0;
		ShowStatus("ai-server: char-server connection closed.\n");
		return 0;
	}
	while (RFIFOREST(fd) >= 2) {
		uint16 cmd = RFIFOW(fd, 0);
		switch (cmd) {
			case PACKET_AI_ACK:
				if (!aichrif_parse_connectack(fd))
					return 0;
				break;
			case PACKET_AI_PONG:
				if (!aichrif_parse_pong(fd))
					return 0;
				break;
			default:
				ShowWarning("ai-server: unknown packet 0x%04x from char-server, disconnecting.\n", cmd);
				set_eof(fd);
				return 0;
		}
	}
	RFIFOFLUSH(fd);
	return 0;
}

int32 aichrif_check_connect(void){
	if (char_fd >= 0 && session[char_fd] != nullptr)
		return 0;

	t_tick now = gettick();
	if (aichrif_last_attempt && DIFF_TICK(now, aichrif_last_attempt) < 10000)
		return 0;
	aichrif_last_attempt = now;

	uint32 ip = host2ip(ai_config.char_server_ip.c_str());
	if (ip == 0) {
		ShowError("ai-server: cannot resolve char_server_ip '%s'.\n", ai_config.char_server_ip.c_str());
		return -1;
	}

	ShowStatus("Connecting to char-server %s:%u ...\n", ai_config.char_server_ip.c_str(), ai_config.char_server_port);
	char_fd = make_connection(ip, ai_config.char_server_port, false, 10);
	if (char_fd < 0) {
		ShowError("ai-server: connection to char-server failed.\n");
		return -1;
	}

	session[char_fd]->func_parse = aichrif_parse;
	session[char_fd]->flag.server = 1;
	realloc_fifo(char_fd, FIFOSIZE_SERVERLINK, FIFOSIZE_SERVERLINK);

	aichrif_send_hello(char_fd);
	aichrif_state = 1;
	return 0;
}

static TIMER_FUNC(aichrif_check_connect_timer){
	aichrif_check_connect();
	return 0;
}

static TIMER_FUNC(aichrif_ping_timer){
	if (aichrif_state == 2 && char_fd >= 0)
		aichrif_send_ping(char_fd);
	return 0;
}

void do_init_aichrif(void){
	char_fd = -1;
	aichrif_state = 0;
	aichrif_last_attempt = 0;
	// Open TCP connection synchronously so do_sockets() has at least one fd
	// to poll on the first tick. If the char-server is offline we keep
	// retrying every 10s without ever exiting.
	aichrif_check_connect();
	add_timer_func_list(aichrif_check_connect_timer, "aichrif_check_connect");
	add_timer_interval(gettick() + 10 * 1000, aichrif_check_connect_timer, 0, 0, 10 * 1000);
	// Smoke-test ping; will be replaced by real shell-spawn traffic in Phase 1.2.
	add_timer_func_list(aichrif_ping_timer, "aichrif_ping");
	add_timer_interval(gettick() + 3 * 1000, aichrif_ping_timer, 0, 0, 5 * 1000);
}

void do_final_aichrif(void){
	if (char_fd >= 0) {
		do_close(char_fd);
		char_fd = -1;
	}
	aichrif_state = 0;
}
