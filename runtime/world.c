/* World snapshot: players, capture points (flags), spawn points, team kits. Simulation thread only,
   except the capture-point memory scan, which runs on a worker thread and only reads memory. */
#ifdef PHX_HARNESS
#include "harness_win.h"
#else
#include "win_common.h"
#endif
#include "rt.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <ctype.h>

World g_world;

static int obs_read(void *ctx, uint32_t a, void *out, unsigned n) { (void)ctx; return eng_read(a, out, n); }

/* ---------------- capture point scan (worker thread) ---------------- */
#define SCAN_MAX 64
static volatile LONG g_scanState;          /* 0 idle, 1 running, 2 done */
#define VEH_SCAN_MAX 2048   /* the vehicle vtable shows up in many stale copies: 0.8.0 kept 64 and missed Arica's tanks */
static uint32_t g_scanFound[SCAN_MAX], g_scanVeh[VEH_SCAN_MAX];
static volatile LONG g_scanCount, g_scanVehCount;
static uint32_t g_scanValue, g_scanVehValue, g_scanSpValue;   /* capture point, vehicle and vehicle-spawner vtables, found in one pass */
static uint32_t g_scanSp[256]; static volatile LONG g_scanSpCount;
static uint32_t g_scanObjValue, g_scanObj[128]; static volatile LONG g_scanObjCount;   /* 1.5: interaction entities (M-COMs) */
#define ROAD_SCAN_MAX 1024
static uint32_t g_scanRoadValue[3], g_scanRoad[3][ROAD_SCAN_MAX]; static volatile LONG g_scanRoadCount[3];   /* 1.6.6: RoadData, TerrainSplineDecal, TerrainSplineData */
static double g_scanLastStart = -1000;
static unsigned g_scanForLevel;
static DWORD g_scanMs;

#ifdef PHX_HARNESS
static DWORD WINAPI scan_thread(void *arg)
{
    (void)arg;
    g_scanMs = 0;
    int found = harness_scan(g_scanValue, g_scanFound, SCAN_MAX);
    InterlockedExchange(&g_scanVehCount, harness_scan(g_scanVehValue, g_scanVeh, VEH_SCAN_MAX));
    InterlockedExchange(&g_scanSpCount, g_scanSpValue ? harness_scan(g_scanSpValue, g_scanSp, 256) : 0);
    InterlockedExchange(&g_scanObjCount, g_scanObjValue ? harness_scan(g_scanObjValue, g_scanObj, 128) : 0);
    for (int r = 0; r < 3; r++) InterlockedExchange(&g_scanRoadCount[r], g_scanRoadValue[r] ? harness_scan(g_scanRoadValue[r], g_scanRoad[r], ROAD_SCAN_MAX) : 0);
    InterlockedExchange(&g_scanCount, found);
    InterlockedExchange(&g_scanState, 2);
    return 0;
}
#else
static DWORD WINAPI scan_thread(void *arg)
{
    (void)arg;
    DWORD t0 = GetTickCount();
    static uint32_t buf[16384];
    uint32_t addr = 0x10000; int found = 0, veh = 0, sp = 0, ob = 0, rd[3] = {0, 0, 0};
    MEMORY_BASIC_INFORMATION mbi;
    while (addr < 0xfffe0000u && (found < SCAN_MAX || veh < VEH_SCAN_MAX)) {
        if (!VirtualQuery((void *)(uintptr_t)addr, &mbi, sizeof mbi)) break;
        uint32_t base = (uint32_t)(uintptr_t)mbi.BaseAddress, size = (uint32_t)mbi.RegionSize;
        uint32_t next = base + size; if (next <= addr) break;
        DWORD prot = mbi.Protect & 0xff;
        if (mbi.State == MEM_COMMIT && (mbi.Type == MEM_PRIVATE || mbi.Type == MEM_MAPPED) && !(mbi.Protect & PAGE_GUARD) &&
            (prot == PAGE_READWRITE || prot == PAGE_EXECUTE_READWRITE)) {
            for (uint32_t p = base; p < next; p += sizeof buf) {
                uint32_t n = next - p; if (n > sizeof buf) n = sizeof buf;
                if (!phx_read(GetCurrentProcess(), p, buf, n)) continue;
                for (uint32_t k = 0; k < n / 4; k++) {
                    if (buf[k] == g_scanValue && found < SCAN_MAX) g_scanFound[found++] = p + k * 4;
                    else if (buf[k] == g_scanVehValue && g_scanVehValue && veh < VEH_SCAN_MAX) g_scanVeh[veh++] = p + k * 4;
                    else if (buf[k] == g_scanSpValue && g_scanSpValue && sp < 256) g_scanSp[sp++] = p + k * 4;
                    else if (buf[k] == g_scanObjValue && g_scanObjValue && ob < 128) g_scanObj[ob++] = p + k * 4;
                    else if (g_scanRoadValue[0] && (buf[k] == g_scanRoadValue[0] || buf[k] == g_scanRoadValue[1] || buf[k] == g_scanRoadValue[2])) {
                        int r = buf[k] == g_scanRoadValue[0] ? 0 : buf[k] == g_scanRoadValue[1] ? 1 : 2;
                        if (rd[r] < ROAD_SCAN_MAX) g_scanRoad[r][rd[r]++] = p + k * 4; }
                }
            }
        }
        addr = next;
    }
    g_scanMs = GetTickCount() - t0;
    InterlockedExchange(&g_scanVehCount, veh);
    InterlockedExchange(&g_scanSpCount, sp);
    InterlockedExchange(&g_scanObjCount, ob);
    for (int r = 0; r < 3; r++) InterlockedExchange(&g_scanRoadCount[r], rd[r]);
    InterlockedExchange(&g_scanCount, found);
    InterlockedExchange(&g_scanState, 2);
    return 0;
}
#endif
void world_cp_scan_request(void)
{
    if (g_scanState == 1) return;
    g_scanValue = RB(VA_CAPTUREPOINT_VT);
    g_scanVehValue = g_cfg.vehicles ? RB(VA_VEHICLE_VT) : 0;
    g_scanSpValue = g_cfg.vehicles ? RB(VA_VEHICLE_SPAWN_VT) : 0;
    g_scanObjValue = g_cfg.objectives && (!g_world.cpCount || g_world.mode == MODE_RUSH || g_world.mode == MODE_SQRUSH) ? RB(VA_INTERACTION_VT) : 0;
    { int want = g_cfg.vehicles && roads_wanted();   /* 1.6.6: the level's roads, until found (3 tries a level) */
      g_scanRoadValue[0] = want ? RB(VA_ROAD_DATA_VT) : 0; g_scanRoadValue[1] = want ? RB(VA_TSPLINE_DECAL_VT) : 0; g_scanRoadValue[2] = want ? RB(VA_TSPLINE_VT) : 0; }
    g_scanForLevel = g_world.levelSerial;
    g_scanLastStart = rt_now();
    InterlockedExchange(&g_scanState, 1);
    HANDLE h = CreateThread(NULL, 0, scan_thread, NULL, 0, NULL);
    if (h) { SetThreadPriority(h, THREAD_PRIORITY_BELOW_NORMAL); CloseHandle(h); }
    else InterlockedExchange(&g_scanState, 0);
}

