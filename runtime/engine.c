#include "win_common.h"
#include "engine.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#define TC __attribute__((thiscall))
BC2Engine g_engine;

typedef struct Fingerprint { const char *name; uint32_t va; unsigned char bytes[16]; } Fingerprint;
static const Fingerprint kPrints[] = {
{"allocate_player",VA_ALLOCATE_PLAYER,{0x81,0xec,0xa8,0x00,0x00,0x00,0x56,0x8b,0xf1,0x8b,0x4e,0x5c,0x2b,0x4e,0x58,0x33}},
{"register_player",VA_REGISTER_PLAYER,{0x81,0xec,0xec,0x00,0x00,0x00,0x89,0x4c,0x24,0x08,0x81,0xc1,0x90,0x00,0x00,0x00}},
{"set_team",VA_SET_TEAM,{0x8b,0x54,0x24,0x04,0x56,0x8b,0xf1,0x3b,0x96,0xa8,0x00,0x00,0x00,0x74,0x50,0x8b}},
{"set_ai",VA_SET_AI,{0x8a,0x44,0x24,0x04,0x88,0x81,0x84,0x0d,0x00,0x00,0xc2,0x04,0x00,0xcc,0xcc,0xcc}},
{"ready",VA_READY,{0x56,0x8b,0xf1,0x80,0xbe,0x68,0x0d,0x00,0x00,0x00,0x75,0x1b,0xc6,0x86,0x68,0x0d}},
{"select_unlocks",VA_SELECT_UNLOCKS,{0x8b,0x44,0x24,0x04,0x56,0x8b,0xf1,0x8d,0x8e,0xe8,0x01,0x00,0x00,0x3b,0xc8,0x74}},
{"select_kit",VA_SELECT_KIT,{0x56,0x8b,0xf1,0x80,0xbe,0x68,0x0d,0x00,0x00,0x00,0x0f,0x84,0x0e,0x01,0x00,0x00}},
{"selected_spawn",VA_SELECTED_SPAWN,{0x81,0xec,0x98,0x01,0x00,0x00,0x57,0x8b,0xf9,0x83,0xbf,0x3c,0x0c,0x00,0x00,0x00}},
{"resource_to_id",VA_RESOURCE_TO_ID,{0x81,0xec,0xbc,0x00,0x00,0x00,0x56,0x57,0x8b,0xbc,0x24,0xc8,0x00,0x00,0x00,0x85}},
{"resource_by_id",VA_RESOURCE_BY_ID,{0x81,0xec,0xb8,0x00,0x00,0x00,0x83,0xbc,0x24,0xbc,0x00,0x00,0x00,0x00,0x56,0x8b}},
{"input_ctor",VA_INPUT_CTOR,{0x51,0x56,0x57,0x89,0x4c,0x24,0x08,0xbf,0x01,0x00,0x00,0x00,0x8d,0x71,0x08,0x90}},
{"input_attach",VA_INPUT_ATTACH,{0x8b,0x44,0x24,0x04,0x89,0x81,0x38,0x0c,0x00,0x00,0xc2,0x04,0x00,0xcc,0xcc,0xcc}},
{"state_set_level",VA_STATE_SET_LEVEL,{0xd9,0x44,0x24,0x08,0x81,0xec,0xb0,0x00,0x00,0x00,0xdd,0x05,0xb8,0x34,0x6e,0x01}},
{"state_set_dt",VA_STATE_SET_DT,{0xf3,0x0f,0x10,0x44,0x24,0x04,0x81,0xec,0xa8,0x00,0x00,0x00,0x0f,0x2f,0x05,0x90}},
{"input_push",VA_INPUT_PUSH,{0x8b,0x54,0x24,0x04,0x56,0x8b,0xf1,0x8b,0x86,0xa0,0x01,0x00,0x00,0x8b,0x8e,0xa4}},
{"soldier_set_aiming",VA_SOLDIER_SET_AIMING,{0xf3,0x0f,0x10,0x44,0x24,0x04,0x81,0xec,0xb8,0x00,0x00,0x00,0x0f,0x2f,0x05,0x90}},
{"ray_query",VA_RAY_QUERY,{0x55,0x8b,0xec,0x83,0xe4,0xf0,0x81,0xec,0xf4,0x01,0x00,0x00,0x53,0x56,0x57,0x6a}},
{"leave_game",VA_LEAVE_GAME,{0x81,0xec,0xa8,0x00,0x00,0x00,0x56,0x8b,0xf1,0x80,0xbe,0x80,0x0c,0x00,0x00,0x00}},
{"delete_player",VA_DELETE_PLAYER,{0x81,0xec,0xb0,0x00,0x00,0x00,0x53,0x56,0x8b,0xf1,0x8d,0x86,0x90,0x00,0x00,0x00}},
{"spawn_entity_id",VA_SPAWN_ENTITY_ID,{0x8b,0x41,0x0c,0x50,0xe8,0xb7,0x99,0xee,0xff,0x8b,0xc8,0xe8,0x10,0xf1,0xfb,0xff}},
{"manager_update",VA_MANAGER_UPDATE,{0x81,0xec,0xb0,0x00,0x00,0x00,0x56,0x8b,0xf1,0x83,0x7e,0x68,0xff,0x74,0x6e,0x80}},
{"soldier_move_input",VA_SOLDIER_MOVE_INPUT,{0x56,0x8b,0xf1,0x8b,0x86,0xa0,0x02,0x00,0x00,0x85,0xc0,0x57,0x8b,0x7c,0x24,0x10}},
{"character_step",VA_CHARACTER_STEP,{0x55,0x8b,0xec,0x83,0xe4,0xf0,0xf3,0x0f,0x10,0x05,0x70,0xdb,0x8d,0x01,0x81,0xec}},
{"enter_vehicle",VA_ENTER_VEHICLE,{0x81,0xec,0x90,0x01,0x00,0x00,0x53,0x55,0x56,0x8b,0xb4,0x24,0xa0,0x01,0x00,0x00}},
{"exit_vehicle",VA_EXIT_VEHICLE,{0x81,0xec,0xe4,0x00,0x00,0x00,0x56,0x6a,0x00,0x68,0x48,0xaf,0x8d,0x01,0x68,0x00}},
{"eastl_alloc",VA_EASTL_ALLOC,{0x8b,0x54,0x24,0x04,0x56,0xb9,0x04,0x00,0x00,0x00,0x2b,0xca,0x8b,0xf1,0xc1,0xfe}},
{"set_online_id",VA_SET_ONLINE_ID,{0x8d,0x41,0x18,0x8b,0x4c,0x24,0x04,0x81,0xec,0xa8,0x00,0x00,0x00,0x3b,0xc8,0x74}},
{"set_net_online_id",VA_SET_NET_ONLINE_ID,{0x53,0x8b,0xd9,0x8b,0x8b,0xb0,0x00,0x00,0x00,0x8d,0x83,0xb0,0x00,0x00,0x00,0x56}},
{"set_squad_id",VA_SET_SQUAD_ID,{0x81,0xec,0xa8,0x00,0x00,0x00,0x55,0x57,0x8b,0xbc,0x24,0xb4,0x00,0x00,0x00,0x8b}},
{"set_weapon_index",VA_SET_WEAPON_INDEX,{0x81,0xec,0xa8,0x00,0x00,0x00,0x56,0x8b,0xf1,0x83,0xbe,0x90,0x02,0x00,0x00,0x00}},
{"interact",VA_INTERACT,{0x81,0xec,0xb0,0x00,0x00,0x00,0x56,0x8b,0xf1,0x8a,0x46,0x26,0xc0,0xe8,0x03,0xa8}},
{"entity_transform",VA_ENTITY_TRANSFORM,{0xf6,0x41,0x26,0x80,0x8b,0x41,0x28,0x74,0x0e,0x0f,0xb6,0x48,0x0e,0x8d,0x0c,0x89}},
};

