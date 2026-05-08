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
#include "party.hpp"
#include "clif.hpp"
#include <common/mapindex.hpp>

#include "map.hpp"
#include "mob.hpp"
#include "itemdb.hpp"
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
/// Phase 6 — owner_aid per hired-mercenary shell. 0 / absent = autonomous
/// (population spawner). Looked up at REPORT time to fill owner_* fields,
/// and used to emit AI_EVT_OWNER_GONE when the owner logs out.
std::unordered_map<uint32, uint32> g_owner_aid;
/// Phase 6 — owner char_id per hired-mercenary shell. The merc is bound
/// to a specific character — switching to a different char on the same
/// account does NOT count as the owner being present. We verify cid on
/// every REPORT and on the relogin poll.
std::unordered_map<uint32, uint32> g_owner_cid;
/// Whether each owned shell has seen its owner online at least once. Used
/// to defer AI_EVT_OWNER_GONE until after the first sighting (otherwise
/// shells spawn just-before the owner zones in and immediately get nuked).
std::unordered_map<uint32, bool> g_owner_seen_once;
/// Phase 6 — gettick() of last successful map_id2sd lookup per shell.
/// We treat the owner as "gone" only after the lookup has failed for
/// >= AI_OWNER_GONE_GRACE_MS, which lets the player zone between maps
/// without nuking the merc.
std::unordered_map<uint32, t_tick> g_owner_last_seen_tick;
constexpr t_tick AI_OWNER_GONE_GRACE_MS = 8000;

/// Phase 6 — shells whose owner went offline. Map-server has already
/// despawned the shell unit; we keep the owner identity so the relogin
/// poll can fire AI_EVT_OWNER_BACK once the right character reconnects,
/// and ai-server re-spawns the merc next to them.
struct suspended_owner_t { uint32 aid; uint32 cid; };
std::unordered_map<uint32, suspended_owner_t> g_suspended_owner;
constexpr t_tick AI_OWNER_BACK_POLL_MS = 1000;
}

bool aishell_is_shell(uint32 account_id){
	return account_id >= 95'000'000 && account_id < 100'000'000;
}

map_session_data* aishell_find(uint32 shell_id){
	auto it = g_shells.find(shell_id);
	return it == g_shells.end() ? nullptr : it->second;
}

/// Reply to ai-server with PACKET_AI_PONG. Length-prefixed (8 bytes).
int32 aichrif_send_hire(uint32 owner_aid, uint32 owner_cid, uint16 job, uint8 tier,
		const char* map_name, uint16 x, uint16 y, uint32 duration_ms,
		uint16 base_level_override, uint16 job_level_override){
	if (char_fd < 0 || session[char_fd] == nullptr) return -1;
	PACKET_AI_HIRE_REQUEST_S p{};
	p.cmd = PACKET_AI_HIRE_REQUEST;
	p.len = sizeof(p);
	p.owner_aid = owner_aid;
	p.owner_cid = owner_cid;
	p.job = job;
	p.tier = tier;
	safestrncpy(p.map_name, map_name, MAP_NAME_LENGTH_EXT);
	p.x = x;
	p.y = y;
	p.duration_ms = duration_ms;
	p.base_level_override = base_level_override;
	p.job_level_override  = job_level_override;
	WFIFOHEAD(char_fd, sizeof(p));
	memcpy(WFIFOP(char_fd, 0), &p, sizeof(p));
	WFIFOSET(char_fd, sizeof(p));
	return 0;
}