static float dist2d_w(const float *a, const float *b) { return hypotf(a[0] - b[0], a[2] - b[2]); }
static int read_cp(uint32_t e, CapturePoint *cp)
{
    uint32_t vt = 0, data = 0; uint8_t flags = 0;
    memset(cp, 0, sizeof *cp);
    if (!eng_u32(e, &vt) || vt != RB(VA_CAPTUREPOINT_VT) || !eng_u32(e + CP_DATA, &data) || !eng_ptr(data)) return 0;
    if (!eng_read(e + 0x26, &flags, 1) || !(flags & 0x08)) return 0;           /* spawned / active */
    float radius = 0, progress = 0; int32_t owner = 0, attacker = 0, only = 0, index = 0; uint8_t locked = 0;
    if (!eng_read(data + CPD_RADIUS, &radius, 4) || !isfinite(radius) || radius < 0 || radius > 400) return 0;
    if (radius < 6) radius = radius < 1 ? 15.0f : 10.0f;   /* Conquest flags store 0 or 1 here: the capture area is level geometry */
    if (!eng_read(e + CP_OWNER, &owner, 4) || owner < 0 || owner > 16) return 0;
    eng_read(e + CP_ATTACKER, &attacker, 4); eng_read(e + CP_PROGRESS, &progress, 4);
    eng_read(data + CPD_ONLY_TEAM, &only, 4); eng_read(data + CPD_NOCHANGE, &locked, 1); eng_read(data + CPD_INDEX, &index, 4);
    if (!eng_entity_position(e, cp->pos)) return 0;
    cp->entity = e; cp->data = data; cp->radius = radius; cp->owner = owner;
    cp->attacker = (attacker >= 0 && attacker <= 16) ? attacker : 0;
    cp->progress = isfinite(progress) ? progress : 0; cp->onlyTeam = only; cp->locked = locked; cp->index = index;
    if (!eng_string(data + CPD_NAME, cp->name, sizeof cp->name) || !cp->name[0])
        eng_string(data + CPD_LOCATION, cp->name, sizeof cp->name);
    if (!cp->name[0]) snprintf(cp->name, sizeof cp->name, "CP%d", index);
    return 1;
}

/* 1.5: an interaction entity (Rush M-COM arm / defuse point). GameInteractionEntityData: TeamId +160, HoldToInteractTime
   +164, InteractionEntityType +172, IsHumanAllowed +184; InteractionEntityData: UseWithinRadius +144 */
