/* Offline harness: a fake BC2 server laid out in memory exactly as the runtime reads it
   (context, player manager roster, players, soldiers, transforms, team info / kits / unlocks,
   spawn manager, capture points) plus a tiny game simulation (movement from EntryInput levels and
   setAiming, hitscan firing with line of sight, deaths that DELETE AI players like BC2 does,
   Conquest capture logic) and an analytic physics world for rays (hills + buildings + walls). */
#include "harness_win.h"
#include "rt.h"
#include "bc2_input_actions.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

BC2Engine g_engine;
#define FA0 0x10000000u
#define ARENA (96u << 20)
static uint8_t *A;
static uint32_t heapTop;

static int inA(uint32_t a, unsigned n) { return a >= FA0 && (uint64_t)a + n <= (uint64_t)FA0 + ARENA; }
int eng_read(uint32_t a, void *out, unsigned n) { if (!inA(a, n)) return 0; memcpy(out, A + (a - FA0), n); return 1; }
int eng_u32(uint32_t a, uint32_t *o) { return eng_read(a, o, 4); }
int eng_write32(uint32_t a, uint32_t v) { if (!inA(a, 4)) return 0; memcpy(A + (a - FA0), &v, 4); return 1; }
uint32_t g_mockTick;
uint32_t eng_game_tick(void) { return g_mockTick; }
static double g_mockTime;
int sim_tool_shots;
typedef struct MPlayer MPlayer_; static int *mock_wpn_of_soldier(uint32_t soldier);
int eng_soldier_weapon(uint32_t soldier) { int *w = mock_wpn_of_soldier(soldier); return w ? *w : -1; }
int eng_soldier_set_weapon(uint32_t soldier, int idx) { int *w = mock_wpn_of_soldier(soldier); if (!w) return 0; *w = idx; return 1; }
static int g_keepAI; int sim_team_switch; static uint32_t lastTeamOfId[64];
int eng_keep_ai_players(int on) { g_keepAI = on; return 1; }
int sim_armed, sim_disarmed, sim_mcom_destroyed;
static int mock_interact(uint32_t e, uint32_t soldier);
int eng_interact(uint32_t e, uint32_t soldier) { return mock_interact(e, soldier); }
int eng_interact_why(void) { return 0; }


int eng_ray_masked(uint32_t mask, const float *a, const float *b, float *h, float *n);
void eng_ray_set_masks(const uint32_t *m, int c) { (void)m; (void)c; }
int eng_ptr(uint32_t p) { return p >= 0x10000u && p <= 0xfffe0000u && !(p & 3); }
static void W(uint32_t a, uint32_t v) { memcpy(A + (a - FA0), &v, 4); }
static void WF(uint32_t a, float v) { memcpy(A + (a - FA0), &v, 4); }
static void WB(uint32_t a, uint8_t v) { A[a - FA0] = v; }
static uint32_t R(uint32_t a) { uint32_t v; memcpy(&v, A + (a - FA0), 4); return v; }
static float RF(uint32_t a) { float v; memcpy(&v, A + (a - FA0), 4); return v; }
static uint32_t alloc(uint32_t n) { uint32_t p = heapTop; heapTop += (n + 15) & ~15u; memset(A + (p - FA0), 0, n); return p; }
static uint32_t mkstr(uint32_t field, const char *s)
{ uint32_t n = (uint32_t)strlen(s), p = alloc(n + 1); memcpy(A + (p - FA0), s, n + 1); W(field, p); W(field + 4, p + n); W(field + 8, p + n + 1); return p; }

int eng_verify(char *why, unsigned n) { (void)why; (void)n; g_engine.ready = 1; return 1; }
int eng_string(uint32_t field, char *out, unsigned size)
{
    uint32_t se[2]; out[0] = 0;
    if (!eng_read(field, se, 8) || se[1] < se[0] || se[1] - se[0] > 255) return 0;
    unsigned n = se[1] - se[0]; if (n >= size) n = size - 1;
    if (n && !eng_read(se[0], out, n)) return 0; out[n] = 0; return 1;
}
uint32_t eng_context(void) { uint32_t c = RB(VA_SERVER_CONTEXT), vt; return eng_u32(c, &vt) && vt == RB(VA_SERVER_CONTEXT_VT) ? c : 0; }
uint32_t eng_player_manager(void) { uint32_t c = eng_context(); return c ? R(c + 0x30) : 0; }
uint32_t eng_level_object(void) { uint32_t c = eng_context(); return c ? R(c + 0x34) : 0; }
uint32_t eng_team_info(void) { uint32_t l = eng_level_object(); return l ? R(R(l + 0x78) + 4) : 0; }
uint32_t eng_spawn_manager(void) { uint32_t l = eng_level_object(); return l ? R(R(l + 0x78) + 8) : 0; }
uint32_t eng_resources(void) { return R(RB(VA_RESOURCES_PTR)); }

/* resources */
static uint32_t resAsset[4096]; static int resCount = 1;
static uint32_t res_add(uint32_t asset) { resAsset[resCount] = asset; return (uint32_t)resCount++; }
uint32_t eng_resource_to_id(uint32_t asset) { for (int i = 1; i < resCount; i++) if (resAsset[i] == asset) return (uint32_t)i; return 0; }
uint32_t eng_resource_by_id(uint32_t id) { return id && (int)id < resCount ? resAsset[id] : 0; }

/* ---------------- world geometry ---------------- */
typedef struct Box { float mn[3], mx[3]; } Box;
static Box boxes[256]; static int boxCount; static int g_doorBox = -1; static float g_houseY; int sim_doors;
static float terrain(float x, float z) { return 2.0f * sinf(x * 0.013f) + 1.5f * cosf(z * 0.017f) + 0.004f * x; }
static void add_box(float x0, float y0, float z0, float x1, float y1, float z1) { boxes[boxCount++] = (Box){{x0, y0, z0}, {x1, y1, z1}}; }
static unsigned rays;
unsigned eng_ray_count(void) { return rays; }
static uint32_t rayMask = 0x7e;
void eng_ray_set_mask(uint32_t m) { rayMask = m; }
uint32_t eng_ray_mask(void) { return rayMask; }
uint32_t eng_kind_mask(int k) { return k == 2 ? 0x38 : 0x7e; }

int eng_ray_masked(uint32_t mask, const float *a, const float *b, float *h, float *n) { (void)mask; return eng_ray(a, b, h, n); }
int eng_ray(const float *a, const float *b, float *hit, float *normal)
{
    rays++;
    float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, best = 2; int bestBox = -1, axis = 0; float sign = 0;
    for (int i = 0; i < boxCount; i++) {
        float t0 = 0, t1 = 1; int ax = -1; float sg = 0, ok = 1;
        for (int k = 0; k < 3 && ok; k++) {
            if (fabsf(d[k]) < 1e-9f) { if (a[k] < boxes[i].mn[k] || a[k] > boxes[i].mx[k]) ok = 0; continue; }
            float u0 = (boxes[i].mn[k] - a[k]) / d[k], u1 = (boxes[i].mx[k] - a[k]) / d[k]; float s = -1;
            if (u0 > u1) { float t = u0; u0 = u1; u1 = t; s = 1; }
            if (u0 > t0) { t0 = u0; ax = k; sg = s; }
            if (u1 < t1) t1 = u1;
            if (t0 > t1) ok = 0;
        }
        if (ok && ax >= 0 && t0 < best) { best = t0; bestBox = i; axis = ax; sign = sg; }
    }
    /* terrain: march */
    float tt = 2; int steps = (int)(sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]) / 0.5f) + 2;
    float prev = a[1] - terrain(a[0], a[2]);
    if (prev >= 0) for (int s = 1; s <= steps; s++) {
        float t = (float)s / steps, x = a[0] + d[0]*t, y = a[1] + d[1]*t, z = a[2] + d[2]*t, h = y - terrain(x, z);
        if (h < 0) { float lo = (float)(s - 1) / steps, hi = t;
            for (int it = 0; it < 20; it++) { float m = (lo + hi) / 2; if (a[1] + d[1]*m - terrain(a[0]+d[0]*m, a[2]+d[2]*m) < 0) hi = m; else lo = m; }
            tt = hi; break; }
    }
    if (bestBox < 0 && tt > 1) return 0;
    float t = bestBox >= 0 && best < tt ? best : tt;
    float p[3] = {a[0] + d[0]*t, a[1] + d[1]*t, a[2] + d[2]*t};
    if (hit) memcpy(hit, p, 12);
    if (normal) {
        if (bestBox >= 0 && best < tt) { normal[0] = normal[1] = normal[2] = 0; normal[axis] = sign; }
        else { float e = 0.3f, hx = terrain(p[0]+e, p[2]) - terrain(p[0]-e, p[2]), hz = terrain(p[0], p[2]+e) - terrain(p[0], p[2]-e);
            float n[3] = {-hx, 2*e, -hz}, l = sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]); normal[0] = n[0]/l; normal[1] = n[1]/l; normal[2] = n[2]/l; }
    }
    return 1;
}
static float ground_at(float x, float y, float z)
{ float a[3] = {x, y + 1.0f, z}, b[3] = {x, y - 30, z}, h[3]; return eng_ray(a, b, h, NULL) ? h[1] : terrain(x, z); }

/* ---------------- entities ---------------- */
static uint32_t ctx, manager, levelObj, levelSub, teamInfo, spawnMgr, roster, idTable;
static uint32_t matrix_new(float x, float y, float z, float heading)
{
    uint32_t m = alloc(64); float s = sinf(heading), c = cosf(heading);
    float v[16] = {c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, x, y, z, 1};
    memcpy(A + (m - FA0), v, 64); return m;
}
static uint32_t entity_new(uint32_t size, uint32_t vt, float x, float y, float z)
{ uint32_t e = alloc(size); W(e, vt); WB(e + 0x26, 0x08); W(e + 0x28, matrix_new(x, y, z, 0)); return e; }

