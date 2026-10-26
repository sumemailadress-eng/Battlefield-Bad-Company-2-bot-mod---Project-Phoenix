/* Project Phoenix BC2 0.6 - bot roster, lifecycle and brain.
   BC2 deletes an AI player when its soldier dies (ServerSoldierEntity::onKilled -> leaveGame + deletePlayer),
   so every life is a fresh native player: create -> register -> kit -> deploy -> play -> (engine deletes it).
   A dead bot's pointer is never touched again; bots are only found through the live roster. */
#ifdef PHX_HARNESS
#include "harness_win.h"
#else
#include "win_common.h"
#endif
#include "rt.h"
#include <ctype.h>
#include "bc2_input_actions.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define PI 3.14159265f
#define TWO_PI 6.28318531f
#define MAX_PATH_PTS 48

enum { BS_OFF, BS_WAIT, BS_DEPLOYING, BS_ALIVE };

typedef struct Bot {
    int used, team, cls, state, lives, kills;
    char name[32];
    uint32_t player, id, soldier;
    int inputSlot;
    double nextAt, stateSince, lastSeenAlive;
    int deployTries;
    /* movement */
    float pos[3], fwd[3];
    int goalCp; float goal[3]; int haveGoal; double goalChosenAt, holdUntil; float holdPoint[3]; int holding;
    float path[MAX_PATH_PTS * 3]; int pathLen, pathIdx; float pathGoal[3]; double pathAt; int pathPending;
    float progressPos[3]; double progressAt; int stuckCount; double recoverUntil; float recoverStrafe; int recoverBack;
    double steerAt; float steerYaw; int steerJump; double jumpAt;
    /* aim / combat */
    float yaw, pitch;               /* engine convention: yaw = -heading (0..2pi) */
    uint32_t target, targetId; double targetSince, lastLos, lastVisible; int visible;
    float aimErr[2]; double aimErrAt;
    double burstUntil, pauseUntil, reloadUntil, grenadeAt, meleeAt, strafeUntil; float strafe; float fireTime;
    double perceiveAt;
    float statusPos[3]; int statusPosValid;
    float stepPrev[3]; int stepPrevValid, fastTicks;
    float tryStart[3], tryTime;
    /* fireteam / following */
    int fireteam; uint32_t followP, followId; double followUntil;
    /* equipment: select a slot, use it, switch back */
    int eqState, eqSelect, eqBackAction; double eqAt, eqUseFor; float eqPitch; int eqPitchMode;
    double glAt, gadgetAt, medAt, reviveCheckAt, grenadeHold;
    uint32_t reviveP, reviveId; double reviveUntil;
    /* stuck handling */
    float pgBest; double pgAt; int knifeLeft; double knifeAt; float knifeHeading;
    float detour[3]; double detourUntil; int giveUps; double giveUpAt;
    int lspot, lastAct; double lspotAt, hintAt, lastShotAt; float lspotPos[3], lspotHeading; int owCounted;
    /* objective director assignment (AltBotMod bm_director port) */
    int aFlag, aRole; double aAssignedAt, aArrivedAt, aProgressAt; float aDist;
    int avoidCp; double avoidUntil; double zoneShiftAt;
    float via[3]; int viaOn; float owSpot[3]; int owOn; double owUntil, owNext, owScanAt; float owScan;
    /* vault / crouch / frozen recovery */
    double vaultUntil, vaultAt, crouchUntil; int crouched; float vaultHeading;
    float frozenPos[3]; double frozenSince, unpinUntil, unpinAt; int unpinTries, unpinKick, redeploy, skyCount; double skyCheckAt; float skyY; float unpinHeading;
    /* vehicles */
    uint32_t vehicle; int seat, vehRole; double vehSince, vehCheckAt, vehEnterUntil, vehProgressAt; float vehProgressDist;
    int hState; double hStateAt, fireUntil, shootAt; float hBias, hPrevY, hPrevPitch, hPrevRoll, hPrevHeading, hPrevPos[3], hAlt, hLand[3]; int hHaveLand, hFlag; double hLoiterUntil; float hOrbit;
    float tPush; int tStage, tAct, tCand, gunner, runOut, tBase, tVerOk, tTgtVeh, aimOwned, tLogged; float tBaseYaw, tBasePitch, tPrevRel, viewOffY, viewOffP; uint32_t tVerVeh, tTgtRef; float tVerRel0, tVerExp, tIy, tIp; double tAt, engageUntil, engageCool, hullUntil; float tD1, tPrevPos[3], tSpeed, tTarget[3];
    int hasAT, hasGL, atOn, atHeli, atGL; uint32_t atVeh; double atAt, atUntil, atPrevAt; float atPt[3], atPrev[3], atVel[3];
    double svAt, rushUntil, supplyAt; float pvPrevHeading, pvPrevErr, pvPrevThr, hNoTurn; double swimAt; uint32_t svRef; float svPrev[3], svVel[3];
    double exitBlockUntil, doorAt, avoidAt, wiggleUntil; int airHoldLogged, crewBoard, revCount, airOn, airLogged; double airAt, airSeenAt, tgtSeenAt; float airPt[3], airPrev[3], airVel[3], tgtPrev[3], tgtVel[3]; float stuckTime, avoidBias, frontClear, revSpot[3];
    uint32_t lastVeh; int exitFails; double vehZoneAt, vehWaitUntil, vehParkAt; float steerSign; int steerCalib; float calibStartHeading, calibStart[3]; double calibAt; double reverseUntil;
    /* 1.5: vehicle speed along the hull, escape manoeuvres, route requests from a fixed start, emplacements, Rush */
    float vPrev[3], vFwd, vFwdPrev, escHeading, pathFrom[3]; double vPrevAt, escUntil, escDriveUntil, thrOnSince, pathFailLogAt, emplUntil, emplNoTgtAt, armUntil; uint32_t vPrevVeh, repairVeh; double armStart, armBlockUntil, fenceLogAt, doorKnifeAt; float vTrail[8][3]; int vTrailN; uint32_t vTrailVeh; const float *inChain; int inIdx, inOn, inLogged; double inDbgAt, inIdxAt, inBanUntil; double swimSince, wpnCheckAt, wpnOffSince, armBestAt; float armBest; int escTries, rockets, mcom, armTries, newLife, boatLand; double repairCheckAt, repairUntil, boatAgroundAt;
    /* 1.6.6 */
    int stopKilled, pathPartial; double stopExitAt, rejoinAt, rejoinPlanAt; double noRouteSince, hazAt, hazCutAt, routeWaitSince; float hazSpot[3], pathGap;
    /* 1.6.9 */
    float swimBank[3], swimBest; double swimBestAt, swimJumpAt; int swimHave; double islandAt; float island[3]; double islandUntil;
    double boostLogAt, ppAt, pastEndSince; float ppScale, routeLeft; int pastEnd;   /* 1.6.10 */
    double heldUntil[BC2_EiaUndefined];
    float levels[BC2_EiaUndefined];
    int pulse[BC2_EiaUndefined];
} Bot;

static Bot g_bots[MAX_BOTS];
static unsigned char g_inputs[MAX_BOTS * 2][0x1c0] __attribute__((aligned(16)));
static unsigned char g_staging[MAX_BOTS][0xd0] __attribute__((aligned(16)));
static double g_nextCreate;
static int g_stopping;
static int g_keepOn, g_keepWins, g_keepFails;   /* 1.5: dead bots keep their player (KeepBotPlayers) */

static const char *kNames[] = {
    "Bob","Jack","Melanie","Joe","Ana","Mike","Sarah","Tom","Lisa","Dave","Emma","Chris","Kate","Steve","Nina","Paul",
    "Rosa","Mark","Julia","Sam","Olga","Ben","Maria","Alex","Zoe","Nick","Ivy","Pete","Lena","Frank","Hannah","Ivan",
    "Grace","Leo","Mia","Owen","Tara","Hank","Vera","Carl","Dana","Gus","Elena","Ray","Fiona","Kyle","Irina","Max"};

float rt_randf(void) { return (rt_rand() & 0xffffff) / 16777216.0f; }
static float randr(float a, float b) { return a + (b - a) * rt_randf(); }
static float wrap_pi(float a) { while (a > PI) a -= TWO_PI; while (a < -PI) a += TWO_PI; return a; }
static float dist2d(const float *a, const float *b) { return hypotf(a[0] - b[0], a[2] - b[2]); }
static float dist3(const float *a, const float *b) { float x = a[0]-b[0], y = a[1]-b[1], z = a[2]-b[2]; return sqrtf(x*x+y*y+z*z); }
/* engine yaw convention (verified from captured human input): heading = atan2(fx, fz) = -stateYaw */
static float heading_to(const float *from, const float *to) { return atan2f(to[0] - from[0], to[2] - from[2]); }
static float yaw_from_heading(float h) { float y = -h; while (y < 0) y += TWO_PI; while (y >= TWO_PI) y -= TWO_PI; return y; }
static float heading_from_yaw(float y) { return wrap_pi(-y); }

int bots_count(void) { int n = 0; for (int i = 0; i < MAX_BOTS; i++) if (g_bots[i].used) n++; return n; }

static const char *state_name(int s) { static const char *n[] = {"off", "waiting", "deploying", "alive"}; return s >= 0 && s < 4 ? n[s] : "?"; }
static const char *cls_name(int c) { static const char *n[] = {"assault", "engineer", "medic", "recon"}; return c >= 0 && c < 4 ? n[c] : "?"; }

/* ---------------------------------------------------------------- roster */
static int nteams(void) { return g_world.teamCount >= 4 ? 4 : 2; }
static int bots_on_team(int team) { int n = 0; for (int i = 0; i < MAX_BOTS; i++) if (g_bots[i].used && g_bots[i].team == team) n++; return n; }
static int wanted_on_team(int team)
{
    if (team > nteams()) return 0;
    if (g_world.mode == MODE_SQDM && nteams() == 4) {   /* Squad Deathmatch: four squads of four */
        int per = g_cfg.botsPerTeam < 4 ? g_cfg.botsPerTeam : 4, w = per - g_world.humans[team]; return w < 0 ? 0 : w; }
    if (g_cfg.fillToPerTeam > 0) { int w = g_cfg.fillToPerTeam - g_world.humans[team]; return w < 0 ? 0 : w; }
    return g_cfg.botsPerTeam;
}
static void roster_balance(double now)
{
    for (int team = 1; team <= 4; team++) {
        int want = wanted_on_team(team), have = bots_on_team(team);
        if (have < want) {
            for (int i = 0; i < MAX_BOTS; i++) if (!g_bots[i].used) {
                Bot *b = &g_bots[i]; memset(b, 0, sizeof *b);
                b->used = 1; b->team = team; b->state = BS_WAIT; b->nextAt = now + 0.5 + 0.35 * i; b->goalCp = -1;
                b->aFlag = -1; b->avoidCp = -1; b->aDist = 1e30f; b->steerSign = 1.0f; b->lspot = -1; b->lspot = -1;
                b->cls = (int)(rt_rand() % CLS_COUNT);
                b->fireteam = (bots_on_team(team) - 1) / 4;   /* fireteams of four */
                snprintf(b->name, sizeof b->name, "%s", kNames[i % (int)(sizeof kNames / sizeof kNames[0])]);
                rt_log("roster: %s joins team %d as %s", b->name, team, cls_name(b->cls));
                break;
            }
        } else if (have > want) {
            /* retire a waiting bot first; living ones retire when they die */
            for (int i = MAX_BOTS - 1; i >= 0; i--) if (g_bots[i].used && g_bots[i].team == team && g_bots[i].state == BS_WAIT) {
                rt_log("roster: %s leaves team %d", g_bots[i].name, team); g_bots[i].used = 0; break;
            }
        }
    }
}

/* ---------------------------------------------------------------- native life cycle */
static void input_neutral_push(Bot *b, float dt)
{
    void *input = g_inputs[b->inputSlot], *st = g_staging[b - g_bots];
    for (int a = 0; a < BC2_EiaUndefined; a++) eng_state_set_level(st, a, 0);
    eng_state_set_dt(st, dt > 0 && dt < 0.2f ? dt : 1.0f / 30);
    uint32_t tick = eng_game_tick(); memcpy((unsigned char *)st + ST_TICK, &tick, 4);
    eng_input_push(input, st);
}

extern int phx_observe_now(BC2Observation *o);
/* 1.6.6: does this bot's player still control a living soldier on foot? (leaveGame would leave it behind) */
static uint32_t living_soldier(const Bot *b)
{
    uint32_t sol = 0, ctl = 0; float hp = 0;
    if (!b->player || !eng_u32(b->player + PL_SOLDIER, &sol) || !eng_ptr(sol) || !eng_u32(b->player + PL_CONTROLLED, &ctl)) return 0;
    if (ctl && ctl != sol && eng_vehicle_seats(ctl) > 0) return 0;   /* in a vehicle */
    return eng_read(sol + 0x34, &hp, 4) && hp > 0 ? sol : 0;
}
static void remove_player(Bot *b, const char *why)
{
    int idx = world_find_player(b->player, b->id);
    if (idx >= 0 && g_world.obs.players[idx].isAI && g_world.manager) {
        if (living_soldier(b)) rt_log("bot %s: removed while its soldier lives - it may stay behind until the round ends", b->name);
        uint8_t inGame = 0; eng_read(b->player + PL_INGAME, &inGame, 1);
        if (inGame) eng_leave_game(b->player);
        /* leaveGame may already have removed it */
        BC2Observation o; int still = 0;
        extern int phx_observe_now(BC2Observation *o);
        if (phx_observe_now(&o)) for (int i = 0; i < o.count; i++) if (o.players[i].pointer == b->player && o.players[i].id == b->id) still = 1;
        if (still) {
            uint32_t ctl = 0; eng_u32(b->player + PL_SOLDIER, &ctl);
            if (!ctl) eng_delete_player(g_world.manager, b->player);
            else rt_log("bot %s: player still controls a soldier, deletion skipped", b->name);
        }
        rt_log("bot %s: removed (%s)", b->name, why);
    }
    b->player = b->id = b->soldier = 0;
}

/* one random selectable weapon per slot; ones with a resource id go through selectKit's pairs, the rest are
   appended to the player's selected list directly (most BC2 kit weapons have no resource id) */
static int pick_unlocks(const KitInfo *kit, PhxPair *pairs, int max, const UnlockChoice **picked, int *npicked)
{
    int n = 0; *npicked = 0;
    for (uint32_t slot = 0; slot <= 16 && n < max; slot++) {
        int cand[MAX_UNLOCK_CHOICES], c = 0;
        for (int k = 0; k < kit->choiceCount; k++) if (kit->choices[k].slot == slot) cand[c++] = k;
        if (!c) continue;
        /* weighted: shotguns are shared by every kit and would otherwise be a third of all primaries (with every
           unlock on, the kit's own default for the slot is the last one - the USAS-12 - so the pick matters) */
        float w[MAX_UNLOCK_CHOICES], tot = 0;
        for (int k = 0; k < c; k++) {
            const char *nm = kit->choices[cand[k]].name;
            w[k] = (strstr(nm, "_shg_") || strstr(nm, "USAS") || strstr(nm, "Saiga") || strstr(nm, "Neostead") || strstr(nm, "870")) ? 0.06f : 1.0f;
            /* the slot-2 gadget is fired as a launcher: no AT mines there; the tracer dart is no sidearm */
            if (strstr(nm, "Mine") || strstr(nm, "_Min") || strstr(nm, "TracerDart")) w[k] = 0.001f;
            /* assault: the explosive 40 mm, not the buckshot or smoke versions (they do nothing to a tank) */
            if (strstr(nm, "_shg_XM320") || strstr(nm, "_smk_") || strstr(nm, "_shg_F2000") || strstr(nm, "_shg_M16") || strstr(nm, "_shg_AEK")) w[k] = 0.15f;
            tot += w[k];
        }
        /* only mines for the gadget slot (the Vietnam engineer kits list just NAM_At_mine there): pick nothing, so the kit's
           own launcher (RPG-7 / M72 LAW) stays - 1.4 gave every Vietnam engineer mines and no launcher */
        if (slot == 2) { int any = 0; for (int k = 0; k < c; k++) if (w[k] > 0.01f) any = 1; if (!any) continue; }
        float r = rt_randf() * tot; int pickK = c - 1;
        for (int k = 0; k < c; k++) { if (r < w[k]) { pickK = k; break; } r -= w[k]; }
        const UnlockChoice *u = &kit->choices[cand[pickK]];
        picked[(*npicked)++] = u;
        if (u->resourceId) { pairs[n].category = u->slot; pairs[n].resourceId = u->resourceId; n++; }
    }
    return n;
}

static int apply_kit(Bot *b)
{
    const KitInfo *kit = world_kit_for(b->team, b->cls);
    if (!kit) return 0;
    /* the kit must still be the loaded asset (a level change frees it) */
    uint32_t vt = 0;
    if (eng_resource_by_id(kit->resourceId) != kit->data || !eng_u32(kit->data, &vt) || vt != RB(VA_SOLDIER_DATA_VT)) {
        rt_log("kits: cached kit '%s' is stale - reloading the team kits", kit->name);
        g_world.kitCount[1] = g_world.kitCount[2] = 0;
        return 0;
    }
    static PhxPair pairs[17]; const UnlockChoice *picked[17]; int npicked = 0;
    int n = pick_unlocks(kit, pairs, 17, picked, &npicked);
    /* engineers carry a launcher in slot 2 (Carl Gustaf, M136, RPG-7, M72 LAW) - the kit's own when none was picked;
       assault soldiers a 40 mm (XM320 / GP-30 / M79) unless the buckshot or smoke version was picked */
    b->hasAT = kit->cls == CLS_ENGINEER; b->hasGL = kit->cls == CLS_ASSAULT;
    for (int i = 0; i < npicked; i++) if (picked[i]->slot == 2) {
        const char *nm = picked[i]->name;
        if (strstr(nm, "Mine") || strstr(nm, "_Min")) b->hasAT = 0;
        if (strstr(nm, "_shg_") || strstr(nm, "_smk_")) b->hasGL = 0;
    }
    PhxPairVector v = {pairs, pairs + n, pairs + n};
    eng_ready(b->player);
    eng_select_unlocks(b->player, &v);
    eng_select_kit(b->player, kit->resourceId, &v, g_world.humanKitFlagsKnown ? g_world.humanKitFlags : 0x00010810u);
    uint32_t sel = 0;
    if (!eng_u32(b->player + PL_SOLDIER_DAT, &sel) || sel != kit->data) {
        rt_log("bot %s: kit '%s' was not applied (data 0x%08lx)", b->name, kit->name, (unsigned long)sel);
        return 0;
    }
    b->cls = kit->cls;
    int direct = 0;
    /* the soldier's weapon setup matches each selected unlock against the kit entry's weapon DATA
       (SoldierWeaponAsset +0x50, a SoldierWeaponData) - 0.8.0 appended the asset itself, nothing matched and every
       bot kept the last default of its kit (the USAS-12) */
    for (int i = 0; i < npicked; i++) {
        uint32_t data = 0;
        if (picked[i]->resourceId) continue;   /* went through selectKit's pairs */
        if (eng_u32(picked[i]->weapon + 0x50, &data) && eng_ptr(data) && eng_player_add_weapon(b->player, data)) direct++;
    }
    static int loadoutLogs;
    if (g_cfg.verbose || loadoutLogs < 12) {
        loadoutLogs++;
        char list[200]; int used = 0; list[0] = 0;
        for (int i = 0; i < npicked && used < 180; i++) { const char *nm = strrchr(picked[i]->name, '/'); nm = nm ? nm + 1 : picked[i]->name;
            used += snprintf(list + used, sizeof list - used, " %u:%s", picked[i]->slot, nm); }
        rt_log("bot %s: kit %s (%s) weapons:%s (%d given directly, selected list %d)", b->name, kit->name, cls_name(kit->cls), list, direct, eng_player_weapon_count(b->player));
    }
    return 1;
}

/* scoreboard hunt: the player fields the server sends to clients (ghost bits) - logged for a bot and for the human */
static void ghost_fields(uint32_t q, const char *who)
{
    if (!q) return;
    uint32_t a8 = 0, ac = 0, f140 = 0, f144 = 0, f148 = 0, f14c = 0, dc8 = 0, c1c[2] = {0, 0}, id18[2] = {0, 0}, idb0[2] = {0, 0}; uint8_t bc8 = 0, c28 = 0, cac[4] = {0}, d84 = 0;
    eng_u32(q + 0xa8, &a8); eng_u32(q + 0xac, &ac); eng_u32(q + 0x140, &f140); eng_u32(q + 0x144, &f144); eng_u32(q + 0x148, &f148);
    eng_u32(q + 0x14c, &f14c); eng_u32(q + 0xdc8, &dc8); eng_read(q + 0xc1c, c1c, 8); eng_read(q + 0xbc8, &bc8, 1); eng_read(q + 0xc28, &c28, 1);
    eng_read(q + 0xcac, cac, 4); eng_read(q + 0xd84, &d84, 1); eng_read(q + 0x18, id18, 8); eng_read(q + 0xb0, idb0, 8);
    rt_log("players: ghost fields %s: team %u +ac %08x +140 %08x +144 %08x +148 %08x +14c %08x flags+bc8 %02x +c28 %02x +cac %02x%02x%02x%02x +c1c %08x/%08x +dc8 %08x ai+d84 %u id+18 %08x%08x id+b0 %08x%08x",
           who, a8, ac, f140, f144, f148, f14c, bc8, c28, cac[0], cac[1], cac[2], cac[3], c1c[0], c1c[1], dc8, d84, id18[1], id18[0], idb0[1], idb0[0]);
}
static int create_life(Bot *b, double now)
{
    uint32_t m = g_world.manager;
    if (!m || !g_world.obsReady) return 0;
    if (g_world.obs.count >= g_world.obs.maxPlayers - 1) { rt_log("bot %s: server is full", b->name); return 0; }
    if (!world_kit_for(b->team, b->cls)) return 0;
    /* choose this life's class and input buffer */
    if (b->lives > 0 && rt_randf() < 0.6f) b->cls = (int)(rt_rand() % CLS_COUNT);
    b->inputSlot = (int)(b - g_bots) * 2 + (b->lives & 1);
    uint32_t p = eng_allocate_player(m), vt = 0, id = 0;
    if (!p) { rt_log("bot %s: allocator returned no player", b->name); return 0; }
    if (!eng_u32(p, &vt) || vt != RB(VA_PLAYER_VT) || !eng_u32(p + PL_ID, &id) || id >= 64) {
        rt_log("bot %s: unexpected player layout 0x%08lx - bots stopped", b->name, (unsigned long)p); g_stopping = 1; return 0;
    }
    b->player = p; b->id = id;
    /* every life is a new player: setTeam on it broadcast "<bot> switched teams" each respawn (1.3). The team is set
       before the player is registered, so writing it is enough - the client gets it with the new player. */
    if (g_cfg.quietTeam) eng_write32(p + PL_TEAM, (uint32_t)b->team); else eng_set_team(p, (uint32_t)b->team);
    eng_set_ai(p, 1);
    void *input = g_inputs[b->inputSlot];
    memset(input, 0, 0x1c0);
    eng_input_ctor(input);
    memcpy(g_staging[b - g_bots], input, 0xd0);
    eng_input_attach(p, input);
    static char names[MAX_BOTS][32];
    char *nm = names[b - g_bots]; snprintf(nm, 32, "%s", b->name);
    PhxName name = {nm, nm + strlen(nm), nm + strlen(nm) + 1};
    eng_register_player(m, p, &name);
    /* give the bot the unlocks a fully ranked player has, so kit weapons/gadgets are not filtered out */
    if (g_cfg.unlockAll) {
        static int logged; int bits = 0;
        int ok = eng_player_unlock_all(p, &bits);
        if (logged < 1 || !ok) {
            logged++;
            unsigned hs = 0, ht = 0; int human = -1;
            for (int i = 0; i < g_world.obs.count; i++) if (!g_world.obs.players[i].isAI) { human = i; break; }
            if (human >= 0) eng_player_unlock_bits(g_world.obs.players[human].pointer, &hs, &ht);
            rt_log("unlocks: %s %s (%d unlock bits)%s", b->name, ok ? "has every unlock" : "has no unlock bitset - default weapons only", bits,
                   human >= 0 ? "" : "");
            if (human >= 0) rt_log("unlocks: human player has %u of %u unlock bits", hs, ht);
        }
    }
    {   /* scoreboard check: registerPlayer puts the player in the manager's ghosted player table (manager+0x70, 0x1C per
           id, player at +0x14) and its name (player+0xC2C) is sent with it - log what a client receives */
        static int diag;
        if (diag < 2) {
            diag++;
            uint32_t tbl = 0, slot = 0; char nm[40] = "";
            eng_u32(m + 0x70, &tbl); if (eng_ptr(tbl)) eng_u32(tbl + id * 0x1c + 0x14, &slot);
            eng_string(p + 0xc2c, nm, sizeof nm);
            rt_log("players: %s registered as id %lu, network name '%s', in the ghosted player table: %s", b->name, (unsigned long)id, nm, slot == p ? "yes" : "no");
            for (int i = 0; i < g_world.obs.count; i++) if (!g_world.obs.players[i].isAI) {
                char d[200]; eng_describe_online_id(g_world.obs.players[i].pointer, d, sizeof d); rt_log("players: human online identity %s", d); break; }
        }
        /* a scoreboard entry needs an online identity (humans get theirs from their connection) */
        if (g_cfg.scoreboard) {
            int ok;
            if (g_cfg.scoreboard >= 2) {
                /* 1.3.1: the persona of the bot's backend account (backend\Phoenix-bot-accounts.sql: user_id 9001 + name
                   index), set the way a human's login sets it - +0x18 (setOnlineId) and the ghosted +0xB0. A human's +0x18
                   holds its account id (e.g. 32); 1.0-1.3 left the bots' +0x18 empty. */
                int ni = 0; for (int k = 0; k < (int)(sizeof kNames / sizeof *kNames); k++) if (!strcmp(kNames[k], b->name)) { ni = k; break; }
                uint32_t pid = 9001u + (uint32_t)ni;
                eng_set_online_id(p, pid, 0, b->name);
                ok = eng_set_net_online_id(p, pid, 0, b->name);
            } else ok = eng_set_net_online_id(p, 0x50000000u + (uint32_t)(b - g_bots) * 64 + (uint32_t)(b->lives & 63), 0x1, b->name);
            if (diag <= 2) { diag++; char d[200]; eng_describe_online_id(p, d, sizeof d); rt_log("players: %s given an online identity (%s) - %s", b->name, ok ? "ok" : "not stored", d);
                ghost_fields(p, b->name);
            }
        }
    }
    if (g_cfg.realSquads && b->team >= 1 && b->team <= 2) {
        uint32_t squad = (uint32_t)(b->fireteam + 1) + (b->team == 2 ? 0 : 0);
        if (squad < 24) eng_set_squad(p, squad);
    }
    if (!apply_kit(b)) { remove_player(b, "kit selection failed"); return 0; }
    b->lives++; b->state = BS_DEPLOYING; b->stateSince = now; b->deployTries = 0; b->nextAt = now + 0.1;
    return 1;
}

static double g_spawnBusyUntil[MAX_SPAWNS];
static uint32_t g_spawnBusyEntity[MAX_SPAWNS];
static int spawn_busy(int i, double now) { return g_spawnBusyEntity[i] == g_world.spawns[i].entity && now < g_spawnBusyUntil[i]; }
static int g_lastSpawnIndex = -1;

/* BC2 asserts ("AI already spawned from the spawnPoint, not allowed due to load/save game") when a spawn
   point's last soldier (weak reference at +0x9C) is a living AI. Clear that reference first - it only
   serves single-player save games. */
static void spawn_release_ai(uint32_t spawn)
{
    uint32_t ref = 0, holder = 0, player = 0; uint8_t ai = 0;
    if (!eng_u32(spawn + 0x9c, &ref) || !ref || !eng_u32(ref, &holder) || !holder || holder < 4) return;
    uint32_t soldier = holder - 4;
    if (!eng_u32(soldier + 0x240, &player) || !eng_ptr(player) || !eng_read(player + PL_AI, &ai, 1) || !ai) return;
    static int logged;
    if (eng_write32(spawn + 0x9c, 0) && logged < 5) { logged++; rt_log("spawn point %08x: released the previous bot's claim so the next bot can use it", spawn); }
}

static int team_spawn_near(int team, const float *to, float *out);
static int crew_spawn(Bot *b, const int *cand, int n, const float *to);
static uint32_t choose_spawn(Bot *b)
{
    int cand[MAX_SPAWNS], n = 0; double now = rt_now();
    for (int i = 0; i < g_world.spawnCount; i++)
        if (g_world.spawns[i].enabled && g_world.spawns[i].team == b->team && g_world.spawns[i].id && !spawn_busy(i, now)) cand[n++] = i;
    if (!n) return 0;   /* never pass 0: the engine's own pick asserts when the team has no spawn */
    /* deploy at the team spawn nearest the bot's assigned flag (the director runs for bots that are
       waiting too); without an assignment, nearest the closest flag the team does not own */
    const float *to = NULL;
    if (b->aFlag >= 0 && b->aFlag < g_world.cpCount) to = g_world.cps[b->aFlag].pos;
    else {
        float c[3], bd = 1e30f;
        if (team_spawn_near(b->team, NULL, c))
            for (int f = 0; f < g_world.cpCount; f++) { CapturePoint *cp = &g_world.cps[f];
                if (cp->locked || cp->owner == b->team) continue;
                float d = dist2d(c, cp->pos); if (d < bd) { bd = d; to = cp->pos; } }
    }
    if (to) { int k = crew_spawn(b, cand, n, to); if (k >= 0) { g_lastSpawnIndex = k; return g_world.spawns[k].id; } }
    if (to) {
        int best = -1; float bestScore = 1e30f;
        for (int k = 0; k < n; k++) {
            SpawnPoint *s = &g_world.spawns[cand[k]]; if (!s->posValid) continue;
            float d = dist2d(s->pos, to) + randr(0, 25);
            if (d < bestScore) { bestScore = d; best = cand[k]; }
        }
        if (best >= 0) { g_lastSpawnIndex = best; return g_world.spawns[best].id; }
    }
    int k = cand[rt_rand() % (unsigned)n];
    g_lastSpawnIndex = k;
    return g_world.spawns[k].id;
}

