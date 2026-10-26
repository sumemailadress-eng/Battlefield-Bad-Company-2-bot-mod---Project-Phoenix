/* Project Phoenix BC2 0.6 runtime: shared types. */
#pragma once
#include <stdint.h>
#include "engine.h"
#include "bc2_observe.h"

#define PHX_VERSION "1.6.6"
#define MAX_BOTS 48
#define MAX_CPS 16
#define MAX_SPAWNS 64
#define MAX_KITS 16
#define MAX_UNLOCK_CHOICES 48

enum { CLS_ASSAULT, CLS_ENGINEER, CLS_MEDIC, CLS_RECON, CLS_COUNT };

typedef struct PhxConfig {
    int botsPerTeam;        /* bots on each team (humans are extra) */
    int fillToPerTeam;      /* 0 = off; otherwise bots fill each team up to this many players */
    float respawnDelay;     /* seconds after death */
    int difficulty;         /* 1 easy, 2 normal, 3 hard */
    int navBuild;           /* build the ray navigation grid */
    float navCell;          /* grid cell size (m) */
    float navBudgetMs;      /* per-tick build budget */
    int pitchSign;          /* +1: positive pitch looks up */
    int logEvery;           /* status line period (s) */
    int grenades;
    float aimSkill;         /* multiplier on aim error (lower = better) */
    int verbose;
    int moveStep;           /* drive the soldier movement step ourselves (AI has no network input) */
    int unlockAll;          /* bots get every weapon/gadget unlock */
    int equipment;          /* gadgets, grenade launcher */
    int revive;             /* medics revive downed human teammates */
    int fireteams;          /* bots move in groups of four */
    int followHuman;        /* a fireteam sometimes follows a human teammate */
    int realSquads;         /* also put bots into engine squads (experimental) */
    int groupSize;          /* attack groups: one flag targeted per this many bots */
    int guardEvery;         /* one front-line guard per this many bots (0 = none) */
    int vehicles;           /* 0 off, 1 ride as passengers, 2 also drive */
    float frozenRespawn;    /* seconds pinned in place before a bot is redeployed (0 = never) */
    int scoreboard;         /* give bots an online identity so clients list them (experimental) */
    int learn;              /* learn stuck fixes, danger and good positions per map (navcache\\<map>.learn) */
    int helicopters;        /* bots fly helicopters with a self-calibrating pilot (Helicopters=1) */
    float botDamage;
    int quietTeam;          /* join the team without the "switched teams" message (QuietTeamJoin=1) */
    /* 1.5 */
    int keepPlayers;        /* a dead bot keeps its player and redeploys (KeepBotPlayers=1): no "switched teams", scores kept */
    int emplacements;       /* bots use stationary weapons when it is safe (StationaryWeapons=1) */
    int boats;              /* bots drive boats along the water (Boats=1) */
    int learnHumans;        /* bots learn the routes human players walk and drive (LearnFromHumans=1) */
    int objectives;         /* Rush / Squad Rush: arm and defuse the M-COMs (RushObjectives=1) */
} PhxConfig;
extern PhxConfig g_cfg;

typedef struct CapturePoint {
    uint32_t entity, data;
    float pos[3], radius;
    int owner, attacker, onlyTeam, locked, index;
    float progress;
    char name[32];
    /* 1.5 Rush / Squad Rush: an M-COM (ServerInteractionEntity) stands in for a flag - owner = the defending team */
    int isObj; uint32_t objEnt[4]; int objTeam[4], objType[4], objN; uint32_t armEnt, disarmEnt; int armed; float holdTime, armHold, disarmHold; double armedAt, enemyNearAt;
} CapturePoint;

typedef struct SpawnPoint {
    uint32_t entity, id;
    int team, enabled;
    float pos[3]; int posValid;
} SpawnPoint;

typedef struct UnlockChoice { uint32_t slot, resourceId, weapon; char name[64]; } UnlockChoice;

typedef struct KitInfo {
    uint32_t asset, data, resourceId;
    int cls;
    char name[64];
    int choiceCount;
    UnlockChoice choices[MAX_UNLOCK_CHOICES];
} KitInfo;

#define MAX_VEH 64
enum { VK_GROUND, VK_HELI, VK_BOAT, VK_FIXED, VK_UNKNOWN, VK_EMPLACE };   /* VK_FIXED: jets, UAV, mortar - never used */
typedef struct VehicleInfo {
    uint32_t entity, data;
    float pos[3], heading;
    float right[3], up[3], fwd[3];   /* model axes (rows of the world matrix) */
    int seats, kind;
    int team;                        /* team of the spawner that made it (0 = neutral / unknown) */
    char name[48];
    float health; int healthOk;      /* 1.5: ServerControllableEntity health (+0x34), as for soldiers */
} VehicleInfo;

typedef struct VehicleSpawner {
    uint32_t entity, data, blueprint;
    float pos[3];
    int team, enabled, autoSpawn, fromStart, alive, kind;
    float delay, initDelay;
    char name[64];
} VehicleSpawner;
/* 1.6.6: a road line read from the level data (roads.c) */
enum { ROAD_SRC_ROAD, ROAD_SRC_DECAL, ROAD_SRC_SPLINE };
typedef struct RoadLine { int src, first, n, named, accepted; float length, onGround; char name[48]; } RoadLine;
void roads_collect(const uint32_t *roadData, int nr, const uint32_t *decals, int nd, const uint32_t *splines, int ns);
int  roads_wanted(void);
const RoadLine *roads_lines(int *n, const float (**pts)[3]);
void navg_roads_set(RoadLine *lines, int n, const float (*pts)[3]);
#define MAX_VSPAWN 64