typedef struct MPlayer { uint32_t p; void *input; float in[BC2_EiaUndefined]; float yaw, pitch, vyaw, vpitch; int human, wpn, prevGl, prevSw; double launcherAt, gadget1At; float vy; int kills, deaths; uint32_t tick; uint32_t stepped; } MPlayer;
static MPlayer mp[64];
static int roster_count(void) { return (int)((R(manager + 0x5c) - R(manager + 0x58)) / 4); }
static void roster_add(uint32_t p) { uint32_t e = R(manager + 0x5c); W(e, p); W(manager + 0x5c, e + 4); }
static void roster_remove(uint32_t p)
{
    uint32_t b = R(manager + 0x58), e = R(manager + 0x5c);
    for (uint32_t q = b; q < e; q += 4) if (R(q) == p) { for (uint32_t r = q; r + 4 < e; r += 4) W(r, R(r + 4)); W(manager + 0x5c, e - 4); break; }
    for (int i = 0; i < 64; i++) if (mp[i].p == p) { mp[i].p = 0; W(idTable + i * 4, 0); }
}
static MPlayer *mp_of(uint32_t p) { for (int i = 0; i < 64; i++) if (mp[i].p == p) return &mp[i]; return NULL; }
static int *mock_wpn_of_soldier(uint32_t soldier) { if (!inA(soldier, 0x244)) return NULL; MPlayer *m = mp_of(R(soldier + 0x240)); return m ? &m->wpn : NULL; }
static uint32_t g_velTick[64];
int eng_character_drive_velocity(uint32_t s, float x, float z)
{ (void)x; (void)z; for (int i = 0; i < 64; i++) if (mp[i].p && R(mp[i].p + PL_SOLDIER) == s) g_velTick[i] = g_mockTick; return 1; }
int eng_soldier_move_step(uint32_t soldier, uint32_t player, void *input, float dt, int ps)
{ (void)soldier; (void)input; (void)dt; MPlayer *m = mp_of(player); if (!m) return 0; if (ps) m->stepped = g_mockTick; return ps ? 2 : 1; }
int eng_silence_spawn_assert(void) { return 1; }
int eng_character_player_mode(uint32_t s) { (void)s; return 2; }
int eng_character_ai_mode(uint32_t s) { (void)s; return 2; }

void eng_debug_character(uint32_t s, char *o, unsigned n) { (void)s; snprintf(o, n, "mock"); }

uint32_t eng_allocate_player(uint32_t m)
{
    (void)m; int id = -1; for (int i = 0; i < 64; i++) if (!mp[i].p) { id = i; break; } if (id < 0) return 0;
    uint32_t p = alloc(0xe00); W(p, RB(VA_PLAYER_VT)); W(p + PL_ID, (uint32_t)id);
    memset(&mp[id], 0, sizeof mp[id]); mp[id].p = p; W(idTable + id * 4, p);
    roster_add(p); return p;
}
void eng_register_player(uint32_t m, uint32_t p, const PhxName *n) { (void)m; (void)n; WB(p + PL_INGAME, 1);
    uint32_t id = R(p + PL_ID), t = R(p + PL_TEAM); if (id < 64) { if (lastTeamOfId[id] && lastTeamOfId[id] != t) sim_team_switch++; lastTeamOfId[id] = t; } }
void eng_set_team(uint32_t p, uint32_t t) { W(p + PL_TEAM, t); }
void eng_set_ai(uint32_t p, int ai) { WB(p + PL_AI, (uint8_t)ai); }
void eng_ready(uint32_t p) { WB(p + PL_READY, 1); }
void eng_select_unlocks(uint32_t p, PhxPairVector *v) { (void)p; (void)v; }
void eng_select_kit(uint32_t p, uint32_t id, PhxPairVector *v, uint32_t f)
{ (void)v; if (!A[p + PL_READY - FA0]) return; uint32_t d = eng_resource_by_id(id);
  if (!d || R(d) != RB(VA_SOLDIER_DATA_VT)) { fprintf(stderr, "MOCK FAIL: select_kit with a stale/unknown soldier id %u\n", id); abort(); }
  W(p + PL_FLAGS17C, f); W(p + PL_SOLDIER_DAT, d); }
static uint32_t spawnData[64]; static int spawnN;
uint32_t eng_spawn_entity_id(uint32_t e) { return eng_resource_to_id(R(e + 0xc)); }
static uint32_t soldier_new(uint32_t player, float x, float y, float z, float heading)
{
    uint32_t s = alloc(0x400); W(s, RB(VA_SOLDIER_VT)); W(s + 0x240, player); WF(s + 0x34, 100); W(s + 0x28, matrix_new(x, y, z, heading));
    W(player + PL_SOLDIER, s); return s;
}
int eng_selected_spawn(uint32_t p, uint32_t id)
{
    if (R(p + PL_SOLDIER) || !R(p + PL_SOLDIER_DAT)) return 0;
    uint32_t b = R(spawnMgr + 8), e = R(spawnMgr + 0xc), team = R(p + PL_TEAM), chosen = 0;
    for (uint32_t q = b; q < e; q += 4) { uint32_t s = R(q); if (R(s + 0x2c) != team || !(A[s + 0x26 - FA0] & 8)) continue; if (!id || eng_spawn_entity_id(s) == id) { chosen = s; break; } }
    if (!chosen) return 0;
    float pos[3]; eng_entity_position(chosen, pos);
    float x = pos[0] + (rand() % 100 - 50) * 0.1f, z = pos[2] + (rand() % 100 - 50) * 0.1f;
    soldier_new(p, x, ground_at(x, pos[1] + 5, z), z, (rand() % 628) / 100.0f);
    return 1;
}
void eng_input_ctor(void *in) { memset(in, 0, 0x1b0); }
void eng_input_attach(uint32_t p, void *in) { W(p + PL_INPUT, 0x7777); MPlayer *m = mp_of(p); if (m) m->input = in; }
void eng_state_set_level(void *s, int a, float v)
{
    if (a < 0 || a >= BC2_EiaUndefined) return;
    if (a < 11) memcpy((uint8_t *)s + 0x08 + a * 4, &v, 4);
    else { uint32_t *bits = (uint32_t *)((uint8_t *)s + 0x98); if (v > 0.5f) bits[a / 32] |= 1u << (a % 32); else bits[a / 32] &= ~(1u << (a % 32)); }
}
void eng_state_set_dt(void *s, float dt) { memcpy((uint8_t *)s + 0xa0, &dt, 4); }
void eng_input_push(void *in, const void *s)
{
    for (int i = 0; i < 64; i++) if (mp[i].p && mp[i].input == in) {
        memcpy(&mp[i].tick, (const uint8_t *)s + 0xb8, 4);
        memcpy(&mp[i].vyaw, (const uint8_t *)s + 0xa8, 4); memcpy(&mp[i].vpitch, (const uint8_t *)s + 0xac, 4);
        for (int a = 0; a < BC2_EiaUndefined; a++) {
            if (a < 11) memcpy(&mp[i].in[a], (const uint8_t *)s + 0x08 + a * 4, 4);
            else mp[i].in[a] = (((const uint32_t *)((const uint8_t *)s + 0x98))[a / 32] >> (a % 32)) & 1;
        }
        if (mp[i].in[BC2_EiaDynamicGadget1] > 0.5f) { mp[i].gadget1At = g_mockTime; mp[i].wpn = 5; }
        /* like BC2: the gadget keys select their slot; SwitchPrimaryWeapon steps to the NEXT weapon (0 1 2 5) */
        if (mp[i].in[BC2_EiaGrenadeLauncher] > 0.5f && !mp[i].prevGl) mp[i].wpn = 2;
        if (mp[i].in[BC2_EiaSwitchPrimaryWeapon] > 0.5f && !mp[i].prevSw) mp[i].wpn = mp[i].wpn == 0 ? 1 : mp[i].wpn == 1 ? 2 : mp[i].wpn == 2 ? 5 : 0;
        mp[i].prevGl = mp[i].in[BC2_EiaGrenadeLauncher] > 0.5f; mp[i].prevSw = mp[i].in[BC2_EiaSwitchPrimaryWeapon] > 0.5f;
    }
}
void eng_soldier_set_aiming(uint32_t soldier, float yaw, float pitch)
{
    if (yaw < 0 || yaw > 6.2831855f) { fprintf(stderr, "ASSERT yaw out of range %f\n", yaw); abort(); }
    uint32_t p = R(soldier + 0x240); MPlayer *m = mp_of(p); if (m) { m->yaw = yaw; m->pitch = pitch; }
}
void eng_leave_game(uint32_t p)
{   /* BC2: asserts in-game, clears it, then releaseLevelDependencies (detach soldier, clear kit and ready) */
    if (!A[p + PL_INGAME - FA0]) { fprintf(stderr, "MOCK FAIL: leaveGame on a player that is not in game\n"); abort(); }
    eng_exit_vehicle(p);
    WB(p + PL_INGAME, 0); W(p + PL_SOLDIER, 0); W(p + PL_SOLDIER_DAT, 0); WB(p + PL_READY, 0);
}
void eng_delete_player(uint32_t m, uint32_t p)
{ (void)m; if (R(p + PL_SOLDIER)) { fprintf(stderr, "MOCK FAIL: deletePlayer while it controls a soldier\n"); abort(); } roster_remove(p); }
int eng_entity_position(uint32_t e, float out[3])
{ uint32_t root = R(e + 0x28); if (!root) return 0; out[0] = RF(root + 48); out[1] = RF(root + 52); out[2] = RF(root + 56); return 1; }

int harness_scan(uint32_t value, uint32_t *out, int max)
{ int n = 0; for (uint32_t a = FA0 + 0x2000000; a + 4 <= heapTop && n < max; a += 4) if (R(a) == value) out[n++] = a; return n; }

/* ---------------- vehicles ---------------- */
typedef struct MVeh { uint32_t e, d; uint32_t occ[4]; float speed; int heli; float vel[3], pitch, roll, y;
    float tYaw, tPitch, gYaw, gPitch, rotor, wheel, hp, home[3]; uint32_t turObj, gunObj, rotObj; int team; } MVeh;
