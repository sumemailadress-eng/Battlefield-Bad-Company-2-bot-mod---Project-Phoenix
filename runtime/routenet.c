/* 1.7.0: the route network - saved routes bots always follow (the ThugBot / FloodBots idea, written for BC2's
   Frostbite 1.5 server and Phoenix's grid, not copied from their engine code).

   Foot routes  navcache\<map>.froutes   "route <from> <to> <length m> <n> x,y,z x,y,z ..."
                From every team's spawn area to every objective (flag or M-COM) and between every pair of objectives,
                planned once on the walking grid (in the background, one at a time) and saved. Routes drawn in the 3D map
                go in the same file and are used the same way. A bot going to an objective joins the route that ends there
                at its nearest point and walks it to the end - after a fight, a side-step or a push it goes back to it.
   Vehicle routes navcache\<map>.vroutes "route ax ay az bx by bz n x y z x y z ..." (drawn in the 3D map), plus the
                lines drawn in LearnViewer (<map>.routes). A driver whose vehicle is near one that leads towards its flag
                drives it to its end and never swaps it for its own plan; from the end the drive map takes over.
   Both files are re-read when they change on disk, so routes saved from the 3D map apply to the running round.
   Also written here for the 3D map: <map>.objectives (flags, M-COMs, spawns) and <map>.vehicles (where vehicles stand).
   Simulation thread only. */
#ifdef PHX_HARNESS
#include "harness_win.h"
#else
#include "win_common.h"
#endif
#include "rt.h"
#include "nav.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/stat.h>

#define RN_FMAX 400
#define RN_VMAX 160
#define RN_OWNER 120   /* the planner slot for building routes (bots 0..47, vehicles 64..111) */

typedef struct RNRoute { char from[40], to[40]; float len; int n, fails; float pts[RN_PTS][3]; } RNRoute;
static RNRoute g_f[RN_FMAX], g_v[RN_VMAX];
static int g_fn, g_vn, g_fDirty, g_imported;
static char g_key[48];
static long long g_fStamp = -1, g_vStamp = -1, g_rStamp = -1;
static double g_checkAt, g_saveAt, g_objAt;
/* the route being built */
static int g_building, g_buildA, g_buildB; static float g_ba[3], g_bb[3]; static char g_bFrom[40], g_bTo[40];
static unsigned char g_pairFailed[64][64];
static unsigned g_built, g_used, g_vUsed;

static float d2(const float *a, const float *b) { return hypotf(a[0] - b[0], a[2] - b[2]); }
static int file_path(const char *ext, char *out, unsigned size)
{
    if (!g_key[0]) return 0;
    rt_nav_cache_path(g_key, out, size); char *dot = strrchr(out, '.');
    if (!dot) return 0;
    snprintf(dot, size - (unsigned)(dot - out), "%s", ext);
    return 1;
}
static long long stamp_of(const char *ext)
{
    char p[1200]; struct stat st;
    if (!file_path(ext, p, sizeof p) || stat(p, &st) != 0) return 0;
    return (long long)st.st_mtime * 1000003LL + (long long)st.st_size;
}
static float poly_len(const float (*p)[3], int n) { float s = 0; for (int i = 1; i < n; i++) s += d2(p[i - 1], p[i]); return s; }

