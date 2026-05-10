// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder
//
// AI inter-server wire formats. Shared between ai-server, char-server
// forwarder and map-server. All packets are length-prefixed:
//   cmd:W, len:W, payload...
// Length includes the cmd+len header itself.

#ifndef COMMON_AI_PACKETS_HPP
#define COMMON_AI_PACKETS_HPP

#include "cbasetypes.hpp"
#include "mmo.hpp" // NAME_LENGTH, MAP_NAME_LENGTH_EXT

#pragma pack(push, 1)

/// PACKET_AI_SHELL_SPAWN (0x2b40)
/// ai-server -> char-server -> map-server
struct PACKET_AI_SHELL_SPAWN_S {
	uint16 cmd;          // 0x2b40
	uint16 len;          // sizeof(*this)
	uint32 shell_id;     // virtual account_id (and char_id)
	char   name[NAME_LENGTH];
	uint16 class_;       // job
	uint8  sex;          // 0=female, 1=male
	uint16 hair;
	uint16 hair_color;
	uint16 cloth_color;
	uint16 head_top;
	uint16 head_mid;
	uint16 head_bottom;
	uint16 weapon;
	uint16 shield;
	uint16 robe;
	char   map_name[MAP_NAME_LENGTH_EXT];
	uint16 x;
	uint16 y;
	uint8  dir;          // facing 0..7
	uint8  behavior_id;  // ai-server-side enum (Phase 2)
	uint8  tier;         // 0=town, 1=field, 2=dungeon (Phase 3.12 stat ramp)
	uint8  role;         // Phase 6.5 — AI_HIRE_ROLE_* (was pad)
	// Phase 6 — mercenary mode. 0 = autonomous shell (population spawner);
	// non-zero = the BL_PC account_id this shell is hired by. Map-server
	// resolves the owner via map_id2sd at REPORT time and ai-server uses
	// it to drive follow / support behavior. When the owner logs out, map
	// emits AI_EVT_OWNER_GONE and ai-server despawns the shell.
	// owner_cid = char_id; the merc is bound to a specific character, not
	// the whole account, so logging in with a different char on the same
	// account does NOT bring the merc back.
	uint32 owner_aid;
	uint32 owner_cid;
	uint16 base_level;
	uint16 job_level;
	uint16 str_, agi_, vit_, int_, dex_, luk_;
	uint16 speed;
	uint16 pad2;
	// Equipment item nameids by slot, see ai_equip_slot in ai/profile.hpp.
	// Indexes: 0=HandR/weapon 1=HeadTop 2=HeadMid 3=HeadLow 4=Armor
	//          5=HandL/shield 6=Garment 7=Shoes 8=AccR 9=AccL
	// 0 means empty slot.
	uint32 equip[10];
};

/// PACKET_AI_SHELL_DESPAWN (0x2b41)
struct PACKET_AI_SHELL_DESPAWN_S {
	uint16 cmd;
	uint16 len;
	uint32 shell_id;
	uint8  reason;
};

/// PACKET_AI_SHELL_CMD (0x2b42) — variable-length, parsed by op
struct PACKET_AI_SHELL_CMD_HEADER {
	uint16 cmd;
	uint16 len;
	uint32 shell_id;
	uint8  op;
};

constexpr uint8 AI_CMD_WALK_TO     = 1;
constexpr uint8 AI_CMD_ATTACK      = 2;
constexpr uint8 AI_CMD_CAST        = 3;
constexpr uint8 AI_CMD_SAY         = 4;
constexpr uint8 AI_CMD_EMOTE       = 5;
constexpr uint8 AI_CMD_STOP_ATTACK = 6;
constexpr uint8 AI_CMD_SIT         = 7;
constexpr uint8 AI_CMD_STAND       = 8;

constexpr uint8 AI_CAST_KIND_ID     = 0;
constexpr uint8 AI_CAST_KIND_GROUND = 1;
constexpr uint8 AI_CAST_KIND_SELF   = 2;

struct PACKET_AI_CMD_WALK_TO_S {
	PACKET_AI_SHELL_CMD_HEADER hdr;
	uint16 x;
	uint16 y;
};

/// AI_CMD_ATTACK — start auto-attack on a target (mob or shell).
struct PACKET_AI_CMD_ATTACK_S {
	PACKET_AI_SHELL_CMD_HEADER hdr;
	uint32 target_id;
	uint8  continuous; // 1=keep attacking, 0=single hit
};

