/* Project Phoenix BC2 0.9 - learning.
   Per map, bots remember what happened and adapt, round after round:
   - stuck spots: where bots get stuck, heading which way, and which action got them through (jump, knife, crouch,
     side-step, back off). The next bot that walks into the spot does the winning action at once; a spot nobody ever
     gets through is cut out of the navigation grid for good;
   - danger: where bots die (10 m cells). Routes cost more through deadly ground, so bots learn other ways in;
     flanking routes pick the safer side;
   - overwatch spots: sniper / window positions score their kills and deaths; the good ones are used more.
   Everything is kept in navcache\<map>.learn, a plain text file that can be copied to another server (or merged by
   hand). Old knowledge fades a little each time a map is loaded, so the bots keep adapting. */
#ifdef PHX_HARNESS
#include "harness_win.h"
#else
#include "win_common.h"
#endif
#include "rt.h"
#include "nav.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <sys/stat.h>

void rt_nav_cache_path(const char *key, char *out, unsigned size);
const char *navg_key(void);
NavGrid *navg_grid(void);

#define MAX_STUCK 512
#define MAX_SPOTS 256
#define HEAT_CELL 10.0f
#define HEAT_W 256          /* 2.56 km square around the grid origin */

typedef struct StuckSpot { float pos[3], heading; float seen, fix[LEARN_ACTIONS], bad; int cut; } StuckSpot;
typedef struct SpotScore { float pos[3]; float kills, deaths, uses; } SpotScore;

static StuckSpot g_stuck[MAX_STUCK]; static int g_nStuck;
static SpotScore g_spots[MAX_SPOTS]; static int g_nSpots;
static float g_death[HEAT_W * HEAT_W], g_kill[HEAT_W * HEAT_W];
static float g_heatX0, g_heatZ0; static int g_heatValid;
static char g_learnKey[64]; static int g_loaded, g_dirty; static double g_saveAt, g_dangerAt;
static unsigned char *g_dangerMask; static const NavGrid *g_dangerFor;
static unsigned g_learnLevel;
static const char *kActionName[LEARN_ACTIONS] = {"jump", "knife", "crouch", "detour", "back"};
/* 1.5: routes humans take - every nav cell a human walks through (on foot) or drives through (driver's seat), counted.
   Infantry routes prefer the walked cells (the planner's road classes: x0.6 / x0.85); vehicle routes may use every cell a
   human drove, at the cheapest drive cost - including ground the drive map had ruled out. Saved with the rest. */
static unsigned char *g_walk, *g_drive, *g_trailRoad; static const NavGrid *g_trailFor; static int g_trailW, g_trailH;
static unsigned g_walkCells, g_driveCells;
/* 1.6.4: routes drawn in the learning map (navcache\<map>.routes) - vehicles prefer them like a human's drive; the
   file is the user's, never written here, and is read again whenever it changes (draw while you play) */
static unsigned char *g_route, *g_driveAll; static int g_routeCells, g_routeLines; static long long g_routeStamp; static double g_routeCheckAt;
static float g_water[64][3]; static int g_nWater;
static float g_vstuck[256][3]; static int g_nVstuck;   /* 1.6.5: where vehicles got stuck twice - cut from the drive map every round */
void navg_vehicle_stuck_spots(const float (*pos)[3], int n);   /* 1.5.2: where bots had to swim (seeds of the water surface) */
void navg_learn_drive(const unsigned char *drive, int w, int h);

static float wrapd(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }
static int heat_index(float x, float z)
{
    if (!g_heatValid) return -1;
    int cx = (int)floorf((x - g_heatX0) / HEAT_CELL), cz = (int)floorf((z - g_heatZ0) / HEAT_CELL);
    if (cx < 0 || cz < 0 || cx >= HEAT_W || cz >= HEAT_W) return -1;
    return cz * HEAT_W + cx;
}

