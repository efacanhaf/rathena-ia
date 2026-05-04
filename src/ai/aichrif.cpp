// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "aichrif.hpp"

#include <cstring>
#include <vector>

#include <common/ai_packets.hpp>
#include <common/random.hpp>
#include <common/cbasetypes.hpp>
#include <common/showmsg.hpp>
#include <common/socket.hpp>
#include <common/strlib.hpp>
#include <common/timer.hpp>

#include "ai-server.hpp"
#include "names.hpp"
#include "shell_pool.hpp"
#include "spawner.hpp"

int32 char_fd = -1;

namespace {
struct shell_state {
	uint32 shell_id;
	uint16 base_x, base_y; // anchor point; wander stays within ±radius
};
std::vector<shell_state> g_shells_local;
constexpr uint16 WANDER_RADIUS = 6;

// Spawn-emission cursor: walks through spawner_targets() in chunks so the
// network buffer isn't blown up at startup.
size_t g_spawn_target_idx = 0;
uint16 g_spawn_within = 0;
int32  g_spawn_pending = 0;
int32  g_spawn_emitted = 0;
constexpr int32 SPAWN_BATCH_PER_TICK = 5;
constexpr int32 SPAWN_HARD_CAP = 50;
}

/// 0 = not connected
/// 1 = TCP up, 0x2b30 sent, awaiting 0x2b31
/// 2 = handshake OK
int32 aichrif_state = 0;

static t_tick aichrif_last_attempt = 0;

int32 aichrif_send_walk_to(int32 fd, uint32 shell_id, uint16 x, uint16 y){
	PACKET_AI_CMD_WALK_TO_S p{};
	p.hdr.cmd = PACKET_AI_SHELL_CMD;
	p.hdr.len = sizeof(p);
	p.hdr.shell_id = shell_id;
	p.hdr.op = AI_CMD_WALK_TO;
	p.x = x;
	p.y = y;
	WFIFOHEAD(fd, sizeof(p));
	memcpy(WFIFOP(fd, 0), &p, sizeof(p));
	WFIFOSET(fd, sizeof(p));
	return 0;
}

int32 aichrif_send_shell_spawn(int32 fd, const ai_shell_init& init){
	PACKET_AI_SHELL_SPAWN_S p{};
	p.cmd = PACKET_AI_SHELL_SPAWN;
	p.len = sizeof(p);
	p.shell_id = init.shell_id;
	safestrncpy(p.name, init.name, NAME_LENGTH);
	p.class_ = init.class_;
	p.sex = init.sex;
	p.hair = init.hair;
	p.hair_color = init.hair_color;
	p.cloth_color = 0;
	safestrncpy(p.map_name, init.map_name, MAP_NAME_LENGTH_EXT);
	p.x = init.x;
	p.y = init.y;
	p.dir = init.dir;
	p.behavior_id = init.behavior_id;
	WFIFOHEAD(fd, sizeof(p));
	memcpy(WFIFOP(fd, 0), &p, sizeof(p));
	WFIFOSET(fd, sizeof(p));
	return 0;
}

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

	// Phase 1.5c: queue spawn requests; the throttled timer will drain them
	// over time so we don't saturate map-server's wfifo at startup.
	g_spawn_pending = 0;
	for (const auto& t : spawner_targets())
		g_spawn_pending += t.count;
	ShowStatus("ai-server: queued %d shells from population_spawn.yml.\n", g_spawn_pending);
	return 1;
}

/// 0x2b50 SHELL_SPAWNED ack from map-server.
static int32 aichrif_parse_shell_spawned(int32 fd){
	if (RFIFOREST(fd) < sizeof(PACKET_AI_SHELL_SPAWNED_S)) return 0;
	const PACKET_AI_SHELL_SPAWNED_S* p = (const PACKET_AI_SHELL_SPAWNED_S*)RFIFOP(fd, 0);
	ShowInfo("ai-server: shell %u spawn ack ok=%u err=%u.\n", p->shell_id, p->ok, p->err);
	RFIFOSKIP(fd, p->len);
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
			case PACKET_AI_SHELL_SPAWNED:
				if (!aichrif_parse_shell_spawned(fd))
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

static TIMER_FUNC(aichrif_spawn_drain_timer){
	if (aichrif_state != 2 || char_fd < 0) return 0;
	if (g_spawn_emitted >= SPAWN_HARD_CAP) return 0;
	const auto& targets = spawner_targets();
	int32 emitted = 0;
	while (emitted < SPAWN_BATCH_PER_TICK && g_spawn_target_idx < targets.size() &&
			g_spawn_emitted < SPAWN_HARD_CAP) {
		const auto& t = targets[g_spawn_target_idx];
		if (g_spawn_within >= t.count) {
			g_spawn_target_idx++;
			g_spawn_within = 0;
			continue;
		}
		uint32 sid = shell_pool_alloc();
		if (sid == 0) {
			g_spawn_pending = 0;
			return 0;
		}
		char nm[NAME_LENGTH];
		names_generate(nm);
		// Tight prontera bbox (validated). Per-map walkable sampling later.
		uint16 bx = (uint16)(146 + (rnd() % 21));    // 146..166
		uint16 by = (uint16)(170 + (rnd() % 21));    // 170..190
		ai_shell_init init{};
		init.shell_id = sid;
		init.name = nm;
		init.class_ = t.job;
		init.sex = (uint8)(rnd() % 2);
		init.hair = (uint16)(1 + rnd() % 20);
		init.hair_color = (uint16)(rnd() % 8);
		// Phase 1.5c: ignore yaml's map list, force prontera until the
		// walkable-cell sampler lands.
		init.map_name = "prontera";
		init.x = bx;
		init.y = by;
		init.dir = (uint8)(rnd() % 8);
		init.behavior_id = 0;
		aichrif_send_shell_spawn(char_fd, init);
		g_shells_local.push_back({sid, bx, by});
		g_spawn_within++;
		g_spawn_pending--;
		g_spawn_emitted++;
		emitted++;
	}
	return 0;
}

static TIMER_FUNC(aichrif_wander_timer){
	if (aichrif_state != 2 || char_fd < 0) return 0;
	for (auto& s : g_shells_local) {
		int16 dx = (rnd() % (WANDER_RADIUS * 2 + 1)) - WANDER_RADIUS;
		int16 dy = (rnd() % (WANDER_RADIUS * 2 + 1)) - WANDER_RADIUS;
		uint16 tx = (uint16)std::max<int16>(1, (int16)s.base_x + dx);
		uint16 ty = (uint16)std::max<int16>(1, (int16)s.base_y + dy);
		aichrif_send_walk_to(char_fd, s.shell_id, tx, ty);
	}
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
	// Wander loop: every 5s, each tracked shell picks a new destination.
	add_timer_func_list(aichrif_wander_timer, "aichrif_wander");
	add_timer_interval(gettick() + 5 * 1000, aichrif_wander_timer, 0, 0, 5 * 1000);
	// Spawn drain: 20 SHELL_SPAWN packets per 200ms = 100/s ramp-up.
	add_timer_func_list(aichrif_spawn_drain_timer, "aichrif_spawn_drain");
	add_timer_interval(gettick() + 1 * 1000, aichrif_spawn_drain_timer, 0, 0, 200);
}

void do_final_aichrif(void){
	if (char_fd >= 0) {
		do_close(char_fd);
		char_fd = -1;
	}
	aichrif_state = 0;
}