/* ---------------------------------------------------------------- perception & combat */
static int los_clear(const float *eye, const float *target, float targetRadius)
{
    float dir[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    float len = sqrtf(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
    if (len < 0.5f) return 1;
    for (int k = 0; k < 3; k++) dir[k] /= len;
    float a[3] = {eye[0] + dir[0] * 0.6f, eye[1] + dir[1] * 0.6f, eye[2] + dir[2] * 0.6f}, h[3], n[3];
    if (!eng_ray(a, target, h, n)) return 1;
    return dist3(h, target) < targetRadius;   /* the ray stopped at (or on) the target itself */
}

static int friendly_in_line(Bot *b, const float *eye, const float *target)
{
    float d[3] = {target[0]-eye[0], target[1]-eye[1], target[2]-eye[2]}; float len = sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
    if (len < 1) return 0;
    for (int i = 0; i < g_world.obs.count; i++) {
        BC2ObservedPlayer *p = &g_world.obs.players[i];
        if ((int)p->team != b->team || p->pointer == b->player || !p->soldierValid || !p->transformValid) continue;
        float c[3] = {p->position[0], p->position[1] + 1.1f, p->position[2]};
        float t = ((c[0]-eye[0])*d[0] + (c[1]-eye[1])*d[1] + (c[2]-eye[2])*d[2]) / (len*len);
        if (t <= 0 || t >= 1) continue;
        float q[3] = {eye[0] + d[0]*t - c[0], eye[1] + d[1]*t - c[1], eye[2] + d[2]*t - c[2]};
        if (q[0]*q[0] + q[1]*q[1] + q[2]*q[2] < 1.0f) return 1;
    }
    return 0;
}

static float class_range(int cls) { return cls == CLS_RECON ? 300.0f : cls == CLS_MEDIC ? 165.0f : 145.0f; }

static void perceive(Bot *b, double now, const BC2ObservedPlayer *self)
{
    float eye[3] = {self->position[0], self->position[1] + 1.55f, self->position[2]};
    float range = class_range(b->cls);
    /* keep the current target while it stays valid */
    int cur = b->target ? world_find_player(b->target, b->targetId) : -1;
    if (cur >= 0) {
        BC2ObservedPlayer *t = &g_world.obs.players[cur];
        if (!t->soldierValid || !t->transformValid || t->health <= 0 || (int)t->team == b->team || dist3(t->position, self->position) > range * 1.2f) cur = -1;
    }
    if (cur < 0) { b->target = 0; b->visible = 0; }
    /* candidates by distance; LOS checks are rationed */
    int order[64], n = 0; float dd[64];
    for (int i = 0; i < g_world.obs.count && n < 64; i++) {
        BC2ObservedPlayer *t = &g_world.obs.players[i];
        if (t->team < 1 || (int)t->team > nteams() || (int)t->team == b->team || !t->soldierValid || !t->transformValid || t->health <= 0 || t->entry) continue;
        float d = dist3(t->position, self->position); if (d > range) continue;
        /* field of view: wide, plus hearing nearby */
        float h = heading_to(self->position, t->position), fh = heading_from_yaw(b->yaw);
        if (fabsf(wrap_pi(h - fh)) > 2.1f && d > 30) continue;   /* wide awareness, hears footsteps and fire within 30 m */
        int k = n++; while (k > 0 && dd[k-1] > d) { order[k] = order[k-1]; dd[k] = dd[k-1]; k--; } order[k] = i; dd[k] = d;
    }
    int checks = 0;
    if (b->target) {
        BC2ObservedPlayer *t = &g_world.obs.players[cur];
        float chest[3] = {t->position[0], t->position[1] + 1.2f, t->position[2]};
        b->visible = los_clear(eye, chest, 1.0f); checks++;
        if (b->visible) b->lastVisible = now;
        else if (now - b->lastVisible > 2.5) { b->target = 0; }
    }
    for (int k = 0; k < n && checks < 3; k++) {
        BC2ObservedPlayer *t = &g_world.obs.players[order[k]];
        if (t->pointer == b->target) continue;
        if (b->target && b->visible && dd[k] > dist3(self->position, g_world.obs.players[cur].position) * 0.6f) break;
        float chest[3] = {t->position[0], t->position[1] + 1.2f, t->position[2]};
        checks++;
        if (los_clear(eye, chest, 1.0f)) {
            if (b->target != t->pointer) {
                b->target = t->pointer; b->targetId = t->id; b->targetSince = now; b->visible = 1; b->lastVisible = now;
                b->aimErrAt = 0;
            }
            break;
        }
    }
}

static float skill_error(void) { return (g_cfg.difficulty <= 1 ? 0.05f : g_cfg.difficulty == 2 ? 0.026f : 0.014f) * g_cfg.aimSkill; }   /* 1.2: tighter */
static float reaction_time(void) { return g_cfg.difficulty <= 1 ? 0.5f : g_cfg.difficulty == 2 ? 0.3f : 0.18f; }

static void press(Bot *b, int action) { b->pulse[action] = 2; }

/* ---------------------------------------------------------------- objectives
   Team objective director, ported from AltBotMod V20 (director_policy.h, bm_director_update) and adapted
   to BC2 Conquest. Flags come first:
   - every second each team's bots get a job: ATTACK a flag the team does not own, DEFEND an owned flag
     that is being taken, GUARD a front-line flag (a small share only), or PATROL when the team owns all;
   - attacks go to the flags nearest the team's spawns first, in groups (GroupSize), and roll forward to
     the next flags as flags fall; a flag the team just lost is retaken first;
   - an attack stays committed while the bot gets closer (45 s) and while it is capturing; a bot that sat
     on a flag 25 s without capturing, or got stuck on the way, is sent elsewhere;
   - a captured flag releases its attackers at once, so nobody idles on a flag or at the deployment. */
enum { OBJ_NONE, OBJ_ATTACK, OBJ_GUARD, OBJ_PATROL, OBJ_DEFEND };
static const char *role_name(int r) { static const char *n[] = {"none", "attack", "guard", "patrol", "defend"}; return r >= 0 && r < 5 ? n[r] : "?"; }
static double g_directorAt;
static uint32_t g_cpEntity[MAX_CPS]; static int g_cpPrevOwner[MAX_CPS]; static double g_cpLostAt[3][MAX_CPS];
static int g_cpEnemyNear[3][MAX_CPS];   /* [team][flag]: enemies of that team near the flag */
static float g_cpProg[MAX_CPS]; static double g_cpProgAt[MAX_CPS];   /* capture bar and when it last moved */
static float g_cpReach[MAX_CPS]; static double g_cpIdleSince[MAX_CPS];

static int cp_capturable(const CapturePoint *cp) { return !cp->locked; }   /* locked = the two main bases */
static int cp_recent_loss(int team, int f, double now) { return now - g_cpLostAt[team][f] < 120; }

static void assign(Bot *b, int f, int role, double now)
{
    if (b->aFlag != f || b->aRole != role) {
        if (g_cfg.verbose && f >= 0) rt_log("director: %s t%d -> %s %s", b->name, b->team, role_name(role), g_world.cps[f].name);
        b->aFlag = f; b->aRole = role; b->aAssignedAt = now; b->aArrivedAt = 0; b->aProgressAt = now; b->aDist = 1e30f;
    }
}

/* where a bot that is not on the field will appear: the team spawn nearest its flag, else the team's centre */
static int team_spawn_near(int team, const float *to, float *out)
{
    float best = 1e30f; int found = 0, any = 0; float sum[3] = {0, 0, 0};
    for (int i = 0; i < g_world.spawnCount; i++) {
        SpawnPoint *s = &g_world.spawns[i];
        if (!s->enabled || s->team != team || !s->posValid) continue;
        sum[0] += s->pos[0]; sum[1] += s->pos[1]; sum[2] += s->pos[2]; any++;
        if (to) { float d = dist2d(s->pos, to); if (d < best) { best = d; memcpy(out, s->pos, 12); found = 1; } }
    }
    if (!found && any) { out[0] = sum[0] / any; out[1] = sum[1] / any; out[2] = sum[2] / any; found = 1; }
    return found;
}

static void director_update(double now)
{
    int nf = g_world.cpCount < MAX_CPS ? g_world.cpCount : MAX_CPS;
    /* ownership history: who lost what, when */
    for (int f = 0; f < nf; f++) {
        CapturePoint *cp = &g_world.cps[f];
        if (g_cpEntity[f] != cp->entity) { g_cpEntity[f] = cp->entity; g_cpPrevOwner[f] = cp->owner; g_cpLostAt[1][f] = g_cpLostAt[2][f] = -1e9; g_cpReach[f] = 0; g_cpIdleSince[f] = 0; }
        else if (cp->owner != g_cpPrevOwner[f]) {
            int was = g_cpPrevOwner[f];
            if (was >= 1 && was <= 2) g_cpLostAt[was][f] = now;
            rt_log("flags: %s is now %s", cp->name, cp->owner ? (cp->owner == 1 ? "team 1's" : "team 2's") : "neutral");
            g_cpPrevOwner[f] = cp->owner;
        }
    }
    for (int f = 0; f < nf; f++) {
        float pr = g_world.cps[f].progress;
        if (fabsf(pr - g_cpProg[f]) > 0.001f) { g_cpProg[f] = pr; g_cpProgAt[f] = now; }
    }
    if (now < g_directorAt || !nf) return;
    g_directorAt = now + 1.0;
    memset(g_cpEnemyNear, 0, sizeof g_cpEnemyNear);
    for (int i = 0; i < g_world.obs.count; i++) {
        BC2ObservedPlayer *p = &g_world.obs.players[i];
        if (p->team < 1 || p->team > 2 || !p->soldierValid || !p->transformValid || p->health <= 0) continue;
        for (int f = 0; f < nf; f++) {
            if (dist2d(p->position, g_world.cps[f].pos) < g_world.cps[f].radius + 30) g_cpEnemyNear[3 - p->team][f]++;
            /* Rush: an attacker at the M-COM (it may be arming it - the defenders go to defuse) */
            if (g_world.cps[f].isObj && (int)p->team != g_world.cps[f].owner && dist2d(p->position, g_world.cps[f].pos) < 5) g_world.cps[f].enemyNearAt = now;
        }
    }
    for (int team = 1; team <= 2; team++) {
        int units[MAX_BOTS], nu = 0; float upos[MAX_BOTS][3]; int alive[MAX_BOTS];
        for (int i = 0; i < MAX_BOTS; i++) {
            Bot *b = &g_bots[i];
            if (!b->used || b->team != team) continue;
            int k = nu++; units[k] = i; alive[k] = b->state == BS_ALIVE;
            if (alive[k]) memcpy(upos[k], b->pos, 12);
            else if (!team_spawn_near(team, b->aFlag >= 0 && b->aFlag < nf ? g_world.cps[b->aFlag].pos : NULL, upos[k])) memcpy(upos[k], b->pos, 12);
            if (b->avoidCp >= 0 && now > b->avoidUntil) b->avoidCp = -1;
        }
        if (!nu) continue;
        /* the team's hostile flags, nearest to its spawns (the front) first */
        int hostile[MAX_CPS], nh = 0; float front[MAX_CPS];
        for (int f = 0; f < nf; f++) {
            CapturePoint *cp = &g_world.cps[f];
            float sp[3]; front[f] = team_spawn_near(team, cp->pos, sp) ? dist2d(sp, cp->pos) : 0;
            if (cp_capturable(cp) && cp->owner != team) {
                float r = front[f] - (cp_recent_loss(team, f, now) ? 150.0f : 0) - (cp->owner == 0 ? 40.0f : 0);
                int k = nh++; while (k > 0 && front[hostile[k-1]] - (cp_recent_loss(team, hostile[k-1], now) ? 150.0f : 0) - (g_world.cps[hostile[k-1]].owner == 0 ? 40.0f : 0) > r) { hostile[k] = hostile[k-1]; k--; }
                hostile[k] = f;
            }
        }
        int groupSize = g_cfg.groupSize > 0 ? g_cfg.groupSize : 4;
        int groups = (nu + groupSize - 1) / groupSize; if (groups < 1) groups = 1; if (groups > nh) groups = nh;
        int active[MAX_CPS] = {0}; for (int k = 0; k < groups; k++) active[hostile[k]] = 1;
        int counts[MAX_CPS] = {0}, guards[MAX_CPS] = {0}; unsigned char used[MAX_BOTS] = {0};
        int share = groups ? (nu + groups - 1) / groups + 1 : nu;

        /* 1. defend owned flags that are being taken: nearest bots, about one more than the attackers */
        for (int f = 0; f < nf; f++) {
            CapturePoint *cp = &g_world.cps[f];
            int armedObj = cp->isObj && cp->armed;   /* Rush: an armed M-COM pulls defenders in to defuse it */
            if (!cp_capturable(cp) || cp->owner != team || !((cp->attacker && cp->attacker != team) || g_cpEnemyNear[team][f] || armedObj)) continue;
            /* 0.7.2 pulled up to 6 bots from 160 m - "everyone ran back". Now 2-4 of the nearest, within 90 m. */
            int want = g_cpEnemyNear[team][f] + 1; if (want < 2) want = 2; if (want > 4) want = 4; if (armedObj && want < 6) want = 6; if (want > nu - (nh ? 1 : 0)) want = nu - (nh ? 1 : 0);
            for (int k = 0; k < nu && guards[f] < want; k++) { Bot *b = &g_bots[units[k]];
                if (!used[k] && b->aFlag == f && (b->aRole == OBJ_DEFEND || b->aRole == OBJ_GUARD) && b->avoidCp != f) { used[k] = 1; guards[f]++; counts[f]++; if (b->aRole != OBJ_DEFEND) assign(b, f, OBJ_DEFEND, now); } }
            while (guards[f] < want) {
                int best = -1; float bd = armedObj ? 400 : 90;
                for (int k = 0; k < nu; k++) { Bot *b = &g_bots[units[k]];
                    if (used[k] || !alive[k] || b->avoidCp == f) continue;
                    float d = dist2d(upos[k], cp->pos); if (d < bd) { bd = d; best = k; } }
                if (best < 0) break;
                assign(&g_bots[units[best]], f, OBJ_DEFEND, now); used[best] = 1; guards[f]++; counts[f]++;
            }
        }
        /* 2. a few guards on front-line flags, rotated every 90 s. GuardEvery=0 (auto): a share that grows with
           the flags held - one bot in nine per owned flag (10 bots, 2 of 3 flags held: 2 guards). */
        int owned = 0, capt = 0;
        for (int f = 0; f < nf; f++) if (cp_capturable(&g_world.cps[f])) { capt++; if (g_world.cps[f].owner == team) owned++; }
        int budget = g_cfg.guardEvery > 0 ? nu / g_cfg.guardEvery : g_cfg.guardEvery < 0 || !capt ? 0 : (nu * owned) / (capt * 3);
        if (nh && budget > nu - 1) budget = nu - 1;
        /* Rush defenders (1.5): everyone guards the M-COMs, spread evenly */
        int rushDef = !nh && nf && g_world.cps[0].isObj && g_world.cps[0].owner == team, gcap = rushDef ? (nu + nf - 1) / nf + 1 : 2;
        if (rushDef) budget = nu;
        for (int k = 0; k < nu && budget > 0; k++) { Bot *b = &g_bots[units[k]]; int f = b->aFlag;
            if (used[k] || b->aRole != OBJ_GUARD || f < 0 || f >= nf || g_world.cps[f].owner != team || b->avoidCp == f || guards[f] >= gcap || now - b->aAssignedAt > (rushDef ? 240 : 90)) continue;
            used[k] = 1; guards[f]++; counts[f]++; budget--; }
        while (budget > 0 && (nh || rushDef)) {
            int bk = -1, bf = -1; float score = 1e30f;
            for (int f = 0; f < nf; f++) {
                CapturePoint *cp = &g_world.cps[f];
                if (!cp_capturable(cp) || cp->owner != team || guards[f] >= gcap) continue;
                float toHostile = 1e30f; for (int h = 0; h < nh; h++) toHostile = fminf(toHostile, dist2d(cp->pos, g_world.cps[hostile[h]].pos));
                if (toHostile > 260 && !rushDef) continue;   /* only the front line */
                for (int k = 0; k < nu; k++) { Bot *b = &g_bots[units[k]];
                    if (used[k] || !alive[k] || b->avoidCp == f || (b->aRole == OBJ_GUARD && b->aFlag == f)) continue;   /* a released guard moves on */
                    float d = dist2d(upos[k], cp->pos); if (d > 110 && !rushDef) continue;
                    float v = rushDef ? d + guards[f] * 300.0f : d - (guards[f] ? 250 : 0); if (v < score) { score = v; bk = k; bf = f; } }
            }
            if (bk < 0) break;
            assign(&g_bots[units[bk]], bf, OBJ_GUARD, now); used[bk] = 1; guards[bf]++; counts[bf]++; budget--;
        }
        /* 3. keep committed attacks that are getting somewhere */
        for (int k = 0; k < nu; k++) {
            Bot *b = &g_bots[units[k]]; int f = b->aFlag;
            if (used[k] || f < 0 || f >= nf || b->avoidCp == f) continue;
            CapturePoint *cp = &g_world.cps[f];
            if (nh && b->aRole == OBJ_ATTACK && cp_capturable(cp) && cp->owner != team) {
                float d = dist2d(upos[k], cp->pos);
                if (!alive[k]) b->aProgressAt = now;   /* respawning near it */
                else if (d < b->aDist - 10) { b->aDist = d; b->aProgressAt = now; }
                if (d < cp->radius + 10) { if (!b->aArrivedAt) b->aArrivedAt = now; } else b->aArrivedAt = 0;
                /* at the flag, stay until it falls: the capture bar is moving, someone is capturing, or enemies
                   contest it. 0.7 released a bot 25 s after it arrived unless the flag's attacker field was set -
                   which BC2 leaves at 0 while a NEUTRAL flag is taken, so bots walked off half-captured flags. */
                int capturing = cp->attacker == team || g_cpEnemyNear[team][f] > 0 || now - g_cpProgAt[f] < 15;
                int keep = b->aArrivedAt ? (capturing || now - b->aArrivedAt < 60) : now - b->aProgressAt < 45;
                if (b->aArrivedAt && counts[f] >= share && capturing) keep = 2;   /* already on it: never pulled off by the share */
                if ((counts[f] < share || keep == 2) && (nh == 1 || keep)) { used[k] = 1; counts[f]++; }
            } else if (!nh && b->aRole == OBJ_PATROL && cp->owner == team) {
                if (dist2d(upos[k], cp->pos) < cp->radius + 10) { if (!b->aArrivedAt) b->aArrivedAt = now; if (now - b->aArrivedAt < 8) { used[k] = 1; counts[f]++; } }
                else { b->aArrivedAt = 0; used[k] = 1; counts[f]++; }
            }
        }
        /* 4. everyone else: spread over the uncovered front flags first, then reinforce */
        for (int turn = 0; turn < nu; turn++) {
            int k = (turn + (int)now) % nu;
            if (used[k]) continue;
            Bot *b = &g_bots[units[k]]; int prev = b->aFlag, best = -1; float score = 1e30f;
            int candidates = nh ? groups : nf, uncovered = 0;
            if (nh) for (int f = 0; f < nf; f++) if (active[f] && !counts[f] && b->avoidCp != f && (candidates == 1 || f != prev)) uncovered = 1;
            for (int f = 0; f < nf; f++) {
                CapturePoint *cp = &g_world.cps[f];
                if (!cp_capturable(cp)) continue;
                if ((nh && !active[f]) || b->avoidCp == f || (candidates > 1 && f == prev && b->aRole == OBJ_ATTACK) || (uncovered && counts[f]) || (nh && counts[f] >= share)) continue;
                if (!nh && cp->owner != team) continue;
                int retake = nh && cp_recent_loss(team, f, now);
                float v = dist2d(upos[k], cp->pos) + counts[f] * (retake ? 700.0f : 1400.0f);
                if (nh && g_cpEnemyNear[team][f]) v -= 350;
                if (nh && cp->owner == 0) v -= 250;
                if (retake) v -= 600;
                if (!nh && f == prev) v += 2000;   /* patrol: move on to another flag */
                if (v < score) { score = v; best = f; }
            }
            if (best < 0 && nh) for (int h = 0; h < nh; h++) if (b->avoidCp != hostile[h]) { best = hostile[h]; break; }   /* never idle */
            if (best < 0) for (int f = 0; f < nf; f++) if (g_world.cps[f].owner == team && cp_capturable(&g_world.cps[f]) && b->avoidCp != f) {
                float v = dist2d(upos[k], g_world.cps[f].pos) + counts[f] * 1400.0f; if (v < score) { score = v; best = f; } }
            assign(b, best, best < 0 ? OBJ_NONE : g_world.cps[best].owner != team ? OBJ_ATTACK : OBJ_PATROL, now);
            if (best >= 0) { counts[best]++; if (best == prev && !nh) b->aArrivedAt = now; }
        }
    }
    static double logAt;
    if (now > logAt) {
        logAt = now + 30;
        for (int team = 1; team <= 2; team++) {
            char line[300]; int u = 0; line[0] = 0;
            for (int f = 0; f < nf; f++) {
                int a = 0, d = 0, g = 0, p = 0;
                for (int i = 0; i < MAX_BOTS; i++) { Bot *b = &g_bots[i]; if (!b->used || b->team != team || b->aFlag != f) continue;
                    if (b->aRole == OBJ_ATTACK) a++; else if (b->aRole == OBJ_DEFEND) d++; else if (b->aRole == OBJ_GUARD) g++; else if (b->aRole == OBJ_PATROL) p++; }
                if (a + d + g + p) u += snprintf(line + u, sizeof line - u, " %s:%s%d%s", g_world.cps[f].name, a ? "attack " : d ? "defend " : g ? "guard " : "patrol ", a + d + g + p,
                                                 (a && (d || g || p)) ? "+" : "");
            }
            rt_log("director: team %d%s", team, u ? line : " no assignments");
        }
    }
}

/* the bot's walking goal for its assignment: a spot inside the capture zone that shifts now and then */
/* where a soldier must stand to capture: the data radius is 0/1 on the shipped flags (Phoenix assumes 10-15 m),
   so bots stand close to the pole - and the reach shrinks further if a flag never starts capturing */

static float cp_reach(int f)
{
    if (g_cpReach[f] <= 0) g_cpReach[f] = fminf(g_world.cps[f].radius * 0.5f, 6.0f);
    return g_cpReach[f];
}
static void reach_learn(double now)
{
    for (int f = 0; f < g_world.cpCount && f < MAX_CPS; f++) {
        CapturePoint *cp = &g_world.cps[f];
        int team = 0, n = 0, enemy = 0;
        for (int i = 0; i < MAX_BOTS; i++) { Bot *b = &g_bots[i];
            if (!b->used || b->state != BS_ALIVE || b->vehSince || dist2d(b->pos, cp->pos) > cp_reach(f) + 0.5f) continue;
            if (team && b->team != team) enemy = 1; team = b->team; n++; }
        if (!n || enemy || cp->owner == team || cp->locked || now - g_cpProgAt[f] < 10 || g_cpEnemyNear[team][f]) { g_cpIdleSince[f] = 0; continue; }
        if (!g_cpIdleSince[f]) g_cpIdleSince[f] = now;
        if (now - g_cpIdleSince[f] > 12 && g_cpReach[f] > 3.0f) {
            g_cpReach[f] = fmaxf(3.0f, g_cpReach[f] * 0.75f); g_cpIdleSince[f] = now;
            rt_log("flags: %s did not start capturing with bots %.0f m from it - they now stand within %.1f m", cp->name, g_cpReach[f] / 0.75f, g_cpReach[f]);
        }
    }
}

/* Overwatch spots per flag (computed once per map): far ones for snipers (upper floors, rooftops, ridges that see
   the flag), near ones for window shooters guarding it. */
#define OW_MAX 8
static float g_ow[2][MAX_CPS][OW_MAX * 3]; static int g_owN[2][MAX_CPS]; static unsigned char g_owDone[2][MAX_CPS]; static unsigned g_owLevel;
static int overwatch_pick(Bot *b, int f, int kind, double now)
{
    if (g_owLevel != g_world.levelSerial) { memset(g_owDone, 0, sizeof g_owDone); memset(g_owN, 0, sizeof g_owN); g_owLevel = g_world.levelSerial; }
    if (!navg_ready()) return 0;
    if (!g_owDone[kind][f]) {
        g_owDone[kind][f] = 1;
        g_owN[kind][f] = kind == 0 ? navg_overwatch(g_world.cps[f].pos, 35, 125, 0, g_ow[kind][f], OW_MAX)
                                   : navg_overwatch(g_world.cps[f].pos, 8, 40, 1, g_ow[kind][f], OW_MAX);
        rt_log("flags: %s - %d %s position(s) found", g_world.cps[f].name, g_owN[kind][f], kind == 0 ? "sniper/overwatch" : "window/upper-floor");
    }
    int n = g_owN[kind][f]; if (!n) return 0;
    /* learned: spots that got kills and did not get their sniper killed are used more */
    int best = -1; float bs = -1;
    for (int k = 0; k < n; k++) {
        float *p = &g_ow[kind][f][k * 3]; int taken = 0;
        for (int i = 0; i < MAX_BOTS; i++) { Bot *o = &g_bots[i]; if (o != b && o->used && o->owOn && dist2d(o->owSpot, p) < 4) taken = 1; }
        if (taken) continue;
        float sc = learn_spot_score(p) * randr(0.6f, 1.4f);
        if (sc > bs) { bs = sc; best = k; }
    }
    if (best < 0) return 0;
    memcpy(b->owSpot, &g_ow[kind][f][best * 3], 12); b->owOn = 1; b->owUntil = now + randr(70, 110); b->owCounted = 0;
    return 1;
}

/* a flanking route: a point off to one side of the straight line, so not every bot comes down the same street */
static void flank_pick(Bot *b, const float *flag)
{
    float dx = flag[0] - b->pos[0], dz = flag[2] - b->pos[2], len = hypotf(dx, dz);
    if (len < 90) return;
    dx /= len; dz /= len;
    float off = randr(30, 60), along = len * randr(0.45f, 0.65f);
    /* learned: flank on the side where fewer bots died */
    float l[3] = {b->pos[0] + dx * along + dz * off, b->pos[1], b->pos[2] + dz * along - dx * off};
    float r[3] = {b->pos[0] + dx * along - dz * off, b->pos[1], b->pos[2] + dz * along + dx * off};
    float dl = learn_danger(l) + randr(0, 1.5f), dr = learn_danger(r) + randr(0, 1.5f);
    float side = dl < dr ? -1.0f : 1.0f;
    float want[3] = {b->pos[0] + dx * along - dz * off * side, b->pos[1], b->pos[2] + dz * along + dx * off * side}, at[3];
    if (!navg_snap(want, at) || dist2d(at, want) > 10 || dist2d(at, flag) < 40) return;
    memcpy(b->via, at, 12); b->viaOn = 1;
}

/* the bot's walking goal for its assignment: a spot near the pole that shifts now and then */
static void objective_goal(Bot *b, double now)
{
    if (b->aFlag < 0 || b->aFlag >= g_world.cpCount) return;
    CapturePoint *cp = &g_world.cps[b->aFlag];
    int fresh = b->goalCp != b->aFlag || !b->haveGoal;
    float d = dist2d(b->pos, cp->pos), reach = cp_reach(b->aFlag);
    if (b->goalCp != b->aFlag) {   /* a new job: maybe overwatch, maybe a flanking route */
        b->viaOn = 0; b->owOn = 0;
        int hold = b->aRole == OBJ_GUARD || b->aRole == OBJ_DEFEND;
        if (b->cls == CLS_RECON && now > b->owNext && rt_randf() < 0.7f) overwatch_pick(b, b->aFlag, 0, now);
        else if (hold && now > b->owNext && rt_randf() < 0.45f) overwatch_pick(b, b->aFlag, 1, now);
        if (!b->owOn && b->aRole == OBJ_ATTACK && rt_randf() < 0.4f) flank_pick(b, cp->pos);
    }
    if (fresh || (d < fmaxf(reach, cp->radius) && now > b->zoneShiftAt)) {
        float r = (b->aRole == OBJ_GUARD || b->aRole == OBJ_DEFEND) ? cp->radius * randr(0.3f, 0.9f) : reach * randr(0.15f, 0.8f), a = randr(0, TWO_PI);
        b->goalCp = b->aFlag; memcpy(b->goal, cp->pos, 12); b->goal[0] += cosf(a) * r; b->goal[2] += sinf(a) * r; b->haveGoal = 1;
        b->goalChosenAt = now; b->zoneShiftAt = now + randr(6, 11);
    }
}

static void hunt_goal(Bot *b, double now)
{
    b->goalChosenAt = now; b->holding = 0; b->goalCp = -1;
    float best = 1e30f; int found = 0;
    for (int i = 0; i < g_world.obs.count; i++) {
        BC2ObservedPlayer *t = &g_world.obs.players[i];
        if ((int)t->team == b->team || t->team < 1 || (int)t->team > nteams() || !t->soldierValid || !t->transformValid) continue;
        float d = dist2d(b->pos, t->position); if (d < best) { best = d; memcpy(b->goal, t->position, 12); found = 1; }
    }
    if (!found && g_world.boundsValid) {
        for (int k = 0; k < 3; k++) b->goal[k] = (g_world.boundsMin[k] + g_world.boundsMax[k]) * 0.5f;
        b->goal[0] += randr(-60, 60); b->goal[2] += randr(-60, 60); found = 1;
    }
    b->haveGoal = found;
}

/* AltBotMod bm_director_threat_in_path: a distant enemy matters when it is on the way to the objective */
static int threat_in_path(const float *origin, const float *goal, const float *enemy)
{
    float dx = goal[0] - origin[0], dz = goal[2] - origin[2], length = hypotf(dx, dz);
    if (length < 40) return 1;
    float ex = enemy[0] - origin[0], ez = enemy[2] - origin[2];
    float along = (ex * dx + ez * dz) / length, side = fabsf(ex * dz - ez * dx) / length;
    return along > 0 && along < length + 30 && side < 45;
}

/* ---------------------------------------------------------------- movement */
static int ray_free(const float *from, float heading, float len, float height)
{
    float a[3] = {from[0], from[1] + height, from[2]};
    float c[3] = {a[0] + sinf(heading) * len, a[1], a[2] + cosf(heading) * len}, h[3], n[3];
    return !eng_ray(a, c, h, n);
}
static int ground_ahead(const float *from, float heading, float ahead)
{
    float a[3] = {from[0] + sinf(heading) * ahead, from[1] + 1.2f, from[2] + cosf(heading) * ahead};
    float c[3] = {a[0], from[1] - 4.0f, a[2]}, h[3], n[3];
    return eng_ray(a, c, h, n);
}

/* Obstacle profile straight ahead (AltBotMod route actions): rays at six heights 1.3 m ahead.
   bit k set = blocked at kProbeH[k]. */
static const float kProbeH[6] = {0.3f, 0.6f, 0.9f, 1.2f, 1.5f, 1.8f};
static int obstacle_profile(const float *from, float heading, float ahead)
{
    int m = 0;
    for (int k = 0; k < 6; k++) if (!ray_free(from, heading, ahead, kProbeH[k])) m |= 1 << k;
    return m;
}
enum { STEER_FREE, STEER_VAULT, STEER_CROUCH };
/* Ray steering: straight ahead first - a low obstacle (ledge, wall, the wooden beams that stop cars) with
   free space above is vaulted (jump, keep pushing forward), a head-height bar is crouched under; otherwise
   a fan of directions is probed at knee, chest and head height and the free one closest to the wish wins. */
static float steer(Bot *b, double now, float wish, int *action)
{
    *action = STEER_FREE;
    if (now < b->steerAt) { *action = b->steerJump; return b->steerYaw; }
    b->steerAt = now + 0.2;
    float best = wish; int found = 0;
    int prof = obstacle_profile(b->pos, wish, 1.3f);
    if (!prof && ground_ahead(b->pos, wish, 1.8f)) found = 1;
    else if ((prof & 0x07) && !(prof & 0x30)) { found = 1; *action = STEER_VAULT; }          /* low: jump it */
    else if (!(prof & 0x07) && (prof & 0x30) && !(prof & 0x08)) { found = 1; *action = STEER_CROUCH; }   /* bar at head height */
    if (!found) {
        static const float offs[] = {0.35f, -0.35f, 0.7f, -0.7f, 1.1f, -1.1f, 1.6f, -1.6f};
        for (unsigned k = 0; k < sizeof offs / sizeof offs[0]; k++) {
            float h = wrap_pi(wish + offs[k]);
            if (ray_free(b->pos, h, 2.2f, 0.45f) && ray_free(b->pos, h, 2.2f, 1.1f) && ray_free(b->pos, h, 2.2f, 1.7f) && ground_ahead(b->pos, h, 1.8f)) { best = h; found = 1; break; }
        }
    }
    if (!found && prof && !(prof & 0x30)) *action = STEER_VAULT;   /* boxed in by low stuff: try jumping it */
    b->steerYaw = best; b->steerJump = *action;
    return best;
}

static int obstacle_ahead(Bot *b, float h);
/* a route asked for from an earlier position: start at the point after the one nearest to where the bot is now */
static int path_nearest(const float *path, int n, const float *pos)
{
    int best = 0; float bd = 1e30f;
    for (int i = 0; i < n && i < 16; i++) { float d = dist2d(pos, &path[i * 3]); if (d < bd) { bd = d; best = i; } }
    if (best + 1 < n && dist2d(&path[best * 3], &path[(best + 1) * 3]) > 0.1f) {
        /* past the nearest point already (closer to the next one than the nearest is): take the next */
        if (dist2d(pos, &path[(best + 1) * 3]) < dist2d(&path[best * 3], &path[(best + 1) * 3])) best++;
    }
    return best;
}
/* 1.6.7: nothing solid between a and b at knee and chest height (a wall, a building - not the floor or a step) */
static int line_clear(const float *a, const float *b)
{
    float h[3], n[3], d = dist2d(a, b);
    for (int k = 0; k < 2; k++) { float up = k ? 1.4f : 0.7f;
        float p0[3] = {a[0], a[1] + up, a[2]}, p1[3] = {b[0], b[1] + up, b[2]};
        if (!eng_ray(p0, p1, h, n) || fabsf(n[1]) > 0.6f || dist2d(a, h) > d - 0.6f) return 1; }
    return 0;
}
static void follow(Bot *b, double now, const float *dest, float *moveHeading, int *moving, float *remaining, float *wayDist)
{
    *moving = 0; *remaining = 0; *wayDist = 0;
    if (!dest) return;
    float target[3]; memcpy(target, dest, 12);
    float dGoal = dist2d(b->pos, dest); *remaining = dGoal;
    /* 1.6.3: a way into a building (an M-COM upstairs, behind a door): walk the surveyed chain once on it; until then the
       grid route goes to the chain's start outside */
    {
        const float *ip = NULL; const unsigned char *idr = NULL; int in = indoor_route(dest, &ip, &idr);
        b->inOn = 0;
        if (in > 0 && !(dGoal < 1.5f && fabsf(dest[1] - b->pos[1]) < 1.5f)) {
            /* on it = near one of its legs (at that leg's height there): head for the leg's end */
            int k = -1; float bd = 1e9f;
            for (int i = 0; i < in; i++) {
                const float *a = &ip[i * 3], *c = &ip[(i + 1 < in ? i + 1 : i) * 3];
                float vx = c[0] - a[0], vz = c[2] - a[2], L2 = vx * vx + vz * vz, u = 0;
                if (L2 > 1e-4f) { u = ((b->pos[0] - a[0]) * vx + (b->pos[2] - a[2]) * vz) / L2; u = u < 0 ? 0 : u > 1 ? 1 : u; }
                float px = a[0] + vx * u, pz = a[2] + vz * u, py = a[1] + (c[1] - a[1]) * u;
                float dd = hypotf(b->pos[0] - px, b->pos[2] - pz), dy = fabsf(py - b->pos[1]);
                if (dd < (i == 0 ? 2.5f : 1.8f) && dy < 1.3f && dd + dy < bd) { bd = dd + dy; k = i + 1 < in ? (u > 0.98f || i == 0 ? i + 1 : i + 1) : i; }
            }
            if (k > 0 && dist2d(b->pos, &ip[(k - 1) * 3]) < 2.5f && fabsf(ip[(k - 1) * 3 + 1] - b->pos[1]) > 0.3f &&
                fabsf(ip[(k - 1) * 3 + 1] - b->pos[1]) < 1.3f && dist2d(b->pos, &ip[(k - 1) * 3]) > 0.45f) k--;   /* a stair step not yet climbed: that one first */
            if (k >= 0 && now < b->inBanUntil) k = -1;   /* this way failed us a moment ago */
            if (k >= 0 && !(b->inChain == ip && b->inIdx >= k) && !idr[k]) {   /* a point through a wall is not "on it" */
                float a[3] = {b->pos[0], b->pos[1] + 0.6f, b->pos[2]}, c2[3] = {ip[k * 3], ip[k * 3 + 1] + 0.6f, ip[k * 3 + 2]}, hh[3], nn[3];
                if (eng_ray(a, c2, hh, nn)) k = -1;
            }
            if (k >= 0) {
                if (b->inChain == ip && b->inIdx > k && b->inIdx < in && fabsf(ip[b->inIdx * 3 + 1] - b->pos[1]) < 1.6f && dist2d(b->pos, &ip[b->inIdx * 3]) < 7)
                    k = b->inIdx;   /* (never step back along it - unless we fell off it) */
                if (b->inChain != ip || k != b->inIdx) b->inIdxAt = now;
                else if (now - b->inIdxAt > 10) {   /* no further along it in 10 s: let the usual unsticking have a go */
                    b->inBanUntil = now + 15; b->inIdxAt = now;
                    static int bl; if (bl++ < 12) rt_log("indoor: %s made no progress on the way in at (%.0f, %.0f, %.0f) - off it for 15 s", b->name, b->pos[0], b->pos[1], b->pos[2]);
                }
                while (k < in - 1 && dist2d(b->pos, &ip[k * 3]) < (fabsf(ip[(k + 1) * 3 + 1] - ip[k * 3 + 1]) > 0.1f ? 0.45f : 0.75f) && fabsf(ip[k * 3 + 1] - b->pos[1]) < 0.3f) k++;   /* on the point, not beside or under it (a stair step) */
                b->inChain = ip; b->inIdx = k; b->inOn = 1;
                memcpy(target, &ip[k * 3], 12);
#ifdef PHX_HARNESS
                if (getenv("INDBG") && now > b->inDbgAt) { b->inDbgAt = now + 1; rt_log("INDBG %s at (%.1f, %.1f, %.1f) on point %d/%d (%.1f, %.1f, %.1f)%s", b->name, b->pos[0], b->pos[1], b->pos[2], k, in, target[0], target[1], target[2], idr[k] ? " door" : ""); }
#endif
                if (idr[k] && dist2d(b->pos, target) < 1.8f && b->knifeLeft <= 0 && now > b->doorKnifeAt && obstacle_ahead(b, heading_to(b->pos, target))) {
                    b->knifeLeft = 3; b->knifeAt = now; b->knifeHeading = heading_to(b->pos, target); b->doorKnifeAt = now + 2.5;
                    static int dl; if (dl++ < 12) rt_log("indoor: %s knifes the door at (%.0f, %.0f, %.0f)", b->name, target[0], target[1], target[2]);
                }
                if (now < b->detourUntil) b->detourUntil = 0;
                *wayDist = dist2d(b->pos, target);
                if (k == in - 1 && dGoal < 0.9f && fabsf(dest[1] - b->pos[1]) < 1.5f) return;   /* there */
                *moveHeading = heading_to(b->pos, target); *moving = 1;
                if (!b->inLogged) { b->inLogged = 1; static int il; if (il++ < 16) rt_log("indoor: %s takes the surveyed way in (%d point(s))", b->name, in); }
                return;
            }
#ifdef PHX_HARNESS
            if (getenv("INDBG") && now > b->inDbgAt) { b->inDbgAt = now + 1; rt_log("INDBG %s at (%.1f, %.1f, %.1f) off the chain, start (%.1f, %.1f, %.1f)", b->name, b->pos[0], b->pos[1], b->pos[2], ip[0], ip[1], ip[2]); }
#endif
            /* not on it yet: the grid route to its start */
            if (dist2d(b->pos, ip) > 1.0f) { dest = ip; dGoal = dist2d(b->pos, dest); memcpy(target, dest, 12); }
        } else b->inLogged = 0;
    }
    /* 1.6.7: stay on the route. A fight (planted feet, strafing, a knife rush), a side-step or a push can leave a bot
       metres off its route; 1.6.6 then walked straight at its old waypoint, through whatever was in between. Twice a
       second: if a later waypoint is closer and in clear sight, skip to it; if the bot is more than 5 m off the
       current leg, or the way to its waypoint is walled off, plan again from where it stands (every 3 s at most). */
    if (navg_ready() && dGoal > 1.5f && b->pathLen && !b->pathPending && now >= b->rejoinAt && now >= b->detourUntil) {
        b->rejoinAt = now + 0.5;
        if (b->pathIdx >= b->pathLen) b->pathIdx = b->pathLen - 1;
        for (int k = b->pathIdx + 1; k < b->pathLen && k <= b->pathIdx + 6; k++)
            if (dist2d(b->pos, &b->path[k * 3]) + 1.0f < dist2d(b->pos, &b->path[b->pathIdx * 3]) && fabsf(b->path[k * 3 + 1] - b->pos[1]) < 2.0f && line_clear(b->pos, &b->path[k * 3])) b->pathIdx = k;
        const float *a = b->pathIdx > 0 ? &b->path[(b->pathIdx - 1) * 3] : b->pathFrom, *c = &b->path[b->pathIdx * 3];
        float vx = c[0] - a[0], vz = c[2] - a[2], L2 = vx * vx + vz * vz, u = L2 > 1e-4f ? ((b->pos[0] - a[0]) * vx + (b->pos[2] - a[2]) * vz) / L2 : 1;
        u = u < 0 ? 0 : u > 1 ? 1 : u;
        float off = hypotf(b->pos[0] - (a[0] + vx * u), b->pos[2] - (a[2] + vz * u));
        int walled = off > 1.5f && fabsf(c[1] - b->pos[1]) < 1.5f && !line_clear(b->pos, c);
        if ((off > 5.0f || walled) && now - b->rejoinPlanAt > 3.0) {
            b->rejoinPlanAt = now; b->pathAt = now - 100; memcpy(b->pathFrom, b->pos, 12);   /* forces a new route below */
            static int rl; if (rl++ < 30 || g_cfg.verbose) rt_log("move: %s is %.0f m off its route%s - a new route from here", b->name, off, walled ? " and walled off from it" : "");
        }
    }
    /* 1.6.7: routes at every distance. 1.6.6 only planned when the goal was over 6 m away and walked the last 6 m
       straight - into the wall between it and a flag point or an M-COM. Now a goal 1.5-6 m away is walked straight
       only when that straight line IS the route (clear at knee and chest height, no floor change); anything else gets a
       planned route, however close. */
    int shortClear = dGoal <= 6 && fabsf(dest[1] - b->pos[1]) < 1.0f && line_clear(b->pos, dest);
    if (shortClear && b->pathLen && dist2d(b->pathGoal, dest) < 1.0f) b->pathLen = 0;   /* in sight now: no detour via old waypoints */
    /* path refresh (a moving destination - a squad leader, a human - is re-planned at most every 2 s) */
    if (navg_ready() && dGoal > 1.5f && !shortClear) {
        float drift = dist2d(b->pathGoal, dest);
        int needNew = !b->pathLen || (drift > 3 && (drift > 12 || now - b->pathAt > 2)) || now - b->pathAt > 25;
        if (needNew || b->pathPending) {
            float out[MAX_PATH_PTS * 3]; int deferred = 0;
            if (!b->pathPending || dist2d(b->pathFrom, b->pos) > 25) memcpy(b->pathFrom, b->pos, 12);   /* (a request restarts when its start moves) */
            int n = navg_path((int)(b - g_bots), b->pathFrom, dest, out, MAX_PATH_PTS, &deferred);
            if (n > 0) { memcpy(b->path, out, (size_t)n * 12); b->pathLen = n; b->pathIdx = path_nearest(b->path, n, b->pos); memcpy(b->pathGoal, dest, 12); b->pathAt = now; b->pathPending = 0; b->pgAt = 0; }
            else if (deferred) b->pathPending = 1;
            else { b->pathPending = 0; b->pathLen = 0; b->pathAt = now; memcpy(b->pathGoal, dest, 12);
                if (dGoal < 45) indoor_request(dest, "no grid route to it");   /* 1.6.3 */
                /* 1.6.9: no route because the bot stands on an island of the grid (rubble, a ledge, a roof, a bank the map
                   never linked): walk to the nearest ground on the connected map - however far - and plan from there,
                   instead of walking straight at the goal off any route */
                if (now > b->islandAt && !navg_main_area(b->pos)) {
                    b->islandAt = now + 6;
                    if (navg_main_near(b->pos, 150, b->island) && dist2d(b->island, b->pos) > 1.0f) {
                        memcpy(b->detour, b->island, 12); b->detourUntil = now + fminf(30.0f, 4.0f + dist2d(b->pos, b->island) / 2.5f); b->islandUntil = b->detourUntil;
                        static int il; if (il++ < 20 || g_cfg.verbose) rt_log("move: %s has no route from (%.0f, %.0f, %.0f) - off the connected map; back to it %.0f m away first", b->name, b->pos[0], b->pos[1], b->pos[2], dist2d(b->pos, b->island));
                    }
                } }
        }
    }
    if (b->pathLen) {
        int before = b->pathIdx;
        while (b->pathIdx < b->pathLen - 1 && dist2d(b->pos, &b->path[b->pathIdx * 3]) < 2.0f) b->pathIdx++;
        if (b->pathIdx != before) b->pgAt = 0;   /* new waypoint: restart the progress check */
        memcpy(target, &b->path[b->pathIdx * 3], 12);
        if (b->pathIdx == b->pathLen - 1 && dist2d(b->pos, target) < 1.5f) { b->pathLen = 0; memcpy(target, dest, 12); }
    }
    if (now < b->islandUntil && dist2d(b->pos, b->island) < 1.5f) { b->islandUntil = 0; b->detourUntil = 0; b->pathAt = 0; b->pathLen = 0; }   /* 1.6.9: on the map again - a route from here */
    if (now < b->detourUntil) memcpy(target, b->detour, 12);
    *wayDist = dist2d(b->pos, target);
    if (*wayDist < 1.2f && dGoal < 2.0f) return;
    *moveHeading = heading_to(b->pos, target);
    *moving = 1;
}

static int obstacle_ahead(Bot *b, float h) { return !ray_free(b->pos, h, 1.7f, 0.45f) || !ray_free(b->pos, h, 1.7f, 1.1f); }

/* Stuck = no progress towards the current waypoint. Jitter or recovery strafing does not reset it.
   Escalation: knife the obstacle (fences and other destructible cover break) -> strafe/back off and jump ->
   mark the cells blocked and re-plan -> side-step detour -> another objective. */
static void stuck_check(Bot *b, double now, int moving, float moveHeading, float wayDist)
{
    if (!moving || b->knifeLeft > 0) { memcpy(b->progressPos, b->pos, 12); b->progressAt = now; b->pgAt = 0; return; }
    if (b->pgAt == 0 || wayDist < b->pgBest - 0.8f) { b->pgBest = wayDist; b->pgAt = now; }
    int noProgress = now - b->pgAt > 3.0;
    if (now - b->progressAt < 1.0 && !noProgress) return;
    float moved = dist2d(b->pos, b->progressPos);
    memcpy(b->progressPos, b->pos, 12); b->progressAt = now;
    if (!noProgress && moved > 0.6f) { if (now - b->recoverUntil > 6) b->stuckCount = 0; return; }
    b->pgAt = now; b->pgBest = wayDist;
    if (now < b->recoverUntil || now < b->detourUntil - 3) return;
    b->stuckCount++;
    if (b->stuckCount == 1 && g_cfg.learn && b->lspot < 0) {   /* remember the spot; what gets us through is learned */
        b->lspot = learn_stuck_event(b->pos, moveHeading); b->lspotAt = now; memcpy(b->lspotPos, b->pos, 12); b->lspotHeading = moveHeading; b->lastAct = -1;
    }
    if (b->stuckCount == 1 && obstacle_ahead(b, moveHeading)) {
        b->knifeLeft = 3; b->knifeAt = now; b->knifeHeading = moveHeading;   /* fence or similar right in front */
        b->lastAct = LA_KNIFE;
    } else {
        b->recoverUntil = now + 1.0; b->recoverStrafe = rt_randf() < 0.5f ? -1.0f : 1.0f; b->recoverBack = b->stuckCount >= 2;
        press(b, BC2_EiaJump);
        b->lastAct = b->recoverBack ? LA_BACK : LA_JUMP;
    }
    if (b->stuckCount == 2) navg_reprobe(b->pos, 6.0f);   /* look again: a wall blown open, rubble, a wreck */
    if (b->stuckCount >= 3) {
        /* AltBotMod V18: never block links where a soldier is in the way (a teammate in a doorway) or on stairs -
           blocking a stairwell kept every bot on one floor */
        int crowded = 0, stairs = 0;
        for (int i = 0; i < g_world.obs.count && !crowded; i++) { const BC2ObservedPlayer *p = &g_world.obs.players[i];
            if (p->pointer != b->player && p->soldierValid && p->transformValid && p->health > 0 && dist2d(p->position, b->pos) < 1.8f && fabsf(p->position[1] - b->pos[1]) < 2.0f) crowded = 1; }
        if (b->pathLen && b->pathIdx < b->pathLen && fabsf(b->path[b->pathIdx * 3 + 1] - b->pos[1]) > 0.8f) stairs = 1;
        if (b->inOn) stairs = 1;   /* on a surveyed way in: never cut the grid there */
        float ahead[3] = {b->pos[0] + sinf(moveHeading) * 3, b->pos[1], b->pos[2] + cosf(moveHeading) * 3};
        if (!crowded && !stairs) navg_blocked(b->pos, ahead);
        b->pathLen = 0; b->pathAt = 0;
    }
    if (b->stuckCount >= 3) {   /* side-step: a point 8-14 m to one side that is reachable by ray */
        for (int k = 0; k < 6; k++) {
            float h = moveHeading + (k & 1 ? 1.0f : -1.0f) * randr(1.1f, 2.2f), d = randr(8, 14);
            if (ray_free(b->pos, h, d, 1.0f)) {
                b->detour[0] = b->pos[0] + sinf(h) * d; b->detour[1] = b->pos[1]; b->detour[2] = b->pos[2] + cosf(h) * d;
                b->detourUntil = now + 4.5; b->lastAct = LA_DETOUR; break;
            }
        }
    }
    if (b->stuckCount >= 6) {   /* this spot will not let us through: re-plan around it; only repeated failures change the flag */
        b->stuckCount = 0; b->detourUntil = 0; b->pathLen = 0; b->pathAt = 0;
        if (now - b->giveUpAt > 90) b->giveUps = 0;
        b->giveUps++; b->giveUpAt = now;
        b->owOn = 0; b->viaOn = 0;   /* a sniper spot / flank route that cannot be reached is dropped */
        if (b->lspot >= 0) { learn_stuck_result(b->lspot, -1, 0); b->lspot = -1; }
        if (b->giveUps >= 2 && learn_near_water(b->pos, 40)) {   /* 1.6: wading round a harbour (Valparaiso's defenders) - start again */
            rt_log("bot %s: stuck at the water's edge near (%.0f, %.0f, %.0f) - redeploying it", b->name, b->pos[0], b->pos[1], b->pos[2]);
            b->giveUps = 0; b->redeploy = 1; b->frozenSince = now;
        } else if (b->giveUps >= 3 && b->aFlag >= 0) {
            rt_log("bot %s: can't get through near (%.0f, %.0f, %.0f) three times - another flag for 40 s", b->name, b->pos[0], b->pos[1], b->pos[2]);
            b->avoidCp = b->aFlag; b->avoidUntil = now + 40; b->giveUps = 0;
            b->goalCp = -1; b->haveGoal = 0; b->aFlag = -1; b->aRole = OBJ_NONE; g_directorAt = 0;
        } else if (g_cfg.verbose) rt_log("bot %s: blocked near (%.0f, %.0f, %.0f) - re-planning", b->name, b->pos[0], b->pos[1], b->pos[2]);
    }
}

/* ---------------------------------------------------------------- equipment */
enum { EQ_IDLE, EQ_SELECTING, EQ_USING, EQ_RETURNING };
static int eq_start(Bot *b, double now, int selectAction, double useFor, int pitchMode, float pitch, int backAction)
{
    if (!g_cfg.equipment || b->eqState != EQ_IDLE) return 0;
    b->eqState = EQ_SELECTING; b->eqAt = now; b->eqSelect = selectAction; b->eqUseFor = useFor;
    b->eqPitchMode = pitchMode; b->eqPitch = pitch; b->eqBackAction = backAction;
    press(b, selectAction);
    return 1;
}
/* returns 1 while equipment owns the trigger */
static int eq_update(Bot *b, double now)
{
    switch (b->eqState) {
    case EQ_SELECTING: if (now - b->eqAt > 0.8) { b->eqState = EQ_USING; b->eqAt = now; } return 1;
    case EQ_USING:
        b->levels[BC2_EiaFire] = 1;
        if (now - b->eqAt > b->eqUseFor) {
            b->eqState = EQ_RETURNING; b->eqAt = now;
            /* back to the primary by selecting it (1.6): SwitchPrimaryWeapon steps to the NEXT weapon - from the launcher
               that is the repair tool, which engineers then carried into fights */
            if (b->eqBackAction == BC2_EiaSwitchPrimaryWeapon && b->soldier && eng_soldier_set_weapon(b->soldier, 0)) {}
            else press(b, b->eqBackAction);
        }
        return 1;
    case EQ_RETURNING: if (now - b->eqAt > 0.7) b->eqState = EQ_IDLE; return 1;
    default: return 0;
    }
}

/* ---------------------------------------------------------------- revive */
static const BC2ObservedPlayer *downed_teammate(Bot *b, float maxd)
{
    const BC2ObservedPlayer *best = NULL; float bd = maxd;
    for (int i = 0; i < g_world.obs.count; i++) {
        const BC2ObservedPlayer *p = &g_world.obs.players[i];
        if (p->isAI || (int)p->team != b->team || !p->soldierValid || !p->transformValid || p->health > 0) continue;
        float d = dist2d(b->pos, p->position); if (d < bd) { bd = d; best = p; }
    }
    return best;
}
/* human teammates within r (hurt below hurtBelow, if > 0) */
static int humans_near(Bot *b, float r, float hurtBelow)
{
    int n = 0;
    for (int i = 0; i < g_world.obs.count; i++) {
        const BC2ObservedPlayer *p = &g_world.obs.players[i];
        if (p->isAI || (int)p->team != b->team || !p->soldierValid || !p->transformValid || p->health <= 0 || p->entry) continue;
        if (hurtBelow > 0 && p->health >= hurtBelow) continue;
        if (dist2d(b->pos, p->position) < r) n++;
    }
    return n;
}
static int teammates_near(Bot *b, float r, float hurtBelow)
{
    int n = 0;
    for (int i = 0; i < g_world.obs.count; i++) {
        const BC2ObservedPlayer *p = &g_world.obs.players[i];
        if ((int)p->team != b->team || p->pointer == b->player || !p->soldierValid || !p->transformValid || p->health <= 0) continue;
        if (hurtBelow > 0 && p->health >= hurtBelow) continue;
        if (dist2d(b->pos, p->position) < r) n++;
    }
    return n;
}

/* ---------------------------------------------------------------- vehicles (0.7.1, experimental)
   Passengers: a bot near a ground vehicle with a teammate driving gets in a free seat and rides towards
   its flag, getting out close to it (or when the ride stops). Drivers (Vehicles=2): a bot whose flag is
   far takes an empty ground vehicle near it, waits a moment for passengers, learns the controls with a
   short test (which input steers, which way), then drives the route to its flag and gets out there.
   Seats are entered through ServerPlayer::enterVehicle and left through the same call ChangeVehicle uses.
   Helicopters, boats and emplacements are never used. */
enum { VR_NONE, VR_DRIVER, VR_PASSENGER, VR_GUNNER };   /* VR_GUNNER: a stationary weapon (1.5) */
enum { HS_TEST_LIFT, HS_TEST_PITCH, HS_TEST_ROLL, HS_TEST_YAW, HS_CLIMB, HS_CRUISE, HS_LAND, HS_GROUND, HS_LOITER, HS_EXIT };
typedef struct DriveCalib { uint32_t data; int ok, steerAction; float steerSign, fwdSign; float collSign, pitchSign, rollSign, yawSign; int crashes, rollAction, yawAction, tryAlt, air, fails, presetGround, presetHeli, pvScore, pvBack; } DriveCalib;
static DriveCalib g_calib[48]; static int g_calibN;
static uint32_t g_vehClaim[MAX_VEH]; static int g_vehClaimBot[MAX_VEH]; static double g_vehClaimUntil[MAX_VEH];

static DriveCalib *calib_for(uint32_t data, int create)
{
    for (int i = 0; i < g_calibN; i++) if (g_calib[i].data == data) return &g_calib[i];
    if (!create || g_calibN >= (int)(sizeof g_calib / sizeof *g_calib)) return NULL;
    DriveCalib *c = &g_calib[g_calibN++]; memset(c, 0, sizeof *c); c->data = data; return c;
}
static int veh_tracked(const char *n)
{
    static const char *k[] = {"M1A2", "ABRAMS", "T90", "T-90", "BMD", "BRADLEY", "M3A3", "BMP", "M48", "T54", "T-54", "TANK"};
    char u[64]; int i = 0; for (; n[i] && i < 63; i++) u[i] = (char)toupper((unsigned char)n[i]); u[i] = 0;
    for (unsigned j = 0; j < sizeof k / sizeof *k; j++) if (strstr(u, k[j])) return 1;
    return 0;
}
static int veh_armoured(const char *n)
{
    static const char *k[] = {"M1A2", "ABRAMS", "T90", "T-90", "BMD", "BRADLEY", "M3A3", "BMP", "M48", "T54", "T-54", "TANK", "APC", "IFV", "CAV"};
    char u[64]; int i = 0; for (; n[i] && i < 63; i++) u[i] = (char)toupper((unsigned char)n[i]); u[i] = 0;
    for (unsigned j = 0; j < sizeof k / sizeof *k; j++) if (strstr(u, k[j])) return 1;
    return 0;
}
static const VehicleInfo *veh_info(uint32_t e) { for (int i = 0; i < g_world.vehCount; i++) if (g_world.veh[i].entity == e) return &g_world.veh[i]; return NULL; }
static int veh_claimed(uint32_t e, Bot *b, double now)
{
    for (int i = 0; i < MAX_VEH; i++) if (g_vehClaim[i] == e && now < g_vehClaimUntil[i] && g_vehClaimBot[i] != (int)(b - g_bots)) return 1;
    return 0;
}
static void veh_claim(uint32_t e, Bot *b, double now, double secs)
{
    int slot = -1;
    for (int i = 0; i < MAX_VEH; i++) if (g_vehClaim[i] == e || now >= g_vehClaimUntil[i]) { slot = i; if (g_vehClaim[i] == e) break; }
    if (slot >= 0) { g_vehClaim[slot] = e; g_vehClaimBot[slot] = (int)(b - g_bots); g_vehClaimUntil[slot] = now + secs; }
}
/* 1.6.8: a vehicle its crew left for want of a route (no drivable route, waited 25 s, no progress) is left alone for a
   while. 1.6.7 let the next bot - or the same one, "back to the ride we came in" - board it again 6 s later, wait 25 s
   and get out: the vehicle parked at a freshly taken flag held its team there for the rest of the round. 1.6.10: 45 s
   (was 90) - with routes kept and the planner no longer restarting, a vehicle left once is often drivable soon after. */
static uint32_t g_vehBan[MAX_VEH]; static double g_vehBanUntil[MAX_VEH];
static int veh_banned(uint32_t e, double now) { for (int i = 0; i < MAX_VEH; i++) if (g_vehBan[i] == e && now < g_vehBanUntil[i]) return 1; return 0; }
static void veh_ban(uint32_t e, double now, double secs)
{
    if (!e) return;
    int slot = 0; for (int i = 0; i < MAX_VEH; i++) { if (g_vehBan[i] == e) { slot = i; break; } if (g_vehBanUntil[i] < g_vehBanUntil[slot]) slot = i; }
    g_vehBan[slot] = e; g_vehBanUntil[slot] = now + secs;
}
/* crews (1.0.1): live 1.0 had RU bots deploy at the flags while their T-90s and Mi-28 sat at the base. A bot whose flag
   is far away may deploy at the spawn next to an idle vehicle of its team and take it (up to 3 drivers and every
   helicopter per team). */
static DriveCalib *calib_for(uint32_t data, int create);
static int crew_spawn(Bot *b, const int *cand, int n, const float *to)
{
    if (g_cfg.vehicles < 2 || b->cls == CLS_RECON) return -1;
    double now = rt_now();
    int drivers = 0; for (int k = 0; k < MAX_BOTS; k++) if (g_bots[k].used && g_bots[k].team == b->team && g_bots[k].vehRole == VR_DRIVER) drivers++;
    int bestK = -1; float bestD = 1e30f; uint32_t bestV = 0;
    for (int i = 0; i < g_world.vehCount; i++) {
        const VehicleInfo *v = &g_world.veh[i];
        if (v->team != b->team || v->seats < 2) continue;
        const DriveCalib *cc = calib_for(v->data, 0);
        int heli = v->kind == VK_HELI || (cc && cc->air);
        if (heli ? !g_cfg.helicopters : (v->kind != VK_GROUND && v->kind != VK_UNKNOWN)) continue;
        if (!heli && !navg_ready()) continue;   /* 1.6.8: no drive map yet */
        if (!heli && drivers >= 5) continue;
        if (cc && (cc->ok < 0 || cc->crashes >= 3)) continue;
        if (eng_vehicle_occupant(v->entity, 0) || veh_claimed(v->entity, b, now) || veh_banned(v->entity, now)) continue;
        VehicleInfo nv; if (!world_vehicle_read(v->entity, &nv)) continue;
        for (int k = 0; k < n; k++) {
            const SpawnPoint *sp = &g_world.spawns[cand[k]]; if (!sp->posValid) continue;
            float d = dist2d(sp->pos, nv.pos);
            if (d > 75 || dist2d(sp->pos, to) < 180) continue;
            if (d < bestD) { bestD = d; bestK = cand[k]; bestV = v->entity; }
        }
    }
    if (bestK < 0 || rt_randf() > (veh_armoured(veh_info(bestV) ? veh_info(bestV)->name : "") || (veh_info(bestV) && veh_info(bestV)->kind == VK_HELI) ? 0.95f : 0.6f)) return -1;
    b->lastVeh = bestV; veh_claim(bestV, b, now, 40); b->vehCheckAt = 0; b->crewBoard = 1;
    return bestK;
}
static int player_team(uint32_t p) { for (int i = 0; i < g_world.obs.count; i++) if (g_world.obs.players[i].pointer == p) return (int)g_world.obs.players[i].team; return 0; }
static Bot *bot_of_player(uint32_t p) { for (int i = 0; i < MAX_BOTS; i++) if (g_bots[i].used && g_bots[i].player == p) return &g_bots[i]; return NULL; }

/* on foot: decide whether to board something; sets b->vehicle/seat/vehRole (boarding) */
static int veh_on_ground(const float *pos)
{
    float up[3] = {pos[0], pos[1] + 1.0f, pos[2]}, down[3] = {pos[0], pos[1] - 3.5f, pos[2]}, h[3], n[3];
    return eng_ray(up, down, h, n);
}
/* ---------------------------------------------------------------- boats (1.5)
   The navigation grid has no water on the Vietnam maps (the rays go through it), so a boat finds its way by looking:
   water is where the riverbed is well below the hull (or the ray hits a flat surface at the hull's height). The driver
   steers for the heading with the longest run of water that still points at the flag, and beaches it near the flag. */
static int boat_water_at(float x, float z, float wy)
{
    float a[3] = {x, wy + 3.0f, z}, c[3] = {x, wy - 6.0f, z}, h[3], n[3];
    if (!eng_ray(a, c, h, n)) return 1;                        /* nothing within 6 m under the surface: deep water */
    if (h[1] < wy - 0.9f) return 1;                            /* riverbed well below the boat */
    if (fabsf(h[1] - wy) < 0.35f && n[1] > 0.97f) return 1;    /* the water surface itself */
    return 0;
}
static float boat_run(const float *from, float hd, float wy, float maxLen)
{
    float len = 0;
    for (float d = 4; d <= maxLen; d += 4) { if (!boat_water_at(from[0] + sinf(hd) * d, from[2] + cosf(hd) * d, wy)) break; len = d; }
    float a[3] = {from[0], wy + 1.2f, from[2]}, e[3] = {from[0] + sinf(hd) * len, wy + 1.2f, from[2] + cosf(hd) * len}, h[3], n[3];
    if (len > 0 && eng_ray(a, e, h, n)) len = fmaxf(0, fminf(len, dist2d(a, h) - 2));   /* rocks, piers, bridges at deck height */
    return len;
}
static int boat_useful(const VehicleInfo *v, const float *flag)
{
    VehicleInfo nv; if (!world_vehicle_read(v->entity, &nv)) return 0;
    if (dist2d(nv.pos, flag) < 120) return 0;
    float to = heading_to(nv.pos, flag);
    for (int k = -2; k <= 2; k++) if (boat_run(nv.pos, to + k * 0.5f, nv.pos[1], 40) >= 28) return 1;
    return 0;
}
/* vehicle health (1.5): +0x34 like a soldier's; the most seen for each vehicle is taken as its full health */
static uint32_t g_vhE[96]; static float g_vhMax[96];
static float veh_max_health(const VehicleInfo *v)
{
    int k = 0; for (; k < 96; k++) if (g_vhE[k] == v->entity) break;
    if (k == 96) { k = (int)(rt_rand() % 96); for (int j = 0; j < 96; j++) if (!g_vhE[j]) { k = j; break; } g_vhE[k] = v->entity; g_vhMax[k] = 0; }
    if (v->healthOk && v->health > g_vhMax[k]) g_vhMax[k] = v->health;
    return g_vhMax[k];
}
static int veh_needs_repair(const VehicleInfo *v, float below)
{
    if (!v->healthOk) return 0;
    float mx = veh_max_health(v);
    return mx > 1 && v->health < mx * below && v->health > 0;
}
static uint32_t repair_target(Bot *b, float range)
{
    uint32_t best = 0; float bd = range;
    for (int i = 0; i < g_world.vehCount; i++) {
        const VehicleInfo *v = &g_world.veh[i];
        if (v->kind != VK_GROUND && v->kind != VK_UNKNOWN && v->kind != VK_HELI && v->kind != VK_BOAT && v->kind != VK_EMPLACE) continue;
        VehicleInfo nv; if (!world_vehicle_read(v->entity, &nv)) continue;
        float d = dist2d(b->pos, nv.pos); if (d > bd || fabsf(nv.pos[1] - b->pos[1]) > 3) continue;
        int mineSide = v->team == b->team, enemyIn = 0;
        for (int s2 = 0; s2 < nv.seats; s2++) { uint32_t o = eng_vehicle_occupant(v->entity, s2); if (!o) continue; int tm = player_team(o); if (tm == b->team) mineSide = 1; else if (tm > 0) enemyIn = 1; }
        if (!mineSide || enemyIn || !veh_on_ground(nv.pos)) continue;
        if (!veh_needs_repair(&nv, 0.8f)) continue;
        /* standing still (parked at a flag, waiting): it moved under 1 m since the last scan of it */
        if (dist2d(nv.pos, v->pos) > 1.5f) continue;
        bd = d; best = v->entity;
    }
    return best;
}
/* stationary weapons (1.5): a bot near an empty one takes it when no enemy is close to it and there is something to
   shoot at from it - defending a flag, or enemies further out than 40 m */
static void emplacement_consider(Bot *b, double now, int engaging, float tdist)
{
    if (!g_cfg.emplacements || !g_cfg.vehicles || b->vehRole || b->reviveP || now < b->emplUntil) return;
    b->emplUntil = now + 2.0;
    if (engaging && tdist < 40) return;   /* close fights are fought on foot */
    int bestV = -1; float bestD = 22;
    for (int i = 0; i < g_world.vehCount; i++) {
        const VehicleInfo *v = &g_world.veh[i];
        if (v->kind != VK_EMPLACE || (v->team && v->team != b->team)) continue;
        float d = dist2d(b->pos, v->pos); if (d > bestD || fabsf(v->pos[1] - b->pos[1]) > 4) continue;
        if (eng_vehicle_occupant(v->entity, 0) || veh_claimed(v->entity, b, now) || !eng_vehicle_seat_allowed(v->entity, b->player, 0)) continue;
        int nClose = 0, nFar = 0;
        for (int k = 0; k < g_world.obs.count; k++) { const BC2ObservedPlayer *t = &g_world.obs.players[k];
            if (t->team < 1 || (int)t->team == b->team || !t->soldierValid || !t->transformValid || t->health <= 0) continue;
            float e = dist2d(t->position, v->pos); if (e < 30) nClose++; else if (e < 220) nFar++; }
        if (nClose || (!nFar && !b->holding && !engaging)) continue;
        bestD = d; bestV = i;
    }
    if (bestV < 0 || rt_randf() > 0.6f) return;
    b->vehicle = g_world.veh[bestV].entity; b->seat = 0; b->vehRole = VR_GUNNER; b->vehEnterUntil = now + 12; b->vehSince = 0;
    veh_claim(b->vehicle, b, now, 20);
}
static void vehicle_consider(Bot *b, double now, int engaging)
{
    if (!g_cfg.vehicles || engaging || b->reviveP || now < b->vehCheckAt || b->aFlag < 0 || b->aFlag >= g_world.cpCount) return;
    b->vehCheckAt = now + 1.5;
    if (b->vehRole) return;
    float distFlag = dist2d(b->pos, g_world.cps[b->aFlag].pos);
    if (distFlag < 60) return;
    int bestV = -1, bestSeat = -1, bestRole = 0; float bestD = 1e30f;
    for (int i = 0; i < g_world.vehCount; i++) {
        const VehicleInfo *v = &g_world.veh[i];
        const DriveCalib *ac = calib_for(v->data, 0);
        int heli = v->kind == VK_HELI || (ac && ac->air), ground = !heli && (v->kind == VK_GROUND || v->kind == VK_UNKNOWN);   /* unnamed ones are tested by driving */
        if (!heli && v->kind == VK_BOAT && g_cfg.boats) ground = 1;   /* boats: only when there is water towards the flag (below) */
        if (!(ground || (heli && g_cfg.helicopters))) continue;
        if (veh_banned(v->entity, now)) continue;   /* 1.6.8: no route from where it stands - walk the route instead */
        int mine = v->entity == b->lastVeh;
        /* 1.6.10: the other side's vehicles are theirs - unless it is standing empty (abandoned): BC2 lets anyone take an
           empty vehicle, and 1.6.9 bots walked past every enemy tank left by the road */
        int empty = 1; for (int s2 = 0; s2 < v->seats && s2 < 8; s2++) if (eng_vehicle_occupant(v->entity, s2)) { empty = 0; break; }
        if (v->team && v->team != b->team && !mine && !(empty && !heli && v->kind != VK_EMPLACE)) continue;
        /* a helicopter's gunner seat: any job, as long as a teammate flies it or is about to */
        if (heli && v->seats >= 2 && !eng_vehicle_occupant(v->entity, 1) && dist2d(b->pos, v->pos) < 60 && veh_on_ground(v->pos)) {
            uint32_t pil = eng_vehicle_occupant(v->entity, 0);
            if (pil && player_team(pil) == b->team && eng_vehicle_seat_allowed(v->entity, b->player, 1)) {
                float d = dist2d(b->pos, v->pos); if (d < bestD) { bestD = d; bestV = i; bestSeat = 1; bestRole = VR_PASSENGER; } continue;
            }
        }
        /* back to the ride we came in, from further away; 1.6.10: an empty vehicle (to drive) from up to 100 m, armour 140 m -
           60 m left the vehicles abandoned out on the map standing all round */
        float d = dist2d(b->pos, v->pos), reach = mine ? 120 : heli ? 80 : 60;
        if (empty && !heli && g_cfg.vehicles >= 2) reach = fmaxf(reach, veh_armoured(v->name) ? 140.0f : 100.0f);
        if (d > reach) continue;
        if (v->kind == VK_BOAT) {   /* (cached a few seconds per boat and flag: it costs rays) */
            static uint32_t be[16]; static int bf[16], bu[16]; static double bt[16]; int k = 0, slot = -1;
            for (; k < 16; k++) if (be[k] == v->entity && bf[k] == b->aFlag) break;
            if (k == 16 || now > bt[k]) { if (k == 16) { slot = (int)(rt_rand() % 16); k = slot; } be[k] = v->entity; bf[k] = b->aFlag; bt[k] = now + 6; bu[k] = boat_useful(v, g_world.cps[b->aFlag].pos); }
            if (!bu[k]) continue;
        }
        uint32_t driver = eng_vehicle_occupant(v->entity, 0);
        if (driver) {   /* passenger: a teammate drives / flies */
            if (player_team(driver) != b->team || distFlag < (heli ? 140 : 70)) continue;
            if (heli && !veh_on_ground(v->pos)) continue;
            Bot *db = bot_of_player(driver);
            if (db && db->aFlag != b->aFlag && dist2d(g_world.cps[db->aFlag >= 0 ? db->aFlag : b->aFlag].pos, g_world.cps[b->aFlag].pos) > 150) continue;
            for (int seat = 1; seat < v->seats; seat++) if (!eng_vehicle_occupant(v->entity, seat) && eng_vehicle_seat_allowed(v->entity, b->player, seat)) {
                if (d < bestD) { bestD = d; bestV = i; bestSeat = seat; bestRole = VR_PASSENGER; } break; }
        } else if (g_cfg.vehicles >= 2 && !veh_claimed(v->entity, b, now) && eng_vehicle_seat_allowed(v->entity, b->player, 0)) {
            const DriveCalib *cc = calib_for(v->data, 0); if (cc && (cc->ok < 0 || cc->crashes >= 3)) continue;
            if (heli) {
                if (distFlag < 80 || !veh_on_ground(v->pos)) continue;
            } else {
                if (!navg_ready() && v->kind != VK_BOAT) continue;   /* 1.6.8: no drive map yet (first round on a map) - no route to drive */
                /* 1.6.10: 60 / 70 m (was 80 / 110): Panama Canal's flags are 94-166 m apart, so the vehicle that appears at a
                   flag just taken was mostly left standing - the next flag was "too close" to be worth driving to */
                if (distFlag < (veh_armoured(v->name) ? 60 : 70)) { if (g_cfg.verbose && d < 30 && now > b->pathFailLogAt) { b->pathFailLogAt = now + 30;
                    rt_log("vehicles: %s leaves '%s' %.0f m away - its flag %c is only %.0f m off", b->name, v->name, d, 'A' + b->aFlag, distFlag); } continue; }
                int drivers = 0; for (int k = 0; k < MAX_BOTS; k++) if (g_bots[k].used && g_bots[k].team == b->team && g_bots[k].vehRole == VR_DRIVER) drivers++;
                if (drivers >= 5 || (!mine && !veh_armoured(v->name) && rt_randf() > 0.8f)) continue;   /* armour is always taken */
            }
            if (d < bestD) { bestD = d; bestV = i; bestSeat = 0; bestRole = VR_DRIVER; }
        }
    }
    if (bestV < 0) return;
    b->vehicle = g_world.veh[bestV].entity; b->seat = bestSeat; b->vehRole = bestRole; b->vehEnterUntil = now + (bestD > 40 ? 30 : 14); b->vehSince = 0;
    if (bestRole == VR_DRIVER) veh_claim(b->vehicle, b, now, 35);
}

/* on foot, boarding: walk to the vehicle, get in when close. Returns the walk target or NULL. */
static const float *vehicle_board(Bot *b, double now)
{
    if (!b->vehRole || b->vehSince) return NULL;
    const VehicleInfo *v = veh_info(b->vehicle);
    if (!v || now > b->vehEnterUntil || eng_vehicle_occupant(v->entity, b->seat) || (b->vehRole == VR_PASSENGER && !eng_vehicle_occupant(v->entity, 0))) {
        b->vehRole = VR_NONE; b->vehicle = 0; b->vehCheckAt = now + 8; return NULL;
    }
    /* a crew that deployed at the vehicle's spawn gets straight in (the HQ spawns sit up to ~75 m from the vehicles and
       1.1's crews spent a minute walking down to them); if the game wants us closer, walk */
    int fast = b->crewBoard && dist2d(b->pos, v->pos) < 90;
    b->crewBoard = 0;
    if (!fast && dist2d(b->pos, v->pos) > 4.5f) return v->pos;
    if (eng_enter_vehicle(b->player, v->entity, b->seat)) {
        b->vehSince = now; b->vehEnterUntil = 0; b->vehProgressAt = now; b->vehProgressDist = 1e30f; b->steerCalib = 0; b->reverseUntil = 0; b->unpinTries = 0;
        b->pathLen = 0; b->pathPending = 0; b->pathAt = 0; memcpy(b->pathFrom, v->pos, 12); b->noRouteSince = 0; b->routeWaitSince = 0;   /* 1.6.6 */
        memcpy(b->calibStart, v->pos, 12); b->calibAt = now;
        rt_log("vehicles: %s gets in '%s' as %s (seat %d)", b->name, v->name, b->vehRole == VR_DRIVER ? "driver" : "passenger", b->seat);
        if (b->vehRole == VR_DRIVER) veh_claim(v->entity, b, now, 600);
    } else if (fast) { return v->pos;   /* too far for the game: walk over */
    } else {
        static int fails; if (fails++ < 6) rt_log("vehicles: %s could not enter '%s' seat %d", b->name, v->name, b->seat);
        b->vehRole = VR_NONE; b->vehicle = 0; b->vehCheckAt = now + 15;
    }
    return NULL;
}

static void vehicle_leave(Bot *b, double now, const char *why)
{
    /* never out of a vehicle in the air (1.1: a bot got out of a helicopter mid-flight and floated off): a pilot lands
       first, a passenger waits for the ground */
    { VehicleInfo av; if (b->vehicle && world_vehicle_read(b->vehicle, &av)) {
        float up[3] = {av.pos[0], av.pos[1] + 0.5f, av.pos[2]}, dn[3] = {av.pos[0], av.pos[1] - 4.0f, av.pos[2]}, h[3], n[3];
        if (!eng_ray(up, dn, h, n)) {
            if (b->airHoldLogged++ < 1) rt_log("vehicles: %s stays in - in the air (%s)", b->name, why);
            if (b->vehRole == VR_DRIVER && b->hState != HS_EXIT) { b->hState = HS_EXIT; b->hStateAt = now; }
            return;
        } } }
    b->airHoldLogged = 0;
    b->lastVeh = b->vehicle; b->vehZoneAt = 0; b->vehWaitUntil = 0; b->vehParkAt = 0;
    int ok = eng_exit_vehicle(b->player);
    if (!ok) press(b, BC2_EiaChangeVehicle);
    if (!ok && ++b->exitFails >= 4) {   /* the game does not let go of it: give up on the vehicle state */
        rt_log("vehicles: %s could not get out of the vehicle (%s) - left as it is", b->name, why);
        b->exitFails = 0; b->vehRole = VR_NONE; b->vehicle = 0; b->vehSince = 0; b->vehCheckAt = now + 60; b->lastVeh = 0;
        b->exitBlockUntil = now + 45;   /* ride it out quietly for a while (1.1 retried every 3 s forever) */
        return;
    }
    if (ok || b->exitFails == 1) rt_log("vehicles: %s gets out (%s)%s", b->name, why, ok ? "" : " - exit call refused, pressing ChangeVehicle");
    if (ok) { b->exitFails = 0; b->vehRole = VR_NONE; b->vehicle = 0; b->vehCheckAt = now + 6; b->pathLen = 0; b->stuckCount = 0; b->frozenSince = now; }
    else b->vehEnterUntil = now + 1.0;   /* retry shortly */
}

/* ---------------------------------------------------------------- shooting from vehicles (1.0.1)
   Targets: enemy soldiers on foot and enemy-crewed vehicles in sight. Turrets: the first bot in each seat of a vehicle
   type finds its turret (turret.c: which transform turns when it moves an input, and which input / which way), then
   aims it with a P controller on the turret's angle relative to the hull and fires when it is on target. A seat
   without a turret that can be found falls back: a ground vehicle's driver turns the hull onto the target. */
float tur_rel0(int i); float tur_pit0(int i);
typedef struct TurretCal { uint32_t data, calVeh; int seat, state, yawAct, pitchAct, havePitch, retests; float yawSign, pitchSign, yawOff, pitchOff; TurPath yaw, pitch;
    /* 1.5: the transform path can differ between vehicles of a type (live 1.4: T-54 seat 0 at depth 1 +0x380 on one,
       depth 2 +0x10 on another) - each vehicle tested keeps its own */
    uint32_t ents[8]; TurPath entYaw[8], entPitch[8]; int entHavePitch[8], entN; } TurretCal;
static TurretCal g_turCal[96]; static int g_turCalN; static int g_turOwner = -1; static double g_turOwnerAt;
static TurretCal *turcal_for(uint32_t data, int seat, int create)
{
    for (int i = 0; i < g_turCalN; i++) if (g_turCal[i].data == data && g_turCal[i].seat == seat) return &g_turCal[i];
    if (!create || g_turCalN >= 96) return NULL;
    TurretCal *t = &g_turCal[g_turCalN++]; memset(t, 0, sizeof *t); t->data = data; t->seat = seat; return t;
}
/* line of sight from a vehicle: the ray starts outside its own hull (from inside it hit the vehicle itself, so 1.1's
   gunners never saw anyone) */
static int veh_los(const float *eye, const float *target, float targetRadius)
{
    float d[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]}, len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len < 7) return 1;
    float k = fminf(5.5f, len * 0.4f) / len, st[3] = {eye[0] + d[0] * k, eye[1] + d[1] * k, eye[2] + d[2] * k};
    return los_clear(st, target, targetRadius);
}
static int vehicle_target(Bot *b, const float *pos, float range, float *out, int *isVeh)
{
    float best = range; int found = 0;
    float eye[3] = {pos[0], pos[1] + 2.2f, pos[2]};
    for (int i = 0; i < g_world.obs.count; i++) {
        const BC2ObservedPlayer *t = &g_world.obs.players[i];
        if (t->team < 1 || (int)t->team > nteams() || (int)t->team == b->team || !t->soldierValid || !t->transformValid || t->health <= 0) continue;
        float d = dist3(t->position, pos); if (d > best || d < 3) continue;
        float c[3] = {t->position[0], t->position[1] + 1.2f, t->position[2]};
        if (!veh_los(eye, c, 2.0f)) continue;
        best = d; memcpy(out, c, 12); *isVeh = 0; found = 1; b->tTgtRef = t->pointer; b->tTgtVeh = 0;
    }
    /* enemy vehicles count as closer (armour first) */
    for (int i = 0; i < g_world.vehCount; i++) {
        const VehicleInfo *v = &g_world.veh[i];
        if (v->kind == VK_FIXED && v->seats <= 1) continue;
        uint32_t occ = 0; for (int s2 = 0; s2 < v->seats && !occ; s2++) occ = eng_vehicle_occupant(v->entity, s2);
        if (!occ) continue;
        int tm = player_team(occ); if (tm < 1 || tm == b->team) continue;
        VehicleInfo now; if (!world_vehicle_read(v->entity, &now)) continue;
        float d = dist3(now.pos, pos) * 0.7f; if (d > best || d < 3) continue;
        float c[3] = {now.pos[0], now.pos[1] + 1.5f, now.pos[2]};
        if (!veh_los(eye, c, 4.5f)) continue;
        best = d; memcpy(out, c, 12); *isVeh = 1; found = 1; b->tTgtRef = v->entity; b->tTgtVeh = 1;
    }
    return found;
}
/* direction from the gun to the target as angles in the hull frame */
static void hull_angles(const VehicleInfo *vi, const float *from, const float *to, float flip, float *rel, float *pit)
{
    float d[3] = {(to[0] - from[0]) * flip, (to[1] - from[1]) * flip, (to[2] - from[2]) * flip};
    float l0 = d[0] * vi->right[0] + d[1] * vi->right[1] + d[2] * vi->right[2];
    float l1 = d[0] * vi->up[0] + d[1] * vi->up[1] + d[2] * vi->up[2];
    float l2 = d[0] * vi->fwd[0] + d[1] * vi->fwd[1] + d[2] * vi->fwd[2];
    *rel = atan2f(l0, l2); *pit = atan2f(l1, hypotf(l0, l2));
}
/* returns 1 while the seat's turret test is running (a driver holds still for it) */
static int turret_calibrating(Bot *b) { return b->tStage > 0 && g_turOwner == (int)(b - g_bots); }
static void turret_release(Bot *b) { if (g_turOwner == (int)(b - g_bots)) { g_turOwner = -1; tur_done(); } b->tStage = 0; }