static void learn_path(char *out, unsigned size)
{
    rt_nav_cache_path(g_learnKey, out, size);
    char *dot = strrchr(out, '.'); if (dot) snprintf(dot, size - (unsigned)(dot - out), ".learn");
}

static int trail_alloc(void)
{
    NavGrid *g = navg_grid(); if (!g) return 0;
    if (g_trailFor == g && g_walk) return 1;
    navg_learn_drive(NULL, 0, 0);
    free(g_walk); free(g_drive); free(g_trailRoad); g_walk = g_drive = g_trailRoad = NULL;
    free(g_route); free(g_driveAll); g_route = g_driveAll = NULL; g_routeStamp = -1;
    size_t n = (size_t)g->w * g->h;
    g_route = calloc(n, 1); g_driveAll = calloc(n, 1);
    g_walk = calloc(n, 1); g_drive = calloc(n, 1); g_trailRoad = calloc(n * NAV_LAYERS, 1);
    if (!g_walk || !g_drive || !g_trailRoad) { free(g_walk); free(g_drive); free(g_trailRoad); g_walk = g_drive = g_trailRoad = NULL; return 0; }
    g_trailFor = g; g_trailW = g->w; g_trailH = g->h; return 1;
}
void learn_reset(void)
{
    learn_save_now();
    nav_set_road_mask(NULL, NULL); navg_learn_drive(NULL, 0, 0);
    free(g_route); free(g_driveAll); g_route = g_driveAll = NULL; g_routeStamp = -1; g_routeCells = g_routeLines = 0;
    free(g_walk); free(g_drive); free(g_trailRoad); g_walk = g_drive = g_trailRoad = NULL; g_trailFor = NULL; g_walkCells = g_driveCells = 0; g_nWater = 0; g_nVstuck = 0;
    g_nStuck = g_nSpots = 0; g_loaded = 0; g_dirty = 0; g_learnKey[0] = 0; g_heatValid = 0;
    memset(g_death, 0, sizeof g_death); memset(g_kill, 0, sizeof g_kill);
    if (g_dangerMask) { free(g_dangerMask); g_dangerMask = NULL; }
    g_dangerFor = NULL;
    nav_set_danger(NULL, NULL);
}