typedef struct ObjRaw { uint32_t e, data; float pos[3], hold, radius; int team, type, human, active; } ObjRaw;
static int read_obj(uint32_t e, ObjRaw *o)
{
    uint32_t vt = 0, data = 0; uint8_t flags = 0, human = 0; int32_t team = 0, type = 0; float hold = 0, radius = 0;
    memset(o, 0, sizeof *o);
    if (!eng_u32(e, &vt) || vt != RB(VA_INTERACTION_VT) || !eng_u32(e + 0x0c, &data) || !eng_ptr(data)) return 0;
    if (!eng_read(e + 0x26, &flags, 1)) return 0;
    eng_read(data + 160, &team, 4); eng_read(data + 164, &hold, 4); eng_read(data + 172, &type, 4); eng_read(data + 184, &human, 1); eng_read(data + 144, &radius, 4);
    if (!eng_entity_position(e, o->pos)) return 0;
    o->e = e; o->data = data; o->team = team >= 0 && team <= 4 ? team : -1; o->type = type; o->human = human;
    o->hold = isfinite(hold) && hold >= 0 && hold < 60 ? hold : 0; o->radius = isfinite(radius) && radius > 0 && radius < 50 ? radius : 2.0f;
    o->active = (flags >> 3) & 1;
    return 1;
}
/* InteractionEntityType (data +172): 2 = arm, 3 = defuse (ServerPlayer::entityInteract flags them at +0xCA4 / +0xCA5) */
static void objectives_collect(const uint32_t *list, int n)
{
    ObjRaw raw[96]; int nr = 0, logged = 0, typed = 0;
    for (int i = 0; i < n && nr < 96; i++) {
        ObjRaw o; if (!read_obj(list[i], &o)) continue;
        int dup = 0; for (int k = 0; k < nr; k++) if (raw[k].e == o.e) dup = 1; if (dup) continue;
        if (logged++ < 24) rt_log("objectives:   interaction 0x%08lx at (%.0f, %.0f, %.0f) team %d hold %.1f s type %d radius %.1f%s%s", (unsigned long)o.e,
                                  o.pos[0], o.pos[1], o.pos[2], o.team, o.hold, o.type, o.radius, o.active ? " active" : " inactive", o.human ? "" : " (not for humans)");
        if (o.type == 2 || o.type == 3) typed++;
        raw[nr++] = o;
    }
    /* one M-COM per spot: its arm and its defuse interaction stand together; only the stage that is on (an active one) */
    CapturePoint cps[MAX_CPS]; int kept = 0;
    for (int i = 0; i < nr; i++) {
        if (typed && raw[i].type != 2 && raw[i].type != 3) continue;   /* doors, ladders and other interactions */
        int k = 0; for (; k < kept; k++) if (dist2d_w(cps[k].pos, raw[i].pos) < 4 && fabsf(cps[k].pos[1] - raw[i].pos[1]) < 3) break;
        if (k == kept) { if (kept >= MAX_CPS) break; memset(&cps[k], 0, sizeof cps[k]); kept++; memcpy(cps[k].pos, raw[i].pos, 12); cps[k].isObj = 1; cps[k].radius = 6; cps[k].index = k; }
        CapturePoint *c = &cps[k];
        if (c->objN < 4) { c->objEnt[c->objN] = raw[i].e; c->objTeam[c->objN] = raw[i].team; c->objType[c->objN] = raw[i].type; c->objN++; }
        if (raw[i].type == 3) { c->disarmEnt = raw[i].e; if (raw[i].active) c->armed = 1; }
        else if (!c->armEnt || raw[i].active) c->armEnt = raw[i].e;
        if (raw[i].hold > c->holdTime) c->holdTime = raw[i].hold;
        if (raw[i].type == 3) c->disarmHold = raw[i].hold; else c->armHold = raw[i].hold;
        if (raw[i].active) c->locked = 2;   /* (marks "on" while collecting) */
        if (!c->entity) { c->entity = raw[i].e; c->data = raw[i].data; }
    }
    int w = 0; for (int k = 0; k < kept; k++) if (cps[k].locked == 2) { cps[k].locked = 0; cps[w] = cps[k]; cps[w].index = w; w++; }
    kept = w;
    if (!kept) { static unsigned lv; static int lc; if (lv != g_world.levelSerial) { lv = g_world.levelSerial; lc = 0; } if (lc++ < 3) rt_log("objectives: %d interaction entit(ies), %d arm/defuse, none active yet", nr, typed); return; }
    /* which team attacks: the team the arm interaction is for; else the team whose spawns are further from the M-COMs */
    int att = 0;
    for (int k = 0; k < kept && !att; k++) for (int j = 0; j < cps[k].objN; j++) if (cps[k].objType[j] != 3 && cps[k].objTeam[j] >= 1 && cps[k].objTeam[j] <= 2) { att = cps[k].objTeam[j]; break; }
    for (int k = 0; k < kept && !att; k++) for (int j = 0; j < cps[k].objN; j++) if (cps[k].objType[j] == 3 && cps[k].objTeam[j] >= 1 && cps[k].objTeam[j] <= 2) { att = 3 - cps[k].objTeam[j]; break; }
    if (!att) {
        float dsum[3] = {0, 0, 0}; int dn[3] = {0, 0, 0};
        for (int i = 0; i < g_world.spawnCount; i++) { const SpawnPoint *sp = &g_world.spawns[i]; if (!sp->posValid || !sp->enabled || sp->team < 1 || sp->team > 2) continue;
            float best = 1e9f; for (int k = 0; k < kept; k++) { float d = dist2d_w(sp->pos, cps[k].pos); if (d < best) best = d; } dsum[sp->team] += best; dn[sp->team]++; }
        att = (dn[1] && dn[2]) ? (dsum[1] / dn[1] > dsum[2] / dn[2] ? 1 : 2) : 1;
    }
    /* 1.6: only the stage being played. Isla Inocentes keeps every stage's arm interaction on from the start (6 M-COMs
       "on"), so 1.6.0 sent attackers to all three stages. Every M-COM seen this level is remembered in order of distance
       from the attackers' spawns; one that is no longer on was destroyed. The stage is the first 2 (Squad Rush: 1) of
       the order that are still standing. */
    {
        static struct { float pos[3], d; int gone, rank; } seen[32]; static int nSeen; static unsigned seenLevel;
        if (seenLevel != g_world.levelSerial) { seenLevel = g_world.levelSerial; nSeen = 0; }
        float ref[3] = {0, 0, 0}; int rn = 0;
        for (int i = 0; i < g_world.spawnCount; i++) { const SpawnPoint *sp = &g_world.spawns[i];
            if (!sp->posValid || !sp->enabled || sp->team != att) continue; ref[0] += sp->pos[0]; ref[2] += sp->pos[2]; rn++; }
        if (rn) { ref[0] /= rn; ref[2] /= rn; }
        for (int j = 0; j < nSeen; j++) {   /* standing ones still on? */
            int on = 0; for (int k = 0; k < kept; k++) if (dist2d_w(cps[k].pos, seen[j].pos) < 4) on = 1;
            if (!on && !seen[j].gone) { seen[j].gone = 1; rt_log("objectives: the M-COM at (%.0f, %.0f) is off now - destroyed", seen[j].pos[0], seen[j].pos[2]); }
        }
        int first = nSeen;
        for (int k = 0; k < kept; k++) {
            int j = 0; for (; j < nSeen; j++) if (dist2d_w(cps[k].pos, seen[j].pos) < 4) break;
            if (j == nSeen && nSeen < 32) { memcpy(seen[j].pos, cps[k].pos, 12); seen[j].d = rn ? dist2d_w(cps[k].pos, ref) : (float)j; seen[j].gone = 0; nSeen++; }
        }
        /* new ones are ranked after every one seen before, nearest first; stage = rank / M-COMs per stage */
        for (int j = first; j < nSeen; j++) { int r = first; for (int q = first; q < nSeen; q++) if (q != j && (seen[q].d < seen[j].d || (seen[q].d == seen[j].d && q < j))) r++; seen[j].rank = r; }
        int perStage = g_world.mode == MODE_SQRUSH ? 1 : 2, pick[32], np = 0, cur = 1 << 20;
        for (int j = 0; j < nSeen; j++) if (!seen[j].gone && seen[j].rank / perStage < cur) cur = seen[j].rank / perStage;
        for (int j = 0; j < nSeen; j++) if (!seen[j].gone && seen[j].rank / perStage == cur) pick[np++] = j;
        if (kept > np && np) {
            int w2 = 0;
            for (int k = 0; k < kept; k++) { int on = 0; for (int q = 0; q < np; q++) if (dist2d_w(cps[k].pos, seen[pick[q]].pos) < 4) on = 1; if (on) cps[w2++] = cps[k]; }
            rt_log("objectives: %d M-COM(s) are on; stage %d is %d of them (nearest team %d's spawns) - %d later-stage one(s) left for later", kept, cur + 1, w2, att, kept - w2);
            kept = w2;
        }
    }
    for (int k = 0; k < kept; k++) {
        cps[k].owner = 3 - att; cps[k].attacker = 0; cps[k].onlyTeam = 0; cps[k].locked = 0;
        if (cps[k].holdTime < 0.5f) cps[k].holdTime = 4.0f;   /* (unknown: hold long enough) */
        if (cps[k].armHold < 0.5f) cps[k].armHold = cps[k].holdTime;
        if (cps[k].disarmHold < 0.5f) cps[k].disarmHold = cps[k].holdTime;
        snprintf(cps[k].name, sizeof cps[k].name, "M-COM %c", 'A' + k);
    }
    memcpy(g_world.cps, cps, sizeof(CapturePoint) * kept); g_world.cpCount = kept;
    rt_log("objectives: %d M-COM(s) on - team %d attacks, team %d defends", kept, att, 3 - att);
    for (int k = 0; k < kept; k++) indoor_request(cps[k].pos, "an M-COM");   /* 1.6.3: the way in, if it stands inside */
    for (int k = 0; k < kept; k++) rt_log("objectives:   %s at (%.1f, %.1f, %.1f), arm 0x%08lx, defuse 0x%08lx%s, hold %.1f s", cps[k].name, cps[k].pos[0], cps[k].pos[1], cps[k].pos[2],
                                          (unsigned long)cps[k].armEnt, (unsigned long)cps[k].disarmEnt, cps[k].armed ? " - ARMED" : "", cps[k].holdTime);
}
static int refresh_obj(CapturePoint *c)
{
    int alive = 0, armed = 0;
    for (int j = 0; j < c->objN; j++) { ObjRaw o; if (read_obj(c->objEnt[j], &o) && o.active) { alive = 1; if (c->objEnt[j] == c->disarmEnt) armed = 1; } }
    if (armed != c->armed) { rt_log("objectives: %s %s", c->name, armed ? "is ARMED - defenders go to defuse it" : "is no longer armed"); c->armed = armed; if (armed) c->armedAt = rt_now(); }
    return alive;
}