/* the turret test for this bot's seat; returns the calibration once known */
/* VIEW: the seat's own aim direction (the input's yaw / pitch, what the mouse moves) - BC2 turrets may simply follow it */
#define TUR_VIEW_YAW 100
#define TUR_VIEW_PITCH 101
static void tur_apply(Bot *b, int act, float v)
{
    if (act == TUR_VIEW_YAW) { b->yaw = b->tBaseYaw + v; while (b->yaw < 0) b->yaw += TWO_PI; while (b->yaw >= TWO_PI) b->yaw -= TWO_PI; }
    else if (act == TUR_VIEW_PITCH) b->pitch = fmaxf(-1.2f, fminf(1.2f, b->tBasePitch + v * 0.5f));
    else if (act >= 0 && act < BC2_EiaUndefined) b->levels[act] = v;
}
static TurretCal *turret_think(Bot *b, double now, const VehicleInfo *vi, int seat, int steerAct)
{
    TurretCal *t = turcal_for(vi->data, seat, 1);
    if (!t) return NULL;
    if (t->state) { if (b->tStage) turret_release(b); return t->state > 0 ? t : NULL; }
    int me = (int)(b - g_bots);
    if (g_turOwner >= 0 && g_turOwner != me && now - g_turOwnerAt < 40 && g_bots[g_turOwner].used && g_bots[g_turOwner].vehicle) return NULL;   /* someone else is testing */
    int heli = vi->kind == VK_HELI;
    if (!heli && seat == 0 && b->tSpeed > 1.5f) { if (b->tStage > 1) b->tStage = 1; return NULL; }   /* a driver tests standing still; gunners also on the move */
    if (heli) { const DriveCalib *hc = calib_for(vi->data, 0); if (!hc || hc->ok <= 0) return NULL; }   /* helicopters: after the pilot's own test */
    if (g_turOwner != me) { g_turOwner = me; g_turOwnerAt = now; b->tStage = 0; tur_done(); }
    /* live 1.2: the Abrams, T-90 and AH-64 gunner turrets turn with Roll and elevate with Pitch - tried first */
    static const int yawList[2][5] = {{BC2_EiaRoll, TUR_VIEW_YAW, BC2_EiaCameraYaw, BC2_EiaYaw, -1}, {BC2_EiaRoll, TUR_VIEW_YAW, BC2_EiaYaw, BC2_EiaCameraYaw, -1}};
    static const int pitchList[4] = {BC2_EiaPitch, TUR_VIEW_PITCH, BC2_EiaCameraPitch, -1};
    const int *yl = yawList[seat == 0 ? 0 : 1];
    double tt = now - b->tAt;
    switch (b->tStage) {
    case 0: case 1: {
        int r = tur_search(vi->entity, vi->pos, 120);
        if (r < 0) { t->state = -1; rt_log("vehicles: '%s' seat %d - no turret transform found (%s)", vi->name, seat, seat == 0 && !heli ? "the driver will turn the hull at targets" : "gunner fires where it looks"); turret_release(b); return NULL; }
        if (r == 0) { b->tStage = 1; return NULL; }
        if (!b->tLogged++) rt_log("vehicles: '%s' seat %d - %d candidate transform(s), testing the turret inputs", vi->name, seat, r);
        b->tStage = 2; b->tAct = 0; b->tAt = now; tur_baseline(vi->entity, vi); return NULL; }
    case 2:   /* settle (the last input still lands for a frame or two), then drop anything that moves by itself */
        if (tt < 0.2) { b->tBase = 0; return NULL; }
        if (!b->tBase) { b->tBase = 1; tur_baseline(vi->entity, vi); return NULL; }
        if (tt < 0.6) return NULL;
        tur_mark_noisy(vi->entity, vi, 0.04f);
        b->tBaseYaw = b->yaw; b->tBasePitch = b->pitch;
        while (yl[b->tAct] >= 0 && yl[b->tAct] == steerAct) b->tAct++;
        if (yl[b->tAct] < 0) { t->state = -1; rt_log("vehicles: '%s' seat %d - no input turned a turret", vi->name, seat); turret_release(b); return NULL; }
        b->tStage = 3; b->tAt = now; return NULL;
    case 3: {
        tur_apply(b, yl[b->tAct], 0.6f);
        float amt; int c = tur_moved(vi->entity, vi, 0, &amt);
        if (c >= 0 && fabsf(amt) > 0.15f) { b->tCand = c; b->tD1 = amt; b->tStage = 4; b->tAt = now; return NULL; }
        if (tt > 1.6) { tur_apply(b, yl[b->tAct], 0); b->tAct++; b->tStage = 2; b->tAt = now; tur_baseline(vi->entity, vi); }
        return NULL; }
    case 4: {   /* back the other way: it has to follow */
        tur_apply(b, yl[b->tAct], yl[b->tAct] == TUR_VIEW_YAW ? 0.0f : -0.6f);
        float back = tur_change(vi->entity, vi, b->tCand, 0);
        if (fabsf(back) < fabsf(b->tD1) * 0.4f) { b->tStage = 10; b->tAt = now; return NULL; }   /* and once more the first way */
        if (tt > 2.0) { tur_apply(b, yl[b->tAct], 0); b->tAct++; b->tStage = 2; b->tAt = now; tur_baseline(vi->entity, vi); }
        return NULL; }
    case 10: {   /* third push: something else moving by chance (another seat's turret) does not follow three times */
        tur_apply(b, yl[b->tAct], 0.6f);
        float again = tur_change(vi->entity, vi, b->tCand, 0);
        if (again * b->tD1 > 0 && fabsf(again) > fabsf(b->tD1) * 0.7f) {
            tur_apply(b, yl[b->tAct], 0);
            const TurPath *pp = tur_path(b->tCand); if (!pp) { t->state = -1; turret_release(b); return NULL; }
            t->yaw = *pp; t->yawAct = yl[b->tAct]; t->yawSign = b->tD1 > 0 ? 1.0f : -1.0f;
            /* 1.6.4: the part's own forward axis need not be the gun's (a model turned 90 or 180 degrees): BC2 spawns turrets
               facing the hull's front, so where this part pointed before the test (to the nearest quarter turn) is "ahead".
               1.6.3 took the part's axis as the barrel - tanks fired sideways or behind */
            { float r0 = tur_rel0(b->tCand); t->yawOff = r0 > 90 ? 0 : roundf(r0 / 1.5707963f) * 1.5707963f;
              if (fabsf(t->yawOff) > 0.01f) rt_log("vehicles: '%s' seat %d - turret part is turned %.0f degrees from the gun - corrected", vi->name, seat, t->yawOff * 57.2958f); }
            rt_log("vehicles: '%s' seat %d - turret turns with %s (%+.0f), transform depth %d at +0x%x", vi->name, seat, t->yawAct == TUR_VIEW_YAW ? "the aim (view)" : t->yawAct == BC2_EiaCameraYaw ? "CameraYaw" : t->yawAct == BC2_EiaYaw ? "Yaw" : "Roll", t->yawSign, pp->depth, pp->off[pp->depth]);
            b->tStage = 5; b->tAct = 0; b->tAt = now; tur_baseline(vi->entity, vi); return NULL;
        }
        if (tt > 2.0) { tur_apply(b, yl[b->tAct], 0); b->tAct++; b->tStage = 2; b->tAt = now; tur_baseline(vi->entity, vi); }
        return NULL; }
    case 5:
        if (tt < 0.2) { b->tBase = 0; return NULL; }
        if (!b->tBase) { b->tBase = 1; tur_baseline(vi->entity, vi); return NULL; }
        if (tt < 0.6) return NULL;
        tur_mark_noisy(vi->entity, vi, 0.03f);
        b->tBaseYaw = b->yaw; b->tBasePitch = b->pitch;
        b->tStage = pitchList[b->tAct] >= 0 && pitchList[b->tAct] != steerAct ? 6 : 8; b->tAt = now; return NULL;
    case 6: {   /* up, then down: a gun resting at its lowest (or highest) elevation does not move one way (1.4 tried one way only:
                   "no elevation found" on the T-54, the Gaz-69 and the Huey) */
        float push = tt < 0.65 ? 0.6f : -0.6f;
        tur_apply(b, pitchList[b->tAct], push);
        float amt; int c = tur_moved(vi->entity, vi, 1, &amt);
        if (c >= 0 && fabsf(amt) > 0.05f) { b->tCand = c; b->tD1 = amt; b->tPush = push; b->tStage = 7; b->tAt = now; tur_baseline(vi->entity, vi); return NULL; }
        if (tt > 1.4) { tur_apply(b, pitchList[b->tAct], 0); b->tAct++; b->tStage = 5; b->tAt = now; tur_baseline(vi->entity, vi); }
        return NULL; }
    case 7: {   /* the other way: it has to follow back */
        tur_apply(b, pitchList[b->tAct], -b->tPush);
        float back = tur_change(vi->entity, vi, b->tCand, 1);
        if (back * b->tD1 < 0 && fabsf(back) > 0.03f) {
            const TurPath *pp = tur_path(b->tCand);
            if (pp) { t->pitch = *pp; t->pitchAct = pitchList[b->tAct]; { float p0 = tur_pit0(b->tCand); t->pitchOff = p0 > 90 || fabsf(p0) < 0.3f ? 0 : p0; } t->pitchSign = (b->tD1 > 0) == (b->tPush > 0) ? 1.0f : -1.0f; t->havePitch = 1;
                rt_log("vehicles: '%s' seat %d - gun elevates with %s (%+.0f)", vi->name, seat, t->pitchAct == TUR_VIEW_PITCH ? "the aim (view)" : t->pitchAct == BC2_EiaCameraPitch ? "CameraPitch" : "Pitch", t->pitchSign); }
            b->tStage = 8; return NULL;
        }
        if (tt > 1.8) { tur_apply(b, pitchList[b->tAct], 0); b->tAct++; b->tStage = 5; b->tAt = now; tur_baseline(vi->entity, vi); }
        return NULL; }
    default:
        t->state = 1; t->calVeh = vi->entity; turret_release(b);
        { int k = 0; while (k < t->entN && t->ents[k] != vi->entity) k++;
          if (k == t->entN) { if (t->entN < 8) t->entN++; else k = (int)(rt_rand() % 8); }
          t->ents[k] = vi->entity; t->entYaw[k] = t->yaw; t->entPitch[k] = t->pitch; t->entHavePitch[k] = t->havePitch; }
        rt_log("vehicles: '%s' seat %d - turret ready%s", vi->name, seat, t->havePitch ? "" : " (no elevation found: aims level)"); b->tLogged = 0;
        return t;
    }
}