int eng_read(uint32_t address, void *out, unsigned size)
{ return phx_read(GetCurrentProcess(), address, out, size); }
int eng_u32(uint32_t address, uint32_t *out) { return eng_read(address, out, 4); }
int eng_write32(uint32_t a, uint32_t v)
{ SIZE_T n = 0; return WriteProcessMemory(GetCurrentProcess(), (void *)(uintptr_t)a, &v, 4, &n) && n == 4; }
int eng_ptr(uint32_t p) { return p >= 0x10000u && p <= 0xfffe0000u && !(p & 3); }

int eng_verify(char *why, unsigned whySize)
{
    if (why && whySize) why[0] = 0;
    for (unsigned i = 0; i < sizeof kPrints / sizeof kPrints[0]; i++) {
        unsigned char b[16];
        if (!eng_read(RB(kPrints[i].va), b, 16) || memcmp(b, kPrints[i].bytes, 16)) {
            if (why) { unsigned n = 0; const char *s = kPrints[i].name; while (*s && n + 1 < whySize) why[n++] = *s++; why[n] = 0; }
            g_engine.ready = 0; return 0;
        }
    }
    g_engine.ready = 1; return 1;
}

int eng_string(uint32_t field, char *out, unsigned size)
{
    uint32_t se[2]; if (!size) return 0; out[0] = 0;
    if (!eng_read(field, se, 8) || !eng_ptr(se[0] & ~3u) || se[1] < se[0] || se[1] - se[0] > 255) return 0;
    unsigned n = se[1] - se[0]; if (n >= size) n = size - 1;
    if (n && !eng_read(se[0], out, n)) { out[0] = 0; return 0; }
    out[n] = 0;
    for (unsigned i = 0; i < n; i++) if ((unsigned char)out[i] < 32 || (unsigned char)out[i] > 126) out[i] = '?';
    return 1;
}

uint32_t eng_context(void)
{
    uint32_t ctx = RB(VA_SERVER_CONTEXT), vt = 0;
    return eng_u32(ctx, &vt) && vt == RB(VA_SERVER_CONTEXT_VT) ? ctx : 0;
}
uint32_t eng_player_manager(void) { uint32_t c = eng_context(), m = 0; return c && eng_u32(c + 0x30, &m) && eng_ptr(m) ? m : 0; }
uint32_t eng_level_object(void)  { uint32_t c = eng_context(), m = 0; return c && eng_u32(c + 0x34, &m) && eng_ptr(m) ? m : 0; }
static uint32_t level_sub(void) { uint32_t l = eng_level_object(), s = 0; return l && eng_u32(l + 0x78, &s) && eng_ptr(s) ? s : 0; }
uint32_t eng_team_info(void)     { uint32_t s = level_sub(), t = 0; return s && eng_u32(s + 4, &t) && eng_ptr(t) ? t : 0; }
uint32_t eng_spawn_manager(void) { uint32_t s = level_sub(), t = 0; return s && eng_u32(s + 8, &t) && eng_ptr(t) ? t : 0; }
uint32_t eng_resources(void)     { uint32_t r = 0; return eng_u32(RB(VA_RESOURCES_PTR), &r) && eng_ptr(r) ? r : 0; }