/* ---- files */
static void load_froutes(void)
{
    char p[1200]; g_fn = 0; if (!file_path(".froutes", p, sizeof p)) return;
    FILE *f = fopen(p, "r"); if (!f) return;
    static char line[RN_PTS * 28 + 256];
    while (fgets(line, sizeof line, f) && g_fn < RN_FMAX) {
        if (strncmp(line, "route ", 6)) continue;
        RNRoute *r = &g_f[g_fn]; memset(r, 0, sizeof *r);
        int n = 0, off = 0; float len = 0;
        if (sscanf(line + 6, "%39s %39s %f %d%n", r->from, r->to, &len, &n, &off) < 4 || n < 2) continue;
        const char *s = line + 6 + off; int k = 0;
        while (k < n && k < RN_PTS) { float x, y, z; int used = 0; if (sscanf(s, " %f,%f,%f%n", &x, &y, &z, &used) != 3) break; r->pts[k][0] = x; r->pts[k][1] = y; r->pts[k][2] = z; k++; s += used; }
        if (k < 2) continue;
        r->n = k; r->len = poly_len((const float (*)[3])r->pts, k); g_fn++;
    }
    fclose(f);
}
static void save_froutes(void)
{
    char p[1200]; if (!file_path(".froutes", p, sizeof p)) return;
    FILE *f = fopen(p, "w"); if (!f) return;
    fprintf(f, "# Project Phoenix foot routes for %s (%s): route <from> <to> <length m> <points> x,y,z ... - edit them in the 3D map\n", g_key, g_world.mapName[0] ? g_world.mapName : g_world.levelName);
    for (int i = 0; i < g_fn; i++) { const RNRoute *r = &g_f[i];
        fprintf(f, "route %s %s %.0f %d", r->from, r->to, r->len, r->n);
        for (int k = 0; k < r->n; k++) fprintf(f, " %.1f,%.1f,%.1f", r->pts[k][0], r->pts[k][1], r->pts[k][2]);
        fputc('\n', f); }
    fclose(f); g_fDirty = 0; g_fStamp = stamp_of(".froutes");
}
static void add_vroute(const float (*pts)[3], int n, const char *from)
{
    if (n < 2 || g_vn >= RN_VMAX) return;
    RNRoute *r = &g_v[g_vn++]; memset(r, 0, sizeof *r);
    if (n > RN_PTS) n = RN_PTS;
    memcpy(r->pts, pts, sizeof(float) * 3 * (size_t)n); r->n = n; r->len = poly_len(pts, n); snprintf(r->from, sizeof r->from, "%s", from);
}
static void load_vroutes(void)
{
    char p[1200]; g_vn = 0; static float pts[RN_PTS * 4][3];
    if (file_path(".vroutes", p, sizeof p)) { FILE *f = fopen(p, "r");
        if (f) { static char line[RN_PTS * 4 * 30 + 256];
            while (fgets(line, sizeof line, f)) {
                if (strncmp(line, "route ", 6)) continue;
                float a[6]; int n = 0, off = 0;
                if (sscanf(line + 6, "%f %f %f %f %f %f %d%n", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &n, &off) < 7 || n < 2) continue;
                const char *s = line + 6 + off; int k = 0;
                while (k < n && k < RN_PTS * 4) { int used = 0; if (sscanf(s, " %f %f %f%n", &pts[k][0], &pts[k][1], &pts[k][2], &used) != 3) break; k++; s += used; }
                for (int st = 0; st + 1 < k; st += RN_PTS - 1) add_vroute((const float (*)[3])&pts[st], k - st < RN_PTS ? k - st : RN_PTS, "3d map");
            }
            fclose(f); } }
    /* the lines drawn in LearnViewer (1.6.x): "route" then "x z" or "x y z" per line, "end" */
    if (file_path(".routes", p, sizeof p)) { FILE *f = fopen(p, "r");
        if (f) { char line[256]; int k = 0, in = 0;
            while (fgets(line, sizeof line, f)) {
                if (!strncmp(line, "route", 5)) { if (in && k >= 2) add_vroute((const float (*)[3])pts, k, "learnviewer"); in = 1; k = 0; continue; }
                if (!strncmp(line, "end", 3)) { if (in && k >= 2) add_vroute((const float (*)[3])pts, k, "learnviewer"); in = 0; k = 0; continue; }
                float v[3]; int c = sscanf(line, "%f %f %f", &v[0], &v[1], &v[2]);
                if (in && k < RN_PTS && c >= 2) { pts[k][0] = v[0]; pts[k][1] = c == 3 ? v[1] : NAN; pts[k][2] = c == 3 ? v[2] : v[1]; k++; }
            }
            if (in && k >= 2) add_vroute((const float (*)[3])pts, k, "learnviewer");
            fclose(f); } }
    /* drawn lines with no heights: the ground from the grid */
    for (int i = 0; i < g_vn; i++) for (int k = 0; k < g_v[i].n; k++) if (isnan(g_v[i].pts[k][1])) {
        float y = navg_ground_at(g_v[i].pts[k][0], g_v[i].pts[k][2]); g_v[i].pts[k][1] = isnan(y) ? 0 : y; }
}