int32 aichrif_send_dismiss(uint32 owner_cid){
	if (char_fd < 0 || session[char_fd] == nullptr) return -1;
	PACKET_AI_DISMISS_REQUEST_S p{};
	p.cmd = PACKET_AI_DISMISS_REQUEST;
	p.len = sizeof(p);
	p.owner_cid = owner_cid;
	WFIFOHEAD(char_fd, sizeof(p));
	memcpy(WFIFOP(char_fd, 0), &p, sizeof(p));
	WFIFOSET(char_fd, sizeof(p));
	return 0;
}

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
	// View IDs come straight from the packet — head_top/mid/bottom are item
	// nameids (or view IDs) but weapon/shield/robe are WEAPON_TYPE / shield
	// view enums set by status_calc_pc → pc_calcweapontype based on what's
	// actually equipped after pc_setequipindex. Don't override them here.
	sd->status.head_top = p->head_top;
	sd->status.head_mid = p->head_mid;
	sd->status.head_bottom = p->head_bottom;
	sd->status.weapon = 0;
	sd->status.shield = 0;
	sd->status.robe = 0;
	sd->status.body = p->class_; // pc_jobchange sets body = class_; mirror that
	sd->status.base_level = 1;
	sd->status.job_level = 1;
	sd->status.zeny = 0;
	sd->status.guild_id = 0;
	sd->status.party_id = 0;
	safestrncpy(sd->status.last_point.map, p->map_name, sizeof(sd->status.last_point.map));
	sd->status.last_point.x = p->x;
	sd->status.last_point.y = p->y;

	// Mirror pc_authok class validation exactly — if the job isn't valid the
	// REAL pc_authok would have reset to NOVICE, and any difference between
	// shell flow and real-player flow shows up in the client as a Novice
	// sprite even though we set Sage on the server side.
	uint64 mapid = pc_jobid2mapid(sd->status.class_);
	if (mapid == (uint64)-1 || !job_db.exists(sd->status.class_)) {
		ShowError("aishell_create: invalid class %u for shell %u — falling back to NOVICE.\n",
			sd->status.class_, p->shell_id);
		sd->status.class_ = JOB_NOVICE;
		sd->class_ = MAPID_NOVICE;
	} else {
		sd->class_ = mapid;
	}

	// Geometry
	sd->m = m;
	sd->x = p->x;
	sd->y = p->y;
	sd->mapindex = mapindex_name2id(p->map_name);
	// Phase 3.13: nudge to a walkable cell. If the freecell search fails
	// (e.g. requested cell is way out of map bounds), drop the spawn —
	// otherwise map_addblock crashes the server with out-of-bounds coords.
	// Phase 6 — hired mercs use a tight 4-cell radius so they spawn next
	// to the player who hired them; autonomous shells take the wider
	// 100-cell sweep since they're seeded on randomly-picked cells.
	{
		int16 sx = sd->x, sy = sd->y;
		int16 range = (p->owner_aid != 0) ? 4 : 100;
		if (map_search_freecell(nullptr, m, &sx, &sy, range, range, 1)) {
			sd->x = sx;
			sd->y = sy;
		} else {
			ShowError("aishell_create: no walkable cell near %s(%d,%d).\n",
				p->map_name, p->x, p->y);
			sd->~map_session_data();
			aFree(sd);
			return nullptr;
		}
	}

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

	// Phase 4: feed sd through the canonical PC stat path. We populate
	// status (level + stats) and inventory (equipped items) the same way
	// pc_authok would after char-server load, then call status_calc_pc.
	// Result: shell at lv X with weapon Y has the SAME final stats as a
	// real player would, no hardcoded battle_status anywhere.
	sd->status.base_level = p->base_level ? p->base_level : 1;
	sd->status.job_level  = p->job_level  ? p->job_level  : 1;
	sd->status.str = p->str_; sd->status.agi = p->agi_;
	sd->status.vit = p->vit_; sd->status.int_ = p->int_;
	sd->status.dex = p->dex_; sd->status.luk = p->luk_;
	// HP/SP get overwritten by status_calc_pc but seed with 1 so it doesn't
	// look "dead" before the calc runs (status.hp == 0 is the dead check).
	sd->status.hp = 1; sd->status.sp = 1;

	// Vars allocator (anti-bot, scripts) — alloc empty before any pc_*.
	sd->regs.vars = i64db_alloc(DB_OPT_BASE);

	sd->state.active = 1;
	sd->state.connect_new = 0;
	sd->state.pc_loaded = 1;

	// Equipment: copy each profile slot into the inventory and mark equipped.
	// pc_setequipindex reads the equip mask to fill equip_index[] and the
	// weapontype1/2. Use item_data->equip when available so 2H weapons
	// occupy both hand slots automatically.
	static const uint32 default_eqp[] = {
		EQP_HAND_R, EQP_HEAD_TOP, EQP_HEAD_MID, EQP_HEAD_LOW, EQP_ARMOR,
		EQP_HAND_L, EQP_GARMENT, EQP_SHOES,    EQP_ACC_R,   EQP_ACC_L,
	};
	int32 inv_i = 0;
	for (int32 es = 0; es < 10 && inv_i < MAX_INVENTORY; es++) {
		t_itemid nameid = (t_itemid)p->equip[es];
		if (nameid == 0) continue;
		std::shared_ptr<item_data> idata = item_db.find(nameid);
		if (idata == nullptr) {
			ShowWarning("aishell_create: shell %u equip slot %d nameid=%u unknown in item_db.\n",
				p->shell_id, es, nameid);
			continue;
		}
		auto& it = sd->inventory.u.items_inventory[inv_i];
		it.nameid = nameid;
		it.amount = 1;
		it.identify = 1;
		it.equip = (idata->equip != 0) ? idata->equip : default_eqp[es];
		inv_i++;
	}

	pc_setinventorydata(*sd);

	// Strip equip flag from items the class can't actually wear (Novice with
	// Falchion, Mage with Sword, etc.). pc_setequipindex would happily wire
	// them in otherwise — same anti-pattern that lets a shell visually carry
	// a class-locked weapon.
	for (int32 i = 0; i < MAX_INVENTORY; i++) {
		if (sd->inventory.u.items_inventory[i].nameid == 0) continue;
		if (sd->inventory.u.items_inventory[i].equip == 0) continue;
		uint8 ack = pc_isequip(sd, i);
		if (ack != ITEM_EQUIP_ACK_OK) {
			sd->inventory.u.items_inventory[i].equip = 0;
		}
	}

	pc_setequipindex(sd);

	// CRITICAL: register sd in id_db BEFORE status_calc_pc. Item bonus
	// scripts run during the calc and call script_rid2sd → map_id2sd,
	// which fails (and aborts the script with "player not attached") if
	// the player isn't in id_db yet. map_addblock can wait until after
	// the calc is done.
	map_addiddb(sd);

	// Canonical recalc — exactly what pc_authok runs after char load.
	status_calc_pc(sd, SCO_FIRST);

	// Make sure the shell is alive after the recalc and has the AI mob bits
	// status_check_skilluse expects (MD_CANATTACK).
	sd->battle_status.hp = sd->battle_status.max_hp;
	sd->battle_status.sp = sd->battle_status.max_sp;
	sd->status.hp = sd->battle_status.max_hp;
	sd->status.sp = sd->battle_status.max_sp;
	sd->base_status.mode   = (e_mode)(sd->base_status.mode   | MD_CANMOVE | MD_CANATTACK);
	sd->battle_status.mode = (e_mode)(sd->battle_status.mode | MD_CANMOVE | MD_CANATTACK);
	if (p->speed > 0) {
		sd->base_status.speed = p->speed;
		sd->battle_status.speed = p->speed;
	}

	status_set_viewdata(sd, sd->status.class_);
	// Force standing pose + re-anchor LOOK_BASE.
	sd->vd.dead_sit = 0;
	sd->vd.look[LOOK_BASE] = sd->status.class_;
	// CRITICAL: broadcast the class via clif_changelook (same path the
	// `changebase` script uses). Without this the client renders any
	// manually-set class as Novice — even though the spawn packet carries
	// the right class, the client only updates its sprite when LOOK_BASE
	// changelook arrives.

	if (map_addblock(sd) != 0) {
		ShowError("aishell_create: map_addblock failed for shell %u.\n", p->shell_id);
		map_deliddb(sd);
		db_destroy(sd->regs.vars);
		sd->~map_session_data();
		aFree(sd);
		return nullptr;
	}
	ShowInfo("aishell pre-clif_spawn: status.class_=%u vd.look[BASE]=%d vd.dead_sit=%u sex=%d\n",
		sd->status.class_, sd->vd.look[LOOK_BASE], (uint32)sd->vd.dead_sit, (int)sd->vd.sex);
	clif_spawn(sd);
	// Broadcast the full changelook chain AFTER spawn — mirrors what
	// pc_jobchange runs at the very end. Without this the client keeps the
	// placeholder Novice sprite from the initial spawn.
	clif_changelook(sd, LOOK_BASE, sd->vd.look[LOOK_BASE]);
