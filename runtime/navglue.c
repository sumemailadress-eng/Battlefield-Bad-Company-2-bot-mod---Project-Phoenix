/* Navigation glue: one ray-built grid per level (AltBotMod's nav.c), cached on disk once complete. */
#ifdef PHX_HARNESS
#include "harness_win.h"
#else
#include "win_common.h"
#endif
#include "rt.h"
#include "nav.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

void rt_nav_cache_path(const char *key, char *out, unsigned size);   /* module / harness */

static NavGrid *g_grid;
static int g_gridDone, g_saved, g_failed;
static unsigned g_gridLevel;
static char g_key[48];
static double g_lastProgressLog;
static int g_stairsDone;                 /* strict staircase re-check after the grid is ready */
static double g_unblockAt, g_reprobeAt;
static unsigned char *g_driveMask; static int g_driveCount;
static unsigned char *g_dscan;      /* per node: bit0 E link bad, bit1 N link bad, bit7 scanned */
static int g_dscanCursor, g_dscanDone, g_dscanBad, g_dscanApplied; static const NavGrid *g_dscanFor;
static float g_vstuck[256][3]; static int g_nVstuck;

static const unsigned char *g_lastDrive; static int g_lastDriveW, g_lastDriveH;
/* 1.6.6: connected drive areas (labels per node) - routes start and end in the same one */
static int *g_dlab, *g_dlabSize; static int g_dlabN, g_dlabCap, g_dlabDirty = 1; static const NavGrid *g_dlabGrid;
static float g_vehGap; static int g_vehPartial;
static void mask_changed(void) { g_dlabDirty = 1; }
static RoadLine *g_roadLines; static int g_nRoadLines; static const float (*g_roadPts)[3]; static int g_roadsApplied;
void navg_learn_drive(const unsigned char *drive, int w, int h);

#define MAX_REPROBE 16
static float g_reprobeQ[MAX_REPROBE][4]; static int g_reprobeN;   /* x, y, z, radius */
static unsigned g_reprobes, g_reprobeAdded;

static int nav_ray(const float *from, const float *to, float *hit, float *normal)
{
    float h[3], n[3];
    int r = eng_ray(from, to, h, n);
    if (r) { if (hit) memcpy(hit, h, 12); if (normal) memcpy(normal, n, 12); }
    return r;
}

void navg_reset(void)
{
    learn_reset();   /* saves what was learned on the old map; its danger mask belongs to the old grid */
    nav_cancel_paths();
    nav_set_path_mask(NULL);
    if (g_driveMask) { nav_mask_free(g_driveMask); g_driveMask = NULL; g_driveCount = 0; }
    nav_set_bad_links(NULL, NULL);
    g_roadLines = NULL; g_nRoadLines = 0; g_roadsApplied = 0;
    free(g_dscan); g_dscan = NULL; g_dscanFor = NULL; g_dscanDone = 0; g_nVstuck = 0;
    free(g_dlab); free(g_dlabSize); g_dlab = g_dlabSize = NULL; g_dlabN = g_dlabCap = 0; g_dlabDirty = 1; g_dlabGrid = NULL;
    if (g_grid) { nav_free(g_grid); g_grid = NULL; }
    g_gridDone = g_saved = g_failed = 0; g_key[0] = 0; g_stairsDone = 0; g_reprobeN = 0;
}

int navg_ready(void) { return g_grid && g_gridDone; }
const char *navg_key(void) { return g_key; }
const char *navg_last_error(void) { const char *e = nav_last_error(); return e && e[0] ? e : "no reason given"; }
NavGrid *navg_grid(void) { return navg_ready() ? g_grid : NULL; }

static void make_key(void)
{
    /* Key the map by its flag layout (stable across sessions), else by its spawn layout. */
    uint32_t h = 2166136261u; int n = 0;
    for (int i = 0; i < g_world.cpCount; i++) {
        int v[3] = {(int)lroundf(g_world.cps[i].pos[0] / 4), (int)lroundf(g_world.cps[i].pos[1] / 4), (int)lroundf(g_world.cps[i].pos[2] / 4)};
        for (int k = 0; k < 3; k++) { h ^= (uint32_t)v[k]; h *= 16777619u; } n++;
    }
    if (!n) for (int i = 0; i < g_world.spawnCount; i++) if (g_world.spawns[i].posValid) {
        int v[3] = {(int)lroundf(g_world.spawns[i].pos[0] / 4), (int)lroundf(g_world.spawns[i].pos[1] / 4), (int)lroundf(g_world.spawns[i].pos[2] / 4)};
        for (int k = 0; k < 3; k++) { h ^= (uint32_t)v[k]; h *= 16777619u; } n++;
    }
    snprintf(g_key, sizeof g_key, "bc2v2_%s%d_%08lx", g_world.cpCount ? "cq" : "sp", n, (unsigned long)h);
}

/* Grid upkeep once it is ready (AltBotMod's nav upkeep):
   - the strict staircase re-check (wall faces taken for stairs are dropped, missed stairs added, rail tops removed),
     a few thousand rays per tick - real stairs are how bots reach upper floors;
   - links blocked where a bot got stuck come back after 90 s (0.7 never reopened them: routes decayed all round);
   - areas queued by bots (a stuck spot, a flag) are re-probed: walls blown open become passable, rubble and wrecks
     that close a way are learned. */
static void navg_maintain(double now)
{
    if (!g_stairsDone) {
        int removed = 0;
        if (nav_fix_stairs_step(g_grid, nav_ray, 2500, &removed)) {
            g_stairsDone = 1;
            rt_log("nav: stair re-check done - %d false staircase link(s) removed, %d restored, %d staircase(s) added, %d rail top(s) dropped",
                   removed, nav_stairs_restored(), nav_stairs_added(), nav_rails_removed());
        }
        return;
    }
    if (now > g_unblockAt) {
        g_unblockAt = now + 1.0;
        int back = nav_unblock_expired(g_grid, 0);
        if (back && g_cfg.verbose) rt_log("nav: %d link(s) blocked at stuck spots are open again", back);
    }
    if (g_reprobeN && now > g_reprobeAt) {
        g_reprobeAt = now + 0.35;
        float *q = g_reprobeQ[--g_reprobeN];
        int added = nav_reprobe_area(g_grid, nav_ray, q[0] - q[3], q[2] - q[3], q[0] + q[3], q[2] + q[3], q[1] + 3.0f);
        g_reprobes++; g_reprobeAdded += (unsigned)added;
    }
}

