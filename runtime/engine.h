/* Project Phoenix BC2 0.6 - native engine bindings for the exact CA8169FA server build.
   Every address below is a preferred-image virtual address (base 0x400000) taken from
   Frost.Game.Main_Win32_Final.exe and rebased at runtime. Function prologues are
   fingerprinted at start-up; nothing is called if any fingerprint differs. */
#pragma once
#include <stdint.h>

#define BC2_PREFERRED_BASE 0x400000u

/* ---- functions ---- */
#define VA_ALLOCATE_PLAYER    0x64cdf0u  /* ServerPlayer* thiscall(ServerPlayerManager*, int)            */
#define VA_REGISTER_PLAYER    0x63a660u  /* void thiscall(manager, player, const Name*)                   */
#define VA_SET_TEAM           0x614460u  /* void thiscall(player, int team)                               */
#define VA_SET_AI             0x5f97e0u  /* void thiscall(player, bool)                                   */
#define VA_READY              0x614e10u  /* void thiscall(player)  ready-to-select-kit                     */
#define VA_SELECT_UNLOCKS     0x645290u  /* void thiscall(player, PairVector*)                            */
#define VA_SELECT_KIT         0x625670u  /* void thiscall(player, soldierResId, PairVector*, flags)        */
#define VA_SELECTED_SPAWN     0x6257a0u  /* bool thiscall(player, spawnId)                                */
#define VA_RESOURCE_TO_ID     0x6cd030u  /* uint thiscall(resources, asset)                               */
#define VA_RESOURCE_BY_ID     0x6ccf00u  /* asset thiscall(resources, id)                                 */
#define VA_INPUT_CTOR         0x724d10u  /* EntryInput::EntryInput (0x1b0 bytes, two 0xd0 states)          */
#define VA_INPUT_ATTACH       0x5f9110u  /* player+0xc38 = input                                          */
#define VA_STATE_SET_LEVEL    0x71d290u  /* EntryInputState::setLevel(action, float)                       */
#define VA_STATE_SET_DT       0x710100u  /* EntryInputState::setDeltaTime(float)                          */
#define VA_INPUT_PUSH         0x724e30u  /* EntryInput::push(const EntryInputState*) - swaps cur/prev       */
#define VA_SOLDIER_SET_AIMING 0x7d0700u  /* ServerSoldierEntity::setAiming(yaw 0..2pi, pitch -pi..pi)      */
#define VA_RAY_QUERY          0x741510u  /* physics ray query (ident, mode, from, to, hits, max, flags, mask, exclude) */
#define VA_RAY_KIND_TO_MASK   0x659f80u  /* AI collision kind -> mask (cdecl)                              */
#define VA_LEAVE_GAME         0x64c130u  /* ServerPlayer::leaveGame                                       */
#define VA_SOLDIER_SUICIDE    0x7d7fb0u  /* 1.6.6: soldier suicide (100 damage to itself), as the console's  */
#define VA_ROAD_DATA_VT       0x17aa1acu /* 1.6.6: RoadData (VectorShapeAttachment)                        */
#define VA_VECTOR_SHAPE_VT    0x17aa174u /* 1.6.6: VectorShapeData (Points: Vec3 struct array at +52)       */
#define VA_TSPLINE_VT         0x17e442cu /* 1.6.6: TerrainSplineData (Points: reference array at +60)       */
#define VA_TSPLINE_POINT_VT   0x17e4448u /* 1.6.6: TerrainSplinePointData (Position at +16)                */
#define VA_TSPLINE_DECAL_VT   0x17e46b4u /* 1.6.6: TerrainSplineDecal (Name +16, Material +28, Spline +40)  */
#define VA_DELETE_PLAYER      0x62de20u  /* ServerPlayerManager::deletePlayer(player)                     */
#define VA_SPAWN_ENTITY_ID    0x70df10u  /* spawn entity -> numeric spawn id                              */
#define VA_MANAGER_UPDATE     0x653dd0u  /* ServerPlayerManager::update - post-update detour target         */
#define VA_ENTITY_TRANSFORM   0x70daf0u  /* GameEntity world transform accessor                           */
#define VA_SOLDIER_MOVE_INPUT 0x7dd770u  /* ServerSoldierEntity vtable +0x11c: movement input (entryIndex, EntryInput*) */
#define VA_SPAWN_AI_ASSERT    0x876aa2u  /* cmp [player+0xd84],0 / je / cmp byte [ignoreFlag],0 */
#define VA_SPAWN_AI_IGNORE    0x19cf48bu /* that assert's "Ignore Always" byte */
#define VA_ONKILLED_AI_JE     0x7f580bu  /* ServerSoldierEntity::onKilled: cmp byte [player+0xd84],0 / je skip -> leaveGame + deletePlayer */
#define VA_VEHICLE_VT         0x17475c0u /* ServerVehicleEntity primary vtable (getType 0x7b4a30 -> type 0x209e480) */
#define VA_VEHICLE_SPAWN_VT   0x1757400u /* ServerVehicleSpawnEntity primary vtable (getType 0x835820 -> 0x209F680); spawned vehicles vector +0x370/+0x374 */
#define VA_ENTER_VEHICLE      0x64b800u  /* ServerPlayer::enterVehicle(controllable, entryIndex) - thiscall, ret 8 */
#define VA_EXIT_VEHICLE       0x645080u  /* ServerPlayer: leave the vehicle (bool) - what ChangeVehicle(16) does inside one */
#define VA_EASTL_ALLOC        0x432870u  /* cdecl EASTL allocator (size, "EASTL", 0, 0, file, line) - selectKit allocates its weapon list nodes with it */
#define VA_SET_ONLINE_ID      0x5f94e0u  /* ServerPlayer::setOnlineId: {u64 id; char name[0x81]} -> +0x18 (the client connection's persona) */
#define VA_SET_NET_ONLINE_ID  0x5f9580u  /* ServerPlayer: online id {u64 id; char name[0x81]} at +0xB0, ghosted (dirty bit 8) - set from the client connection */
#define VA_SET_SQUAD_ID       0x61c0a0u  /* ServerPlayer::setSquadId(int) - player+0xac, 24 = no squad */
#define VA_CHARACTER_STEP     0xd60950u  /* character physics single step (dt) - "Physics.SingleStepCharacter"       */