/* ---------------- vehicles ---------------- */
static int upper_has(const char *s, const char *needle)
{
    char u[64]; int i = 0; for (; s[i] && i < 63; i++) u[i] = (char)toupper((unsigned char)s[i]); u[i] = 0;
    return strstr(u, needle) != NULL;
}
static int vehicle_kind(const char *name, int seats)
{
    static const char *heli[] = {"UH60", "UH-60", "MI24", "MI-24", "MI28", "MI-28", "AH64", "AH-64", "AH60", "BLACK HAWK", "HELI", "HUEY", "UH1", "UH-1", "HIND", "APACHE", "HAVOC", "HAVOK", "BLACKHAWK", "CHOPPER", "LITTLEBIRD"};
    static const char *boat[] = {"BOAT", "PBL", "PBR", "RHIB", "RIB", "JETSKI", "WATER", "PWC"};   /* PWC: Isla Inocentes' jet skis (1.6) */
    static const char *fixed[] = {"UAV", "MORTAR", "ARTILLERY"};
    static const char *guns[] = {"KORNET", "TOW", "KORD", "X312", "X-312", "STATIONARY", "ZU23", "ZU-23", "HMG", "EMPLACE", "TURRET", "DSHK", "M2HB", "M60_ST", "STATIC"};
    static const char *bikes[] = {"QUAD", "ATV", "BIKE", "MOTO"};
    static const char *jets[] = {"UAV", "SU39", "SU-39", "SU25", "SU-25", "A10", "A-10", "F16", "F18", "F35", "MIG", "JET", "AIRSTRIKE", "PLANE"};
    /* the UAV is a small helicopter but a station; the Su-39s are scripted jets that never spawn - neither is for bots */
    for (unsigned i = 0; i < sizeof jets / sizeof *jets; i++) if (upper_has(name, jets[i])) return VK_FIXED;
    for (unsigned i = 0; i < sizeof heli / sizeof *heli; i++) if (upper_has(name, heli[i])) return VK_HELI;
    for (unsigned i = 0; i < sizeof boat / sizeof *boat; i++) if (upper_has(name, boat[i])) return VK_BOAT;
    for (unsigned i = 0; i < sizeof fixed / sizeof *fixed; i++) if (upper_has(name, fixed[i])) return VK_FIXED;
    for (unsigned i = 0; i < sizeof bikes / sizeof *bikes; i++) if (upper_has(name, bikes[i])) return VK_GROUND;
    for (unsigned i = 0; i < sizeof guns / sizeof *guns; i++) if (upper_has(name, guns[i])) return VK_EMPLACE;
    if (seats <= 1) return VK_EMPLACE;   /* one seat: a stationary gun (1.5: bots use them; quad bikes are named) */
    return name[0] ? VK_GROUND : VK_UNKNOWN;
}
static void vehicle_name(uint32_t data, char *out, unsigned size)
{
    uint32_t p = 0; out[0] = 0;
    if (eng_u32(data + VEHD_NAMESID, &p) && eng_ptr(p)) {   /* char* */
        char t[48] = {0};
        if (eng_read(p, t, sizeof t - 1)) { int ok = 1, n = 0; for (; n < 47 && t[n]; n++) if (t[n] < 32 || t[n] > 126) ok = 0;
            if (ok && n >= 2) { snprintf(out, size, "%s", t); return; } }
    }
    eng_string(data + VEHD_NAMESID, out, size);
}
int world_vehicle_read(uint32_t e, VehicleInfo *v)
{
    uint32_t vt = 0, data = 0; uint8_t flags = 0;
    if (!eng_ptr(e) || !eng_u32(e, &vt) || vt != RB(VA_VEHICLE_VT) || !eng_u32(e + VEH_DATA, &data) || !eng_ptr(data)) return 0;
    if (!eng_read(e + 0x26, &flags, 1) || !(flags & 0x08)) return 0;
    int seats = eng_vehicle_seats(e); if (seats < 1 || seats > 16) return 0;
    float m[16]; if (!eng_entity_matrix(e, m)) return 0;
    v->entity = e; v->data = data; v->seats = seats;
    v->pos[0] = m[12]; v->pos[1] = m[13]; v->pos[2] = m[14];
    v->heading = atan2f(m[8], m[10]);
    memcpy(v->right, m, 12); memcpy(v->up, m + 4, 12); memcpy(v->fwd, m + 8, 12);
    v->health = 0; v->healthOk = eng_read(e + 0x34, &v->health, 4) && isfinite(v->health) && v->health > 0 && v->health < 100000;
    return 1;
}
typedef struct VehIdent { uint32_t entity, data; char name[48]; int kind, team; } VehIdent;
static VehIdent g_ident[256]; static int g_identN; static unsigned g_identLevel;
static VehIdent *ident_find(uint32_t e, uint32_t data)
{
    if (g_identLevel != g_world.levelSerial) { g_identLevel = g_world.levelSerial; g_identN = 0; }
    for (int i = 0; i < g_identN; i++) if (g_ident[i].entity == e && g_ident[i].data == data) return &g_ident[i];
    /* a new vehicle of a known type (respawned, or spawned when a flag fell): the type's name and kind, team unknown yet */
    for (int i = 0; i < g_identN; i++) if (g_ident[i].data == data && g_identN < 256) {
        VehIdent *n = &g_ident[g_identN++]; *n = g_ident[i]; n->entity = e; n->team = 0; return n; }
    return NULL;
}
static VehIdent *ident_store(uint32_t e, uint32_t data, const char *name, int kind, int team)
{
    VehIdent *id = NULL;
    for (int i = 0; i < g_identN; i++) if (g_ident[i].entity == e && g_ident[i].data == data) id = &g_ident[i];
    if (!id) { if (g_identN >= 256) return NULL; id = &g_ident[g_identN++]; }
    id->entity = e; id->data = data; snprintf(id->name, sizeof id->name, "%s", name); id->kind = kind; id->team = team;
    return id;
}
/* which spawner made this vehicle: its spawned-vehicles list (+0x370/+0x374), else the nearest spawner within 12 m */
static const VehicleSpawner *spawner_of(uint32_t e, const float *pos)
{
    const VehicleSpawner *closest = NULL; float bd = 12.0f;
    for (int i = 0; i < g_world.vspawnCount; i++) {
        const VehicleSpawner *sp = &g_world.vspawn[i];
        uint32_t vb = 0, ve = 0, list[16];
        if (eng_u32(sp->entity + 0x370, &vb) && eng_u32(sp->entity + 0x374, &ve) && ve > vb && ve - vb <= sizeof list && eng_read(vb, list, ve - vb))
            for (unsigned k = 0; k < (ve - vb) / 4; k++) if (list[k] == e) return sp;
        float dx = sp->pos[0] - pos[0], dy = sp->pos[1] - pos[1], dz = sp->pos[2] - pos[2], d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d < bd) { bd = d; closest = sp; }
    }
    return closest;
}
static void vehicles_collect(const uint32_t *found, int n)
{
    VehicleInfo keep[MAX_VEH]; int kept = 0;
    for (int i = 0; i < n && kept < MAX_VEH; i++) {
        VehicleInfo v; memset(&v, 0, sizeof v);
        if (!world_vehicle_read(found[i], &v)) continue;
        int dup = 0; for (int k = 0; k < kept; k++) if (keep[k].entity == v.entity) dup = 1;
        if (dup) continue;
        vehicle_name(v.data, v.name, sizeof v.name);
        v.kind = vehicle_kind(v.name, v.seats);
        /* the spawner that made it names it (most BC2 vehicles have no readable name of their own: live 1.0 showed
           the RU T-90s and Mi-28 as "(no name)", so bots treated the Mi-28 as a car) and says whose it is */
        /* identity is remembered per vehicle (and per vehicle type): 1.1 only looked for a spawner within 12 m, so a
           vehicle that had driven off lost its name and team - the AH-64 became "unknown" and its gunner bailed out
           in mid-air "close to the flag" */
        VehIdent *id = ident_find(v.entity, v.data);
        if (!id || !id->team) {
            const VehicleSpawner *sp = spawner_of(v.entity, v.pos);
            if (sp) {
                const char *sn = strrchr(sp->name, '/'); sn = sn ? sn + 1 : sp->name;
                char nm[48]; snprintf(nm, sizeof nm, "%s", v.name[0] && v.kind != VK_UNKNOWN ? v.name : sn);
                id = ident_store(v.entity, v.data, nm[0] ? nm : v.name, vehicle_kind(nm[0] ? nm : v.name, v.seats), sp->team);
            }
        }
        if (id) {
            if (id->name[0]) snprintf(v.name, sizeof v.name, "%s", id->name);
            v.kind = id->kind; v.team = id->team;
        }
        keep[kept++] = v;
    }
    /* log new vehicles (first time each name/kind is seen this level) */
    static unsigned loggedLevel; static uint32_t loggedData[64]; static int loggedN;
    if (loggedLevel != g_world.levelSerial) { loggedLevel = g_world.levelSerial; loggedN = 0; }
    static const char *kn[] = {"ground", "helicopter", "boat", "not for bots", "unknown", "stationary weapon"};
    for (int k = 0; k < kept; k++) {
        int seen = 0; for (int j = 0; j < loggedN; j++) if (loggedData[j] == keep[k].data) seen = 1;
        if (seen || loggedN >= 64) continue;
        loggedData[loggedN++] = keep[k].data;
        int usable = keep[k].kind == VK_GROUND || keep[k].kind == VK_UNKNOWN || (keep[k].kind == VK_HELI && g_cfg.helicopters) ||
                     (keep[k].kind == VK_BOAT && g_cfg.boats) || (keep[k].kind == VK_EMPLACE && g_cfg.emplacements);
        rt_log("vehicles: '%s' %d seat(s), %s, team %d, at (%.0f, %.0f, %.0f), health %s%.0f%s", keep[k].name[0] ? keep[k].name : "(no name)", keep[k].seats, keep[k].kind >= 0 && keep[k].kind < (int)(sizeof kn / sizeof *kn) ? kn[keep[k].kind] : "?", keep[k].team,
               keep[k].pos[0], keep[k].pos[1], keep[k].pos[2], keep[k].healthOk ? "" : "unreadable ", keep[k].health, usable ? " - bots may use it" : "");
    }
    memcpy(g_world.veh, keep, sizeof(VehicleInfo) * kept); g_world.vehCount = kept;
    static int scanLogs; static unsigned scanLevel;
    if (scanLevel != g_world.levelSerial) { scanLevel = g_world.levelSerial; scanLogs = 0; }
    if (scanLogs < 4 || (n >= VEH_SCAN_MAX && scanLogs < 8)) { scanLogs++; rt_log("vehicles: scan found %d candidate(s)%s, %d live vehicle(s)", n, n >= VEH_SCAN_MAX ? " (limit reached)" : "", kept); }
}