void navg_reprobe(const float *pos, float radius)
{
    if (!navg_ready() || g_reprobeN >= MAX_REPROBE) return;
    for (int i = 0; i < g_reprobeN; i++) if (fabsf(g_reprobeQ[i][0] - pos[0]) < 4 && fabsf(g_reprobeQ[i][2] - pos[2]) < 4) return;
    float *q = g_reprobeQ[g_reprobeN++]; q[0] = pos[0]; q[1] = pos[1]; q[2] = pos[2]; q[3] = radius;
}

int navg_snap(const float *pos, float *out) { return navg_ready() && nav_snap(g_grid, pos[0], pos[1], pos[2], out); }

/* ---- 1.6.5: vehicle scan ----
   The soldier grid has every cell a soldier can walk; the drive map keeps the flat, open ones. What it cannot see at
   2.5 m a cell is what stops a wheel: a kerb, a road divider, a low wall, a flight of steps - a soldier steps over them,
   a jeep does not. The scan walks every pair of neighbouring drive nodes with rays - the ground every 0.5 m between
   them (a rise of more than 0.4 m at once is a step) and rays at bumper and bonnet height (0.4 / 0.9 m) - and takes the links that fail
   off: a node with two or more failed links leaves the drive map, one makes it the dearest class (routes cross there
   only if there is no way round, never on a short cut). ~600 rays a tick; the result is saved (navcache\<map>.dscan) so
   it runs once per map. Vehicles' stuck spots are added the same way (persisted by learn.c) */