/* ---- the objectives and spawn areas the foot routes join */
typedef struct End { float p[3]; char label[40]; int team; } End;
static int ends_list(End *e, int max)
{
    int n = 0;
    for (int i = 0; i < g_world.cpCount && n < max; i++) {
        const CapturePoint *c = &g_world.cps[i];
        e[n].team = 0; memcpy(e[n].p, c->pos, 12);
        snprintf(e[n].label, sizeof e[n].label, "p-%s%c@%.0f,%.0f", c->isObj ? "mcom" : "flag", 'A' + i, c->pos[0], c->pos[2]); n++;
    }
    int objs = n;
    for (int t = 1; t <= 2; t++) {   /* spawn areas: spawn points of a team within 30 m make one area */
        int start = n;
        for (int i = 0; i < g_world.spawnCount && n < max; i++) {
            const SpawnPoint *s = &g_world.spawns[i]; if (!s->posValid || s->team != t) continue;
            int dup = 0; for (int k = start; k < n; k++) if (d2(e[k].p, s->pos) < 30) dup = 1;
            int nearObj = 0; for (int k = 0; k < objs; k++) if (d2(e[k].p, s->pos) < 25) nearObj = 1;   /* (a flag's own spawns) */
            if (dup || nearObj || n - start >= 6) continue;
            e[n].team = t; memcpy(e[n].p, s->pos, 12); snprintf(e[n].label, sizeof e[n].label, "p-spawn%d@%.0f,%.0f", t, s->pos[0], s->pos[2]); n++;
        }
    }
    return n;
}
static int have_route(const float *a, const float *b)
{
    for (int i = 0; i < g_fn; i++) { const RNRoute *r = &g_f[i];
        if ((d2(r->pts[0], a) < 12 && d2(r->pts[r->n - 1], b) < 12) || (d2(r->pts[0], b) < 12 && d2(r->pts[r->n - 1], a) < 12)) return 1; }
    return 0;
}
static int g_objWritten;   /* (1.7.1: this map's objectives are on disk) */
static void write_objectives(void)
{
    char p[1200]; if (!file_path(".objectives", p, sizeof p)) return;
    FILE *f = fopen(p, "w"); if (!f) return;
    fprintf(f, "# Project Phoenix: %s - flag <letter> x y z owner name | mcom <letter> x y z owner stage name | spawn <team> x y z (for the 3D map)\nlevel %s\nname %s\n", g_key, g_world.levelName, g_world.mapName);
    if (g_world.mcomAllN)   /* 1.7.1: every stage's M-COMs (the bots play only the stage that is on) */
        for (int i = 0; i < g_world.mcomAllN; i++) fprintf(f, "mcom %c %.1f %.1f %.1f %d %d Stage %d%s%s\n", 'A' + i, g_world.mcomAll[i].pos[0], g_world.mcomAll[i].pos[1], g_world.mcomAll[i].pos[2],
                                                           g_world.mcomDefender, g_world.mcomAll[i].stage, g_world.mcomAll[i].stage, g_world.mode == MODE_SQRUSH ? "" : " ", g_world.mode == MODE_SQRUSH ? "" : (g_world.mcomAll[i].sub ? "B" : "A"));
    for (int i = 0; i < g_world.cpCount; i++) {
        if (g_world.cps[i].isObj) { if (!g_world.mcomAllN) fprintf(f, "mcom %c %.1f %.1f %.1f %d 0 %s\n", 'A' + i, g_world.cps[i].pos[0], g_world.cps[i].pos[1], g_world.cps[i].pos[2], g_world.cps[i].owner, g_world.cps[i].name); continue; }
        char nm[40]; snprintf(nm, sizeof nm, "%s%s", g_world.cps[i].name, g_world.cps[i].locked ? " (base)" : "");
        for (char *c = nm; *c; c++) if (*c == '\r' || *c == '\n') *c = ' ';
        fprintf(f, "flag %c %.1f %.1f %.1f %d %s\n", 'A' + i, g_world.cps[i].pos[0], g_world.cps[i].pos[1], g_world.cps[i].pos[2], g_world.cps[i].owner, nm);
    }
    for (int i = 0; i < g_world.spawnCount; i++) if (g_world.spawns[i].posValid) fprintf(f, "spawn %d %.1f %.1f %.1f\n", g_world.spawns[i].team, g_world.spawns[i].pos[0], g_world.spawns[i].pos[1], g_world.spawns[i].pos[2]);
    fclose(f);
    if (file_path(".vehicles", p, sizeof p) && g_world.vspawnCount && (f = fopen(p, "w"))) {
        for (int i = 0; i < g_world.vspawnCount; i++) { const VehicleSpawner *v = &g_world.vspawn[i];
            fprintf(f, "veh %s %.1f %.1f %.1f 0 %d\n", v->name[0] ? v->name : "vehicle", v->pos[0], v->pos[1], v->pos[2], v->team); }
        fclose(f);
    }
    char live[1200]; rt_nav_cache_path("live", live, sizeof live); char *dot = strrchr(live, '.'); if (dot) strcpy(dot, ".txt");
    if ((f = fopen(live, "w"))) { fprintf(f, "%s\n", g_key); fclose(f); }
}