static void learn_load(void)
{
    char path[1200]; learn_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    int ns = 0, nd = 0, nk = 0, no = 0, nc = 0;
    if (f) {
        char line[512];
        while (fgets(line, sizeof line, f)) {
            if (line[0] == '#') continue;
            float a[12] = {0}; int cut = 0;
            if (!strncmp(line, "stuck ", 6) && g_nStuck < MAX_STUCK &&
                sscanf(line + 6, "%f %f %f %f %f %f %f %f %f %f %f %d", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6], &a[7], &a[8], &a[9], &a[10], &cut) >= 11) {
                StuckSpot *s = &g_stuck[g_nStuck++]; memset(s, 0, sizeof *s);
                s->pos[0] = a[0]; s->pos[1] = a[1]; s->pos[2] = a[2]; s->heading = a[3] * 3.14159265f / 180.0f;
                s->seen = a[4] * 0.85f; for (int k = 0; k < LEARN_ACTIONS; k++) s->fix[k] = a[5 + k] * 0.9f; s->bad = a[10] * 0.85f; s->cut = cut;
                ns++;
            } else if (!strncmp(line, "death ", 6) && sscanf(line + 6, "%f %f %f", &a[0], &a[1], &a[2]) == 3) {
                int i = heat_index(a[0], a[1]); if (i >= 0) { g_death[i] += a[2] * 0.7f; nd++; }
            } else if (!strncmp(line, "kill ", 5) && sscanf(line + 5, "%f %f %f", &a[0], &a[1], &a[2]) == 3) {
                int i = heat_index(a[0], a[1]); if (i >= 0) { g_kill[i] += a[2] * 0.7f; nk++; }
            } else if ((!strncmp(line, "walk ", 5) || !strncmp(line, "drive ", 6)) && trail_alloc() && sscanf(line + (line[0] == 'w' ? 5 : 6), "%f %f %f", &a[0], &a[1], &a[2]) == 3) {
                int c = nav_cell_index(navg_grid(), a[0], a[1]);
                if (c >= 0) { unsigned char *t = line[0] == 'w' ? g_walk : g_drive; float v = a[2] * 0.85f + 0.5f; t[c] = (unsigned char)(v > 250 ? 250 : v); if (line[0] == 'w') g_walkCells++; else g_driveCells++; }
            } else if (!strncmp(line, "vstuck ", 7) && g_nVstuck < 256 && sscanf(line + 7, "%f %f %f", &a[0], &a[1], &a[2]) == 3) {
                memcpy(g_vstuck[g_nVstuck++], a, 12);
            } else if (!strncmp(line, "water ", 6) && g_nWater < 64 && sscanf(line + 6, "%f %f %f", &a[0], &a[1], &a[2]) == 3) {
                memcpy(g_water[g_nWater++], a, 12);
            } else if (!strncmp(line, "spot ", 5) && g_nSpots < MAX_SPOTS && sscanf(line + 5, "%f %f %f %f %f %f", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]) == 6) {
                SpotScore *s = &g_spots[g_nSpots++]; s->pos[0] = a[0]; s->pos[1] = a[1]; s->pos[2] = a[2];
                s->kills = a[3] * 0.8f; s->deaths = a[4] * 0.8f; s->uses = a[5] * 0.8f; no++;
            }
        }
        fclose(f);
    }
    navg_vehicle_stuck_spots((const float (*)[3])g_vstuck, g_nVstuck);
    if (g_nVstuck) rt_log("learn: %d spot(s) where vehicles got stuck before - cut from the drive map", g_nVstuck);
    { int wn = 0; for (int i = 0; i < g_nWater; i++) wn += navg_mark_water(g_water[i], 50);
      if (g_nWater) rt_log("learn: %d water spot(s) - %d grid node(s) marked as water, routes keep out of it", g_nWater, wn); }
    /* spots nobody ever got through: cut those links again */
    NavGrid *g = navg_grid();
    for (int i = 0; i < g_nStuck && g; i++) if (g_stuck[i].cut) {
        StuckSpot *s = &g_stuck[i];
        nc += nav_cut_towards(g, s->pos[0], s->pos[1], s->pos[2], s->pos[0] + sinf(s->heading) * 3, s->pos[2] + cosf(s->heading) * 3) > 0;
    }
    rt_log("learn: %s - %d stuck spot(s), %d death cell(s), %d kill cell(s), %d overwatch score(s), %u cell(s) humans walked, %u they drove loaded%s; %d impassable spot(s) cut",
           f ? path : "no learned file yet", ns, nd, nk, no, g_walkCells, g_driveCells, f ? " (older knowledge faded a little)" : "", nc);
}