typedef uint32_t (TC *FnAlloc)(void *, uint32_t);
typedef void (TC *FnRegister)(void *, void *, const void *);
typedef void (TC *FnOneU)(void *, uint32_t);
typedef void (TC *FnNoArg)(void *);
typedef void (TC *FnKit)(void *, uint32_t, void *, uint32_t);
typedef unsigned char (TC *FnSpawn)(void *, uint32_t);
typedef uint32_t (TC *FnRes)(void *, uint32_t);
typedef void (TC *FnLevel)(void *, int, float);
typedef void (TC *FnFloat)(void *, float);
typedef void (TC *FnPush)(void *, const void *);
typedef void (TC *FnAim)(void *, float, float);
typedef uint32_t (TC *FnRetU)(void *);
typedef int (TC *FnRay)(void *, const char *, int, const float *, const float *, void *, int, uint32_t, uint32_t, void *);

#define CALL(type, va) ((type)(uintptr_t)RB(va))
#define P(x) ((void *)(uintptr_t)(x))

uint32_t eng_allocate_player(uint32_t m) { return CALL(FnAlloc, VA_ALLOCATE_PLAYER)(P(m), 0); }
void eng_register_player(uint32_t m, uint32_t p, const PhxName *n) { CALL(FnRegister, VA_REGISTER_PLAYER)(P(m), P(p), n); }
void eng_set_team(uint32_t p, uint32_t t) { CALL(FnOneU, VA_SET_TEAM)(P(p), t); }
void eng_set_ai(uint32_t p, int ai) { CALL(FnOneU, VA_SET_AI)(P(p), ai ? 1u : 0u); }
void eng_ready(uint32_t p) { CALL(FnNoArg, VA_READY)(P(p)); }
void eng_select_unlocks(uint32_t p, PhxPairVector *v) { CALL(FnOneU, VA_SELECT_UNLOCKS)(P(p), (uint32_t)(uintptr_t)v); }
void eng_select_kit(uint32_t p, uint32_t id, PhxPairVector *v, uint32_t f) { CALL(FnKit, VA_SELECT_KIT)(P(p), id, v, f); }
int eng_selected_spawn(uint32_t p, uint32_t id) { return CALL(FnSpawn, VA_SELECTED_SPAWN)(P(p), id) ? 1 : 0; }
uint32_t eng_resource_to_id(uint32_t asset)
{ uint32_t r = eng_resources(); return r && eng_ptr(asset) ? CALL(FnRes, VA_RESOURCE_TO_ID)(P(r), asset) : 0; }
uint32_t eng_resource_by_id(uint32_t id)
{ uint32_t r = eng_resources(); return r && id ? CALL(FnRes, VA_RESOURCE_BY_ID)(P(r), id) : 0; }
void eng_input_ctor(void *input) { CALL(FnNoArg, VA_INPUT_CTOR)(input); }
void eng_input_attach(uint32_t p, void *input) { CALL(FnOneU, VA_INPUT_ATTACH)(P(p), (uint32_t)(uintptr_t)input); }
void eng_state_set_level(void *s, int a, float v) { CALL(FnLevel, VA_STATE_SET_LEVEL)(s, a, v); }
void eng_state_set_dt(void *s, float dt) { CALL(FnFloat, VA_STATE_SET_DT)(s, dt); }
void eng_input_push(void *input, const void *state) { CALL(FnPush, VA_INPUT_PUSH)(input, state); }
void eng_soldier_set_aiming(uint32_t soldier, float yaw, float pitch)
{
    const float twoPi = 6.2831850f;
    uint32_t vt = 0;
    if (!isfinite(yaw) || !isfinite(pitch) || !eng_u32(soldier, &vt) || vt != RB(VA_SOLDIER_VT)) return;
    yaw = fmodf(yaw, twoPi); if (yaw < 0) yaw += twoPi; if (yaw < 0 || yaw > twoPi) yaw = 0;
    if (pitch > 1.45f) pitch = 1.45f; if (pitch < -1.45f) pitch = -1.45f;
    CALL(FnAim, VA_SOLDIER_SET_AIMING)(P(soldier), yaw, pitch);
}
typedef void (TC *FnMoveInput)(void *, uint32_t, void *);
typedef void (TC *FnStep)(void *, float);
int eng_character_player_mode(uint32_t soldier)
{
    uint32_t vt = 0, phys = 0, aiBody = 1; uint8_t f[4] = {0};
    if (!eng_ptr(soldier) || !eng_u32(soldier, &vt) || vt != RB(VA_SOLDIER_VT)) return 0;
    if (!eng_u32(soldier + SOL_PHYSICS, &phys) || !eng_ptr(phys) || !eng_read(phys + 0xdc, f, 4)) return 0;
    if (f[1] == 0 && f[2] == 1) return 2;
    if (!eng_u32(phys + 0xb8, &aiBody) || aiBody != 0) return 0;   /* an AI rigid body exists: leave it */
    uint8_t mode[2] = {0, 1}; SIZE_T n = 0;
    return WriteProcessMemory(GetCurrentProcess(), (void *)(uintptr_t)(phys + 0xdd), mode, 2, &n) && n == 2;
}
static uint32_t char_physics(uint32_t soldier)
{
    uint32_t vt = 0, phys = 0;
    if (!eng_ptr(soldier) || !eng_u32(soldier, &vt) || vt != RB(VA_SOLDIER_VT)) return 0;
    return eng_u32(soldier + SOL_PHYSICS, &phys) && eng_ptr(phys) ? phys : 0;
}
int eng_character_ai_mode(uint32_t soldier)
{
    uint32_t phys = char_physics(soldier); uint8_t f[2] = {0}, mode[2] = {1, 0}; SIZE_T n = 0;
    if (!phys || !eng_read(phys + 0xdd, f, 2)) return 0;
    if (f[0] == 1 && f[1] == 0) return 2;
    return WriteProcessMemory(GetCurrentProcess(), (void *)(uintptr_t)(phys + 0xdd), mode, 2, &n) && n == 2;
}
int eng_character_drive_velocity(uint32_t soldier, float vx, float vz)
{
    uint32_t phys = char_physics(soldier), proxy = 0; float v[4]; SIZE_T n = 0;
    if (!phys || !eng_u32(phys + 0x40, &proxy) || !eng_ptr(proxy) || !eng_read(proxy + 0x10, v, 16)) return 0;
    if (!isfinite(v[0]) || !isfinite(v[1]) || !isfinite(v[2]) || fabsf(v[1]) > 200) return 0;
    v[0] = vx; v[2] = vz;
    return WriteProcessMemory(GetCurrentProcess(), (void *)(uintptr_t)(proxy + 0x10), v, 12, &n) && n == 12;
}
/* Selected unlocks: ServerPlayer+0xD28 is an intrusive list {next, prev} of 12-byte nodes {next, prev, asset}
   (refcounted: asset vt[0] = addRef, vt[1] = release); selectKit (0x625670) fills it from the pair resource ids and
   the soldier's weapon setup gives every listed selectable weapon. Kits whose weapons have no resource id (most of
   them) can still be selected by appending the asset itself - built exactly as selectKit builds its nodes. */