#if PACKETVER >= 20151001
	clif_changelook(sd, LOOK_HAIR, sd->vd.look[LOOK_HAIR]);
#endif
	clif_changelook(sd, LOOK_CLOTHES_COLOR, sd->vd.look[LOOK_CLOTHES_COLOR]);
	clif_changelook(sd, LOOK_BODY2, sd->vd.look[LOOK_BODY2]);
	clif_changelook(sd, LOOK_WEAPON, sd->status.weapon);

	g_shells[p->shell_id] = sd;
	if (p->owner_aid != 0) {
		g_owner_aid[p->shell_id] = p->owner_aid;
		g_owner_cid[p->shell_id] = p->owner_cid;
		g_owner_seen_once[p->shell_id] = false;
	}
	ShowStatus("ai-server: shell %u '%s' (class=%u) spawned at %s(%d,%d) lv=%u/%u weapon=%u/equip[HAND_R]=%u.\n",
		p->shell_id, p->name, p->class_, p->map_name, p->x, p->y,
		p->base_level, p->job_level, sd->status.weapon, p->equip[0]);
	return sd;
}

void aishell_destroy(uint32 shell_id){
	auto it = g_shells.find(shell_id);
	if (it == g_shells.end()) return;
	map_session_data* sd = it->second;
	g_shells.erase(it);
	g_last_hp.erase(shell_id);
	g_owner_aid.erase(shell_id);
	g_owner_cid.erase(shell_id);
	g_owner_seen_once.erase(shell_id);
	g_owner_last_seen_tick.erase(shell_id);
	// NOTE: g_suspended_owner is intentionally NOT erased here. When
	// OWNER_GONE fires we move the owner identity into g_suspended_owner
	// and ai-server immediately sends DESPAWN — which calls this function.
	// Erasing here would wipe the suspension entry before the relogin
	// poll ever runs. The entry is consumed when the right char logs back
	// in (poll erases it after sending OWNER_BACK).

	// Phase 6 — pull the merc out of its party before tearing down sd.
	if (sd->status.party_id != 0)
		party_remove_aishell(sd->status.party_id, sd->status.account_id);

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

/// Phase 5 — pack the ~16 combat-relevant SCs of a bl into the AI_ST_*
/// bitmask wire format. Returns 0 if the bl has no status_change attached.
static uint32 ai_collect_statuses(block_list* bl){
	if (bl == nullptr) return 0;
	status_change* sc = status_get_sc(bl);
	if (sc == nullptr) return 0;
	uint32 m = 0;
	if (sc->getSCE(SC_STONE) || sc->getSCE(SC_STONEWAIT)) m |= AI_ST_STONE;
	if (sc->getSCE(SC_FREEZE))     m |= AI_ST_FREEZE;
	if (sc->getSCE(SC_STUN))       m |= AI_ST_STUN;
	if (sc->getSCE(SC_SLEEP))      m |= AI_ST_SLEEP;
	if (sc->getSCE(SC_SILENCE))    m |= AI_ST_SILENCE;
	if (sc->getSCE(SC_CURSE))      m |= AI_ST_CURSE;
	if (sc->getSCE(SC_CONFUSION))  m |= AI_ST_CONFUSION;
	if (sc->getSCE(SC_BLIND))      m |= AI_ST_BLIND;
	if (sc->getSCE(SC_POISON) || sc->getSCE(SC_DPOISON)) m |= AI_ST_POISON;
	if (sc->getSCE(SC_BLEEDING))   m |= AI_ST_BLEEDING;
	if (sc->getSCE(SC_HIDING))     m |= AI_ST_HIDING;
	if (sc->getSCE(SC_CLOAKING))   m |= AI_ST_CLOAKING;
	if (sc->getSCE(SC_ENDURE))     m |= AI_ST_ENDURE;
	if (sc->getSCE(SC_PROVOKE))    m |= AI_ST_PROVOKE;
	if (sc->getSCE(SC_AUTOGUARD))  m |= AI_ST_AUTOGUARD;
	if (sc->getSCE(SC_INCREASEAGI)) m |= AI_ST_INC_AGI;
	// Phase 6 — Priest/AB buff line for mercenary refresh logic.
	if (sc->getSCE(SC_BLESSING))   m |= AI_ST_BLESSING;
	if (sc->getSCE(SC_KYRIE))      m |= AI_ST_KYRIE;
	if (sc->getSCE(SC_MAGNIFICAT)) m |= AI_ST_MAGNIFICAT;
	if (sc->getSCE(SC_ASSUMPTIO))  m |= AI_ST_ASSUMPTIO;
	if (sc->getSCE(SC_ANGELUS))    m |= AI_ST_ANGELUS;
	if (sc->getSCE(SC_IMPOSITIO))  m |= AI_ST_IMPOSITIO;
	if (sc->getSCE(SC_GLORIA))     m |= AI_ST_GLORIA;
	if (sc->getSCE(SC_EXPIATIO))   m |= AI_ST_EXPIATIO;
	if (sc->getSCE(SC_SECRAMENT))  m |= AI_ST_SECRAMENT;
	if (sc->getSCE(SC_OFFERTORIUM)) m |= AI_ST_OFFERTORIUM;
	if (sc->getSCE(SC_RENOVATIO))  m |= AI_ST_RENOVATIO;
	return m;
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
	// Phase 5 — race / element / boss flags so the picker can gate skills
	// like Holy Light (vs UNDEAD), Ice Bolt (vs FIRE-element), and avoid
	// MVPs entirely if the rotation prefers it.
	row.race = (uint8)md->status.race;
	uint8 ele_type = (uint8)(md->status.def_ele & 0x0F);
	uint8 ele_lv   = (uint8)(md->status.ele_lv & 0x0F);
	row.element = (uint8)((ele_lv << 4) | ele_type);
	uint8 fl = 0;
	auto bt = md->get_bosstype();
	if (bt == BOSSTYPE_MINIBOSS) fl |= AI_ENEMY_FLAG_BOSS;
	if (bt == BOSSTYPE_MVP)      fl |= AI_ENEMY_FLAG_BOSS | AI_ENEMY_FLAG_MVP;
	row.flags = fl;
	row.statuses = ai_collect_statuses(bl);  // Phase 5 — SC bitmask

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
	p.mapindex = (uint16)sd->mapindex;
	p.x = (uint16)sd->x;
	p.y = (uint16)sd->y;
	p.hp = (uint32)sd->battle_status.hp;
	p.max_hp = (uint32)sd->battle_status.max_hp;
	p.sp = (uint32)sd->battle_status.sp;
	p.max_sp = (uint32)sd->battle_status.max_sp;
	p.target_id = (uint32)sd->ud.target;
	p.self_statuses = ai_collect_statuses(const_cast<map_session_data*>(sd)); // Phase 5
	p.enemy_count = ctx.count;
	for (uint8 i = 0; i < ctx.count; i++)
		p.enemies[i] = ctx.rows[i];

	// Phase 6 — owner snapshot for hired-mercenary shells. owner_present=0
	// when no owner OR owner offline; ai-server treats both as "no follow".
	auto oit = g_owner_aid.find(shell_id);
	if (oit != g_owner_aid.end()) {
		map_session_data* osd = map_id2sd((int32)oit->second);
		// Phase 6 — verify the merc is bound to THIS specific character
		// (not just the same account on a different char). If a different
		// char of the same account is online, treat the owner as gone.
		uint32 expected_cid = 0;
		auto cit = g_owner_cid.find(shell_id);
		if (cit != g_owner_cid.end()) expected_cid = cit->second;
		if (osd != nullptr && expected_cid != 0
		    && (uint32)osd->status.char_id != expected_cid) {
			osd = nullptr;	// wrong character on this account
		}
		if (osd != nullptr) {
			p.owner_present = 1;
			p.owner_mapindex = (uint16)osd->mapindex;
			p.owner_x = (uint16)osd->x;
			p.owner_y = (uint16)osd->y;
			uint32 ohp_max = osd->battle_status.max_hp ? osd->battle_status.max_hp : 1;
			uint32 osp_max = osd->battle_status.max_sp ? osd->battle_status.max_sp : 1;
			p.owner_hp_pct = (uint16)((osd->battle_status.hp * 100) / ohp_max);
			p.owner_sp_pct = (uint16)((osd->battle_status.sp * 100) / osp_max);
			p.owner_statuses = ai_collect_statuses(osd);
			p.owner_target_id = (uint32)osd->ud.target;	// 0 if not engaging
			g_owner_seen_once[shell_id] = true;
			g_owner_last_seen_tick[shell_id] = gettick();
		} else if (g_owner_seen_once[shell_id]) {
			// Owner not online RIGHT NOW (or different char on same account).
			// Could be a brief zone transition (NPC warp, @warp, login limbo)
			// or a real disconnect / char switch. Wait AI_OWNER_GONE_GRACE_MS
			// before firing OWNER_GONE so the merc survives map changes.
			// After the grace expires we move the owner identity to
			// g_suspended_owner so the relogin poll can fire AI_EVT_OWNER_BACK
			// when the right character reconnects.
			t_tick last_seen = g_owner_last_seen_tick[shell_id];
			if (last_seen != 0 && DIFF_TICK(gettick(), last_seen) >= AI_OWNER_GONE_GRACE_MS) {
				suspended_owner_t so{};
				so.aid = oit->second;
				so.cid = expected_cid;
				aichrif_send_event(shell_id, AI_EVT_OWNER_GONE, so.aid, 0);
				g_owner_aid.erase(shell_id);
				g_owner_cid.erase(shell_id);
				g_owner_seen_once.erase(shell_id);
				g_owner_last_seen_tick.erase(shell_id);
				g_suspended_owner[shell_id] = so;
			}
		}
	}

	// Phase 6 — fill the party roster (excluding the merc itself) so the
	// support tick can iterate buff/heal targets across the whole party.
	if (sd->status.party_id != 0) {
		struct party_data* pd = party_search(sd->status.party_id);
		if (pd != nullptr) {
			uint8 idx = 0;
			for (int i = 0; i < MAX_PARTY && idx < AI_REPORT_MAX_PARTY; i++) {
				map_session_data* msd = pd->data[i].sd;
				if (msd == nullptr) continue;
				if (msd->status.account_id == sd->status.account_id) continue;	// skip self
				if (aishell_is_shell((uint32)msd->status.account_id)) continue;	// skip other shells
				PACKET_AI_PARTY_MEMBER& m = p.party_members[idx];
				m.account_id = (uint32)msd->status.account_id;
				uint32 mhp_max = msd->battle_status.max_hp ? msd->battle_status.max_hp : 1;
				uint32 msp_max = msd->battle_status.max_sp ? msd->battle_status.max_sp : 1;
				m.hp_pct = (uint16)((msd->battle_status.hp * 100) / mhp_max);
				m.sp_pct = (uint16)((msd->battle_status.sp * 100) / msp_max);
				m.statuses = ai_collect_statuses(msd);
				idx++;
			}
			p.party_count = idx;
		}
	}

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
	// Phase 6 — auto-join the owner's party so the merc shows up in the
	// party UI (heal targets work via account_id either way).
	if (sd != nullptr && p->owner_aid != 0) {
		map_session_data* osd = map_id2sd((int32)p->owner_aid);
		if (osd != nullptr && osd->status.party_id != 0)
			party_add_aishell(*sd, osd->status.party_id);
	}
	aichrif_send_spawned_ack(p->shell_id, sd != nullptr, sd != nullptr ? 0 : 1);
	return 1;
}

static int32 aichrif_handle_despawn(int32 fd){
	uint32 shell_id = RFIFOL(fd, 4);
	if (shell_id == 0) {
		// Sentinel: wipe ALL shells. ai-server sends this on reconnect to
		// clear any leftovers from a previous ai-server session that map
		// still has alive in g_shells.
		std::vector<uint32> ids;
		ids.reserve(g_shells.size());
		for (auto& kv : g_shells) ids.push_back(kv.first);
		for (uint32 id : ids) aishell_destroy(id);
		ShowInfo("ai-server: SHELL_DESPAWN wipe — destroyed %zu shells.\n", ids.size());
		return 1;
	}
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
		case AI_CMD_SIT: {
			if (!pc_issit(sd)) {
				pc_setsit(sd);
				skill_sit(sd, true);
				clif_sitting(*sd);
			}
			break;
		}
		case AI_CMD_STAND: {
			if (pc_issit(sd)) {
				pc_setstand(sd, true);
				skill_sit(sd, false);
				clif_standing(*sd);
			}
			break;
		}
		case AI_CMD_EMOTE: {
			if (RFIFOREST(fd) < sizeof(PACKET_AI_CMD_EMOTE_S)) return 0;
			const PACKET_AI_CMD_EMOTE_S* e = (const PACKET_AI_CMD_EMOTE_S*)RFIFOP(fd, 0);
			clif_emotion(*sd, (emotion_type)e->emote_id);
			break;
		}
		case AI_CMD_WARP: {
			if (RFIFOREST(fd) < sizeof(PACKET_AI_CMD_WARP_S)) return 0;
			const PACKET_AI_CMD_WARP_S* w = (const PACKET_AI_CMD_WARP_S*)RFIFOP(fd, 0);
			char nm[sizeof(w->map_name) + 1] = {0};
			memcpy(nm, w->map_name, sizeof(w->map_name));
			int16 m = map_mapname2mapid(nm);
			ShowStatus("aichrif: WARP shell %u to '%s' (m=%d) %u,%u\n",
				hdr->shell_id, nm, (int32)m, w->x, w->y);
			if (m < 0) break;
			int16 dx = (int16)w->x, dy = (int16)w->y;
			// Tight radius — the warp packet already targets a cell next
			// to the owner. A wide search nudges the merc 30+ cells away,
			// out of viewport. 4 cells is enough to avoid walls/portals.
			if (!map_search_freecell(nullptr, m, &dx, &dy, 4, 4, 1)) {
				dx = (int16)w->x; dy = (int16)w->y;
			}
			// pc_setpos triggers char-server save which crashes for shells
			// (no char row). Do the warp manually: clear from old map, move,
			// add to new map, respawn visual.
			clif_clearunit_area(*sd, CLR_TELEPORT);
			map_delblock(sd);
			sd->m = m;
			sd->x = dx;
			sd->y = dy;
			sd->mapindex = mapindex_name2id(nm);
			if (map_addblock(sd) != 0) {
				// new map full / out-of-bounds: try to put it back where it was
				ShowError("aichrif: warp shell %u to %s(%d,%d) failed.\n",
					hdr->shell_id, nm, dx, dy);
				break;
			}
			clif_spawn(sd);
			ShowStatus("aichrif: WARP shell %u DONE on %s(%d,%d)\n",
				hdr->shell_id, nm, dx, dy);
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
				clif_disp_overhead(static_cast<block_list*>(sd), buf);
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
			int32 inf = skill_get_inf(sid);
			if (inf == INF_PASSIVE_SKILL) {
				ShowWarning("ai-server: CAST passive skill '%s' (shell %u) — dropped.\n", nm, hdr->shell_id);
				break;
			}
			uint8 kind = c->kind;
			if (kind == 1 && !(inf & INF_GROUND_SKILL)) kind = 0;
			if (kind == 0 && (inf & INF_SELF_SKILL))    kind = 2;
			if (kind == 1) {
				unit_skilluse_pos(sd, c->x, c->y, sid, c->skill_lv);
			} else {
				uint32 tid = (kind == 2) ? sd->id : c->target_id;
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

/// Phase 6 — poll suspended owners. When the right character reconnects
/// (account_id online AND char_id matches) we send PACKET_AI_OWNER_BACK
/// so ai-server re-spawns the merc next to them. Cheap: usually empty;
/// at most one entry per offline merc owner.
static TIMER_FUNC(aichrif_owner_back_timer){
	if (g_suspended_owner.empty()) return 0;
	if (char_fd < 0 || session[char_fd] == nullptr) return 0;
	std::vector<uint32> resolved;
	resolved.reserve(g_suspended_owner.size());
	for (auto& kv : g_suspended_owner) {
		uint32 shell_id = kv.first;
		uint32 owner_aid = kv.second.aid;
		uint32 owner_cid = kv.second.cid;
		map_session_data* osd = map_id2sd((int32)owner_aid);
		if (osd == nullptr) continue;
		// Verify the right character is online (different char on same
		// account does NOT bring the merc back).
		if (owner_cid != 0 && (uint32)osd->status.char_id != owner_cid)
			continue;
		const char* mname = mapindex_id2name(osd->mapindex);
		if (mname == nullptr || mname[0] == '\0') continue;
		PACKET_AI_OWNER_BACK_S p{};
		p.cmd = PACKET_AI_OWNER_BACK;
		p.len = sizeof(p);
		p.shell_id = shell_id;
		p.owner_aid = owner_aid;
		p.owner_cid = owner_cid;
		safestrncpy(p.map_name, mname, MAP_NAME_LENGTH_EXT);
		p.x = (uint16)osd->x;
		p.y = (uint16)osd->y;
		WFIFOHEAD(char_fd, sizeof(p));
		memcpy(WFIFOP(char_fd, 0), &p, sizeof(p));
		WFIFOSET(char_fd, sizeof(p));
		ShowStatus("aichrif: OWNER_BACK shell %u char %u (aid %u) at %s(%u,%u)\n",
			shell_id, owner_cid, owner_aid, mname, p.x, p.y);
		resolved.push_back(shell_id);
	}
	for (uint32 sid : resolved) g_suspended_owner.erase(sid);
	return 0;
}

void do_init_map_aichrif(void){
	g_shells.reserve(8192);
	add_timer_func_list(aichrif_report_timer, "aichrif_report");
	add_timer_interval(gettick() + AI_REPORT_PERIOD_MS, aichrif_report_timer,
		0, 0, AI_REPORT_PERIOD_MS);
	add_timer_func_list(aichrif_owner_back_timer, "aichrif_owner_back");
	add_timer_interval(gettick() + AI_OWNER_BACK_POLL_MS, aichrif_owner_back_timer,
		0, 0, AI_OWNER_BACK_POLL_MS);
}

void do_final_map_aichrif(void){
	// Snapshot the keys to avoid mutating while iterating.
	std::vector<uint32> ids;
	ids.reserve(g_shells.size());
	for (auto& kv : g_shells) ids.push_back(kv.first);
	for (uint32 id : ids) aishell_destroy(id);
}
