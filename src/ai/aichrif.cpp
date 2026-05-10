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
#include <common/mapindex.hpp>
#include <common/showmsg.hpp>
#include <common/socket.hpp>
#include <common/strlib.hpp>
#include <common/timer.hpp>

#include "ai-server.hpp"
#include "chat.hpp"
#include "map_difficulty.hpp"
#include "profile.hpp"
#include "names.hpp"
#include "shell_pool.hpp"
#include "skill_picker.hpp"
#include "spawner.hpp"

int32 char_fd = -1;

static ai_stats g_stats{};
const ai_stats& aichrif_stats(){ return g_stats; }

namespace {
/// Phase 5 — owned snapshot of the spawn-time identity. Used to re-spawn the
/// same shell after death without regenerating name/profile/equip.
/// Strings are owned (std::string), so c_str() pointers stay valid as long
/// as the snapshot lives in g_shells_local.
struct shell_init_snapshot {
	std::string name;
	std::string map_name;
	uint16 class_;
	uint8  sex;
	uint16 hair, hair_color, head_top, weapon;
	uint16 x, y;
	uint8  dir, behavior_id, tier;
	uint16 base_level, job_level;
	uint16 str_, agi_, vit_, int_, dex_, luk_;
	uint16 speed;
	uint32 equip[10];
	uint32 owner_aid;	// Phase 6 — preserved through respawn (mercs respawn
						// to owner's side, not population home)
	uint32 owner_cid;	// Phase 6 — char_id; merc is bound to char, not account
};

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
	// Phase 6.2 — separate gate for combat-tick offensive casts. Tank mercs
	// run BOTH support tick (every 250ms, bumps last_cast_tick frequently
	// for self-buffs) and combat tick (every 250ms via 4-phase rotation).
	// Without a separate counter, the support tick's frequent updates
	// would starve the combat tick's 5s offensive-cast window.
	t_tick last_combat_cast_tick;
	t_tick last_chat_tick;  // throttle ambient chat per shell
	t_tick last_action_tick;
	bool   sitting;
	int8   patrol_dx;       // persistent search direction for field/dungeon
	int8   patrol_dy;       // (-1, 0, +1) per axis; rerolled occasionally
	uint8  patrol_count;    // ticks left on current direction
	t_tick fleeing_until;   // gettick() < this → don't engage, run away
	size_t skill_cursor;    // round-robin cursor into skill rotation
	// Phase 5 — death/respawn.
	t_tick respawn_at_tick; // 0 = alive; gettick()+delay = scheduled respawn.
	shell_init_snapshot init_snap;
	// Phase 5 — last-seen status bitmask (AI_ST_*) on self. Populated from
	// PACKET_AI_SHELL_REPORT. Per-enemy statuses are inside `enemies[]` so
	// no separate field is needed for those.
	uint32 self_statuses;
	uint32 last_attacker;   // most recent ATTACKED_BY actor_id
	// Phase 6 — mercenary mode. owner_aid != 0 = hired by player; switches
	// behavior from autonomous wander/combat to follow/support. owner_cid
	// is the char_id (persistent identity used for anti-stack and relogin
	// matching); owner_aid is the runtime account_id used for map_id2sd.
	uint32 owner_aid;
	uint32 owner_cid;
	// Phase 6.2 — hire role. 0 = SUPPORT (heal-only, default Acolyte/Priest
	// line); 1 = TANK (Sw/Crusader/Paladin/Royal Guard, runs combat tick +
	// defensive self-buffs). Carried into init_snap for relogin respawn.
	uint8  role;
	uint8  owner_present;   // 1 if last REPORT had owner online
	uint16 owner_mapindex;
	uint16 owner_x, owner_y;
	uint16 owner_hp_pct, owner_sp_pct;
	uint32 owner_statuses;
	uint32 owner_target_id;	// Phase 6 — owner's current attack target
	t_tick hire_expires_at;	// 0 = no expiry; gettick() >= this → despawn
	t_tick last_follow_tick;	// throttle WALK_TO commands while following
	// Phase 6 — per-skill cooldown so we don't spam the same buff before
	// the SC bit shows up in the next REPORT. Keyed by skill_name.
	std::unordered_map<std::string, t_tick> skill_cooldown_until;
	// Phase 6 — party roster snapshot from REPORT. Used by support tick
	// when a skill has Target=PARTY: walk the members and cast on the
	// first one whose state still matches the skill's condition.
	uint8 party_count;
	PACKET_AI_PARTY_MEMBER party_members[AI_REPORT_MAX_PARTY];
	// Phase 6 — derived flags for "decision quality" filtering. The
	// support tick refreshes them every report and uses them to skip
	// non-emergency skills when the owner is taking damage or moving.
	uint16 prev_owner_x, prev_owner_y;
	uint16 prev_owner_hp_pct;
	t_tick last_owner_move_tick;
	t_tick last_owner_damage_tick;
	// Phase 6.3 — smoothed owner velocity vector. Updated each report
	// when the owner moves; the follow tick uses it to position the
	// tanker ahead of the owner instead of just trailing behind.
	int8   lead_dx, lead_dy;
	// Phase 6 — when the merc dies we pause the hire contract; on
	// resurrection we shift hire_expires_at by the time spent dead so
	// the player isn't billed for downtime.
	t_tick died_at_tick;
	// Phase 6.3 — out-of-combat tracking. Set on the first REPORT where
	// the owner has no target AND hasn't taken damage in the last 5s;
	// cleared whenever the owner re-engages. Used by the dead-merc
	// respawn gate: the merc only revives 30s after the owner has been
	// continuously out of combat. Means: if the owner is still fighting
	// the next pull, the corpse stays put.
	t_tick out_of_combat_since_tick;
	// Phase 6 — owner logged out. Map-server has already despawned the
	// shell; we keep the shell_state alive so we can re-spawn at the
	// owner's location once they reconnect (PACKET_AI_OWNER_BACK).
	// While suspended, follow/support timers skip this shell and the
	// hire contract is paused (suspended_at_tick shifts hire_expires_at
	// on resume).
	bool   suspended;
	t_tick suspended_at_tick;
};
std::vector<shell_state> g_shells_local;
std::unordered_map<uint32, size_t> g_shell_idx; // shell_id -> g_shells_local index
// Phase 6 — (char_id, role) -> shell_id. A char can hold one merc per
// role (support + tank simultaneously). Composite key = cid*2 + role.
// Used for anti-stack at hire time and for routing DISMISS_REQUEST to
// the right shell.
static inline uint32 merc_key(uint32 cid, uint8 role) {
	return cid * 2u + (role & 1u);
}
std::unordered_map<uint32, uint32> g_merc_by_owner;
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
constexpr int32 SPAWN_BATCH_PER_TICK = 20;
// Phase 6 — autonomous spawner is paused while we focus on the mercenary
// system (Acolyte/Priest line). Set to >0 later to bring back ambient
// shells. Hired mercs (aichrif_hire) bypass this cap entirely.
constexpr int32 SPAWN_HARD_CAP = 0;
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