static MVeh mveh[8]; static int mvehN;
static MVeh *mveh_of(uint32_t e) { for (int i = 0; i < mvehN; i++) if (mveh[i].e == e) return &mveh[i]; return NULL; }
int eng_entity_matrix(uint32_t e, float m[16]) { uint32_t root = R(e + 0x28); if (!root) return 0; memcpy(m, A + (root - FA0), 64); return 1; }
int eng_vehicle_seats(uint32_t v) { return inA(v, 4) && R(v) == RB(VA_VEHICLE_VT) && mveh_of(v) ? 4 : 0; }
uint32_t eng_vehicle_occupant(uint32_t v, int seat) { MVeh *m = mveh_of(v); return m && seat >= 0 && seat < 4 ? m->occ[seat] : 0; }
int eng_vehicle_seat_allowed(uint32_t v, uint32_t p, int seat) { (void)p; MVeh *m = mveh_of(v); return m && seat >= 0 && seat < 4 && !m->occ[seat]; }
uint32_t eng_player_controlled(uint32_t p) { uint32_t c = R(p + PL_CONTROLLED); return c ? c : R(p + PL_SOLDIER); }
int sim_vehicle_enters, sim_vehicle_exits; float sim_vehicle_km, sim_heli_max_agl;
float sim_ground_km, sim_road_km;
/* 1.6.9: distance to the ROADS scenario's road lines (the road object along z=-4 from x=320 to 0, the decal from (-330,10) to (-50,80)) */
static float seg_dist(float px, float pz, float ax, float az, float bx, float bz) { float vx = bx - ax, vz = bz - az, L2 = vx * vx + vz * vz, t = L2 > 0 ? ((px - ax) * vx + (pz - az) * vz) / L2 : 0; t = t < 0 ? 0 : t > 1 ? 1 : t; return hypotf(px - ax - vx * t, pz - az - vz * t); }
static float mock_road_dist(float x, float z) { if (!getenv("ROADS")) return 1e9f; float d = seg_dist(x, z, 320, -4, 0, -4); for (int i = 0; i < 7; i++) d = fminf(d, seg_dist(x, z, -330 + i * 40.0f, 10 + i * 10.0f, -330 + (i + 1) * 40.0f, 10 + (i + 1) * 10.0f)); return d; }
int sim_rocket_soldier, sim_vf[3], sim_vh[3], sim_veh_fire, sim_veh_hits, sim_veh_kills, sim_rockets, sim_rocket_hits;
int eng_enter_vehicle(uint32_t p, uint32_t v, int seat)
{
    MVeh *m = mveh_of(v); if (!m || seat < 0 || seat >= 4 || m->occ[seat] || !R(p + PL_SOLDIER)) return 0;
    for (int i = 0; i < mvehN; i++) for (int k = 0; k < 4; k++) if (mveh[i].occ[k] == p) { fprintf(stderr, "MOCK FAIL: enter while already in a vehicle\n"); abort(); }
    m->occ[seat] = p; W(p + PL_CONTROLLED, v); W(p + PL_ENTRY, v + 0x200 + (uint32_t)seat * 0x40); sim_vehicle_enters++;   /* like BC2: a pointer to the entry component */ return 1;
}
int eng_exit_vehicle(uint32_t p)
{
    for (int i = 0; i < mvehN; i++) for (int k = 0; k < 4; k++) if (mveh[i].occ[k] == p) {
        mveh[i].occ[k] = 0; W(p + PL_CONTROLLED, 0); W(p + PL_ENTRY, 0); sim_vehicle_exits++;
        uint32_t s = R(p + PL_SOLDIER), vr = R(mveh[i].e + 0x28), sr = s ? R(s + 0x28) : 0;
        if (sr) { float x = RF(vr + 48) + 3.0f, z = RF(vr + 56); WF(sr + 48, x); WF(sr + 56, z); WF(sr + 52, ground_at(x, RF(vr + 52) + 3, z)); }
        return 1;
    }
    return 0;
}
static void build_vehicles(void)
{
    mvehN = 0;
    float at[4][3] = {{-335, 0, 15}, {335, 0, -15}, {-320, 0, -20}, {320, 0, 20}};
    /* VODNIK and MI24 have no readable name (like most live BC2 vehicles): the spawner names them */
    const char *nm[4] = {"ID_P_VNAME_HMMWV", "", "ID_P_VNAME_UH60", ""};
    const char *bp[4] = {"Vehicles/US/HMMWV/HMMWV", "Vehicles/RU/Vodnik/Vodnik", "Vehicles/US/UH60/UH60", "Vehicles/RU/Mi24/Mi24"};
    int tm[4] = {1, 2, 1, 2};
    for (int i = 0; i < 4; i++) {
        uint32_t e = entity_new(0x200, RB(VA_VEHICLE_VT), at[i][0], terrain(at[i][0], at[i][2]), at[i][2]), d = alloc(0x260);
        W(e + VEH_DATA, d); uint32_t str = alloc(32); memcpy(A + (str - FA0), nm[i], strlen(nm[i]) + 1); W(d + VEHD_NAMESID, str);
        MVeh *v = &mveh[mvehN]; memset(v, 0, sizeof *v); v->e = e; v->d = d; v->heli = i >= 2; v->hp = 100; v->team = tm[i];
        v->home[0] = at[i][0]; v->home[1] = terrain(at[i][0], at[i][2]); v->home[2] = at[i][2];
        if (i == 0) v->tPitch = 0.4f;   /* the HMMWV's gun rests at its highest elevation: +Pitch does not move it */
        /* parts: driver turret (local matrix, two pointers deep), gunner turret (world matrix), rotor / wheel (spinning) */
        uint32_t o1 = alloc(0x100), o2 = alloc(0x100); W(e + 0x180, o1); W(o1 + 0x24, o2); v->turObj = o2 + 0x40;
        uint32_t og = alloc(0x80); W(e + 0x184, og); v->gunObj = og + 0x20;
        uint32_t orr = alloc(0x80); W(e + 0x188, orr); v->rotObj = orr + 0x10;
        mvehN++;
        /* its spawner */
        uint32_t se = entity_new(0x380, RB(VA_VEHICLE_SPAWN_VT), at[i][0] + 1, terrain(at[i][0], at[i][2]), at[i][2]), sd = alloc(0x120), ba = alloc(0x40);
        W(se + 0xc, sd); W(sd + 0x88, (uint32_t)tm[i]); W(sd + 0xd8, ba); mkstr(ba + ASSET_NAME, bp[i]);
        uint32_t vec = alloc(16); W(vec, e); W(se + 0x370, vec); W(se + 0x374, vec + 4);
    }
}
static float wrapf(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }
static void put_matrix(uint32_t at, float hd, float pt, const float *t)
{
    float f[3] = {sinf(hd) * cosf(pt), sinf(pt), cosf(hd) * cosf(pt)}, r[3] = {cosf(hd), 0, -sinf(hd)};
    float u[3] = {f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0]};
    float m[16] = {r[0], r[1], r[2], 0, u[0], u[1], u[2], 0, f[0], f[1], f[2], 0, t[0], t[1], t[2], 1};
    memcpy(A + (at - FA0), m, 64);
}
/* a part turned (hd, pt) relative to the hull matrix hm, stored in world space */
static void put_matrix_on(uint32_t at, const float *hm, float hd, float pt, const float *t)
{
    float lf[3] = {sinf(hd) * cosf(pt), sinf(pt), cosf(hd) * cosf(pt)}, lr[3] = {cosf(hd), 0, -sinf(hd)};
    float lu[3] = {lf[1] * lr[2] - lf[2] * lr[1], lf[2] * lr[0] - lf[0] * lr[2], lf[0] * lr[1] - lf[1] * lr[0]};
    float m[16]; const float *L[3] = {lr, lu, lf};
    for (int r = 0; r < 3; r++) for (int k = 0; k < 3; k++) m[r * 4 + k] = L[r][0] * hm[k] + L[r][1] * hm[4 + k] + L[r][2] * hm[8 + k];
    m[3] = m[7] = m[11] = 0; m[12] = t[0]; m[13] = t[1]; m[14] = t[2]; m[15] = 1;
    memcpy(A + (at - FA0), m, 64);
}
static void kill_soldier(uint32_t p, uint32_t killer);
static int sim_suicides;
static void vehicle_destroyed(MVeh *v, uint32_t killer);
static int los(const float *a, const float *b);
static float in_of(MVeh *v, int seat, int a) { MPlayer *d = v->occ[seat] ? mp_of(v->occ[seat]) : NULL; return d && d->tick + 2 >= g_mockTick ? d->in[a] : 0; }
/* a gun on a vehicle: hits enemy soldiers within 1 m of the line, damages enemy-crewed vehicles within 3 m */
static void vehicle_gun(MVeh *v, int seat, const float *o, const float *dir, float dt)
{
    uint32_t shooter = v->occ[seat]; if (!shooter) return;
    sim_veh_fire++; int cat = seat == 0 ? (v->heli ? 1 : 0) : 2; sim_vf[cat]++;
    uint32_t team = R(shooter + PL_TEAM);
    for (int j = 0; j < 64; j++) {
        if (!mp[j].p || R(mp[j].p + PL_TEAM) == team || R(mp[j].p + PL_CONTROLLED)) continue;
        uint32_t ts = R(mp[j].p + PL_SOLDIER); if (!ts) continue;
        uint32_t tr = R(ts + 0x28); float t[3] = {RF(tr + 48), RF(tr + 52) + 1.2f, RF(tr + 56)};
        float w[3] = {t[0]-o[0], t[1]-o[1], t[2]-o[2]}, along = w[0]*dir[0] + w[1]*dir[1] + w[2]*dir[2];
        if (along <= 0 || along > 300) continue;
        {   /* soldiers walk ~3.5 m/s along their facing while the round flies */
            MPlayer *tm2 = &mp[j]; float spd = tm2->in[BC2_EiaThrottle] * 3.5f, hh2 = -tm2->yaw, tf = along / (cat == 2 ? 600.0f : cat == 1 ? 150.0f : 300.0f);
            t[0] += sinf(hh2) * spd * tf; t[2] += cosf(hh2) * spd * tf;
            w[0] = t[0]-o[0]; w[1] = t[1]-o[1]; w[2] = t[2]-o[2]; along = w[0]*dir[0] + w[1]*dir[1] + w[2]*dir[2];
            w[1] += 0.5f * 9.81f * tf * tf;   /* the round falls on the way */
        }
        float off[3] = {w[0]-dir[0]*along, w[1]-dir[1]*along, w[2]-dir[2]*along};
        if (sqrtf(off[0]*off[0] + off[1]*off[1] + off[2]*off[2]) < (cat == 2 ? 1.0f : 3.0f) && los(o, t)) {   /* shells / rockets splash */
            sim_veh_hits++; sim_vh[cat]++; float hp = RF(ts + 0x34) - 60.0f * dt; WF(ts + 0x34, hp);
            if (hp <= 0) kill_soldier(mp[j].p, shooter);
            return;
        }
    }
    for (int k = 0; k < mvehN; k++) {   /* enemy-crewed vehicles */
        MVeh *t = &mveh[k]; if (t == v) continue;
        int crewed = 0; for (int q = 0; q < 4; q++) if (t->occ[q] && R(t->occ[q] + PL_TEAM) != team) crewed = 1;
        if (!crewed) continue;
        uint32_t tr = R(t->e + 0x28); float c[3] = {RF(tr + 48), RF(tr + 52) + 1.5f, RF(tr + 56)};
        float w[3] = {c[0]-o[0], c[1]-o[1], c[2]-o[2]}, along = w[0]*dir[0] + w[1]*dir[1] + w[2]*dir[2];
        if (along <= 0 || along > 500) continue;
        {   /* the target vehicle moves while the round flies */
            float tf = along / (cat == 2 ? 600.0f : cat == 1 ? 150.0f : 300.0f), hv = atan2f(RF(tr + 32), RF(tr + 40));
            float tvx = t->heli ? t->vel[0] : sinf(hv) * t->speed, tvy = t->heli ? t->vel[1] : 0, tvz = t->heli ? t->vel[2] : cosf(hv) * t->speed;
            c[0] += tvx * tf; c[1] += tvy * tf; c[2] += tvz * tf;
            w[0] = c[0]-o[0]; w[1] = c[1]-o[1]; w[2] = c[2]-o[2]; along = w[0]*dir[0] + w[1]*dir[1] + w[2]*dir[2];
            w[1] += 0.5f * 9.81f * tf * tf;
        }
        float off[3] = {w[0]-dir[0]*along, w[1]-dir[1]*along, w[2]-dir[2]*along};
        if (sqrtf(off[0]*off[0] + off[1]*off[1] + off[2]*off[2]) < 3.5f) {
            sim_veh_hits++; sim_vh[cat]++; t->hp -= (cat == 2 ? 8.0f : 25.0f) * dt;
            if (t->hp <= 0) vehicle_destroyed(t, shooter);
            return;
        }
    }
}
static void vehicle_destroyed(MVeh *v, uint32_t killer)
{
    sim_veh_kills++;
    for (int k = 0; k < 4; k++) if (v->occ[k]) { uint32_t p = v->occ[k]; kill_soldier(p, killer); }
    uint32_t root = R(v->e + 0x28); float m[16]; memcpy(m, A + (root - FA0), 64);
    m[12] = v->home[0]; m[13] = v->home[1]; m[14] = v->home[2]; m[0] = 1; m[1] = 0; m[2] = 0; m[4] = 0; m[5] = 1; m[6] = 0; m[8] = 0; m[9] = 0; m[10] = 1;
    memcpy(A + (root - FA0), m, 64);
    v->hp = 100; v->speed = 0; memset(v->vel, 0, 12); v->pitch = v->roll = 0; v->tYaw = v->tPitch = v->gYaw = v->gPitch = 0;
}
int sim_repairs;
static void vehicles_step(float dt)
{
    for (int i = 0; i < mvehN; i++) {
        MVeh *v = &mveh[i]; uint32_t root = R(v->e + 0x28); float m[16]; memcpy(m, A + (root - FA0), 64);
        /* repair tool: an engineer next to it with the tool out (DynamicGadget1 pressed in the last 6 s) and firing */
        for (int k = 0; k < 64 && v->hp < 100; k++) if (mp[k].p && mp[k].in[BC2_EiaFire] > 0.5f && g_mockTime - mp[k].gadget1At < 6) {
            uint32_t sol = R(mp[k].p + PL_SOLDIER); if (!sol) continue; uint32_t sr = R(sol + 0x28);
            if (hypotf(RF(sr + 48) - m[12], RF(sr + 56) - m[14]) < 5.5f) { v->hp = fminf(100, v->hp + 12 * dt); if (v->hp >= 99.9f) sim_repairs++; }
        }
        WF(v->e + 0x34, v->hp);
        float h = atan2f(m[8], m[10]), x = m[12], z = m[14];
        MPlayer *d = v->occ[0] ? mp_of(v->occ[0]) : NULL;
        {   /* turrets (rate inputs): driver's on CameraYaw / CameraPitch (not on helicopters), gunner's on Yaw / Pitch */
            if (!v->heli && i == 1) {   /* the Vodnik's turret follows the driver's aim (world yaw / pitch of the input), 1.0 rad/s */
                MPlayer *dv = v->occ[0] ? mp_of(v->occ[0]) : NULL;
                if (dv) { float want = wrapf(-dv->vyaw - atan2f(m[8], m[10]) - v->tYaw), wp = fmaxf(-0.3f, fminf(0.4f, dv->vpitch * (float)g_cfg.pitchSign)) - v->tPitch;
                    v->tYaw += fmaxf(-1.0f * dt, fminf(1.0f * dt, want)); v->tPitch += fmaxf(-0.6f * dt, fminf(0.6f * dt, wp)); }
            } else if (!v->heli) { v->tYaw += in_of(v, 0, BC2_EiaCameraYaw) * 1.2f * dt; v->tPitch = fmaxf(-0.3f, fminf(0.4f, v->tPitch + in_of(v, 0, BC2_EiaCameraPitch) * 0.8f * dt)); }
            v->gYaw += in_of(v, 1, BC2_EiaYaw) * 1.6f * dt; v->gPitch = fmaxf(-0.6f, fminf(0.4f, v->gPitch + in_of(v, 1, BC2_EiaPitch) * 1.0f * dt));
            while (v->tYaw > 3.14159265f) v->tYaw -= 6.2831853f; while (v->tYaw < -3.14159265f) v->tYaw += 6.2831853f;
            while (v->gYaw > 3.14159265f) v->gYaw -= 6.2831853f; while (v->gYaw < -3.14159265f) v->gYaw += 6.2831853f;
            float lt[3] = {0, 2.0f, 0.5f}; put_matrix(v->turObj, v->tYaw, v->tPitch, lt);
            float hh = atan2f(m[8], m[10]);
            float gt[3] = {m[12], m[13] + 2.5f, m[14]}; put_matrix_on(v->gunObj, m, v->gYaw, v->gPitch, gt);
            if (v->heli) v->rotor += 20.0f * dt; else v->wheel += v->speed * dt / 0.5f;
            float rt[3] = {0, 3, 0}; if (v->heli) put_matrix(v->rotObj, v->rotor, 0, rt); else put_matrix(v->rotObj, 0, v->wheel, rt);
            float o[3] = {m[12], m[13] + 2.4f, m[14]};
            if (!v->heli && in_of(v, 0, BC2_EiaFire) > 0.5f) { float hw = hh + v->tYaw, dir[3] = {sinf(hw) * cosf(v->tPitch), sinf(v->tPitch), cosf(hw) * cosf(v->tPitch)}; vehicle_gun(v, 0, o, dir, dt); }
            if (v->heli && in_of(v, 0, BC2_EiaFire) > 0.5f) { float dir[3] = {m[8], m[9], m[10]}; vehicle_gun(v, 0, o, dir, dt); }
            if (in_of(v, 1, BC2_EiaFire) > 0.5f) { float gm[16]; memcpy(gm, A + (v->gunObj - FA0), 64); float dir[3] = {gm[8], gm[9], gm[10]}; vehicle_gun(v, 1, o, dir, dt); }
        }
        float thr = d ? d->in[BC2_EiaThrottle] : 0, yaw = d ? d->in[BC2_EiaYaw] : 0, brake = d ? d->in[BC2_EiaBrake] : 0;
        if (d && d->tick + 2 < g_mockTick) thr = yaw = 0;
        if (v->heli) {
            /* mock helicopter: Throttle = collective (0 hovers), +Pitch noses DOWN, +Roll lifts the LEFT side,
               +Yaw turns towards -heading (signs the bot has to find out); thrust along the rotor axis */
            float pin = d && d->tick + 2 >= g_mockTick ? d->in[BC2_EiaPitch] : 0, rin = d && d->tick + 2 >= g_mockTick ? d->in[BC2_EiaRoll] : 0;
            float y = m[13];
            float gy0 = ground_at(x, y + 2, z);
            int onGround = y <= gy0 + 0.05f;
            float coll = d ? thr : -1;
            float rsign = i == 2 ? 1.0f : -1.0f;   /* the UH60 rolls like the real BC2 helicopters (+Roll lifts the right side); the MI24 the other way */
            v->pitch += (-pin * 1.2f - v->pitch * 0.15f) * dt; v->roll += (rsign * rin * 1.2f - v->roll * 0.15f) * dt;
            if (onGround && coll < 0.2f) { v->pitch *= 0.8f; v->roll *= 0.8f; }
            if (i == mvehN - 1) yaw = d && d->tick + 2 >= g_mockTick ? d->in[BC2_EiaStrafe] : 0;   /* last helicopter: rudder on A-D */
            h -= yaw * 0.9f * dt;
            float cp = cosf(v->pitch), sp = sinf(v->pitch), cr = cosf(v->roll), sr = sinf(v->roll);
            float f[3] = {sinf(h) * cp, sp, cosf(h) * cp}, r[3] = {cosf(h) * cr, sr, -sinf(h) * cr};
            float u[3] = {f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0]};
            float ul = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]); for (int k = 0; k < 3; k++) u[k] /= ul;
            float T = 9.81f * (1.0f + fmaxf(-1, fminf(1, coll)) * 0.8f);
            for (int k = 0; k < 3; k++) v->vel[k] += (u[k] * T - v->vel[k] * 0.25f) * dt;
            v->vel[1] -= 9.81f * dt;
            float nx = x + v->vel[0] * dt, ny = y + v->vel[1] * dt, nz = z + v->vel[2] * dt;
            float gy = ground_at(nx, ny + 2, nz);
            if (ny <= gy) { if (v->vel[1] < -12) fprintf(stderr, "mock: helicopter hit the ground at %.1f m/s\n", -v->vel[1]);
                            ny = gy; if (v->vel[1] < 0) v->vel[1] = 0; v->vel[0] *= 0.5f; v->vel[2] *= 0.5f; }
            sim_vehicle_km += hypotf(nx - x, nz - z) / 1000.0f;
            sim_heli_max_agl = fmaxf(sim_heli_max_agl, ny - gy);
            m[0] = r[0]; m[1] = r[1]; m[2] = r[2]; m[4] = u[0]; m[5] = u[1]; m[6] = u[2]; m[8] = f[0]; m[9] = f[1]; m[10] = f[2];
            m[12] = nx; m[13] = ny; m[14] = nz;
            memcpy(A + (root - FA0), m, 64);
            for (int k = 0; k < 4; k++) if (v->occ[k]) {
                uint32_t s = R(v->occ[k] + PL_SOLDIER); if (!s) { v->occ[k] = 0; continue; }
                uint32_t sr2 = R(s + 0x28); WF(sr2 + 48, nx); WF(sr2 + 52, ny); WF(sr2 + 56, nz);
            }
            continue;
        }
        /* like BC2: Brake slows a moving vehicle and reverses a slow one */
        if (brake > 0.5f) { if (v->speed > 1.0f) v->speed *= 0.8f; else thr = -0.5f; }
        v->speed += (thr * 12.0f - v->speed) * fminf(1, dt * 1.5f);
        h -= yaw * 0.7f * dt * (v->speed >= 0 ? 1 : -1) * fminf(1, fabsf(v->speed) / 3);   /* mock: +Yaw turns towards -heading */
        float nx = x + sinf(h) * v->speed * dt, nz = z + cosf(h) * v->speed * dt;
        float a[3] = {x, m[13] + 1.0f, z}, b[3] = {nx + sinf(h) * 2.5f * (v->speed > 0 ? 1 : -1), m[13] + 1.0f, nz + cosf(h) * 2.5f * (v->speed > 0 ? 1 : -1)}, hh[3], nn[3];
        if (fabsf(v->speed) > 0.05f && eng_ray(a, b, hh, nn) && nn[1] < 0.7f) { v->speed = 0; nx = x; nz = z; }
        if (getenv("LOWBLOCK") && fabsf(v->speed) > 0.05f) {   /* 1.6.6: like BC2 - wheels stop at a barrel, a divider, a stair */
            float a2[3] = {x, m[13] + 0.45f, z}, b2[3] = {nx + sinf(h) * 2.0f * (v->speed > 0 ? 1 : -1), m[13] + 0.45f, nz + cosf(h) * 2.0f * (v->speed > 0 ? 1 : -1)};
            if (eng_ray(a2, b2, hh, nn) && nn[1] < 0.7f) { v->speed = 0; nx = x; nz = z; }
            else if (fabsf(ground_at(nx, m[13] + 2, nz) - m[13]) > 0.45f) { v->speed = 0; nx = x; nz = z; }
        }
        sim_vehicle_km += hypotf(nx - x, nz - z) / 1000.0f;
        { float dk = hypotf(nx - x, nz - z) / 1000.0f; sim_ground_km += dk; if (mock_road_dist(nx, nz) < 6.0f) sim_road_km += dk; }   /* 1.6.9: how much of the driving is on the level's roads */
        float gy = ground_at(nx, m[13] + 2, nz);
        m[0] = cosf(h); m[2] = -sinf(h); m[8] = sinf(h); m[10] = cosf(h); m[12] = nx; m[13] = gy; m[14] = nz;
        memcpy(A + (root - FA0), m, 64);
        for (int k = 0; k < 4; k++) if (v->occ[k]) {   /* occupants ride along */
            uint32_t s = R(v->occ[k] + PL_SOLDIER); if (!s) { v->occ[k] = 0; continue; }
            uint32_t sr = R(s + 0x28); WF(sr + 48, nx); WF(sr + 52, gy); WF(sr + 56, nz);
        }
    }
}