enum { MODE_CONQUEST, MODE_RUSH, MODE_SQRUSH, MODE_SQDM };
typedef struct World {
    uint32_t manager, level, spawnMgr, teamInfo;
    unsigned levelSerial;           /* increments when the level object changes */
    double levelSince;
    BC2Observation obs;
    int obsReady;
    CapturePoint cps[MAX_CPS]; int cpCount;
    SpawnPoint spawns[MAX_SPAWNS]; int spawnCount;
    KitInfo kits[5][MAX_KITS]; int kitCount[5];
    int humans[5], bots[5];
    int teamCount;                  /* 2, or 4 in Squad Deathmatch (1.5) */
    int mode;                       /* MODE_* from the level name and what is on the map */
    float boundsMin[3], boundsMax[3]; int boundsValid;
    uint32_t humanKitFlags; int humanKitFlagsKnown;
    VehicleInfo veh[MAX_VEH]; int vehCount;
    VehicleSpawner vspawn[MAX_VSPAWN]; int vspawnCount;
    char levelName[64], mapName[48];   /* e.g. Levels/MP_006CQ, Arica Harbor (if found in the level object) */
} World;
extern World g_world;

/* logging */
void rt_log(const char *fmt, ...);
double rt_now(void);   /* seconds */

/* world.c */
void world_tick(double now);
void world_cp_scan_request(void);
int  world_find_player(uint32_t pointer, uint32_t id);   /* index into g_world.obs.players or -1 */
const KitInfo *world_kit_for(int team, int cls);
int  world_ray_selftest(void);
int  world_vehicle_read(uint32_t entity, VehicleInfo *v);   /* 1 = still a live vehicle (position refreshed) */

/* bots.c */
void bots_tick(double now, float dt);
void bots_stop_all(void);
int bots_stop_step(double now);   /* 1.6.6: staged stop, 1 = done */
void bots_status(void);
int  bots_count(void);

/* nav glue (navglue.c) */
void navg_tick(double now);
int  navg_ready(void);
int  navg_path(int owner, const float *from, const float *to, float *out, int maxPts, int *deferred);
void navg_status(char *out, unsigned size);
void navg_reset(void);
void navg_blocked(const float *pos, const float *towards);
void navg_reprobe(const float *pos, float radius);            /* queue a re-probe (blown walls, new obstacles) */
int  navg_snap(const float *pos, float *out);
int  navg_main_area(const float *pos);   /* 1.6.3: on the big connected walkable area */
int  navg_on_ground(const float *pos);   /* 1.6.3: the lowest walkable surface of its cell */
int  navg_floors(const float *pos, float *heights, int max);
int  navg_vehicle_block(const float *pos, float radius);
/* turret.c: find a vehicle's turret transform by testing inputs */
typedef struct TurPath { uint8_t depth, local; uint16_t off[3]; } TurPath;
void tur_reset(void);
uint32_t tur_busy_vehicle(void);
int  tur_search(uint32_t veh, const float *pos, int budget);
int  tur_read(uint32_t veh, const TurPath *p, const VehicleInfo *vi, float *rel, float *pit);
int  tur_count(void);
const TurPath *tur_path(int i);
void tur_baseline(uint32_t veh, const VehicleInfo *vi);
int  tur_mark_noisy(uint32_t veh, const VehicleInfo *vi, float thresh);
int  tur_moved(uint32_t veh, const VehicleInfo *vi, int pitch, float *amount);
float tur_change(uint32_t veh, const VehicleInfo *vi, int i, int pitch);
void tur_done(void);
int  navg_path_vehicle(int owner, const float *from, const float *to, float *out, int maxPts, int *deferred);
float navg_vehicle_gap(void);       /* 1.6.6: last vehicle route: metres from its end to the flag (off-road flag) */
int  navg_vehicle_partial(void);    /* 1.6.6: last vehicle route is a partial one (search ran out) */
int  navg_drive_line_ok(const float *a, const float *b);   /* 1.6.6: the straight line stays on the drive map */
const char *navg_last_error(void);
int  navg_mark_water(const float *pos, float radius);
int  navg_land_near(const float *pos, float radius, float *out);
void learn_water(const float *pos);
void learn_vehicle_stuck(const float *pos);   /* 1.6.5 */
int  learn_near_water(const float *pos, float r);   /* 1.6 */
int  navg_overwatch(const float *target, float rmin, float rmax, int upperOnly, float *out, int max);

/* learning (learn.c) */
enum { LA_JUMP, LA_KNIFE, LA_CROUCH, LA_DETOUR, LA_BACK, LEARN_ACTIONS };
void  learn_tick(double now);
/* indoor.c (1.6.3): surveyed ways into buildings */
void indoor_reset(void);
void indoor_request(const float *target, const char *why);
void indoor_tick(void);
int  indoor_route(const float *target, const float **pts, const unsigned char **doors);
int  indoor_known(const float *target);
void  learn_humans(double now);   /* 1.5: record the routes human players walk and drive */
void  learn_reset(void);
void  learn_save_now(void);
int   learn_stuck_event(const float *pos, float heading);
void  learn_stuck_result(int spot, int action, int success);
int   learn_hint(const float *pos, float heading);
void  learn_death(const float *pos);
void  learn_kill(const float *pos);
float learn_danger(const float *pos);
void  learn_spot_result(const float *spot, int used, int kill, int death);
float learn_spot_score(const float *spot);

/* misc */
float rt_randf(void);   /* 0..1 */
uint32_t rt_rand(void);