static void dscan_path(char *out, unsigned size) { rt_nav_cache_path(g_key, out, size); char *dot = strrchr(out, '.'); if (dot) snprintf(dot, size - (unsigned)(dot - out), ".dscan"); }
static int dscan_alloc(void)
{
    if (g_dscanFor == g_grid && g_dscan) return 1;
    free(g_dscan); g_dscan = NULL; g_dscanCursor = 0; g_dscanDone = 0; g_dscanBad = 0; g_dscanApplied = 0;
    size_t n = (size_t)g_grid->w * g_grid->h * NAV_LAYERS; g_dscan = calloc(n, 1); if (!g_dscan) return 0;
    g_dscanFor = g_grid;
    char p[1200]; dscan_path(p, sizeof p); FILE *f = fopen(p, "rb");
    if (f) { unsigned hdr[2] = {0, 0};
        if (fread(hdr, 4, 2, f) == 2 && hdr[0] == 0x4e435344u && hdr[1] == (unsigned)n && fread(g_dscan, 1, n, f) == n) { g_dscanDone = 1; nav_set_bad_links(g_grid, g_dscan); rt_log("nav: vehicle scan loaded from %s", p); }
        else memset(g_dscan, 0, n);
        fclose(f); }
    return 1;
}
static float ground_y(float x, float z, float top, float bot)
{
    float a[3] = {x, top, z}, b[3] = {x, bot, z}, h[3], nn[3];
    return eng_ray(a, b, h, nn) ? h[1] : NAN;
}
/* 1 = a wheel cannot cross from a to b */
static int edge_bad(int a, int b)
{
    float pa[3], pb[3]; nav_node_pos(g_grid, a, pa); nav_node_pos(g_grid, b, pb);
    float top = fmaxf(pa[1], pb[1]) + 1.5f, bot = fminf(pa[1], pb[1]) - 1.5f, prev = pa[1];
    for (int k = 1; k <= 8; k++) {
        float t = k / 8.0f, y = ground_y(pa[0] + (pb[0] - pa[0]) * t, pa[2] + (pb[2] - pa[2]) * t, top, bot);
        if (isnan(y)) return 1;
        if (fabsf(y - prev) > 0.4f) return 1;   /* a step, a kerb or a divider */
        prev = y;
    }
    float h[3], nn[3];
    for (int k = 0; k < 2; k++) { float up = k ? 0.9f : 0.4f;   /* bumper and bonnet height: a jersey barrier, a low wall */
        float a2[3] = {pa[0], pa[1] + up, pa[2]}, b2[3] = {pb[0], pb[1] + up, pb[2]};
        if (eng_ray(a2, b2, h, nn) && nn[1] < 0.7f) {
            float h2[3], n2[3];   /* a thin board (a wooden fence a vehicle breaks) is no wall; a concrete divider is */
            if (!eng_ray(b2, a2, h2, n2) || hypotf(h[0] - h2[0], h[2] - h2[2]) > 0.15f) return 1; } }
    return 0;
}
static void dscan_apply(void)
{
    if (!g_driveMask || !navg_ready() || !dscan_alloc()) return;
    int n = g_grid->w * g_grid->h * NAV_LAYERS, removed = 0, dear = 0;
    for (int node = 0; node < n; node++) {
        if (!g_driveMask[node] || g_driveMask[node] == 6) continue;
        int bad = 0;
        if (g_dscan[node] & 1) bad++; if (g_dscan[node] & 2) bad++;
        int w = nav_drive_step(g_grid, node, 4), s2 = nav_drive_step(g_grid, node, 6);   /* (the links from the west / south neighbours) */
        if (w >= 0 && (g_dscan[w] & 1)) bad++; if (s2 >= 0 && (g_dscan[s2] & 2)) bad++;
        if (bad >= 2) { g_driveMask[node] = 0; removed++; } else if (bad == 1) { g_driveMask[node] = 5; dear++; }
    }
    int stuck = 0; for (int i = 0; i < g_nVstuck; i++) stuck += nav_mask_block(g_grid, g_driveMask, g_vstuck[i][0], g_vstuck[i][2], 3.5f);
    if (g_dscanDone || g_nVstuck) rt_log("nav: vehicle scan - %d node(s) off the drive map (steps, kerbs, dividers), %d made dear; %d cut where vehicles got stuck before", removed, dear, stuck);
    g_dscanApplied = g_dscanDone; mask_changed();
}
static void roads_apply(void);
/* the drive map, made once per grid: geometry, then the level's roads, the vehicle scan, stuck spots, human driving */
static int ensure_drive_mask(void)
{
    if (g_driveMask) return 1;
    g_driveMask = nav_drive_mask(g_grid, &g_driveCount);
    rt_log("nav: %d node(s) drivable for vehicles", g_driveCount);
    if (!g_driveMask) return 0;
    roads_apply();   /* 1.6.6: the level's own roads (if read yet) */
    dscan_apply();   /* 1.6.5: what the vehicle scan found (kerbs, dividers, steps), and where vehicles got stuck */
    if (g_lastDrive) navg_learn_drive(g_lastDrive, g_lastDriveW, g_lastDriveH);   /* the routes humans drove */
    mask_changed();
    return 1;
}
static void dscan_step(int budget)
{
    if (g_dscanDone || !navg_ready() || !dscan_alloc()) return;
    if (!ensure_drive_mask()) return;
    int n = g_grid->w * g_grid->h * NAV_LAYERS;
    unsigned before = eng_ray_count();
    for (; g_dscanCursor < n; g_dscanCursor++) {
        if (eng_ray_count() - before > (unsigned)budget) return;
        int node = g_dscanCursor; if (!g_driveMask[node]) continue;
        unsigned char v = 0x80;
        int e = nav_drive_step(g_grid, node, 0), nn = nav_drive_step(g_grid, node, 2);   /* east (+x) and north (+z) */
        if (e >= 0 && g_driveMask[e] && edge_bad(node, e)) { v |= 1; g_dscanBad++; }
        if (nn >= 0 && g_driveMask[nn] && edge_bad(node, nn)) { v |= 2; g_dscanBad++; }
        g_dscan[node] = v;
    }
#ifdef PHX_HARNESS
    if (getenv("DSCANDBG")) { int tot = 0, near = 0; for (int i = 0; i < n; i++) if (g_driveMask[i]) { float q[3]; nav_node_pos(g_grid, i, q); tot++; if (fabsf(q[2] - 40) < 4 && q[0] > -120 && q[0] < 60) near++; }
        rt_log("DSCANDBG mask %d nodes, %d within 4 m of the divider line", tot, near);
        int shown = 0; for (int i = 0; i < n && shown < 4; i++) if (g_driveMask[i]) { float q[3]; nav_node_pos(g_grid, i, q); if (fabsf(q[2] - 40) < 2.6f && q[0] > -50 && q[0] < -40) {
            int e2 = nav_drive_step(g_grid, i, 2), e6 = nav_drive_step(g_grid, i, 6); float q2[3] = {0}; if (e2 >= 0) nav_node_pos(g_grid, e2, q2);
            rt_log("DSCANDBG node (%.2f %.2f %.2f) +z %d (%.2f %.2f %.2f) bad %d, -z %d bad %d, mask +z %d", q[0], q[1], q[2], e2, q2[0], q2[1], q2[2], e2 >= 0 ? edge_bad(i, e2) : -1, e6, e6 >= 0 ? edge_bad(i, e6) : -1, e2 >= 0 ? g_driveMask[e2] : -1); shown++; } } }
#endif
    g_dscanDone = 1; nav_set_bad_links(g_grid, g_dscan);   /* 1.6.6: routes never cross a bad link */
    char p[1200]; dscan_path(p, sizeof p); FILE *f = fopen(p, "wb");
    if (f) { unsigned hdr[2] = {0x4e435344u, (unsigned)n}; fwrite(hdr, 4, 2, f); fwrite(g_dscan, 1, (size_t)n, f); fclose(f); }
    rt_log("nav: vehicle scan done - %d link(s) a wheel cannot cross (steps, kerbs, dividers, low walls); saved", g_dscanBad);
    dscan_apply();
}
void navg_vehicle_stuck_spots(const float (*pos)[3], int n)
{
    g_nVstuck = n > 256 ? 256 : n; memcpy(g_vstuck, pos, sizeof(float) * 3 * g_nVstuck);
    if (g_driveMask) for (int i = 0; i < g_nVstuck; i++) nav_mask_block(g_grid, g_driveMask, g_vstuck[i][0], g_vstuck[i][2], 3.5f);
    mask_changed();
}

/* ---- 1.6.6: connected drive areas ----
   The 1.6.5 log: 170 "no drivable route" in one round - the start or the flag's nearest drive node sat in a pocket of
   the drive map that does not connect to the rest ("A* exhausted after 6 nodes", "...after 5375 nodes"), and the bot
   then drove in a straight line at the flag, into stairwells, dividers and alleys. Now every drive node carries the
   number of its connected area; a route starts in the big area near the vehicle and ends at the node of THAT area
   closest to the flag. If the flag is off the drive map the route ends short and the crew walks the rest. */
