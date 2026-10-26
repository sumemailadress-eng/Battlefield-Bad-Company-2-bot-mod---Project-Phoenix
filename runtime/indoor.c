/* Project Phoenix BC2 1.6.3 - routes into buildings.
   The level grid is 2.5 m per cell. A BC2 house's stairwell is ~1 m wide and turns back on itself, and its doors are
   physics panels the grid's rays see as wall, so an M-COM upstairs is often not connected to the grid at all ("path
   0/0", ten bots at the foot of the stairs on White Pass). For each such target Phoenix surveys the building itself
   at 0.5 m with rays: from the target's floor outwards - floor under each step (stairs rise <= 0.55 m a step),
   head room, no wall in between - until it is out under the open sky on the level grid. A closed door is a thin panel
   with floor on both sides: the survey goes through it and marks it, and bots knife it open on the way.
   The result is a chain of points from the door outside to the target that bots walk instead of the grid route. */
#ifdef PHX_HARNESS
#include "harness_win.h"
#else
#include "win_common.h"
#endif
#include "rt.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define IN_STEP     0.5f
#define IN_HALF     48          /* columns each side of the target: 24 m */
#define IN_SIDE     (2 * IN_HALF + 1)
#define IN_MAXN     9000
#define IN_LAYERS   6
#define IN_ROUTES   8
#define IN_MAXPTS   48

typedef struct INode { short ix, iz; float y; int parent; unsigned char door, depth, edge; } INode;
typedef struct IRoute {
    int state;                  /* 0 free, 1 surveying, 2 done (route), 3 done (no way found) */
    float target[3];
    unsigned level;
    double at;
    int n; float pts[IN_MAXPTS * 3]; unsigned char door[IN_MAXPTS];
    int doors; float exitPos[3];
} IRoute;

static IRoute g_routes[IN_ROUTES];
static int g_active = -1;
/* survey state of the active route */
static INode *g_n; static int g_nn, g_head, g_doorHead;
static int *g_queue, g_qn, *g_doorQ, g_dqn, *g_edgeQ, g_eqn, g_edgeHead;
static int (*g_col)[IN_LAYERS];   /* node ids per column */
static unsigned char *g_colN;
static float g_floor0;
static unsigned g_rays;

static int ray(const float *a, const float *b, float *hit) { float h[3], nrm[3]; g_rays++; int r = eng_ray(a, b, h, nrm); if (r && hit) memcpy(hit, h, 12); return r; }
static void npos(const INode *n, const float *t, float *o) { o[0] = t[0] + n->ix * IN_STEP; o[1] = n->y; o[2] = t[2] + n->iz * IN_STEP; }

/* the floor at (x, z) from height y: the first surface hit going down from y + 0.9 */
static int floor_at(float x, float y, float z, float *fy)
{
    float a[3] = {x, y + 0.9f, z}, b[3] = {x, y - 1.3f, z}, h[3];
    if (!ray(a, b, h)) return 0;
    if (h[1] > y + 0.8f) return 0;   /* the ray started inside something */
    *fy = h[1]; return 1;
}
static int headroom(float x, float fy, float z) { float a[3] = {x, fy + 0.25f, z}, b[3] = {x, fy + 1.7f, z}; return !ray(a, b, NULL); }
static int open_sky(float x, float fy, float z) { float a[3] = {x, fy + 1.9f, z}, b[3] = {x, fy + 45.0f, z}; return !ray(a, b, NULL); }
/* how thick the obstacle is along a ray a -> b (0 = no hit, -1 = no exit hit) */
static float thickness(const float *a, const float *b)
{
    float h1[3], h2[3];
    if (!ray(a, b, h1)) return 0;
    if (!ray(b, a, h2)) return -1;
    float dx = h1[0] - h2[0], dz = h1[2] - h2[2]; return sqrtf(dx * dx + dz * dz);
}
/* a wall between two points (knee and chest height): 1 = wall, 2 = a closed door - a thin panel (under 0.2 m) set in
   something different beside it (a frame, a thicker wall, or open): a plain thin wall reads the same 0.7 m to each side */