/// AI_CMD_STOP_ATTACK — stop the current attack chain.
struct PACKET_AI_CMD_STOP_ATTACK_S {
	PACKET_AI_SHELL_CMD_HEADER hdr;
};

/// AI_CMD_SAY — chat overhead the shell, broadcast to nearby clients.
struct PACKET_AI_CMD_SAY_S {
	PACKET_AI_SHELL_CMD_HEADER hdr;
	uint16 mes_len;
	char   mes[120];
};

/// AI_CMD_EMOTE — play an emoticon over the shell.
struct PACKET_AI_CMD_EMOTE_S {
	PACKET_AI_SHELL_CMD_HEADER hdr;
	uint8 emote_id;
	uint8 pad[3];
};

/// AI_CMD_CAST — cast a skill on a target id (BL) or on a ground cell (x,y).
/// kind: 0 = id-targeted, 1 = ground-targeted, 2 = self-targeted.
/// SkillId is sent as a string so the map-server can resolve via skill_db.
struct PACKET_AI_CMD_CAST_S {
	PACKET_AI_SHELL_CMD_HEADER hdr;
	char   skill_name[24];
	uint16 skill_lv;
	uint8  kind;        // 0=id, 1=ground, 2=self
	uint8  pad;
	uint32 target_id;   // when kind=0 or kind=2
	uint16 x, y;        // when kind=1
};

/// PACKET_AI_SHELL_SPAWNED (0x2b50) — map-server ack
struct PACKET_AI_SHELL_SPAWNED_S {
	uint16 cmd;
	uint16 len;
	uint32 shell_id;
	uint8  ok;
	uint8  err;
};

/// Phase 5 — status bitmask used in REPORT for self + each NEARBY_ENEMY.
/// 16 combat-relevant SCs. skill_picker maps SC names from YAML to these
/// bits and evaluates SELF_STATUS / ENEMY_STATUS conditions against them.
/// Bits 16..31 reserved for future expansion (buff line, song line, etc).
constexpr uint32 AI_ST_STONE     = 1u << 0;
constexpr uint32 AI_ST_FREEZE    = 1u << 1;
constexpr uint32 AI_ST_STUN      = 1u << 2;
constexpr uint32 AI_ST_SLEEP     = 1u << 3;
constexpr uint32 AI_ST_SILENCE   = 1u << 4;
constexpr uint32 AI_ST_CURSE     = 1u << 5;
constexpr uint32 AI_ST_CONFUSION = 1u << 6;
constexpr uint32 AI_ST_BLIND     = 1u << 7;
constexpr uint32 AI_ST_POISON    = 1u << 8;
constexpr uint32 AI_ST_BLEEDING  = 1u << 9;
constexpr uint32 AI_ST_HIDING    = 1u << 10;
constexpr uint32 AI_ST_CLOAKING  = 1u << 11;
constexpr uint32 AI_ST_ENDURE    = 1u << 12;
constexpr uint32 AI_ST_PROVOKE   = 1u << 13;
constexpr uint32 AI_ST_AUTOGUARD = 1u << 14;
constexpr uint32 AI_ST_INC_AGI   = 1u << 15;
// Phase 6 — Priest/AB support buffs. Mercenary skill picker uses
// NOT_OWNER_STATUS to decide when to refresh.
constexpr uint32 AI_ST_BLESSING  = 1u << 16;
constexpr uint32 AI_ST_KYRIE     = 1u << 17;
constexpr uint32 AI_ST_MAGNIFICAT= 1u << 18;
constexpr uint32 AI_ST_ASSUMPTIO = 1u << 19;
constexpr uint32 AI_ST_ANGELUS   = 1u << 20;
constexpr uint32 AI_ST_IMPOSITIO = 1u << 21;
constexpr uint32 AI_ST_GLORIA    = 1u << 22;
constexpr uint32 AI_ST_EXPIATIO  = 1u << 23;
constexpr uint32 AI_ST_SECRAMENT = 1u << 24;
constexpr uint32 AI_ST_OFFERTORIUM = 1u << 25;
constexpr uint32 AI_ST_RENOVATIO   = 1u << 26;