typedef uint32_t (__cdecl *FnEastlAlloc)(uint32_t, const char *, int, int, const char *, int);
typedef void (TC *FnRef)(void *);
int eng_player_add_weapon(uint32_t player, uint32_t weapon)
{
    uint32_t head = player + 0xd28, first = 0, prev = 0, vt = 0, addref = 0;
    if (!eng_ptr(player) || !eng_ptr(weapon) || !eng_u32(head, &first) || !eng_u32(head + 4, &prev) || !eng_ptr(first) || !eng_ptr(prev)) return 0;
    if (!eng_u32(weapon, &vt) || !eng_ptr(vt) || !eng_u32(vt, &addref) || addref < g_engine.base || addref > g_engine.base + 0x2000000) return 0;
    uint32_t node = ((FnEastlAlloc)(uintptr_t)RB(VA_EASTL_ALLOC))(12, (const char *)(uintptr_t)RB(0x16e1b3cu), 0, 0, (const char *)(uintptr_t)RB(0x16e1b48u), 0xe9);
    if (!eng_ptr(node)) return 0;
    ((FnRef)(uintptr_t)addref)(P(weapon));
    uint32_t n[3] = {head, prev, weapon}; SIZE_T w = 0;
    if (!WriteProcessMemory(GetCurrentProcess(), (void *)(uintptr_t)node, n, 12, &w) || w != 12) return 0;
    eng_write32(prev, node);        /* prev->next = node */
    eng_write32(head + 4, node);    /* head->prev = node */
    return 1;
}
/* Scoreboard: a human's online identity (+0xB0: 64-bit id + persona name) comes from the client connection and is
   sent to every client with the player; bots have none. Phoenix gives each bot one with its own name. */
