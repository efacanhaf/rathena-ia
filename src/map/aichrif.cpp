// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "aichrif.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdlib>
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
#include "mob.hpp"
#include "pc.hpp"
#include "skill.hpp"
#include "status.hpp"
#include "unit.hpp"

extern int32 char_fd; ///< inter-server fd to char-server (defined in chrif.cpp)

namespace {
/// account_id -> sd, lifetime-managed by aishell_create / aishell_destroy.
std::unordered_map<uint32, map_session_data*> g_shells;
/// Per-shell hp tracking so the REPORT timer can synthesise ATTACKED_BY
/// events without hooking status_damage (status.cpp is off-limits).
std::unordered_map<uint32, uint32> g_last_hp;
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

	// fd=-1: shells have no client. session_isValid(fd) returns false for
	// negative fd, so clif's SELF send path becomes a no-op. Passing 0
	// hits session[0] (console listener) and crashes WFIFOHEAD.
	pc_setnewpc(sd, p->shell_id, p->shell_id, 0, gettick(), p->sex, -1);

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
	sd->base_status.mode = (e_mode)(MD_CANMOVE | MD_CANATTACK);
	sd->battle_status.mode = (e_mode)(MD_CANMOVE | MD_CANATTACK);
	// Melee range 1 — without this, status_get_range returns 0 and
	// unit_attack/battle_check_range never lets the shell connect.
	sd->battle_status.rhw.range = 1;
	sd->base_status.rhw.range = 1;
	sd->battle_status.amotion = 1000; // attack motion
	sd->battle_status.adelay = 1500;  // attack delay
	sd->battle_status.dmotion = 500;  // damage motion
	// Phase 2 baseline so hit rate isn't garbage. Without status_calc_pc
	// running, hit/flee/atk all default to 0 and the shell whiffs every
	// poring. Real per-job stat tables come in Phase 4 (job profiles).
	sd->status.base_level = 50;
	sd->status.job_level = 30;
	sd->status.str = 50; sd->status.agi = 50; sd->status.vit = 30;
	sd->status.int_ = 30; sd->status.dex = 50; sd->status.luk = 30;
	sd->battle_status.str = 50; sd->battle_status.agi = 50; sd->battle_status.vit = 30;
	sd->battle_status.int_ = 30; sd->battle_status.dex = 50; sd->battle_status.luk = 30;
	sd->battle_status.batk = 80;
	sd->battle_status.rhw.atk = 80;
	sd->battle_status.rhw.atk2 = 100;
	sd->battle_status.hit = 150;       // base_lv 50 + dex 50 + buffer
	sd->battle_status.flee = 100;      // base_lv 50 + agi 50
	sd->battle_status.flee2 = 10;
	sd->battle_status.cri = 10;
	sd->battle_status.def = 5;
	sd->battle_status.def2 = 10;
	sd->battle_status.mdef = 5;
	sd->battle_status.mdef2 = 10;
	sd->battle_status.matk_min = 30;
	sd->battle_status.matk_max = 50;

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
	g_last_hp.erase(shell_id);

	clif_clearunit_area(*sd, CLR_OUTSIGHT);
	map_delblock(sd);
	map_deliddb(sd);
	if (sd->regs.vars) db_destroy(sd->regs.vars);
	sd->~map_session_data();
	aFree(sd);
}

// ---------------------------------------------------------------------------
// REPORT timer: streams shell snapshot + nearby_enemies back to ai-server.
// ---------------------------------------------------------------------------

constexpr int16 AI_REPORT_RANGE = 20;   // wider than client AREA so shells engage early
constexpr int32 AI_REPORT_PERIOD_MS = 1000;

namespace {
struct enemy_scan_ctx {
	const map_session_data* center;
	uint8 count;
	PACKET_AI_NEARBY_ENEMY rows[AI_REPORT_MAX_ENEMIES];
};
}