/// One row in PACKET_AI_SHELL_REPORT.enemies[]. mob_class kept for
/// behavior-side mob_db lookups; distance pre-computed on map side.
constexpr uint8 AI_ENEMY_FLAG_BOSS    = 1 << 0;  // miniboss or MVP
constexpr uint8 AI_ENEMY_FLAG_MVP     = 1 << 1;  // MVP only

struct PACKET_AI_NEARBY_ENEMY {
	uint32 id;          // block_list id
	uint16 mob_class;   // mob_db id; 0 if not BL_MOB
	uint16 hp_pct;      // 0..100
	uint16 x, y;
	uint8  distance;    // chebyshev cells
	uint8  race;        // Phase 5 — see e_race (RC_FORMLESS..RC_ALL)
	uint8  element;     // Phase 5 — packed: low nibble = e_element type, high nibble = ele_lv
	uint8  flags;       // Phase 5 — AI_ENEMY_FLAG_* bits
	uint32 statuses;    // Phase 5 — bitmask of AI_ST_* on this enemy
};

constexpr uint8 AI_REPORT_MAX_ENEMIES = 8;

/// Phase 6 — party member snapshot for mercenary support targeting.
/// Sent as a fixed-size array in PACKET_AI_SHELL_REPORT so the ai-server
/// can iterate party members and pick whichever one needs the next buff/
/// heal (target=PARTY in the rotation YAML).
struct PACKET_AI_PARTY_MEMBER {
	uint32 account_id;	// 0 = empty slot
	uint16 hp_pct;		// 0..100
	uint16 sp_pct;
	uint32 statuses;	// AI_ST_* bitmask
};
constexpr uint8 AI_REPORT_MAX_PARTY = 12;

/// PACKET_AI_SHELL_EVENT (0x2b52) — async map → ai notification.
/// kind tells the payload shape: ATTACKED_BY supplies attacker_id+dmg,
/// DIED supplies killer_id, WHISPERED_BY/MENTIONED reserved for Phase 3.
constexpr uint8 AI_EVT_ATTACKED_BY     = 1;
constexpr uint8 AI_EVT_DIED            = 2;
constexpr uint8 AI_EVT_RESURRECTED     = 3;
constexpr uint8 AI_EVT_WHISPERED_BY    = 4;
constexpr uint8 AI_EVT_MENTIONED       = 5;
constexpr uint8 AI_EVT_OWNER_GONE      = 6;	// Phase 6 — owner logged out / disconnected
constexpr uint8 AI_EVT_OWNER_BACK      = 7;	// Phase 6 — owner reconnected (carried by PACKET_AI_OWNER_BACK)

struct PACKET_AI_SHELL_EVENT_S {
	uint16 cmd;
	uint16 len;
	uint32 shell_id;
	uint8  kind;
	uint8  pad[3];
	uint32 actor_id;     // attacker / killer / whisperer
	uint32 dmg;          // ATTACKED_BY only
};

/// PACKET_AI_SHELL_REPORT (0x2b51) — periodic map → ai snapshot.
/// Fixed-size to keep parsing trivial; padded with zeroed enemies if fewer.
struct PACKET_AI_SHELL_REPORT_S {
	uint16 cmd;
	uint16 len;
	uint32 shell_id;
	uint16 mapindex;    // mapindex_id of current map (Phase 3.7 drift check)
	uint16 x, y;
	uint32 hp, max_hp;
	uint32 sp, max_sp;
	uint32 target_id;   // 0 = no target
	uint32 self_statuses; // Phase 5 — bitmask of AI_ST_* on the shell itself
	uint8  enemy_count; // 0..AI_REPORT_MAX_ENEMIES
	uint8  pad[3];
	// Phase 6 — owner snapshot for mercenary follow/support behavior.
	// owner_present=0 when the shell has no owner (autonomous) OR when the
	// owner is offline/zoning; in both cases the rest of the owner_* block
	// is meaningless and should be ignored.
	uint8  owner_present;
	uint8  owner_pad[3];
	uint16 owner_mapindex;
	uint16 owner_x, owner_y;
	uint16 owner_hp_pct;
	uint16 owner_sp_pct;
	uint32 owner_statuses;
	uint32 owner_target_id;	// Phase 6 — owner's current attack target (BL id), 0 if none
	PACKET_AI_NEARBY_ENEMY enemies[AI_REPORT_MAX_ENEMIES];
	// Phase 6 — full party roster (mercenary support targeting). Slots
	// with account_id=0 are empty; party_count is the number of valid
	// entries (<= AI_REPORT_MAX_PARTY). Excludes the merc itself.
	uint8 party_count;
	uint8 party_pad[3];
	PACKET_AI_PARTY_MEMBER party_members[AI_REPORT_MAX_PARTY];
};