typedef void (TC *FnOnline)(void *, const void *);
int eng_set_net_online_id(uint32_t player, uint32_t idLow, uint32_t idHigh, const char *name)
{
    unsigned char id[0x8c]; memset(id, 0, sizeof id);
    memcpy(id, &idLow, 4); memcpy(id + 4, &idHigh, 4);
    strncpy((char *)id + 8, name, 0x80);
    CALL(FnOnline, VA_SET_NET_ONLINE_ID)(P(player), id);
    uint32_t got = 0; return eng_u32(player + 0xb0, &got) && got == idLow;
}
int eng_set_online_id(uint32_t player, uint32_t idLow, uint32_t idHigh, const char *name)
{
    unsigned char id[0x8c]; memset(id, 0, sizeof id);
    memcpy(id, &idLow, 4); memcpy(id + 4, &idHigh, 4);
    strncpy((char *)id + 8, name, 0x80);
    CALL(FnOnline, VA_SET_ONLINE_ID)(P(player), id);
    uint32_t got = 0; return eng_u32(player + 0x18, &got) && got == idLow;
}
void eng_describe_online_id(uint32_t player, char *out, unsigned size)
{
    uint32_t a[2] = {0, 0}, b[2] = {0, 0}; char n1[40] = "", n2[40] = "";
    eng_read(player + 0x18, a, 8); eng_read(player + 0x20, n1, 39);
    eng_read(player + 0xb0, b, 8); eng_read(player + 0xb8, n2, 39);
    for (int i = 0; n1[i]; i++) if (n1[i] < 32 || n1[i] > 126) n1[i] = '?';
    for (int i = 0; n2[i]; i++) if (n2[i] < 32 || n2[i] > 126) n2[i] = '?';
    snprintf(out, size, "+0x18 id %08x%08x '%s', +0xB0 id %08x%08x '%s'", a[1], a[0], n1, b[1], b[0], n2);
}
int eng_player_weapon_count(uint32_t player)
{
    uint32_t head = player + 0xd28, n = 0; int c = 0;
    if (!eng_u32(head, &n)) return -1;
    while (n && n != head && c < 64) { if (!eng_u32(n, &n)) return -1; c++; }
    return c;
}
/* character state [[phys+0x44]+0x10]: 0 OnGround 1 Jumping 2 InAir 3 Climbing 4 Falling 5 Parachute 6 Swimming */
int eng_character_state(uint32_t soldier)
{
    uint32_t phys = char_physics(soldier), ctx = 0; int32_t st = -1;
    if (!phys || !eng_u32(phys + 0x44, &ctx) || !eng_ptr(ctx) || !eng_read(ctx + 0x10, &st, 4) || st < 0 || st > 16) return -1;
    return st;
}
int eng_character_push(uint32_t soldier, float vx, float vy, float vz)
{
    uint32_t phys = char_physics(soldier), proxy = 0; float v[3]; SIZE_T n = 0;
    if (!phys || !eng_u32(phys + 0x40, &proxy) || !eng_ptr(proxy) || !eng_read(proxy + 0x10, v, 12)) return 0;
    if (!isfinite(v[0]) || !isfinite(v[1]) || !isfinite(v[2])) return 0;
    v[0] = vx; v[1] = vy; v[2] = vz;
    return WriteProcessMemory(GetCurrentProcess(), (void *)(uintptr_t)(proxy + 0x10), v, 12, &n) && n == 12;
}
void eng_debug_character(uint32_t soldier, char *out, unsigned size)
{
    uint32_t phys = 0; unsigned char f[4] = {0}; float in[20] = {0}; int n = 0;
    out[0] = 0;
    if (!eng_u32(soldier + SOL_PHYSICS, &phys) || !eng_ptr(phys)) { snprintf(out, size, "no physics"); return; }
    uint32_t proxy = 0; float vel[3] = {0};
    eng_read(phys + 0xdc, f, 4); eng_read(phys + 0x120, in, sizeof in);
    if (eng_u32(phys + 0x40, &proxy) && eng_ptr(proxy)) eng_read(proxy + 0x10, vel, 12);
    static const char *sn[] = {"on ground", "jumping", "in air", "climbing", "falling", "parachute", "swimming"};
    int cs = eng_character_state(soldier);
    n = snprintf(out, size, "physics %08x state %d (%s) flags dc..df %u %u %u %u velocity %.2f %.2f %.2f input", phys, cs, cs >= 0 && cs < 7 ? sn[cs] : "?",
                 f[0], f[1], f[2], f[3], vel[0], vel[1], vel[2]);
    for (int i = 0; i < 20 && n > 0 && (unsigned)n < size; i++) n += snprintf(out + n, size - n, " %.2f", in[i]);
}
int eng_player_unlock_bits(uint32_t player, unsigned *setBits, unsigned *totalBits)
{
    uint32_t words = 0, bits = 0, nwords = 0; *setBits = *totalBits = 0;
    if (!eng_u32(player + PL_UNLOCK_BITS + 4, &words) || !eng_u32(player + PL_UNLOCK_BITS + 0xc, &bits) ||
        !eng_u32(player + PL_UNLOCK_BITS + 0x10, &nwords) || !eng_ptr(words) || !nwords || nwords > 4096 || bits > nwords * 32) return 0;
    *totalBits = bits;
    for (uint32_t i = 0; i < nwords; i++) { uint32_t w = 0; if (!eng_u32(words + i * 4, &w)) return 0; while (w) { *setBits += w & 1; w >>= 1; } }
    return 1;
}
int eng_player_unlock_all(uint32_t player, int *bitsOut)
{
    uint32_t words = 0, bits = 0, nwords = 0; *bitsOut = 0;
    if (!eng_u32(player + PL_UNLOCK_BITS + 4, &words) || !eng_u32(player + PL_UNLOCK_BITS + 0xc, &bits) ||
        !eng_u32(player + PL_UNLOCK_BITS + 0x10, &nwords) || !eng_ptr(words) || !nwords || nwords > 4096 || bits > nwords * 32) return 0;
    for (uint32_t i = 0; i < nwords; i++) {
        uint32_t w = 0xffffffffu;
        if (i == nwords - 1 && bits % 32) w = (1u << (bits % 32)) - 1;
        SIZE_T n = 0; if (!WriteProcessMemory(GetCurrentProcess(), (void *)(uintptr_t)(words + i * 4), &w, 4, &n) || n != 4) return 0;
    }
    *bitsOut = (int)bits; return 1;
}
typedef void (TC *FnSquad)(void *, uint32_t);
void eng_set_squad(uint32_t player, uint32_t squad) { CALL(FnSquad, VA_SET_SQUAD_ID)(P(player), squad); }
int eng_resource_scan(const uint32_t *objects, uint32_t *ids, int count)
{
    uint32_t res = eng_resources(), buckets = 0, nb = 0; int found = 0;
    if (!res || !eng_u32(res + 4, &buckets) || !eng_u32(res + 8, &nb) || !eng_ptr(buckets) || !nb || nb > (1u << 22)) return 0;
    for (uint32_t b = 0; b < nb; b++) {
        uint32_t node = 0; if (!eng_u32(buckets + b * 4, &node)) break;
        for (int guard = 0; node && eng_ptr(node) && guard < 64; guard++) {
            uint32_t kv[3]; if (!eng_read(node, kv, 12)) break;
            for (int i = 0; i < count; i++) if (!ids[i] && objects[i] == kv[1]) { ids[i] = kv[0]; found++; }
            node = kv[2];
        }
    }
    return found;
}
uint32_t eng_resource_id_any(uint32_t object)
{
    uint32_t id = eng_resource_to_id(object);
    if (id && eng_resource_by_id(id) == object) return id;
    id = 0; eng_resource_scan(&object, &id, 1); return id;
}
int eng_silence_spawn_assert(void)
{
    static const unsigned char site[20] = {0x80,0xb8,0x84,0x0d,0x00,0x00,0x00,0x0f,0x84,0x8e,0x00,0x00,0x00,0x80,0x3d,0x8b,0xf4,0x9c,0x01,0x00};
    unsigned char b[20]; uint8_t one = 1; SIZE_T n = 0;
    if (!eng_read(RB(VA_SPAWN_AI_ASSERT), b, 20) || memcmp(b, site, 20)) return 0;
    return WriteProcessMemory(GetCurrentProcess(), (void *)(uintptr_t)RB(VA_SPAWN_AI_IGNORE), &one, 1, &n) && n == 1;
}
/* 1.5: BC2 deletes an AI player when its soldier dies, so every bot life was a new player - and every new player with a
   recycled player id of the other team showed as "<bot> switched teams" on the clients, all game long. The jump over
   leaveGame + deletePlayer in onKilled (je -> jmp) keeps the dead bot's player, as the game does for humans; Phoenix
   redeploys it. Restored when bots stop. */