void learn_save_now(void)
{
    if (!g_loaded || !g_dirty || !g_learnKey[0]) return;
    char path[1200]; learn_path(path, sizeof path);
    char tmp[1210]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) { rt_log("learn: could not write %s", tmp); return; }
    fprintf(f, "# Project Phoenix - what bots learned on %s%s%s (%s). Plain text; copy it next to another server's\n",
            g_world.levelName[0] ? g_world.levelName : "this map", g_world.mapName[0] ? " - " : "", g_world.mapName, g_learnKey);
    fprintf(f, "# navcache\\%s.nav to share it. stuck x y z heading seen jump knife crouch detour back bad cut | death/kill x z count | spot x y z kills deaths uses\n", g_learnKey);
    for (int i = 0; i < g_nStuck; i++) {
        StuckSpot *s = &g_stuck[i];
        if (s->seen < 0.3f && !s->cut) continue;
        fprintf(f, "stuck %.1f %.1f %.1f %.0f %.1f", s->pos[0], s->pos[1], s->pos[2], s->heading * 180.0f / 3.14159265f, s->seen);
        for (int k = 0; k < LEARN_ACTIONS; k++) fprintf(f, " %.1f", s->fix[k]);
        fprintf(f, " %.1f %d\n", s->bad, s->cut);
    }
    for (int i = 0; i < HEAT_W * HEAT_W; i++) {
        float x = g_heatX0 + (i % HEAT_W + 0.5f) * HEAT_CELL, z = g_heatZ0 + (i / HEAT_W + 0.5f) * HEAT_CELL;
        if (g_death[i] >= 0.3f) fprintf(f, "death %.0f %.0f %.1f\n", x, z, g_death[i]);
        if (g_kill[i] >= 0.3f) fprintf(f, "kill %.0f %.0f %.1f\n", x, z, g_kill[i]);
    }
    if (g_walk && g_trailFor == navg_grid() && g_trailFor) {
        const NavGrid *g = g_trailFor;
        for (int c = 0; c < g->w * g->h; c++) {
            float x = g->x0 + (c % g->w) * g->cell, z = g->z0 + (c / g->w) * g->cell;
            if (g_walk[c]) fprintf(f, "walk %.1f %.1f %d\n", x, z, g_walk[c]);
            if (g_drive[c]) fprintf(f, "drive %.1f %.1f %d\n", x, z, g_drive[c]);
        }
    }
    for (int i = 0; i < g_nVstuck; i++) fprintf(f, "vstuck %.1f %.1f %.1f\n", g_vstuck[i][0], g_vstuck[i][1], g_vstuck[i][2]);
    for (int i = 0; i < g_nWater; i++) fprintf(f, "water %.1f %.1f %.1f\n", g_water[i][0], g_water[i][1], g_water[i][2]);
    for (int i = 0; i < g_nSpots; i++) if (g_spots[i].uses >= 0.3f)
        fprintf(f, "spot %.1f %.1f %.1f %.1f %.1f %.1f\n", g_spots[i].pos[0], g_spots[i].pos[1], g_spots[i].pos[2], g_spots[i].kills, g_spots[i].deaths, g_spots[i].uses);
    fclose(f);
    remove(path);
    if (rename(tmp, path) != 0) { rt_log("learn: could not replace %s", path); return; }
    g_dirty = 0;
}

/* the danger mask the planner reads: deaths (minus where bots also kill) spread over the nodes of each cell */
static void build_danger(void)
{
    NavGrid *g = navg_grid(); if (!g) return;
    size_t n = (size_t)g->w * g->h * NAV_LAYERS;
    if (!g_dangerMask || g_dangerFor != g) { free(g_dangerMask); g_dangerMask = calloc(n, 1); g_dangerFor = g; }
    if (!g_dangerMask) return;
    int hot = 0;
    for (int cz = 0; cz < g->h; cz++) for (int cx = 0; cx < g->w; cx++) {
        float x = g->x0 + cx * g->cell, z = g->z0 + cz * g->cell; int i = heat_index(x, z);
        float d = i >= 0 ? g_death[i] - 0.5f * g_kill[i] : 0;
        unsigned char v = d <= 0.5f ? 0 : d >= 12 ? 255 : (unsigned char)(d * 21.0f);
        if (v) hot++;
        for (int l = 0; l < NAV_LAYERS; l++) g_dangerMask[((size_t)cz * g->w + cx) * NAV_LAYERS + l] = v;
    }
    nav_set_danger(g, g_dangerMask);
    if (g_cfg.learnHumans && g_walk && g_trailFor == g) {   /* the routes humans take */
        int nw = 0;
        for (int c = 0; c < g->w * g->h; c++) {
            unsigned char cls = g_walk[c] >= 3 ? 1 : g_walk[c] >= 1 ? 2 : 0; if (cls) nw++;
            for (int l = 0; l < NAV_LAYERS; l++) g_trailRoad[(size_t)c * NAV_LAYERS + l] = cls;
        }
        nav_set_road_mask(g, g_trailRoad);
        const unsigned char *drv = g_drive;
        if (g_driveAll && g_route) { for (int c = 0; c < g->w * g->h; c++) g_driveAll[c] = g_route[c] > g_drive[c] ? g_route[c] : g_drive[c]; drv = g_driveAll; }
        navg_learn_drive(drv, g->w, g->h);
        static int tl; if ((tl++ < 3 || g_cfg.verbose) && (nw || g_driveCells)) rt_log("learn: routes from human players - %d cell(s) walked preferred, drive map opened along %u cell(s) they drove", nw, g_driveCells);
    }
    static int logs; if (logs++ < 3 || g_cfg.verbose) rt_log("learn: danger map updated - %d grid cell(s) where bots keep dying are avoided", hot);
}

