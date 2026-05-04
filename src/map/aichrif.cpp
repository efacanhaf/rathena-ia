// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "aichrif.hpp"

#include <cstring>
#include <unordered_map>
#include <vector>

#include <common/ai_packets.hpp>
#include <common/cbasetypes.hpp>
#include <common/db.hpp>
#include <common/malloc.hpp>
#include <common/showmsg.hpp>
#include <common/socket.hpp>
#include <common/strlib.hpp>
#include <common/timer.hpp>

#include "battle.hpp"
#include "chrif.hpp"
#include "clif.hpp"
#include <common/mapindex.hpp>

#include "map.hpp"
#include "pc.hpp"
#include "status.hpp"
#include "unit.hpp"

extern int32 char_fd; ///< inter-server fd to char-server (defined in chrif.cpp)

namespace {
/// account_id -> sd, lifetime-managed by aishell_create / aishell_destroy.
std::unordered_map<uint32, map_session_data*> g_shells;
}

bool aishell_is_shell(uint32 account_id){
	return account_id >= 95'000'000 && account_id < 100'000'000;
}

map_session_data* aishell_find(uint32 shell_id){
	auto it = g_shells.find(shell_id);
	return it == g_shells.end() ? nullptr : it->second;
}

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

/// Allocate a viable map_session_data for a shell. Mirrors the autotrade-load
/// pattern (buyingstore.cpp) but skips chrif_authreq and inventory loading —
/// shells don't have a row in `char` and don't carry items in Phase 1.
static map_session_data* aishell_create(const PACKET_AI_SHELL_SPAWN_S* p){
	int16 m = map_mapname2mapid(p->map_name);
	if (m < 0) {
		ShowError("aishell_create: map '%s' unknown.\n", p->map_name);
		return nullptr;
	}
	if (g_shells.count(p->shell_id)) {
		ShowWarning("aishell_create: shell_id %u already alive.\n", p->shell_id);
		return nullptr;
	}

	map_session_data* sd = nullptr;
	CREATE(sd, map_session_data, 1);
	new (sd) map_session_data();

	pc_setnewpc(sd, p->shell_id, p->shell_id, 0, gettick(), p->sex, 0);

	// Identity
	safestrncpy(sd->status.name, p->name, NAME_LENGTH);
	sd->status.class_ = p->class_;
	sd->status.hair = p->hair;
	sd->status.hair_color = p->hair_color;
	sd->status.clothes_color = p->cloth_color;
	sd->status.head_top = p->head_top;
	sd->status.head_mid = p->head_mid;
	sd->status.head_bottom = p->head_bottom;
	sd->status.weapon = p->weapon;
	sd->status.shield = p->shield;
	sd->status.robe = p->robe;
	sd->status.body = p->class_;
	sd->status.base_level = 1;
	sd->status.job_level = 1;
	sd->status.zeny = 0;
	sd->status.guild_id = 0;
	sd->status.party_id = 0;
	safestrncpy(sd->status.last_point.map, p->map_name, sizeof(sd->status.last_point.map));
	sd->status.last_point.x = p->x;
	sd->status.last_point.y = p->y;

	uint64 mapid = pc_jobid2mapid(sd->status.class_);
	sd->class_ = ((int64)mapid < 0) ? MAPID_NOVICE : mapid;

	// Geometry
	sd->m = m;
	sd->x = p->x;
	sd->y = p->y;
	sd->mapindex = mapindex_name2id(p->map_name);

	unit_dataset(sd);
	sd->ud.dir = p->dir;
	sd->head_dir = p->dir;

	// Timers required by clif/unit
	sd->followtimer = INVALID_TIMER;
	sd->invincible_timer = INVALID_TIMER;
	sd->npc_timer_id = INVALID_TIMER;
	sd->pvp_timer = INVALID_TIMER;
	sd->expiration_tid = INVALID_TIMER;
	sd->autotrade_tid = INVALID_TIMER;
	sd->respawn_tid = INVALID_TIMER;
	sd->tid_queue_active = INVALID_TIMER;
	sd->macro_detect.timer = INVALID_TIMER;
	sd->skill_keep_using.tid = INVALID_TIMER;
	sd->rental_timer = INVALID_TIMER;
	for (int32 i = 0; i < MAX_SPIRITBALL; i++) sd->spirit_timer[i] = INVALID_TIMER;
	for (int32 i = 0; i < MAX_EVENTTIMER; i++) sd->eventtimer[i] = INVALID_TIMER;
	for (int32 i = 0; i < 3; i++) sd->hate_mob[i] = -1;

	// Stats / status
	sd->battle_status.hp = sd->battle_status.max_hp = 100;
	sd->battle_status.sp = sd->battle_status.max_sp = 100;
	sd->battle_status.speed = DEFAULT_WALK_SPEED;
	sd->base_status.speed = DEFAULT_WALK_SPEED;
	sd->battle_status.size = SZ_SMALL;
	sd->battle_status.race = RC_PLAYER_HUMAN;
	sd->base_status.mode = MD_CANMOVE;
	sd->battle_status.mode = MD_CANMOVE;

	// Vars allocator (used by anti-bot, scripts; safe to alloc empty).
	sd->regs.vars = i64db_alloc(DB_OPT_BASE);

	sd->state.active = 1;
	sd->state.connect_new = 0;
	sd->state.pc_loaded = 1;

	status_set_viewdata(sd, sd->status.class_);

	map_addiddb(sd);
	if (map_addblock(sd) != 0) {
		ShowError("aishell_create: map_addblock failed for shell %u.\n", p->shell_id);
		map_deliddb(sd);
		db_destroy(sd->regs.vars);
		sd->~map_session_data();
		aFree(sd);
		return nullptr;
	}
	clif_spawn(sd);

	g_shells[p->shell_id] = sd;
	ShowStatus("ai-server: shell %u '%s' spawned at %s(%d,%d).\n",
		p->shell_id, p->name, p->map_name, p->x, p->y);
	return sd;
}