/* ---------------- level ---------------- */
typedef struct MCP { uint32_t e, d; } MCP;
typedef struct MMcom { uint32_t e, e2; double armedAt; } MMcom;
static MMcom mcom[4]; static int mcomN;
static int mock_interact(uint32_t e, uint32_t soldier)
{   /* like ServerInteractionEntity::interact: active, the soldier next to it (the game finds it by range) */
    if (R(e) != RB(VA_INTERACTION_VT) || !(A[e + 0x26 - FA0] & 8)) return 0;
    float p[3]; eng_entity_position(e, p); uint32_t rt = R(soldier + 0x28);
    if (hypotf(RF(rt + 48) - p[0], RF(rt + 56) - p[2]) > 2.5f || fabsf(RF(rt + 52) - p[1]) > 2.0f) { fprintf(stderr, "MOCK: interact from too far\n"); return 0; }
    for (int c = 0; c < mcomN; c++) {
        if (mcom[c].e == e) { mcom[c].armedAt = g_mockTime; WB(e + 0x26, 0); WB(mcom[c].e2 + 0x26, 8); sim_armed++; fprintf(stderr, "mock: M-COM %d armed\n", c); return 1; }
        if (mcom[c].e2 == e) { mcom[c].armedAt = -1; WB(e + 0x26, 0); WB(mcom[c].e + 0x26, 8); sim_disarmed++; fprintf(stderr, "mock: M-COM %d defused\n", c); return 1; }
    }
    return 0;
}
static MCP cps[8]; static int cpN;
static uint32_t human;
static const char *weaponNames[4][6] = {{"aek", "xm8", "f2000", "an94", "m416", "m16"}, {"pp2000", "ump", "uzi", "xm8c", "aks74u", "scar"},
                                        {"m249", "pkm", "m60", "mg36", "xm8lmg", "type88"}, {"m24", "sv98", "vss", "m95", "gol", "qbu88"}};