/* ---- tick: files, then build the missing foot routes one at a time */
/* 1.7.1 map scanner: navcache\scan.txt tells the launcher how far the survey of the map being played is */
static void write_scan(double now)
{
    static double at; if (now < at) return; at = now + 3;
    int pct, grid, stairs, vscan; navg_scan_state(&pct, &grid, &stairs, &vscan);
    char p[1200]; rt_nav_cache_path("scan", p, sizeof p); char *dot = strrchr(p, '.'); if (dot) strcpy(dot, ".txt");
    FILE *f = fopen(p, "w"); if (!f) return;
    const char *k = navg_key();
    int objs = g_world.mcomAllN ? g_world.mcomAllN : g_world.cpCount;
    int done = grid && stairs && (vscan || !g_cfg.vehicles) && g_objWritten && !strcmp(g_key, k ? k : "") && (objs || g_world.mode == MODE_SQDM);
    fprintf(f, "level %s\nname %s\nkey %s\nnav %d\nstairs %d\nvehicles %s\nobjectives %d\nseconds %.0f\ndone %d\n", g_world.levelName, g_world.mapName,
            k && k[0] ? k : "-", pct, stairs, g_cfg.vehicles ? (vscan ? "1" : "0") : "off", objs, now - g_world.levelSince, done);
    fclose(f);
}

void rn_tick(double now)
{
    if (g_cfg.scanMode && g_world.level) write_scan(now);
    const char *k = navg_key();
    if (!navg_ready() || !k || !k[0]) return;
    if (strcmp(k, g_key)) {
        snprintf(g_key, sizeof g_key, "%s", k);
        if (g_building) nav_cancel_path(RN_OWNER);
        g_fn = g_vn = 0; g_fDirty = 0; g_building = 0; g_built = g_used = g_vUsed = 0; memset(g_pairFailed, 0, sizeof g_pairFailed);
        g_fStamp = g_vStamp = g_rStamp = -1; g_checkAt = 0; g_objAt = now + 5; g_imported = 0; g_objWritten = 0;
    }
    if (now >= g_checkAt) {   /* (re-read when the 3D map or LearnViewer saved new routes) */
        g_checkAt = now + 5;
        long long fs = stamp_of(".froutes"), vs = stamp_of(".vroutes"), rs = stamp_of(".routes");
        if (fs != g_fStamp && !g_fDirty) { g_fStamp = fs; load_froutes(); if (g_fn) rt_log("routes: %d foot route(s) loaded for %s", g_fn, g_key); }
        if (vs != g_vStamp || rs != g_rStamp) { g_vStamp = vs; g_rStamp = rs; load_vroutes(); if (g_vn) rt_log("routes: %d vehicle route(s) loaded - drivers near one that leads to their flag drive it to its end", g_vn); }
    }
    /* (1.7.1: Squad Deathmatch has no flags - its spawns are written once the level has settled; the scanner every 5 s) */
    if (now >= g_objAt && (g_world.cpCount || g_world.mcomAllN || now - g_world.levelSince > 45)) { g_objAt = now + (g_cfg.scanMode ? 5 : 30); write_objectives(); g_objWritten = 1; }
    if (g_fDirty && now >= g_saveAt) save_froutes();
    if (!g_cfg.routes || !g_cfg.autoRoutes || !g_world.cpCount) return;
    End e[64]; int ne = ends_list(e, 64);
    if (!g_building) {   /* the next missing pair */
        for (int i = 0; i < ne && !g_building; i++) for (int j = 0; j < ne && !g_building; j++) {
            if (i == j || g_pairFailed[i][j] || g_fn >= RN_FMAX) continue;
            if (e[i].team && (e[j].team || j < i)) continue;   /* spawn -> objective, objective -> objective (once per pair) */
            if (!e[i].team && !e[j].team && j < i) continue;
            if (have_route(e[i].p, e[j].p)) continue;
            g_building = 1; g_buildA = i; g_buildB = j; memcpy(g_ba, e[i].p, 12); memcpy(g_bb, e[j].p, 12);
            snprintf(g_bFrom, sizeof g_bFrom, "%s", e[i].label); snprintf(g_bTo, sizeof g_bTo, "%s", e[j].label);
        }
        if (!g_building) return;
    }
    static float out[RN_PTS * 3]; int deferred = 0;
    int n = navg_path(RN_OWNER, g_ba, g_bb, out, RN_PTS - 1, &deferred);
    if (!n && deferred) return;
    g_building = 0;
    if (n < 1 || d2(&out[(n - 1) * 3], g_bb) > 12) { g_pairFailed[g_buildA][g_buildB] = 1; return; }
    RNRoute *r = &g_f[g_fn++]; memset(r, 0, sizeof *r);
    memcpy(r->pts[0], g_ba, 12); memcpy(r->pts[1], out, (size_t)n * 12); r->n = n + 1;
    r->len = poly_len((const float (*)[3])r->pts, r->n);
    snprintf(r->from, sizeof r->from, "%s", g_bFrom); snprintf(r->to, sizeof r->to, "%s", g_bTo);
    g_fDirty = 1; g_saveAt = now + 10; g_built++;
    if (g_built <= 3 || g_built % 10 == 0) rt_log("routes: %u foot route(s) made for this map so far (%s -> %s, %.0f m)", g_built, r->from, r->to, r->len);
}

