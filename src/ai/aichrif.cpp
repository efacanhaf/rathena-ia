// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "aichrif.hpp"

#include <cstring>
#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>

#include <common/ai_packets.hpp>
#include <common/random.hpp>
#include <common/cbasetypes.hpp>
#include <common/showmsg.hpp>
#include <common/socket.hpp>
#include <common/strlib.hpp>
#include <common/timer.hpp>

#include "ai-server.hpp"
#include "chat.hpp"
#include "profile.hpp"
#include "names.hpp"
#include "shell_pool.hpp"
#include "skill_picker.hpp"
#include "spawner.hpp"

int32 char_fd = -1;

static ai_stats g_stats{};
const ai_stats& aichrif_stats(){ return g_stats; }

namespace {
struct shell_state {
	uint32 shell_id;
	uint16 job;            // for skill rotation lookup
	spawn_category cat;    // map category drives behavior
	std::string home_map;  // intended map (target after warp)
	uint16 home_x, home_y; // intended cell on target map
	t_tick warp_at_tick;   // when to fire the initial warp from prontera; 0 = warped
	uint16 base_x, base_y; // anchor point; wander stays within ±radius
	// Last REPORT snapshot (Phase 2.2). Zeroed until first packet arrives.
	uint16 cur_mapindex;
	uint16 home_mapindex_seen; // first reported mapindex (== home)
	uint16 cur_x, cur_y;
	uint32 hp, max_hp;
	uint32 sp, max_sp;
	uint32 target_id;
	uint8  enemy_count;
	PACKET_AI_NEARBY_ENEMY enemies[AI_REPORT_MAX_ENEMIES];
	t_tick last_report_tick;
	t_tick last_cast_tick;  // throttle CAST attempts per shell
	t_tick last_chat_tick;  // throttle ambient chat per shell
	t_tick last_action_tick;
	bool   sitting;
	int8   patrol_dx;       // persistent search direction for field/dungeon
	int8   patrol_dy;       // (-1, 0, +1) per axis; rerolled occasionally
	uint8  patrol_count;    // ticks left on current direction
	t_tick fleeing_until;   // gettick() < this → don't engage, run away
	size_t skill_cursor;    // round-robin cursor into skill rotation
	uint32 last_attacker;   // most recent ATTACKED_BY actor_id
};
std::vector<shell_state> g_shells_local;
std::unordered_map<uint32, size_t> g_shell_idx; // shell_id -> g_shells_local index
// Free-roam wander: town shells take small steps every tick; field/dungeon
// patrols use a larger step (PATROL_STEP) with persistent direction.
constexpr uint16 WANDER_STEP = 5;

// Spawn-emission cursor: walks through spawner_targets() in chunks so the
// network buffer isn't blown up at startup.
size_t g_spawn_target_idx = 0;
uint16 g_spawn_within = 0;
int32  g_spawn_pending = 0;
int32  g_spawn_emitted = 0;
// Phase 4 — gate spawning on map-server readiness. Set true the first time
// a PONG round-trips back from map-server (proves char→map routing AND that
// map-server has finished its initialize() and is parsing AI packets).
// Reset on reconnect so we re-probe after a map restart.
bool   g_map_ready = false;
// Triage step: cap=1 to see if 1 shell + all timers active is enough to
// reproduce the crash, or if it only happens at scale.
constexpr int32 SPAWN_BATCH_PER_TICK = 20;
constexpr int32 SPAWN_HARD_CAP = 1;
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

int32 aichrif_send_attack(int32 fd, uint32 shell_id, uint32 target_id, bool continuous){
	PACKET_AI_CMD_ATTACK_S p{};
	p.hdr.cmd = PACKET_AI_SHELL_CMD;
	p.hdr.len = sizeof(p);
	p.hdr.shell_id = shell_id;
	p.hdr.op = AI_CMD_ATTACK;
	p.target_id = target_id;
	p.continuous = continuous ? 1 : 0;
	WFIFOHEAD(fd, sizeof(p));
	memcpy(WFIFOP(fd, 0), &p, sizeof(p));
	WFIFOSET(fd, sizeof(p));
	g_stats.attacks_sent++;
	return 0;
}

static int32 aichrif_send_simple_op(int32 fd, uint32 shell_id, uint8 op){
	PACKET_AI_SHELL_CMD_HEADER p{};
	p.cmd = PACKET_AI_SHELL_CMD;
	p.len = sizeof(p);
	p.shell_id = shell_id;
	p.op = op;
	WFIFOHEAD(fd, sizeof(p));
	memcpy(WFIFOP(fd, 0), &p, sizeof(p));
	WFIFOSET(fd, sizeof(p));
	return 0;
}

int32 aichrif_send_sit(int32 fd, uint32 shell_id){
	return aichrif_send_simple_op(fd, shell_id, AI_CMD_SIT);
}
int32 aichrif_send_stand(int32 fd, uint32 shell_id){
	return aichrif_send_simple_op(fd, shell_id, AI_CMD_STAND);
}

int32 aichrif_send_warp(int32 fd, uint32 shell_id, const char* map_name, uint16 x, uint16 y){
	g_stats.warps_drift++;
	PACKET_AI_CMD_WARP_S p{};
	p.hdr.cmd = PACKET_AI_SHELL_CMD;
	p.hdr.len = sizeof(p);
	p.hdr.shell_id = shell_id;
	p.hdr.op = AI_CMD_WARP;
	safestrncpy(p.map_name, map_name, sizeof(p.map_name));
	p.x = x;
	p.y = y;
	WFIFOHEAD(fd, sizeof(p));
	memcpy(WFIFOP(fd, 0), &p, sizeof(p));
	WFIFOSET(fd, sizeof(p));
	return 0;
}

int32 aichrif_send_emote(int32 fd, uint32 shell_id, uint8 emote_id){
	PACKET_AI_CMD_EMOTE_S p{};
	p.hdr.cmd = PACKET_AI_SHELL_CMD;
	p.hdr.len = sizeof(p);
	p.hdr.shell_id = shell_id;
	p.hdr.op = AI_CMD_EMOTE;
	p.emote_id = emote_id;
	WFIFOHEAD(fd, sizeof(p));
	memcpy(WFIFOP(fd, 0), &p, sizeof(p));
	WFIFOSET(fd, sizeof(p));
	return 0;
}

int32 aichrif_send_say(int32 fd, uint32 shell_id, const char* msg){
	g_stats.chats_sent++;
	PACKET_AI_CMD_SAY_S p{};
	p.hdr.cmd = PACKET_AI_SHELL_CMD;
	p.hdr.len = sizeof(p);
	p.hdr.shell_id = shell_id;
	p.hdr.op = AI_CMD_SAY;
	safestrncpy(p.mes, msg, sizeof(p.mes));
	p.mes_len = (uint16)strnlen(p.mes, sizeof(p.mes));
	WFIFOHEAD(fd, sizeof(p));
	memcpy(WFIFOP(fd, 0), &p, sizeof(p));
	WFIFOSET(fd, sizeof(p));
	return 0;
}

int32 aichrif_send_cast(int32 fd, uint32 shell_id, const char* skill_name,
		uint16 skill_lv, uint8 kind, uint32 target_id, uint16 x, uint16 y){
	g_stats.casts_sent++;
	PACKET_AI_CMD_CAST_S p{};
	p.hdr.cmd = PACKET_AI_SHELL_CMD;
	p.hdr.len = sizeof(p);
	p.hdr.shell_id = shell_id;
	p.hdr.op = AI_CMD_CAST;
	safestrncpy(p.skill_name, skill_name, sizeof(p.skill_name));
	p.skill_lv = skill_lv;
	p.kind = kind;
	p.target_id = target_id;
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
	p.head_top = init.head_top;
	p.weapon = init.weapon;
	safestrncpy(p.map_name, init.map_name, MAP_NAME_LENGTH_EXT);
	p.x = init.x;
	p.y = init.y;
	p.dir = init.dir;
	p.behavior_id = init.behavior_id;
	p.tier = init.tier;
	p.base_level = init.base_level;
	p.job_level  = init.job_level;
	p.str_ = init.str_; p.agi_ = init.agi_; p.vit_ = init.vit_;
	p.int_ = init.int_; p.dex_ = init.dex_; p.luk_ = init.luk_;
	p.speed    = init.speed;
	for (int i = 0; i < 10; i++) p.equip[i] = init.equip[i];
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
	g_map_ready = false; // re-probe map-server before draining
	ShowStatus("ai-server: handshake OK with char-server (fd=%d). Probing map-server...\n", fd);

	// Phase 1.5c: queue spawn requests; the throttled timer will drain them
	// over time so we don't saturate map-server's wfifo at startup.
	g_spawn_pending = 0;
	for (const auto& t : spawner_targets())
		g_spawn_pending += t.count;
	ShowStatus("ai-server: queued %d shells from population_spawn.yml (waiting for map ready).\n", g_spawn_pending);

	// Wipe any leftovers from a previous ai-server session — map-server
	// caches shells in g_shells and reuses our shell_id pool collides with
	// them. shell_id=0 = wipe-all sentinel (handled in map/aichrif.cpp).
	{
		PACKET_AI_SHELL_DESPAWN_S w{};
		w.cmd = PACKET_AI_SHELL_DESPAWN;
		w.len = sizeof(w);
		w.shell_id = 0;
		w.reason = 0;
		WFIFOHEAD(fd, sizeof(w));
		memcpy(WFIFOP(fd, 0), &w, sizeof(w));
		WFIFOSET(fd, sizeof(w));
	}

	// Probe map-server right away — first pong unlocks the spawn drain.
	aichrif_send_ping(fd);
	return 1;
}

/// 0x2b50 SHELL_SPAWNED ack from map-server.
static int32 aichrif_parse_shell_spawned(int32 fd){
	if (RFIFOREST(fd) < sizeof(PACKET_AI_SHELL_SPAWNED_S)) return 0;
	const PACKET_AI_SHELL_SPAWNED_S* p = (const PACKET_AI_SHELL_SPAWNED_S*)RFIFOP(fd, 0);
	if (p->ok) {
		g_stats.spawn_acked_ok++;
	} else {
		g_stats.spawn_acked_err++;
		// Free the slot, drop the optimistic shell_state, and let the drain
		// timer try again — otherwise SPAWN_HARD_CAP gets eaten by failed
		// spawns and nothing alive ever exists.
		shell_pool_free(p->shell_id);
		auto it = g_shell_idx.find(p->shell_id);
		if (it != g_shell_idx.end()) {
			size_t idx = it->second;
			g_shell_idx.erase(it);
			if (idx < g_shells_local.size()) {
				if (idx + 1 != g_shells_local.size()) {
					g_shells_local[idx] = g_shells_local.back();
					g_shell_idx[g_shells_local[idx].shell_id] = idx;
				}
				g_shells_local.pop_back();
			}
		}
		if (g_spawn_emitted > 0) g_spawn_emitted--;
	}
	ShowInfo("ai-server: shell %u spawn ack ok=%u err=%u.\n", p->shell_id, p->ok, p->err);
	RFIFOSKIP(fd, p->len);
	return 1;
}

/// 0x2b52 SHELL_EVENT — async notification from map-server.
static int32 aichrif_parse_event(int32 fd){
	if (RFIFOREST(fd) < sizeof(PACKET_AI_SHELL_EVENT_S)) return 0;
	const PACKET_AI_SHELL_EVENT_S* p = (const PACKET_AI_SHELL_EVENT_S*)RFIFOP(fd, 0);
	auto it = g_shell_idx.find(p->shell_id);
	if (it != g_shell_idx.end() && it->second < g_shells_local.size()) {
		shell_state& s = g_shells_local[it->second];
		switch (p->kind) {
			case AI_EVT_ATTACKED_BY: {
				g_stats.attacked_by_events++;
				s.last_attacker = p->actor_id;
				// Flee if HP gets low: pause combat for 4s and move away.
				int32 hp_pct = (s.max_hp > 0)
					? (int32)((uint64)s.hp * 100 / s.max_hp) : 100;
				if (hp_pct < 30) {
					s.fleeing_until = gettick() + 4000;
					int16 dx = ((rnd() % 21) - 10) + ((rnd() & 1) ? 8 : -8);
					int16 dy = ((rnd() % 21) - 10) + ((rnd() & 1) ? 8 : -8);
					uint16 tx = (uint16)std::max<int16>(1, (int16)s.cur_x + dx);
					uint16 ty = (uint16)std::max<int16>(1, (int16)s.cur_y + dy);
					aichrif_send_walk_to(char_fd, s.shell_id, tx, ty);
				}
				break;
			}
			case AI_EVT_DIED:
				g_stats.died_events++;
				s.target_id = 0;
				s.fleeing_until = 0;
				break;
			case AI_EVT_RESURRECTED:
				s.fleeing_until = 0;
				break;
			default:
				break;
		}
	}
	RFIFOSKIP(fd, p->len);
	return 1;
}

/// 0x2b51 REPORT { ... } — periodic shell snapshot from map-server.
static int32 aichrif_parse_report(int32 fd){
	if (RFIFOREST(fd) < sizeof(PACKET_AI_SHELL_REPORT_S)) return 0;
	const PACKET_AI_SHELL_REPORT_S* p = (const PACKET_AI_SHELL_REPORT_S*)RFIFOP(fd, 0);
	auto it = g_shell_idx.find(p->shell_id);
	if (it != g_shell_idx.end() && it->second < g_shells_local.size()) {
		shell_state& s = g_shells_local[it->second];
		if (s.home_mapindex_seen == 0) s.home_mapindex_seen = p->mapindex;
		s.cur_mapindex = p->mapindex;
		s.cur_x = p->x; s.cur_y = p->y;
		s.hp = p->hp; s.max_hp = p->max_hp;
		s.sp = p->sp; s.max_sp = p->max_sp;
		s.target_id = p->target_id;
		s.enemy_count = (p->enemy_count > AI_REPORT_MAX_ENEMIES) ? AI_REPORT_MAX_ENEMIES : p->enemy_count;
		for (uint8 i = 0; i < s.enemy_count; i++)
			s.enemies[i] = p->enemies[i];
		s.last_report_tick = gettick();
	}
	RFIFOSKIP(fd, p->len);
	return 1;
}

/// 0x2b53 PONG { len:W=8, token:L }
static int32 aichrif_parse_pong(int32 fd){
	if (RFIFOREST(fd) < 4) return 0;
	int32 plen = RFIFOW(fd, 2);
	if (plen < 8 || RFIFOREST(fd) < (size_t)plen) return 0;
	uint32 token = RFIFOL(fd, 4);
	if (!g_map_ready) {
		g_map_ready = true;
		ShowStatus("ai-server: map-server is ready (first pong, token=%u). Spawn drain unlocked.\n", token);
	}
	RFIFOSKIP(fd, plen);
	return 1;
}

int32 aichrif_parse(int32 fd){
	if (session[fd]->flag.eof) {
		do_close(fd);
		char_fd = -1;
		aichrif_state = 0;
		g_map_ready = false;
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
			case PACKET_AI_SHELL_REPORT:
				if (!aichrif_parse_report(fd))
					return 0;
				break;
			case PACKET_AI_SHELL_EVENT:
				if (!aichrif_parse_event(fd))
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
	static bool fired_once = false;
	if (!fired_once) {
		ShowInfo("ai-server: drain fired state=%d char_fd=%d emitted=%d cap=%d targets=%zu\n",
			aichrif_state, char_fd, g_spawn_emitted, SPAWN_HARD_CAP, spawner_targets().size());
		fired_once = true;
	}
	if (aichrif_state != 2 || char_fd < 0) return 0;
	if (!g_map_ready) return 0; // wait for map-server to ack a ping first
	if (g_spawn_emitted >= SPAWN_HARD_CAP) return 0;
	const auto& targets = spawner_targets();
	if (targets.empty()) return 0;
	int32 emitted = 0;
	while (emitted < SPAWN_BATCH_PER_TICK && g_spawn_emitted < SPAWN_HARD_CAP) {
		// Random target each draw so the hard cap doesn't favor whichever
		// job sat at the front of the shuffled cursor.
		size_t idx = (size_t)(rnd() % targets.size());
		const auto& t = targets[idx];
		if (t.count == 0) {
			emitted++;
			continue;
		}
		if (t.category != spawn_category::FIELD) {
			emitted++;
			continue;
		}
		(void)g_spawn_target_idx; (void)g_spawn_within;
		uint32 sid = shell_pool_alloc();
		if (sid == 0) {
			g_spawn_pending = 0;
			return 0;
		}
		char nm[NAME_LENGTH];
		names_generate(nm);
		// Spawn directly on the target map. map_search_freecell on the
		// map-server side nudges to a walkable cell on a 50-cell radius.
		uint16 bx = (uint16)(40 + (rnd() % 280));
		uint16 by = (uint16)(40 + (rnd() % 280));
		ai_shell_init init{};
		init.shell_id = sid;
		init.name = nm;
		init.class_ = t.job;
		init.sex = (uint8)(rnd() % 2);
		init.hair = (uint16)(1 + rnd() % 20);
		init.hair_color = (uint16)(rnd() % 8);
		// Phase 4: weapon + hat come straight from population_profile.yml
		// (Defaults if no per-job entry). No hardcoded job-specific fallback —
		// shells stay bare-handed/hat-less if the profile says so.
		init.weapon = 0;
		init.head_top = 0;
		uint8 tier = (uint8)((t.category == spawn_category::TOWN) ? 0
			: (t.category == spawn_category::FIELD) ? 1 : 2);
		init.tier = tier;
		init.map_name = t.map_name.c_str();
		init.x = bx;
		init.y = by;
		init.dir = (uint8)(rnd() % 8);
		init.behavior_id = 0;
		// Phase 4 — overlay per-job, per-tier profile from population_profile.yml.
		// All equip + stat fields go into the packet; map-server runs them
		// through status_calc_pc so the shell ends up stat-equivalent to a
		// real player of the same job/level/equip.
		const auto& pr = rathena::server_ai::profile_get(t.job, tier);
		init.base_level = pr.base_level;
		init.job_level  = pr.job_level;
		init.str_ = pr.str; init.agi_ = pr.agi; init.vit_ = pr.vit;
		init.int_ = pr.int_; init.dex_ = pr.dex; init.luk_ = pr.luk;
		init.speed = pr.speed;
		for (int es = 0; es < rathena::server_ai::AI_EQ_COUNT; es++)
			init.equip[es] = pr.equip[es];
		// Sprite mirrors equip — keep weapon/head_top visible IDs in sync.
		init.weapon   = (uint16)init.equip[rathena::server_ai::AI_EQ_HAND_R];
		init.head_top = (uint16)init.equip[rathena::server_ai::AI_EQ_HEAD_TOP];
		// Skip spawn if profile is incomplete for this (job, tier) — better
		// no shell than a misequipped one (Sage with sword, lvl 0, etc.).
		if (init.base_level == 0 || init.equip[rathena::server_ai::AI_EQ_HAND_R] == 0) {
			ShowWarning("ai-server: skip spawn — profile incomplete for job=%u tier=%u (no level or no weapon).\n",
				t.job, tier);
			shell_pool_free(sid);
			emitted++;
			continue;
		}
		aichrif_send_shell_spawn(char_fd, init);
		g_stats.spawned++;
		shell_state st{};
		st.shell_id = sid;
		st.job = t.job;
		st.cat = t.category;
		st.home_map = t.map_name;
		st.home_x = bx;
		st.home_y = by;
		st.base_x = bx;
		st.base_y = by;
		st.warp_at_tick = 0;
		// Seed hp=1/max_hp=1 so the wander timer doesn't treat this shell as
		// dead (hp==0) until the first REPORT lands ~1s later. Real values
		// overwrite on first SHELL_REPORT.
		st.hp = 1;
		st.max_hp = 1;
		g_shell_idx[sid] = g_shells_local.size();
		g_shells_local.push_back(st);
		g_spawn_within++;
		g_spawn_pending--;
		g_spawn_emitted++;
		emitted++;
	}
	return 0;
}

static TIMER_FUNC(aichrif_wander_timer){
	if (aichrif_state != 2 || char_fd < 0) return 0;
	t_tick now_w = gettick();
	for (auto& s : g_shells_local) {
		// Don't override an active attack with a walk command.
		if (s.target_id != 0) continue;
		if (s.hp == 0) continue;
		if (s.sitting) continue; // sitting → stay put
		if (s.fleeing_until && DIFF_TICK(now_w, s.fleeing_until) < 0) continue;
		// Anchor walk on the shell's current position (REPORT-fed). Falls back
		// to spawn anchor if no REPORT yet (cur_x is 0).
		uint16 cx = s.cur_x ? s.cur_x : s.base_x;
		uint16 cy = s.cur_y ? s.cur_y : s.base_y;
		int16 dx, dy;
		if (s.cat == spawn_category::TOWN) {
			// Town: random short stroll, ±8 cells.
			dx = (rnd() % (WANDER_STEP * 2 + 1)) - WANDER_STEP;
			dy = (rnd() % (WANDER_STEP * 2 + 1)) - WANDER_STEP;
		} else {
			// Field/dungeon: persistent patrol direction so we actually cover
			// ground searching for mobs. Reroll direction every 3-6 ticks.
			if (s.patrol_count == 0) {
				s.patrol_dx = (int8)((rnd() % 3) - 1);
				s.patrol_dy = (int8)((rnd() % 3) - 1);
				if (s.patrol_dx == 0 && s.patrol_dy == 0) s.patrol_dx = 1;
				s.patrol_count = (uint8)(3 + (rnd() % 4));
			}
			s.patrol_count--;
			constexpr int16 PATROL_STEP = 8;
			dx = (int16)(s.patrol_dx * PATROL_STEP + ((rnd() % 5) - 2));
			dy = (int16)(s.patrol_dy * PATROL_STEP + ((rnd() % 5) - 2));
		}
		uint16 tx = (uint16)std::max<int16>(1, (int16)cx + dx);
		uint16 ty = (uint16)std::max<int16>(1, (int16)cy + dy);
		aichrif_send_walk_to(char_fd, s.shell_id, tx, ty);
	}
	return 0;
}

/// Phase 2.3+2.5/2.6+3.2: combat tick.
///   - Skips dead shells (hp=0) and town shells.
///   - Per-shell phase offset (shell_id % 4) — only 1/4 of shells act per
///     call, so 250ms tick × 4 phases = each shell decides every ~1s, but
///     the actions are spread out across the second instead of bursting.
///   - Picks among the top-3 closest enemies (weighted toward closest) so
///     ten shells don't dogpile the same poring.
///   - Tries skill_picker_choose first; CAST if a skill matches, else ATTACK.
/// The map-server's ATTACK handler drives the chase via unit_walktobl when
/// out of range (BL_PC's client-driven chase doesn't fire for shells).
static TIMER_FUNC(aichrif_combat_timer){
	using namespace rathena::server_ai;
	if (aichrif_state != 2 || char_fd < 0) return 0;
	static uint32 phase = 0;
	phase = (phase + 1) & 3;
	t_tick now = gettick();
	for (auto& s : g_shells_local) {
		if ((s.shell_id & 3) != phase) continue; // tick offset
		if (s.last_report_tick == 0) continue;
		if (DIFF_TICK(now, s.last_report_tick) > 3000) continue;
		if (s.hp == 0) continue;
		// Town shells don't engage — they're meant to look like idle traffic
		// (chat, sit, vending in Phase 3). Field/dungeon always hunt.
		if (s.cat == spawn_category::TOWN) continue;
		// Fleeing: skip engagement until the timer expires; the WALK_TO that
		// kicked off the flee is already in flight from aichrif_parse_event.
		if (s.fleeing_until && DIFF_TICK(now, s.fleeing_until) < 0) continue;
		// If REPORT shows no enemies in range but target_id is still set,
		// the engagement is stale (mob died, walked away, etc.). Clear it
		// so wander resumes next tick.
		if (s.enemy_count == 0) {
			if (s.target_id != 0) s.target_id = 0;
			continue;
		}
		// Target jitter: pick among top-3 closest (or fewer). Weights:
		// 50% closest, 30% second, 20% third. With <3 enemies, falls back.
		uint8 max_pick = (uint8)std::min<uint8>(s.enemy_count, 3);
		uint8 r = (uint8)(rnd() % 100);
		uint8 idx = (max_pick >= 3 && r >= 80) ? 2
				 : (max_pick >= 2 && r >= 50) ? 1
				 : 0;
		uint32 tid = s.enemies[idx].id;
		if (tid == 0) tid = s.enemies[0].id;
		if (tid == 0) continue;

		const skill_rotation* rot = skill_picker_get(s.job);
		const skill_entry* pick = nullptr;
		if (rot != nullptr && DIFF_TICK(now, s.last_cast_tick) > 5000) {
			shell_ctx ctx;
			ctx.hp = s.hp; ctx.max_hp = s.max_hp;
			ctx.sp = s.sp; ctx.max_sp = s.max_sp;
			ctx.has_target = true;
			ctx.target_hp_pct = s.enemies[idx].hp_pct;
			ctx.target_distance = s.enemies[idx].distance;
			ctx.enemy_count_nearby = s.enemy_count;
			ctx.map_zone = (uint8)((s.cat == spawn_category::TOWN) ? 1
				: (s.cat == spawn_category::FIELD) ? 2 : 3);
			pick = skill_picker_choose(*rot, ctx, &s.skill_cursor);
		}
		if (pick != nullptr && !pick->skill_name.empty()) {
			uint8 kind = AI_CAST_KIND_ID;
			uint32 cast_target = tid;
			if (pick->target == skill_target::SELF) {
				kind = AI_CAST_KIND_SELF;
				cast_target = 0;
			}
			aichrif_send_cast(char_fd, s.shell_id, pick->skill_name.c_str(),
				pick->level, kind, cast_target, 0, 0);
			s.last_cast_tick = now;
		} else {
			aichrif_send_attack(char_fd, s.shell_id, tid, true);
		}
	}
	return 0;
}

/// Phase 3.13c: post-spawn warp from prontera to the shell's intended map.
/// Runs every 500ms, fires AI_CMD_WARP for shells whose warp_at_tick has
/// elapsed. Once warped, shell behavior takes over.
static TIMER_FUNC(aichrif_initial_warp_timer){
	if (aichrif_state != 2 || char_fd < 0) return 0;
	t_tick now = gettick();
	for (auto& s : g_shells_local) {
		if (s.warp_at_tick == 0) continue;
		if (DIFF_TICK(now, s.warp_at_tick) < 0) continue;
		aichrif_send_warp(char_fd, s.shell_id, s.home_map.c_str(), s.home_x, s.home_y);
		s.warp_at_tick = 0;
		// Reset home_mapindex_seen so drift correction re-anchors on the
		// post-warp map (without this it'd think the shell drifted *out*
		// of prontera and warp it back in).
		s.home_mapindex_seen = 0;
		s.cur_mapindex = 0;
	}
	return 0;
}

/// Phase 3.7: drift correction. Once per minute, scan shells whose latest
/// REPORT.mapindex differs from their first-seen one — they wandered into
/// a warp tile or got teleported by a GM — and send AI_CMD_WARP back to
/// home. Only town shells get pulled back; field/dungeon roam is fine.
static TIMER_FUNC(aichrif_drift_timer){
	using namespace rathena::server_ai;
	if (aichrif_state != 2 || char_fd < 0) return 0;
	t_tick now = gettick();
	for (auto& s : g_shells_local) {
		if (s.cat != spawn_category::TOWN) continue;
		if (s.home_mapindex_seen == 0) continue; // never reported
		if (s.cur_mapindex == 0) continue;
		if (s.cur_mapindex == s.home_mapindex_seen) continue;
		// Drifted. Warp back to spawn cell.
		aichrif_send_warp(char_fd, s.shell_id, s.home_map.c_str(), s.home_x, s.home_y);
		s.last_action_tick = now; // pause fidget for a tick
	}
	return 0;
}

/// Phase 3.6: real-clock day phase — 0 day (06-18), 1 evening (18-22),
/// 2 night (22-06). Drives ambient probabilities so the population
/// "breathes" with the time of day.
static uint8 day_phase(){
	std::time_t t = std::time(nullptr);
	std::tm lt{};
#if defined(_WIN32)
	localtime_s(&lt, &t);
#else
	localtime_r(&t, &lt);
#endif
	int32 h = lt.tm_hour;
	if (h >= 6 && h < 18) return 0;   // day
	if (h >= 18 && h < 22) return 1;  // evening
	return 2;                         // night
}

/// Phase 3.5: ambient action — sit/stand cycle for town shells, occasional
/// emote everywhere. Phase-sliced + per-shell throttled so the world looks
/// alive without bursts.
static TIMER_FUNC(aichrif_action_timer){
	using namespace rathena::server_ai;
	if (aichrif_state != 2 || char_fd < 0) return 0;
	t_tick now = gettick();
	static uint32 act_phase = 0;
	act_phase = (act_phase + 1) & 7;
	for (auto& s : g_shells_local) {
		if ((s.shell_id & 7) != act_phase) continue;
		if (s.last_report_tick == 0) continue;
		if (s.hp == 0) continue;
		if (s.target_id != 0) continue; // engaged → don't fidget
		if (s.fleeing_until && DIFF_TICK(now, s.fleeing_until) < 0) continue;
		if (s.last_action_tick && DIFF_TICK(now, s.last_action_tick) < 12000) continue;
		uint32 r = rnd() % 100;
		uint8 phase_now = day_phase();
		if (s.cat == spawn_category::TOWN) {
			// Day: 40% sit/stand, 25% emote. Evening: more sociable (50/30).
			// Night: more sleepy (60% sit, less emote).
			uint32 sit_t = (phase_now == 1 ? 50 : (phase_now == 2 ? 60 : 40));
			uint32 emo_t = sit_t + (phase_now == 0 ? 25 : (phase_now == 1 ? 30 : 10));
			if (r < sit_t) {
				if (s.sitting) { aichrif_send_stand(char_fd, s.shell_id); s.sitting = false; }
				else           { aichrif_send_sit(char_fd, s.shell_id);   s.sitting = true; }
				s.last_action_tick = now;
			} else if (r < emo_t) {
				static const uint8 emotes[] = { 0, 1, 2, 3, 18, 23, 28, 30, 33, 50 };
				aichrif_send_emote(char_fd, s.shell_id, emotes[rnd() % (sizeof(emotes)/sizeof(emotes[0]))]);
				s.last_action_tick = now;
			}
		} else {
			// Field/dungeon: shells should never sit/idle here (player feedback).
			// If somehow sitting (carried from a prior tier), stand back up.
			// Allow a rare emote between mobs.
			if (s.sitting) {
				aichrif_send_stand(char_fd, s.shell_id);
				s.sitting = false;
				s.last_action_tick = now;
			} else if (r < 12) {
				static const uint8 emotes_f[] = { 1, 6, 7, 8, 28, 33, 49 };
				aichrif_send_emote(char_fd, s.shell_id, emotes_f[rnd() % (sizeof(emotes_f)/sizeof(emotes_f[0]))]);
				s.last_action_tick = now;
			}
		}
	}
	return 0;
}

/// Phase 3.4: ambient chat. Every tick, walk a slice of shells (phase
/// offset so they don't all chat at once) and pick a category based on
/// state — town/idle, combat hunt, low-hp lost. Each shell self-throttles.
static TIMER_FUNC(aichrif_chat_timer){
	using namespace rathena::server_ai;
	if (aichrif_state != 2 || char_fd < 0) return 0;
	t_tick now = gettick();
	static uint32 chat_phase = 0;
	chat_phase = (chat_phase + 1) & 7;
	for (auto& s : g_shells_local) {
		if ((s.shell_id & 7) != chat_phase) continue;
		if (s.last_report_tick == 0) continue;
		if (s.hp == 0) continue;
		// Per-shell throttle: max one line every ~25-40s; even longer at night.
		uint8 ph = day_phase();
		t_tick min_gap = (ph == 2 ? 60000 : (ph == 1 ? 25000 : 30000));
		if (s.last_chat_tick && DIFF_TICK(now, s.last_chat_tick) < min_gap) continue;
		uint32 chance = (ph == 2 ? 12 : (ph == 1 ? 35 : 30));
		if ((rnd() % 100) >= chance) continue;
		const char* cat = "idle";
		if (s.cat == spawn_category::DUNGEON) cat = (s.enemy_count > 0) ? "hunt" : "grind";
		else if (s.cat == spawn_category::FIELD) cat = (s.enemy_count > 0) ? "hunt" : "grind";
		else cat = (ph == 2) ? "night_day" : ((rnd() & 1) ? "idle" : "greet");
		const std::string* line = chat_pick(cat);
		if (line == nullptr || line->empty()) continue;
		aichrif_send_say(char_fd, s.shell_id, line->c_str());
		s.last_chat_tick = now;
	}
	return 0;
}

void aichrif_list_shells(){
	if (g_shells_local.empty()) {
		ShowInfo("No live shells.\n");
		return;
	}
	ShowInfo("ai-server: %zu live shells\n", g_shells_local.size());
	ShowInfo("  id        | map (home) | cur (x,y) | hp%%   | cat | last_report_ago_ms\n");
	t_tick now = gettick();
	for (auto& s : g_shells_local) {
		int32 hp_pct = (s.max_hp > 0) ? (int32)((uint64)s.hp * 100 / s.max_hp) : -1;
		t_tick age = s.last_report_tick ? DIFF_TICK(now, s.last_report_tick) : -1;
		const char* cat_s = (s.cat == spawn_category::TOWN ? "TOWN" : s.cat == spawn_category::FIELD ? "FLD" : "DUN");
		const char* drift = (s.home_mapindex_seen && s.cur_mapindex && s.cur_mapindex != s.home_mapindex_seen) ? "*" : " ";
		ShowInfo("  %-9u | %s%-9s | (%3u,%3u)  | %3d%%  | %-3s | %lldms\n",
			s.shell_id, drift, s.home_map.c_str(),
			s.cur_x, s.cur_y, hp_pct, cat_s, (long long)age);
	}
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
	// Wander loop: short 1.5s cadence so movement reads as continuous walking
	// instead of stop-go bursts. Step sizes are small enough to keep this
	// from looking jittery.
	add_timer_func_list(aichrif_wander_timer, "aichrif_wander");
	add_timer_interval(gettick() + 5 * 1000, aichrif_wander_timer, 0, 0, 1500);
	// Combat tick: every 250ms, re-issue ATTACK so a stalled chase resumes.
	add_timer_func_list(aichrif_combat_timer, "aichrif_combat");
	add_timer_interval(gettick() + 2 * 1000, aichrif_combat_timer, 0, 0, 250);
	// Ambient chat: every 3s pick a few shells and have them say a line.
	add_timer_func_list(aichrif_chat_timer, "aichrif_chat");
	add_timer_interval(gettick() + 8 * 1000, aichrif_chat_timer, 0, 0, 3000);
	// Ambient action: sit/stand/emote for town shells; rare emote elsewhere.
	add_timer_func_list(aichrif_action_timer, "aichrif_action");
	add_timer_interval(gettick() + 10 * 1000, aichrif_action_timer, 0, 0, 5000);
	// Drift correction: pull town shells back home if they warped out.
	add_timer_func_list(aichrif_drift_timer, "aichrif_drift");
	add_timer_interval(gettick() + 60 * 1000, aichrif_drift_timer, 0, 0, 60 * 1000);
	// Initial post-spawn warp from prontera to the intended target map.
	add_timer_func_list(aichrif_initial_warp_timer, "aichrif_initial_warp");
	add_timer_interval(gettick() + 2000, aichrif_initial_warp_timer, 0, 0, 500);
	// Spawn drain: tighter cadence. 20/tick × 50ms tick = 400/s burst.
	add_timer_func_list(aichrif_spawn_drain_timer, "aichrif_spawn_drain");
	add_timer_interval(gettick() + 1 * 1000, aichrif_spawn_drain_timer, 0, 0, 50);
}

void do_final_aichrif(void){
	if (char_fd >= 0) {
		do_close(char_fd);
		char_fd = -1;
	}
	aichrif_state = 0;
}