typedef void (TC *FnSetWeapon)(void *, int);
int eng_soldier_weapon(uint32_t soldier)
{
    uint32_t vt = 0, sw = 0; int32_t idx = -1;
    if (!eng_ptr(soldier) || !eng_u32(soldier, &vt) || vt != RB(VA_SOLDIER_VT) || !eng_u32(soldier + 0x2d4, &sw) || !eng_ptr(sw) || !eng_read(sw + 0x14c, &idx, 4)) return -1;
    return idx >= 0 && idx < 16 ? idx : -1;
}
int eng_soldier_set_weapon(uint32_t soldier, int index)
{
    uint32_t vt = 0, b = 0, e = 0, w = 0;
    if (!eng_ptr(soldier) || !eng_u32(soldier, &vt) || vt != RB(VA_SOLDIER_VT)) return 0;
    if (!eng_u32(soldier + 0x2d8, &b) || !eng_u32(soldier + 0x2dc, &e) || e < b || index < 0 || (uint32_t)index >= (e - b) / 4) return 0;
    if (!eng_u32(b + (uint32_t)index * 4, &w) || !w) return 0;   /* (the game asserts on an empty slot) */
    CALL(FnSetWeapon, VA_SET_WEAPON_INDEX)(P(soldier), index);
    return eng_soldier_weapon(soldier) == index;
}
typedef uint8_t (TC *FnInteract)(void *, uint32_t);
static int g_interactWhy;
static int code_write(uint32_t at, const unsigned char *b, unsigned n)
{
    DWORD old = 0; void *p = (void *)(uintptr_t)at;
    if (!VirtualProtect(p, n, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memcpy(p, b, n); VirtualProtect(p, n, old, &old); FlushInstructionCache(GetCurrentProcess(), p, n); return 1;
}
int eng_interact(uint32_t interaction, uint32_t soldier)
{
    uint32_t vt = 0, svt = 0;
    if (!eng_ptr(interaction) || !eng_u32(interaction, &vt) || vt != RB(VA_INTERACTION_VT)) return -1;
    if (!eng_ptr(soldier) || !eng_u32(soldier, &svt) || svt != RB(VA_SOLDIER_VT)) return -1;
    uint8_t f91 = 0; { SIZE_T n = 0; WriteProcessMemory(GetCurrentProcess(), (void *)(uintptr_t)(interaction + 0x91), &f91, 1, &n); }
    if (CALL(FnInteract, VA_INTERACT)(P(interaction), soldier)) { g_interactWhy = 0; return 1; }
    /* 1.6.3: refused (Arica Harbor: every bot arm refused, "on, 0 uses, no delay"). Past the use/delay checks interact()
       refuses on (a) soldier+0xC0 vfunc 0x28 (0x7D994F), (b) a blocking-area test 0x6131D0 (0x7D998A) or (c) the arm
       animation not starting for this soldier (0x7D99A4, sets +0x91). Those are about a human's body and HUD; the bot is
       at the M-COM holding Interact for the hold time, so retry once with the three refusals jumped over. */
    eng_read(interaction + 0x91, &f91, 1); g_interactWhy = f91 ? 3 : 1;
    static const struct { uint32_t va; unsigned char orig[2], patch[2]; } site[3] = {
        {0x7d994f, {0x75, 0x60}, {0x90, 0x90}}, {0x7d998a, {0x75, 0x25}, {0x90, 0x90}}, {0x7d9994, {0x74, 0x33}, {0xeb, 0x33}}};
    for (int i = 0; i < 3; i++) { unsigned char b[2]; if (!eng_read(RB(site[i].va), b, 2) || memcmp(b, site[i].orig, 2)) return 0; }
    for (int i = 0; i < 3; i++) code_write(RB(site[i].va), site[i].patch, 2);
    int ok = CALL(FnInteract, VA_INTERACT)(P(interaction), soldier) ? 1 : 0;
    for (int i = 0; i < 3; i++) code_write(RB(site[i].va), site[i].orig, 2);
    if (ok) { g_interactWhy += 10; return 2; }
    return 0;
}
int eng_interact_why(void) { return g_interactWhy; }
int eng_keep_ai_players(int on)
{
    static const unsigned char site[9] = {0x80,0xbf,0x84,0x0d,0x00,0x00,0x00,0x74,0x15};   /* cmp byte [edi+0xd84],0 ; je +0x15 */
    unsigned char b[9];
    uint32_t at = RB(VA_ONKILLED_AI_JE) - 7;
    if (!eng_read(at, b, 9) || memcmp(b, site, 7) || (b[7] != 0x74 && b[7] != 0xeb) || b[8] != 0x15) return 0;
    unsigned char op = on ? 0xeb : 0x74;
    if (b[7] == op) return 1;
    DWORD old = 0;
    void *p = (void *)(uintptr_t)RB(VA_ONKILLED_AI_JE);
    if (!VirtualProtect(p, 1, PAGE_EXECUTE_READWRITE, &old)) return 0;
    *(volatile unsigned char *)p = op;
    VirtualProtect(p, 1, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 1);
    return 1;
}
int eng_soldier_move_step(uint32_t soldier, uint32_t player, void *input, float dt, int physicsStep)
{
    uint32_t vt = 0, slot = 0, begin = 0, end = 0, phys = 0, index = 0xffffffffu; uint8_t single = 0;
    if (!eng_ptr(soldier) || !input || !(dt > 0) || dt > 0.25f) return 0;
    if (!eng_u32(soldier, &vt) || vt != RB(VA_SOLDIER_VT)) return 0;
    if (!eng_u32(vt + 0x11c, &slot) || slot != RB(VA_SOLDIER_MOVE_INPUT)) return 0;
    if (!eng_u32(soldier + SOL_ENTRIES, &begin) || !eng_u32(soldier + SOL_ENTRIES + 4, &end) || end < begin || end - begin > 64) return 0;
    for (uint32_t i = 0; i < (end - begin) / 4; i++) {
        uint32_t entry = 0, who = 0;
        if (eng_u32(begin + i * 4, &entry) && eng_ptr(entry) && eng_u32(entry + 0xe8, &who) && who == player) { index = i; break; }
    }
    if (index == 0xffffffffu) return 0;
    if (!eng_u32(soldier + SOL_PHYSICS, &phys) || !eng_ptr(phys) || !eng_read(phys + 0xde, &single, 1)) return 0;
    CALL(FnMoveInput, VA_SOLDIER_MOVE_INPUT)(P(soldier), index, input);
    /* The server's own per-input path (soldier vt+0x110) steps the character every time, whatever +0xDE
       says; in 0.6.2 Phoenix trusted +0xDE (0 here) and nothing ever stepped the bots. */
    (void)single;
    if (physicsStep) CALL(FnStep, VA_CHARACTER_STEP)(P(phys), dt);
    return physicsStep ? 2 : 1;
}
void eng_leave_game(uint32_t p) { CALL(FnNoArg, VA_LEAVE_GAME)(P(p)); }
/* 1.6.6: what the console's suicide does (ServerConnection::onReqSuicide 0x7470D0 -> player->controllable -> 0x7D7FB0):
   the soldier deals itself 100 damage through its own damage path and dies the normal way (onKilled). 1.6.5 and older
   "redeployed" a pinned bot with leaveGame alone, which only DETACHES a living soldier: it stayed in the world with no
   player - standing still, no AI, hit reactions but no damage (the ghosts), drifting off if it was detached mid-hop. */
int eng_soldier_kill(uint32_t soldier)
{
    static const unsigned char fp[16] = {0x55,0x8b,0xec,0x83,0xe4,0xf0,0x81,0xec,0xcc,0x00,0x00,0x00,0xd9,0x05,0xec,0xdd};
    static int ok = -1;
    if (ok < 0) { unsigned char b[16]; ok = eng_read(RB(VA_SOLDIER_SUICIDE), b, 16) && !memcmp(b, fp, 16); }
    uint32_t vt = 0; float hp = 0;
    if (!ok || !eng_ptr(soldier) || !eng_u32(soldier, &vt) || vt != RB(VA_SOLDIER_VT) || !eng_read(soldier + 0x34, &hp, 4) || !(hp > 0)) return 0;
    CALL(FnNoArg, VA_SOLDIER_SUICIDE)(P(soldier));
    return 1;
}
void eng_delete_player(uint32_t m, uint32_t p) { CALL(FnOneU, VA_DELETE_PLAYER)(P(m), p); }
uint32_t eng_spawn_entity_id(uint32_t e) { return CALL(FnRetU, VA_SPAWN_ENTITY_ID)(P(e)); }

static int matrix_ok(const float *m)
{
    for (int i = 0; i < 16; i++) if (!isfinite(m[i])) return 0;
    for (int r = 0; r < 3; r++) { float n = m[r*4]*m[r*4] + m[r*4+1]*m[r*4+1] + m[r*4+2]*m[r*4+2]; if (n < 0.5f || n > 1.5f) return 0; }
    return fabsf(m[12]) < 30000 && fabsf(m[13]) < 30000 && fabsf(m[14]) < 30000;
}
int eng_entity_position(uint32_t e, float out[3])
{
    uint16_t flags = 0; uint32_t root = 0, t; float m[16];
    if (!eng_read(e + 0x26, &flags, 2) || !eng_u32(e + 0x28, &root) || !eng_ptr(root)) return 0;
    t = root;
    if (flags & 0x80) { uint8_t idx = 0; if (!eng_read(root + 0xe, &idx, 1)) return 0; t = root + 0x10 + (uint32_t)idx * 0x50; }
    if (!eng_read(t, m, sizeof m) || !matrix_ok(m)) return 0;
    out[0] = m[12]; out[1] = m[13]; out[2] = m[14];
    return 1;
}

int eng_entity_matrix(uint32_t e, float m[16])
{
    uint16_t flags = 0; uint32_t root = 0, t;
    if (!eng_read(e + 0x26, &flags, 2) || !eng_u32(e + 0x28, &root) || !eng_ptr(root)) return 0;
    t = root;
    if (flags & 0x80) { uint8_t idx = 0; if (!eng_read(root + 0xe, &idx, 1)) return 0; t = root + 0x10 + (uint32_t)idx * 0x50; }
    return eng_read(t, m, 64) && matrix_ok(m);
}
static int is_vehicle(uint32_t v) { uint32_t vt = 0; return eng_ptr(v) && eng_u32(v, &vt) && vt == RB(VA_VEHICLE_VT); }
int eng_vehicle_seats(uint32_t v)
{
    uint32_t b = 0, e = 0;
    if (!is_vehicle(v) || !eng_u32(v + 0x90, &b) || !eng_u32(v + 0x94, &e) || e < b || e - b > 64) return 0;
    return (int)((e - b) / 4);
}
uint32_t eng_vehicle_occupant(uint32_t v, int seat)
{
    uint32_t b = 0, entry = 0, p = 0;
    if (seat < 0 || seat >= eng_vehicle_seats(v) || !eng_u32(v + 0x90, &b) || !eng_u32(b + seat * 4, &entry) || !eng_ptr(entry)) return 0;
    return eng_u32(entry + 0xe8, &p) && eng_ptr(p) ? p : 0;
}
typedef uint8_t (TC *FnAllowed)(void *, void *, int);
int eng_vehicle_seat_allowed(uint32_t v, uint32_t player, int seat)
{
    uint32_t vt = 0, fn = 0;
    if (seat < 0 || seat >= eng_vehicle_seats(v) || !eng_u32(v, &vt) || !eng_u32(vt + 0x104, &fn) || fn < g_engine.base) return 0;
    return ((FnAllowed)(uintptr_t)fn)(P(v), P(player), seat) != 0;
}
typedef void (TC *FnEnter)(void *, void *, int);
int eng_enter_vehicle(uint32_t player, uint32_t v, int seat)
{
    if (!eng_vehicle_seat_allowed(v, player, seat)) return 0;
    CALL(FnEnter, VA_ENTER_VEHICLE)(P(player), P(v), seat);
    return eng_player_controlled(player) == v;
}
typedef void (TC *FnExit)(void *, uint8_t);
int eng_exit_vehicle(uint32_t player)
{
    uint32_t c = eng_player_controlled(player);
    if (!is_vehicle(c)) return 0;
    CALL(FnExit, VA_EXIT_VEHICLE)(P(player), 0);
    return eng_player_controlled(player) != c;
}
uint32_t eng_player_controlled(uint32_t player) { uint32_t c = 0; return eng_u32(player + PL_CONTROLLED, &c) && eng_ptr(c) ? c : 0; }

/* ---- rays ---- */
typedef struct __attribute__((aligned(16))) RayHit {
    float pos[4], normal[4];
    uint32_t a20, a24, a28; float f2c; uint32_t entity, pad[3];
} RayHit;
static uint32_t g_rayMasks[4] = {0x38, 0x7e}; static int g_rayMaskCount = 2;
static unsigned g_rays;
void eng_ray_set_mask(uint32_t mask) { g_rayMasks[0] = mask; g_rayMaskCount = 1; }
void eng_ray_set_masks(const uint32_t *m, int n) { if (n < 1) return; if (n > 4) n = 4; for (int i = 0; i < n; i++) g_rayMasks[i] = m[i]; g_rayMaskCount = n; }
uint32_t eng_ray_mask(void) { return g_rayMasks[0]; }
uint32_t eng_kind_mask(int kind) { return kind == 2 ? 0x38u : 0x7eu; }
unsigned eng_ray_count(void) { return g_rays; }
uint32_t eng_game_tick(void)
{
    uint32_t ctx = eng_context(), timer = 0, tick = 0;
    return ctx && eng_u32(ctx + 0xc, &timer) && eng_ptr(timer) && eng_u32(timer, &tick) ? tick : 0;
}

int eng_ray_masked(uint32_t mask, const float from[3], const float to[3], float hit[3], float normal[3])
{
    static const char ident[] = "PhoenixBotRay";
    uint32_t ctx = eng_context(), physics = 0;
    if (!ctx || !eng_u32(ctx + 4, &physics) || !eng_ptr(physics)) return 0;
    float a[4] __attribute__((aligned(16))) = {from[0], from[1], from[2], 0};
    float b[4] __attribute__((aligned(16))) = {to[0], to[1], to[2], 0};
    if (!isfinite(a[0]+a[1]+a[2]+b[0]+b[1]+b[2])) return 0;
    RayHit hits[8];
    memset(hits, 0, sizeof hits);
    for (int i = 0; i < 8; i++) { hits[i].a24 = 0xffffffffu; hits[i].a28 = 0xffffffffu; }
    g_rays++;
    int n = CALL(FnRay, VA_RAY_QUERY)(P(physics), ident, 1, a, b, hits, 8, 0x20000000u, mask, NULL);
    if (n <= 0) return 0;
    if (n > 8) n = 8;
    int best = -1; float bestD = 1e30f;
    for (int i = 0; i < n; i++) {
        float *p = hits[i].pos;
        if (!isfinite(p[0]+p[1]+p[2])) continue;
        float dx = p[0]-a[0], dy = p[1]-a[1], dz = p[2]-a[2], d = dx*dx+dy*dy+dz*dz;
        if (d < bestD) { bestD = d; best = i; }
    }
    if (best < 0) return 0;
    if (hit) { hit[0] = hits[best].pos[0]; hit[1] = hits[best].pos[1]; hit[2] = hits[best].pos[2]; }
    if (normal) { normal[0] = hits[best].normal[0]; normal[1] = hits[best].normal[1]; normal[2] = hits[best].normal[2]; }
    return 1;
}

/* Every configured collision filter is tried and the nearest hit wins: on BC2 maps filter 0x38 sees
   buildings, decks and props, filter 0x7E mostly the terrain. */
int eng_ray(const float from[3], const float to[3], float hit[3], float normal[3])
{
    float bh[3], bn[3], h[3], n[3]; float bestD = 1e30f; int any = 0;
    for (int i = 0; i < g_rayMaskCount; i++) {
        if (!eng_ray_masked(g_rayMasks[i], from, to, h, n)) continue;
        float dx = h[0]-from[0], dy = h[1]-from[1], dz = h[2]-from[2], d = dx*dx+dy*dy+dz*dz;
        if (d < bestD) { bestD = d; memcpy(bh, h, 12); memcpy(bn, n, 12); any = 1; }
    }
    if (!any) return 0;
    if (hit) memcpy(hit, bh, 12);
    if (normal) memcpy(normal, bn, 12);
    return 1;
}