static void routes_path(char *out, unsigned size)
{
    rt_nav_cache_path(g_learnKey, out, size);
    char *dot = strrchr(out, '.'); if (dot) snprintf(dot, size - (unsigned)(dot - out), ".routes");
}
static void route_mark(const NavGrid *g, float x, float z)
{
    int c = nav_cell_index(g, x, z); if (c < 0) return;
    int cx = c % g->w, cz = c / g->w;
    for (int oz = -1; oz <= 1; oz++) for (int ox = -1; ox <= 1; ox++) {
        int nx = cx + ox, nz = cz + oz; if (nx < 0 || nz < 0 || nx >= g->w || nz >= g->h) continue;
        unsigned char *v = &g_route[nz * g->w + nx]; if (!*v) g_routeCells++; *v = 250;
    }
}
/* returns 1 when the file changed (and was read again) */
static int routes_load(void)
{
    NavGrid *g = navg_grid(); if (!g || !trail_alloc() || !g_route) return 0;
    char path[1200]; routes_path(path, sizeof path);
    struct stat st; long long stamp = stat(path, &st) == 0 ? (long long)st.st_mtime * 1000 + (long long)st.st_size : 0;
    if (stamp == g_routeStamp) return 0;
    g_routeStamp = stamp;
    memset(g_route, 0, (size_t)g->w * g->h); g_routeCells = 0; g_routeLines = 0;
    FILE *f = stamp ? fopen(path, "r") : NULL;
    if (f) {
        char line[256]; float px = 0, pz = 0; int have = 0;
        while (fgets(line, sizeof line, f)) {
            float x, z;
            if (!strncmp(line, "route", 5)) { have = 0; g_routeLines++; continue; }
            if (sscanf(line, "%f %f", &x, &z) != 2) continue;
            if (have) { float d = hypotf(x - px, z - pz); int n = (int)(d / 1.0f) + 1;
                for (int k = 0; k <= n; k++) route_mark(g, px + (x - px) * k / n, pz + (z - pz) * k / n); }
            else route_mark(g, x, z);
            px = x; pz = z; have = 1;
        }
        fclose(f);
    }
    rt_log("learn: %d drawn route(s) for vehicles (%d cell(s)) from %s", g_routeLines, g_routeCells, path);
    return 1;
}

void learn_tick(double now)
{
    if (g_learnLevel != g_world.levelSerial) { g_learnLevel = g_world.levelSerial; learn_reset(); }
    if (!g_cfg.learn || !navg_ready()) return;
    if (!g_loaded) {
        const char *k = navg_key(); NavGrid *g = navg_grid();
        if (!k || !k[0] || !g) return;
        snprintf(g_learnKey, sizeof g_learnKey, "%s", k);
        g_heatX0 = g->x0 - 300; g_heatZ0 = g->z0 - 300; g_heatValid = 1;
        g_loaded = 1; learn_load(); routes_load(); build_danger(); g_routeCheckAt = now + 10;
        g_saveAt = now + 60; g_dangerAt = now + 30;
        return;
    }
    if (now > g_routeCheckAt) { g_routeCheckAt = now + 10; if (routes_load()) build_danger(); }
    if (now > g_dangerAt) { g_dangerAt = now + 30; build_danger(); }
    if (now > g_saveAt) { g_saveAt = now + 60; learn_save_now(); }
}