/* vehicle spawners: what each team can get on this map, where, and whether it is up */
static void spawners_collect(const uint32_t *found, int n)
{
    VehicleSpawner keep[MAX_VSPAWN]; int kept = 0;
    for (int i = 0; i < n && kept < MAX_VSPAWN; i++) {
        uint32_t e = found[i], vt = 0, data = 0, bp = 0; uint8_t flags = 0;
        if (!eng_u32(e, &vt) || vt != RB(VA_VEHICLE_SPAWN_VT) || !eng_u32(e + 0xc, &data) || !eng_ptr(data)) continue;
        int dup = 0; for (int k = 0; k < kept; k++) if (keep[k].entity == e) dup = 1; if (dup) continue;
        VehicleSpawner *v = &keep[kept]; memset(v, 0, sizeof *v);
        v->entity = e; v->data = data;
        eng_read(e + 0x26, &flags, 1); v->enabled = (flags >> 3) & 1;
        if (!eng_entity_position(e, v->pos)) continue;
        int32_t team = 0; eng_read(data + 0x88, &team, 4); v->team = team;
        uint8_t b1 = 0, b2 = 0; eng_read(data + 0x105, &b1, 1); eng_read(data + 0x108, &b2, 1); v->autoSpawn = b1; v->fromStart = b2;
        eng_read(data + 0x80, &v->delay, 4); eng_read(data + 0x84, &v->initDelay, 4);
        if (eng_u32(data + 0xd8, &bp) && eng_ptr(bp)) { v->blueprint = bp; eng_string(bp + ASSET_NAME, v->name, sizeof v->name); }
        uint32_t vb = 0, ve = 0; if (eng_u32(e + 0x370, &vb) && eng_u32(e + 0x374, &ve) && ve >= vb && ve - vb < 256) v->alive = (int)((ve - vb) / 4);
        const char *s = strrchr(v->name, '/'); s = s ? s + 1 : v->name;
        v->kind = vehicle_kind(s, 4);
        kept++;
    }
    static unsigned logged; static int loggedN;
    if (logged != g_world.levelSerial || kept != loggedN) {
        logged = g_world.levelSerial; loggedN = kept;
        /* 1.6.6: six labels for six kinds - 1.6.5 had five ("emplacement" for VK_FIXED, nothing for VK_EMPLACE): the first
           stationary-gun spawner (Atacama's) read past the table and the server crashed on the map change */
        static const char *kn[] = {"ground", "helicopter", "boat", "aircraft/fixed", "unknown", "stationary gun"};
        rt_log("vehicles: %d vehicle spawner(s) on this map", kept);
        for (int i = 0; i < kept; i++) { VehicleSpawner *v = &keep[i]; const char *s = strrchr(v->name, '/'); s = s ? s + 1 : v->name;
            rt_log("vehicles:   %-20s %-11s team %d at (%.0f, %.0f, %.0f) %s, %s%s, respawn %.0f s, %d out", s[0] ? s : "?", v->kind >= 0 && v->kind < (int)(sizeof kn / sizeof *kn) ? kn[v->kind] : "?", v->team,
                   v->pos[0], v->pos[1], v->pos[2], v->enabled ? "enabled" : "DISABLED", v->autoSpawn ? "auto" : "manual", v->fromStart ? " from start" : "", v->delay, v->alive); }
    }
    memcpy(g_world.vspawn, keep, sizeof(VehicleSpawner) * kept); g_world.vspawnCount = kept;
}

static void scan_collect(void)
{
    if (g_scanState != 2) return;
    InterlockedExchange(&g_scanState, 0);
    if (g_scanForLevel != g_world.levelSerial) return;
    if (g_scanSpValue) spawners_collect(g_scanSp, (int)g_scanSpCount);   /* first: vehicles take name and team from them */
    if (g_scanVehValue) vehicles_collect(g_scanVeh, (int)g_scanVehCount);
    if (g_scanRoadValue[0]) roads_collect(g_scanRoad[0], (int)g_scanRoadCount[0], g_scanRoad[1], (int)g_scanRoadCount[1], g_scanRoad[2], (int)g_scanRoadCount[2]);
    if (g_world.cpCount) return;   /* this scan was for vehicles; the flags are known */
    int n = (int)g_scanCount, kept = 0, logged = 0;

    CapturePoint cps[MAX_CPS];
    for (int i = 0; i < n && kept < MAX_CPS; i++) {
        CapturePoint cp;
        if (!read_cp(g_scanFound[i], &cp)) {
            if (logged++ < 6) {
                uint32_t e = g_scanFound[i], data = 0, owner = 0; uint8_t flags = 0; float radius = 0, pos[3] = {0, 0, 0};
                eng_u32(e + CP_DATA, &data); eng_read(e + 0x26, &flags, 1); eng_u32(e + CP_OWNER, &owner);
                if (eng_ptr(data)) eng_read(data + CPD_RADIUS, &radius, 4);
                int pv = eng_entity_position(e, pos);
                rt_log("flags:   candidate 0x%08lx skipped: data 0x%08lx flags 0x%02x radius %.1f owner %lu position %s (%.0f, %.0f, %.0f)",
                       (unsigned long)e, (unsigned long)data, flags, radius, (unsigned long)owner, pv ? "ok" : "unreadable", pos[0], pos[1], pos[2]);
            }
            continue;
        }
        int dup = 0; for (int k = 0; k < kept; k++) if (cps[k].entity == cp.entity) dup = 1;
        if (!dup) cps[kept++] = cp;
    }
    /* stable order: the level's own flag index, then name */
    for (int i = 0; i < kept; i++) for (int j = i + 1; j < kept; j++)
        if (cps[j].index < cps[i].index || (cps[j].index == cps[i].index && strcmp(cps[j].name, cps[i].name) < 0)) { CapturePoint t = cps[i]; cps[i] = cps[j]; cps[j] = t; }
    /* Rush / Squad Rush (1.5.1): no live flag - the M-COMs. (1.5.0 only looked for them when no flag object at all was
       in memory; Cold War Rush has inactive ones, so the M-COMs were never looked at) */
    /* Rush / Squad Rush: the M-COMs are the objectives. (Rush has live flag objects too - the two sides' spawn bases, which
       move up with each stage; 1.5.1 took those for objectives and sent Valparaiso's defenders to "defend" the Fishing Port
       base marker over the harbour - into the water) */
    if (g_scanObjValue && (g_world.mode == MODE_RUSH || g_world.mode == MODE_SQRUSH)) {
        static unsigned lv; if (lv != g_world.levelSerial) { lv = g_world.levelSerial; rt_log("flags: Rush - %d spawn base flag(s) ignored, looking for M-COMs (%ld interaction entities)", kept, (long)g_scanObjCount); }
        objectives_collect(g_scanObj, (int)g_scanObjCount);
        return;   /* none on yet (between stages): bots hunt until the next search finds them */
    }
    memcpy(g_world.cps, cps, sizeof(CapturePoint) * kept); g_world.cpCount = kept;
    rt_log("flags: memory scan %lu ms, %d candidate(s), %d active capture point(s)", (unsigned long)g_scanMs, n, kept);
    for (int i = 0; i < kept; i++)
        rt_log("flags:   %c '%s' at (%.1f, %.1f, %.1f) radius %.1f owner %d%s%s", 'A' + i, cps[i].name, cps[i].pos[0], cps[i].pos[1],
               cps[i].pos[2], cps[i].radius, cps[i].owner, cps[i].locked ? " locked" : "", cps[i].onlyTeam ? " team-only" : "");
}