/* aim this seat's weapon at the best target and fire. Returns 1 when a target is engaged. */
/* another bot in this vehicle is running its turret test: hold still so its measurement is clean */
static int enemy_heli(Bot *b, const VehicleInfo *self, float range, float *out, uint32_t *ent);
static int veh_test_running(uint32_t veh, const Bot *b)
{
    return g_turOwner >= 0 && g_turOwner != (int)(b - g_bots) && g_bots[g_turOwner].used && g_bots[g_turOwner].vehicle == veh && g_bots[g_turOwner].tStage > 1;
}
static int vehicle_shoot(Bot *b, double now, const VehicleInfo *vi, int seat, int steerAct, float flip)
{
    /* speed of the vehicle (turret test waits for a stop on the ground) */
    { float d = dist3(vi->pos, b->tPrevPos); memcpy(b->tPrevPos, vi->pos, 12); b->tSpeed = b->tSpeed * 0.8f + fminf(d * 30.0f, 40) * 0.2f; }
    TurretCal *t = turret_think(b, now, vi, seat, steerAct);
    if (turret_calibrating(b) || veh_test_running(vi->entity, b)) return 0;
    const TurPath *yp = t ? &t->yaw : NULL, *pp = t ? &t->pitch : NULL; int hp = t ? t->havePitch : 0, known = 0;
    if (t) for (int k = 0; k < t->entN; k++) if (t->ents[k] == vi->entity) { yp = &t->entYaw[k]; pp = &t->entPitch[k]; hp = t->entHavePitch[k]; known = 1; break; }
    float tp[3]; int isVeh = 0;
    float range = vi->kind == VK_HELI ? 300 : seat == 0 ? 220 : 180;
    if (now >= b->shootAt) {   /* re-pick the target 3x a second */
        b->shootAt = now + 0.33;
        float hp[3];
        uint32_t he = 0;
        if (vi->kind == VK_HELI && !veh_on_ground(vi->pos) && enemy_heli(b, vi, 450, hp, &he)) {   /* helicopter gunners: enemy helicopters first */
            memcpy(b->tTarget, hp, 12); b->engageUntil = now + 1.0; b->tTgtRef = he; b->tTgtVeh = 1;
        } else if (vehicle_target(b, vi->pos, range, tp, &isVeh)) { memcpy(b->tTarget, tp, 12); b->engageUntil = now + 1.0; }
    }
    if (now > b->engageUntil) { /* no target: bring the turret back to the front */
        if (t && t->yawAct < BC2_EiaUndefined) { float r, p; if (tur_read(vi->entity, yp, vi, &r, &p)) b->levels[t->yawAct] = fmaxf(-1, fminf(1, t->yawSign * wrap_pi(0 - wrap_pi(r - t->yawOff)) * 2.0f)); }
        b->viewOffY *= 0.98f; b->viewOffP *= 0.98f;
        return 0;
    }
    /* follow the target every frame between re-picks */
    if (b->tTgtRef) {
        if (b->tTgtVeh) { VehicleInfo tv; if (world_vehicle_read(b->tTgtRef, &tv)) { b->tTarget[0] = tv.pos[0]; b->tTarget[1] = tv.pos[1] + 1.5f; b->tTarget[2] = tv.pos[2]; } }
        else for (int i = 0; i < g_world.obs.count; i++) { const BC2ObservedPlayer *op = &g_world.obs.players[i];
            if (op->pointer == b->tTgtRef && op->transformValid) { b->tTarget[0] = op->position[0]; b->tTarget[1] = op->position[1] + 1.2f; b->tTarget[2] = op->position[2]; break; } }
    }
    {   /* target velocity (for the lead) */
        float k = b->tgtSeenAt > 0 && now - b->tgtSeenAt > 0.01 && now - b->tgtSeenAt < 1.0 ? 1.0f / (float)(now - b->tgtSeenAt) : 0;
        if (dist3(b->tTarget, b->tgtPrev) > 30) k = 0;   /* a new target */
        for (int j = 0; j < 3; j++) b->tgtVel[j] = k ? b->tgtVel[j] * 0.8f + (b->tTarget[j] - b->tgtPrev[j]) * k * 0.2f : 0;
        memcpy(b->tgtPrev, b->tTarget, 12); b->tgtSeenAt = now;
    }
    memcpy(tp, b->tTarget, 12);
    {   /* lead the target and hold over for the drop: main guns / heavy guns ~300 m/s, machine guns ~600 m/s */
        float tf = dist3(vi->pos, tp) / (seat == 0 || vi->kind == VK_HELI ? 300.0f : 600.0f);
        for (int j = 0; j < 3; j++) tp[j] += b->tgtVel[j] * tf;
        tp[1] += 0.5f * 9.81f * tf * tf;
    }
    float gun[3] = {vi->pos[0] + vi->up[0] * 2.4f, vi->pos[1] + vi->up[1] * 2.4f, vi->pos[2] + vi->up[2] * 2.4f};
    int mainGun = seat == 0 && vi->kind != VK_HELI && veh_armoured(vi->name);
    float wantRel, wantPit; hull_angles(vi, gun, tp, flip, &wantRel, &wantPit);
    /* the soldier's own view (what a seat without a turret transform uses) */
    b->yaw = yaw_from_heading(heading_to(gun, tp)); b->pitch = atan2f(tp[1] - gun[1], dist2d(gun, tp)) * (float)g_cfg.pitchSign;
    if (!t) { b->levels[BC2_EiaFire] = (rt_rand() % 3) != 0; return 1; }   /* fire where it looks */
    float r, p; if (!tur_read(vi->entity, yp, vi, &r, &p)) {
        if (0) {}
        if (!known && t->retests < 8) { t->retests++; t->state = 0; rt_log("vehicles: '%s' seat %d - turret transform not found on this one - testing it", vi->name, seat); }
        return 1; }
    r = wrap_pi(r - t->yawOff);
    float ey = wrap_pi(wantRel - r);
    float slew = fabsf(wrap_pi(r - b->tPrevRel)); b->tPrevRel = r;
    if (t->yawAct == TUR_VIEW_YAW) {
        /* the turret follows the aim: aim at the target, plus a small learned offset once the turret has settled */
        if (slew < 0.004f && fabsf(ey) < 0.5f) b->viewOffY = fmaxf(-0.6f, fminf(0.6f, b->viewOffY + ey * 0.15f * t->yawSign));
        b->yaw += b->viewOffY; while (b->yaw < 0) b->yaw += TWO_PI; while (b->yaw >= TWO_PI) b->yaw -= TWO_PI;
    } else {
        /* PI on a rate input: the integral takes out the lag on a moving target / moving vehicle */
        if (fabsf(ey) < 0.15f) b->tIy = fmaxf(-0.4f, fminf(0.4f, b->tIy + ey * 0.033f * 4.0f)); else b->tIy = 0;
        b->levels[t->yawAct] = fmaxf(-1, fminf(1, t->yawSign * (ey * 2.5f + b->tIy)));
    }
    /* on another vehicle of the type: check the turret really follows (the path may point elsewhere on this one) */
    if (b->tVerVeh != vi->entity) { b->tVerVeh = vi->entity; b->tVerOk = vi->entity == t->calVeh || known; b->tVerRel0 = r; b->tVerExp = 0; }
    if (t->yawAct == TUR_VIEW_YAW) b->tVerOk = 1;   /* follows the aim: nothing to check */
    if (!b->tVerOk) {
        b->tVerExp += b->levels[t->yawAct] * t->yawSign * 0.033f;
        if (fabsf(b->tVerExp) > 0.5f) {
            float got = wrap_pi(r - b->tVerRel0);
            if (got * b->tVerExp > 0 && fabsf(got) > 0.05f) b->tVerOk = 1;
            else if (t->retests++ < 8) { t->state = 0; b->tVerVeh = 0; rt_log("vehicles: '%s' seat %d - the turret found on another one does not follow here - testing again", vi->name, seat); return 1; }
            else b->tVerOk = 1;
        }
    }
    float ep = 0;
    if (hp) { float r2, p2; if (tur_read(vi->entity, pp, vi, &r2, &p2)) { p2 -= t->pitchOff; ep = wantPit - p2;
            if (t->pitchAct == TUR_VIEW_PITCH) {
                if (slew < 0.004f && fabsf(ep) < 0.4f) b->viewOffP = fmaxf(-0.4f, fminf(0.4f, b->viewOffP + ep * 0.15f * t->pitchSign));
                b->pitch = fmaxf(-1.2f, fminf(1.2f, b->pitch + b->viewOffP));
            } else {
                if (fabsf(ep) < 0.15f) b->tIp = fmaxf(-0.4f, fminf(0.4f, b->tIp + ep * 0.033f * 4.0f)); else b->tIp = 0;
                b->levels[t->pitchAct] = fmaxf(-1, fminf(1, t->pitchSign * (ep * 3.0f + b->tIp)));
            } } }
    /* 1.4 fired only inside ~1.5 m of the aim point and drivers' tanks almost never fired on the move. A main gun's shell
       bursts: ~3 m and a degree will do; a machine gun sprays in bursts once it is close */
    float dT = fmaxf(15, dist3(gun, tp)), tol = mainGun ? 0.018f + 3.0f / dT : 0.03f + 2.0f / dT;
    if (fabsf(ey) < tol && fabsf(ep) < tol * 1.6f) {
        if (mainGun) b->levels[BC2_EiaFire] = 1;
        else { if (now > b->burstUntil + 0.25) b->burstUntil = now + randr(0.4f, 1.1f); if (now < b->burstUntil) b->levels[BC2_EiaFire] = 1; }
    }
    return 1;
}
/* a hull-fixed gun (helicopter rockets, a tank without a found turret): fire when the nose is on the target */
static int nose_on(const VehicleInfo *vi, const float *tp, float flip, float tolH, float tolP)
{
    float rel, pit; float gun[3] = {vi->pos[0], vi->pos[1] + 1.0f, vi->pos[2]};
    hull_angles(vi, gun, tp, flip, &rel, &pit);
    return fabsf(rel) < tolH && fabsf(pit) < tolP;
}

/* ---------------------------------------------------------------- helicopters (1.0, experimental)
   A bot pilot learns the controls with a short test (which input lifts, which pitches / rolls / turns the
   helicopter, and which way), then flies by holding altitude over the ground, levelling the cyclic and pointing the
   nose at the next point: lift off, climb to ~35 m above the ground, cruise to the flag, put down 40-70 m short of
   it for passengers, take off again and circle the flag shooting what the nose points at, then go on to the next
   flag. A pilot that cannot control it lands and gets out; three crashes of a type stop bots using it. */
static float ground_below(const float *p, float *agl)
{
    float a[3] = {p[0], p[1] - 0.5f, p[2]}, c[3] = {p[0], p[1] - 300.0f, p[2]}, h[3], n[3];
    if (eng_ray(a, c, h, n)) { *agl = p[1] - h[1]; return h[1]; }
    /* nothing below: sitting on the ground (the origin is at the skids) - look from just above it */
    float a2[3] = {p[0], p[1] + 1.0f, p[2]}, c2[3] = {p[0], p[1] - 1.5f, p[2]};
    if (eng_ray(a2, c2, h, n)) { *agl = fmaxf(0, p[1] - h[1]); return h[1]; }
    *agl = 100; return p[1] - 100;
}
static void heli_set(Bot *b, int st, double now) { b->hState = st; b->hStateAt = now; }
/* an enemy helicopter in the air and in sight, within range */
static int enemy_heli(Bot *b, const VehicleInfo *self, float range, float *out, uint32_t *ent)
{
    float best = range; int found = 0;
    float eye[3] = {self->pos[0], self->pos[1] + 1.0f, self->pos[2]};
    for (int i = 0; i < g_world.vehCount; i++) {
        const VehicleInfo *v = &g_world.veh[i];
        if (v->entity == self->entity) continue;
        const DriveCalib *cc = calib_for(v->data, 0);
        if (v->kind != VK_HELI && !(cc && cc->air)) continue;
        uint32_t pil = eng_vehicle_occupant(v->entity, 0); if (!pil) continue;
        int tm = player_team(pil); if (tm < 1 || tm == b->team) continue;
        VehicleInfo nv; if (!world_vehicle_read(v->entity, &nv) || veh_on_ground(nv.pos)) continue;
        float d = dist3(nv.pos, self->pos); if (d > best) continue;
        float c[3] = {nv.pos[0], nv.pos[1] + 1.0f, nv.pos[2]};
        if (!veh_los(eye, c, 6.0f)) continue;
        best = d; memcpy(out, c, 12); found = 1; if (ent) *ent = v->entity;
    }
    return found;
}
static int heli_land_spot(const float *flag, const float *from, float *out)
{
    /* open, flat ground 40-70 m short of the flag on the side we come from, nothing overhead */
    float dx = from[0] - flag[0], dz = from[2] - flag[2], l = hypotf(dx, dz); if (l < 1) { dx = 1; dz = 0; l = 1; }
    dx /= l; dz /= l;
    for (int k = 0; k < 14; k++) {
        float a = (k / 2) * 0.35f * (k & 1 ? 1 : -1), r = 40 + (k % 3) * 15.0f;
        float c = cosf(a), s2 = sinf(a), ux = dx * c - dz * s2, uz = dx * s2 + dz * c;
        float want[3] = {flag[0] + ux * r, flag[1] + 40, flag[2] + uz * r}, top[3] = {want[0], want[1] + 60, want[2]}, g[3], n[3];
        if (!eng_ray(top, (float[3]){want[0], want[1] - 120, want[2]}, g, n) || n[1] < 0.93f) continue;   /* flat */
        float up2[3] = {g[0], g[1] + 1.5f, g[2]}, sky[3] = {g[0], g[1] + 50, g[2]}, h2[3], n2[3];
        if (eng_ray(up2, sky, h2, n2)) continue;   /* roof / tree overhead */
        int clear = 1;
        for (int d = 0; d < 4 && clear; d++) { float ang = d * 1.5708f; float a2[3] = {g[0], g[1] + 2.0f, g[2]}, b2[3] = {g[0] + sinf(ang) * 9, g[1] + 2.0f, g[2] + cosf(ang) * 9};
            if (eng_ray(a2, b2, h2, n2)) clear = 0; }
        if (!clear) continue;
        memcpy(out, g, 12); return 1;
    }
    return 0;
}
static void heli_brain(Bot *b, double now, float dt, const VehicleInfo *vi, int f, float distFlag, double inFor)
{
    DriveCalib *c = calib_for(vi->data, 1);
    if (!c) { vehicle_leave(b, now, "calibration table full"); return; }
    c->air = 1;   /* it flies: whatever the scan calls it later, it is a helicopter */
    if (!c->ok && !c->presetHeli) {
        /* the BC2 helicopters all fly the same way (live 1.0-1.2 tests of the AH-64 and Mi-28: Throttle +1 climbs, +Pitch
           noses down, +Roll lifts the right side, +Yaw turns towards -heading): take off straight away; a crash with
           these runs the full control test next time */
        c->presetHeli = 1; c->ok = 1; c->collSign = 1; c->pitchSign = -1; c->rollSign = 1; c->yawSign = -1;
        rt_log("vehicles: '%s' - using the known BC2 helicopter controls", vi->name);
    }
    float agl; float gy = ground_below(vi->pos, &agl); (void)gy;
    float sdt = dt > 0.005f ? dt : 1.0f / 30;
    float pitch = asinf(fmaxf(-1, fminf(1, vi->fwd[1]))), roll = asinf(fmaxf(-1, fminf(1, vi->right[1])));
    float heading = atan2f(vi->fwd[0], vi->fwd[2]);
    float vy = (vi->pos[1] - b->hPrevY) / sdt, pr = (pitch - b->hPrevPitch) / sdt, rr = (roll - b->hPrevRoll) / sdt, yr = wrap_pi(heading - b->hPrevHeading) / sdt;
    float vx = (vi->pos[0] - b->hPrevPos[0]) / sdt, vz = (vi->pos[2] - b->hPrevPos[2]) / sdt;
    if (inFor < 0.2) { vy = pr = rr = yr = vx = vz = 0; }
    b->hPrevY = vi->pos[1]; b->hPrevPitch = pitch; b->hPrevRoll = roll; b->hPrevHeading = heading; memcpy(b->hPrevPos, vi->pos, 12);
    if (inFor < 8 && agl < 2.0f) {   /* a teammate is walking over to the gunner / passenger seats: wait for them on the pad */
        int boarding = 0;
        for (int k = 0; k < MAX_BOTS; k++) { Bot *o = &g_bots[k]; if (o != b && o->used && o->vehRole == VR_PASSENGER && o->vehicle == vi->entity && !o->vehSince) boarding++; }
        if (boarding) { b->hStateAt = 0; b->levels[BC2_EiaThrottle] = 0; return; }
    }
    double t = now - b->hStateAt;
    if (inFor < 0.1 || b->hStateAt <= 0) { heli_set(b, c->ok > 0 ? HS_CLIMB : HS_TEST_LIFT, now); b->hBias = 0; b->hHaveLand = 0; t = 0; }
    /* a crash on the way (upside down / hitting the ground fast) is counted against this type */
    if (fabsf(roll) > 1.2f || fabsf(pitch) > 1.2f) {
        if (b->hState != HS_EXIT) { c->crashes++; rt_log("vehicles: %s lost control of '%s' (pitch %.2f roll %.2f) - %d crash(es) with this type", b->name, vi->name, pitch, roll, c->crashes);
            if (c->presetHeli == 1) { c->presetHeli = 2; c->ok = 0; c->collSign = c->pitchSign = c->rollSign = c->yawSign = 0; c->rollAction = c->yawAction = 0; rt_log("vehicles: '%s' - testing its controls next time", vi->name); } }
        heli_set(b, HS_EXIT, now);
    }
    /* --- the controllers --- */
    float wantAlt = 0, wantPitch = 0, wantHeading = heading; int altHold = 0, land = 0, rawCyclic = 0;
    float steer = 0;
    if (b->hState == HS_TEST_LIFT) {
        /* full collective until it rises 3 m; if it does not, try the other way */
        float dir = t < 5 ? 1.0f : -1.0f;
        b->levels[BC2_EiaThrottle] = dir;
        if (agl > 3.0f && vy > 0.3f) { c->collSign = dir; rt_log("vehicles: helicopter test - '%s' lifts with Throttle %+.0f", vi->name, dir); heli_set(b, HS_TEST_PITCH, now); }
        else if (t > 10) { c->ok = -1; rt_log("vehicles: helicopter test - '%s' does not lift with Throttle - bots will not fly it", vi->name); heli_set(b, HS_EXIT, now); }
        return;
    }
    if (b->hState >= HS_TEST_PITCH && b->hState <= HS_TEST_YAW) {
        altHold = 1; wantAlt = 14;
        static float start[MAX_BOTS];
        int bi = (int)(b - g_bots);
        float cur = b->hState == HS_TEST_PITCH ? pitch : b->hState == HS_TEST_ROLL ? roll : heading;
        if (agl < 10) b->hStateAt = now;
        if (agl < 10 || t < 1.0) start[bi] = cur;            /* value just before the test input */
        else if (t < 1.6) rawCyclic = b->hState;            /* test input applied below */
        else {
            float dv = b->hState == HS_TEST_YAW ? wrap_pi(cur - start[bi]) : cur - start[bi];
            float sign = fabsf(dv) < 0.02f ? 0 : dv > 0 ? 1.0f : -1.0f;
            const char *nm = b->hState == HS_TEST_PITCH ? "Pitch (nose up)" : b->hState == HS_TEST_ROLL ? "Roll (right side up)" : "Yaw (heading)";
            if (sign == 0 && b->hState != HS_TEST_PITCH && !c->tryAlt) {
                /* the rudder / bank may be on the A-D (Strafe) input instead: try that once */
                c->tryAlt = 1; rt_log("vehicles: helicopter test - '%s' did not respond to %s - trying Strafe", vi->name, nm); heli_set(b, b->hState, now);
            } else if (sign == 0) { c->ok = -1; rt_log("vehicles: helicopter test - '%s' did not respond to %s - bots will not fly it, landing", vi->name, nm); heli_set(b, HS_EXIT, now); }
            else {
                int act = c->tryAlt ? BC2_EiaStrafe : b->hState == HS_TEST_ROLL ? BC2_EiaRoll : BC2_EiaYaw;
                if (b->hState == HS_TEST_PITCH) c->pitchSign = sign; else if (b->hState == HS_TEST_ROLL) { c->rollSign = sign; c->rollAction = act; } else { c->yawSign = sign; c->yawAction = act; }
                c->tryAlt = 0;
                rt_log("vehicles: helicopter test - '%s' +input %d changes %s by %+.2f", vi->name, b->hState == HS_TEST_PITCH ? BC2_EiaPitch : act, nm, dv);
                if (b->hState == HS_TEST_YAW) { c->ok = 1; rt_log("vehicles: helicopter '%s' calibrated - bots will fly it", vi->name); heli_set(b, HS_CLIMB, now); }
                else heli_set(b, b->hState + 1, now);
            }
        }
    }
    const float *goal = f >= 0 ? g_world.cps[f].pos : vi->pos;
    if (f != b->hFlag) {   /* new job from the director: forget the old landing spot, fly there */
        b->hFlag = f; b->hHaveLand = 0;
        if (b->hState == HS_LOITER || b->hState == HS_CRUISE) { heli_set(b, HS_CLIMB, now); b->vehProgressAt = now; }
    }
    switch (b->hState) {
    case HS_CLIMB:
        altHold = 1; wantAlt = 35; wantHeading = heading_to(vi->pos, goal);
        if (agl > 25) heli_set(b, HS_CRUISE, now);
        break;
    case HS_CRUISE: {
        altHold = 1; wantAlt = 35;
        /* terrain ahead: look 60 m along the way and climb over it */
        float ah[3] = {vi->pos[0] + sinf(heading) * 60, vi->pos[1], vi->pos[2] + cosf(heading) * 60}, agl2; float g2 = ground_below((float[3]){ah[0], vi->pos[1] + 80, ah[2]}, &agl2);
        if (g2 + 30 > vi->pos[1] - agl + wantAlt) wantAlt = g2 + 30 - (vi->pos[1] - agl);
        if (!b->hHaveLand && f >= 0) b->hHaveLand = heli_land_spot(goal, vi->pos, b->hLand);
        const float *to = b->hHaveLand ? b->hLand : goal;
        float d = dist2d(vi->pos, to);
        wantHeading = heading_to(vi->pos, to);
        float fwdSpeed = vx * sinf(heading) + vz * cosf(heading), want = fminf(28.0f, d * 0.18f);
        if (fabsf(wrap_pi(wantHeading - heading)) > 0.5f) want = 3;
        wantPitch = -fmaxf(-0.25f, fminf(0.30f, (want - fwdSpeed) * 0.03f));
        int passengers = 0; for (int s2 = 2; s2 < vi->seats; s2++) if (eng_vehicle_occupant(vi->entity, s2)) passengers++;   /* seat 1 is the gunner: stays */
        if (d < 25 && fabsf(fwdSpeed) < 6) { if (b->hHaveLand && passengers) heli_set(b, HS_LAND, now); else { heli_set(b, HS_LOITER, now); b->hLoiterUntil = now + randr(60, 100); } }
        if (now - b->vehProgressAt > 60) { rt_log("vehicles: %s is not getting closer in '%s' - landing", b->name, vi->name); heli_set(b, HS_LAND, now); b->hHaveLand = 0; }
        break; }
    case HS_LAND:
        land = 1; wantHeading = heading;
        if (agl < 1.6f && fabsf(vy) < 1.0f) heli_set(b, HS_GROUND, now);
        break;
    case HS_GROUND: {
        land = 1;
        int passengers = 0; for (int s2 = 2; s2 < vi->seats; s2++) if (eng_vehicle_occupant(vi->entity, s2)) passengers++;   /* seat 1 is the gunner: stays */
        if (c->ok <= 0 || now - b->vehProgressAt > 90 || f < 0) { heli_set(b, HS_EXIT, now); break; }
        if (t > 4 && (!passengers || t > 15)) { heli_set(b, HS_CLIMB, now);   /* a passenger that stays (a human) does not ground it */ b->hHaveLand = 0; b->vehProgressAt = now; b->hLoiterUntil = now + randr(60, 100); }
        break; }
    case HS_LOITER: {
        /* over the flag: attack runs on what is there - dive at the target nose-first, fire the rockets when the nose
           is on it, break away past 70 m and come round again; nothing there: circle the flag at 45 m */
        altHold = 1; wantAlt = 45;
        float fwdSpeed = vx * sinf(heading) + vz * cosf(heading);
        float tp[3]; int isVeh = 0;
        if (now >= b->shootAt) { b->shootAt = now + 0.33; if (vehicle_target(b, vi->pos, 320, tp, &isVeh)) { memcpy(b->tTarget, tp, 12); b->engageUntil = now + 1.5; } }
        if (now < b->engageUntil && dist2d(b->tTarget, goal) < 260) {
            float d = dist2d(vi->pos, b->tTarget);
            if (b->runOut) {
                wantHeading = heading_to(b->tTarget, vi->pos); wantPitch = -0.25f; wantAlt = 40;
                if (d > 190) b->runOut = 0;
            } else {
                wantHeading = heading_to(vi->pos, b->tTarget);
                float elev = atan2f(b->tTarget[1] - vi->pos[1], d);
                wantPitch = fmaxf(-0.55f, fminf(0.05f, elev)); wantAlt = 32;
                if (fabsf(wrap_pi(wantHeading - heading)) > 0.4f) wantPitch = -fmaxf(-0.2f, fminf(0.2f, (6 - fwdSpeed) * 0.03f));   /* turn in first */
                if (d < 70) b->runOut = 1;
                { float tf = dist3(vi->pos, b->tTarget) / 150.0f, aimp[3] = {b->tTarget[0], b->tTarget[1] + 0.5f * 9.81f * tf * tf, b->tTarget[2]};
                  if (nose_on(vi, aimp, 1.0f, 0.02f + 3.0f / fmaxf(30, d), 0.03f + 3.0f / fmaxf(30, d))) b->levels[BC2_EiaFire] = 1; }
            }
        } else {
            b->runOut = 0;
            b->hOrbit += sdt * 0.12f;
            float op[3] = {goal[0] + cosf(b->hOrbit) * 80, goal[1], goal[2] + sinf(b->hOrbit) * 80};
            wantHeading = heading_to(vi->pos, op);
            wantPitch = -fmaxf(-0.2f, fminf(0.2f, (12 - fwdSpeed) * 0.03f));
        }
        b->vehProgressAt = now;
        if (now > b->hLoiterUntil) { heli_set(b, HS_CLIMB, now); b->hHaveLand = 0; }
        if (f >= 0 && g_world.cps[f].owner == b->team && now > b->hLoiterUntil - 30 && now > b->engageUntil) b->hLoiterUntil = now;   /* flag taken, nothing to shoot: next */
        break; }
    case HS_EXIT:
        land = 1;
        if (agl < 1.8f && fabsf(vy) < 1.5f) { vehicle_leave(b, now, "landed"); return; }
        break;
    default: break;
    }
    /* air to air (1.2): an enemy helicopter up and in sight within 500 m takes priority. Stand off at 150-320 m at its
       height, nose on it, rockets when lined up - the gunner (seat 1) engages it too. */
    if ((b->hState == HS_CRUISE || b->hState == HS_LOITER) && agl > 12) {
        if (now >= b->airAt) {
            b->airAt = now + 0.3; b->airOn = enemy_heli(b, vi, 500, b->airPt, NULL);
            if (b->airOn) {
                float k = b->airSeenAt > 0 && now - b->airSeenAt < 1.0 ? 1.0f / (float)(now - b->airSeenAt) : 0;
                for (int j = 0; j < 3; j++) b->airVel[j] = k ? b->airVel[j] * 0.5f + (b->airPt[j] - b->airPrev[j]) * k * 0.5f : 0;
                memcpy(b->airPrev, b->airPt, 12); b->airSeenAt = now;
            } else b->airSeenAt = 0;
        }
        if (b->airOn) {
            /* lead: rockets ~150 m/s */
            float tf = dist3(vi->pos, b->airPrev) / 150.0f;
            for (int j = 0; j < 3; j++) b->airPt[j] = b->airPrev[j] + b->airVel[j] * ((float)(now - b->airSeenAt) + tf);
            b->airPt[1] += 0.5f * 9.81f * tf * tf;   /* rockets fall: 1.2 aimed straight at the other helicopter and hit low */
            float d = dist2d(vi->pos, b->airPt), fwdSpeed = vx * sinf(heading) + vz * cosf(heading);
            float groundY = vi->pos[1] - agl;
            altHold = 1; land = 0; wantAlt = fmaxf(30, fminf(70, b->airPt[1] - groundY));
            wantHeading = heading_to(vi->pos, b->airPt);
            if (d > 320) wantPitch = -0.22f;
            else if (d < 150) wantPitch = 0.12f;
            else wantPitch = fmaxf(-0.15f, fminf(0.15f, atan2f(b->airPt[1] - vi->pos[1], d) * 0.5f - fwdSpeed * 0.02f));
            if (nose_on(vi, b->airPt, 1.0f, 0.008f + 2.5f / fmaxf(40, d), 0.015f + 2.5f / fmaxf(40, d))) b->levels[BC2_EiaFire] = 1;
            b->vehProgressAt = now;
            if (!b->airLogged) { b->airLogged = 1; static int dfLogs; if (dfLogs++ < 20) rt_log("vehicles: %s engages an enemy helicopter %.0f m away", b->name, d); }
        }
    } else b->airOn = 0;
    if (!b->airOn) b->airLogged = 0;
    /* 1.6.3: keep inside the battlefield - the box round every flag, M-COM and spawn plus 60 m. Outside it BC2 counts down
       and kills the pilot ("return to the battlefield"): turn back for the middle of the map */
    if (g_world.boundsValid && !land && agl > 6) {
        const float m = 60;
        float cx = (g_world.boundsMin[0] + g_world.boundsMax[0]) * 0.5f, cz = (g_world.boundsMin[2] + g_world.boundsMax[2]) * 0.5f;
        int out = vi->pos[0] < g_world.boundsMin[0] - m || vi->pos[0] > g_world.boundsMax[0] + m || vi->pos[2] < g_world.boundsMin[2] - m || vi->pos[2] > g_world.boundsMax[2] + m;
        int nearEdge = vi->pos[0] < g_world.boundsMin[0] - m * 0.4f || vi->pos[0] > g_world.boundsMax[0] + m * 0.4f || vi->pos[2] < g_world.boundsMin[2] - m * 0.4f || vi->pos[2] > g_world.boundsMax[2] + m * 0.4f;
        if (out || (nearEdge && fabsf(wrap_pi(wantHeading - heading_to(vi->pos, (float[3]){cx, 0, cz}))) > 1.6f)) {
            float ctr[3] = {cx, vi->pos[1], cz};
            wantHeading = heading_to(vi->pos, ctr); altHold = 1; if (wantAlt < 35) wantAlt = 35;
            wantPitch = fabsf(wrap_pi(wantHeading - heading)) > 0.6f ? 0.0f : -0.12f;   /* turn on the spot, then fly back in (nose down = forward) */
            if (now - b->fenceLogAt > 20) { b->fenceLogAt = now; static int fl; if (fl++ < 12) rt_log("vehicles: %s's helicopter at the edge of the battlefield (%.0f, %.0f) - turning back to the middle", b->name, vi->pos[0], vi->pos[2]); }
        }
    }
    {
        float collSign = c->collSign ? c->collSign : 1.0f;
        float wantVy;
        if (land) wantVy = agl > 8 ? -3.5f : -fmaxf(0.6f, agl * 0.35f);
        else if (altHold) wantVy = fmaxf(-4.0f, fminf(5.0f, (wantAlt - agl) * 0.5f));
        else wantVy = 0;
        b->hBias += (wantVy - vy) * 0.12f * sdt; if (b->hBias > 0.8f) b->hBias = 0.8f; if (b->hBias < -0.8f) b->hBias = -0.8f;
        b->levels[BC2_EiaThrottle] = fmaxf(-1, fminf(1, collSign * ((wantVy - vy) * 0.25f + b->hBias)));
        if (land && agl < 1.0f) b->levels[BC2_EiaThrottle] = -collSign * 0.3f;
        if (c->pitchSign) b->levels[BC2_EiaPitch] = fmaxf(-1, fminf(1, c->pitchSign * ((wantPitch - pitch) * 2.0f - pr * 0.5f)));
        int rollAct = c->rollAction ? c->rollAction : BC2_EiaRoll, yawAct = c->yawAction ? c->yawAction : BC2_EiaYaw;
        if (c->rollSign)  b->levels[rollAct] = fmaxf(-1, fminf(1, c->rollSign * ((0 - roll) * 2.0f - rr * 0.5f)));
        if (c->yawSign) { steer = fmaxf(-1, fminf(1, wrap_pi(wantHeading - heading) * 1.5f - yr * 0.3f)); b->levels[yawAct] = c->yawSign * steer; }
        /* control test: only the tested axis gets the fixed input, the rest keeps it level */
        if (rawCyclic == HS_TEST_PITCH) b->levels[BC2_EiaPitch] = 0.6f;
        else if (rawCyclic == HS_TEST_ROLL) b->levels[c->tryAlt ? BC2_EiaStrafe : BC2_EiaRoll] = 0.6f;
        else if (rawCyclic == HS_TEST_YAW) b->levels[c->tryAlt ? BC2_EiaStrafe : BC2_EiaYaw] = 0.8f;
    }
    /* the preset controls do not turn this one (full rudder, no turn for 3 s): test its controls in the air */
    if (c->presetHeli == 1 && agl > 10) {
        if (fabsf(steer) > 0.8f && fabsf(yr) < 0.05f) b->hNoTurn += sdt; else b->hNoTurn = 0;
        if (b->hNoTurn > 3) {
            b->hNoTurn = 0; c->presetHeli = 2; c->ok = 0; c->pitchSign = c->rollSign = c->yawSign = 0; c->rollAction = c->yawAction = 0;
            rt_log("vehicles: '%s' does not turn with the usual controls - testing them", vi->name);
            heli_set(b, HS_TEST_PITCH, now);
        }
    }
    if (b->hState == HS_CRUISE) {   /* targets of opportunity on the way */
        float tp[3]; int isVeh = 0;
        if (now >= b->shootAt) { b->shootAt = now + 0.33; if (vehicle_target(b, vi->pos, 300, tp, &isVeh)) { memcpy(b->tTarget, tp, 12); b->engageUntil = now + 1.0; } }
        if (now < b->engageUntil) { float tf = dist3(vi->pos, b->tTarget) / 150.0f, aimp[3] = {b->tTarget[0], b->tTarget[1] + 0.5f * 9.81f * tf * tf, b->tTarget[2]};
            if (nose_on(vi, aimp, 1.0f, 0.03f + 3.0f / fmaxf(30, dist3(vi->pos, b->tTarget)), 0.04f + 3.0f / fmaxf(30, dist3(vi->pos, b->tTarget)))) b->levels[BC2_EiaFire] = 1; }
    }
    b->yaw = yaw_from_heading(heading); b->pitch = 0;
    if (g_cfg.verbose && now - b->steerAt > 2) { b->steerAt = now;
        rt_log("vehicles: %s heli state %d agl %.1f vy %.1f pitch %.2f->%.2f roll %.2f hdg %.2f->%.2f thr %.2f pin %.2f rin %.2f yin %.2f flag %d %.0f m, timer %.0f", b->name, b->hState, agl, vy, pitch, wantPitch, roll,
               heading, wantHeading, b->levels[BC2_EiaThrottle], b->levels[BC2_EiaPitch], b->levels[BC2_EiaRoll], b->levels[BC2_EiaYaw], f, distFlag, b->hLoiterUntil - now); }
}