/* ---- globals / vtables / layouts ---- */
#define VA_SERVER_CONTEXT     0x19c4cc0u /* vtable 0x170e120; +0x04 physics, +0x30 player manager, +0x34 level */
#define VA_SERVER_CONTEXT_VT  0x170e120u
#define VA_RESOURCES_PTR      0x19cb950u
#define VA_SPAWN_RULES_PTR    0x19cdd20u
#define VA_PLAYER_VT          0x1716c84u
#define VA_SOLDIER_VT         0x1749f70u
#define VA_SOLDIER_DATA_VT    0x1859b78u
#define VA_CAPTUREPOINT_VT    0x17572e8u /* ServerCapturePointEntity primary vtable */
#define VA_SET_WEAPON_INDEX   0x7be4d0u  /* ServerSoldierEntity::setCurrentWeaponIndex(int) - the game uses 0 (primary) at spawn */
#define VA_INTERACT           0x7d98b0u  /* ServerInteractionEntity::interact(soldier) -> bool: MaxUses, DelayBetweenUses, animation, then fires its event (arms / defuses) */
#define VA_INTERACTION_VT     0x1746c68u /* ServerInteractionEntity (getType 0x7b5dd0 -> 0x209e758, static info 0x209df48): Rush M-COM arm/defuse */
#define VA_ENTRY_STATE_VT     0x17194c8u