/* ---- stuck spots ---- */
int learn_stuck_event(const float *pos, float heading)
{
    if (!g_loaded) return -1;
    int best = -1; float bd = 2.5f;
    for (int i = 0; i < g_nStuck; i++) {
        float d = hypotf(g_stuck[i].pos[0] - pos[0], g_stuck[i].pos[2] - pos[2]);
        if (d < bd && fabsf(g_stuck[i].pos[1] - pos[1]) < 2.0f && fabsf(wrapd(g_stuck[i].heading - heading)) < 0.8f) { bd = d; best = i; }
    }
    if (best < 0) {
        if (g_nStuck >= MAX_STUCK) {   /* forget the least seen */
            int w = 0; for (int i = 1; i < g_nStuck; i++) if (g_stuck[i].seen < g_stuck[w].seen) w = i;
            best = w;
        } else best = g_nStuck++;
        memset(&g_stuck[best], 0, sizeof g_stuck[best]);
        memcpy(g_stuck[best].pos, pos, 12); g_stuck[best].heading = heading;
    }
    g_stuck[best].seen += 1; g_dirty = 1;
    return best;
}

void learn_stuck_result(int spot, int action, int success)
{
    if (spot < 0 || spot >= g_nStuck) return;
    StuckSpot *s = &g_stuck[spot];
    if (success && action >= 0 && action < LEARN_ACTIONS) {
        s->fix[action] += 1;
        static int logs; if (logs++ < 8 || g_cfg.verbose)
            rt_log("learn: at (%.0f, %.0f, %.0f) '%s' got a bot through (%.0f time(s) now)", s->pos[0], s->pos[1], s->pos[2], kActionName[action], s->fix[action]);
    } else if (!success) {
        s->bad += 1;
        float fixes = 0; for (int k = 0; k < LEARN_ACTIONS; k++) fixes += s->fix[k];
        if (!s->cut && s->bad >= 4 && fixes < 1) {   /* nobody gets through here: cut it out of the grid */
            NavGrid *g = navg_grid();
            if (g && nav_cut_towards(g, s->pos[0], s->pos[1], s->pos[2], s->pos[0] + sinf(s->heading) * 3, s->pos[2] + cosf(s->heading) * 3)) {
                s->cut = 1;
                rt_log("learn: bots never get through at (%.0f, %.0f, %.0f) - that way is now cut from the map for good", s->pos[0], s->pos[1], s->pos[2]);
            }
        }
    }
    g_dirty = 1;
}

/* a learned fix for a spot just ahead: returns the action (or -1) */
int learn_hint(const float *pos, float heading)
{
    if (!g_loaded) return -1;
    for (int i = 0; i < g_nStuck; i++) {
        StuckSpot *s = &g_stuck[i];
        float dx = s->pos[0] - pos[0], dz = s->pos[2] - pos[2], d = hypotf(dx, dz);
        if (d > 3.0f || d < 0.3f || fabsf(s->pos[1] - pos[1]) > 2.0f || fabsf(wrapd(s->heading - heading)) > 0.9f) continue;
        if (fabsf(wrapd(atan2f(dx, dz) - heading)) > 1.0f) continue;   /* the spot must be ahead */
        int best = -1; float bv = 1.5f;
        for (int k = 0; k < LEARN_ACTIONS; k++) if (s->fix[k] > bv) { bv = s->fix[k]; best = k; }
        if (best >= 0) return best;
    }
    return -1;
}