/* ---------------- spawns ---------------- */
static void refresh_spawns(void)
{
    uint32_t m = eng_spawn_manager(), b = 0, e = 0;
    g_world.spawnMgr = m; g_world.spawnCount = 0;
    if (!m || !eng_u32(m + 8, &b) || !eng_u32(m + 0xc, &e) || e < b || (e - b) / 4 > 512 || (b && !eng_ptr(b))) return;
    for (uint32_t p = b; p < e && g_world.spawnCount < MAX_SPAWNS; p += 4) {
        uint32_t s = 0; uint8_t flags = 0; int32_t team = 0;
        if (!eng_u32(p, &s) || !eng_ptr(s) || !eng_read(s + 0x26, &flags, 1) || !eng_read(s + 0x2c, &team, 4)) continue;
        SpawnPoint *sp = &g_world.spawns[g_world.spawnCount];
        memset(sp, 0, sizeof *sp);
        sp->entity = s; sp->team = team; sp->enabled = (flags >> 3) & 1;
        if (team >= 3 && team <= 4 && g_world.teamCount < 4) { g_world.teamCount = 4; rt_log("spawns: spawn points for team %d - four squads (Squad Deathmatch)", team); }
        sp->posValid = eng_entity_position(s, sp->pos);
        uint32_t data = 0; if (!eng_u32(s + 0xc, &data) || !eng_ptr(data)) continue;
        sp->id = eng_spawn_entity_id(s);
        g_world.spawnCount++;
    }
}

/* ---------------- kits ---------------- */
static int class_of(const char *name, int index)
{
    char low[64]; size_t n = strlen(name); if (n > 63) n = 63;
    for (size_t i = 0; i < n; i++) low[i] = (char)tolower((unsigned char)name[i]); low[n] = 0;
    if (strstr(low, "assault")) return CLS_ASSAULT;
    if (strstr(low, "engineer") || strstr(low, "engi") || strstr(low, "demo")) return CLS_ENGINEER;
    if (strstr(low, "medic") || strstr(low, "support")) return CLS_MEDIC;
    if (strstr(low, "recon") || strstr(low, "sniper") || strstr(low, "spec")) return CLS_RECON;
    return index % CLS_COUNT;
}
static void refresh_kits(void)
{
    uint32_t ti = eng_team_info();
    g_world.teamInfo = ti;
    for (int team = 1; team <= 4; team++) {
        g_world.kitCount[team] = 0;
        uint32_t ted = 0, td = 0, count = 0, elems = 0;
        if (!ti || !eng_u32(ti + team * 4, &ted) || !eng_ptr(ted) || !eng_u32(ted + TEAMENT_TEAMDATA, &td) || !eng_ptr(td)) continue;
        if (!eng_u32(td + TEAMDATA_KITS + 0xc, &count) || !eng_u32(td + TEAMDATA_KITS + 0x10, &elems) || count > 32 || !eng_ptr(elems)) continue;
        for (uint32_t k = 0; k < count && g_world.kitCount[team] < MAX_KITS; k++) {
            uint32_t asset = 0, data = 0, vt = 0;
            if (!eng_u32(elems + k * 4, &asset) || !eng_ptr(asset) || !eng_u32(asset + SOLDIERASSET_DATA, &data) || !eng_ptr(data) ||
                !eng_u32(data, &vt) || vt != RB(VA_SOLDIER_DATA_VT)) continue;
            KitInfo *kit = &g_world.kits[team][g_world.kitCount[team]];
            memset(kit, 0, sizeof *kit);
            kit->asset = asset; kit->data = data;
            eng_string(asset + ASSET_NAME, kit->name, sizeof kit->name);
            kit->resourceId = eng_resource_to_id(data);
            if (!kit->resourceId || eng_resource_by_id(kit->resourceId) != data) continue;
            kit->cls = class_of(kit->name, (int)k);
            /* struct array (0x10 bytes): begin at +8, end at +0xc, 12-byte SoldierWeaponUnlockData elements */
            uint32_t ub = 0, uend = 0, uc = 0, ue = 0;
            if (eng_u32(data + SED_WEAPON_UNLOCKS + 0x8, &ub) && eng_u32(data + SED_WEAPON_UNLOCKS + 0xc, &uend) && uend >= ub &&
                (uend - ub) % 12 == 0) { uc = (uend - ub) / 12; ue = ub; }
            int notSel = 0, noRes = 0;
            uint32_t pend[MAX_UNLOCK_CHOICES], pendSlot[MAX_UNLOCK_CHOICES]; int npend = 0;
            char all[900]; int allUsed = 0; all[0] = 0;
            if (uc && uc <= 160 && eng_ptr(ue)) {
                for (uint32_t u = 0; u < uc; u++) {
                    uint32_t slot = 0, weapon = 0; uint8_t bundled = 0, selectable = 0;
                    if (!eng_u32(ue + u * 12, &slot) || !eng_u32(ue + u * 12 + 4, &weapon) || !eng_read(ue + u * 12 + 8, &bundled, 1) ||
                        !eng_read(ue + u * 12 + 9, &selectable, 1) || !eng_ptr(weapon) || slot > 16) continue;
                    if (team == 1 && k < 4 && slot >= 2 && allUsed < 860) {   /* gadget slots - which key holds what */
                        char nm[128] = ""; eng_string(weapon + ASSET_NAME, nm, sizeof nm);
                        const char *s = strrchr(nm, '/'); s = s ? s + 1 : nm;
                        char item[80]; snprintf(item, sizeof item, " %u%s:%s", slot, selectable ? "*" : bundled ? "+" : "", s);
                        if (!strstr(all, item)) allUsed += snprintf(all + allUsed, sizeof all - allUsed, "%s", item);
                    }
                    if (!selectable) { notSel++; continue; }
                    if (kit->choiceCount + npend >= MAX_UNLOCK_CHOICES) continue;
                    /* given directly (its data object is appended to the player's selected list) */
                    uint32_t id = 0;
                    if (!id) { pend[npend] = weapon; pendSlot[npend] = slot; npend++; continue; }
                    UnlockChoice *c = &kit->choices[kit->choiceCount++];
                    c->slot = slot; c->resourceId = id; c->weapon = weapon;
                    eng_string(weapon + ASSET_NAME, c->name, sizeof c->name);
                }
            }
            if (npend) {   /* not in the reverse table: look them up in the id table */
                uint32_t ids[MAX_UNLOCK_CHOICES] = {0};
                for (int p = 0; p < npend; p++) {
                    /* no resource id: kept anyway - Phoenix appends the asset itself to the player's selected list */
                    if (!ids[p] || eng_resource_by_id(ids[p]) != pend[p]) { noRes++; ids[p] = 0; }
                    UnlockChoice *c = &kit->choices[kit->choiceCount++];
                    c->slot = pendSlot[p]; c->resourceId = ids[p]; c->weapon = pend[p];
                    eng_string(pend[p] + ASSET_NAME, c->name, sizeof c->name);   /* (64 chars: the short name is after the last '/') */
                }
            }
            if (all[0]) rt_log("kits: '%s' gadget slots (slot, *selectable, +bundled):%s", kit->name, all);
            if (noRes) rt_log("kits: '%s' weapon unlock list: %u entries, %d not selectable, %d selectable without resource id (given directly)",
                              kit->name, uc, notSel, noRes);
            g_world.kitCount[team]++;
        }
    }
    static const char *cls[] = {"assault", "engineer", "medic", "recon"};
    for (int team = 1; team <= 4; team++)
        for (int k = 0; k < g_world.kitCount[team]; k++) {
            KitInfo *kit = &g_world.kits[team][k];
            char list[600]; int used = 0; list[0] = 0;
            for (int c = 0; c < kit->choiceCount && used < 560; c++)
                { const char *nm = strrchr(kit->choices[c].name, '/'); nm = nm ? nm + 1 : kit->choices[c].name;
                  used += snprintf(list + used, sizeof list - used, "%s%u:%s", c ? " " : "", kit->choices[c].slot, nm); }
            rt_log("kits: team %d kit %d '%s' -> %s, %d selectable unlock(s): %s", team, k, kit->name, cls[kit->cls], kit->choiceCount, list);
        }
}