static const char *kitName[4] = {"Assault", "Engineer", "Medic", "Recon"};

static void build_level(void);
static uint32_t levelGen;
static void build_level(void)
{
    levelGen++;
    levelObj = alloc(0x100); W(ctx + 0x34, levelObj); levelSub = alloc(0x40); W(levelObj + 0x78, levelSub);
    teamInfo = alloc(0x48); W(levelSub + 4, teamInfo); spawnMgr = alloc(0x40); W(levelSub + 8, spawnMgr);
    resCount = 1; cpN = 0; boxCount = 0; spawnN = 0;
    /* kits */
    for (int team = 1; team <= 2; team++) {
        uint32_t ted = alloc(0xb0), td = alloc(0x38); W(teamInfo + team * 4, ted); W(ted + TEAMENT_TEAMDATA, td);
        uint32_t elems = alloc(8 * 4); W(td + TEAMDATA_KITS + 0xc, 4); W(td + TEAMDATA_KITS + 0x10, elems);
        for (int k = 0; k < 4; k++) {
            uint32_t asset = alloc(0x54), data = alloc(0x230); W(elems + k * 4, asset); W(asset + SOLDIERASSET_DATA, data); W(data, RB(VA_SOLDIER_DATA_VT));
            char nm[64]; snprintf(nm, sizeof nm, "Characters/Soldiers/%s/%s_%s", team == 1 ? "US" : "RU", team == 1 ? "US" : "RU", kitName[k]);
            mkstr(asset + ASSET_NAME, nm); res_add(data);
            uint32_t ul = alloc(12 * 8); W(data + SED_WEAPON_UNLOCKS + 8, ul); W(data + SED_WEAPON_UNLOCKS + 0xc, ul + 12 * 8);
            for (int u = 0; u < 8; u++) {
                uint32_t w = alloc(0x58); mkstr(w + ASSET_NAME, u < 6 ? weaponNames[k][u] : (u == 6 ? "m9" : "knife")); res_add(w);
                W(ul + u * 12, u < 6 ? 0 : (uint32_t)(u - 5)); W(ul + u * 12 + 4, w); WB(ul + u * 12 + 8, 0); WB(ul + u * 12 + 9, 1);
            }
        }
    }
    /* map: two bases, five flags, a town with buildings and a long wall */
    float sp[2][3][3] = {{{-350, 0, -40}, {-340, 0, 30}, {-330, 0, 0}}, {{350, 0, 40}, {340, 0, -30}, {330, 0, 0}}};
    uint32_t sv = alloc(64 * 4); W(spawnMgr + 8, sv); W(spawnMgr + 0xc, sv);
    for (int t = 0; t < 2; t++) for (int i = 0; i < 3; i++) {
        float x = sp[t][i][0], z = sp[t][i][2];
        uint32_t s = entity_new(0x40, 0x1234, x, terrain(x, z), z); W(s + 0x2c, (uint32_t)(t + 1));
        uint32_t d = alloc(0x80); W(s + 0xc, d); res_add(d);
        uint32_t e = R(spawnMgr + 0xc); W(e, s); W(spawnMgr + 0xc, e + 4); spawnN++;
    }
    float fl[5][2] = {{-180, -60}, {-60, 90}, {0, 0}, {70, -95}, {190, 60}};
    const char *names[5] = {"Docks", "Church", "Square", "Factory", "Bridge"};
    mcomN = 0;
    if (getenv("RUSH")) {   /* Rush: no flags; two M-COMs (interaction entities for team 1, the attackers) */
        uint32_t ln = alloc(32); memcpy(A + (ln - FA0), "Levels/MP_005GR", 16); W(levelObj + 0x10, ln);
        float mc[4][2] = {{-60, 90}, {70, -95}, {250, 120}, {280, -60}};   /* RUSHSTAGES: a second stage, on from the start (Isla Inocentes) */
    if (getenv("HOUSE")) { mc[0][0] = -62; mc[0][1] = 92; }
        for (int i = 0; i < (getenv("RUSHSTAGES") ? 4 : 2); i++) {
            float x = mc[i][0], z = mc[i][1], y = terrain(x, z) + (getenv("HOUSE") && i == 0 ? 3.2f : 0);
            uint32_t e = entity_new(0x94, RB(VA_INTERACTION_VT), x, y, z), d = alloc(0xc0);   /* HOUSE: A upstairs */
            W(e + 0x0c, d); W(d + 160, 1); WF(d + 164, 3.0f); WF(d + 144, 2.0f); WB(d + 184, 1); W(d + 172, 2);
            uint32_t e2 = entity_new(0x94, RB(VA_INTERACTION_VT), x + 0.5f, y, z), d2 = alloc(0xc0);
            W(e2 + 0x0c, d2); W(d2 + 160, 2); WF(d2 + 164, 4.0f); WF(d2 + 144, 2.0f); WB(d2 + 184, 1); W(d2 + 172, 3); WB(e2 + 0x26, 0);   /* defuse: off until armed */
            mcom[mcomN].e = e; mcom[mcomN].e2 = e2; mcom[mcomN].armedAt = -1; mcomN++;
        }
    }
    for (int i = 0; i < 5; i++) {
        if (getenv("RUSH") && i != 0 && i != 4) continue;   /* Rush: only the two spawn bases are flags (like Valparaiso) */
        float x = fl[i][0], z = fl[i][1];
        uint32_t e = entity_new(0xb4, RB(VA_CAPTUREPOINT_VT), x, terrain(x, z), z), d = alloc(0x140);
        W(e + CP_DATA, d); WF(d + CPD_RADIUS, 12); mkstr(d + CPD_NAME, names[i]); W(d + CPD_INDEX, (uint32_t)i);
        W(e + CP_OWNER, i == 0 ? 1 : i == 4 ? 2 : 0); WF(e + CP_PROGRESS, i == 0 || i == 4 ? 100 : 0);
        cps[cpN].e = e; cps[cpN].d = d; cpN++;
    }
    /* buildings around the square, a wall with a gap between Church and Square */
    if (getenv("DIVIDER")) { float y = terrain(-40, 40); add_box(-120, y - 2, 39.8f, 60, y + 0.6f, 40.2f); fprintf(stderr, "mock: road divider z=40, x -120..60\n"); }
    if (getenv("COURTYARD")) {   /* 1.6.6: the Square flag walled in - a 2 m gate on foot only */
        add_box(-28, -5, -28, 28, 5, -27); add_box(-28, -5, 27, 28, 5, 28); add_box(-28, -5, -28, -27, 5, 28);
        add_box(27, -5, -28, 28, 5, -1); add_box(27, -5, 1, 28, 5, 28);
        fprintf(stderr, "mock: Square walled in, 2 m gate east\n"); }
    if (getenv("TOWN")) {   /* 1.6.6: a town of blocks with 12 m streets, barriers and barrels in the streets, a raised plaza */
        int nb0 = boxCount;
        for (int bx = 0; bx < 4; bx++) for (int bz = 0; bz < 3; bz++) {
            float x0 = -170 + bx * 32, z0 = -45 + bz * 32; if (bx == 2 && bz == 1) continue;   /* the plaza */
            add_box(x0, -5, z0, x0 + 20, 9, z0 + 20); }
        { float x0 = -170 + 2 * 32, z0 = -45 + 32, y = terrain(x0 + 10, z0 + 10); add_box(x0, y - 3, z0, x0 + 20, y + 1.4f, z0 + 20); }   /* plaza: 1.4 m up, steps only */
        for (int k = 0; k < 6; k++) { float x = -158 + k * 32 + 6, z = -13, y = terrain(x, z); add_box(x - 3, y - 1, z - 0.3f, x + 3, y + 0.8f, z + 0.3f); }   /* jersey barriers mid-street */
        for (int k = 0; k < 10; k++) { float x = -165 + k * 13.0f, z = 19 + (k % 3) * 2.0f, y = terrain(x, z); add_box(x - 0.3f, y - 1, z - 0.3f, x + 0.3f, y + 0.9f, z + 0.3f); }   /* barrels */
        fprintf(stderr, "mock: town, %d boxes\n", boxCount - nb0); }
    if (getenv("BARRELS")) {   /* 1.6.6: barrels and blockers scattered over the open ground between the bases */
        int nb2 = 0; for (int i = 0; i < 120; i++) { float x = -300 + (i * 97) % 600, z = -120 + (i * 61) % 240; if (hypotf(x, z) < 35) continue;
            float y = terrain(x, z); add_box(x - 0.3f, y - 1, z - 0.3f, x + 0.3f, y + 0.9f, z + 0.3f); nb2++; }
        fprintf(stderr, "mock: %d barrels\n", nb2); }
    add_box(-20, -5, 15, -8, 8, 28); add_box(10, -5, -30, 24, 9, -14); add_box(25, -5, 10, 40, 7, 22);
    add_box(-100, -5, 20, -40, 4, 23);      /* wall, gap from -40 to -30 */
    add_box(-30, -5, 20, 20, 4, 23);
    add_box(-140, -5, -80, -125, 10, -40);
    add_box(120, -5, 20, 150, 12, 35);
    if (getenv("HOUSE")) {   /* 1.6.3: a two-storey house round M-COM A - door (a thin panel) on the south side, stairs up the east side */
        float Y = terrain(-60, 90); g_houseY = Y;
        add_box(-66, Y - 2, 84, -62, Y + 6, 84.3f); add_box(-60, Y - 2, 84, -54, Y + 6, 84.3f); add_box(-62, Y + 2.2f, 84, -60, Y + 6, 84.3f);   /* south wall, doorway x -62..-60 */
        g_doorBox = boxCount; add_box(-62, Y - 1, 84.12f, -60, Y + 2.2f, 84.18f);   /* the door: a 6 cm panel in the 30 cm wall */
        add_box(-66, Y - 2, 95.7f, -54, Y + 6, 96); add_box(-66.3f, Y - 2, 84, -66, Y + 6, 96); add_box(-54, Y - 2, 84, -53.7f, Y + 6, 96);   /* north, west, east walls */
        add_box(-66.3f, Y + 6, 84, -53.7f, Y + 6.3f, 96);   /* roof */
        add_box(-66, Y + 3.0f, 88, -56, Y + 3.2f, 95.7f); add_box(-56, Y + 3.0f, 94, -54, Y + 3.2f, 95.7f);   /* upper floor + landing */
        for (int k = 0; k < 16; k++) add_box(-56.0f, Y - 1, 86 + k * 0.5f, -54, Y + 0.2f * (k + 1), 86.5f + k * 0.5f);   /* 16 steps up to the landing */
        fprintf(stderr, "mock: house at (-60, %.1f, 90), %d boxes\n", Y, boxCount);
    }
    if (getenv("ROADS")) {   /* 1.6.6: the level's road data, as BC2 keeps it */
        /* RoadData -> VectorShapeData (Points: Vec3 struct array at +52: begin +8, end +0xC) */
        int np = 9; uint32_t vs = alloc(0x60), arr = alloc(16 * 9);
        W(vs, RB(VA_VECTOR_SHAPE_VT)); W(vs + 52 + 8, arr); W(vs + 52 + 0xc, arr + 16 * np);
        for (int i = 0; i < np; i++) { float x = 320 - i * 40.0f, z = -4; WF(arr + i * 16, x); WF(arr + i * 16 + 4, terrain(x, z)); WF(arr + i * 16 + 8, z); }
        uint32_t rd = alloc(0x50); W(rd, RB(VA_ROAD_DATA_VT)); W(rd + 48, vs); mkstr(rd + 12, "MainRoad_East");
        /* TerrainSplineDecal -> TerrainSplineData (Points: reference array at +60: count +0xC, elements +0x10) */
        uint32_t sp = alloc(0x70), el = alloc(4 * 8); W(sp, RB(VA_TSPLINE_VT)); W(sp + 60 + 0xc, 8); W(sp + 60 + 0x10, el);
        for (int i = 0; i < 8; i++) { uint32_t pt = alloc(0x30); W(pt, RB(VA_TSPLINE_POINT_VT)); float x = -330 + i * 40.0f, z = 10 + i * 10.0f; WF(pt + 16, x); WF(pt + 20, terrain(x, z)); WF(pt + 24, z); W(el + i * 4, pt); }
        uint32_t dc = alloc(0x30), mat = alloc(0x20); W(dc, RB(VA_TSPLINE_DECAL_VT)); W(dc + 40, sp); WB(dc + 33, 1); mkstr(dc + 16, "Road_West"); mkstr(mat + 12, "Terrain/Decals/Dirt_Road_01"); W(dc + 28, mat);
        /* a shape in local coordinates (not a road on this ground) - must be dropped */
        uint32_t sp2 = alloc(0x70), el2 = alloc(4 * 4); W(sp2, RB(VA_TSPLINE_VT)); W(sp2 + 60 + 0xc, 4); W(sp2 + 60 + 0x10, el2);
        for (int i = 0; i < 4; i++) { uint32_t pt = alloc(0x30); W(pt, RB(VA_TSPLINE_POINT_VT)); WF(pt + 16, 3000 + i * 30.0f); WF(pt + 20, 0); WF(pt + 24, 3000); W(el2 + i * 4, pt); }
        fprintf(stderr, "mock: level road data (a road object, a road decal, a stray spline)\n");
    }
    build_vehicles();
}
void mock_level_change(void)
{
    /* old level objects become garbage: overwrite their vtables so stale use is caught */
    for (int c = 0; c < cpN; c++) W(cps[c].e, 0xdeadbeef);
    for (int i = 1; i < resCount; i++) W(resAsset[i], 0xdeadbeef);
    for (int a = FA0 + 0x2000000; a + 0x40 <= heapTop; a += 16) if (R(a) == RB(VA_SOLDIER_VT)) W(a, 0xdeadbeef);   /* the old level's soldiers are gone */
    for (int i = 0; i < 64; i++) if (mp[i].p) { W(mp[i].p + PL_SOLDIER, 0); W(mp[i].p + PL_CONTROLLED, 0); W(mp[i].p + PL_SOLDIER_DAT, 0); WB(mp[i].p + PL_READY, 0); }
    build_level();
    if (human) soldier_new(human, 10, terrain(10, 5), 5, 1.0f);
    printf("MOCK: level changed (generation %u)\n", levelGen);
}
void mock_init(void)
{
    A = calloc(1, ARENA); heapTop = FA0 + 0x2000000; g_engine.base = FA0; g_engine.ready = 1;
    ctx = RB(VA_SERVER_CONTEXT); W(ctx, RB(VA_SERVER_CONTEXT_VT));
    manager = alloc(0x100); W(ctx + 0x30, manager); W(manager + 4, 64); roster = alloc(64 * 4); W(manager + 0x58, roster); W(manager + 0x5c, roster);
    W(manager + 0x68, 0xffffffffu); idTable = alloc(64 * 4); W(manager + 0x6c, idTable);
    W(ctx + 4, alloc(16));
    uint32_t resources = alloc(16); W(RB(VA_RESOURCES_PTR), resources);
    build_level();
    /* one human on team 1, standing near the Square */
    human = eng_allocate_player(manager); W(human + PL_TEAM, 1); WB(human + PL_INGAME, 1); mp_of(human)->human = 1;
    soldier_new(human, 10, terrain(10, 5), 5, 1.0f);
}