/* ---- picking. A route (either way along it) that ends within endR of the goal, joined at its point nearest the bot
   within joinR; the shortest walk wins (joining distance counts twice, failures 30 m each). Writes the points from
   the joining point to the end. */
static int pick(RNRoute *set, int count, const float *from, const float *goal, float endR, float joinR, int vehicle, float *out, int max, float *joinDist, int *id)
{
    int best = -1, bestDir = 0, bestK = 0; float bestScore = 1e30f, bestJoin = 0;
    float here = d2(from, goal);
    for (int i = 0; i < count; i++) { const RNRoute *r = &set[i];
        for (int dir = 0; dir < 2; dir++) {
            const float *end = dir ? r->pts[0] : r->pts[r->n - 1];
            float endGap = d2(end, goal);
            if (!vehicle && (endGap > endR || fabsf(end[1] - goal[1]) > 5)) continue;
            if (vehicle && here - endGap < 30) continue;   /* a vehicle route must take it 30 m+ closer to its flag */
            int k = -1; float kd = 1e30f;
            for (int q = 0; q < r->n; q++) { const float *p = r->pts[q]; float d = d2(p, from) + fabsf(p[1] - from[1]) * (vehicle ? 0.5f : 2.0f); if (d < kd) { kd = d; k = q; } }
            if (k < 0 || kd > joinR) continue;
            float rem = 0;
            if (!dir) { for (int q = k + 1; q < r->n; q++) rem += d2(r->pts[q - 1], r->pts[q]); if (k == r->n - 1) continue; }
            else { for (int q = k; q > 0; q--) rem += d2(r->pts[q - 1], r->pts[q]); if (k == 0) continue; }
            if (vehicle && rem < 20) continue;
            float score = kd * 2 + rem + endGap * (vehicle ? 1.0f : 0.5f) + r->fails * 30;
            if (score < bestScore) { bestScore = score; best = i; bestDir = dir; bestK = k; bestJoin = kd; }
        }
    }
    if (best < 0) return 0;
    const RNRoute *r = &set[best]; int n = 0;
    if (!bestDir) for (int q = bestK; q < r->n && n < max; q++) { memcpy(&out[n * 3], r->pts[q], 12); n++; }
    else for (int q = bestK; q >= 0 && n < max; q--) { memcpy(&out[n * 3], r->pts[q], 12); n++; }
    if (joinDist) *joinDist = bestJoin;
    if (id) *id = best;
    return n;
}
int rn_pick_foot(const float *from, const float *goal, float *out, int max, float *joinDist, int *id)
{
    if (!g_cfg.routes || !g_fn) return 0;
    int n = pick(g_f, g_fn, from, goal, 12, 45, 0, out, max, joinDist, id);
    if (n) g_used++;
    return n;
}
int rn_pick_vehicle(const float *from, const float *goal, float *out, int max, float *joinDist, int *id)
{
    if (!g_cfg.routes || !g_vn) return 0;
    int n = pick(g_v, g_vn, from, goal, 0, 60, 1, out, max, joinDist, id);
    if (n) g_vUsed++;
    return n;
}
void rn_foot_failed(int id)
{
    if (id < 0 || id >= g_fn) return;
    if (++g_f[id].fails >= 3 && !strncmp(g_f[id].from, "p-", 2)) {
        /* a planned route bots keep failing on: dropped (planned again, round the trouble); drawn ones are kept */
        rt_log("routes: bots kept getting stuck on %s -> %s - planning it again", g_f[id].from, g_f[id].to);
        g_f[id] = g_f[--g_fn]; g_fDirty = 1; g_saveAt = rt_now() + 10;
    }
}
void rn_status(void) { if (g_key[0]) rt_log("routes: %d foot route(s) (%u made this round, %u joined), %d vehicle route(s) (%u driven)", g_fn, g_built, g_used, g_vn, g_vUsed); }