/* ServerPlayer */
#define PL_TEAM        0xa8
#define PL_ID          0x154
#define PL_FLAGS17C    0x17c
#define PL_UNLOCKS     0x1e8
#define PL_INPUT       0xc38
#define PL_SOLDIER     0xc3c
#define PL_ENTRY       0xc5c
#define PL_INGAME      0xc80
#define PL_SPAWN_ID    0xd20
#define PL_SOLDIER_DAT 0xd24
#define PL_READY       0xd68
#define PL_AI          0xd84
#define PL_SQUAD       0xac
#define PL_CONTROLLED  0xc6c   /* current controllable: the soldier, or the vehicle the player sits in */
#define VEH_DATA       0x0c
#define VEHD_NAMESID   0x210
#define VEHD_MAXPLAYERS 0x20c
#define PL_UNLOCK_BITS 0x168   /* bitset {?, words +4, ?, bits +0xc, word count +0x10}; ServerPlayer::setUnlocks 0x5f96a0 */
/* ServerSoldierEntity */
#define SOL_ENTRIES    0x90    /* entry vector begin/end +0x90/+0x94; entry +0xe8 = player */
#define SOL_PHYSICS    0x140   /* character physics; +0xde = stepped per input frame */
/* EntryInputState (0xd0) */
#define ST_ANALOG      0x08
#define ST_DIGITAL     0x98
#define ST_DT          0xa0
#define ST_YAW         0xa8
#define ST_PITCH       0xac
#define ST_TICK        0xb8   /* frame number of the input (EntryInputState::setTick 0x710690) */
/* ServerCapturePointEntity */
#define CP_DATA        0x0c
#define CP_OWNER       0x7c
#define CP_PROGRESS    0x98
#define CP_ATTACKER    0xa8
/* CapturePointEntityData */
#define CPD_ONLY_TEAM  0x84
#define CPD_INDEX      0xa4
#define CPD_LOCATION   0xcc
#define CPD_RADIUS     0x11c
#define CPD_INIT_OWNER 0x120
#define CPD_NAME       0x124
#define CPD_NOCHANGE   0x13a
/* TeamData / kits */
#define TEAMENT_TEAMDATA 0x88
#define TEAMDATA_KITS    0x20
#define SOLDIERASSET_DATA 0x50
#define ASSET_NAME       0x0c
#define SED_WEAPON_UNLOCKS 0x1a0

typedef struct BC2Engine {
    uint32_t base;          /* live module base */
    int ready;              /* fingerprints verified */
} BC2Engine;

extern BC2Engine g_engine;
#define RB(va) (g_engine.base + ((uint32_t)(va) - BC2_PREFERRED_BASE))

typedef struct PhxPair { uint32_t category, resourceId; } PhxPair;
typedef struct PhxPairVector { PhxPair *begin, *end, *capacity; } PhxPairVector;
typedef struct PhxName { const char *begin, *end, *capacity; } PhxName;

int  eng_verify(char *why, unsigned whySize);       /* checks every native fingerprint */
int  eng_read(uint32_t address, void *out, unsigned size);
int  eng_u32(uint32_t address, uint32_t *out);
int  eng_write32(uint32_t address, uint32_t value);
int  eng_ptr(uint32_t p);
int  eng_string(uint32_t stringField, char *out, unsigned size); /* {begin,end,cap} string */

uint32_t eng_context(void);
uint32_t eng_player_manager(void);
uint32_t eng_level_object(void);
uint32_t eng_team_info(void);
uint32_t eng_spawn_manager(void);
uint32_t eng_resources(void);

uint32_t eng_allocate_player(uint32_t manager);
void eng_register_player(uint32_t manager, uint32_t player, const PhxName *name);
void eng_set_team(uint32_t player, uint32_t team);
void eng_set_ai(uint32_t player, int ai);
void eng_ready(uint32_t player);
void eng_select_unlocks(uint32_t player, PhxPairVector *v);
void eng_select_kit(uint32_t player, uint32_t soldierId, PhxPairVector *v, uint32_t flags);
int  eng_selected_spawn(uint32_t player, uint32_t spawnId);
uint32_t eng_resource_to_id(uint32_t asset);
uint32_t eng_resource_by_id(uint32_t id);
void eng_input_ctor(void *input);
void eng_input_attach(uint32_t player, void *input);
void eng_state_set_level(void *state, int action, float value);
void eng_state_set_dt(void *state, float dt);
void eng_input_push(void *input, const void *state);
void eng_soldier_set_aiming(uint32_t soldier, float yaw, float pitch);
/* Walks the soldier one input frame, the way the server does for each network input of a human:
   movement input (vt+0x11c) and, with Physics.SingleStepCharacter, the character physics step.
   Returns 0 if the soldier/entry/physics layout does not check out (nothing is called then). */
int  eng_soldier_move_step(uint32_t soldier, uint32_t player, void *input, float dt, int physicsStep);
/* ServerSoldierSpawnEntity::spawnSoldier asserts "AI already spawned from the spawnPoint" (a load/save
   restriction of the single-player engine). Sets the assert's own "Ignore Always" flag. */