static void dlab_build(void)
{
    g_dlabDirty = 0; g_dlabN = 0; g_dlabGrid = g_grid;
    if (!g_driveMask || !navg_ready()) return;
    int n = g_grid->w * g_grid->h * NAV_LAYERS;
    free(g_dlab); g_dlab = malloc((size_t)n * sizeof *g_dlab);
    int *queue = malloc((size_t)n * sizeof *queue);
    if (!g_dlab || !queue) { free(g_dlab); g_dlab = NULL; free(queue); return; }
    for (int i = 0; i < n; i++) g_dlab[i] = -1;
    int big = 0, bigSize = 0;
    for (int s0 = 0; s0 < n; s0++) {
        if (!g_driveMask[s0] || g_dlab[s0] >= 0) continue;
        if (g_dlabN >= g_dlabCap) { int nc = g_dlabCap ? g_dlabCap * 2 : 256; int *ns = realloc(g_dlabSize, (size_t)nc * sizeof *ns); if (!ns) break; g_dlabSize = ns; g_dlabCap = nc; }
        int lab = g_dlabN++, qh = 0, qt = 0; queue[qt++] = s0; g_dlab[s0] = lab;
        while (qh < qt) {
            int cur = queue[qh++];
            for (int d = 0; d < 8; d++) {
                int nb = nav_step_node(g_grid, cur, d);
                if (nb < 0 || !g_driveMask[nb] || g_dlab[nb] >= 0 || nav_link_bad(g_grid, cur, nb, d)) continue;
                g_dlab[nb] = lab; queue[qt++] = nb;
            }
        }
        g_dlabSize[lab] = qt; if (qt > bigSize) { bigSize = qt; big = lab; }
    }
    free(queue);
    static unsigned logged; if (logged != g_gridLevel || g_dlabN < 2) { logged = g_gridLevel;
        rt_log("nav: drive map in %d connected area(s), the biggest %d node(s)", g_dlabN, bigSize); }
    (void)big;
}
/* the drive node nearest p within maxR (height within maxDy unless p[1] is NaN); lab >= 0: only in that area;
   avoidSmall: pockets under 300 nodes count 60 m further away */
static int drive_pick(const float *p, float maxR, float maxDy, int lab, int avoidSmall, float *out, int *outLab)
{
    if (!g_dlab || g_dlabGrid != g_grid) return 0;
    NavGrid *g = g_grid; int c = nav_cell_index(g, p[0], p[2]);
    int cx, cz;
    if (c >= 0) { cx = c % g->w; cz = c / g->w; }
    else { cx = (int)floorf((p[0] - g->x0) / g->cell + 0.5f); cz = (int)floorf((p[2] - g->z0) / g->cell + 0.5f); }
    int r = (int)(maxR / g->cell) + 1, bn = -1; float best = 1e30f;
    for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) {
        int x = cx + dx, z = cz + dz; if (x < 0 || z < 0 || x >= g->w || z >= g->h) continue;
        float d = hypotf((float)dx, (float)dz) * g->cell; if (d > maxR || d >= best) continue;
        for (int l = 0; l < NAV_LAYERS; l++) {
            int node = (z * g->w + x) * NAV_LAYERS + l;
            if (!g_driveMask[node] || g_dlab[node] < 0) continue;
            if (!isnan(p[1]) && fabsf(g->height[node] - p[1]) > maxDy) continue;
            int nl = g_dlab[node]; if (lab >= 0 && nl != lab) continue;
            float sc = d + (avoidSmall && g_dlabSize[nl] < 300 ? 60.0f : 0.0f);
            if (sc < best) { best = sc; bn = node; }
        }
    }
    if (bn < 0) return 0;
    nav_node_pos(g, bn, out); if (outLab) *outLab = g_dlab[bn];
    return 1;
}
float navg_vehicle_gap(void) { return g_vehGap; }
int navg_vehicle_partial(void) { return g_vehPartial; }
/* 1 = the straight line a-b stays on the drive map (every 1.25 m a drive node within 3.5 m of the line's height) */
int navg_drive_line_ok(const float *a, const float *b)
{
    if (!navg_ready() || !g_driveMask) return 1;
    NavGrid *g = g_grid; float len = hypotf(b[0] - a[0], b[2] - a[2]); int steps = (int)(len / 1.25f) + 1;
    for (int k = 1; k <= steps; k++) {
        float t = (float)k / steps, x = a[0] + (b[0] - a[0]) * t, y = a[1] + (b[1] - a[1]) * t, z = a[2] + (b[2] - a[2]) * t;
        int c = nav_cell_index(g, x, z); if (c < 0) return 0;
        int ok = 0;
        for (int l = 0; l < NAV_LAYERS && !ok; l++) { int node = c * NAV_LAYERS + l; if (g_driveMask[node] && fabsf(g->height[node] - y) < 3.5f) ok = 1; }
        if (!ok) return 0;
    }
    return 1;
}

/* ---- 1.6.6: the level's roads (roads.c reads them) ----
   A line is trusted when, sampled every 2 m, at least 60 % of it (35 % for a line named like a road) runs over
   drive-map ground within 6 m of its height. The drive nodes within 3.5 m of a trusted line become class 7: routes
   follow them (cost x0.35; only cells humans drove are cheaper) and string-pulling keeps to them. */