const KitInfo *world_kit_for(int team, int cls)
{
    if (team >= 3 && team <= 4 && !g_world.kitCount[team]) team = team - 2;   /* Squad Deathmatch squads 3/4: the kits of 1/2 */
    if (team < 1 || team > 4 || !g_world.kitCount[team]) return NULL;
    for (int k = 0; k < g_world.kitCount[team]; k++) if (g_world.kits[team][k].cls == cls) return &g_world.kits[team][k];
    return &g_world.kits[team][rt_rand() % (unsigned)g_world.kitCount[team]];
}

int world_find_player(uint32_t pointer, uint32_t id)
{
    if (!g_world.obsReady) return -1;
    for (int i = 0; i < g_world.obs.count; i++)
        if (g_world.obs.players[i].pointer == pointer && g_world.obs.players[i].id == id) return i;
    return -1;
}

/* ---------------- ray self-test ---------------- */
static int g_rayTested;
int world_ray_selftest(void)
{
    if (g_rayTested) return 1;
    for (int i = 0; i < g_world.obs.count; i++) {
        BC2ObservedPlayer *p = &g_world.obs.players[i];
        if (!p->soldierValid || !p->transformValid || p->entry) continue;
        uint32_t masks[2] = {0x7e, 0x38}; float under[2] = {NAN, NAN}, over[2] = {NAN, NAN};
        for (int m = 0; m < 2; m++) {
            float a[3] = {p->position[0], p->position[1] + 30, p->position[2]}, b[3] = {p->position[0], p->position[1] - 30, p->position[2]}, h[3], n[3];
            if (eng_ray_masked(masks[m], a, b, h, n)) over[m] = h[1] - p->position[1];
            float c[3] = {p->position[0] + 0.7f, p->position[1] + 1.0f, p->position[2] + 0.7f}, d[3] = {c[0], c[1] - 20, c[2]};
            if (eng_ray_masked(masks[m], c, d, h, n)) under[m] = h[1] - p->position[1];
        }
        rt_log("rays: self-test at (%.1f, %.1f, %.1f): filter 0x7e from above %+.2f m, beside %+.2f m | filter 0x38 above %+.2f m, beside %+.2f m",
               p->position[0], p->position[1], p->position[2], over[0], under[0], over[1], under[1]);
        uint32_t both[2] = {0x38, 0x7e};
        eng_ray_set_masks(both, 2);
        rt_log("rays: using filters 0x38 + 0x7e (nearest hit wins)");
        g_rayTested = 1; return 1;
    }
    return 0;
}

/* ---------------- tick ---------------- */
static double g_nextSlow, g_nextCp;

/* the level's path ("Levels/MP_006CQ") - found by looking for it among the level object's string pointers */
static const struct { const char *code, *name; } kMaps[] = {
    {"MP_001", "Panama Canal"}, {"MP_002", "Valparaiso"}, {"MP_003", "Laguna Alta"}, {"MP_004", "Isla Inocentes"},
    {"MP_005", "Atacama Desert"}, {"MP_006", "Arica Harbor"}, {"MP_007", "White Pass"}, {"MP_008", "Nelson Bay"},
    {"MP_009", "Laguna Presa"}, {"MP_012", "Port Valdez"}, {"BC1_OASIS", "Oasis"}, {"BC1_HARVEST", "Harvest Day"},
    {"MP_SP_002", "Cold War"}, {"MP_SP_005", "Heavy Metal"}, {"NAM_MP_002", "Vantage Point"}, {"NAM_MP_003", "Hill 137"},
    {"NAM_MP_005", "Cao Son Temple"}, {"NAM_MP_006", "Phu Bai Valley"}, {"NAM_MP_007", "Operation Hastings"}};