/// va_list callback for map_foreachinrange. Picks BL_MOB only; insertion-sort
/// by chebyshev distance into a fixed-size top-N table.
static int32 ai_report_collect_mob(block_list* bl, va_list ap){
	enemy_scan_ctx* ctx = va_arg(ap, enemy_scan_ctx*);
	if (bl->type != BL_MOB) return 0;
	mob_data* md = (mob_data*)bl;
	if (status_isdead(*bl)) return 0;

	int16 dx = bl->x - ctx->center->x;
	int16 dy = bl->y - ctx->center->y;
	int16 ad = (int16)std::max(std::abs(dx), std::abs(dy));
	uint8 dist = (ad < 0 ? 0 : (ad > 255 ? 255 : (uint8)ad));

	uint16 hp_pct = 0;
	if (md->status.max_hp > 0) {
		uint64 v = (uint64)md->status.hp * 100 / md->status.max_hp;
		hp_pct = (uint16)(v > 100 ? 100 : v);
	}

	PACKET_AI_NEARBY_ENEMY row{};
	row.id = bl->id;
	row.mob_class = (uint16)md->mob_id;
	row.hp_pct = hp_pct;
	row.x = (uint16)bl->x;
	row.y = (uint16)bl->y;
	row.distance = dist;

	// insert keeping ascending distance order; cap at AI_REPORT_MAX_ENEMIES.
	uint8 i = ctx->count;
	while (i > 0 && ctx->rows[i - 1].distance > dist) {
		if (i < AI_REPORT_MAX_ENEMIES)
			ctx->rows[i] = ctx->rows[i - 1];
		i--;
	}
	if (i < AI_REPORT_MAX_ENEMIES)
		ctx->rows[i] = row;
	if (ctx->count < AI_REPORT_MAX_ENEMIES)
		ctx->count++;
	return 1;
}

/// Emit a SHELL_EVENT (0x2b52) async notification to ai-server.
static void aichrif_send_event(uint32 shell_id, uint8 kind, uint32 actor_id, uint32 dmg){
	if (char_fd < 0 || session[char_fd] == nullptr) return;
	PACKET_AI_SHELL_EVENT_S e{};
	e.cmd = PACKET_AI_SHELL_EVENT;
	e.len = sizeof(e);
	e.shell_id = shell_id;
	e.kind = kind;
	e.actor_id = actor_id;
	e.dmg = dmg;
	WFIFOHEAD(char_fd, sizeof(e));
	memcpy(WFIFOP(char_fd, 0), &e, sizeof(e));
	WFIFOSET(char_fd, sizeof(e));
}

static void aichrif_send_report(uint32 shell_id, const map_session_data* sd){
	if (char_fd < 0 || session[char_fd] == nullptr) return;

	// Synthesise ATTACKED_BY from hp delta. Without hooking status_damage we
	// can't see the real attacker, so attribute the damage to the closest
	// nearby enemy when one exists. Phase 3 can add a proper hook.
	uint32 cur_hp = (uint32)sd->battle_status.hp;
	auto it = g_last_hp.find(shell_id);
	if (it != g_last_hp.end()) {
		if (cur_hp == 0 && it->second > 0) {
			aichrif_send_event(shell_id, AI_EVT_DIED, 0, it->second);
		} else if (cur_hp < it->second) {
			uint32 dmg = it->second - cur_hp;
			// Find the closest BL_MOB as best-guess attacker.
			enemy_scan_ctx pre{};
			pre.center = sd;
			map_foreachinrange(ai_report_collect_mob, sd, 4, BL_MOB, &pre);
			uint32 actor = pre.count ? pre.rows[0].id : 0;
			aichrif_send_event(shell_id, AI_EVT_ATTACKED_BY, actor, dmg);
		} else if (cur_hp > 0 && it->second == 0) {
			aichrif_send_event(shell_id, AI_EVT_RESURRECTED, 0, 0);
		}
	}
	g_last_hp[shell_id] = cur_hp;

	enemy_scan_ctx ctx{};
	ctx.center = sd;
	map_foreachinrange(ai_report_collect_mob, sd, AI_REPORT_RANGE, BL_MOB, &ctx);

	PACKET_AI_SHELL_REPORT_S p{};
	p.cmd = PACKET_AI_SHELL_REPORT;
	p.len = sizeof(p);
	p.shell_id = shell_id;
	p.x = (uint16)sd->x;
	p.y = (uint16)sd->y;
	p.hp = (uint32)sd->battle_status.hp;
	p.max_hp = (uint32)sd->battle_status.max_hp;
	p.sp = (uint32)sd->battle_status.sp;
	p.max_sp = (uint32)sd->battle_status.max_sp;
	p.target_id = (uint32)sd->ud.target;
	p.enemy_count = ctx.count;
	for (uint8 i = 0; i < ctx.count; i++)
		p.enemies[i] = ctx.rows[i];

	WFIFOHEAD(char_fd, sizeof(p));
	memcpy(WFIFOP(char_fd, 0), &p, sizeof(p));
	WFIFOSET(char_fd, sizeof(p));
}