static void roads_path(char *out, unsigned size) { rt_nav_cache_path(g_key, out, size); char *dot = strrchr(out, '.'); if (dot) snprintf(dot, size - (unsigned)(dot - out), ".roads"); }
static int drive_node_near(float x, float y, float z, float dy)
{
    int c = nav_cell_index(g_grid, x, z); if (c < 0) return -1;
    for (int l = 0; l < NAV_LAYERS; l++) { int node = c * NAV_LAYERS + l; if (g_driveMask[node] && fabsf(g_grid->height[node] - y) < dy) return node; }
    return -1;
}
static void roads_apply(void)
{
    if (!g_roadLines || !g_driveMask || !navg_ready() || g_roadsApplied) return;
    g_roadsApplied = 1;
    int accepted = 0, marked = 0; float km = 0;
    NavGrid *g = g_grid; int hw = (int)(3.5f / g->cell) + 1;
    for (int i = 0; i < g_nRoadLines; i++) {
        RoadLine *l = &g_roadLines[i]; const float (*p)[3] = g_roadPts + l->first;
        int samples = 0, onGround = 0;
        for (int k = 1; k < l->n; k++) {
            float seg = hypotf(p[k][0] - p[k - 1][0], p[k][2] - p[k - 1][2]); int m = (int)(seg / 2.0f) + 1;
            for (int j = 0; j < m; j++) { float t = (float)j / m, x = p[k - 1][0] + (p[k][0] - p[k - 1][0]) * t, y = p[k - 1][1] + (p[k][1] - p[k - 1][1]) * t, z = p[k - 1][2] + (p[k][2] - p[k - 1][2]) * t;
                samples++; if (drive_node_near(x, y, z, 6.0f) >= 0) onGround++; }
        }
        l->onGround = samples ? (float)onGround / samples : 0;
        l->accepted = l->onGround >= (l->named ? 0.35f : 0.6f);
        if (!l->accepted) continue;
        accepted++; km += l->length / 1000.0f;
        for (int k = 1; k < l->n; k++) {
            float seg = hypotf(p[k][0] - p[k - 1][0], p[k][2] - p[k - 1][2]); int m = (int)(seg / 1.25f) + 1;
            for (int j = 0; j <= m; j++) {
                float t = (float)j / m, x = p[k - 1][0] + (p[k][0] - p[k - 1][0]) * t, y = p[k - 1][1] + (p[k][1] - p[k - 1][1]) * t, z = p[k - 1][2] + (p[k][2] - p[k - 1][2]) * t;
                int c = nav_cell_index(g, x, z); if (c < 0) continue;
                int cx = c % g->w, cz = c / g->w;
                for (int dz = -hw; dz <= hw; dz++) for (int dx = -hw; dx <= hw; dx++) {
                    int xx = cx + dx, zz = cz + dz; if (xx < 0 || zz < 0 || xx >= g->w || zz >= g->h) continue;
                    float cxw = g->x0 + xx * g->cell, czw = g->z0 + zz * g->cell; if (hypotf(cxw - x, czw - z) > 3.5f) continue;
                    for (int ly = 0; ly < NAV_LAYERS; ly++) { int node = (zz * g->w + xx) * NAV_LAYERS + ly;
                        if (g_driveMask[node] >= 1 && g_driveMask[node] <= 4 && fabsf(g->height[node] - y) < 6.0f) { g_driveMask[node] = 7; marked++; } }
                }
            }
        }
    }
    if (marked) mask_changed();
    rt_log("roads: %d of %d line(s) from the level data run over drivable ground (%.1f km) - %d drive cell(s) are road now; vehicles prefer them",
           accepted, g_nRoadLines, km, marked);
    for (int i = 0, shown = 0; i < g_nRoadLines && shown < 12; i++, shown++)
        rt_log("roads:   %s '%s' %.0f m, %d point(s), %.0f%% on drivable ground%s", g_roadLines[i].src == ROAD_SRC_ROAD ? "road object" : g_roadLines[i].src == ROAD_SRC_DECAL ? "road decal" : "terrain spline",
               g_roadLines[i].name, g_roadLines[i].length, g_roadLines[i].n, g_roadLines[i].onGround * 100, g_roadLines[i].accepted ? " - used" : " - not used");
    char path[1200]; roads_path(path, sizeof path); FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "# Project Phoenix - road lines read from the level data (1.6.6). road <used 1/0> <road|decal|spline> <name>, then x y z per point, end\n");
        for (int i = 0; i < g_nRoadLines; i++) { const RoadLine *l = &g_roadLines[i];
            fprintf(f, "road %d %s %s\n", l->accepted, l->src == ROAD_SRC_ROAD ? "road" : l->src == ROAD_SRC_DECAL ? "decal" : "spline", l->name);
            for (int k = 0; k < l->n; k++) fprintf(f, "%.1f %.1f %.1f\n", g_roadPts[l->first + k][0], g_roadPts[l->first + k][1], g_roadPts[l->first + k][2]);
            fprintf(f, "end\n"); }
        fclose(f);
    }
}
void navg_roads_set(RoadLine *lines, int n, const float (*pts)[3])
{
    g_roadLines = lines; g_nRoadLines = n; g_roadPts = pts; g_roadsApplied = 0;
    roads_apply();   /* now if the drive map exists, else when it is made */
}

/* vehicles: plan on the drivable mask (open ground, roads preferred, away from walls) */
int navg_path_vehicle(int owner, const float *from, const float *to, float *out, int maxPts, int *deferred)
{
    if (deferred) *deferred = 0;
    if (!navg_ready()) return 0;
    if (!ensure_drive_mask()) return 0;
    if (g_dlabDirty || g_dlabGrid != g_grid) dlab_build();
    float a[3], b[3]; int la = -1;
    g_vehGap = 0; g_vehPartial = 0;
    if (!drive_pick(from, 25, 6, -1, 1, a, &la) && !nav_drive_near(g_grid, g_driveMask, from[0], from[1], from[2], 25, a)) memcpy(a, from, 12);
    if (la >= 0 && drive_pick(to, 260, 15, la, 0, b, NULL)) g_vehGap = hypotf(b[0] - to[0], b[2] - to[2]);
    else if (!nav_drive_near(g_grid, g_driveMask, to[0], to[1], to[2], 60, b)) memcpy(b, to, 12);
    nav_set_path_owner(owner + 64);
    nav_set_path_mask(g_driveMask); nav_set_vehicle_width(1.8f);
    int n = nav_find_path(g_grid, nav_ray, a[0], a[1], a[2], b[0], b[1], b[2], out, maxPts);
    if (n > 0) g_vehPartial = nav_last_partial();
    if (!n && nav_last_deferred() && deferred) *deferred = 1;
    nav_set_path_mask(NULL); nav_set_vehicle_width(1.5f);
    nav_set_path_owner(-1);
    return n;
}