uint32 aichrif_hire(uint16 job, uint8 tier, uint32 owner_aid, uint32 owner_cid,
		const char* map_name, uint16 x, uint16 y, uint32 duration_ms,
		uint16 base_level_override, uint16 job_level_override, uint8 role){
	using namespace rathena::server_ai;
	if (aichrif_state != 2 || char_fd < 0) {
		ShowWarning("aichrif_hire: not connected to char-server (state=%d).\n", aichrif_state);
		return 0;
	}
	// Phase 6.3 — anti-stack per (char_id, role). A char can hold one
	// merc per role (1 support + 1 tank), but not two of the same role.
	uint32 mkey = merc_key(owner_cid, role);
	if (g_merc_by_owner.find(mkey) != g_merc_by_owner.end()) {
		ShowWarning("aichrif_hire: char %u already has a merc of role %u (shell %u).\n",
			owner_cid, role, g_merc_by_owner[mkey]);
		return 0;
	}
	if (!profile_has_exact(job, tier)) {
		ShowWarning("aichrif_hire: no profile for job=%u tier=%u.\n", job, tier);
		return 0;
	}
	uint32 sid = shell_pool_alloc();
	if (sid == 0) {
		ShowWarning("aichrif_hire: shell pool empty.\n");
		return 0;
	}
	ai_profile pr = profile_get(job, tier);
	if (pr.base_level == 0 || pr.equip[AI_EQ_HAND_R] == 0) {
		ShowWarning("aichrif_hire: profile incomplete for job=%u tier=%u.\n", job, tier);
		shell_pool_free(sid);
		return 0;
	}
	char nm[NAME_LENGTH];
	names_generate(nm);
	ai_shell_init init{};
	init.shell_id   = sid;
	init.name       = nm;
	init.class_     = job;
	init.sex        = (uint8)(rnd() % 2);
	init.hair       = (uint16)(1 + rnd() % 20);
	init.hair_color = (uint16)(rnd() % 8);
	init.head_top   = (uint16)pr.equip[AI_EQ_HEAD_TOP];
	init.weapon     = (uint16)pr.equip[AI_EQ_HAND_R];
	init.map_name   = map_name;
	init.x          = x;
	init.y          = y;
	init.dir        = 0;
	init.behavior_id= 0;
	init.tier       = tier;
	init.base_level = (base_level_override > 0) ? base_level_override : pr.base_level;
	init.job_level  = (job_level_override  > 0) ? job_level_override  : pr.job_level;
	init.str_ = pr.str; init.agi_ = pr.agi; init.vit_ = pr.vit;
	init.int_ = pr.int_; init.dex_ = pr.dex; init.luk_ = pr.luk;
	init.speed = pr.speed;
	for (int es = 0; es < AI_EQ_COUNT; es++) init.equip[es] = pr.equip[es];
	init.owner_aid  = owner_aid;
	init.owner_cid  = owner_cid;
	aichrif_send_shell_spawn(char_fd, init);
	g_stats.spawned++;

	shell_state st{};
	st.shell_id = sid;
	st.job = job;
	st.cat = spawn_category::FIELD; // mercs travel with owner; "field" so the
	                                // combat tick treats them as engaging
	st.home_map = map_name;
	st.home_x = x; st.home_y = y;
	st.base_x = x; st.base_y = y;
	st.warp_at_tick = 0;
	st.hp = 1; st.max_hp = 1;
	st.owner_aid = owner_aid;
	st.owner_cid = owner_cid;
	st.role = role;
	st.hire_expires_at = duration_ms ? (gettick() + (t_tick)duration_ms) : 0;
	// Snapshot for in-place respawn — though mercs don't respawn (they
	// despawn on death/owner-gone), we keep the snapshot symmetrical.
	st.init_snap.name      = nm;
	st.init_snap.map_name  = map_name;
	st.init_snap.class_    = init.class_;
	st.init_snap.sex       = init.sex;
	st.init_snap.hair      = init.hair;
	st.init_snap.hair_color= init.hair_color;
	st.init_snap.head_top  = init.head_top;
	st.init_snap.weapon    = init.weapon;
	st.init_snap.x = x; st.init_snap.y = y;
	st.init_snap.dir = 0;
	st.init_snap.behavior_id = 0;
	st.init_snap.tier = tier;
	st.init_snap.base_level = pr.base_level;
	st.init_snap.job_level  = pr.job_level;
	st.init_snap.str_ = pr.str; st.init_snap.agi_ = pr.agi;
	st.init_snap.vit_ = pr.vit; st.init_snap.int_ = pr.int_;
	st.init_snap.dex_ = pr.dex; st.init_snap.luk_ = pr.luk;
	st.init_snap.speed = pr.speed;
	for (int es = 0; es < 10; es++) st.init_snap.equip[es] = init.equip[es];
	st.init_snap.owner_aid = owner_aid;
	st.init_snap.owner_cid = owner_cid;
	g_shell_idx[sid] = g_shells_local.size();
	g_shells_local.push_back(st);
	g_merc_by_owner[mkey] = sid;

	ShowStatus("ai-server: hired job=%u tier=%u role=%u as shell %u for char %u (aid %u) at %s(%u,%u) dur=%ums.\n",
		job, tier, role, sid, owner_cid, owner_aid, map_name, x, y, duration_ms);
	return sid;
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
	p.owner_aid = init.owner_aid;	// Phase 6 — 0 = autonomous, !=0 = hired
	p.owner_cid = init.owner_cid;	// Phase 6 — char_id for relogin matching
	WFIFOHEAD(fd, sizeof(p));
	memcpy(WFIFOP(fd, 0), &p, sizeof(p));
	WFIFOSET(fd, sizeof(p));
	return 0;
}