static TIMER_FUNC(aichrif_report_timer){
	if (char_fd < 0 || session[char_fd] == nullptr) return 0;
	for (auto& kv : g_shells) {
		map_session_data* sd = kv.second;
		if (sd == nullptr) continue;
		aichrif_send_report(kv.first, sd);
	}
	return 0;
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
	// Drop motion/attack commands while the shell is dead so corpses don't
	// teleport or respawn from leftover ATTACKs in the AI queue. Resurrection
	// (skill/item from a player) bypasses this — it goes through status code
	// directly and restores hp; once hp > 0 the AI naturally resumes.
	if (sd->battle_status.hp == 0) return 1;
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
			block_list* tgt = map_id2bl(a->target_id);
			if (tgt == nullptr) break;
			int32 range = sd->battle_status.rhw.range;
			// unit_attack_timer_sub treats BL_PC as client-driven chase: when
			// the target is out of range it sends clif_movetoattack and waits
			// for a WALK_TO from the client. Shells have no client, so we
			// drive the chase here just like mob_data does (unit.cpp:3274).
			if (!check_distance_bl(sd, tgt, range)) {
				unit_walktobl(sd, tgt, range, 1 | 2); // easy walk + chase mode
			}
			unit_attack(sd, a->target_id, a->continuous);
			break;
		}
		case AI_CMD_STOP_ATTACK: {
			unit_stop_attack(sd);
			break;
		}
		case AI_CMD_SAY: {
			if (RFIFOREST(fd) < sizeof(PACKET_AI_CMD_SAY_S)) return 0;
			const PACKET_AI_CMD_SAY_S* s = (const PACKET_AI_CMD_SAY_S*)RFIFOP(fd, 0);
			char buf[200];
			int32 plen = (int32)s->mes_len;
			if (plen <= 0 || plen >= (int32)sizeof(s->mes)) break;
			// "Name : message" overhead + AREA broadcast.
			int32 n = snprintf(buf, sizeof(buf), "%s : %.*s",
				sd->status.name, plen, s->mes);
			if (n > 0)
				clif_disp_overhead(sd, buf);
			break;
		}
		case AI_CMD_CAST: {
			if (RFIFOREST(fd) < sizeof(PACKET_AI_CMD_CAST_S)) return 0;
			const PACKET_AI_CMD_CAST_S* c = (const PACKET_AI_CMD_CAST_S*)RFIFOP(fd, 0);
			char nm[sizeof(c->skill_name) + 1] = {0};
			memcpy(nm, c->skill_name, sizeof(c->skill_name));
			uint16 sid = skill_name2id(nm);
			if (sid == 0) {
				ShowWarning("ai-server: CAST unknown skill '%s' (shell %u).\n", nm, hdr->shell_id);
				break;
			}
			if (c->kind == 1) {
				unit_skilluse_pos(sd, c->x, c->y, sid, c->skill_lv);
			} else {
				uint32 tid = (c->kind == 2) ? sd->id : c->target_id;
				unit_skilluse_id(sd, tid, sid, c->skill_lv);
			}
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
	add_timer_func_list(aichrif_report_timer, "aichrif_report");
	add_timer_interval(gettick() + AI_REPORT_PERIOD_MS, aichrif_report_timer,
		0, 0, AI_REPORT_PERIOD_MS);
}

void do_final_map_aichrif(void){
	// Snapshot the keys to avoid mutating while iterating.
	std::vector<uint32> ids;
	ids.reserve(g_shells.size());
	for (auto& kv : g_shells) ids.push_back(kv.first);
	for (uint32 id : ids) aishell_destroy(id);
}