/* in a vehicle: drive or ride. levels are vehicle controls here. */
/* 1.5: stopping. Live 1.4 (Phu Bai Valley): a T-54 held on Brake while passengers boarded reversed 37 m, and one held on
   Brake for its turret test backed 20 m - BC2's Brake input is the reverse pedal once the vehicle is slow. Phoenix never
   presses it now: a moving vehicle is slowed with the throttle against its motion, a slow one is just left at 0. */
static void veh_hold(Bot *b, const DriveCalib *c)
{
    float fs = b->vFwd * (c && c->fwdSign < 0 ? -1.0f : 1.0f);   /* + = the way the throttle drives it */
    b->levels[BC2_EiaThrottle] = fabsf(fs) > 0.9f ? fmaxf(-1, fminf(1, -fs * 0.3f)) : 0;
    b->levels[BC2_EiaBrake] = 0; b->levels[BC2_EiaHandBrake] = 0;
}
/* 1.6.6: what is in front of a wheeled vehicle's bumper, below the 1.2 m wall feelers: a barrel, a road blocker, a
   low wall (rays at 0.5 m over the ground there), and the ground every 0.8 m - a step or a stair up or down, a kerb
   too high for it, a drop. Returns the distance to the first hazard (1e9 none); kind 1 low obstacle, 2 step, 3 drop. */
static float veh_hazard(const VehicleInfo *vi, float heading, float bumper, float stepMax, float range, int *kind, float *where)
{
    float sx = sinf(heading), sz = cosf(heading), fx = vi->pos[0] + sx * bumper, fz = vi->pos[2] + sz * bumper, h[3], n[3];
    float a[3] = {fx, vi->pos[1] + 1.0f, fz}, c[3] = {fx, vi->pos[1] - 6.0f, fz};
    *kind = 0;
    if (!eng_ray(a, c, h, n) || h[1] > vi->pos[1] + 0.5f) return 1e9f;   /* no ground found there (or our own hull) */
    float g0 = h[1], best = 1e9f;
    for (int k = -1; k <= 1; k++) {
        float hh = heading + k * 0.32f, p0[3] = {fx, g0 + 0.5f, fz}, p1[3] = {fx + sinf(hh) * range, g0 + 0.5f, fz + cosf(hh) * range};
        if (eng_ray(p0, p1, h, n) && n[1] < 0.6f) { float d = dist2d(p0, h) * (k ? 1.2f : 1.0f); if (d < best) { best = d; *kind = 1; memcpy(where, h, 12); } }
    }
    float prev = g0, prevD = 0;
    for (float d = 0.8f; d <= range && d < best; d += 0.8f) {
        float x = fx + sx * d, z = fz + sz * d, t0[3] = {x, prev + 1.3f, z}, t1[3] = {x, prev - 2.5f, z};
        if (!eng_ray(t0, t1, h, n)) {
            float u0[3] = {x, prev + 4.0f, z};
            if (eng_ray(u0, t1, h, n) && h[1] > prev + 1.0f) { best = d; *kind = 1; } else { best = d; *kind = 3; }   /* a wall / a drop */
            where[0] = x; where[1] = prev; where[2] = z; break;
        }
        float dy = h[1] - prev;
        if (fabsf(dy) > stepMax && (fabsf(dy - prevD) > stepMax * 0.6f || fabsf(dy) > 0.9f)) { best = d; *kind = 2; where[0] = x; where[1] = h[1]; where[2] = z; break; }
        prevD = dy; prev = h[1];
    }
    return best;
}
/* 1.6.10: vehicle routes are kept and reused - vehicles drive the same few trips (base to the first flags, flag to flag),
   and a route planned once from a spot to a flag is good for the next vehicle there, without waiting for the planner.
   A kept route is used only if every leg of it is still on the drive map (cheap, no rays); a hazard cut on it drops it. */
#define VRC_N 48
typedef struct { float from[3], pts[MAX_PATH_PTS * 3]; int n, flag, partial; float gap; double at; unsigned level; } VRoute;
static VRoute g_vrc[VRC_N];
static int vroute_find(const float *from, int flag, double now)
{
    int best = -1; float bd = 15.0f;
    for (int i = 0; i < VRC_N; i++) { VRoute *r = &g_vrc[i];
        if (!r->n || r->flag != flag || r->level != g_world.levelSerial || now - r->at > 600 || fabsf(r->from[1] - from[1]) > 4) continue;
        float d = dist2d(r->from, from); if (d < bd) { bd = d; best = i; } }
    if (best < 0) return -1;
    VRoute *r = &g_vrc[best];
    if (!navg_drive_line_ok(from, r->pts)) return -1;
    for (int k = 1; k < r->n; k++) if (!navg_drive_line_ok(&r->pts[(k - 1) * 3], &r->pts[k * 3])) { r->n = 0; return -1; }
    return best;
}
static void vroute_store(const float *from, int flag, const float *pts, int n, int partial, float gap, double now)
{
    if (n < 2 || partial) return;
    int slot = 0; for (int i = 0; i < VRC_N; i++) {
        if (g_vrc[i].n && g_vrc[i].flag == flag && dist2d(g_vrc[i].from, from) < 15.0f) { slot = i; break; }
        if (g_vrc[i].at < g_vrc[slot].at) slot = i; }
    VRoute *r = &g_vrc[slot]; memcpy(r->from, from, 12); memcpy(r->pts, pts, (size_t)n * 12); r->n = n; r->flag = flag; r->partial = partial;
    r->gap = gap; r->at = now; r->level = g_world.levelSerial;
}
static void vroute_drop_near(const float *p) { for (int i = 0; i < VRC_N; i++) for (int k = 0; k < g_vrc[i].n; k++) if (dist2d(&g_vrc[i].pts[k * 3], p) < 6.0f) { g_vrc[i].n = 0; break; } }