int32 aichrif_send_despawn(int32 fd, uint32 shell_id, uint8 reason){
	PACKET_AI_SHELL_DESPAWN_S p{};
	p.cmd = PACKET_AI_SHELL_DESPAWN;
	p.len = sizeof(p);
	p.shell_id = shell_id;
	p.reason = reason;
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
				// Phase 6 — mercs wait dead so a Priest/AB can revive
				// them with Resurrection. The follow_timer watches the
				// owner: if the owner respawns at save point (different
				// map than where the merc died), trigger a fresh spawn
				// next to the owner. Until then we leave the corpse on
				// the map and let players Resurrect.
				if (s.owner_aid != 0) {
					ShowStatus("ai-server: merc shell %u died — waiting for owner.\n", s.shell_id);
					s.respawn_at_tick = 0;	// no auto-respawn; gated on owner state
					s.died_at_tick = gettick();	// pause hire contract clock
					break;
				}
				// Phase 5 — autonomous shells: schedule respawn at home if
				// enabled. The actual despawn+spawn pair is emitted by the
				// respawn tick once the delay elapses.
				if (ai_config.respawn_delay_ms > 0) {
					s.respawn_at_tick = gettick() + (t_tick)ai_config.respawn_delay_ms;
					g_stats.respawns_scheduled++;
				}
				break;
			case AI_EVT_RESURRECTED:
				s.fleeing_until = 0;
				s.died_at_tick = 0;
				break;
			case AI_EVT_OWNER_GONE:
				// Phase 6 — owner logged out / disconnected. Despawn the
				// shell on map-server but KEEP the shell_state on
				// ai-server in suspended mode. When map-server detects
				// the owner reconnect it sends PACKET_AI_OWNER_BACK and
				// we re-spawn next to them. Hire contract is paused while
				// suspended so the player isn't billed for downtime.
				if (s.owner_aid != 0 && !s.suspended) {
					ShowStatus("ai-server: merc shell %u owner %u gone — suspending.\n",
						s.shell_id, s.owner_aid);
					aichrif_send_despawn(char_fd, s.shell_id, /*reason=*/2);
					s.suspended = true;
					s.suspended_at_tick = gettick();
					s.respawn_at_tick = 0;
					s.target_id = 0;
					s.fleeing_until = 0;
					// Don't clear owner_aid / g_merc_by_owner — re-hire
					// while suspended would orphan this shell.
				}
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
		s.self_statuses = p->self_statuses;	// Phase 5
		s.enemy_count = (p->enemy_count > AI_REPORT_MAX_ENEMIES) ? AI_REPORT_MAX_ENEMIES : p->enemy_count;
		for (uint8 i = 0; i < s.enemy_count; i++)
			s.enemies[i] = p->enemies[i];	// row-level statuses ride along
		// Phase 6 — owner snapshot. owner_present clears every REPORT (so
		// after the owner zones out and back in, we re-acquire cleanly).
		s.owner_present  = p->owner_present;
		s.owner_mapindex = p->owner_mapindex;
		s.owner_x = p->owner_x;
		s.owner_y = p->owner_y;
		s.owner_hp_pct = p->owner_hp_pct;
		s.owner_sp_pct = p->owner_sp_pct;
		s.owner_statuses = p->owner_statuses;
		s.owner_target_id = p->owner_target_id;
		s.party_count = (p->party_count > AI_REPORT_MAX_PARTY) ? AI_REPORT_MAX_PARTY : p->party_count;
		for (uint8 i = 0; i < s.party_count; i++)
			s.party_members[i] = p->party_members[i];

		// Phase 6 — track owner movement / damage events to feed the
		// support tick's emergency-mode filter.
		t_tick now_r = gettick();
		if (p->owner_present) {
			int32 mx = (int32)p->owner_x - (int32)s.prev_owner_x;
			int32 my = (int32)p->owner_y - (int32)s.prev_owner_y;
			if (mx != 0 || my != 0) {
				s.last_owner_move_tick = now_r;
				// Phase 6.3 — exponential smoothing on owner velocity vector.
				// We store unit direction in [-1,0,1] per axis (snapped from
				// per-report deltas). Smoothing over ~4 reports avoids the
				// follow tick chasing a single jitter cell. Big jumps (>5
				// cells, e.g. teleport) bypass smoothing — direction is
				// taken at face value so the lead snaps to the new heading.
				int32 sx = (mx > 0) - (mx < 0);
				int32 sy = (my > 0) - (my < 0);
				if (mx > 5 || mx < -5 || my > 5 || my < -5) {
					s.lead_dx = (int8)sx;
					s.lead_dy = (int8)sy;
				} else {
					int32 nx = ((int32)s.lead_dx * 3 + sx) / 4;
					int32 ny = ((int32)s.lead_dy * 3 + sy) / 4;
					if (nx == 0 && sx != 0) nx = sx;
					if (ny == 0 && sy != 0) ny = sy;
					s.lead_dx = (int8)nx;
					s.lead_dy = (int8)ny;
				}
			}
			if (s.prev_owner_hp_pct > 0 && p->owner_hp_pct < s.prev_owner_hp_pct)
				s.last_owner_damage_tick = now_r;
			s.prev_owner_x = p->owner_x;
			s.prev_owner_y = p->owner_y;
			s.prev_owner_hp_pct = p->owner_hp_pct;

			// Phase 6.3 — track when the owner became "out of combat" so
			// the dead-merc respawn gate can require N seconds of peace.
			constexpr t_tick AI_OWNER_DAMAGE_GRACE_MS = 5000;
			bool owner_in_combat =
				(p->owner_target_id != 0) ||
				(s.last_owner_damage_tick != 0 &&
					DIFF_TICK(now_r, s.last_owner_damage_tick) < AI_OWNER_DAMAGE_GRACE_MS);
			if (owner_in_combat) {
				s.out_of_combat_since_tick = 0;
			} else if (s.out_of_combat_since_tick == 0) {
				s.out_of_combat_since_tick = now_r;
			}
		}
		s.last_report_tick = now_r;
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
			case PACKET_AI_HIRE_REQUEST: {
				if (RFIFOREST(fd) < sizeof(PACKET_AI_HIRE_REQUEST_S)) return 0;
				const PACKET_AI_HIRE_REQUEST_S* h = (const PACKET_AI_HIRE_REQUEST_S*)RFIFOP(fd, 0);
				char mname[MAP_NAME_LENGTH_EXT];
				safestrncpy(mname, h->map_name, MAP_NAME_LENGTH_EXT);
				aichrif_hire(h->job, h->tier, h->owner_aid, h->owner_cid, mname,
					h->x, h->y, h->duration_ms,
					h->base_level_override, h->job_level_override, h->role);
				RFIFOSKIP(fd, h->len);
				break;
			}
			case PACKET_AI_DISMISS_REQUEST: {
				if (RFIFOREST(fd) < sizeof(PACKET_AI_DISMISS_REQUEST_S)) return 0;
				const PACKET_AI_DISMISS_REQUEST_S* d = (const PACKET_AI_DISMISS_REQUEST_S*)RFIFOP(fd, 0);
				// Phase 6.3 — dismiss can target a single role or both.
				auto dismiss_role = [&](uint8 r){
					auto it = g_merc_by_owner.find(merc_key(d->owner_cid, r));
					if (it == g_merc_by_owner.end()) return;
					uint32 sid = it->second;
					ShowStatus("ai-server: dismiss requested for char %u role %u (shell %u).\n",
						d->owner_cid, r, sid);
					aichrif_send_despawn(char_fd, sid, /*reason=*/5);
					auto sit = g_shell_idx.find(sid);
					if (sit != g_shell_idx.end() && sit->second < g_shells_local.size()) {
						shell_state& s = g_shells_local[sit->second];
						s.owner_aid = 0;
						s.owner_cid = 0;
						s.respawn_at_tick = 0;
						s.suspended = false;
					}
					g_merc_by_owner.erase(it);
				};
				if (d->role == AI_HIRE_ROLE_ALL) {
					dismiss_role(AI_HIRE_ROLE_SUPPORT);
					dismiss_role(AI_HIRE_ROLE_TANK);
				} else {
					dismiss_role(d->role);
				}
				RFIFOSKIP(fd, d->len);
				break;
			}
			case PACKET_AI_OWNER_BACK: {
				if (RFIFOREST(fd) < sizeof(PACKET_AI_OWNER_BACK_S)) return 0;
				const PACKET_AI_OWNER_BACK_S* b = (const PACKET_AI_OWNER_BACK_S*)RFIFOP(fd, 0);
				auto sit = g_shell_idx.find(b->shell_id);
				if (sit != g_shell_idx.end() && sit->second < g_shells_local.size()) {
					shell_state& s = g_shells_local[sit->second];
					// Verify char_id (persistent identity). The aid may have
					// changed across login if the player logged the same
					// account from elsewhere; cid is the source of truth.
					if (s.suspended && s.owner_cid == b->owner_cid) {
						s.owner_aid = b->owner_aid;	// refresh runtime aid
						s.init_snap.owner_aid = b->owner_aid;	// re-spawn carries new aid
						t_tick now_b = gettick();
						// Resume the hire contract: shift expiry by the
						// time spent suspended so the player isn't billed
						// for the offline window.
						if (s.suspended_at_tick != 0 && s.hire_expires_at != 0) {
							t_tick paused = DIFF_TICK(now_b, s.suspended_at_tick);
							if (paused > 0) s.hire_expires_at += paused;
						}
						s.suspended = false;
						s.suspended_at_tick = 0;
						// Update init_snap so the respawn lands next to
						// the player wherever they logged back in.
						char nm[MAP_NAME_LENGTH_EXT];
						safestrncpy(nm, b->map_name, MAP_NAME_LENGTH_EXT);
						s.init_snap.map_name = nm;
						s.init_snap.x = b->x;
						s.init_snap.y = b->y;
						// Schedule an immediate respawn — the existing
						// respawn timer will emit DESPAWN+SPAWN next tick.
						s.respawn_at_tick = now_b;
						ShowStatus("ai-server: merc shell %u char %u (aid %u) back — respawning at %s(%u,%u).\n",
							s.shell_id, s.owner_cid, s.owner_aid, nm, b->x, b->y);
					}
				}
				RFIFOSKIP(fd, b->len);
				break;
			}
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
		// Phase 5 — pick tier from the map's avg mob level (Towns always T0).
		// Then skip this draw if the job's profile doesn't have that exact
		// tier; better no shell than a lvl-35 High Mage in a lvl-80 field.
		uint8 tier = (t.category == spawn_category::TOWN)
			? (uint8)0
			: rathena::server_ai::map_difficulty_tier(t.map_name.c_str());
		if (!rathena::server_ai::profile_has_exact(t.job, tier)) {
			shell_pool_free(sid);
			emitted++;
			continue;
		}
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
		// Phase 5 — capture spawn-time identity for later respawn. We OWN
		// the strings so the const char* fields in the synthesized
		// ai_shell_init at respawn time stay valid until the snapshot is
		// destructed.
		st.init_snap.name      = nm;        // local buffer is fine — copied
		st.init_snap.map_name  = t.map_name;
		st.init_snap.class_    = init.class_;
		st.init_snap.sex       = init.sex;
		st.init_snap.hair      = init.hair;
		st.init_snap.hair_color= init.hair_color;
		st.init_snap.head_top  = init.head_top;
		st.init_snap.weapon    = init.weapon;
		st.init_snap.x         = init.x;
		st.init_snap.y         = init.y;
		st.init_snap.dir       = init.dir;
		st.init_snap.behavior_id = init.behavior_id;
		st.init_snap.tier      = init.tier;
		st.init_snap.base_level= init.base_level;
		st.init_snap.job_level = init.job_level;
		st.init_snap.str_ = init.str_; st.init_snap.agi_ = init.agi_;
		st.init_snap.vit_ = init.vit_; st.init_snap.int_ = init.int_;
		st.init_snap.dex_ = init.dex_; st.init_snap.luk_ = init.luk_;
		st.init_snap.speed = init.speed;
		for (int es = 0; es < 10; es++) st.init_snap.equip[es] = init.equip[es];
		g_shell_idx[sid] = g_shells_local.size();
		g_shells_local.push_back(st);
		g_spawn_within++;
		g_spawn_pending--;
		g_spawn_emitted++;
		emitted++;
	}
	return 0;
}