/* 1.5.2: a bot is swimming here: the flat surface around it is water (the probes took it for ground) */
int navg_mark_water(const float *pos, float radius)
{
    if (!navg_ready()) return 0;
    int n = nav_mark_water(g_grid, pos[0], pos[1], pos[2], radius);
    /* 1.6: take the new water off the drivable mask in place. 1.5.2 freed the mask and rebuilt it, but queued vehicle
       routes still pointed at the freed mask - the server crashed in nav_process_paths (Isla Inocentes) */
    if (n && g_driveMask) { size_t N = (size_t)g_grid->w * g_grid->h * NAV_LAYERS; int off = 0;
        for (size_t i = 0; i < N; i++) if (g_driveMask[i] && (g_grid->flags[i] & NAV_WATER)) { g_driveMask[i] = 0; off++; }
        g_driveCount -= off; if (off) mask_changed(); }
    return n;
}
/* the nearest bank a swimmer can climb: walkable, not water, 0.2-1.8 m above the surface, within 'radius' */
int navg_land_near(const float *pos, float radius, float *out)
{
    if (!navg_ready()) return 0;
    NavGrid *g = g_grid; int c = nav_cell_index(g, pos[0], pos[2]); if (c < 0) return 0;
    int r = (int)(radius / g->cell), cx0 = c % g->w, cz0 = c / g->w; float bd = 1e9f; int found = 0;
    for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) {
        int x = cx0 + dx, z = cz0 + dz; if (x < 0 || z < 0 || x >= g->w || z >= g->h) continue;
        float d = hypotf((float)dx, (float)dz) * g->cell; if (d > radius || d >= bd) continue;
        for (int l = 0; l < NAV_LAYERS; l++) { int nd = (z * g->w + x) * NAV_LAYERS + l;
            if (!(g->flags[nd] & NAV_WALKABLE) || (g->flags[nd] & NAV_WATER)) continue;
            float up = g->height[nd] - pos[1]; if (up < 0.2f || up > 1.8f) continue;
            int links = 0; for (int k = 0; k < 8; k++) if (g->edges[nd] & (1 << k)) links++;
            if (links < 4) continue;   /* a real floor, not a ledge */
            bd = d; out[0] = g->x0 + x * g->cell; out[1] = g->height[nd]; out[2] = g->z0 + z * g->cell; found = 1; break; }
    }
    return found;
}
/* 1.5: cells human players drove through are drivable, at the cheapest cost (class 1) */
/* 1.6: where humans drove is not only allowed but PREFERRED: class 6 (cost x0.3, against x0.5 for paved road and
   x1.0 for open ground), so a route a player showed - round the canal, over the right bridge - beats the
   straight line the drivable mask alone would give. Re-applied whenever the mask is rebuilt. */
void navg_learn_drive(const unsigned char *drive, int w, int h)
{
    g_lastDrive = drive; g_lastDriveW = w; g_lastDriveH = h;   /* NULL: the trails were freed (map change) */
    if (!navg_ready() || !g_driveMask || !drive || w != g_grid->w || h != g_grid->h) return;
    int added = 0, preferred = 0;
    for (int c = 0; c < w * h; c++) if (drive[c] >= 1)
        for (int l = 0; l < NAV_LAYERS; l++) { int node = c * NAV_LAYERS + l;
            if ((g_grid->flags[node] & NAV_WALKABLE) && !(g_grid->flags[node] & NAV_WATER)) {
                if (!g_driveMask[node]) added++;
                if (g_driveMask[node] != 6) { g_driveMask[node] = 6; preferred++; }
                break;
            } }
    if (preferred) mask_changed();
    static int logs; if (preferred && (logs++ < 4 || g_cfg.verbose)) rt_log("nav: %d cell(s) where human players drove are now preferred vehicle routes (%d of them newly opened)", preferred, added);
}
/* a vehicle got stuck here: take the spot off the drivable mask for this map (routes go round it) */
int navg_vehicle_block(const float *pos, float radius)
{
    if (!navg_ready() || !g_driveMask) return 0;
    int n = nav_mask_block(g_grid, g_driveMask, pos[0], pos[2], radius);
    if (n) mask_changed();
    return n;
}

/* Overwatch: walkable spots 'rmin'..'rmax' from target that see it, best first - upper floors and rooftops (for
   snipers and window shooters), then any raised ground. Uses the grid's stacked layers: a node with walkable floor
   below it in the same cell is an upper floor; one with floor above it is indoors (a window position). */
int navg_overwatch(const float *target, float rmin, float rmax, int upperOnly, float *out, int max)
{
    if (!navg_ready() || max <= 0) return 0;
    NavGrid *g = g_grid; const int L = NAV_LAYERS;
    typedef struct { float score, pos[3]; } Cand;
    static Cand cand[256]; int nc = 0;
    int r = (int)(rmax / g->cell) + 1, cx0 = (int)lroundf((target[0] - g->x0) / g->cell), cz0 = (int)lroundf((target[2] - g->z0) / g->cell);
    int step = r > 40 ? 2 : 1;
    for (int dz = -r; dz <= r; dz += step) for (int dx = -r; dx <= r; dx += step) {
        int cx = cx0 + dx, cz = cz0 + dz;
        if (cx < 0 || cz < 0 || cx >= g->w || cz >= g->h) continue;
        float x = g->x0 + cx * g->cell, z = g->z0 + cz * g->cell, d = hypotf(x - target[0], z - target[2]);
        if (d < rmin || d > rmax) continue;
        int cell = cz * g->w + cx; float low = 1e9f;
        for (int l = 0; l < L; l++) if (g->flags[cell * L + l] & NAV_WALKABLE) low = fminf(low, g->height[cell * L + l]);
        for (int l = 0; l < L; l++) {
            int node = cell * L + l;
            if (!(g->flags[node] & NAV_WALKABLE) || (g->flags[node] & NAV_WATER)) continue;
            int links = 0; for (int k = 0; k < 8; k++) if (g->edges[node] & (1 << k)) links++;
            if (links < 3) continue;
            float h = g->height[node], up = h - low, rel = h - target[1];
            int indoors = 0;
            for (int k = 0; k < L; k++) { int o = cell * L + k; if (k != l && (g->flags[o] & NAV_WALKABLE) && g->height[o] > h + 2.0f && g->height[o] < h + 6.0f) indoors = 1; }
            if (upperOnly && up < 2.5f) continue;
            if (!upperOnly && up < 2.5f && rel < 3.0f) continue;   /* flat ground level with the flag: no overwatch */
            float s = fminf(up, 12.0f) * 1.0f + fminf(fmaxf(rel, 0), 15.0f) * 0.6f + (indoors ? 6.0f : 0) - fabsf(d - (rmin + rmax) * 0.5f) * 0.05f;
            if (links >= 6) s += 1.0f;
            if (nc < 256) { cand[nc].score = s; cand[nc].pos[0] = x; cand[nc].pos[1] = h; cand[nc].pos[2] = z; nc++; }
            else { int worst = 0; for (int k = 1; k < nc; k++) if (cand[k].score < cand[worst].score) worst = k;
                   if (s > cand[worst].score) { cand[worst].score = s; cand[worst].pos[0] = x; cand[worst].pos[1] = h; cand[worst].pos[2] = z; } }
        }
    }
    /* best first, then line of sight from eye height to the flag (and spread them apart) */
    for (int i = 0; i < nc; i++) for (int j = i + 1; j < nc; j++) if (cand[j].score > cand[i].score) { Cand t = cand[i]; cand[i] = cand[j]; cand[j] = t; }
    int found = 0, tested = 0;
    for (int i = 0; i < nc && found < max && tested < 48; i++) {
        int close = 0; for (int k = 0; k < found; k++) if (hypotf(out[k * 3] - cand[i].pos[0], out[k * 3 + 2] - cand[i].pos[2]) < 8) close = 1;
        if (close) continue;
        tested++;
        float eye[3] = {cand[i].pos[0], cand[i].pos[1] + 1.5f, cand[i].pos[2]}, tg[3] = {target[0], target[1] + 1.2f, target[2]}, h[3], n[3];
        if (eng_ray(eye, tg, h, n) && hypotf(h[0] - tg[0], h[2] - tg[2]) > 3.0f) continue;
        memcpy(&out[found * 3], cand[i].pos, 12); found++;
    }
    return found;
}