static int name_has(const char *n, const char *sub)
{
    char u[64]; int i = 0; for (; n[i] && i < 63; i++) u[i] = (char)toupper((unsigned char)n[i]); u[i] = 0;
    return strstr(u, sub) != NULL;
}
static void vehicle_brain(Bot *b, double now, float dt, uint32_t veh)
{
    memset(b->levels, 0, sizeof b->levels);
    VehicleInfo vi; memset(&vi, 0, sizeof vi);
    if (!world_vehicle_read(veh, &vi)) return;
    {   /* speed along the hull's +z (smoothed), for braking with the throttle and the drive checks */
        double t = now - b->vPrevAt;
        if (b->vPrevVeh == veh && t > 0.015 && t < 0.5) {
            float vf = ((vi.pos[0] - b->vPrev[0]) * sinf(vi.heading) + (vi.pos[2] - b->vPrev[2]) * cosf(vi.heading)) / (float)t;
            if (fabsf(vf) < 60) { b->vFwdPrev = b->vFwd; b->vFwd = b->vFwd * 0.6f + vf * 0.4f; }
        } else if (b->vPrevVeh != veh) { b->vFwd = b->vFwdPrev = 0; }
        memcpy(b->vPrev, vi.pos, 12); b->vPrevAt = now; b->vPrevVeh = veh;
    }
    const VehicleInfo *known = veh_info(veh);
    if (known) { char nm[48]; memcpy(nm, known->name, 48); memcpy(vi.name, nm, 48); vi.kind = known->kind; vi.team = known->team; }
    { const DriveCalib *ac = calib_for(vi.data, 0); if (ac && ac->air) vi.kind = VK_HELI; }
    memcpy(b->pos, vi.pos, 12);
    if (!b->vehRole || b->vehicle != veh) {   /* in a vehicle we did not plan (e.g. put there by the game) */
        b->vehicle = veh; b->seat = -1;
        for (int s = 0; s < vi.seats; s++) if (eng_vehicle_occupant(veh, s) == b->player) b->seat = s;
        b->vehRole = vi.kind == VK_EMPLACE ? VR_GUNNER : b->seat == 0 ? VR_DRIVER : VR_PASSENGER; b->vehSince = now; b->vehProgressAt = now; b->vehProgressDist = 1e30f;
        memcpy(b->calibStart, vi.pos, 12); b->calibAt = now; b->steerCalib = 0;
        b->hStateAt = 0; b->hFlag = -2; b->vehEnterUntil = 0;
        if (now < b->exitBlockUntil) { b->vehRole = VR_NONE; b->vehicle = 0; veh_hold(b, calib_for(vi.data, 0)); return; }   /* could not get out: sit */
        if (b->vehRole == VR_DRIVER && (g_cfg.vehicles < 2 || (vi.kind != VK_GROUND && vi.kind != VK_UNKNOWN && !(vi.kind == VK_HELI && g_cfg.helicopters) && !(vi.kind == VK_BOAT && g_cfg.boats)))) { vehicle_leave(b, now, "not a vehicle bots drive"); return; }
    }
    if (!b->vehSince) { b->vehSince = now; b->pathLen = 0; b->pathPending = 0; b->pathAt = 0; memcpy(b->pathFrom, vi.pos, 12); b->noRouteSince = 0; b->routeWaitSince = 0; }   /* 1.6.6: a route from the vehicle, not from where the soldier was */
    if (now < b->vehEnterUntil && b->vehEnterUntil - now < 1.01) { if (b->vehRole == VR_DRIVER) veh_hold(b, calib_for(vi.data, 0)); return; }   /* waiting to retry an exit: stop */
    int f = b->aFlag >= 0 && b->aFlag < g_world.cpCount ? b->aFlag : -1;
    float distFlag = f >= 0 ? dist2d(vi.pos, g_world.cps[f].pos) : 0;
    double inFor = now - b->vehSince;
    /* progress towards the flag (AltBotMod: 35 m in 35 s, else give up) */
    if (f >= 0) { if (distFlag < b->vehProgressDist - 35 || b->vehProgressDist > 1e29f) { b->vehProgressDist = distFlag; b->vehProgressAt = now; } }
    if (vi.kind == VK_EMPLACE || b->vehRole == VR_GUNNER) {   /* a stationary weapon: shoot what comes into range, leave when idle */
        b->vehRole = VR_GUNNER;
        if (vehicle_shoot(b, now, &vi, b->seat >= 0 ? b->seat : 0, -1, 1.0f)) b->emplNoTgtAt = now; else if (!b->emplNoTgtAt || inFor < 0.1) b->emplNoTgtAt = now;
        if (turret_calibrating(b)) return;
        const char *why = NULL;
        if (now - b->emplNoTgtAt > 15) why = "nothing to shoot from the stationary weapon";
        else if (inFor > 90) why = "moving on from the stationary weapon";
        else if (f >= 0 && g_world.cps[f].owner != b->team && distFlag > 90 && inFor > 25) why = "needed at the flag";
        else for (int k = 0; k < g_world.obs.count && !why; k++) { const BC2ObservedPlayer *t = &g_world.obs.players[k];
            if (t->team >= 1 && (int)t->team != b->team && t->soldierValid && t->transformValid && t->health > 0 && !t->entry && dist2d(t->position, vi.pos) < 10) why = "enemy too close to the stationary weapon"; }
        if (why) { b->emplNoTgtAt = 0; b->emplUntil = now + 25; vehicle_leave(b, now, why); }
        return;
    }
    if (b->vehRole == VR_PASSENGER) {
        uint32_t driver = eng_vehicle_occupant(veh, 0);
        /* 1.6.8: seat 0 can still name a driver who has got out (or a bot that dropped its vehicle job): passengers and
           gunners then sat in a parked vehicle until "the ride is not getting closer" 35 s later. A driver counts only
           while the game has him controlling this vehicle. */
        if (driver && eng_player_controlled(driver) != veh) driver = 0;
        { Bot *db = driver ? bot_of_player(driver) : NULL; if (db && (db->vehRole != VR_DRIVER || db->vehicle != veh)) driver = 0; }
        /* no driver: take the wheel when bots drive this kind and the flag is worth driving to, else get out */
        if (!driver && vi.kind != VK_HELI && inFor > 1.5 && g_cfg.vehicles >= 2 && (vi.kind == VK_GROUND || vi.kind == VK_UNKNOWN) &&
            f >= 0 && distFlag >= (veh_armoured(vi.name) ? 80 : 110) && !veh_banned(veh, now)) {
            const DriveCalib *dc = calib_for(vi.data, 0);
            if (!(dc && (dc->ok < 0 || dc->crashes >= 3)) && eng_vehicle_seat_allowed(veh, b->player, 0)) {
                /* out of the passenger seat first, then into the driver's (enterVehicle from inside another seat is not
                   something the game expects); if the driver's seat refuses, the bot is simply on foot next to it */
                if (!eng_exit_vehicle(b->player)) { vehicle_leave(b, now, "no driver"); return; }
                if (!eng_enter_vehicle(b->player, veh, 0) || eng_vehicle_occupant(veh, 0) != b->player) {
                    rt_log("vehicles: %s got out of '%s' (no driver) - the driver's seat would not take it", b->name, vi.name);
                    b->lastVeh = veh; b->vehRole = VR_NONE; b->vehicle = 0; b->vehSince = 0; b->vehCheckAt = now + 6; b->pathLen = 0; b->stuckCount = 0; b->frozenSince = now;
                    return;
                }
                {
                    b->seat = 0; b->vehRole = VR_DRIVER; b->vehSince = now; b->vehProgressAt = now; b->vehProgressDist = 1e30f; b->steerCalib = 0; b->reverseUntil = 0; b->unpinTries = 0;
                    b->pathLen = 0; b->pathPending = 0; b->pathAt = 0; memcpy(b->pathFrom, vi.pos, 12); b->noRouteSince = 0; b->routeWaitSince = 0;
                    memcpy(b->calibStart, vi.pos, 12); b->calibAt = now; veh_claim(veh, b, now, 600);
                    rt_log("vehicles: %s moves over to drive '%s' (the driver got out)", b->name, vi.name);
                    return;
                }
            }
        }
        { const DriveCalib *dc = calib_for(vi.data, 0); vehicle_shoot(b, now, &vi, b->seat, -1, dc && dc->fwdSign < 0 ? -1.0f : 1.0f); }   /* gunner seats: aim and fire */
        if (vi.kind == VK_HELI && b->seat == 1) {   /* the helicopter's gunner stays with it */
            if (!driver) { if (inFor > 6) { vehicle_leave(b, now, "no pilot"); return; } }
            else b->vehEnterUntil = 0;
            b->vehProgressAt = now;
            return;
        }
        float down[3] = {vi.pos[0], vi.pos[1] - 5.0f, vi.pos[2]}, up[3] = {vi.pos[0], vi.pos[1] + 0.5f, vi.pos[2]}, hh[3], nn[3];
        if (driver && !eng_ray(up, down, hh, nn)) { b->vehProgressAt = now; return; }   /* airborne: stay in */
        if (f < 0 || distFlag < (vi.kind == VK_HELI ? 110 : 55)) { vehicle_leave(b, now, "close to the flag"); return; }
        if (vi.kind == VK_HELI && driver) {   /* a bot pilot that has landed is dropping us off */
            for (int k = 0; k < MAX_BOTS; k++) if (g_bots[k].used && g_bots[k].player == driver && g_bots[k].hState == HS_GROUND) { vehicle_leave(b, now, "dropped off"); return; }
        }
        if (!driver) { if (inFor > 1.5) { vehicle_leave(b, now, "no driver"); return; } }
        else b->vehEnterUntil = 0;
        /* 1.6.10: a bot driver that is getting somewhere (or waiting for its route) is trusted - a road that goes round, or
           the planner, is not "the ride is not getting closer" (most passenger exits in the harness were this) */
        { Bot *db = driver ? bot_of_player(driver) : NULL; if (db && db->vehRole == VR_DRIVER && now - db->vehProgressAt < 30 && inFor < 180) b->vehProgressAt = now; }
        if (now - b->vehProgressAt > 35 || inFor > 180) { vehicle_leave(b, now, "the ride is not getting closer"); return; }
        return;
    }
    /* ---- driver ---- */
    if (vi.kind == VK_HELI) { heli_brain(b, now, dt, &vi, f, distFlag, inFor); return; }
    if (f < 0) { vehicle_leave(b, now, "no flag to drive to"); return; }
    CapturePoint *cp = &g_world.cps[f];
    /* the gun: turret found by test (once per type) and aimed at the nearest threat; without one, armour turns its
       hull onto the target for a few seconds and fires */
    int engaged = 0, hullAim = 0;
    {
        DriveCalib *c0 = calib_for(vi.data, 1);
        if (c0 && c0->ok > 0) {
            engaged = vehicle_shoot(b, now, &vi, 0, c0->steerAction, c0->fwdSign < 0 ? -1.0f : 1.0f);
            if (turret_calibrating(b)) {
                veh_hold(b, c0); b->levels[c0->steerAction] = 0;
                b->vehProgressAt = now; b->calibAt = now; memcpy(b->calibStart, vi.pos, 12);
                return;
            }
            const TurretCal *tc = turcal_for(vi.data, 0, 0);
            if (engaged && tc && tc->state < 0 && veh_armoured(vi.name) && now > b->engageCool) { b->hullUntil = now + 7; b->engageCool = now + 16; }   /* 7 s on a target, then drive on a while */
            if (engaged && now < b->hullUntil) {
                float vh = vi.heading + (c0->fwdSign < 0 ? PI : 0), err = wrap_pi(heading_to(vi.pos, b->tTarget) - vh);
                veh_hold(b, c0);   /* (1.4 pressed Brake here: the tank backed away from its target) */
                b->levels[c0->steerAction] = fmaxf(-1, fminf(1, err * 2.0f)) * c0->steerSign;
                b->levels[BC2_EiaFire] = fabsf(err) < 0.05f;
                hullAim = 1;
            }
        }
    }
    if (hullAim) { b->vehProgressAt = now; return; }
    /* passengers walking over (at the start, or after a flag fell): wait up to 7 s for them */
    {
        int boarding = 0;
        for (int k = 0; k < MAX_BOTS; k++) { Bot *o = &g_bots[k]; if (o != b && o->used && o->vehRole == VR_PASSENGER && o->vehicle == veh && !o->vehSince && dist2d(o->pos, vi.pos) < 75) boarding++; }
        if (boarding && !b->vehWaitUntil) b->vehWaitUntil = now + 7;
        if (!boarding) b->vehWaitUntil = 0;
        if (inFor < 3 || (boarding && now < b->vehWaitUntil)) { veh_hold(b, calib_for(vi.data, 0)); if (inFor < 8) { b->calibAt = now; memcpy(b->calibStart, vi.pos, 12); } b->vehProgressAt = now; return; }
    }
    /* at the flag: park in the zone - a crew captures from inside the vehicle - until it falls, then on to
       the next flag the director gives. Nothing happening after 20 s: get out and take it on foot. */
    if (distFlag < cp->radius * 0.7f || (cp->owner == b->team && distFlag < cp->radius + 20)) {
        veh_hold(b, calib_for(vi.data, 0)); b->vehProgressAt = now; b->vehProgressDist = distFlag;
        if (!b->vehZoneAt) b->vehZoneAt = now;
        if (cp->owner != b->team && now - b->vehZoneAt > 20 && cp->attacker != b->team) { vehicle_leave(b, now, "flag not capturing from the vehicle - taking it on foot"); return; }
        if (cp->owner == b->team && now - b->vehZoneAt > 25) { vehicle_leave(b, now, "waiting too long for the next flag"); return; }
        return;
    }
    b->vehZoneAt = 0;
    /* the drivable route ends short of the flag (it is in a building, up stairs, off-road): park there and walk */
    if (b->pathLen && !b->pathPartial && b->pathIdx >= b->pathLen - 1 && !b->pathPending && dist2d(vi.pos, &b->path[(b->pathLen - 1) * 3]) < 8 && distFlag < 60) {
        veh_hold(b, calib_for(vi.data, 0)); if (!b->vehParkAt) b->vehParkAt = now + 2;
        if (now > b->vehParkAt) vehicle_leave(b, now, "end of the drivable route - on foot from here");
        return;
    }
    if (now - b->vehProgressAt > 35 && vi.kind != VK_BOAT) { veh_ban(veh, now, 45); vehicle_leave(b, now, "no progress in 35 s"); return; }
    DriveCalib *c = calib_for(vi.data, 1);
    if (!c) { vehicle_leave(b, now, "calibration table full"); return; }
    if (!c->ok && !c->presetGround && (vi.kind == VK_GROUND || vi.kind == VK_BOAT)) {
        /* every BC2 ground vehicle tested live (Abrams, T-90, CAV, Humvee, Cobra, BMD-3, quads) drives the same way: Throttle
           forward along +z, Yaw steers, +1 towards -heading. Drive off at once (1.1's test rolled and reversed for ~8 s);
           the first metres check it and flip a sign if the vehicle disagrees. */
        c->presetGround = 1; c->ok = 1; c->steerAction = BC2_EiaYaw; c->steerSign = -1; c->fwdSign = 1;
    }
    float vehHeading = vi.heading + (c->fwdSign < 0 ? PI : 0);
    /* control test: phase 1 throttle only (which way is forward), phase 2.. which input steers */
    static const int steerCand[3] = {BC2_EiaYaw, BC2_EiaStrafe, BC2_EiaRoll};
    if (c->ok < 0) { vehicle_leave(b, now, "bots cannot drive this vehicle"); return; }
    if (!c->ok) {
        double t = now - b->calibAt;
        if (b->steerCalib == 0) {
            b->levels[BC2_EiaThrottle] = 1;
            if (t > 3.5) {
                float moved = dist2d(vi.pos, b->calibStart);
                if (moved < 2.0f) {   /* wedged, or the engine needed longer: try again later; give up on the type after 3 */
                    if (++c->fails >= 3) c->ok = -1;
                    rt_log("vehicles: driving test - '%s' did not move with Throttle (%.1f m)%s", vi.name, moved, c->ok < 0 ? " three times - bots will not drive it" : " - will try again");
                    vehicle_leave(b, now, "vehicle does not respond"); return; }
                if (vi.pos[1] - b->calibStart[1] > 2.5f) {   /* an unnamed helicopter: hand it to the pilot code */
                    c->air = 1; c->ok = 0; c->collSign = 0;
                    rt_log("vehicles: driving test - '%s' climbed %.1f m: a helicopter%s", vi.name, vi.pos[1] - b->calibStart[1], g_cfg.helicopters ? " - flying it" : " - Helicopters=0, getting out");
                    if (!g_cfg.helicopters) { c->ok = -1; vehicle_leave(b, now, "aircraft"); }
                    else { b->hStateAt = 0; }
                    return;
                }
                float mh = heading_to(b->calibStart, vi.pos);
                c->fwdSign = fabsf(wrap_pi(mh - vi.heading)) > PI / 2 ? -1.0f : 1.0f;
                b->steerCalib = 1; b->calibAt = now; b->calibStartHeading = vi.heading;
                rt_log("vehicles: driving test - '%s' moves with Throttle (%.1f m in %.1f s), model forward %s", vi.name, moved, t, c->fwdSign > 0 ? "+z" : "-z");
                b->unpinTries = 0; memcpy(b->calibStart, vi.pos, 12);
            }
        } else if (b->steerCalib <= 3) {
            int a = steerCand[b->steerCalib - 1];
            if (now < b->reverseUntil) { b->levels[BC2_EiaThrottle] = -1; b->calibAt = now; b->calibStartHeading = vi.heading; memcpy(b->calibStart, vi.pos, 12); return; }
            b->levels[BC2_EiaThrottle] = 0.7f; b->levels[a] = 1;
            if (t > 2.2) {
                float dh = wrap_pi(vi.heading - b->calibStartHeading);
                if (fabsf(dh) <= 0.25f && dist2d(vi.pos, b->calibStart) < 3.0f && b->unpinTries++ < 3) {
                    /* it did not roll (against a wall after the throttle test): back up and try the same input again */
                    b->reverseUntil = now + 0.6; return;
                }
                if (fabsf(dh) > 0.25f) {
                    c->steerAction = a; c->steerSign = dh > 0 ? 1.0f : -1.0f; c->ok = 1;
                    rt_log("vehicles: driving test - '%s' steers with input %d (%s), +1 turns %s (%.2f rad in %.1f s)", vi.name, a,
                           a == BC2_EiaYaw ? "Yaw" : a == BC2_EiaStrafe ? "Strafe" : "Roll", dh > 0 ? "towards +heading" : "towards -heading", dh, t);
                } else { b->steerCalib++; b->calibAt = now; b->calibStartHeading = vi.heading; memcpy(b->calibStart, vi.pos, 12); b->unpinTries = 0; }
            }
        } else {
            if (++c->fails >= 3) c->ok = -1;
            rt_log("vehicles: driving test - no input steered '%s'%s", vi.name, c->ok < 0 ? " (3 tries) - bots will not drive it" : " - will try again");
            b->steerCalib = 0; vehicle_leave(b, now, "cannot steer"); return; }
        return;
    }
    if (vi.kind == VK_BOAT) {   /* boats: steer by the water (1.5) */
        float sdt = dt > 0.005f ? dt : 1.0f / 30, speed = inFor > 0.3 ? dist2d(vi.pos, b->hPrevPos) / sdt : 0; memcpy(b->hPrevPos, vi.pos, 12);
        float wy = vi.pos[1], toFlag = heading_to(vi.pos, cp->pos);
        if (!b->boatLand && (distFlag < 85 || now - b->vehProgressAt > 40)) { b->boatLand = 1; rt_log("vehicles: %s brings the boat in to land, %.0f m from flag %c", b->name, distFlag, 'A' + f); }
        if (now >= b->avoidAt) {
            b->avoidAt = now + 0.3;
            float best = vehHeading, bestS = -1e9f;
            for (int k = -6; k <= 6; k++) {
                float hd = vehHeading + k * 0.3f, run = boat_run(vi.pos, hd, wy, 48);
                float sc = fminf(run, 40) - fabsf(wrap_pi(hd - toFlag)) * 9.0f - fabsf((float)k) * 0.8f - (run < 8 ? 100 : 0);
                if (sc > bestS) { bestS = sc; best = hd; }
            }
            b->escHeading = wrap_pi(best); b->frontClear = boat_run(vi.pos, vehHeading, wy, 24);
        }
        float wantB = b->boatLand ? toFlag : b->escHeading, errB = wrap_pi(wantB - vehHeading);
        float thr = fabsf(errB) < 0.4f ? 1.0f : 0.6f;
        if (!b->boatLand && b->frontClear < 10) thr = 0.4f;
        if (b->boatLand) thr = fminf(thr, 0.7f);
        if (engaged) thr = fminf(thr, 0.6f);
        b->levels[BC2_EiaThrottle] = thr; b->levels[c->steerAction] = fmaxf(-1, fminf(1, errB * 1.8f)) * c->steerSign; b->levels[BC2_EiaBrake] = 0;
        b->pvPrevThr = thr; b->pvPrevHeading = vehHeading; b->pvPrevErr = errB;
        if (!engaged) { b->yaw = yaw_from_heading(wantB); b->pitch = 0; }
        /* aground: the throttle on and not moving - everyone out (near the flag or not) */
        if (thr > 0.5f && speed < 0.7f && inFor > 4) { if (!b->boatAgroundAt) b->boatAgroundAt = now; } else b->boatAgroundAt = 0;
        if (b->boatAgroundAt && now - b->boatAgroundAt > 2.5) { b->boatLand = 0; b->boatAgroundAt = 0; vehicle_leave(b, now, b->boatLand || distFlag < 120 ? "boat landed near the flag" : "boat aground"); return; }
        if (now - b->vehProgressAt > 90) { vehicle_leave(b, now, "the boat is not getting closer"); return; }
        if (g_cfg.verbose && now - b->steerAt > 2) { b->steerAt = now;
            rt_log("vehicles: %s steers a boat at (%.0f, %.0f) heading %.2f want %.2f thr %.2f water ahead %.0f m, flag %c %.0f m%s", b->name, vi.pos[0], vi.pos[2], vehHeading, wantB, thr, b->frontClear, 'A' + f, distFlag, b->boatLand ? " LANDING" : ""); }
        return;
    }
    /* route: the nav path to the flag, followed with a 10 m look-ahead. The request starts where the vehicle was when it
       was asked (1.4 asked again from every new position: a route request restarts when its start moves 4 m, so a vehicle
       rolling on while its long route was searched never got one - half the drives of the 22:54 log were straight lines) */
    float *goal = g_world.cps[f].pos;
    /* 1.6.8: the grid is still being built (a map's first round): there is no drive map to plan on. 1.6.6/1.6.7 counted
       that as "no drivable route" - and 1.6.8's 90 s skip then kept every vehicle parked for minutes after the grid was
       done. Get out without marking the vehicle; bots take it again once the map is ready. */
    if (!navg_ready()) { vehicle_leave(b, now, "the map is still being surveyed - no routes yet"); return; }
    /* 1.6.10: a route kept from an earlier trip from here to this flag: drive it now (the planner refreshes it in 15 s) */
    if (!b->pathLen && !b->pathPending) {
        int k = vroute_find(vi.pos, f, now);
        if (k >= 0) { VRoute *r = &g_vrc[k];
            memcpy(b->path, vi.pos, 12); int n = r->n < MAX_PATH_PTS - 1 ? r->n : MAX_PATH_PTS - 1; memcpy(b->path + 3, r->pts, (size_t)n * 12);
            b->pathLen = n + 1; b->pathIdx = 1; b->pathPartial = 0; b->pastEnd = 0; b->routeLeft = 1e9f; b->pathGap = r->gap; b->noRouteSince = 0; b->pathAt = now; memcpy(b->pathGoal, goal, 12); memcpy(b->pathFrom, vi.pos, 12);
            if (g_cfg.verbose) rt_log("vehicles: %s takes the kept route to flag %c (%d points) - no wait for the planner", b->name, 'A' + f, b->pathLen);
        }
    }
    if (b->pathPending || !b->pathLen || now - b->pathAt > 15 || dist2d(b->pathGoal, goal) > 5) {
        if (!b->pathPending || dist2d(b->pathFrom, vi.pos) > 60) memcpy(b->pathFrom, vi.pos, 12);
        float out[MAX_PATH_PTS * 3]; int deferred = 0;
        int n = navg_ready() ? navg_path_vehicle((int)(b - g_bots), b->pathFrom, goal, out, MAX_PATH_PTS, &deferred) : 0;
        if (n > 0) {
            vroute_store(b->pathFrom, f, out, n, navg_vehicle_partial(), navg_vehicle_gap(), now);   /* 1.6.10: for the next trip */
            /* the planner's points start at the first waypoint (up to 80 m away): the route line starts where the request did */
            int pre = n < MAX_PATH_PTS && dist2d(b->pathFrom, out) > 2.0f;
            if (pre) memcpy(b->path, b->pathFrom, 12);
            memcpy(b->path + pre * 3, out, (size_t)n * 12); n += pre; b->pathLen = n; b->pathIdx = n > 1 ? 1 : 0; b->pathPending = 0; b->pastEnd = 0; b->routeLeft = 1e9f;
            b->pathPartial = navg_vehicle_partial(); b->pathGap = navg_vehicle_gap(); b->noRouteSince = 0;
#ifdef PHX_HARNESS
            if (getenv("PATHDBG")) { char t[600]; int u = 0; for (int i = 0; i < n && u < 560; i++) u += snprintf(t + u, sizeof t - (size_t)u, " (%.0f,%.0f)", b->path[i * 3], b->path[i * 3 + 2]);
                rt_log("PATHDBG %s from (%.0f,%.0f) at (%.0f,%.0f): %d pts%s", b->name, b->pathFrom[0], b->pathFrom[2], vi.pos[0], vi.pos[2], n, t); }
#endif
            if (b->pathGap > 25 && now > b->pathFailLogAt) { b->pathFailLogAt = now + 30;
                rt_log("vehicles: flag %c is off the drive map - %s's '%s' route ends %.0f m short of it, the crew walks from there", 'A' + f, b->name, vi.name, b->pathGap); }
        }
        else if (deferred) { b->pathPending = 1; if (dist2d(b->pathGoal, goal) > 5) b->pathLen = 0; }   /* planner busy: keep the old route to the same flag, else wait */
        else {
            /* 1.6.6: no route is no route - 1.6.5 drove in a straight line at the flag instead (170 times in one round:
               into stairwells, dividers, alleys) */
            b->pathLen = 0; b->pathPending = 0; if (!b->noRouteSince) b->noRouteSince = now;
            if (navg_ready() && now > b->pathFailLogAt) { b->pathFailLogAt = now + 30; rt_log("vehicles: no drivable route for %s's '%s' from (%.0f, %.0f) to flag %c - %s", b->name, vi.name, vi.pos[0], vi.pos[2], 'A' + f, navg_last_error()); }
        }
        memcpy(b->pathGoal, goal, 12);
        if (!b->pathPending) b->pathAt = n > 0 ? now : now - 11;   /* no route: try again in 4 s */
    }
    /* 1.6.6: never drive blind - wait for the planner (25 s at most), and get out when there is no route at all */
    if (!b->pathLen) {
        if (!b->routeWaitSince) b->routeWaitSince = now;
        veh_hold(b, c); b->vehProgressAt = now;
        if (b->noRouteSince && now - b->noRouteSince > 8) { b->noRouteSince = 0; b->routeWaitSince = 0; veh_ban(veh, now, 45); vehicle_leave(b, now, "no drivable route to the flag"); return; }
        /* 1.6.10: the planner still working on it is not "no route": wait up to 60 s, and leave the vehicle unmarked */
        if (now - b->routeWaitSince > (b->pathPending ? 60 : 25)) { b->routeWaitSince = 0; if (!b->pathPending) veh_ban(veh, now, 45); vehicle_leave(b, now, b->pathPending ? "waited 60 s for the planner" : "waited 25 s for a route"); return; }
        if (!engaged) { b->yaw = yaw_from_heading(heading_to(vi.pos, goal)); b->pitch = 0; }
        return;
    }
    b->routeWaitSince = 0;
    /* speed (for the look-ahead and braking into bends) */
    float sdt = dt > 0.005f ? dt : 1.0f / 30, speed = inFor > 0.3 ? dist2d(vi.pos, b->hPrevPos) / sdt : 0;
    if (speed > 40) speed = 40;
    float fwdSpeed = b->vFwd * c->fwdSign;   /* + = forward, the way the throttle drives */
    int tracked = veh_tracked(vi.name);
    if (b->pvPrevThr > 0.4f) { if (!b->thrOnSince) b->thrOnSince = now; } else b->thrOnSince = 0;
    if (c->presetGround == 1 && inFor > 1 && speed > 2.5f && now > b->reverseUntil + 2.5 && now > b->wiggleUntil + 1.5 && now > b->escUntil + 1.0) {
        /* check the preset: speeding up backwards with the throttle held for a while means +z is the rear; turning the
           wrong way means the steering is the other way round. (1.4 flipped on a vehicle still rolling back after a
           reverse and flipped back a second later - Gaz-69 and M151 drove back and forth while it did) */
        if (b->thrOnSince && now - b->thrOnSince > 1.5) {
            if (fwdSpeed < -2.0f && b->vFwd * c->fwdSign < b->vFwdPrev * c->fwdSign - 0.01f) { if (++c->pvBack > 30) { c->fwdSign = -c->fwdSign; c->pvBack = 0; rt_log("vehicles: '%s' drives the other way round - fixed", vi.name); } }
            else if (fwdSpeed > 1.0f && c->pvBack > 0) c->pvBack -= 2;
        }
        float turned = wrap_pi(vehHeading - b->pvPrevHeading);
        if (fabsf(b->pvPrevErr) > 0.35f && fabsf(turned) > 0.002f) c->pvScore += (turned > 0) == (b->pvPrevErr > 0) ? 1 : -1;
        if (c->pvScore <= -25) { c->steerSign = -c->steerSign; c->pvScore = 0; rt_log("vehicles: '%s' steers the other way round - fixed", vi.name); }
        if (c->pvScore >= 40) { c->presetGround = 2; rt_log("vehicles: '%s' drives as expected", vi.name); }
    }
    memcpy(b->hPrevPos, vi.pos, 12);
    /* 1.6.6: follow the route LINE (pure pursuit). 1.6.5 aimed at the first waypoint 8-22 m away, so in a bend it aimed
       at the next corner and cut across the inside - the building, the wall, the alley mouth. Now: the point on the
       route a speed-dependent distance ahead of where the vehicle is along it, never past a sharp corner (it aims just
       beyond the corner instead), and only where the straight line to it stays on the drive map. */
    float target[3]; memcpy(target, goal, 12);
    float bend = 0, offRoute = 0;
    if (b->pathLen >= 2) {
        int k0 = b->pathIdx < 1 ? 1 : b->pathIdx, kBest = k0; float best = 1e30f, proj[3]; memcpy(proj, &b->path[(k0 - 1) * 3], 12);
        for (int k = k0 > 1 ? k0 - 1 : 1; k < b->pathLen && k <= k0 + 6; k++) {
            const float *p0 = &b->path[(k - 1) * 3], *p1 = &b->path[k * 3];
            float sx = p1[0] - p0[0], sz = p1[2] - p0[2], L2 = sx * sx + sz * sz;
            float t = L2 > 1e-4f ? ((vi.pos[0] - p0[0]) * sx + (vi.pos[2] - p0[2]) * sz) / L2 : 1; t = fmaxf(0, fminf(1, t));
            float q[3] = {p0[0] + sx * t, p0[1] + (p1[1] - p0[1]) * t, p0[2] + sz * t}, d = dist2d(q, vi.pos);
            if (d < best - 0.01f || (k > kBest && d < best + 0.5f)) { best = d; kBest = k; memcpy(proj, q, 12); }
        }
        b->pathIdx = kBest; offRoute = best;
        /* 1.6.10: how much route is left, and whether the vehicle has rolled past its end - a tank at 10 m/s passed the
           "within 8 m of the last point" check between two looks, turned round for the point now behind it, and drove
           back and forth there ("driving like ..." on Panama Canal) */
        {
            float rem = dist2d(proj, &b->path[kBest * 3]);
            for (int k = kBest + 1; k < b->pathLen; k++) rem += dist2d(&b->path[(k - 1) * 3], &b->path[k * 3]);
            const float *pl = &b->path[(b->pathLen - 1) * 3], *pp = &b->path[(b->pathLen - 2) * 3];
            b->routeLeft = rem;
            b->pastEnd = kBest >= b->pathLen - 1 && (vi.pos[0] - pl[0]) * (pl[0] - pp[0]) + (vi.pos[2] - pl[2]) * (pl[2] - pp[2]) > 0;
        }
        float La = fmaxf(6.0f, fminf(18.0f, 5.0f + speed * 0.9f)), walked = 0, cur[3]; memcpy(cur, proj, 12);
        float prevHd = heading_to(&b->path[(kBest - 1) * 3], &b->path[kBest * 3]);
        memcpy(target, &b->path[(b->pathLen - 1) * 3], 12);
        for (int k = kBest; k < b->pathLen; k++) {
            const float *p1 = &b->path[k * 3]; float seg = dist2d(cur, p1);
            if (walked + seg >= La) { float t = seg > 1e-3f ? (La - walked) / seg : 0;
                for (int j = 0; j < 3; j++) target[j] = cur[j] + (p1[j] - cur[j]) * t;
                break; }
            walked += seg; memcpy(cur, p1, 12);
            if (k + 1 < b->pathLen) {
                float hd = heading_to(p1, &b->path[(k + 1) * 3]);
                if (fabsf(wrap_pi(hd - prevHd)) > 0.6f && walked > 3.0f) {   /* a corner within reach: just past it */
                    target[0] = p1[0] + sinf(hd) * 2.5f; target[1] = p1[1]; target[2] = p1[2] + cosf(hd) * 2.5f; break; }
                prevHd = hd;
            }
        }
        for (int tries = 0; tries < 3 && !navg_drive_line_ok(proj, target); tries++)
            for (int j = 0; j < 3; j++) target[j] = proj[j] + (target[j] - proj[j]) * 0.5f;
        /* 1.6.10: the whole body must clear the way to the aim point, not just its centre line. Aiming just past a corner
           (or at a point the drive map allows) sent the vehicle diagonally across the bend: its centre passed, its front
           corner caught the building and it stayed there. Every 0.2 s two rays from the front corners of the hull to the
           aim point, offset by half the vehicle's width; blocked, the aim point is pulled back along the route (towards
           the vehicle's own spot on it) until the body clears - the vehicle keeps to its line round the corner. */
        if (now >= b->ppAt) {
            b->ppAt = now + 0.2; b->ppScale = 1.0f;
            int quad = name_has(vi.name, "QUAD") || name_has(vi.name, "ATV");
            float hw = quad ? 0.8f : tracked ? 2.0f : 1.4f, fb = quad ? 0.9f : tracked ? 3.0f : 2.2f;
            float rx = cosf(vehHeading), rz = -sinf(vehHeading), fx = sinf(vehHeading), fz = cosf(vehHeading);
            for (int tries = 0; tries < 4; tries++, b->ppScale *= 0.5f) {
                float t[3]; for (int j = 0; j < 3; j++) t[j] = proj[j] + (target[j] - proj[j]) * b->ppScale;
                if (dist2d(vi.pos, t) < fb + 2.0f) break;   /* close in: nothing left to cut */
                int blocked = 0;
                for (int sd = -1; sd <= 1 && !blocked; sd += 2) {
                    float a[3] = {vi.pos[0] + fx * fb + rx * hw * sd, vi.pos[1] + 0.9f, vi.pos[2] + fz * fb + rz * hw * sd};
                    float e[3] = {t[0] + rx * hw * sd, t[1] + 0.9f, t[2] + rz * hw * sd}, h[3], n[3];
                    if (eng_ray(a, e, h, n) && n[1] < 0.6f) blocked = 1;
                }
                if (!blocked) break;
            }
        }
        if (b->ppScale < 0.99f) for (int j = 0; j < 3; j++) target[j] = proj[j] + (target[j] - proj[j]) * b->ppScale;
        if (offRoute > 5.0f) {   /* drifted off the line (an escape, a push, a bad turn): back to it first */
            float hd = heading_to(&b->path[(kBest - 1) * 3], &b->path[kBest * 3]);
            target[0] = proj[0] + sinf(hd) * 3.0f; target[1] = proj[1]; target[2] = proj[2] + cosf(hd) * 3.0f;
        }
        if (offRoute > 25.0f && now - b->pathAt > 3) { b->pathAt = 0; memcpy(b->pathFrom, vi.pos, 12); }   /* far off: a new route from here */
        if (b->pathPartial && kBest >= b->pathLen - 1 && (dist2d(vi.pos, &b->path[(b->pathLen - 1) * 3]) < 12 || b->pastEnd) && now - b->pathAt > 2) { b->pathAt = 0; memcpy(b->pathFrom, vi.pos, 12); }
        else if (b->pastEnd && now - b->pathAt > 2) { b->pathAt = 0; memcpy(b->pathFrom, vi.pos, 12); }   /* 1.6.10: rolled past the end: plan on from here, never back */
        /* past the end: stop there and wait for the next route (the first 1.6.10 build drove straight on meanwhile - with a
           route that ended short, 35 m off into nowhere); none in 12 s, the crew walks from here */
        /* 1.6.10: never stop - keep driving towards the flag (10 m at a time, kept on the drive map) while the next route
           is planned. Stopping and getting out after 12 s left vehicles abandoned all over the map. */
        if (b->pastEnd) {
            float hf = heading_to(vi.pos, goal);
            target[0] = vi.pos[0] + sinf(hf) * 10; target[1] = vi.pos[1]; target[2] = vi.pos[2] + cosf(hf) * 10;
            for (int tries = 0; tries < 3 && !navg_drive_line_ok(vi.pos, target); tries++) { hf = wrap_pi(hf + (tries & 1 ? -0.8f : 0.8f) * (tries + 1)); target[0] = vi.pos[0] + sinf(hf) * 10; target[2] = vi.pos[2] + cosf(hf) * 10; }
        }
        /* how sharp the route gets 8-35 m ahead (for braking) */
        float along = 0, h0 = heading_to(vi.pos, target); const float *pv = proj;
        for (int k = kBest; k < b->pathLen && along < 35; k++) {
            along += dist2d(pv, &b->path[k * 3]); pv = &b->path[k * 3];
            if (along > 8 && k + 1 < b->pathLen && dist2d(pv, &b->path[(k + 1) * 3]) > 1) { float t = fabsf(wrap_pi(heading_to(pv, &b->path[(k + 1) * 3]) - h0)); if (t > bend) bend = t; }
        }
    } else if (b->pathLen == 1) memcpy(target, b->path, 12);
    float want = heading_to(vi.pos, target);
    /* obstacle avoidance (1.2): three feelers from the front bumper - straight and 26 degrees either side - at 1.2 m;
       an obstacle on the line steers the vehicle to the freer side and slows it down before it hits anything */
    float clearL = 0, clearR = 0;
    if (now >= b->avoidAt) {
        b->avoidAt = now + 0.15;
        float L = fmaxf(7.0f, fminf(20.0f, speed * 1.2f + 6.0f)), clear[3];
        /* 1.6.10: from the vehicle's own bumper - 3 m ahead of the centre suits a tank, but a quad is 2 m long: with a wall
           1-2 m ahead the feeler started beyond it, read "clear" and the quad drove on into the wall at full throttle */
        float bump = name_has(vi.name, "QUAD") || name_has(vi.name, "ATV") ? 1.2f : tracked ? 3.0f : 2.4f;
        for (int k = 0; k < 3; k++) {
            float hh = vehHeading + (k - 1) * 0.45f;
            float a[3] = {vi.pos[0] + sinf(vehHeading) * bump, vi.pos[1] + 1.0f, vi.pos[2] + cosf(vehHeading) * bump}, e[3] = {a[0] + sinf(hh) * L, a[1], a[2] + cosf(hh) * L}, h[3], n[3];
            clear[k] = L;
            if (eng_ray(a, e, h, n) && n[1] < 0.6f) clear[k] = dist2d(a, h);   /* walls, rocks, vehicles - not the slope of a hill */
        }
        /* 1.6.10: a feeler that hit nothing is clear, not "clear for L metres". At low speed L is 7-8 m, under the 8 m
           "something close in front" limit, so a slow vehicle always believed it was blocked, kept the throttle at 0.4 and
           never got faster (Panama Canal: half of all driving samples under 1 m/s, "clear 7.0" in most of them) */
        /* 1.6.10: and the vehicle's own width - two rays straight ahead from the corners of the bumper. The three fanned
           feelers start at the middle of the bumper, so a tree trunk in front of one side of the hull was between them:
           a tank drove straight into a tree it had all the room to miss and stopped there. */
        float hwF = name_has(vi.name, "QUAD") || name_has(vi.name, "ATV") ? 0.8f : tracked ? 1.9f : 1.3f, side2[2] = {L, L};
        for (int sd = 0; sd < 2; sd++) {
            float s = sd ? 1.0f : -1.0f, rx = cosf(vehHeading) * s, rz = -sinf(vehHeading) * s;   /* sd 1: the +heading side (as k=2) */
            float a[3] = {vi.pos[0] + sinf(vehHeading) * bump + rx * hwF, vi.pos[1] + 1.0f, vi.pos[2] + cosf(vehHeading) * bump + rz * hwF};
            float e[3] = {a[0] + sinf(vehHeading) * L, a[1], a[2] + cosf(vehHeading) * L}, h[3], n[3];
            if (eng_ray(a, e, h, n) && n[1] < 0.6f) side2[sd] = dist2d(a, h);
        }
        float cMin = fminf(clear[1], fminf(side2[0], side2[1]));
        b->frontClear = cMin < L ? cMin : 99.0f; clearL = clear[0]; clearR = clear[2];
        float bias = 0;
        if (side2[0] < L || side2[1] < L) {   /* something in front of one side of the hull: steer the other way */
            float side = side2[0] < side2[1] ? 1.0f : -1.0f;
            bias = side * (0.35f + 0.5f * (1.0f - cMin / L));
        } else if (clear[1] < L) {
            float side = clear[0] > clear[2] ? -1.0f : 1.0f;   /* k=0 is towards -heading */
            bias = side * (0.35f + 0.5f * (1.0f - clear[1] / L));
        }
        b->avoidBias = b->avoidBias * 0.5f + bias * 0.5f;
    }
    (void)clearL; (void)clearR;
    /* 1.6.6: hazards the wall feelers cannot see (wheeled vehicles; tanks roll over barrels and kerbs) */
    if (!tracked && now >= b->hazAt && now >= b->reverseUntil && fwdSpeed > -0.5f) {
        b->hazAt = now + 0.25;
        int quad = name_has(vi.name, "QUAD") || name_has(vi.name, "ATV");
        float bumper = quad ? 1.6f : 3.2f, stepMax = quad ? 0.3f : 0.38f, range = fmaxf(5.0f, fminf(10.0f, speed * 0.9f + 4.0f));
        int kind = 0; float where[3] = {0, 0, 0}, hz = veh_hazard(&vi, vehHeading, bumper, stepMax, range, &kind, where);
        if (kind) {
            b->frontClear = fminf(b->frontClear, hz);
            /* on the route itself and close: cut the spot from the drive map and plan round it before the bumper is there */
            if (hz < 7.0f && fabsf(wrap_pi(heading_to(vi.pos, target) - vehHeading)) < 0.45f && now > b->hazCutAt + 4 && dist2d(where, b->hazSpot) > 2.0f) {
                int cut = navg_vehicle_block(where, 1.3f); vroute_drop_near(where);
                /* 1.6.9: follow the obstacle sideways - a row of barriers or a divider across the road is cut as a line, not
                   as one cell the next route then grazes (the harness: the same barrier hit again and again) */
                if (kind == 1) for (int sd = -1; sd <= 1; sd += 2) for (int k = 1; k <= 4; k++) {
                    float px = where[0] + cosf(vehHeading) * sd * 2.5f * k, pz = where[2] - sinf(vehHeading) * sd * 2.5f * k;
                    float a2[3] = {px - sinf(vehHeading) * 1.5f, vi.pos[1] + 0.5f, pz - cosf(vehHeading) * 1.5f}, b2[3] = {px + sinf(vehHeading) * 1.5f, vi.pos[1] + 0.5f, pz + cosf(vehHeading) * 1.5f}, h2[3], n2[3];   /* bumper height above the vehicle's ground */
                    if (!eng_ray(a2, b2, h2, n2) || n2[1] > 0.7f) break;   /* the obstacle ends here */
                    float sp[3] = {px, where[1], pz}; cut += navg_vehicle_block(sp, 1.3f);
                }
                b->hazCutAt = now; memcpy(b->hazSpot, where, 12);
                if (cut) {
                    static const char *kname[] = {"?", "a low obstacle", "a step", "a drop"};
                    static int hl; if (hl++ < 40 || g_cfg.verbose) rt_log("vehicles: %s sees %s %.1f m ahead of '%s' at (%.0f, %.0f) - %d drive cell(s) cut, new route", b->name, kname[kind], hz, vi.name, where[0], where[2], cut);
                    b->pathAt = 0; memcpy(b->pathFrom, vi.pos, 12);
                    /* 1.6.9: and stop on the spot - 1.6.8 drove on along the old route while the new one was planned, into the
                       very barrier it had just seen (the harness: "sees a low obstacle 6.8 m ahead", "stuck" 8 s later) */
                    b->pathLen = 0; b->pathPending = 0; veh_hold(b, c); return;
                }
            }
        }
    }
    /* escaping from a dead end (1.5): turn to the freest heading and drive out along it before taking the route again */
    int escaping = now < b->escUntil;
    if (escaping) want = b->escHeading;
    else if (fabsf(wrap_pi(want - vehHeading)) < 1.2f) want += b->avoidBias;
    float err = wrap_pi(want - vehHeading);
    b->pvPrevHeading = vehHeading; b->pvPrevErr = err;
    float steerCmd = fmaxf(-1, fminf(1, err * 1.8f)) * c->steerSign;
    float thr = fabsf(err) < 0.5f ? 1.0f : fabsf(err) < 1.2f ? 0.65f : 0.45f;
    if (tracked && fabsf(err) > 0.75f && speed < 6) { thr = 0; steerCmd = (err > 0 ? 1.0f : -1.0f) * c->steerSign; }   /* tanks turn on the spot */
    if (bend > 1.0f && speed > 9) thr = 0.0f;          /* hairpin ahead: lift off */
    else if (bend > 0.6f && speed > 12) thr = fminf(thr, 0.45f);
    if (fabsf(err) > 0.9f && fwdSpeed > 10) thr = -0.5f;                 /* too fast for the turn: brake (throttle against the motion) */
    if (!escaping && b->frontClear < 8) { if (fwdSpeed > 5) thr = -0.6f; else thr = fminf(thr, 0.4f); }   /* something close in front: slow, steer round it */
    if (distFlag < 50) thr = fminf(thr, 0.75f);   /* 1.6.10: 50 m / 0.75 (was 80 m / 0.6: half throttle is a crawl in BC2) */
    /* 1.6.10: run out of route gently - slower in the last 30 m, braking from speed in the last 15 m (the flag zone or
       the end of a route that stops short of it), instead of arriving at 10 m/s and overshooting */
    if (b->pathLen >= 2 && !b->pastEnd && !escaping && !b->pathPartial && b->pathGap < 25) {   /* only a route that really ends at the flag */
        if (b->routeLeft < 30) thr = fminf(thr, b->routeLeft < 12 ? 0.5f : 0.75f);
        if (b->routeLeft < 15 && fwdSpeed > 9) thr = -0.5f;
    }
    if (offRoute > 5.0f) thr = fminf(thr, 0.7f);   /* 1.6.6: back onto the route steadily (1.6.10: 0.7, was 0.5) */
    if (engaged) thr = fminf(thr, 0.75f);   /* fighting: ease off so the gun settles (1.6.10: 0.75, was 0.55) */
    if (escaping && now > b->escDriveUntil && fabsf(err) < 0.35f) b->escDriveUntil = now + 2.5;   /* lined up: drive out */
    /* 1.6.10: a wheeled vehicle not yet facing the way out turns by reversing (the wheel the other way swings the nose
       round) - 1.6.9 turned it with the throttle on, which against a wall only pushes the nose into it */
    int escTurnBack = 0;
    if (escaping && fabsf(err) >= 0.35f && now > b->escDriveUntil) { if (tracked) thr = 0; else { thr = -0.6f; escTurnBack = 1; } }
    if (!tracked && !escaping && ((b->frontClear < 2.5f && fabsf(err) > 0.6f) || (b->frontClear < 4.0f && fabsf(err) > 0.9f))) { thr = -0.6f; escTurnBack = 1; }   /* nose at a wall, facing wrong: back round */
    if (escaping && now < b->escDriveUntil) { thr = 0.8f; b->escUntil = fmax(b->escUntil, b->escDriveUntil); }
    /* stuck: pushing without moving for 3 s. 1.4 backed up 0.6 s and drove at the same wall again, every 2.5 s - "forward
       back forward back" for half a minute. Now: find the freest heading (rays round the front half); a tank pivots onto
       it, anything else backs up for a second turning its nose that way, then drives out along it for 2.5 s. Twice at the
       same spot: the spot is cut from the drive map and the route planned again; five times: get out. */
    /* breadcrumbs every 4 m: the way back out (1.6.5) */
    if (b->vTrailVeh != vi.entity) { b->vTrailVeh = vi.entity; b->vTrailN = 0; }
    if (!b->vTrailN || dist2d(vi.pos, b->vTrail[b->vTrailN - 1]) > 4) {
        if (b->vTrailN == 8) { memmove(b->vTrail, b->vTrail + 1, sizeof b->vTrail[0] * 7); b->vTrailN = 7; }
        memcpy(b->vTrail[b->vTrailN++], vi.pos, 12);
    }
    if (now >= b->reverseUntil && now >= b->wiggleUntil) {
        if (thr > 0.3f && speed < 1.0f && !(tracked && fabsf(err) > 0.75f)) b->stuckTime += sdt; else b->stuckTime = fmaxf(0, b->stuckTime - sdt * 2);
        if (b->stuckTime > 3.0f) {
            b->stuckTime = 0;
            if (dist2d(vi.pos, b->revSpot) > 15) { b->revCount = 0; memcpy(b->revSpot, vi.pos, 12); }
            b->revCount++; b->unpinTries++;
            float best = vehHeading + PI, bestClear = -1, bestScore = -1e9f;
            /* 1.6.10: all round, and towards the route - 1.6.9 looked only across the front half and took the freest
               heading whatever the route wanted, so a quad against a wall kept choosing another heading into the wall */
            for (int k = -6; k <= 6; k++) {
                float hh = vehHeading + k * 0.5f, L = 14, r0 = 1.0f;
                float a[3] = {vi.pos[0] + sinf(hh) * r0, vi.pos[1] + 1.0f, vi.pos[2] + cosf(hh) * r0}, e[3] = {a[0] + sinf(hh) * L, a[1], a[2] + cosf(hh) * L}, h[3], n[3];
                float cl = (eng_ray(a, e, h, n) && n[1] < 0.6f) ? dist2d(a, h) : L;
                float sc = cl + 5.0f * cosf(wrap_pi(hh - want)) - (k == 0 ? 6 : 0);   /* not straight on again */
                if (cl >= 4.0f && sc > bestScore) { bestScore = sc; bestClear = cl; best = hh; }
                else if (bestScore < -1e8f && cl > bestClear) { bestClear = cl; best = hh; }
            }
            b->escHeading = wrap_pi(best); b->escUntil = now + 6.0; b->escDriveUntil = 0;
            if (!tracked && b->frontClear < 5) b->reverseUntil = now + 1.0;   /* wheels: back off first, nose swinging towards the way out */
            /* 1.6.5: boxed in (an alley, a dead end: nothing open ahead or to the sides) - back out the way we came, tail
               towards where the vehicle was 10-20 m ago, for up to 4 s, instead of pivoting against the walls */
            if (bestClear < 6 && b->vTrailN > 0) {
                int k = -1; for (int i = b->vTrailN - 1; i >= 0; i--) { float dd = dist2d(vi.pos, b->vTrail[i]); if (dd > 10) { k = i; break; } }
                if (k < 0) k = 0;
                b->escHeading = heading_to(b->vTrail[k], vi.pos); b->reverseUntil = now + 4.0; b->escUntil = now + 8.0;
                static int bo; if (bo++ < 12) rt_log("vehicles: %s boxed in with '%s' at (%.0f, %.0f) - backing out the way it came", b->name, vi.name, vi.pos[0], vi.pos[2]);
            }
            if (b->revCount == 2) {
                float ahead[3] = {vi.pos[0] + sinf(vehHeading) * 5, vi.pos[1], vi.pos[2] + cosf(vehHeading) * 5};
                int n = navg_vehicle_block(ahead, 3.5f); vroute_drop_near(ahead);
                learn_vehicle_stuck(ahead);   /* 1.6.5: kept for the map, cut every round */
                rt_log("vehicles: %s stuck in '%s' at (%.0f, %.0f) - blocked %d drive cell(s) there, new route", b->name, vi.name, ahead[0], ahead[2], n);
                b->pathAt = 0; b->pathLen = 0;
            }
        }
        if (speed > 3) b->unpinTries = 0;
    }
    if (b->unpinTries >= 5) { vehicle_leave(b, now, "stuck"); return; }
    if (now < b->reverseUntil) {
        thr = -0.7f; float e2 = wrap_pi(b->escHeading - vehHeading);
        steerCmd = -fmaxf(-1, fminf(1, e2 * 2.0f)) * c->steerSign;   /* reversing: the wheel the other way swings the nose towards the way out */
    }
    if (escTurnBack && now >= b->reverseUntil) steerCmd = -fmaxf(-1, fminf(1, err * 2.0f)) * c->steerSign;   /* backing round */
    if (thr < 0 && fwdSpeed < -3.0f && now >= b->reverseUntil && !escTurnBack) thr = 0;   /* never keep reversing by accident */
    if (escTurnBack && fwdSpeed < -4.0f) thr = 0;   /* (backing round slowly) */
    b->levels[BC2_EiaThrottle] = thr; b->levels[c->steerAction] = steerCmd; b->levels[BC2_EiaBrake] = 0;
    /* 1.6.10: boost (Sprint, the Shift key) going uphill - when the vehicle points up more than ~4 degrees, or the route
       climbs 1.5 m or more over the next 25 m, with the throttle on and nothing close in front */
    {
        float pitchUp = vi.fwd[1] * (float)c->fwdSign, climb = 0, along = 0; const float *pv = vi.pos;
        for (int k = b->pathIdx; k < b->pathLen && along < 25; k++) { along += dist2d(pv, &b->path[k * 3]); pv = &b->path[k * 3]; if (pv[1] - vi.pos[1] > climb) climb = pv[1] - vi.pos[1]; }
        int boost = thr > 0.5f && !escaping && now >= b->reverseUntil && b->frontClear > 8 && (pitchUp > 0.07f || climb > 1.5f);
        b->levels[BC2_EiaSprint] = boost ? 1.0f : 0.0f;
        if (boost && g_cfg.verbose && now > b->boostLogAt) { b->boostLogAt = now + 20; rt_log("vehicles: %s boosts '%s' uphill (pitch %.2f, route climbs %.1f m in 25 m)", b->name, vi.name, pitchUp, climb); }
    }
    b->pvPrevThr = thr;
    if (!engaged && !veh_test_running(veh, b)) { b->yaw = yaw_from_heading(want); b->pitch = 0; }

    if (g_cfg.verbose && now - b->steerAt > 2) { b->steerAt = now;
        rt_log("vehicles: %s drives at (%.0f, %.0f) heading %.2f want %.2f err %.2f thr %.2f steer %.2f v %.1f, flag %c %.0f m, path %d/%d off %.1f bend %.2f clear %.1f%s%s", b->name, vi.pos[0], vi.pos[2],
               vehHeading, want, err, thr, steerCmd, fwdSpeed, 'A' + f, distFlag, b->pathIdx, b->pathLen, offRoute, bend, b->frontClear, now < b->reverseUntil ? " REVERSING" : "", escaping ? " ESCAPING" : ""); }
}
/* ---------------------------------------------------------------- anti-vehicle (1.0.1)
   Enemy-crewed vehicles in sight: engineers fire their launcher at armour and helicopters (leading moving targets);
   everyone else puts rifle fire on a low helicopter when no soldier is in front of them. */