/// Phase 6 — mercenary follow loop. Shells with owner_aid set track their
/// owner instead of wandering: same-map → WALK_TO when distance > 3 cells,
/// different-map → WARP. WALK_TO is throttled (700ms) so we don't drown the
/// movement queue when the owner is jogging.
static TIMER_FUNC(aichrif_follow_timer){
	if (aichrif_state != 2 || char_fd < 0) return 0;
	t_tick now = gettick();
	for (auto& s : g_shells_local) {
		if (s.owner_aid == 0) continue;
		// Suspended (owner offline) — skip follow + contract expiry. The
		// PACKET_AI_OWNER_BACK handler resumes both.
		if (s.suspended) continue;
		if (s.last_report_tick == 0) continue;
		// Hire expiry — wall-clock; ticks even while dead.
		if (s.hire_expires_at != 0 && DIFF_TICK(now, s.hire_expires_at) >= 0) {
			ShowStatus("ai-server: merc shell %u contract expired — despawning.\n", s.shell_id);
			aichrif_send_despawn(char_fd, s.shell_id, /*reason=*/3);
			if (s.owner_cid != 0) g_merc_by_owner.erase(merc_key(s.owner_cid, s.role));
			s.respawn_at_tick = 0;
			s.owner_aid = 0;
			s.owner_cid = 0;
			continue;
		}
		// Phase 6 — dead merc revival.
		//
		// Two paths:
		//   (a) Owner moved to a DIFFERENT map (e.g. respawned at save).
		//       Revive immediately next to them — corpse on old map is
		//       inaccessible anyway.
		//   (b) Owner is on the SAME map. Wait 30s of *continuous*
		//       out-of-combat (no target, no recent damage) before
		//       reviving, so the merc doesn't pop back mid-fight and
		//       insta-die again. Player can still Resurrect the corpse
		//       in this window — AI_EVT_RESURRECTED clears `hp == 0`.
		if (s.hp == 0) {
			if (s.respawn_at_tick != 0) continue;	// already scheduled
			if (!s.owner_present) continue;
			if (s.cur_mapindex == 0 || s.owner_mapindex == 0) continue;
			const char* mname = mapindex_id2name((int32)s.owner_mapindex);
			if (mname == nullptr || mname[0] == 0) continue;

			constexpr t_tick AI_MERC_DEAD_REVIVE_GAP_MS = 30000;
			bool different_map = (s.cur_mapindex != s.owner_mapindex);
			bool owner_idle_long_enough =
				(s.out_of_combat_since_tick != 0 &&
					DIFF_TICK(now, s.out_of_combat_since_tick) >= AI_MERC_DEAD_REVIVE_GAP_MS);
			if (!different_map && !owner_idle_long_enough) continue;

			// Override the snapshot's spawn point to the owner's current
			// location. respawn_timer reads init_snap.map_name/x/y.
			s.init_snap.map_name = mname;
			s.init_snap.x = s.owner_x;
			s.init_snap.y = s.owner_y;
			s.respawn_at_tick = now;
			s.died_at_tick = 0;
			ShowStatus("ai-server: merc %u — reviving next to owner on %s (%s).\n",
				s.shell_id, mname,
				different_map ? "owner zoned" : "owner out of combat");
			continue;
		}
		if (!s.owner_present) continue;	// owner offline / zoning
		// Different mapindex → warp shell to owner. WARP packet wants a
		// map name; resolve via mapindex_id2name. We approximate the cell
		// to a fixed "near owner" point; map-server's pc_setpos handles
		// the walkable-cell nudge.
		if (s.cur_mapindex != 0 && s.cur_mapindex != s.owner_mapindex) {
			// Throttle so we don't fire 2-3 WARPs while waiting for the
			// next REPORT to confirm the merc is on the new map.
			if (s.last_follow_tick != 0 && DIFF_TICK(now, s.last_follow_tick) < 2000) continue;
			const char* mname = mapindex_id2name((int32)s.owner_mapindex);
			if (mname && mname[0]) {
				aichrif_send_warp(char_fd, s.shell_id, mname,
					(uint16)std::max<int32>(1, (int32)s.owner_x - 2),
					(uint16)std::max<int32>(1, (int32)s.owner_y - 2));
				// Optimistic update — REPORT will overwrite within ~1s.
				s.cur_mapindex = s.owner_mapindex;
				s.last_follow_tick = now;
			}
			continue;
		}
		// Phase 6.3 — formation positioning. Tanker stands AHEAD of the
		// owner in the direction they're moving (lead_dx/dy from smoothed
		// velocity). Support stays close behind. If owner is stationary,
		// fall back to the last known heading; if both are zero, just
		// stand close.
		int32 lead_x, lead_y;
		if (s.role == AI_HIRE_ROLE_TANK) {
			constexpr int32 LEAD_DISTANCE = 3;	// cells in front of owner
			int32 ldx = s.lead_dx, ldy = s.lead_dy;
			if (ldx == 0 && ldy == 0) {
				// Owner never moved (or just spawned). Stand 2 cells north
				// of owner as a default — better than dogpiling cell.
				ldx = 0; ldy = 1;
			}
			lead_x = (int32)s.owner_x + ldx * LEAD_DISTANCE;
			lead_y = (int32)s.owner_y + ldy * LEAD_DISTANCE;
		} else {
			// Support: trail just behind/beside the owner. Use the trail
			// formula based on current relative position.
			int32 tdx = (int32)s.cur_x - (int32)s.owner_x;
			int32 tdy = (int32)s.cur_y - (int32)s.owner_y;
			lead_x = (int32)s.owner_x - (tdx > 0 ? -2 : 2);
			lead_y = (int32)s.owner_y - (tdy > 0 ? -2 : 2);
		}

		int32 dx_to_target = (int32)s.cur_x - lead_x;
		int32 dy_to_target = (int32)s.cur_y - lead_y;
		int32 cheb_target = std::max(std::abs(dx_to_target), std::abs(dy_to_target));
		// Distance from owner directly — used for warp gating.
		int32 dx_owner = (int32)s.cur_x - (int32)s.owner_x;
		int32 dy_owner = (int32)s.cur_y - (int32)s.owner_y;
		int32 cheb_owner = std::max(std::abs(dx_owner), std::abs(dy_owner));

		if (cheb_target <= 1) continue;	// already in formation slot
		// Phase 6.3 — 200ms throttle (was 700ms). With map-server reporting
		// merc shells every 200ms (AI_MERC_REPORT_PERIOD_MS), this lines up
		// 1:1 with REPORTs so we re-walk every time the owner pos snapshot
		// updates. Anything higher creates visible follow lag.
		if (s.last_follow_tick != 0 && DIFF_TICK(now, s.last_follow_tick) < 200) continue;
		// Phase 6.3 — engaging a mob: let pursuit run as long as we're
		// not absurdly far from the owner. The combat tick already gates
		// tanker engagement on owner_target_id, so target_id != 0 means
		// the player chose this fight; stop interrupting it.
		constexpr int32 AI_FOLLOW_PURSUIT_TOLERANCE = 25;
		if (s.target_id != 0 && cheb_owner < AI_FOLLOW_PURSUIT_TOLERANCE) continue;
		// Warp safety net for Fly Wing / @warp / drift past walk range.
		constexpr int32 AI_FOLLOW_WARP_GAP = 25;
		if (cheb_owner >= AI_FOLLOW_WARP_GAP) {
			const char* mname = mapindex_id2name((int32)s.owner_mapindex);
			if (mname && mname[0]) {
				aichrif_send_warp(char_fd, s.shell_id, mname,
					(uint16)std::max<int32>(1, lead_x),
					(uint16)std::max<int32>(1, lead_y));
				s.last_follow_tick = now;
			}
			continue;
		}
		uint16 tx = (uint16)std::max<int32>(1, lead_x);
		uint16 ty = (uint16)std::max<int32>(1, lead_y);
		aichrif_send_walk_to(char_fd, s.shell_id, tx, ty);
		s.last_follow_tick = now;
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
		// Phase 6 — mercenary shells follow their owner via aichrif_follow_timer;
		// they don't get to wander.
		if (s.owner_aid != 0) continue;
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
		// Phase 6 — mercenary shells run their support rotation in
		// aichrif_support_timer.
		// Phase 6.2 — TANK-role mercs ALSO run the combat tick (engage
		// enemies, cast offense). SUPPORT mercs stay heal-only.
		if (s.owner_aid != 0 && s.role != AI_HIRE_ROLE_TANK) continue;
		// Tank mercs need an owner online to make follow/engagement coherent.
		// (Otherwise the shell could chase a mob across the map while the
		//  owner is offline, or pick a target the owner can't see.)
		if (s.owner_aid != 0 && !s.owner_present) continue;
		if (s.suspended) continue;
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
		uint8 idx = 0;
		uint32 tid = 0;
		// Phase 6.2 — tank merc target: ONLY engages what the owner is
		// attacking. If the owner has no target, the tanker stays in
		// formation — no auto-pull, no chasing mobs the player ignored.
		// Autonomous shells (no owner) keep the old top-3 jitter.
		if (s.role == AI_HIRE_ROLE_TANK) {
			if (s.owner_target_id == 0) {
				if (s.target_id != 0) s.target_id = 0;
				continue;
			}
			for (uint8 i = 0; i < s.enemy_count; i++) {
				if (s.enemies[i].id == s.owner_target_id) {
					idx = i;
					tid = s.enemies[i].id;
					break;
				}
			}
			if (tid == 0) continue;	// owner's target out of our view, skip
		} else {
			// Autonomous shell (no owner). Pick among top-3 closest.
			uint8 max_pick = (uint8)std::min<uint8>(s.enemy_count, 3);
			uint8 r = (uint8)(rnd() % 100);
			idx = (max_pick >= 3 && r >= 80) ? 2
				: (max_pick >= 2 && r >= 50) ? 1
				: 0;
			tid = s.enemies[idx].id;
			if (tid == 0) tid = s.enemies[0].id;
		}
		if (tid == 0) continue;

		const skill_rotation* rot = skill_picker_get(s.job);
		const skill_entry* pick = nullptr;
		if (rot != nullptr && DIFF_TICK(now, s.last_combat_cast_tick) > 5000) {
			shell_ctx ctx;
			ctx.hp = s.hp; ctx.max_hp = s.max_hp;
			ctx.sp = s.sp; ctx.max_sp = s.max_sp;
			ctx.has_target = true;
			ctx.target_hp_pct = s.enemies[idx].hp_pct;
			ctx.target_distance = s.enemies[idx].distance;
			ctx.enemy_count_nearby = s.enemy_count;
			ctx.map_zone = (uint8)((s.cat == spawn_category::TOWN) ? 1
				: (s.cat == spawn_category::FIELD) ? 2 : 3);
			// Phase 5 — feed SC bitmasks for SELF_STATUS / ENEMY_STATUS.
			ctx.self_statuses   = s.self_statuses;
			ctx.target_statuses = s.enemies[idx].statuses;
			ctx.target_race     = s.enemies[idx].race;
			ctx.target_element  = (uint8)(s.enemies[idx].element & 0x0F);
			ctx.target_element_lv = (uint8)((s.enemies[idx].element >> 4) & 0x0F);
			ctx.target_is_boss  = (s.enemies[idx].flags & AI_ENEMY_FLAG_BOSS) != 0;
			ctx.target_is_mvp   = (s.enemies[idx].flags & AI_ENEMY_FLAG_MVP) != 0;
			pick = skill_picker_choose(*rot, ctx, &s.skill_cursor);
		}
		// Phase 6.2 — skip OWNER/PARTY/ALLY entries in combat tick. Tank
		// rotations include those for the support tick to handle (heal
		// owner, devotion, kyrie). The picker doesn't filter by target,
		// so we filter here and fall through to ATTACK instead.
		bool pick_is_combat_target = (pick != nullptr) &&
			(pick->target == skill_target::TARGET || pick->target == skill_target::SELF);
		if (pick != nullptr && !pick->skill_name.empty() && pick_is_combat_target) {
			uint8 kind = AI_CAST_KIND_ID;
			uint32 cast_target = tid;
			if (pick->target == skill_target::SELF) {
				kind = AI_CAST_KIND_SELF;
				cast_target = 0;
			}
			aichrif_send_cast(char_fd, s.shell_id, pick->skill_name.c_str(),
				pick->level, kind, cast_target, 0, 0);
			s.last_cast_tick = now;
			s.last_combat_cast_tick = now;
		} else if (!skill_picker_is_caster(s.job)) {
			// Pure casters wait (SP regen, cooldown, condition flip) instead
			// of whacking with a staff. Melee/hybrid jobs auto-attack.
			aichrif_send_attack(char_fd, s.shell_id, tid, true);
		}
	}
	return 0;
}