/* ---- danger / kills ---- */
void learn_death(const float *pos) { int i = heat_index(pos[0], pos[2]); if (i >= 0 && g_loaded) { g_death[i] += 1; g_dirty = 1; } }
void learn_kill(const float *pos) { int i = heat_index(pos[0], pos[2]); if (i >= 0 && g_loaded) { g_kill[i] += 1; g_dirty = 1; } }
float learn_danger(const float *pos)
{
    int i = heat_index(pos[0], pos[2]);
    if (i < 0) return 0;
    float d = 0;   /* this cell and its neighbours */
    for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
        int j = i + dz * HEAT_W + dx; if (j < 0 || j >= HEAT_W * HEAT_W) continue;
        d += (g_death[j] - 0.5f * g_kill[j]) * (dx || dz ? 0.5f : 1.0f);
    }
    return d > 0 ? d : 0;
}

/* ---- overwatch spots ---- */
static SpotScore *spot_find(const float *p, int create)
{
    for (int i = 0; i < g_nSpots; i++) if (hypotf(g_spots[i].pos[0] - p[0], g_spots[i].pos[2] - p[2]) < 3 && fabsf(g_spots[i].pos[1] - p[1]) < 2) return &g_spots[i];
    if (!create || g_nSpots >= MAX_SPOTS) return NULL;
    SpotScore *s = &g_spots[g_nSpots++]; memset(s, 0, sizeof *s); memcpy(s->pos, p, 12); return s;
}
void learn_spot_result(const float *spot, int used, int kill, int death)
{
    if (!g_loaded) return;
    SpotScore *s = spot_find(spot, 1); if (!s) return;
    s->uses += used; s->kills += kill; s->deaths += death; g_dirty = 1;
}
float learn_spot_score(const float *spot)
{
    SpotScore *s = spot_find(spot, 0);
    if (!s) return 1.0f;
    return (s->kills + 1.0f) / (s->deaths + 1.0f);
}