void navg_tick(double now)
{
    if (!g_cfg.navBuild) return;
    nav_process_paths(1.5);
    if (g_cfg.vehicles && g_gridDone && g_stairsDone) dscan_step(600);
    if (g_failed) return;
    if (!g_grid) {
        /* wait until the level is settled and its layout is known */
        if (!g_world.boundsValid || now - g_world.levelSince < 8 || (!g_world.cpCount && now - g_world.levelSince < 40)) return;
        make_key();
        char pa[1200]; rt_nav_cache_path(g_key, pa, sizeof pa);
        g_grid = nav_load(pa);
        if (g_grid) {
            g_gridDone = 1; g_saved = 1; g_gridLevel = g_world.levelSerial;
            rt_log("nav: loaded cached grid %s (%dx%d cells of %.1f m)", g_key, g_grid->w, g_grid->h, g_grid->cell);
            return;
        }
        float m = 90.0f, cell = g_cfg.navCell;
        float x0 = g_world.boundsMin[0] - m, z0 = g_world.boundsMin[2] - m, x1 = g_world.boundsMax[0] + m, z1 = g_world.boundsMax[2] + m;
        float area = (x1 - x0) * (z1 - z0);
        while (area / (cell * cell) > 400000.0f) cell += 0.5f;
        nav_set_probe_range(g_world.boundsMax[1] + 150.0f, g_world.boundsMin[1] - 80.0f);
        g_grid = nav_create(g_key, x0, z0, x1, z1, cell);
        if (!g_grid) { g_failed = 1; rt_log("nav: grid allocation failed (%.0f x %.0f m)", x1 - x0, z1 - z0); return; }
        g_gridLevel = g_world.levelSerial; g_lastProgressLog = now;
        rt_log("nav: building grid %s over %.0f x %.0f m (%dx%d cells of %.1f m) with physics rays - bots walk straight with ray steering until it is done",
               g_key, x1 - x0, z1 - z0, g_grid->w, g_grid->h, cell);
        return;
    }
    if (g_gridDone) { navg_maintain(now); return; }
    if (nav_build_step(g_grid, nav_ray, 200000, g_cfg.navBudgetMs)) {
        g_gridDone = 1;
        rt_log("nav: grid %s complete - %u rays in %.1f s of build time", g_key, g_grid->raysCast, g_grid->buildMs / 1000.0);
        char pa[1200]; rt_nav_cache_path(g_key, pa, sizeof pa);
        g_saved = nav_save(g_grid, pa);
        rt_log("nav: %s %s", g_saved ? "saved to" : "could not save", pa);
    } else if (now - g_lastProgressLog > 30) {
        g_lastProgressLog = now;
        int n = g_grid->w * g_grid->h;
        float pct = g_grid->phase == 0 ? 30.0f * g_grid->cursor / n : 30.0f + 70.0f * g_grid->cursor / (4.0f * n);
        rt_log("nav: building %s %.0f%% (%u rays, %.1f s)", g_key, pct, g_grid->raysCast, g_grid->buildMs / 1000.0);
    }
}

int navg_path(int owner, const float *from, const float *to, float *out, int maxPts, int *deferred)
{
    if (deferred) *deferred = 0;
    if (!navg_ready()) return 0;
    nav_set_path_owner(owner);
    int n = nav_find_path(g_grid, nav_ray, from[0], from[1], from[2], to[0], to[1], to[2], out, maxPts);
    if (!n && nav_last_deferred() && deferred) *deferred = 1;
    nav_set_path_owner(-1);
    return n;
}

void navg_blocked(const float *pos, const float *towards)
{
    if (navg_ready()) nav_block_towards(g_grid, pos[0], pos[1], pos[2], towards[0], towards[2]);
}

int navg_floors(const float *pos, float *heights, int max) { return navg_ready() ? nav_floors_at(g_grid, pos[0], pos[1], pos[2], heights, max) : 0; }

void navg_status(char *out, unsigned size)
{
    if (!g_cfg.navBuild) { snprintf(out, size, "nav off"); return; }
    if (!g_grid) { snprintf(out, size, g_failed ? "nav failed" : "nav waiting for level layout"); return; }
    if (g_gridDone) { snprintf(out, size, "nav ready (%s%s, %u re-probes)", g_key, g_stairsDone ? "" : ", stair check running", g_reprobes); return; }
    int n = g_grid->w * g_grid->h;
    float pct = g_grid->phase == 0 ? 30.0f * g_grid->cursor / n : 30.0f + 70.0f * g_grid->cursor / (4.0f * n);
    snprintf(out, size, "nav building %.0f%%", pct);
}