/// Phase 6 — mercenary support tick. Runs every 1.5s for shells that have
/// an owner. Evaluates the rotation against owner HP/SP/status (no enemy
/// needed) and casts heals/buffs/cleanses on the owner. Cursor is fixed at
/// the start of the rotation each call, so high-priority skills (Heal at
/// the top of the list) always win when their condition matches.
static TIMER_FUNC(aichrif_support_timer){
	using namespace rathena::server_ai;
	if (aichrif_state != 2 || char_fd < 0) return 0;
	t_tick now = gettick();
	for (auto& s : g_shells_local) {
		if (s.owner_aid == 0) continue;
		if (s.suspended) continue;
		if (s.hp == 0) continue;
		if (!s.owner_present) continue;
		if (s.last_report_tick == 0) continue;
		// Throttle — same 5s cadence as combat tick for cast cooldown.
		// Per-shell throttle: 250ms = 4 casts/sec ceiling. Matches the
		// support tick interval so we don't waste tick budget. Real
		// players with high DEX cast ~5/sec — close enough.
		if (DIFF_TICK(now, s.last_cast_tick) < 250) continue;

		const skill_rotation* rot = skill_picker_get(s.job);
		if (rot == nullptr) continue;
		shell_ctx ctx;
		ctx.hp = s.hp; ctx.max_hp = s.max_hp;
		ctx.sp = s.sp; ctx.max_sp = s.max_sp;
		// Phase 6 — Target=TARGET on a merc means "owner's current target"
		// (used for offensive support like PR_LEXAETERNA). When the owner
		// isn't fighting, has_target stays false so those entries skip.
		ctx.has_target = (s.owner_target_id != 0);
		ctx.self_statuses = s.self_statuses;
		ctx.owner_present = true;
		ctx.owner_hp_pct = s.owner_hp_pct;
		ctx.owner_sp_pct = s.owner_sp_pct;
		ctx.owner_statuses = s.owner_statuses;
		// Phase 6 — emergency mode filtering.
		// "owner_damaged": HP just dropped → only heals/cleanses; a buff
		// inserted between two heals burns 250ms cast + 500ms after-cast
		// and can let the party die.
		// "owner_moving": owner moved in last second → only heals,
		// otherwise the merc casts buffs in place and falls behind.
		bool owner_damaged = (s.last_owner_damage_tick != 0) &&
			DIFF_TICK(now, s.last_owner_damage_tick) < 3000;
		bool owner_moving = (s.last_owner_move_tick != 0) &&
			DIFF_TICK(now, s.last_owner_move_tick) < 1000;
		const skill_entry* pick = skill_picker_choose(*rot, ctx,
			/*cursor=*/nullptr, &s.skill_cooldown_until, now);
		if (pick == nullptr || pick->skill_name.empty()) continue;
		// Skip non-emergency skills while damaged or moving — but SELF
		// target skills (Auto Guard / Endure / Reflect Shield etc) keep
		// firing because they don't require facing/staying near owner.
		// Phase 6.2: tank merc needs its self-buffs up before combat,
		// not after first hit. Only OWNER/PARTY/ALLY targets get filtered.
		if ((owner_damaged || owner_moving) && pick->target != skill_target::SELF) {
			bool is_emergency =
				pick->condition == skill_cond::HP_BELOW          ||
				pick->condition == skill_cond::OWNER_HP_BELOW    ||
				pick->condition == skill_cond::ALLY_HP_BELOW     ||
				pick->condition == skill_cond::OWNER_STATUS;
			if (!is_emergency) continue;
		}
		uint8 kind = AI_CAST_KIND_ID;
		uint32 cast_target = 0;
		switch (pick->target) {
			case skill_target::OWNER:
				cast_target = s.owner_aid;
				break;
			case skill_target::SELF:
				kind = AI_CAST_KIND_SELF;
				break;
			case skill_target::TARGET:
				if (s.owner_target_id == 0) continue;
				cast_target = s.owner_target_id;
				break;
			case skill_target::PARTY: {
				// Iterate the party roster and cast on the first member
				// whose state still matches the skill's condition (e.g.
				// missing the buff). The picker already chose this skill
				// because the condition passed for the OWNER; we re-check
				// per member so we don't waste a cast on someone already
				// buffed/full-HP.
				bool found = false;
				// Try owner first.
				shell_ctx mctx = ctx;
				if (skill_picker_cond_passes(*pick, mctx)) {
					cast_target = s.owner_aid;
					found = true;
				}
				for (uint8 i = 0; !found && i < s.party_count; i++) {
					const auto& m = s.party_members[i];
					if (m.account_id == 0) continue;
					mctx.owner_present  = true;
					mctx.owner_hp_pct   = m.hp_pct;
					mctx.owner_sp_pct   = m.sp_pct;
					mctx.owner_statuses = m.statuses;
					if (skill_picker_cond_passes(*pick, mctx)) {
						cast_target = m.account_id;
						found = true;
					}
				}
				if (!found) continue;
				break;
			}
			default:
				continue;	// ALLY not applicable to pure-support mercs
		}
		aichrif_send_cast(char_fd, s.shell_id, pick->skill_name.c_str(),
			pick->level, kind, cast_target, 0, 0);
		s.last_cast_tick = now;
		// Phase 6 — per-skill cooldown. YAML override > 800ms default.
		// 800ms is enough for the SC bit to round-trip on the next
		// REPORT (1s period) so buffs naturally stop after the SC lands.
		// Heals/cleanses fire at this rate during emergencies.
		// Long buffs without reliable SC tracking (Offertorium 90s,
		// Sacrament/Expiatio 4-5min) override via YAML Cooldown field.
		t_tick cdms = (pick->cooldown_ms > 0) ? (t_tick)pick->cooldown_ms : 800;
		s.skill_cooldown_until[pick->skill_name] = now + cdms;
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
		AI_LOG(1, "drift warp shell=%u %u→%s\n",
			s.shell_id, s.cur_mapindex, s.home_map.c_str());
		s.last_action_tick = now; // pause fidget for a tick
	}
	return 0;
}