static int at_target(Bot *b, double now, const float *eye, float *aim)
{
    int best = -1; float bd = 1e30f; VehicleInfo bv;
    for (int i = 0; i < g_world.vehCount; i++) {
        const VehicleInfo *v = &g_world.veh[i];
        if (v->kind != VK_GROUND && v->kind != VK_HELI && v->kind != VK_UNKNOWN && v->kind != VK_BOAT && v->kind != VK_EMPLACE) continue;
        uint32_t occ = 0; for (int s2 = 0; s2 < v->seats && !occ; s2++) occ = eng_vehicle_occupant(v->entity, s2);
        if (!occ) continue;
        int tm = player_team(occ); if (tm < 1 || tm == b->team) continue;
        VehicleInfo nv; if (!world_vehicle_read(v->entity, &nv)) continue;
        nv.kind = v->kind;
        float d = dist3(nv.pos, eye);
        float maxd = b->hasAT && b->rockets > 0 ? (nv.kind == VK_HELI ? 130 : 150) : b->hasGL && nv.kind != VK_HELI ? 80 : (nv.kind == VK_HELI ? 110 : 0);
        if (d > maxd || d < 8) continue;
        float c[3] = {nv.pos[0], nv.pos[1] + 1.2f, nv.pos[2]};
        if (!los_clear(eye, c, 5.0f)) continue;
        if (v->entity == b->atVeh) d *= 0.7f;   /* keep the one we are on */
        if (d < bd) { bd = d; best = i; bv = nv; }
    }
    if (best < 0) return 0;
    if (bv.entity != b->atVeh) { b->atVeh = bv.entity; memcpy(b->atPrev, bv.pos, 12); b->atPrevAt = now; memset(b->atVel, 0, 12); }
    else if (now - b->atPrevAt > 0.2) {
        float k = 1.0f / (float)(now - b->atPrevAt);
        for (int j = 0; j < 3; j++) b->atVel[j] = b->atVel[j] * 0.5f + (bv.pos[j] - b->atPrev[j]) * k * 0.5f;
        memcpy(b->atPrev, bv.pos, 12); b->atPrevAt = now;
    }
    b->atHeli = bv.kind == VK_HELI;
    /* lead: rockets fly ~85 m/s, bullets ~400 m/s */
    b->atGL = !(b->hasAT && b->rockets > 0) && b->hasGL && !b->atHeli;
    float d = dist3(bv.pos, eye), tf = d / (b->hasAT && b->rockets > 0 ? 140.0f : b->atGL ? 75.0f : 400.0f);
    for (int j = 0; j < 3; j++) aim[j] = bv.pos[j] + b->atVel[j] * tf;
    aim[1] += 0.8f;   /* centre of the hull */
    return 1;
}

/* ---------------------------------------------------------------- per-tick brain */
static void brain(Bot *b, double now, float dt, const BC2ObservedPlayer *self)
{
    memset(b->levels, 0, sizeof b->levels);
    for (int a = 0; a < BC2_EiaUndefined; a++) if (now < b->heldUntil[a]) b->levels[a] = 1;
    memcpy(b->pos, self->position, 12); memcpy(b->fwd, self->forward, 12);

    /* objective: the director's assignment (flags first); hunting only on maps without flags */
    int inZone = 0;
    if (b->aFlag >= 0 && b->aFlag < g_world.cpCount) {
        objective_goal(b, now);
        inZone = dist2d(b->pos, g_world.cps[b->aFlag].pos) < fmaxf(cp_reach(b->aFlag) + 1.0f, g_world.cps[b->aFlag].radius * 0.6f);
    } else if (!g_world.cpCount) {
        if (!b->haveGoal || now - b->goalChosenAt > 30 || dist2d(b->pos, b->goal) < 6) hunt_goal(b, now);
    } else { b->haveGoal = 0; b->goalCp = -1; }   /* waiting one director pass for a job */
    b->holding = inZone && b->aRole != OBJ_ATTACK;

    /* perception */
    if (now >= b->perceiveAt) { b->perceiveAt = now + 0.22 + 0.05 * rt_randf(); perceive(b, now, self); }
    int ti = b->target ? world_find_player(b->target, b->targetId) : -1;
    const BC2ObservedPlayer *tgt = ti >= 0 ? &g_world.obs.players[ti] : NULL;
    if (tgt && (!tgt->soldierValid || !tgt->transformValid || tgt->health <= 0)) {
        if (now - b->lastVisible < 1.5 && now - b->lastShotAt < 1.5) {   /* our kill (most likely) */
            b->kills++; learn_kill(b->pos);
            if (b->owOn && dist2d(b->pos, b->owSpot) < 3) learn_spot_result(b->owSpot, 0, 1, 0);
        }
        tgt = NULL; b->target = 0;
    }
    int engaging = tgt && (b->visible || now - b->lastVisible < 1.2);
    float tdist = tgt ? dist3(tgt->position, self->position) : 0;

    /* where to walk: revive a teammate close by > the objective */
    const float *dest = b->haveGoal ? b->goal : NULL;
    float destBuf[3]; int holdHere = 0, atOverwatch = 0;
    if (b->owOn && (now > b->owUntil || b->aFlag < 0)) { b->owOn = 0; b->owNext = now + 45; }
    /* 1.6.10: snipers go back up now and then - 1.6.9 only picked a high spot (a roof, an upper floor, a ridge that sees the
       flag from 35-125 m) when the bot got a new job, so most recons fought at ground level for the whole round */
    if (b->cls == CLS_RECON && !b->owOn && !b->vehRole && b->aFlag >= 0 && now > b->owNext) {
        b->owNext = now + randr(40, 80);
        if (rt_randf() < 0.5f && overwatch_pick(b, b->aFlag, 0, now)) {
            b->pathLen = 0; b->pathAt = 0;
            if (g_cfg.verbose) rt_log("bot %s: recon heads for high ground at (%.0f, %.0f, %.0f) over flag %c", b->name, b->owSpot[0], b->owSpot[1], b->owSpot[2], 'A' + b->aFlag);
        }
    }
    if (b->owOn) { dest = b->owSpot; if (dist2d(b->pos, b->owSpot) < 1.6f && fabsf(b->pos[1] - b->owSpot[1]) < 2.0f) { holdHere = 1; atOverwatch = 1;
        if (!b->owCounted) { b->owCounted = 1; learn_spot_result(b->owSpot, 1, 0, 0); } } }
    else if (b->viaOn && b->aFlag >= 0 && b->aFlag < g_world.cpCount) {
        const float *fp = g_world.cps[b->aFlag].pos;
        if (dist2d(b->pos, b->via) < 10 || dist2d(b->pos, fp) < dist2d(b->via, fp) + 5) b->viaOn = 0;
        else dest = b->via;
    }
    /* Rush (1.5.1): attackers arm the M-COM they are sent to, defenders defuse an armed one. BC2 skips ServerPlayer::update
       for AI players - that is where a human's held Interact reaches the M-COM (entityInteract) - so 1.5.0's bots held
       Interact for nothing. Now a bot walks onto the M-COM, holds Interact for the hold time and Phoenix calls the
       interaction itself, as entityInteract does (ServerInteractionEntity::interact with the bot's soldier). */
    int arming = 0; float armAt[3];
    if (g_cfg.objectives && b->aFlag >= 0 && b->aFlag < g_world.cpCount && g_world.cps[b->aFlag].isObj && !b->vehRole) {
        CapturePoint *oc = &g_world.cps[b->aFlag];
        float d = dist2d(b->pos, oc->pos), dy = fabsf(b->pos[1] - oc->pos[1]);
        int attacker = b->team != oc->owner;
        uint32_t target = attacker ? (oc->armed ? 0 : oc->armEnt) : (oc->armed ? oc->disarmEnt : 0);
        int want = target && now > b->armBlockUntil && (attacker ? (b->aRole == OBJ_ATTACK && d < 70) : d < 250);
        if (want && (!engaging || tdist > 15 || b->armStart > 0)) {
            memcpy(armAt, oc->pos, 12);
            /* the M-COM is a box: a bot cannot stand on its centre - close enough is in reach (2.6 m), or 3.5 m once it stops
               getting closer (pressed against the crate) */
            if (d < b->armBest - 0.3f) { b->armBest = d; b->armBestAt = now; }
            if (d > 6 || b->armBest <= 0) { b->armBest = 1e9f; b->armBestAt = now; }
            int reach = (d < 1.9f && dy < 2.0f) || (d < 2.4f && dy < 2.0f && now - b->armBestAt > 2.0);   /* (UseWithinRadius is 2 m) */
            if (!reach) { memcpy(destBuf, oc->pos, 12); dest = destBuf; b->armStart = 0; }
            else {
                holdHere = 1; arming = 1; b->levels[BC2_EiaInteract] = 1;
                if (b->armStart <= 0) { b->armStart = now; static int al; if (al++ < 20) rt_log("objectives: %s starts %s %s (hold %.1f s)", b->name, attacker ? "arming" : "defusing", oc->name, attacker ? oc->armHold : oc->disarmHold); }
                if (now - b->armStart >= (attacker ? oc->armHold : oc->disarmHold) + 0.2) {
                    int r = b->soldier ? eng_interact(target, b->soldier) : -1;
                    b->armStart = 0; b->armTries++;
                    static int rl;
                    if (rl++ < 30 || r < 1) {
                        uint32_t data = 0, uses = 0, maxUses = 0; uint8_t flags = 0; float last = 0, delay = 0;
                        eng_u32(target + 0x0c, &data); eng_read(target + 0x26, &flags, 1); eng_u32(target + 0x84, &uses); eng_read(target + 0x80, &last, 4);
                        if (eng_ptr(data)) { eng_u32(data + 0x80, &maxUses); eng_read(data + 0xb4, &delay, 4); }
                        int why = eng_interact_why() % 10;
                        rt_log("objectives: %s %s %s from %.1f m -> %s%s (interaction %s, used %u of %u, last %.1f, delay %.1f)", b->name, attacker ? "arms" : "defuses", oc->name, d,
                               r == 1 ? "done" : r == 2 ? "done (with the human-only checks skipped)" : r == 0 ? "refused by the game" : "not an interaction / no soldier",
                               r == 0 || r == 2 ? (why == 3 ? " - arm animation would not start" : why == 1 ? " - soldier / area check" : "") : "", (flags & 8) ? "on" : "off", uses, maxUses, last, delay);
                    }
                    if (r >= 1) { b->armTries = 0; if (attacker) { oc->armed = 1; oc->armedAt = now; } else oc->armed = 0; }
                    else if (b->armTries >= 3) { b->armTries = 0; b->armBlockUntil = now + 20; }
                }
            }
        } else b->armStart = 0;
    }
    /* a stationary weapon close by, or a ride towards a far flag */
    emplacement_consider(b, now, engaging, tdist);
    vehicle_consider(b, now, engaging);
    if (b->vehRole && !b->vehSince) { const float *vp = vehicle_board(b, now); if (vp) { memcpy(destBuf, vp, 12); dest = destBuf; } }
    /* engineers repair damaged vehicles of their side that stand close by (1.5): walk up, repair tool out, aim, hold */
    int repairing = 0; float repairAt[3];
    if (b->cls == CLS_ENGINEER && g_cfg.equipment && !b->vehRole && !b->reviveP && (!engaging || tdist > 45)) {
        if (now > b->repairCheckAt) { b->repairCheckAt = now + 1.5; if (!b->repairVeh) { b->repairVeh = repair_target(b, 30); if (b->repairVeh) b->repairUntil = now + 25; } }
        VehicleInfo rv;
        if (b->repairVeh && (now > b->repairUntil || !world_vehicle_read(b->repairVeh, &rv) || !veh_needs_repair(&rv, 0.97f))) {
            if (b->repairVeh && now <= b->repairUntil) { static int rl; if (rl++ < 8) rt_log("bot %s: vehicle repaired", b->name); }
            b->repairVeh = 0; b->repairCheckAt = now + 4;
        } else if (b->repairVeh) {
            memcpy(repairAt, rv.pos, 12);
            if (dist2d(b->pos, rv.pos) < 4.8f) {
                holdHere = 1; repairing = 1;
                if (b->eqState == EQ_IDLE) {
                    { const VehicleInfo *kn = veh_info(b->repairVeh); snprintf(rv.name, sizeof rv.name, "%s", kn && kn->name[0] ? kn->name : "vehicle"); }
                    float pit = atan2f(rv.pos[1] + 0.8f - (b->pos[1] + 1.55f), fmaxf(1.0f, dist2d(b->pos, rv.pos)));
                    if (eq_start(b, now, BC2_EiaDynamicGadget1, 3.5, 1, pit, BC2_EiaSwitchPrimaryWeapon)) { static int rl2; if (rl2++ < 8) rt_log("bot %s: repairing '%s' (health %.0f)", b->name, rv.name, rv.health); }
                }
            } else { memcpy(destBuf, rv.pos, 12); dest = destBuf; }
        }
    }
    if (b->cls == CLS_MEDIC && g_cfg.revive && g_cfg.equipment) {
        if (b->reviveP) {
            const BC2ObservedPlayer *p = NULL;
            int pi = world_find_player(b->reviveP, b->reviveId); if (pi >= 0) p = &g_world.obs.players[pi];
            if (!p || !p->soldierValid || p->health > 0 || now > b->reviveUntil) b->reviveP = 0;
            else if (dist2d(b->pos, p->position) < 1.8f) {
                /* kit slot 2 (the selectable gadget: defibrillator / rocket / 40 mm / mortar) is the GrenadeLauncher action;
                   DynamicGadget1 is the fixed slot-5 gadget (medkit, ammo, repair drill, C4) - live 0.8.0 */
                if (eq_start(b, now, BC2_EiaGrenadeLauncher, 0.7, 1, -1.05f, BC2_EiaSwitchPrimaryWeapon)) {
                    rt_log("bot %s: reviving a downed teammate", b->name);
                    b->reviveP = 0; b->reviveCheckAt = now + 6;
                }
                holdHere = 1;
            } else { memcpy(destBuf, p->position, 12); dest = destBuf; }
        } else if (now > b->reviveCheckAt && (!engaging || tdist > 30)) {
            b->reviveCheckAt = now + 1.0;
            const BC2ObservedPlayer *p = downed_teammate(b, 35);
            if (p) { b->reviveP = p->pointer; b->reviveId = p->id; b->reviveUntil = now + 14; }
        }
    }
    /* movement wish */
    float moveHeading = heading_from_yaw(b->yaw), remaining = 0, wayDist = 0; int moving = 0;
    follow(b, now, dest, &moveHeading, &moving, &remaining, &wayDist);
    if (holdHere) moving = 0;
    /* fight on the way: stand only for close enemies or enemies on the route; everything else is shot
       while moving on to the flag (AltBotMod threat-in-path rule) */
    /* BC2 spreads shots a lot while moving, so a bot that sees its enemy plants its feet to shoot
       (0.7.1 shot on the move and missed): close or on-route enemies for up to 10 s, others for 5 s,
       recon at any range it engages; then it pushes on to the flag, firing as it goes. */
    if (engaging) {
        int inPath = b->haveGoal && threat_in_path(b->pos, b->goal, tgt->position);
        double fightFor = now - b->targetSince;
        if (b->cls == CLS_RECON && fightFor < 12) moving = 0;
        else if ((tdist < 25 || inPath) && fightFor < 10) moving = 0;
        else if (b->visible && tdist < 70 && fightFor < 5) moving = 0;
    }
    if (b->knifeLeft > 0) moving = 0;
    /* an enemy within 7 m while we reload, or one facing away: go in with the knife */
    if (engaging && tdist < 7.0f && tdist > 2.2f && b->visible && b->eqState == EQ_IDLE &&
        (now < b->reloadUntil || fabsf(wrap_pi(heading_to(tgt->position, b->pos) - atan2f(tgt->forward[0], tgt->forward[2]))) > 2.0f)) {
        moving = 1; moveHeading = heading_to(b->pos, tgt->position); b->rushUntil = now + 0.5;
    }

    int steerAct = STEER_FREE; float steerHeading = moveHeading;
    if (moving && !b->inOn) steerHeading = steer(b, now, moveHeading, &steerAct);   /* (on a surveyed way in: walk it as surveyed - steps read as obstacles) */
    /* vault: jump and keep pushing straight for a second (AltBotMod: 30 frames) */
    if (moving && steerAct == STEER_VAULT && now - b->vaultAt > 1.4 && now > b->vaultUntil) {
        b->vaultAt = now; b->vaultUntil = now + 1.0; b->vaultHeading = steerHeading; press(b, BC2_EiaJump); if (b->lspot >= 0) b->lastAct = LA_JUMP;
    }
    if (now < b->vaultUntil) { moving = 1; steerHeading = b->vaultHeading; }
    /* crouch under a bar, stand up again after it */
    if (moving && steerAct == STEER_CROUCH && !b->crouched) { press(b, BC2_EiaChangePose); b->crouched = 1; b->crouchUntil = now + 1.8; if (b->lspot >= 0) b->lastAct = LA_CROUCH; }
    else if (b->crouched && now > b->crouchUntil && steerAct != STEER_CROUCH) { press(b, BC2_EiaChangePose); b->crouched = 0; }
    else if (b->crouched && steerAct == STEER_CROUCH) b->crouchUntil = now + 1.0;
    stuck_check(b, now, moving, moveHeading, wayDist);
    /* doors: a closed door right in front, with the route going on through it, opens with the knife (BC2) */
    if (moving && b->knifeLeft <= 0 && now > b->doorAt && wayDist > 2.0f && now > b->vaultUntil) {
        b->doorAt = now + 0.35;
        float a[3] = {b->pos[0], b->pos[1] + 1.1f, b->pos[2]}, c[3] = {a[0] + sinf(moveHeading) * 0.9f, a[1], a[2] + cosf(moveHeading) * 0.9f}, h[3], n[3];
        float a2[3] = {a[0], b->pos[1] + 0.35f, a[2]}, c2[3] = {c[0], b->pos[1] + 0.35f, c[2]};
        if (eng_ray(a, c, h, n) && fabsf(n[1]) < 0.35f && eng_ray(a2, c2, h, n) && wayDist > dist2d(b->pos, h) + 0.8f) {
            b->knifeLeft = 1; b->knifeAt = now - 1; b->knifeHeading = moveHeading; b->doorAt = now + 6.0;
            static int doorLogs; if (doorLogs++ < 6) rt_log("move: %s knifes a door (or a thin wall) on its route at (%.0f, %.0f, %.0f)", b->name, h[0], h[1], h[2]);
        }
    }

    /* learning: did the last action get us past the stuck spot? */
    if (b->lspot >= 0) {
        float dx = b->pos[0] - b->lspotPos[0], dz = b->pos[2] - b->lspotPos[2];
        float along = dx * sinf(b->lspotHeading) + dz * cosf(b->lspotHeading);
        if (along > 2.5f && hypotf(dx, dz) > 3.0f) { learn_stuck_result(b->lspot, b->lastAct, 1); b->lspot = -1; }
        else if (now - b->lspotAt > 25) { learn_stuck_result(b->lspot, -1, 0); b->lspot = -1; }
    }
    /* ... and use what was learned: a spot ahead where a jump / knife / crouch / side-step worked before */
    if (g_cfg.learn && moving && now > b->hintAt && b->knifeLeft <= 0 && now > b->vaultUntil) {
        b->hintAt = now + 0.4;
        int act = learn_hint(b->pos, moveHeading);
        if (act == LA_JUMP && now - b->vaultAt > 1.4) { b->vaultAt = now; b->vaultUntil = now + 1.0; b->vaultHeading = moveHeading; press(b, BC2_EiaJump); b->hintAt = now + 2.0; }
        else if (act == LA_KNIFE) { b->knifeLeft = 2; b->knifeAt = now; b->knifeHeading = moveHeading; b->hintAt = now + 4.0; }
        else if (act == LA_CROUCH && !b->crouched) { press(b, BC2_EiaChangePose); b->crouched = 1; b->crouchUntil = now + 2.0; b->hintAt = now + 2.5; }
        else if (act == LA_DETOUR && now > b->detourUntil) {
            for (int k = 0; k < 4; k++) {
                float h = moveHeading + (k & 1 ? 1.0f : -1.0f) * randr(0.9f, 1.6f), d = randr(6, 10);
                if (ray_free(b->pos, h, d, 1.0f)) { b->detour[0] = b->pos[0] + sinf(h) * d; b->detour[1] = b->pos[1]; b->detour[2] = b->pos[2] + cosf(h) * d; b->detourUntil = now + 3.5; break; }
            }
            b->hintAt = now + 5.0;
        }
    }

    /* pinned: the bot wants to walk but has not moved 0.6 m whatever it tried - wedged in rubble, a fence
       corner, on a ledge lip (0.7.0 logs: velocity -0.63 down, every input ignored for minutes). Push the
       character free with a hop away from the obstacle; redeploy it as the last resort. */
    {
        int wants = moving || now < b->recoverUntil;
        if (!wants || dist2d(b->pos, b->frozenPos) > 0.6f || fabsf(b->pos[1] - b->frozenPos[1]) > 0.8f) {
            memcpy(b->frozenPos, b->pos, 12); b->frozenSince = now; b->unpinTries = 0;
        }
        double frozen = now - b->frozenSince;
        int cstate = b->soldier ? eng_character_state(b->soldier) : -1;
        if (frozen > 6 && now > b->unpinAt) {
            /* hop only from the ground: an upward kick in the air is what sent 0.7.1 bots walking up into the sky */
            b->unpinAt = now + 2.5; b->unpinUntil = now + 0.45; b->unpinKick = cstate == 0 && b->unpinTries < 4; b->unpinTries++;
            b->unpinHeading = (b->unpinTries & 1) ? wrap_pi(moveHeading + PI + randr(-0.8f, 0.8f)) : wrap_pi(moveHeading + randr(-1.4f, 1.4f));
            static int logs;
            if (logs < 12 && (b->unpinTries == 1 || b->unpinTries == 4)) {
                logs++; char dbg[400] = "";
                if (b->soldier) eng_debug_character(b->soldier, dbg, sizeof dbg);
                rt_log("bot %s: pinned at (%.1f, %.1f, %.1f) for %.0f s - pushing it free (try %d) - %s", b->name, b->pos[0], b->pos[1], b->pos[2], frozen, b->unpinTries, dbg);
            }
        }
        if (g_cfg.frozenRespawn > 0 && frozen > g_cfg.frozenRespawn) b->redeploy = 1;
        /* walking on air: nothing under the bot for 4 s (and not falling back) -> redeploy */
        if (cstate != 6 && b->swimHave) {   /* 1.6.9: out of the water - drop the bank and plan a route from where it climbed out */
            b->swimHave = 0; b->detourUntil = 0; b->pathAt = 0; b->pathLen = 0; b->pathPending = 0;
            static int outLogs; if (outLogs++ < 12 || g_cfg.verbose) rt_log("bot %s: out of the water at (%.0f, %.0f, %.0f) after %.0f s - back on a route", b->name, b->pos[0], b->pos[1], b->pos[2], now - b->swimSince);
        }
        if (cstate == 6) {   /* swimming (Nelson Bay's river: 1.2 took swimmers for sky-walkers and redeployed 60 of them) */
            b->skyCount = 0;
            if (!b->swimSince) { b->swimSince = now; b->swimHave = 0; b->swimBest = 1e30f; b->swimBestAt = now; }
            if (now > b->swimAt) {
                b->swimAt = now + 1.5;
                /* 1.5.2: the grid took this water for ground (Valparaiso's harbour) - flag the surface as water so routes keep
                   out of it. 1.6.9: swim for the best bank within 120 m - one that can be climbed, on the connected map, on
                   the way to where the bot is going (it is chosen again every 6 s, the bank kept while it gets closer) */
                learn_water(b->pos);
                b->pathAt = 0; b->pathLen = 0; b->pathPending = 0;
                if (!b->swimHave || now - b->swimBestAt > 6) {
                    float land[3]; int got = navg_land_best(b->pos, 120, b->haveGoal ? b->goal : NULL, land);
                    if (!got) got = navg_land_near(b->pos, 45, land);
                    if (got && (!b->swimHave || dist2d(land, b->swimBank) > 4)) { memcpy(b->swimBank, land, 12); b->swimHave = 1; b->swimBest = dist2d(b->pos, land); b->swimBestAt = now; }
                    static int swimLogs; if (swimLogs++ < 12 || g_cfg.verbose) rt_log("bot %s: swimming at (%.0f, %.0f, %.0f) - %s", b->name, b->pos[0], b->pos[1], b->pos[2],
                                                                                    got ? "heading for the best bank to climb out" : "no bank within 120 m");
                }
                if (b->swimHave) { memcpy(b->detour, b->swimBank, 12); b->detourUntil = now + 2.0; }
            }
            if (b->swimHave) {
                float d = dist2d(b->pos, b->swimBank);
                if (d < b->swimBest - 1.0f) { b->swimBest = d; b->swimBestAt = now; }
                if (d < 4.0f && now > b->swimJumpAt) { b->swimJumpAt = now + 0.8; press(b, BC2_EiaJump); }   /* at the bank: climb out over the lip */
            }
            /* 1.6.9: redeployed only when it stops getting closer to land (20 s) or after 2 minutes - 1.6.8 gave every
               swimmer 25 s, however near the bank it was */
            if ((b->swimHave ? now - b->swimBestAt > 20 : now - b->swimSince > 25) || now - b->swimSince > 120) {
                b->redeploy = 1; b->swimSince = 0; b->swimHave = 0; b->frozenSince = now;
                rt_log("bot %s: could not get out of the water - redeploying it", b->name);
            }
        } else if ((b->swimSince = 0), now > b->skyCheckAt) {
            b->skyCheckAt = now + 1.0;
            float a[3] = {b->pos[0], b->pos[1] + 0.3f, b->pos[2]}, c[3] = {b->pos[0], b->pos[1] - 6.0f, b->pos[2]}, hh[3], nn[3];
            if (!eng_ray(a, c, hh, nn) && b->pos[1] >= b->skyY - 0.5f) b->skyCount++; else b->skyCount = 0;
            b->skyY = b->pos[1];
            if (b->skyCount >= 4) { b->skyCount = 0; rt_log("bot %s: in the air with no ground below at (%.0f, %.0f, %.0f), state %d - redeploying", b->name, b->pos[0], b->pos[1], b->pos[2], cstate); b->redeploy = 1; b->frozenSince = now - 999; }
        }
    }

    /* aim */
    float wantYaw, wantPitch = -0.05f * (float)g_cfg.pitchSign;
    if (engaging) {
        float eye[3] = {self->position[0], self->position[1] + 1.55f, self->position[2]};
        float aimPt[3] = {tgt->position[0], tgt->position[1] + (tdist < 10 ? 1.15f : 1.3f), tgt->position[2]};
        /* lead a moving target (bullet flight + a frame of input lag) and hold over for drop at range (1.2) */
        {
            float k = b->svAt > 0 && now - b->svAt > 0.02 && now - b->svAt < 0.6 && b->svRef == tgt->pointer ? 1.0f / (float)(now - b->svAt) : 0;
            for (int j = 0; j < 3; j++) b->svVel[j] = k ? b->svVel[j] * 0.7f + (tgt->position[j] - b->svPrev[j]) * k * 0.3f : 0;
            memcpy(b->svPrev, tgt->position, 12); b->svAt = now; b->svRef = tgt->pointer;
            float lead = tdist / 550.0f + 0.07f;
            aimPt[0] += b->svVel[0] * lead; aimPt[2] += b->svVel[2] * lead;
            if (tdist > 60) aimPt[1] += 0.5f * 9.81f * (tdist / 550.0f) * (tdist / 550.0f);
        }
        if (now - b->aimErrAt > 0.6) {   /* new error each burst-ish, shrinking while the target is tracked */
            b->aimErrAt = now;
            float settle = fmaxf(0.25f, 1.0f - (float)(now - b->targetSince) * 0.6f);
            float e = skill_error() * settle;
            b->aimErr[0] = randr(-e, e); b->aimErr[1] = randr(-e, e) * 0.6f;
        }
        float h = heading_to(eye, aimPt) + b->aimErr[0];
        float horiz = hypotf(aimPt[0] - eye[0], aimPt[2] - eye[2]);
        float p = atan2f(aimPt[1] - eye[1], horiz) + b->aimErr[1];
        wantYaw = yaw_from_heading(h); wantPitch = p * (float)g_cfg.pitchSign;
    } else if (repairing) {
        wantYaw = yaw_from_heading(heading_to(b->pos, repairAt));
    } else if (arming) {
        wantYaw = yaw_from_heading(heading_to(b->pos, armAt)); wantPitch = -0.5f * (float)g_cfg.pitchSign;
    } else if (atOverwatch && b->aFlag >= 0) {   /* watch the flag and the ways into it */
        const float *fp = g_world.cps[b->aFlag].pos;
        if (now > b->owScanAt) { b->owScanAt = now + randr(2, 4); b->owScan = randr(-0.6f, 0.6f); }
        wantYaw = yaw_from_heading(heading_to(b->pos, fp) + b->owScan);
        wantPitch = atan2f(fp[1] + 1.0f - (b->pos[1] + 1.55f), dist2d(b->pos, fp)) * (float)g_cfg.pitchSign;
    } else {
        wantYaw = yaw_from_heading(moving ? steerHeading : heading_from_yaw(b->yaw));
    }
    /* anti-vehicle: pick the vehicle (2x a second) and aim at its lead point */
    if (now >= b->atUntil - 0.01 || !b->atOn) {
        float eyeA[3] = {self->position[0], self->position[1] + 1.55f, self->position[2]}, ap[3];
        int launcher = (b->hasAT && b->rockets > 0) || b->hasGL;
        int want = g_cfg.equipment && (launcher ? (!engaging || tdist > 25) : (!engaging && b->cls != CLS_RECON));
        b->atOn = want && at_target(b, now, eyeA, ap) && ((b->hasAT && b->rockets > 0) || b->atGL || b->atHeli);
        if (b->atOn) memcpy(b->atPt, ap, 12);
        b->atUntil = now + 0.5;
    }
    if (b->atOn && b->knifeLeft <= 0) {
        float eyeA[3] = {self->position[0], self->position[1] + 1.55f, self->position[2]};
        float h = heading_to(eyeA, b->atPt), horiz = dist2d(eyeA, b->atPt);
        float vs = b->atGL ? 75.0f : 140.0f;   /* rocket ~140 m/s, 40 mm ~75 m/s */
        float drop = (b->hasAT && b->rockets > 0) || b->atGL ? 0.5f * asinf(fminf(1.0f, 9.81f * horiz / (vs * vs))) : 0;
        wantYaw = yaw_from_heading(h); wantPitch = (atan2f(b->atPt[1] - eyeA[1], horiz) + drop) * (float)g_cfg.pitchSign;
    }
    if (b->knifeLeft > 0) {   /* knife the obstacle in front */
        wantYaw = yaw_from_heading(b->knifeHeading); wantPitch = -0.15f * (float)g_cfg.pitchSign;
        if (now - b->knifeAt > 0.6) {
            b->knifeAt = now; b->knifeLeft--; press(b, BC2_EiaMeleeAttack);
            if (b->knifeLeft == 0) { b->recoverUntil = now + 0.8; b->recoverStrafe = 0; b->recoverBack = 0; }
        }
    }
    if (b->eqState != EQ_IDLE && b->eqPitchMode == 1) wantPitch = b->eqPitch * (float)g_cfg.pitchSign;
    if (b->eqState != EQ_IDLE && b->eqPitchMode == 2) wantPitch += b->eqPitch * (float)g_cfg.pitchSign;
    if (now < b->grenadeHold) wantPitch += (0.18f + fminf(tdist, 35) * 0.008f) * (float)g_cfg.pitchSign;   /* lob */
    float maxTurn = (g_cfg.difficulty >= 3 ? 9.0f : g_cfg.difficulty == 2 ? 6.5f : 4.5f) * dt;
    float dy = wrap_pi(wantYaw - b->yaw);
    if (dy > maxTurn) dy = maxTurn; if (dy < -maxTurn) dy = -maxTurn;
    b->yaw += dy; while (b->yaw < 0) b->yaw += TWO_PI; while (b->yaw >= TWO_PI) b->yaw -= TWO_PI;
    float dp = wantPitch - b->pitch; float maxP = maxTurn * 0.8f;
    if (dp > maxP) dp = maxP; if (dp < -maxP) dp = -maxP;
    b->pitch += dp;
    float yawErr = fabsf(wrap_pi(wantYaw - b->yaw));

    /* legs: forward/strafe relative to where the bot looks */
    float lookHeading = heading_from_yaw(b->yaw);
    if (moving) {
        float rel = wrap_pi(steerHeading - lookHeading);
        float fwd = cosf(rel), side = sinf(rel);
        b->levels[BC2_EiaThrottle] = fmaxf(-1, fminf(1, fwd));
        /* strafe +1 moved the captured soldier towards -right; right = (fz, -fx) of the forward vector */
        b->levels[BC2_EiaStrafe] = fmaxf(-1, fminf(1, -side));
        if (!engaging && remaining > 25 && fabsf(rel) < 0.35f && now - b->lastVisible > 3 && b->eqState == EQ_IDLE) b->levels[BC2_EiaSprint] = 1;
        if (now < b->vaultUntil) { b->levels[BC2_EiaThrottle] = 1; b->levels[BC2_EiaStrafe] = 0; }
        if (now < b->rushUntil) b->levels[BC2_EiaSprint] = 1;
    }
    if (now < b->recoverUntil) {
        b->levels[BC2_EiaStrafe] = b->recoverStrafe;
        b->levels[BC2_EiaThrottle] = b->recoverBack ? -0.8f : 0.3f;
        b->levels[BC2_EiaSprint] = 0;
        if (now - b->jumpAt > 1.5) { press(b, BC2_EiaJump); b->jumpAt = now; }
    }

    /* equipment owns the trigger while it is out */
    int eqBusy = eq_update(b, now);
    /* the primary weapon in hand whenever no gadget is in use (1.6): a repair tool, launcher or pistol left out after a
       gadget, a vehicle or a respawn is put away */
    if (!eqBusy && b->soldier && now > b->wpnCheckAt) {
        b->wpnCheckAt = now + (engaging ? 0.15 : 0.5);
        int w = eng_soldier_weapon(b->soldier);
        if (w > 0) { if (!b->wpnOffSince) b->wpnOffSince = now; } else b->wpnOffSince = 0;
        if (b->wpnOffSince && now - b->wpnOffSince > (engaging ? 0.1 : 0.9)) {   /* an enemy in sight: the gun out at once */
            int ok = eng_soldier_set_weapon(b->soldier, 0); b->wpnOffSince = 0;
            static int wl; if (wl++ < 12) rt_log("bot %s: had weapon %d out with nothing to use it for - primary back in hand%s", b->name, w, ok ? "" : " (refused)");
        }
    }

    /* class equipment outside combat */
    if (!eqBusy && now > b->gadgetAt && g_cfg.equipment) {
        /* supplies (1.2.1): medics drop a medkit for themselves or a hurt teammate close by (a human first), assault
           soldiers an ammo box when teammates are around - more often next to a human, and after a fight */
        int humanNear = humans_near(b, 12, 0), humanHurt = humans_near(b, 12, 90);
        if (b->cls == CLS_MEDIC && (humanHurt || (self->health < 75 && !b->visible) || teammates_near(b, 12, 80) > 0 || (teammates_near(b, 10, 0) >= 2 && b->holding && rt_randf() < 0.01f))) {
            if (eq_start(b, now, BC2_EiaDynamicGadget1, 0.25, 1, -0.75f, BC2_EiaSwitchPrimaryWeapon)) {
                b->gadgetAt = now + (humanHurt ? randr(12, 18) : randr(20, 32));
                static int mkLogs; if (mkLogs++ < 6) rt_log("supplies: %s drops a medkit%s", b->name, humanHurt ? " for a hurt human teammate" : "");
            }
        } else if (b->cls == CLS_ASSAULT && !engaging && (humanNear || teammates_near(b, 12, 0) > 0) &&
                   (humanNear ? rt_randf() < 0.08f : now - b->lastShotAt < 12 ? rt_randf() < 0.05f : rt_randf() < 0.01f)) {
            if (eq_start(b, now, BC2_EiaDynamicGadget1, 0.25, 1, -0.75f, BC2_EiaSwitchPrimaryWeapon)) {
                b->gadgetAt = now + (humanNear ? randr(20, 30) : randr(30, 45));
                static int amLogs; if (amLogs++ < 6) rt_log("supplies: %s drops an ammo box%s", b->name, humanNear ? " next to a human teammate" : "");
            }
        /* recon: slot 5 is C4 - thrown and fired it blows up at the bot's feet (live 0.8.0); not used */
        }
        eqBusy = b->eqState != EQ_IDLE;
    }

    /* anti-vehicle fire */
    if (b->atOn) {
        b->levels[BC2_EiaSprint] = 0;
        if ((b->hasAT && b->rockets > 0) || b->atGL) {
            if (!moving || dist2d(b->pos, b->atPt) < 90) { b->levels[BC2_EiaThrottle] = 0; b->levels[BC2_EiaStrafe] = 0; }   /* stand to shoot */
            float atTol = fminf(0.04f, 0.008f + 1.5f / fmaxf(20.0f, dist3(b->pos, b->atPt)));
            if (!eqBusy && yawErr < atTol && fabsf(wantPitch - b->pitch) < atTol * 1.5f && now > b->glAt) {
                if (eq_start(b, now, BC2_EiaGrenadeLauncher, b->atGL ? 0.15 : 0.25, 0, 0, b->atGL ? BC2_EiaGrenadeLauncher : BC2_EiaSwitchPrimaryWeapon)) {
                    if (b->atGL) b->glAt = now + randr(4.0f, 6.0f); else { b->glAt = now + randr(5.5f, 8.0f); b->rockets--; }   /* reload, then again */
                    static int atLogs; if (atLogs++ < 16) rt_log("combat: %s fires %s at an enemy %s %.0f m away%s", b->name, b->atGL ? "a 40 mm grenade" : "a rocket", b->atHeli ? "helicopter" : "vehicle",
                                                                 dist3(b->pos, b->atPt), !b->atGL && b->rockets == 0 ? " (last rocket)" : "");
                }
                eqBusy = b->eqState != EQ_IDLE;
            }
        } else if (!eqBusy && yawErr < 0.08f) {   /* rifle fire at a low helicopter */
            if (now < b->burstUntil) b->levels[BC2_EiaFire] = 1;
            else if (now > b->pauseUntil) { b->burstUntil = now + randr(0.3f, 0.7f); b->pauseUntil = b->burstUntil + randr(0.3f, 0.6f); b->levels[BC2_EiaFire] = 1; }
            b->fireTime += dt;
        }
    }

    /* combat */
    if (engaging && !(b->atOn && ((b->hasAT && b->rockets > 0) || b->atGL))) {
        if (now > b->strafeUntil) {
            b->strafeUntil = now + randr(0.6f, 1.6f); b->strafe = tdist < 50 ? (rt_randf() < 0.5f ? -1.0f : 1.0f) * randr(0.4f, 1.0f) : 0;
            /* 1.6.10: strafe along the route, not away from it - more than 2.5 m off the current leg, the side-step goes back
               towards it; 1.5 m off, never further away (strafe +1 moves to the left of where the bot faces) */
            if (b->pathLen && b->pathIdx < b->pathLen) {
                const float *a = b->pathIdx > 0 ? &b->path[(b->pathIdx - 1) * 3] : b->pathFrom, *c2 = &b->path[b->pathIdx * 3];
                float vx = c2[0] - a[0], vz = c2[2] - a[2], L2 = vx * vx + vz * vz, u = L2 > 1e-4f ? ((b->pos[0] - a[0]) * vx + (b->pos[2] - a[2]) * vz) / L2 : 1;
                u = u < 0 ? 0 : u > 1 ? 1 : u;
                float px = a[0] + vx * u - b->pos[0], pz = a[2] + vz * u - b->pos[2], off = hypotf(px, pz), hf = heading_from_yaw(b->yaw);
                float toLine = -px * cosf(hf) + pz * sinf(hf);   /* > 0: the route line is to the left */
                if (off > 2.5f) b->strafe = (toLine > 0 ? 1.0f : -1.0f) * 0.8f;
                else if (off > 1.5f && b->strafe * toLine < 0) b->strafe = -b->strafe;
            }
        }
        if (!moving) b->levels[BC2_EiaStrafe] = b->strafe;
        b->levels[BC2_EiaSprint] = 0;
        int zoom = b->cls == CLS_RECON ? tdist > 12 : tdist > 25;
        if (zoom && !eqBusy && !moving) b->levels[BC2_EiaZoom] = 1;   /* aim down sights when standing */
        float tol = atanf(0.55f / fmaxf(tdist, 1.0f)) + 0.03f;
        int ready = now - b->targetSince > reaction_time() && now > b->reloadUntil && b->visible;
        float eye[3] = {self->position[0], self->position[1] + 1.55f, self->position[2]};
        int clear = !friendly_in_line(b, eye, tgt->position);
        /* heavier tools first: rocket (engineer), grenade launcher (assault), hand grenade (everyone) */
        if (!eqBusy && ready && clear && yawErr < tol * 3 && g_cfg.equipment) {
            if (b->cls == CLS_ENGINEER && b->hasAT && b->rockets > 1 && tdist > 15 && tdist < 90 && now > b->glAt) {   /* (keeps one rocket for armour) */
                /* rocket at a soldier: aim at the feet (splash), plus the little arc a ~110 m/s rocket needs. 1.2 added
                   0.002 rad per metre on top of a chest aim - 2.5 m high at 50 m, so rockets flew over everyone */
                float arc = 0.5f * asinf(fminf(1.0f, 9.81f * tdist / (140.0f * 140.0f)));
                if (eq_start(b, now, BC2_EiaGrenadeLauncher, 0.2, 2, -1.15f / fmaxf(tdist, 5.0f) + arc, BC2_EiaSwitchPrimaryWeapon)) { b->glAt = now + randr(12, 20); b->rockets--; }   /* rocket: slot 2 */
            } else if (b->cls == CLS_ASSAULT && b->hasGL && tdist > 20 && tdist < 80 && now > b->glAt) {
                /* 40 mm: lobbed at ~75 m/s, aimed at the feet (1.2 used 0.0035 rad/m - four times the real arc) */
                float arc = 0.5f * asinf(fminf(1.0f, 9.81f * tdist / (75.0f * 75.0f)));
                if (eq_start(b, now, BC2_EiaGrenadeLauncher, 0.15, 2, -1.15f / fmaxf(tdist, 5.0f) + arc, BC2_EiaGrenadeLauncher)) b->glAt = now + randr(18, 32);
            }
            eqBusy = b->eqState != EQ_IDLE;
        }
        if (eqBusy) { /* equipment is firing */ }
        else if (tdist < 3.0f && now > b->meleeAt) {   /* close: the knife (one hit) */
            press(b, BC2_EiaMeleeAttack); b->meleeAt = now + 1.0; b->burstUntil = 0;
            static int knLogs; if (knLogs++ < 6) rt_log("combat: %s knifes an enemy at %.1f m", b->name, tdist);
        }
        else if (ready && yawErr < tol * 2.5f && clear) {
            if (now < b->burstUntil) { b->levels[BC2_EiaFire] = 1; b->fireTime += dt; b->lastShotAt = now; }
            else if (now > b->pauseUntil) {
                float burst, pause;
                switch (b->cls) {
                case CLS_RECON: burst = 0.07f; pause = randr(0.9f, 1.5f); break;
                case CLS_MEDIC: burst = randr(0.4f, 0.9f); pause = randr(0.2f, 0.45f); break;
                default: burst = tdist > 60 ? randr(0.12f, 0.25f) : randr(0.25f, 0.6f); pause = randr(0.2f, 0.5f); break;
                }
                b->burstUntil = now + burst; b->pauseUntil = b->burstUntil + pause;
                b->levels[BC2_EiaFire] = 1; b->lastShotAt = now;
            }
        }
        /* 1.6: never pull the trigger on a gadget slot (repair tool, launcher, kit) outside its own use - gun first */
        if (!eqBusy && b->levels[BC2_EiaFire] && b->soldier) {
            int w = eng_soldier_weapon(b->soldier);
            if (w >= 2) { b->levels[BC2_EiaFire] = 0; b->burstUntil = 0; eng_soldier_set_weapon(b->soldier, 0); b->wpnOffSince = 0;
                static int gl; if (gl++ < 8) rt_log("bot %s: about to shoot with weapon %d (a gadget) - primary out first", b->name, w); }
        }
        float magSeconds = b->cls == CLS_MEDIC ? 9.0f : b->cls == CLS_RECON ? 0.5f : 2.6f;
        if (!eqBusy && b->fireTime > magSeconds) { press(b, BC2_EiaReload); b->fireTime = 0; b->reloadUntil = now + (b->cls == CLS_MEDIC ? 5.0 : 2.8); b->burstUntil = 0; }
        /* hand grenade: held for a moment (throws on release), lobbed */
        if (g_cfg.grenades && !eqBusy && tdist > 8 && tdist < 35 && now > b->grenadeAt && b->visible && now - b->targetSince > 1.2 && clear) {
            b->heldUntil[BC2_EiaThrowGrenade] = now + 0.35; b->grenadeHold = now + 0.6;
            b->grenadeAt = now + randr(14, 30); b->burstUntil = 0; b->pauseUntil = now + 1.2;
        }
    } else {
        b->strafe = 0;
        if (!eqBusy && b->fireTime > 0.8f && now > b->reloadUntil) { press(b, BC2_EiaReload); b->fireTime = 0; b->reloadUntil = now + 2.8; }
    }
}