static void find_level_name(uint32_t level)
{
    g_world.levelName[0] = g_world.mapName[0] = 0;
    uint32_t objs[3] = {level, 0, 0};
    eng_u32(level + 0x78, &objs[1]);
    if (eng_context()) eng_u32(eng_context() + 0x34, &objs[2]);
    for (int o = 0; o < 3 && !g_world.levelName[0]; o++) {
        if (!eng_ptr(objs[o])) continue;
        for (uint32_t off = 0; off < 0x600 && !g_world.levelName[0]; off += 4) {
            uint32_t p = 0; char t[64];
            if (!eng_u32(objs[o] + off, &p) || !eng_ptr(p & ~3u) || !eng_read(p, t, 8)) continue;
            if (_strnicmp(t, "levels/", 7)) continue;
            memset(t, 0, sizeof t); eng_read(p, t, 63);
            int ok = 1; for (int i = 0; t[i]; i++) if (t[i] < 32 || t[i] > 126) { ok = 0; break; }
            if (ok) snprintf(g_world.levelName, sizeof g_world.levelName, "%s", t);
        }
    }
    g_world.mode = MODE_CONQUEST;
    if (!g_world.levelName[0]) return;
    char up[64]; int i = 0; for (; g_world.levelName[i] && i < 63; i++) up[i] = (char)toupper((unsigned char)g_world.levelName[i]); up[i] = 0;
    {   /* the mode is in the level name: ..CQ Conquest, ..GR Rush, ..SR Squad Rush, ..SDM Squad Deathmatch; the plain MP_00x
           names are Conquest when odd and Rush when even (MP_002 Valparaiso, MP_004 Isla Inocentes, MP_006, MP_008) */
        size_t n = strlen(up);
        if (n >= 3 && !strcmp(up + n - 3, "SDM")) g_world.mode = MODE_SQDM;
        else if (n >= 2 && !strcmp(up + n - 2, "SR")) g_world.mode = MODE_SQRUSH;
        else if (n >= 2 && !strcmp(up + n - 2, "GR")) g_world.mode = MODE_RUSH;
        else if (n >= 2 && !strcmp(up + n - 2, "CQ")) g_world.mode = MODE_CONQUEST;
        else if (n >= 1 && up[n - 1] >= '0' && up[n - 1] <= '9') g_world.mode = ((up[n - 1] - '0') & 1) ? MODE_CONQUEST : MODE_RUSH;
        static const char *mn[] = {"Conquest", "Rush", "Squad Rush", "Squad Deathmatch"};
        rt_log("level: game mode %s", mn[g_world.mode]);
    }
    int best = -1; size_t bestLen = 0;
    for (unsigned k = 0; k < sizeof kMaps / sizeof kMaps[0]; k++) if (strstr(up, kMaps[k].code) && strlen(kMaps[k].code) > bestLen) { best = (int)k; bestLen = strlen(kMaps[k].code); }
    if (best >= 0) snprintf(g_world.mapName, sizeof g_world.mapName, "%s", kMaps[best].name);
}

void world_tick(double now)
{
    uint32_t manager = eng_player_manager(), level = eng_level_object();
    uint32_t ti = level ? eng_team_info() : 0, sm = level ? eng_spawn_manager() : 0;
    static uint32_t lastTi, lastSm;
    int subChanged = (lastTi && ti != lastTi) || (lastSm && sm != lastSm);
    lastTi = ti; lastSm = sm;
    if (level != g_world.level || manager != g_world.manager || subChanged) {
        g_world.level = level; g_world.manager = manager; g_world.levelSerial++; g_world.levelSince = now;
        g_world.cpCount = 0; g_world.vehCount = 0; g_world.vspawnCount = 0; g_world.spawnCount = 0; memset(g_world.kitCount, 0, sizeof g_world.kitCount); g_world.boundsValid = 0; g_world.teamCount = 2;
        rt_log("level: level object 0x%08lx, player manager 0x%08lx (change #%u)", (unsigned long)level, (unsigned long)manager, g_world.levelSerial);
        g_nextSlow = now + 2.0; g_nextCp = now + 4.0;
        navg_reset();
        if (level) { find_level_name(level); rt_log("level: %s%s%s%s", g_world.levelName[0] ? g_world.levelName : "(level name not found)",
                                                     g_world.mapName[0] ? " - " : "", g_world.mapName, ""); }
    }
    g_world.obsReady = bc2_observe(obs_read, NULL, g_engine.base, &g_world.obs) == BC2_OBSERVE_READY;
    if (g_world.obsReady) {
        memset(g_world.humans, 0, sizeof g_world.humans); memset(g_world.bots, 0, sizeof g_world.bots);
        for (int i = 0; i < g_world.obs.count; i++) {
            BC2ObservedPlayer *p = &g_world.obs.players[i];
            if (p->team < 1 || p->team > 4) continue;
            if (p->isAI) g_world.bots[p->team]++; else g_world.humans[p->team]++;
        }
    }
    if (!level || !manager) return;
    scan_collect();
    if (now >= g_nextSlow) {
        g_nextSlow = now + 1.0;
        refresh_spawns();
        if (!g_world.kitCount[1] || !g_world.kitCount[2]) {
            static double lastKitTry;
            if (now - lastKitTry > 5) { lastKitTry = now; refresh_kits(); }
        }
        /* capture points: re-read state, rescan when lost */
        int lost = 0;
        for (int i = 0; i < g_world.cpCount; i++) {
            CapturePoint cp;
            if (g_world.cps[i].isObj) { if (!refresh_obj(&g_world.cps[i])) { lost++; rt_log("objectives: %s is gone (destroyed, or the next stage)", g_world.cps[i].name); } }
            else if (read_cp(g_world.cps[i].entity, &cp)) { char keep[32]; memcpy(keep, g_world.cps[i].name, 32); g_world.cps[i] = cp; memcpy(g_world.cps[i].name, keep, 32); }
            else lost++;
        }
        if (lost) { rt_log("flags: %d capture point(s) no longer valid - rescanning", lost); g_world.cpCount = 0; if (g_world.mode == MODE_RUSH || g_world.mode == MODE_SQRUSH) g_nextCp = now + 3; }
        static int emptyScans; static unsigned emptyLevel;
        if (emptyLevel != g_world.levelSerial) { emptyLevel = g_world.levelSerial; emptyScans = 0; }
        if (!g_world.cpCount && now >= g_nextCp && g_scanState == 0 && now - g_scanLastStart > ((g_world.mode == MODE_RUSH || g_world.mode == MODE_SQRUSH) ? 6 : emptyScans < 3 ? 15 : 90)) {
            if (g_scanLastStart > g_world.levelSince) emptyScans++;
            world_cp_scan_request();
        }
        /* vehicles: refresh known ones, rescan every 20 s (they are destroyed and respawn as new objects) */
        if (g_cfg.vehicles) {
            int w = 0;
            for (int i = 0; i < g_world.vehCount; i++) { VehicleInfo v = g_world.veh[i];
                if (world_vehicle_read(v.entity, &v) && v.data == g_world.veh[i].data) g_world.veh[w++] = v; }
            g_world.vehCount = w;
            if (g_world.cpCount && g_scanState == 0 && now - g_scanLastStart > 10 && now - g_world.levelSince > 8) world_cp_scan_request();   /* new vehicles: respawns, flag-unlocked spawners */
        }
        /* bounds */
        float mn[3] = {1e9f, 1e9f, 1e9f}, mx[3] = {-1e9f, -1e9f, -1e9f}; int any = 0;
        for (int i = 0; i < g_world.cpCount; i++) for (int k = 0; k < 3; k++) { mn[k] = fminf(mn[k], g_world.cps[i].pos[k]); mx[k] = fmaxf(mx[k], g_world.cps[i].pos[k]); any = 1; }
        for (int i = 0; i < g_world.spawnCount; i++) if (g_world.spawns[i].posValid)
            for (int k = 0; k < 3; k++) { mn[k] = fminf(mn[k], g_world.spawns[i].pos[k]); mx[k] = fmaxf(mx[k], g_world.spawns[i].pos[k]); any = 1; }
        if (any) { memcpy(g_world.boundsMin, mn, 12); memcpy(g_world.boundsMax, mx, 12); g_world.boundsValid = 1; }
        static unsigned spawnLogged;
        if (spawnLogged != g_world.levelSerial && g_world.spawnCount) {
            spawnLogged = g_world.levelSerial;
            for (int i = 0; i < g_world.spawnCount; i++) {
                SpawnPoint *s = &g_world.spawns[i];
                rt_log("spawns: #%d id %lu team %d %s at (%.1f, %.1f, %.1f)", i, (unsigned long)s->id, s->team, s->enabled ? "enabled" : "disabled",
                       s->pos[0], s->pos[1], s->pos[2]);
            }
        }
    }
    if (g_world.obsReady) world_ray_selftest();
}