/// Phase 5 — process scheduled respawns.
///
/// On AI_EVT_DIED, the event handler stamps `respawn_at_tick` with
/// `now + respawn_delay_ms`. This timer scans for due alarms and emits a
/// despawn+spawn pair using the captured init_snap. Map-server runs
/// despawn synchronously (g_shells.erase) before reading the next packet
/// from the same fd, so the SPAWN that follows finds a clean slate.
static TIMER_FUNC(aichrif_respawn_timer){
	if (aichrif_state != 2 || char_fd < 0) return 0;
	t_tick now = gettick();
	for (auto& s : g_shells_local) {
		if (s.respawn_at_tick == 0) continue;
		if (s.suspended) continue;
		if (now < s.respawn_at_tick) continue;

		// 1. Tear the dead shell down on the map side.
		aichrif_send_despawn(char_fd, s.shell_id, /*reason*/ 1 /*RESPAWN*/);

		// 2. Re-spawn with the same identity. Strings live in the snapshot
		// so the const char* fields stay valid through WFIFOSET().
		ai_shell_init init{};
		init.shell_id   = s.shell_id;
		init.name       = s.init_snap.name.c_str();
		init.class_     = s.init_snap.class_;
		init.sex        = s.init_snap.sex;
		init.hair       = s.init_snap.hair;
		init.hair_color = s.init_snap.hair_color;
		init.head_top   = s.init_snap.head_top;
		init.weapon     = s.init_snap.weapon;
		init.map_name   = s.init_snap.map_name.c_str();
		init.x          = s.init_snap.x;
		init.y          = s.init_snap.y;
		init.dir        = s.init_snap.dir;
		init.behavior_id= s.init_snap.behavior_id;
		init.tier       = s.init_snap.tier;
		init.base_level = s.init_snap.base_level;
		init.job_level  = s.init_snap.job_level;
		init.str_ = s.init_snap.str_; init.agi_ = s.init_snap.agi_;
		init.vit_ = s.init_snap.vit_; init.int_ = s.init_snap.int_;
		init.dex_ = s.init_snap.dex_; init.luk_ = s.init_snap.luk_;
		init.speed = s.init_snap.speed;
		for (int es = 0; es < 10; es++) init.equip[es] = s.init_snap.equip[es];
		init.owner_aid = s.init_snap.owner_aid;	// Phase 6 — preserve merc binding
		init.owner_cid = s.init_snap.owner_cid;
		aichrif_send_shell_spawn(char_fd, init);

		// 3. Reset transient state. cur_x/y get overwritten on next REPORT;
		// hp seeds to max_hp == max_hp from snapshot would require status
		// recalc, so just leave hp/max_hp untouched and let the first
		// REPORT after respawn refresh them.
		s.respawn_at_tick = 0;
		s.target_id = 0;
		s.fleeing_until = 0;
		s.last_action_tick = now;
		g_stats.respawns_sent++;
		AI_LOG(1, "respawn shell=%u at %s(%u,%u)\n",
			s.shell_id, s.init_snap.map_name.c_str(),
			s.init_snap.x, s.init_snap.y);
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

	// Phase 6 — mercenary follow loop.
	add_timer_func_list(aichrif_follow_timer, "aichrif_follow");
	add_timer_interval(gettick() + 6 * 1000, aichrif_follow_timer, 0, 0, 500);

	// Phase 6 — mercenary support tick (heals/buffs/cleanses on owner).
	add_timer_func_list(aichrif_support_timer, "aichrif_support");
	add_timer_interval(gettick() + 7 * 1000, aichrif_support_timer, 0, 0, 250);
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
	// Phase 5: respawn dead shells once respawn_delay_ms elapsed.
	add_timer_func_list(aichrif_respawn_timer, "aichrif_respawn");
	add_timer_interval(gettick() + 5 * 1000, aichrif_respawn_timer, 0, 0, 1000);
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