static int wall_between(const float *p, const float *q)
{
    int blocked = 0, thin = 1; float tc = 0;
    for (int k = 0; k < 2; k++) {
        float hgt = k ? 1.25f : 0.55f;
        float a[3] = {p[0], p[1] + hgt, p[2]}, b[3] = {q[0], q[1] + hgt, q[2]};
        float t = thickness(a, b);
        if (t == 0) continue;
        blocked = 1;
        if (t < 0 || t > 0.2f) thin = 0; else if (t > tc) tc = t;
    }
    if (!blocked) return 0;
    if (!thin) return 1;
    float dx = q[0] - p[0], dz = q[2] - p[2], len = sqrtf(dx * dx + dz * dz); if (len < 1e-3f) return 1;
    float sx = -dz / len * 0.7f, sz = dx / len * 0.7f;
    int differ = 0;
    for (int side = -1; side <= 1; side += 2) {
        float a[3] = {p[0] + sx * side, p[1] + 0.9f, p[2] + sz * side}, b[3] = {q[0] + sx * side, q[1] + 0.9f, q[2] + sz * side};
        float t = thickness(a, b);
        if (t == 0 || t < 0 || fabsf(t - tc) > 0.1f) differ = 1;
    }
    return differ ? 2 : 1;
}
static int col_index(int ix, int iz) { return (iz + IN_HALF) * IN_SIDE + (ix + IN_HALF); }
static int find_node(int ix, int iz, float y)
{
    int c = col_index(ix, iz);
    for (int k = 0; k < g_colN[c]; k++) { int id = g_col[c][k]; if (fabsf(g_n[id].y - y) < 0.45f) return id; }
    return -1;
}
static int add_node(int ix, int iz, float y, int parent, int door)
{
    int c = col_index(ix, iz);
    if (g_nn >= IN_MAXN || g_colN[c] >= IN_LAYERS) return -1;
    INode *n = &g_n[g_nn]; n->ix = (short)ix; n->iz = (short)iz; n->y = y; n->parent = parent; n->door = (unsigned char)door;
    n->depth = parent >= 0 ? (unsigned char)(g_n[parent].depth < 250 ? g_n[parent].depth + 1 : 250) : 0;
    g_col[c][g_colN[c]++] = g_nn;
    return g_nn++;
}
static void survey_free(void)
{
    free(g_n); free(g_queue); free(g_doorQ); free(g_edgeQ); free(g_col); free(g_colN);
    g_n = NULL; g_queue = g_doorQ = g_edgeQ = NULL; g_col = NULL; g_colN = NULL; g_active = -1;
}
static int survey_alloc(void)
{
    g_n = malloc(sizeof *g_n * IN_MAXN); g_queue = malloc(sizeof(int) * IN_MAXN); g_doorQ = malloc(sizeof(int) * IN_MAXN); g_edgeQ = malloc(sizeof(int) * IN_MAXN);
    g_col = malloc(sizeof *g_col * IN_SIDE * IN_SIDE); g_colN = calloc(IN_SIDE * IN_SIDE, 1);
    if (!g_n || !g_queue || !g_doorQ || !g_edgeQ || !g_col || !g_colN) { survey_free(); return 0; }
    g_nn = g_head = g_qn = g_dqn = g_doorHead = g_eqn = g_edgeHead = 0; return 1;
}
/* exit: under open sky and on the level grid there (so the grid route can bring bots to it) */
static int is_exit(const float *p)
{
    if (!open_sky(p[0], p[1], p[2])) return 0;
    float sn[3]; if (!navg_snap(p, sn)) return 0;
    return hypotf(sn[0] - p[0], sn[2] - p[2]) < 1.6f && fabsf(sn[1] - p[1]) < 0.8f && navg_on_ground(sn);
}
static void finish(IRoute *r, int exitNode)
{
    const float *t = r->target;
    if (exitNode < 0) { r->state = 3; rt_log("indoor: no way found from the open to (%.0f, %.0f, %.0f) - %d point(s) surveyed, %u ray(s)", t[0], t[1], t[2], g_nn, g_rays); survey_free(); return; }
    /* exit -> target, keeping the points where the way turns, climbs or passes a door */
    int chain[512], cn = 0;
    for (int id = exitNode; id >= 0 && cn < 512; id = g_n[id].parent) chain[cn++] = id;
    float pts[512][3]; unsigned char dr[512]; int pn = 0, doors = 0;
    for (int k = 0; k < cn; k++) {
        float p[3]; npos(&g_n[chain[k]], t, p);
        int door = g_n[chain[k]].door; if (door) doors++;
        if (pn >= 2 && !door && !dr[pn - 1] && k < cn - 1) {
            float *a = pts[pn - 2], *b = pts[pn - 1];
            float d1x = b[0] - a[0], d1z = b[2] - a[2], d2x = p[0] - b[0], d2z = p[2] - b[2];
            int straight = fabsf(d1x * d2z - d1z * d2x) < 0.01f && d1x * d2x + d1z * d2z > 0;
            int flat = fabsf(p[1] - b[1]) < 0.15f && fabsf(b[1] - a[1]) < 0.15f;
            if (straight && flat && hypotf(p[0] - a[0], p[2] - a[2]) < 6) { memcpy(pts[pn - 1], p, 12); dr[pn - 1] = 0; continue; }
        }
        memcpy(pts[pn], p, 12); dr[pn] = (unsigned char)door; pn++;
    }
    /* thin out stair steps if there are too many points (keep doors, ends) */
    while (pn > IN_MAXPTS) {
        int w = 0; for (int k = 0; k < pn; k++) { if (k > 0 && k < pn - 1 && (k & 1) && !dr[k]) continue; memcpy(pts[w], pts[k], 12); dr[w] = dr[k]; w++; }
        if (w == pn) break; pn = w;
    }
    if (pn > IN_MAXPTS) pn = IN_MAXPTS;
    r->n = pn; for (int k = 0; k < pn; k++) { memcpy(&r->pts[k * 3], pts[k], 12); r->door[k] = dr[k]; }
    r->doors = doors; memcpy(r->exitPos, pts[0], 12); r->state = 2;
    float rise = pts[pn - 1][1] - pts[0][1];
    rt_log("indoor: way in to (%.0f, %.0f, %.0f) found - from (%.0f, %.0f, %.0f), %d point(s), climbs %.1f m, %d door(s) to knife; %d point(s) surveyed, %u ray(s)",
           t[0], t[1], t[2], pts[0][0], pts[0][1], pts[0][2], pn, rise, doors, g_nn, g_rays);
#ifdef PHX_HARNESS
    if (getenv("INDBG")) for (int k = 0; k < pn; k++) rt_log("INDBG chain %d: (%.1f, %.2f, %.1f)%s", k, pts[k][0], pts[k][1], pts[k][2], dr[k] ? " door" : "");
#endif
    survey_free();
}
static void survey_step(unsigned budget)
{
    IRoute *r = &g_routes[g_active]; const float *t = r->target;
    unsigned start = g_rays;
    static const int dir[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    while (g_rays - start < budget) {
        int cur;
        if (g_head < g_qn) cur = g_queue[g_head++];
        else if (g_edgeHead < g_eqn) cur = g_edgeQ[g_edgeHead++];   /* by a drop (a stair's open side, a balcony) after the middle */
        else if (g_doorHead < g_dqn) cur = g_doorQ[g_doorHead++];   /* through a door only when there is no other way */
        else { finish(r, -1); return; }
        INode c = g_n[cur]; float cp[3]; npos(&c, t, cp);
#ifdef PHX_HARNESS
        if (getenv("INDBG2") && c.depth >= 5 && c.depth <= 6) { float sn[3]; int sk = open_sky(cp[0], cp[1], cp[2]), snp = navg_snap(cp, sn);
            rt_log("INDBG2 node d%d (%.1f %.2f %.1f) edge %d sky %d snap %d (%.1f %.2f %.1f) main %d", c.depth, cp[0], cp[1], cp[2], c.edge, sk, snp, sn[0], sn[1], sn[2], snp ? navg_main_area(sn) : -1); }
#endif
        if (c.depth > 4 && is_exit(cp)) { finish(r, cur); return; }
        for (int d = 0; d < 4; d++) {
            int nx = c.ix + dir[d][0], nz = c.iz + dir[d][1];
            if (nx < -IN_HALF || nx > IN_HALF || nz < -IN_HALF || nz > IN_HALF) continue;
            float x = t[0] + nx * IN_STEP, z = t[2] + nz * IN_STEP, fy;
            if (!floor_at(x, c.y, z, &fy)) continue;
            if (fabsf(fy - c.y) > 0.55f) continue;   /* a stair step (the way is walked back in, so up and down alike) */
            if (find_node(nx, nz, fy) >= 0) continue;
            if (!headroom(x, fy, z)) continue;
            float np[3] = {x, fy, z};
            int w = wall_between(cp, np);
            if (w == 1) continue;
            int id = add_node(nx, nz, fy, cur, w == 2);
            if (id < 0) continue;
            /* next to a drop: a neighbour column with no floor within a step of this one */
            int edge = 0;
            for (int e = 0; e < 4 && !edge; e++) { float ex = x + dir[e][0] * IN_STEP, ez = z + dir[e][1] * IN_STEP, ey;
                if (!floor_at(ex, fy, ez, &ey) || fy - ey > 0.7f) edge = 1; }
            g_n[id].edge = (unsigned char)edge;
            if (w == 2) g_doorQ[g_dqn++] = id; else if (edge) g_edgeQ[g_eqn++] = id; else g_queue[g_qn++] = id;
        }
        if (g_nn >= IN_MAXN - 4) { finish(r, -1); return; }
    }
}

/* ---- API ---- */
void indoor_reset(void) { survey_free(); memset(g_routes, 0, sizeof g_routes); }
static IRoute *route_for(const float *target)
{
    for (int i = 0; i < IN_ROUTES; i++) if (g_routes[i].state && g_routes[i].level == g_world.levelSerial &&
        hypotf(g_routes[i].target[0] - target[0], g_routes[i].target[2] - target[2]) < 2.5f && fabsf(g_routes[i].target[1] - target[1]) < 2.0f) return &g_routes[i];
    return NULL;
}
/* ask for a way in to target (a no-op when one is known or being surveyed) */
void indoor_request(const float *target, const char *why)
{
    if (route_for(target)) return;   /* (waits for the level grid in indoor_tick) */
    int slot = -1; double oldest = 1e30;
    for (int i = 0; i < IN_ROUTES; i++) { if (!g_routes[i].state || g_routes[i].level != g_world.levelSerial) { slot = i; break; } if (g_routes[i].state != 1 && g_routes[i].at < oldest) { oldest = g_routes[i].at; slot = i; } }
    if (slot < 0) return;
    IRoute *r = &g_routes[slot]; memset(r, 0, sizeof *r);
    memcpy(r->target, target, 12); r->level = g_world.levelSerial; r->at = rt_now(); r->state = 1;
    static int logs; if (logs++ < 20) rt_log("indoor: surveying the way in to (%.0f, %.0f, %.0f) - %s", target[0], target[1], target[2], why);
}
void indoor_tick(void)
{
    if (g_active < 0) {
        if (!navg_ready()) return;
        for (int i = 0; i < IN_ROUTES; i++) if (g_routes[i].state == 1) {
            if (g_routes[i].level != g_world.levelSerial) { g_routes[i].state = 0; continue; }
            if (!survey_alloc()) return;
            g_active = i; g_rays = 0;
            /* seeds: the floor round the target (the M-COM crate itself is no floor), within half a metre of its base */
            const float *t = g_routes[i].target; int seeds = 0;
            static const int ring[9][2] = {{0, 0}, {2, 0}, {-2, 0}, {0, 2}, {0, -2}, {2, 2}, {-2, 2}, {2, -2}, {-2, -2}};
            for (int r = 0; r < 9; r++) { float fy, x = t[0] + ring[r][0] * IN_STEP, z = t[2] + ring[r][1] * IN_STEP;
                if (!floor_at(x, t[1] + 0.3f, z, &fy) || fabsf(fy - t[1]) > 0.6f || !headroom(x, fy, z)) continue;
                int id = add_node(ring[r][0], ring[r][1], fy, -1, 0); if (id >= 0) { g_queue[g_qn++] = id; seeds++; } }
            if (!seeds) { int id = add_node(0, 0, t[1], -1, 0); if (id >= 0) g_queue[g_qn++] = id; }
            g_floor0 = t[1];
            break;
        }
        if (g_active < 0) return;
    }
    if (g_routes[g_active].level != g_world.levelSerial) { g_routes[g_active].state = 0; survey_free(); return; }
    survey_step(500);   /* ~3 ms a tick live (the level grid builds with ~5) */
}
/* the way in to target: 1 = a route (out: points exit -> target, doors), 0 = none / not yet */
int indoor_route(const float *target, const float **pts, const unsigned char **doors)
{
    IRoute *r = route_for(target);
    if (!r || r->state != 2) return 0;
    *pts = r->pts; *doors = r->door; return r->n;
}
int indoor_known(const float *target) { IRoute *r = route_for(target); return r ? r->state : 0; }