static int g_physStep = 1, g_moveStrategy = 1;
static int now_unpin(const Bot *b) { return rt_now() < b->unpinUntil; }
static void push_input(Bot *b, float dt, uint32_t soldier)
{
    void *input = g_inputs[b->inputSlot]; unsigned char *st = g_staging[b - g_bots];
    for (int a = 0; a < BC2_EiaUndefined; a++) {
        float v = b->levels[a];
        if (b->pulse[a] > 0) { v = b->pulse[a] == 2 ? 1.0f : 0.0f; b->pulse[a]--; }   /* one tick down, one tick up */
        eng_state_set_level(st, a, v);
    }
    eng_state_set_dt(st, dt > 0 && dt < 0.2f ? dt : 1.0f / 30);
    float yaw = b->yaw, pitch = b->pitch;
    memcpy(st + ST_YAW, &yaw, 4); memcpy(st + ST_PITCH, &pitch, 4);
    /* The native AI stamps every input with the simulation frame number; the soldier's movement code
       ignores throttle/strafe from an input whose frame is stale (0.6 bots stood still because of this). */
    uint32_t tick = eng_game_tick(); memcpy(st + ST_TICK, &tick, 4);
    eng_input_push(input, st);
    if (soldier) eng_soldier_set_aiming(soldier, yaw, pitch);
    /* BC2 only walks a soldier when a network input frame arrives for it (ServerConnection -> soldier
       vt+0x110). AI players have no connection, so their throttle never moved them (0.6/0.6.1).
       Phoenix runs that same step: movement input, then the character physics step. */
    if (soldier && g_cfg.moveStep) {
        static int logged, failed, fastTotal;
        float stepDt = dt > 0 && dt < 0.2f ? dt : 1.0f / 30;
        /* 0.6.3-0.7.1 stopped Phoenix's physics step when a bot moved > 6.5 m/s for a while ("the server
           steps them itself"). Live 0.7.1 tripped it from a bot riding a vehicle, after which no bot walked
           properly. The step is confirmed needed on this server, so speed is only logged now. */
        if (b->stepPrevValid) {
            float v = dist2d(b->pos, b->stepPrev) / stepDt;
            if (v > 8.0f && v < 40 && b->levels[BC2_EiaSprint] < 0.5f) {
                if (++b->fastTicks == 90 && fastTotal++ < 3) rt_log("move: %s moved %.1f m/s for 3 s on foot (riding something?) - physics step kept", b->name, v);
            } else b->fastTicks = 0;
        }
        memcpy(b->stepPrev, b->pos, 12); b->stepPrevValid = 1;
        /* How the character is driven. 1: player-input mode (AI flag cleared, throttle/strafe drive it).
           2: AI mode driven by velocity, the way the single-player AI moves its soldiers.
           Phoenix starts with 1 and switches to 2 if bots that try to walk do not move. */
        {   static int modeLogged, modeFailed, fails, wins, announced;
            float speed = b->levels[BC2_EiaSprint] > 0.5f ? 6.3f : 3.5f;
            float thr = b->levels[BC2_EiaThrottle], str = b->levels[BC2_EiaStrafe];
            if (now_unpin(b)) {
                /* unpin: velocity-driven for a moment - a hop up and away from what holds the bot */
                float vx = sinf(b->unpinHeading) * 4.5f, vz = cosf(b->unpinHeading) * 4.5f;
                eng_character_ai_mode(soldier);
                if (b->unpinKick) { b->unpinKick = 0; eng_character_push(soldier, vx, 3.0f, vz); }
                else eng_character_drive_velocity(soldier, vx, vz);
            } else if (g_moveStrategy == 1) {
                int m = eng_character_player_mode(soldier);
                if (m == 1 && modeLogged < 3) { modeLogged++; rt_log("move: %s character switched from AI-velocity mode to player-input mode", b->name); }
                if (m == 0 && modeFailed < 3) { modeFailed++; rt_log("move: %s character mode could not be switched (unknown layout or AI rigid body)", b->name); }
            } else {
                float h = -b->yaw, fx = sinf(h), fz = cosf(h);
                float vx = (fx * thr - fz * str) * speed, vz = (fz * thr + fx * str) * speed;
                eng_character_ai_mode(soldier);
                if (!eng_character_drive_velocity(soldier, vx, vz) && modeFailed < 6) { modeFailed++; rt_log("move: %s velocity could not be written", b->name); }
            }
            /* did trying to walk move the bot? */
            if (fabsf(thr) + fabsf(str) > 0.5f) {
                if (b->tryTime <= 0) memcpy(b->tryStart, b->pos, 12);
                b->tryTime += stepDt;
                if (b->tryTime > 5) {
                    float moved = dist2d(b->pos, b->tryStart); b->tryTime = 0;
                    if (moved < 0.75f) fails++; else wins++;
                    if (wins >= 3 && !(announced & g_moveStrategy)) { announced |= g_moveStrategy; rt_log("move: bots are walking (%s)", g_moveStrategy == 1 ? "player-input mode" : "AI velocity mode"); }
                    if (fails >= 4 && wins == 0) {
                        if (g_moveStrategy == 1) { g_moveStrategy = 2; fails = 0; rt_log("move: player-input mode did not move the bots - switching to AI velocity mode"); }
                        else if (fails == 4) { char dbg[400]; eng_debug_character(soldier, dbg, sizeof dbg); rt_log("move: AI velocity mode did not move the bots either - %s", dbg); }
                    }
                }
            } else b->tryTime = 0;
        }
        int r = eng_soldier_move_step(soldier, b->player, input, stepDt, g_physStep);
        if (r && logged < 3) { logged++; rt_log("move: %s soldier %08x stepped (%s)", b->name, soldier, r == 2 ? "movement input + character physics step" : "movement input only"); }
        if (!r && failed < 5) { failed++; rt_log("move: %s soldier %08x - entry/physics layout not recognised, movement step skipped", b->name, soldier); }
    }
}

/* ---------------------------------------------------------------- tick */
/* bot damage to human players (BotDamage, default 1.2): when a human loses health while a bot is shooting at them, the
   loss is scaled up by writing the soldier's health (a hit that would kill anyway is left to the game, so kills are
   still credited by the game itself) */
static void damage_boost(double now)
{
    static uint32_t who[64]; static float last[64];
    if (g_cfg.botDamage <= 1.001f && g_cfg.botDamage >= 0.999f) return;
    for (int i = 0; i < g_world.obs.count && i < 64; i++) {
        const BC2ObservedPlayer *p = &g_world.obs.players[i];
        if (p->isAI || !p->soldierValid || p->entry || !p->controllable) { who[i] = 0; continue; }
        if (who[i] != p->pointer) { who[i] = p->pointer; last[i] = p->health; continue; }
        float h = p->health, lost = last[i] - h;
        if (lost > 0.5f && h > 0) {
            int byBot = 0;
            for (int k = 0; k < MAX_BOTS && !byBot; k++) { const Bot *b = &g_bots[k];
                if (b->used && b->team != (int)p->team && ((b->target == p->pointer && now - b->lastShotAt < 0.3) || (b->tTgtRef == p->pointer && now < b->engageUntil))) byBot = 1; }
            if (byBot) {
                float nh = h - lost * (g_cfg.botDamage - 1.0f);
                if (nh > 1.0f && nh < h) { uint32_t bits; memcpy(&bits, &nh, 4); if (eng_write32(p->controllable + 0x34, bits)) h = nh; }
            }
        }
        last[i] = h;
    }
}
/* 1.5.1: what a human is next to / using: ServerPlayer::update (humans only) keeps the interaction in reach at +0xC98
   (weak reference: [[+0xC98]] - 4) and flags a finished arm (+0xCA4) or defuse (+0xCA5). Logged, and an M-COM Phoenix
   does not know yet starts a new search for them. */
static void human_interactions(double now)
{
    static uint32_t lastEnt[64]; static uint8_t lastArm[64], lastDis[64];
    for (int i = 0; i < g_world.obs.count && i < 64; i++) {
        const BC2ObservedPlayer *p = &g_world.obs.players[i];
        if (p->isAI || !p->soldierValid) { lastEnt[i] = 0; continue; }
        uint32_t w = 0, t = 0, ent = 0, vt = 0; uint8_t arm = 0, dis = 0;
        if (eng_u32(p->pointer + 0xc98, &w) && eng_ptr(w) && eng_u32(w, &t) && eng_ptr(t)) { ent = t - 4; if (!eng_u32(ent, &vt) || vt != RB(VA_INTERACTION_VT)) ent = 0; }
        eng_read(p->pointer + 0xca4, &arm, 1); eng_read(p->pointer + 0xca5, &dis, 1);
        if (ent && ent != lastEnt[i]) {
            uint32_t data = 0; int32_t type = 0, team = 0; float pos[3] = {0, 0, 0};
            eng_u32(ent + 0x0c, &data); if (eng_ptr(data)) { eng_read(data + 172, &type, 4); eng_read(data + 160, &team, 4); }
            eng_entity_position(ent, pos);
            int known = 0; for (int f = 0; f < g_world.cpCount; f++) for (int j = 0; j < g_world.cps[f].objN; j++) if (g_world.cps[f].objEnt[j] == ent) known = 1;
            static int hl; if (hl++ < 40) rt_log("objectives: a human (team %d) is at interaction 0x%08lx type %d team %d at (%.1f, %.1f, %.1f)%s", (int)p->team, (unsigned long)ent, type, team, pos[0], pos[1], pos[2],
                                                 known ? "" : " - not one Phoenix knows: searching again");
            if (!known && (type == 2 || type == 3) && (g_world.mode == MODE_RUSH || g_world.mode == MODE_SQRUSH)) { g_world.cpCount = 0; }
        }
        if (arm && !lastArm[i]) rt_log("objectives: a human (team %d) ARMED an M-COM (interaction 0x%08lx)", (int)p->team, (unsigned long)ent);
        if (dis && !lastDis[i]) rt_log("objectives: a human (team %d) DEFUSED an M-COM (interaction 0x%08lx)", (int)p->team, (unsigned long)ent);
        lastEnt[i] = ent; lastArm[i] = arm; lastDis[i] = dis;
    }
}
static void bot_died(Bot *b, double now)
{
    if (b->state != BS_ALIVE) return;
    rt_log("bot %s: died (life %d, %.0f s)", b->name, b->lives, now - b->stateSince);
    if (!b->vehSince && now - g_world.levelSince > 10) learn_death(b->pos);
    if (b->owOn && dist2d(b->pos, b->owSpot) < 3) learn_spot_result(b->owSpot, 0, 0, 1);
    if (b->lspot >= 0) b->lspot = -1;
    if (g_turOwner == (int)(b - g_bots)) turret_release(b);
}
/* 1.6.10: time spent in bot thinking, vehicle driving and the soldier movement step (seconds, since the last read) */
double g_perfBots[3];
void bots_perf(double *out) { memcpy(out, g_perfBots, sizeof g_perfBots); memset(g_perfBots, 0, sizeof g_perfBots); }
void bots_tick(double now, float dt)
{
    if (g_stopping || !g_world.obsReady || !g_world.manager) return;
    { static int tried; if (!tried && g_cfg.keepPlayers) { tried = 1; g_keepOn = eng_keep_ai_players(1);
        rt_log(g_keepOn ? "players: dead bots keep their player and redeploy (KeepBotPlayers=1) - no \"switched teams\" messages, scores add up"
                        : "players: could not find the AI-player deletion in onKilled - every bot life is a new player (as before 1.5)"); } }
    { static int silenced; if (!silenced) { silenced = 1;
        rt_log(eng_silence_spawn_assert() ? "spawn: 'AI already spawned from the spawnPoint' assert set to Ignore Always (no more dialogs)"
                                          : "spawn: could not find the spawn-point assert - the dialog may still appear (press Ignore Always)"); } }
    int levelReady = g_world.spawnCount > 0 && g_world.kitCount[1] > 0 && g_world.kitCount[2] > 0 && now - g_world.levelSince > 3;
    roster_balance(now);
    {   /* scoreboard hunt (1.5): the ghosted player fields of the first human seen, next to a live bot's */
        static int humanDiag; static double diagAt;
        if (humanDiag < 1 && now > diagAt) { diagAt = now + 5;
            uint32_t hp = 0, bp = 0; const char *bn = "";
            for (int i = 0; i < g_world.obs.count; i++) if (!g_world.obs.players[i].isAI && g_world.obs.players[i].soldierValid) { hp = g_world.obs.players[i].pointer; break; }
            for (int i = 0; i < MAX_BOTS && hp; i++) if (g_bots[i].used && g_bots[i].state == BS_ALIVE && g_bots[i].player) { bp = g_bots[i].player; bn = g_bots[i].name; break; }
            if (hp && bp) { humanDiag++; ghost_fields(hp, "human"); ghost_fields(bp, bn); }
        } }
    director_update(now);
    learn_tick(now);
    indoor_tick();
    { static double lhAt; if (now > lhAt) { lhAt = now + 0.5; learn_humans(now); human_interactions(now); } }
    damage_boost(now);
    { static double vhAt; if (now > vhAt) { vhAt = now + 1.0;   /* full health of each vehicle (for repairs) */
        for (int i = 0; i < g_world.vehCount; i++) { VehicleInfo nv; if (world_vehicle_read(g_world.veh[i].entity, &nv)) veh_max_health(&nv); } } }
    { static double reachAt; if (now > reachAt) { reachAt = now + 1.0; reach_learn(now); } }
    /* flags are where walls get blown open: re-probe a patch around one flag every 8 s */
    { static double rpAt; static int rpFlag;
      if (now > rpAt && g_world.cpCount) { rpAt = now + 8.0; rpFlag = (rpFlag + 1) % g_world.cpCount;
        const CapturePoint *cp = &g_world.cps[rpFlag]; float a = randr(0, TWO_PI), r = randr(0, 25);
        float p[3] = {cp->pos[0] + cosf(a) * r, cp->pos[1], cp->pos[2] + sinf(a) * r}; navg_reprobe(p, 10.0f); } }
    for (int i = 0; i < MAX_BOTS; i++) {
        Bot *b = &g_bots[i];
        if (!b->used) continue;
        int idx = b->player ? world_find_player(b->player, b->id) : -1;
        const BC2ObservedPlayer *self = idx >= 0 ? &g_world.obs.players[idx] : NULL;
        if (b->player && (!self || !self->isAI)) {
            /* the engine deleted it: the soldier died (or the round ended) */
            bot_died(b, now);
            b->player = b->id = b->soldier = 0; b->state = BS_WAIT; b->stateSince = now;
            b->nextAt = now + g_cfg.respawnDelay + randr(0, 2.5f);
            b->haveGoal = 0; b->goalCp = -1; b->pathLen = 0; b->target = 0;
            if (bots_on_team(b->team) > wanted_on_team(b->team)) { rt_log("roster: %s retires", b->name); b->used = 0; }
            continue;
        }
        switch (b->state) {
        case BS_WAIT:
            if (!levelReady || now < b->nextAt || now < g_nextCreate) break;
            g_nextCreate = now + 0.25;
            if (!create_life(b, now)) { b->nextAt = now + 5; break; }
            break;
        case BS_DEPLOYING:
            if (self && self->soldierValid && self->transformValid) {
                b->state = BS_ALIVE; b->stateSince = now; b->soldier = self->controllable; b->lastSeenAlive = now;
                b->target = 0; b->haveGoal = 0; b->stuckCount = 0; b->fireTime = 0; b->pitch = 0;
                b->eqState = EQ_IDLE; b->reviveP = 0; b->knifeLeft = 0; b->detourUntil = 0; b->pgAt = 0;
                b->aDist = 1e30f; b->aProgressAt = now; b->aArrivedAt = 0; b->goalCp = -1; b->owOn = 0; b->viaOn = 0; b->lspot = -1; b->owCounted = 0;
                b->vaultUntil = b->crouchUntil = b->unpinUntil = 0; b->crouched = 0; b->frozenSince = 0; b->unpinTries = 0; b->skyCount = 0; b->skyY = self->position[1]; b->giveUps = 0; b->lastVeh = 0;
                b->vehicle = 0; b->vehRole = 0; b->vehSince = 0; b->vehCheckAt = now + randr(2, 6);
                b->grenadeAt = now + randr(6, 20); b->glAt = now + randr(5, 15); b->gadgetAt = now + randr(10, 30);
                memset(b->heldUntil, 0, sizeof b->heldUntil);
                b->yaw = yaw_from_heading(atan2f(self->forward[0], self->forward[2]));
                memcpy(b->progressPos, self->position, 12); b->progressAt = now;
                rt_log("bot %s: deployed as %s on team %d at (%.1f, %.1f, %.1f)%s", b->name, cls_name(b->cls), b->team,
                       self->position[0], self->position[1], self->position[2], b->lives > 1 && g_keepOn ? " (same player)" : "");
                if (b->lives > 1 && g_keepOn && !g_keepWins++) rt_log("players: a kept bot player redeployed - bots stay the same players all round");
                b->rockets = 5;
                break;
            }
            input_neutral_push(b, dt);
            if (!levelReady || now < b->nextAt) break;
            if (self && self->controllable) { b->nextAt = now + 0.5; break; }   /* soldier still attached (in a vehicle?) */
            if (b->newLife) {   /* a kept player: this life's class and weapons, as a new player would get */
                b->newLife = 0;
                if (rt_randf() < 0.6f) b->cls = (int)(rt_rand() % CLS_COUNT);
                if (!apply_kit(b)) { remove_player(b, "new kit refused"); b->state = BS_WAIT; b->nextAt = now + 3; break; }
                b->lives++;
            }
            if (b->lives > 1 && g_keepOn && b->deployTries >= 15) {
                /* the kept player does not spawn again: back to a new player per life for this bot */
                remove_player(b, "kept player would not redeploy"); b->state = BS_WAIT; b->nextAt = now + 2;
                if (++g_keepFails >= 4 && !g_keepWins) { g_keepOn = 0; eng_keep_ai_players(0); rt_log("players: kept bot players do not redeploy on this server - back to a new player per life"); }
                break;
            }
            {
                uint32_t sel = 0;
                if (!eng_u32(b->player + PL_SOLDIER_DAT, &sel) || !sel) { if (!apply_kit(b)) { remove_player(b, "kit lost"); b->state = BS_WAIT; b->nextAt = now + 5; break; } }
                uint32_t spawn = choose_spawn(b);
                if (!spawn) { b->nextAt = now + 1.5; if (++b->deployTries % 20 == 0) rt_log("bot %s: no enabled spawn point for team %d yet", b->name, b->team); break; }
                int si = g_lastSpawnIndex;
                if (si >= 0 && si < g_world.spawnCount) spawn_release_ai(g_world.spawns[si].entity);
                int ok = eng_selected_spawn(b->player, spawn);
                if (ok && si >= 0 && si < g_world.spawnCount) { g_spawnBusyEntity[si] = g_world.spawns[si].entity; g_spawnBusyUntil[si] = now + 1.6; }
                b->deployTries++;
                if (g_cfg.verbose || b->deployTries == 3) rt_log("bot %s: deploy try %d at spawn %lu -> %s", b->name, b->deployTries, (unsigned long)spawn, ok ? "accepted" : "refused");
                b->nextAt = now + (ok ? 2.0 : 1.2);
                if (!ok && g_cfg.quietTeam && b->deployTries == 10) {
                    /* the spawn system may need the team-change notification: fall back to setTeam for every bot */
                    g_cfg.quietTeam = 0; rt_log("spawn: deploys refused with the quiet team join - using setTeam again (the \"switched teams\" message comes back)");
                    eng_set_team(b->player, 0); eng_set_team(b->player, (uint32_t)b->team);
                }
                if (!ok && b->deployTries >= 40) { remove_player(b, "spawn refused 40 times"); b->state = BS_WAIT; b->nextAt = now + 8; }
            }
            break;
        case BS_ALIVE: {
            uint32_t veh = g_cfg.vehicles && b->player ? eng_player_controlled(b->player) : 0;
            if (veh && (veh == self->controllable || eng_vehicle_seats(veh) <= 0)) veh = 0;
            /* only the controlled entity decides (0.7.2 also trusted the seat list and a bot that the game had
               already put back on foot tried to leave a BMD-3 for minutes) */
            if (veh) {
                b->lastSeenAlive = now;
                { double tv0 = rt_now(); vehicle_brain(b, now, dt, veh); g_perfBots[1] += rt_now() - tv0; }
                { double tp0 = rt_now(); push_input(b, dt, 0); g_perfBots[2] += rt_now() - tp0; }   /* vehicle controls; no soldier step or aiming */
                break;
            }
            if (b->vehSince) { b->vehRole = VR_NONE; b->vehicle = 0; b->vehSince = 0; if (b->vehCheckAt < now + 6) b->vehCheckAt = now + 6; b->pathLen = 0; }   /* out of the vehicle */
          }
            if (!self->soldierValid || !self->transformValid) {
                /* soldier gone but player kept (1.5: a death with KeepBotPlayers; also round ends): a new life on the same player */
                if (!self->controllable) {
                    bot_died(b, now);
                    b->haveGoal = 0; b->goalCp = -1; b->pathLen = 0; b->pathPending = 0; b->target = 0; b->vehRole = VR_NONE; b->vehicle = 0; b->vehSince = 0;
                    if (bots_on_team(b->team) > wanted_on_team(b->team)) { remove_player(b, "team has enough bots"); rt_log("roster: %s retires", b->name); b->used = 0; break; }
                    b->state = BS_DEPLOYING; b->stateSince = now; b->deployTries = 0; b->nextAt = now + g_cfg.respawnDelay + randr(0, 2.5f); b->newLife = 1;
                }
                input_neutral_push(b, dt);
                break;
            }
            b->soldier = self->controllable; b->lastSeenAlive = now;
            { double tb0 = rt_now(); brain(b, now, dt, self); g_perfBots[0] += rt_now() - tb0; }
            if (b->redeploy) {
                b->redeploy = 0;
                const char *what = now - b->frozenSince > 900 ? "stuck in the air" : "pinned in place";
                /* 1.6.6: kill the soldier (the engine's own suicide) and let it respawn as the same player. 1.6.5 removed the
                   player with leaveGame, which only detaches a living soldier: it stayed behind as a ghost (no AI, no damage)
                   and floated off if it was detached mid-hop - 109 of them in one Panama Canal round */
                uint32_t ctl = 0; eng_u32(b->player + PL_CONTROLLED, &ctl);
                if ((!ctl || ctl == b->soldier || eng_vehicle_seats(ctl) <= 0) && eng_soldier_kill(b->soldier)) {
                    rt_log("bot %s: %s at (%.0f, %.0f, %.0f) - killed it to respawn (no soldier left behind)", b->name, what, b->pos[0], b->pos[1], b->pos[2]);
                    b->haveGoal = 0; b->goalCp = -1; b->pathLen = 0; b->target = 0; b->frozenSince = now;
                    input_neutral_push(b, dt);
                    break;
                }
                rt_log("bot %s: %s at (%.0f, %.0f, %.0f) - redeploying it", b->name, what, b->pos[0], b->pos[1], b->pos[2]);
                remove_player(b, "pinned/airborne");
                b->state = BS_WAIT; b->stateSince = now; b->nextAt = now + 1.0; b->haveGoal = 0; b->goalCp = -1; b->pathLen = 0; b->target = 0;
                break;
            }
            { double tp0 = rt_now(); push_input(b, dt, b->soldier); g_perfBots[2] += rt_now() - tp0; }
            break;
        default: break;
        }
    }
}

/* 1.6.6: stopping takes a few ticks. First every living bot soldier on foot is killed (the engine's suicide) and the
   KeepBotPlayers patch is taken off, so the engine itself removes those AI players when they die (onKilled: leaveGame +
   deletePlayer). The rest - dead, waiting, in vehicles - are removed as before. 1.6.5 removed living bots with leaveGame,
   which left their soldiers standing in the world as ghosts. Returns 1 when done. */
int bots_stop_step(double now)
{
    static double began; static int phase;
    if (!g_stopping) { g_stopping = 1; phase = 0; began = now; learn_save_now(); }
    if (phase == 0) {
        int killed = 0;
        /* the patch off FIRST: whether the engine processes a death at once or a frame later, it then removes the AI
           player itself (deletePlayer) and we never leaveGame a player whose soldier is dying */
        if (g_keepOn) { eng_keep_ai_players(0); g_keepOn = 0; }
        for (int i = 0; i < MAX_BOTS; i++) { Bot *b = &g_bots[i]; if (!b->used || !b->player) continue;
            uint32_t sol = living_soldier(b); if (sol && eng_soldier_kill(sol)) { killed++; b->stopKilled = 1; } }
        rt_log("stop: %d living bot soldier(s) killed, the engine removes their players", killed);
        phase = 1; began = now;
        if (killed) return 0;
    }
    BC2Observation o; int haveObs = phx_observe_now(&o), left = 0;
    for (int i = 0; i < MAX_BOTS; i++) {
        Bot *b = &g_bots[i]; if (!b->used) continue;
        if (b->player) {
            int present = 0;
            if (haveObs) for (int k = 0; k < o.count; k++) if (o.players[k].pointer == b->player && o.players[k].id == b->id) present = 1;
            if (!present && haveObs) { b->player = b->id = b->soldier = 0; }
            else if (!b->stopKilled && now - began < 3.0) {   /* in a vehicle: out first, then the same suicide */
                uint32_t sol = 0, ctl = 0; eng_u32(b->player + PL_SOLDIER, &sol); eng_u32(b->player + PL_CONTROLLED, &ctl);
                if (sol && ctl && ctl != sol) { if (now - b->stopExitAt > 0.5) { b->stopExitAt = now; eng_exit_vehicle(b->player); } left++; continue; }
                uint32_t live = living_soldier(b);
                if (live && eng_soldier_kill(live)) { b->stopKilled = 1; left++; continue; }
                remove_player(b, "stop requested");
            }
            else if (!b->stopKilled || now - began > 3.0) {
                uint32_t sol = 0; eng_u32(b->player + PL_SOLDIER, &sol);
                if (b->stopKilled && sol) { left++; continue; }   /* still dying: never leaveGame a player whose soldier is mid-death */
                remove_player(b, "stop requested");
            } else { left++; continue; }
        }
        if (!b->player) { b->used = 0; b->stopKilled = 0; }
    }
    if (left && now - began < 4.0) return 0;
    for (int i = 0; i < MAX_BOTS; i++) g_bots[i].used = 0;
    return 1;
}
void bots_stop_all(void)
{   /* one-shot stop (harness): run the steps on the spot */
    double t = rt_now();
    for (int k = 0; k < 200 && !bots_stop_step(t); k++) t += 0.05;
}

void bots_status(void)
{
    int st[5][4] = {{0}};
    for (int i = 0; i < MAX_BOTS; i++) if (g_bots[i].used && g_bots[i].team >= 1 && g_bots[i].team <= 4) st[g_bots[i].team][g_bots[i].state]++;
    char flags[200]; int used = 0; flags[0] = 0;
    for (int c = 0; c < g_world.cpCount && used < 180; c++)
        used += snprintf(flags + used, sizeof flags - used, " %c:%d%s", 'A' + c, g_world.cps[c].owner, g_world.cps[c].attacker ? "*" : "");
    char nav[96]; navg_status(nav, sizeof nav);
    rt_log("status: team1 bots %d alive/%d deploying/%d waiting, humans %d | team2 bots %d/%d/%d, humans %d | flags%s | %s | rays %u",
           st[1][BS_ALIVE], st[1][BS_DEPLOYING], st[1][BS_WAIT], g_world.humans[1], st[2][BS_ALIVE], st[2][BS_DEPLOYING], st[2][BS_WAIT], g_world.humans[2],
           g_world.cpCount ? flags : " none found", nav, eng_ray_count());
    /* movement check: a few bots, distance walked since the last status line */
    int shown = 0;
    for (int i = 0; i < MAX_BOTS && shown < 4; i++) {
        Bot *b = &g_bots[i]; if (!b->used || b->state != BS_ALIVE) continue;
        float moved = b->statusPosValid ? dist2d(b->pos, b->statusPos) : -1;
        memcpy(b->statusPos, b->pos, 12); b->statusPosValid = 1;
        if (moved >= 0 && moved < 1.0f && shown == 0 && fabsf(b->levels[BC2_EiaThrottle]) > 0.2f && b->soldier) {
            char dbg[400]; eng_debug_character(b->soldier, dbg, sizeof dbg); rt_log("  move: %s is not moving - %s", b->name, dbg);
        }
        rt_log("  move: %s t%d at (%.0f, %.0f, %.0f) walked %.1f m, %s %s %.0f m away, throttle %.2f strafe %.2f sprint %d, %s, stuck %d", b->name, b->team,
               b->pos[0], b->pos[1], b->pos[2], moved, b->vehRole ? (b->vehRole == 1 ? "driving to" : "passenger to") : role_name(b->aRole),
               b->goalCp >= 0 ? g_world.cps[b->goalCp].name : (b->haveGoal ? "enemies" : "-"),
               b->haveGoal ? dist2d(b->pos, b->goal) : 0, b->levels[BC2_EiaThrottle], b->levels[BC2_EiaStrafe], (int)b->levels[BC2_EiaSprint],
               b->target ? (b->visible ? "enemy in sight" : "enemy lost") : "no enemy", b->stuckCount);
        shown++;
    }
    if (g_cfg.verbose) for (int i = 0; i < MAX_BOTS; i++) {
        Bot *b = &g_bots[i]; if (!b->used || b->state != BS_ALIVE) continue;
        rt_log("  %s t%d %s at (%.0f, %.0f, %.0f) %s %c%s target %s path %d/%d stuck %d", b->name, b->team, cls_name(b->cls),
               b->pos[0], b->pos[1], b->pos[2], role_name(b->aRole), b->goalCp >= 0 ? 'A' + b->goalCp : '-', b->holding ? " (holding)" : "",
               b->target ? (b->visible ? "visible" : "lost") : "none", b->pathIdx, b->pathLen, b->stuckCount);
    }
    (void)state_name;
}