/* 1.6.3: is this point on the big connected walkable area (not an upper-floor island the grid never linked)? */
int navg_main_area(const float *pos)
{
    if (!navg_ready()) return 0;
    static int *lab, *size, nlab; static const NavGrid *labFor; static double labAt;
    double now = rt_now();
    if (!lab || labFor != g_grid || now - labAt > 60) {
        if (lab) nav_mask_free((unsigned char *)lab); free(size); lab = NULL; size = NULL; nlab = 0;
        lab = nav_land_labels(g_grid); labFor = g_grid; labAt = now;
        if (!lab) return 0;
        int n = g_grid->w * g_grid->h * NAV_LAYERS;
        for (int i = 0; i < n; i++) if (lab[i] > nlab) nlab = lab[i];
        size = calloc((size_t)nlab + 1, sizeof(int)); if (!size) return 0;
        for (int i = 0; i < n; i++) size[lab[i]]++;
        { int big = 0, nb = 0; for (int i = 1; i <= nlab; i++) { if (size[i] > big) big = size[i]; if (size[i] >= 1500) nb++; }
          static int ll; if (ll++ < 6) rt_log("nav: %d walkable area(s), largest %d node(s), %d big one(s)", nlab, big, nb); }
    }
    int l = nav_label_at(g_grid, lab, pos[0], pos[1], pos[2]);
    return l > 0 && l <= nlab && size && size[l] >= 1500;
}

/* 1.6.9: where a swimmer gets out. navg_land_near took the nearest climbable bank within 45 m; on Panama Canal's quays
   (7 m walls) there often was none that close, or the nearest was a ledge cut off from the map, and the bot was
   redeployed after 25 s (38 times in one round). Now: banks up to 'radius' away, scored by distance plus a share of the
   way on from there to where the bot is going, and a bank on the big connected walkable area first. */
int navg_land_best(const float *pos, float radius, const float *goal, float *out)
{
    if (!navg_ready()) return 0;
    NavGrid *g = g_grid; int c = nav_cell_index(g, pos[0], pos[2]); if (c < 0) return 0;
    int r = (int)(radius / g->cell), cx0 = c % g->w, cz0 = c / g->w, found = 0; float best = 1e30f;
    for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) {
        int x = cx0 + dx, z = cz0 + dz; if (x < 0 || z < 0 || x >= g->w || z >= g->h) continue;
        float d = hypotf((float)dx, (float)dz) * g->cell; if (d > radius || d >= best) continue;
        for (int l = 0; l < NAV_LAYERS; l++) { int nd = (z * g->w + x) * NAV_LAYERS + l;
            if (!(g->flags[nd] & NAV_WALKABLE) || (g->flags[nd] & NAV_WATER)) continue;
            float up = g->height[nd] - pos[1]; if (up < 0.2f || up > 1.8f) continue;
            int links = 0; for (int k = 0; k < 8; k++) if (g->edges[nd] & (1 << k)) links++;
            if (links < 4) continue;   /* a real floor, not a ledge */
            float p[3] = {g->x0 + x * g->cell, g->height[nd], g->z0 + z * g->cell};
            float sc = d + (goal ? 0.3f * hypotf(goal[0] - p[0], goal[2] - p[2]) : 0) + (navg_main_area(p) ? 0 : 150.0f);
            if (sc < best) { best = sc; memcpy(out, p, 12); found = 1; }
        }
    }
    return found;
}
/* 1.6.9: the nearest walkable, dry node on the big connected area - where a bot on an island of the grid (rubble, a
   ledge, a roof the grid never linked) walks to so that it can take a route again */
int navg_main_near(const float *pos, float radius, float *out)
{
    if (!navg_ready()) return 0;
    NavGrid *g = g_grid; int c = nav_cell_index(g, pos[0], pos[2]); if (c < 0) return 0;
    int r = (int)(radius / g->cell), cx0 = c % g->w, cz0 = c / g->w;
    for (int ring = 1; ring <= r; ring++) {
        float bd = 1e30f; int found = 0;
        for (int dz = -ring; dz <= ring; dz++) for (int dx = -ring; dx <= ring; dx++) {
            if (abs(dx) != ring && abs(dz) != ring) continue;
            int x = cx0 + dx, z = cz0 + dz; if (x < 0 || z < 0 || x >= g->w || z >= g->h) continue;
            for (int l = 0; l < NAV_LAYERS; l++) { int nd = (z * g->w + x) * NAV_LAYERS + l;
                if (!(g->flags[nd] & NAV_WALKABLE) || (g->flags[nd] & NAV_WATER) || !g->edges[nd]) continue;
                float p[3] = {g->x0 + x * g->cell, g->height[nd], g->z0 + z * g->cell};
                float dd = hypotf(p[0] - pos[0], p[2] - pos[2]) + fabsf(p[1] - pos[1]) * 2.0f;
                if (dd < bd && navg_main_area(p)) { bd = dd; memcpy(out, p, 12); found = 1; }
            }
        }
        if (found) return 1;
    }
    return 0;
}

/* 1.6.3: is this point on the lowest walkable surface of its grid cell (the ground, not a balcony or an upper floor)? */
int navg_on_ground(const float *pos)
{
    if (!navg_ready()) return 0;
    int c = nav_cell_index(g_grid, pos[0], pos[2]); if (c < 0) return 0;
    float lo = 1e9f;
    for (int l = 0; l < NAV_LAYERS; l++) { int nd = c * NAV_LAYERS + l; if ((g_grid->flags[nd] & NAV_WALKABLE) && g_grid->height[nd] < lo) lo = g_grid->height[nd]; }
    return lo < 1e8f && pos[1] - lo < 1.2f;
}