/// PACKET_AI_DISMISS_REQUEST (0x2b45) — map -> char -> ai.
/// Player runs @dismiss / NPC -> ai-server tears down their merc shell
/// without waiting for the contract timer. No ack. Routed by char_id;
/// since Phase 6.3 each char can have one merc per role (support, tank)
/// so we also carry the role to dismiss. role == 0xFF = ALL roles.
struct PACKET_AI_DISMISS_REQUEST_S {
	uint16 cmd;
	uint16 len;
	uint32 owner_cid;
	uint8  role;       // AI_HIRE_ROLE_SUPPORT, AI_HIRE_ROLE_TANK, or 0xFF for ALL
	uint8  pad[3];
};
constexpr uint8 AI_HIRE_ROLE_ALL = 0xFF;

/// PACKET_AI_OWNER_BACK (0x2b54) — map -> char -> ai.
/// Phase 6 — fired by map-server's suspended-owner poll when the merc's
/// owning character reconnects. ai-server reuses init_snap to re-spawn
/// the merc next to the player and resumes the paused hire contract.
/// Both owner_aid (current runtime aid) and owner_cid (persistent identity)
/// are sent; ai-server verifies cid matches before re-spawning.
struct PACKET_AI_OWNER_BACK_S {
	uint16 cmd;
	uint16 len;
	uint32 shell_id;
	uint32 owner_aid;
	uint32 owner_cid;
	char   map_name[MAP_NAME_LENGTH_EXT];
	uint16 x, y;
};

/// PACKET_AI_HIRE_REQUEST (0x2b44) — map -> char -> ai.
/// Player runs @hire / NPC script -> map-server fills owner_aid + spawn
/// point and sends this. ai-server allocates a shell, picks the right
/// profile tier, and emits a normal SHELL_SPAWN. No ack packet (the
/// player feels the success when the merc spawns next to them).
// Phase 6.2 — role flag in HIRE_REQUEST. SUPPORT=0 (default, Acolyte/Priest
// line, support tick only). TANK=1 (Sw/Crusader/Paladin/RG line, runs combat
// tick + defensive support). Old build with role=0-implicit still hires
// support correctly because byte was previously zeroed pad.
constexpr uint8 AI_HIRE_ROLE_SUPPORT = 0;
constexpr uint8 AI_HIRE_ROLE_TANK    = 1;

struct PACKET_AI_HIRE_REQUEST_S {
	uint16 cmd;
	uint16 len;
	uint32 owner_aid;
	uint32 owner_cid;	// Phase 6 — char_id; merc is bound to character, not account
	uint16 job;       // 4=Acolyte, 8=Priest, 4014=High Priest, 4063=Arch Bishop
	uint8  tier;      // 0..2 — picked by map-side based on player's level
	uint8  role;      // Phase 6.2 — AI_HIRE_ROLE_* (was pad; zero = SUPPORT)
	char   map_name[MAP_NAME_LENGTH_EXT];
	uint16 x, y;
	uint32 duration_ms;
	// Phase 6 — when non-zero, override the profile's BaseLevel/JobLevel.
	// Lets the hire NPC scale the merc to the party leader's level so an
	// lvl-175 player gets an lvl-175 AB instead of the fixed T2 lvl 130.
	uint16 base_level_override;
	uint16 job_level_override;
};

/// AI_CMD_WARP — pc_setpos shell back to a known map+cell. Used for drift
/// correction when a town shell wanders through a warp tile.
constexpr uint8 AI_CMD_WARP = 9;
struct PACKET_AI_CMD_WARP_S {
	PACKET_AI_SHELL_CMD_HEADER hdr;
	char   map_name[12]; // MAP_NAME_LENGTH (without _EXT) is enough
	uint16 x, y;
	uint8  pad[2];
};

#pragma pack(pop)

#endif /* COMMON_AI_PACKETS_HPP */