void aishell_destroy(uint32 shell_id){
	auto it = g_shells.find(shell_id);
	if (it == g_shells.end()) return;
	map_session_data* sd = it->second;
	g_shells.erase(it);

	clif_clearunit_area(*sd, CLR_OUTSIGHT);
	map_delblock(sd);
	map_deliddb(sd);
	if (sd->regs.vars) db_destroy(sd->regs.vars);
	sd->~map_session_data();
	aFree(sd);
}

/// Phase 1.4: actually create the shell.
static int32 aichrif_handle_spawn(int32 fd){
	if (RFIFOREST(fd) < sizeof(PACKET_AI_SHELL_SPAWN_S))
		return 0;
	const PACKET_AI_SHELL_SPAWN_S* p = (const PACKET_AI_SHELL_SPAWN_S*)RFIFOP(fd, 0);
	map_session_data* sd = aishell_create(p);
	aichrif_send_spawned_ack(p->shell_id, sd != nullptr, sd != nullptr ? 0 : 1);
	return 1;
}

static int32 aichrif_handle_despawn(int32 fd){
	uint32 shell_id = RFIFOL(fd, 4);
	aishell_destroy(shell_id);
	ShowInfo("ai-server: SHELL_DESPAWN id=%u.\n", shell_id);
	return 1;
}

static int32 aichrif_handle_cmd(int32 fd){
	if (RFIFOREST(fd) < sizeof(PACKET_AI_SHELL_CMD_HEADER))
		return 0;
	const PACKET_AI_SHELL_CMD_HEADER* hdr = (const PACKET_AI_SHELL_CMD_HEADER*)RFIFOP(fd, 0);
	map_session_data* sd = aishell_find(hdr->shell_id);
	if (sd == nullptr) {
		ShowWarning("ai-server: SHELL_CMD for unknown shell %u (op=%u).\n", hdr->shell_id, hdr->op);
		return 1;
	}
	switch (hdr->op) {
		case AI_CMD_WALK_TO: {
			if (RFIFOREST(fd) < sizeof(PACKET_AI_CMD_WALK_TO_S)) return 0;
			const PACKET_AI_CMD_WALK_TO_S* w = (const PACKET_AI_CMD_WALK_TO_S*)RFIFOP(fd, 0);
			// flag=2 bypasses status_bl_has_mode(MD_CANMOVE) + unit_can_move(),
			// neither of which is initialised on a shell sd (no status_calc_pc).
			unit_walktoxy(sd, w->x, w->y, 2);
			break;
		}
		case AI_CMD_ATTACK: {
			if (RFIFOREST(fd) < sizeof(PACKET_AI_CMD_ATTACK_S)) return 0;
			const PACKET_AI_CMD_ATTACK_S* a = (const PACKET_AI_CMD_ATTACK_S*)RFIFOP(fd, 0);
			unit_attack(sd, a->target_id, a->continuous);
			break;
		}
		case AI_CMD_STOP_ATTACK: {
			unit_stop_attack(sd);
			break;
		}
		default:
			ShowWarning("ai-server: SHELL_CMD unknown op=%u (shell %u).\n", hdr->op, hdr->shell_id);
			break;
	}
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
	g_shells.reserve(8192);
}

void do_final_map_aichrif(void){
	// Snapshot the keys to avoid mutating while iterating.
	std::vector<uint32> ids;
	ids.reserve(g_shells.size());
	for (auto& kv : g_shells) ids.push_back(kv.first);
	for (uint32 id : ids) aishell_destroy(id);
}