int  eng_silence_spawn_assert(void);
int  eng_keep_ai_players(int on);
int  eng_interact(uint32_t interaction, uint32_t soldier);   /* 1 ok, 2 ok after skipping the human-only checks, 0 refused, -1 bad */
int  eng_interact_why(void);   /* last refusal: 1 soldier/area check, 3 arm animation; +10 = then accepted with the checks skipped */
int  eng_soldier_weapon(uint32_t soldier);                 /* current weapon index: [[soldier+0x2D4]+0x14C], -1 unknown */
int  eng_soldier_set_weapon(uint32_t soldier, int index);   /* 1.5.1: what ServerPlayer::entityInteract does for humans (AI players skip ServerPlayer::update) */   /* 1.5: dead AI players are kept (like humans) instead of deleted; 0 restores the game's code */
/* The soldier only gets kit weapons whose unlock bit is set in the player's unlock bitset
   (0x7fde20). Human players get theirs from the server on connect; bots have an empty one.
   Sets every bit in place. Returns the number of bits, 0 if there is no bitset. */
int  eng_player_unlock_all(uint32_t player, int *bitsOut);
int  eng_player_unlock_bits(uint32_t player, unsigned *setBits, unsigned *totalBits);
void eng_set_squad(uint32_t player, uint32_t squad);
/* resource id of an object, via the id->object table when the reverse table has no entry */
uint32_t eng_resource_id_any(uint32_t object);
int  eng_resource_scan(const uint32_t *objects, uint32_t *ids, int count);
/* Bots are AI players, so the server makes their character an AI character
   (dice::physics::CharacterEntity::setAI(true): +0xDD = 1, +0xDE = 0). AI characters ignore throttle/strafe and
   take their motion from a velocity the single-player AI sets. This switches the character to the
   human-player mode (+0xDD = 0, +0xDE = 1: stepped once per input frame, which Phoenix does).
   Returns 1 when it changed the character, 2 when it already was in that mode, 0 when it is unsafe/unknown. */
int  eng_character_player_mode(uint32_t soldier);
int  eng_character_ai_mode(uint32_t soldier);                       /* back to +0xDD = 1, +0xDE = 0 */
int  eng_character_drive_velocity(uint32_t soldier, float vx, float vz);   /* Havok proxy (+0x40) velocity +0x10 */
void eng_debug_character(uint32_t soldier, char *out, unsigned size);
int  eng_player_add_weapon(uint32_t player, uint32_t weaponAsset);   /* append to the selected-unlock list (+0xD28) */
int  eng_player_weapon_count(uint32_t player);
int  eng_set_net_online_id(uint32_t player, uint32_t idLow, uint32_t idHigh, const char *name);
int  eng_set_online_id(uint32_t player, uint32_t idLow, uint32_t idHigh, const char *name);   /* +0x18, the logged-in persona */
void eng_describe_online_id(uint32_t player, char *out, unsigned size);
int  eng_character_state(uint32_t soldier);                          /* 0 OnGround .. 6 Swimming, -1 unknown */
int  eng_character_push(uint32_t soldier, float vx, float vy, float vz);   /* overwrite the proxy velocity */   /* physics flags + input block, for logs */
void eng_leave_game(uint32_t player);
int eng_soldier_kill(uint32_t soldier);   /* 1.6.6: 1 = the soldier was given its suicide (dies the normal way) */
void eng_delete_player(uint32_t manager, uint32_t player);
uint32_t eng_spawn_entity_id(uint32_t spawnEntity);
int  eng_entity_position(uint32_t entity, float out[3]); /* via the verified transform layout */
int  eng_entity_matrix(uint32_t entity, float m[16]);
/* vehicles */
int  eng_vehicle_seats(uint32_t vehicle);                          /* entry count, 0 if not a vehicle */
uint32_t eng_vehicle_occupant(uint32_t vehicle, int seat);         /* player in that seat (0 = free) */
int  eng_vehicle_seat_allowed(uint32_t vehicle, uint32_t player, int seat);   /* vt+0x104 isAllowedToEnter */
int  eng_enter_vehicle(uint32_t player, uint32_t vehicle, int seat);
int  eng_exit_vehicle(uint32_t player);
uint32_t eng_player_controlled(uint32_t player);

/* Physics line query. Returns 1 on a hit and fills the nearest hit position/normal. */
int  eng_ray(const float from[3], const float to[3], float hit[3], float normal[3]);
void eng_ray_set_mask(uint32_t mask);
void eng_ray_set_masks(const uint32_t *masks, int count);   /* rays test each mask and keep the nearest hit */
int  eng_ray_masked(uint32_t mask, const float from[3], const float to[3], float hit[3], float normal[3]);
uint32_t eng_game_tick(void);   /* [[context+0xC]]: the simulation frame number the native AI stamps on inputs */
uint32_t eng_ray_mask(void);
uint32_t eng_kind_mask(int kind);
unsigned eng_ray_count(void);