/* ---------------- simulation ---------------- */
int sim_kills[3], sim_deaths[3], sim_captures[3], sim_shots, human_deaths;
int sim_actions[BC2_EiaUndefined]; static float prevIn[64][BC2_EiaUndefined];
int eng_player_unlock_all(uint32_t p, int *bits) { (void)p; *bits = 1024; return 1; }
int eng_player_unlock_bits(uint32_t p, unsigned *s, unsigned *t) { (void)p; *s = 1024; *t = 1024; return 1; }
void eng_set_squad(uint32_t p, uint32_t s) { (void)p; (void)s; }
int eng_resource_scan(const uint32_t *o, uint32_t *ids, int n) { (void)o; (void)ids; (void)n; return 0; }
static void kill_soldier(uint32_t p, uint32_t killer)
{
    MPlayer *m = mp_of(p); int team = (int)R(p + PL_TEAM);
    sim_deaths[team]++; if (killer) sim_kills[R(killer + PL_TEAM)]++;
    eng_exit_vehicle(p);
    { uint32_t sol = R(p + PL_SOLDIER); if (sol) WF(sol + 0x34, 0); }   /* a dead soldier has no health left */
    W(p + PL_SOLDIER, 0);
    if (A[p + PL_AI - FA0] && !g_keepAI) roster_remove(p);   /* BC2: onKilled -> leaveGame + deletePlayer for AI (unless Phoenix patched it) */
    else if (m && !A[p + PL_AI - FA0]) human_deaths++;   /* the test human stays dead */
}
/* 1.6.6: the engine's suicide: 100 damage, the soldier dies the normal way */
int eng_soldier_kill(uint32_t s)
{
    if (!inA(s, 0x244) || R(s) != RB(VA_SOLDIER_VT) || !(RF(s + 0x34) > 0)) return 0;
    WF(s + 0x34, 0); sim_suicides++;
    uint32_t p = R(s + 0x240); if (p && R(p + PL_SOLDIER) == s) kill_soldier(p, 0);
    return 1;
}
/* soldiers alive in the world that no player owns (what leaveGame leaves behind) */
int mock_ghosts(void)
{
    int n = 0;
    for (uint32_t a = FA0 + 0x2000000; a + 0x40 <= heapTop; a += 16) {
        if (R(a) != RB(VA_SOLDIER_VT) || !(RF(a + 0x34) > 0)) continue;
        int owned = 0; for (int j = 0; j < 64; j++) if (mp[j].p && R(mp[j].p + PL_SOLDIER) == a) owned = 1;
        if (!owned) n++;
    }
    return n;
}
static int los(const float *a, const float *b)
{ float h[3]; if (!eng_ray(a, b, h, NULL)) return 1; float dx = h[0]-b[0], dy = h[1]-b[1], dz = h[2]-b[2]; return dx*dx+dy*dy+dz*dz < 0.5f; }