/* called about twice a second: where the human players are (on foot or driving) */
void learn_humans(double now)
{
    if (!g_cfg.learnHumans || !g_loaded || !trail_alloc()) return;
    static uint32_t who[64]; static float last[64][3]; static double lastAt[64], reprobeAt[64];
    const NavGrid *g = g_trailFor;
    for (int i = 0; i < g_world.obs.count && i < 64; i++) {
        const BC2ObservedPlayer *p = &g_world.obs.players[i];
        if (p->isAI || !p->soldierValid || p->health <= 0) { who[i] = 0; continue; }
        /* 1.6: +0xC5C (obs 'entry') is the entry-component POINTER, not a seat number - 1.5 treated it as
           "seat 1 = driver", so a human in a vehicle never counted. In a vehicle: the controlled entity is the
           vehicle, and the human is its driver when seat 0 holds him; passengers / gunners teach nothing. */
        int driving = 0; float pos[3]; memcpy(pos, p->position, 12);
        if (p->entry) {
            uint32_t veh = eng_player_controlled(p->pointer);
            VehicleInfo vi; int known = 0;
            for (int k = 0; k < g_world.vehCount; k++) if (g_world.veh[k].entity == veh) { vi = g_world.veh[k]; known = 1; break; }   /* name + kind */
            if (!known || veh == p->controllable || eng_vehicle_occupant(veh, 0) != p->pointer || !world_vehicle_read(veh, &vi)) { who[i] = 0; continue; }
            if (vi.kind != VK_GROUND) { who[i] = 0; continue; }   /* boats / helis: their tracks are no road */
            memcpy(pos, vi.pos, 12); driving = 1;
            static int said; if (!said++) rt_log("learn: a human drives %s - his route feeds the bots' road map", vi.name[0] ? vi.name : "a vehicle");
        } else if (!p->transformValid) { who[i] = 0; continue; }
        uint32_t key = p->pointer ^ (driving ? 0x80000000u : 0);   /* getting in/out restarts the trail */
        if (who[i] != key || now - lastAt[i] > 3) { who[i] = key; memcpy(last[i], pos, 12); lastAt[i] = now; continue; }
        float dx = pos[0] - last[i][0], dz = pos[2] - last[i][2], d = sqrtf(dx * dx + dz * dz);
        if (d < 1.2f) continue;
        if (d > (driving ? 90 : 45)) { memcpy(last[i], pos, 12); lastAt[i] = now; continue; }   /* respawned / teleported */
        for (float t = 0; t <= d; t += g->cell * 0.5f) {
            float x = last[i][0] + dx * (t / d), z = last[i][2] + dz * (t / d);
            int c = nav_cell_index(g, x, z); if (c < 0) continue;
            if (driving) {   /* a vehicle is wider than a cell: the cells beside it too */
                int cx = c % g->w, cz = c / g->w;
                for (int oz = -1; oz <= 1; oz++) for (int ox = -1; ox <= 1; ox++) {
                    int nx = cx + ox, nz = cz + oz; if (nx < 0 || nz < 0 || nx >= g->w || nz >= g->h) continue;
                    unsigned char *v = &g_drive[nz * g->w + nx]; if (!*v) g_driveCells++; if (*v < 250) (*v)++;
                }
            } else { if (!g_walk[c]) g_walkCells++; if (g_walk[c] < 250) g_walk[c]++; }
        }
        /* the human is somewhere the grid has no floor (a climb, a gap the probes missed): probe there again */
        if (!driving && now > reprobeAt[i]) {
            float sn[3];
            if (!navg_snap(pos, sn) || hypotf(sn[0] - pos[0], sn[2] - pos[2]) > 2.5f || fabsf(sn[1] - pos[1]) > 2.0f) {
                navg_reprobe(pos, 5.0f); reprobeAt[i] = now + 4;
            }
        }
        if (driving) { static double dlog; if (now > dlog) { dlog = now + 60; rt_log("learn: human driving so far: %d road cell(s), %d walked", g_driveCells, g_walkCells); } }
        memcpy(last[i], pos, 12); lastAt[i] = now; g_dirty = 1;
    }
}

/* 1.6: is this spot by water bots already had to swim in (within r, at the surface height)? */
int learn_near_water(const float *pos, float r)
{
    for (int i = 0; i < g_nWater; i++) if (hypotf(g_water[i][0] - pos[0], g_water[i][2] - pos[2]) < r && fabsf(g_water[i][1] - pos[1]) < 2.5f) return 1;
    return 0;
}
/* 1.5.2: a bot swims here - the surface becomes water on the grid (and stays so in the .learn file) */
void learn_water(const float *pos)
{
    for (int i = 0; i < g_nWater; i++) if (hypotf(g_water[i][0] - pos[0], g_water[i][2] - pos[2]) < 25 && fabsf(g_water[i][1] - pos[1]) < 2) { navg_mark_water(pos, 50); return; }
    int n = navg_mark_water(pos, 50);
    if (g_nWater < 64) { memcpy(g_water[g_nWater++], pos, 12); g_dirty = 1; }
    static int logs; if (logs++ < 8) rt_log("learn: water at (%.0f, %.0f, %.0f) - %d grid node(s) marked, routes keep out of it", pos[0], pos[1], pos[2], n);
}

/* 1.6.5: a vehicle got stuck here twice in a round - remembered for the map (the drive map is cut here every round) */
void learn_vehicle_stuck(const float *pos)
{
    for (int i = 0; i < g_nVstuck; i++) if (hypotf(g_vstuck[i][0] - pos[0], g_vstuck[i][2] - pos[2]) < 4) return;
    if (g_nVstuck >= 256) memmove(g_vstuck, g_vstuck + 1, sizeof g_vstuck[0] * 255), g_nVstuck = 255;
    memcpy(g_vstuck[g_nVstuck++], pos, 12); g_dirty = 1;
}