void mock_step(float dt)
{
    g_mockTick++; g_mockTime += dt;
    if (getenv("LATEBLOCK")) {   /* 1.6.6: barriers dropped on the main road after the vehicle scan ran (props the scan never saw) */
        static int done; if (!done && g_mockTime > atof(getenv("LATEBLOCK"))) { done = 1;
            for (int k = 0; k < 3; k++) { float x = 230 - k * 70.0f, y = terrain(x, 0); add_box(x - 0.3f, y - 1, -14, x + 0.3f, y + 0.85f, 8); add_box(x - 0.4f, y - 1, -30, x + 0.4f, y + 0.9f, -29); }
            fprintf(stderr, "mock: barriers across the road at x 230/160/90 (z -14..8) t=%.0f\n", g_mockTime); } }
    vehicles_step(dt);
    if (getenv("HUMANDRIVE") && human && R(human + PL_SOLDIER)) {   /* 1.6: the human drives a ground vehicle round a loop */
        static int vi = -1; static int seg; static float t;
        if (vi < 0) for (int i = 0; i < mvehN; i++) if (!mveh[i].heli && !mveh[i].occ[0] && eng_enter_vehicle(human, mveh[i].e, 0)) { vi = i; fprintf(stderr, "mock: human drives vehicle %d\n", i); break; }
        if (vi >= 0 && mveh[vi].occ[0] == human) {
            static const float wp[4][2] = {{-60, -60}, {-60, 60}, {60, 60}, {60, -60}};
            const float *a = wp[seg], *b = wp[(seg + 1) % 4]; float L = hypotf(b[0] - a[0], b[1] - a[1]);
            t += 9.0f * dt / L; if (t >= 1) { t = 0; seg = (seg + 1) % 4; }
            float x = a[0] + (b[0] - a[0]) * t, z = a[1] + (b[1] - a[1]) * t, h = atan2f(b[0] - a[0], b[1] - a[1]);
            uint32_t root = R(mveh[vi].e + 0x28); float m[16]; memcpy(m, A + (root - FA0), 64);
            m[0] = cosf(h); m[2] = -sinf(h); m[8] = sinf(h); m[10] = cosf(h); m[12] = x; m[13] = terrain(x, z); m[14] = z;
            memcpy(A + (root - FA0), m, 64);
        }
    }
    if (getenv("HUMANWALK") && human && R(human + PL_SOLDIER)) {   /* the human walks a loop between Square, Church and Factory */
        static const float wp[4][2] = {{10, 5}, {-55, 80}, {-35, 30}, {65, -85}}; static int seg; static float t;
        const float *a = wp[seg], *b = wp[(seg + 1) % 4]; float L = hypotf(b[0] - a[0], b[1] - a[1]);
        t += 3.5f * dt / L; if (t >= 1) { t = 0; seg = (seg + 1) % 4; }
        float x = a[0] + (b[0] - a[0]) * t, z = a[1] + (b[1] - a[1]) * t;
        uint32_t rt = R(R(human + PL_SOLDIER) + 0x28); WF(rt + 48, x); WF(rt + 52, terrain(x, z)); WF(rt + 56, z);
    }
    for (int c = 0; c < mcomN; c++) {   /* Rush M-COMs: armed 25 s blows it up */
        MMcom *mc = &mcom[c];
        if (mc->armedAt >= 0 && g_mockTime - mc->armedAt > (getenv("FUSE") ? atof(getenv("FUSE")) : 25)) { mc->armedAt = -1; WB(mc->e + 0x26, 0); WB(mc->e2 + 0x26, 0); sim_mcom_destroyed++; fprintf(stderr, "mock: M-COM %d destroyed\n", c); }
    }
    /* movement + firing */
    for (int i = 0; i < 64; i++) {
        MPlayer *m = &mp[i]; if (!m->p || m->human) continue;
        uint32_t s = R(m->p + PL_SOLDIER); if (!s) continue;
        if (R(m->p + PL_CONTROLLED)) continue;   /* in a vehicle */
        uint32_t root = R(s + 0x28);
        float x = RF(root + 48), y = RF(root + 52), z = RF(root + 56);
        float h = -m->yaw; float fx = sinf(h), fz = cosf(h);
        float speed = m->in[BC2_EiaSprint] > 0.5f ? 6.3f : 3.5f;
        float thr = m->in[BC2_EiaThrottle], str = m->in[BC2_EiaStrafe];
        if (m->tick + 2 < g_mockTick) { thr = 0; str = 0; }   /* like BC2: stale input frames do not move the soldier */
        if (!m->human && m->stepped + 2 < g_mockTick) { thr = 0; str = 0; }
        if (!m->human && getenv("MOCK_VELOCITY_ONLY") && g_velTick[i] + 2 < g_mockTick) { thr = 0; str = 0; }   /* like BC2: AI soldiers only walk when their movement step runs */
        /* strafe +1 moves towards -right, right = (fz, -fx) */
        float vx = (fx * thr - fz * str) * speed, vz = (fz * thr + fx * str) * speed;
        float nx = x + vx * dt, nz = z + vz * dt;
        float a[3] = {x, y + 0.5f, z}, b[3] = {nx + (vx > 0 ? 0.3f : -0.3f) * (fabsf(vx) > 0.01f), y + 0.5f, nz + (vz > 0 ? 0.3f : -0.3f) * (fabsf(vz) > 0.01f)}, hh[3], nn[3];
        if (eng_ray(a, b, hh, nn) && nn[1] < 0.7f) { nx = x; nz = z; }
        if (getenv("PINTEST") && !m->human && i % 5 == 2 && g_mockTime > 20 && g_mockTime < 120) { nx = x; nz = z; }   /* 1.6.6: wedged soldiers */
        float gy = ground_at(nx, y + 0.6f, nz);
        if (gy - y > 0.8f) { nx = x; nz = z; gy = y; }
        float v[16]; memcpy(v, A + (root - FA0), 64);
        v[0] = cosf(h); v[2] = -sinf(h); v[8] = sinf(h); v[10] = cosf(h); v[12] = nx; v[13] = gy; v[14] = nz;
        memcpy(A + (root - FA0), v, 64);
        if (g_doorBox >= 0 && m->in[BC2_EiaMeleeAttack] > 0.5f && prevIn[i][BC2_EiaMeleeAttack] <= 0.5f) {   /* knifing the door breaks it */
            Box *db = &boxes[g_doorBox]; float cx = (db->mn[0] + db->mx[0]) * 0.5f, cz = (db->mn[2] + db->mx[2]) * 0.5f;
            if (getenv("HOUSEDBG") && hypotf(nx - cx, nz - cz) < 6.0f) fprintf(stderr, "mock: melee %.1f m from the door (t=%.1f) at (%.1f, %.1f)\n", hypotf(nx - cx, nz - cz), g_mockTime, nx, nz);
            if (hypotf(nx - cx, nz - cz) < 2.0f) { db->mn[1] = 900; db->mx[1] = 901; g_doorBox = -1; sim_doors++; fprintf(stderr, "mock: the house door is broken (t=%.1f)\n", g_mockTime); }
        }
        for (int a = 0; a < BC2_EiaUndefined; a++) { if (m->in[a] > 0.5f && prevIn[i][a] <= 0.5f) sim_actions[a]++; prevIn[i][a] = m->in[a]; }
        if (m->in[BC2_EiaGrenadeLauncher] > 0.5f) m->launcherAt = g_mockTime;
        if (m->in[BC2_EiaFire] > 0.5f && m->launcherAt > 0 && g_mockTime - m->launcherAt < 2.5) {   /* a rocket */
            m->launcherAt = 0; sim_rockets++;
            float eye[3] = {nx, gy + 1.55f, nz}; float pitch = m->pitch * (float)g_cfg.pitchSign;
            float dir[3] = {sinf(h) * cosf(pitch), sinf(pitch), cosf(h) * cosf(pitch)};
            for (int vi2 = 0; vi2 < mvehN; vi2++) {
                MVeh *v = &mveh[vi2]; int crewed = 0; for (int k = 0; k < 4; k++) if (v->occ[k] && R(v->occ[k] + PL_TEAM) != R(m->p + PL_TEAM)) crewed = 1;
                if (!crewed) continue;
                uint32_t vr = R(v->e + 0x28); float t[3] = {RF(vr + 48), RF(vr + 52) + 1.2f, RF(vr + 56)};
                float w[3] = {t[0]-eye[0], t[1]-eye[1], t[2]-eye[2]}, along = w[0]*dir[0] + w[1]*dir[1] + w[2]*dir[2];
                if (along <= 0) continue;
                float tf = along / 140.0f, drop = 0.5f * 9.81f * tf * tf;   /* rocket at 140 m/s, falling; the target moves meanwhile */
                MVeh *vv = v; float tvx = vv->heli ? vv->vel[0] : sinf(atan2f(RF(vr + 32), RF(vr + 40))) * vv->speed, tvz = vv->heli ? vv->vel[2] : cosf(atan2f(RF(vr + 32), RF(vr + 40))) * vv->speed;
                t[0] += tvx * tf; t[2] += tvz * tf; if (vv->heli) t[1] += vv->vel[1] * tf;
                w[0] = t[0]-eye[0]; w[1] = t[1]-eye[1]; w[2] = t[2]-eye[2]; along = w[0]*dir[0] + w[1]*dir[1] + w[2]*dir[2];
                float off[3] = {w[0]-dir[0]*along, w[1]-dir[1]*along + drop, w[2]-dir[2]*along};
                if (sqrtf(off[0]*off[0] + off[1]*off[1] + off[2]*off[2]) < 3.0f) { sim_rocket_hits++; v->hp -= 40; if (v->hp <= 0) vehicle_destroyed(v, m->p); break; }
            }
            /* soldiers: the rocket bursts within 2.5 m of one (it is aimed at the feet) */
            for (int j = 0; j < 64; j++) {
                if (!mp[j].p || j == i || R(mp[j].p + PL_TEAM) == R(m->p + PL_TEAM) || R(mp[j].p + PL_CONTROLLED)) continue;
                uint32_t ts = R(mp[j].p + PL_SOLDIER); if (!ts) continue;
                uint32_t tr = R(ts + 0x28); float t[3] = {RF(tr + 48), RF(tr + 52) + 0.4f, RF(tr + 56)};
                float w[3] = {t[0]-eye[0], t[1]-eye[1], t[2]-eye[2]}, along = w[0]*dir[0] + w[1]*dir[1] + w[2]*dir[2];
                if (along <= 0 || along > 150) continue;
                float tf = along / 140.0f, drop = 0.5f * 9.81f * tf * tf;
                float off[3] = {w[0]-dir[0]*along, w[1]-dir[1]*along + drop, w[2]-dir[2]*along};
                if (sqrtf(off[0]*off[0] + off[1]*off[1] + off[2]*off[2]) < 2.5f) { sim_rocket_soldier++; kill_soldier(mp[j].p, m->p); break; }
            }
        }
        if (m->in[BC2_EiaFire] > 0.5f && m->wpn == 5 && g_mockTime - m->gadget1At > 6) { if (sim_tool_shots++ % 20 == 0) fprintf(stderr, "mock: tool shot t=%.2f player %d (gadget1 %.1f s ago)\n", g_mockTime, i, g_mockTime - m->gadget1At); }   /* the repair tool used as a gun */
        if (m->in[BC2_EiaFire] > 0.5f) {
            sim_shots++;
            float eye[3] = {nx, gy + 1.55f, nz};
            float pitch = m->pitch * (float)g_cfg.pitchSign;
            float dir[3] = {sinf(h) * cosf(pitch), sinf(pitch), cosf(h) * cosf(pitch)};
            uint32_t bestP = 0; float bestD = 1e9f;
            for (int j = 0; j < 64; j++) {
                if (!mp[j].p || j == i || R(mp[j].p + PL_TEAM) == R(m->p + PL_TEAM)) continue;
                uint32_t ts = R(mp[j].p + PL_SOLDIER); if (!ts) continue;
                uint32_t tr = R(ts + 0x28); float t[3] = {RF(tr + 48), RF(tr + 52) + 1.2f, RF(tr + 56)};
                float w[3] = {t[0]-eye[0], t[1]-eye[1], t[2]-eye[2]}, along = w[0]*dir[0] + w[1]*dir[1] + w[2]*dir[2];
                if (along <= 0) continue;
                float off[3] = {w[0]-dir[0]*along, w[1]-dir[1]*along, w[2]-dir[2]*along};
                float miss = sqrtf(off[0]*off[0] + off[1]*off[1] + off[2]*off[2]);
                if (miss < 0.45f && along < bestD && los(eye, t)) { bestD = along; bestP = mp[j].p; }
            }
            if (bestP) {
                uint32_t ts = R(bestP + PL_SOLDIER); float hp = RF(ts + 0x34) - 4.0f; WF(ts + 0x34, hp);
                if (hp <= 0) kill_soldier(bestP, m->p);
            }
        }
    }
    /* conquest capture */
    for (int c = 0; c < cpN; c++) {
        uint32_t e = cps[c].e; float pos[3]; eng_entity_position(e, pos);
        int n[3] = {0};
        for (int j = 0; j < 64; j++) { if (!mp[j].p) continue; uint32_t s = R(mp[j].p + PL_SOLDIER); if (!s) continue;
            uint32_t r = R(s + 0x28); if (hypotf(RF(r + 48) - pos[0], RF(r + 56) - pos[2]) < 12) n[R(mp[j].p + PL_TEAM)]++; }
        int owner = (int)R(e + CP_OWNER); float prog = RF(e + CP_PROGRESS);
        int att = n[1] && !n[2] ? 1 : n[2] && !n[1] ? 2 : 0;
        W(e + CP_ATTACKER, att != owner ? (uint32_t)att : 0);
        if (att && att != owner) {
            if (owner) { prog -= 10 * dt * n[att]; if (prog <= 0) { prog = 0; W(e + CP_OWNER, 0); } }
            else { prog += 10 * dt * n[att]; if (prog >= 100) { prog = 100; W(e + CP_OWNER, (uint32_t)att); sim_captures[att]++; } }
        } else if (att && att == owner && prog < 100) prog = fminf(100, prog + 10 * dt * n[att]);
        WF(e + CP_PROGRESS, prog);
    }
}
void mock_report(void)
{
    printf("flags:");
    for (int c = 0; c < cpN; c++) printf(" %d", (int)R(cps[c].e + CP_OWNER));
    printf("  vehicles: enters %d exits %d, %.2f km driven, helicopter max height %.0f m\n", sim_vehicle_enters, sim_vehicle_exits, sim_vehicle_km, sim_heli_max_agl);
    if (getenv("ROADS")) printf("  ROADUSE ground vehicles drove %.2f km, %.2f km of it (%.0f%%) within 6 m of a road line\n", sim_ground_km, sim_road_km, sim_ground_km > 0 ? 100 * sim_road_km / sim_ground_km : 0);
    printf("  vehicle guns: firing %.1f s, on target %.1f s; rockets %d (%d hit vehicles, %d killed soldiers), vehicles destroyed %d [driver %d/%d pilot %d/%d gunner %d/%d]\n", sim_veh_fire / 30.0f, sim_veh_hits / 30.0f, sim_rockets, sim_rocket_hits, sim_rocket_soldier, sim_veh_kills, sim_vh[0], sim_vf[0], sim_vh[1], sim_vf[1], sim_vh[2], sim_vf[2]);
    printf("  GHOSTS %d living soldier(s) without a player, %d suicide(s)\n", mock_ghosts(), sim_suicides);
    printf("  human deaths %d, kills t1 %d t2 %d, deaths t1 %d t2 %d, captures t1 %d t2 %d, shots %d, rays %u, roster %d\n",
           human_deaths, sim_kills[1], sim_kills[2], sim_deaths[1], sim_deaths[2], sim_captures[1], sim_captures[2], sim_shots, rays, roster_count());
}
int eng_character_state(uint32_t s) { (void)s; return 0; }
int eng_character_push(uint32_t s, float x, float y, float z) { (void)y; return eng_character_drive_velocity(s, x, z); }
int eng_player_add_weapon(uint32_t p, uint32_t w) { (void)p; (void)w; return 1; }
int eng_player_weapon_count(uint32_t p) { (void)p; return 3; }
int eng_set_net_online_id(uint32_t p, uint32_t lo, uint32_t hi, const char *n) { (void)p; (void)lo; (void)hi; (void)n; return 1; }
int eng_set_online_id(uint32_t p, uint32_t lo, uint32_t hi, const char *n) { (void)p; (void)lo; (void)hi; (void)n; return 1; }
void eng_describe_online_id(uint32_t p, char *o, unsigned n) { (void)p; snprintf(o, n, "mock"); }
