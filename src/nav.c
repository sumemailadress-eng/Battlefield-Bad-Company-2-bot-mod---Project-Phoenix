// Automatic navigation grid: every cell is probed with downward rays that peel off up to NAV_LAYERS stacked
// surfaces (ground height + slope + headroom), every neighbour pair with two horizontal rays (walls/fences) and a
// step-height check. Paths are planned with A* over (cell, layer) nodes and string-pulled.
#include "phoenix_platform.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include "nav.h"

#define PROBE_TOP        g_probeTop     // start height of ground probes (set per level by the host)
#define PROBE_BOTTOM     g_probeBottom
static float g_probeTop = 1200.0f, g_probeBottom = -200.0f;
void nav_set_probe_range(float top, float bottom) { if (top > bottom + 10) { g_probeTop = top; g_probeBottom = bottom; } }
#define PEEL_OFFSET      0.25f    // restart the next downward probe this far below a found surface
#define MIN_NORMAL_Y     0.70f    // ~45 degrees steepest walkable slope
#define MIN_HEADROOM     1.8f     // standing room needed under the surface above
#define MAX_STEP_UP      0.9f     // vertical step a soldier can walk up between adjacent cells
#define MAX_DROP         4.0f     // ledge a soldier may walk off (one-way link down), e.g. hangar deck -> sea
#define MAX_STAIR_UP     1.6f     // neighbours this far apart connect if the ground between them is a staircase
#define MAX_STAIR_STEP   0.6f     // largest single rise between the 0.5 m samples of a staircase
#define KNEE_HEIGHT      0.6f     // clearance rays between cells
#define CHEST_HEIGHT     1.4f
#define NAV_FILE_MAGIC   0x3556414e  // "NAV5" (NAV4 + water flags, fence crossings, top 2 + bottom 4 surfaces)
#define NAV_FILE_MAGIC4  0x3456414e  // "NAV4" (still loads: no fences / water)
#define FENCE_COST       40.0f       // m of walking a knifed fence crossing is worth: go round short fences, cut long ones
#define WATER_COST_MUL   12.0f       // swimming (1.5.2: 4 sent Valparaiso's defenders across the bay - they could not climb out)
#define L                NAV_LAYERS

static const int kDir[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
static const unsigned char *g_pathMask;   // vehicles: only these nodes (NULL = soldiers)
/* 1.6.6: links a wheel cannot cross (the vehicle scan: per node bit0 = its +x link, bit1 = its +z link). 1.6.5 only
   made the nodes beside a bad link dear or took them off, so a route could still cross a divider between two dear nodes */
static const unsigned char *g_badLinks; static const NavGrid *g_badLinksGrid;
static float g_vehicleWidth = 1.5f;
static float g_vehicleAvoid[4];
void nav_set_vehicle_avoid(const float *position,float radius)
{
    memset(g_vehicleAvoid,0,sizeof g_vehicleAvoid);
    if(position && isfinite(radius) && radius>0 && isfinite(position[0]) && isfinite(position[1]) && isfinite(position[2])){
        memcpy(g_vehicleAvoid,position,12);g_vehicleAvoid[3]=fminf(radius,6);
    }
}
static int vehicle_avoids_point(float x,float y,float z)
{
    float dx=x-g_vehicleAvoid[0],dz=z-g_vehicleAvoid[2];
    return g_pathMask && g_vehicleAvoid[3]>0 && fabsf(y-g_vehicleAvoid[1])<4 && dx*dx+dz*dz<g_vehicleAvoid[3]*g_vehicleAvoid[3];
}
void nav_set_vehicle_width(float width) { g_vehicleWidth = isfinite(width) ? fmaxf(0.8f,fminf(2.5f,width)) : 1.5f; }
static int g_longLegs;                    // boats: straight lines across open water
void nav_set_long_legs(int on) { g_longLegs = on; }
void nav_set_path_mask(const unsigned char *mask) { g_pathMask = mask; }
static const unsigned char *g_treeDistSet;
static const NavGrid *g_treeGrid;   // the grid the table belongs to (a table from another map is never read)
static int g_treeW, g_treeH;
void nav_set_tree_dist(const NavGrid *g, const unsigned char *dist) { g_treeGrid = g; g_treeDistSet = dist; g_treeW = g ? g->w : 0; g_treeH = g ? g->h : 0; }
#define TREE_DIST(g) ((g) == g_treeGrid && (g)->w == g_treeW && (g)->h == g_treeH ? g_treeDistSet : NULL)
#define TREE_NEAR_DM 12   // soldiers: within 1.2 m of a trunk
static const unsigned char *g_roadMaskSet;   // soldiers: road classes (cheaper along roads)
static const NavGrid *g_roadGrid;
static int g_roadW, g_roadH;
void nav_set_road_mask(const NavGrid *g, const unsigned char *mask) { g_roadGrid = g; g_roadMaskSet = mask; g_roadW = g ? g->w : 0; g_roadH = g ? g->h : 0; }
#define ROAD_MASK(g) ((g) == g_roadGrid && (g)->w == g_roadW && (g)->h == g_roadH ? g_roadMaskSet : NULL)
// Phoenix: learned danger per node (0..255) - soldiers' routes avoid where bots keep dying (cost x1 .. x6.3)
static const unsigned char *g_dangerSet; static const NavGrid *g_dangerGrid; static int g_dangerW, g_dangerH;
void nav_set_danger(const NavGrid *g, const unsigned char *d) { g_dangerGrid = g; g_dangerSet = d; g_dangerW = g ? g->w : 0; g_dangerH = g ? g->h : 0; }
#define DANGER(g) ((g) == g_dangerGrid && (g)->w == g_dangerW && (g)->h == g_dangerH ? g_dangerSet : NULL)
#define DANGER_MUL(dg, n) ((dg) ? 1.0f + (dg)[n] * (1.0f / 48.0f) : 1.0f)

static void *nav_alloc(size_t n) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n); }
static unsigned g_gridSerial;
static NavGrid *new_grid(void)
{
    NavGrid *g = nav_alloc(sizeof *g);
    if (!g_gridSerial) g_gridSerial = GetTickCount() << 8;   // a hot reload restarts the count while the old grid lives on
    if (g) g->serial = ++g_gridSerial;
    return g;
}
static void nav_release(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }

static struct {const NavGrid *grid;int left,top,width,height;}g_expanded;
static int valid_grid_size(int w,int h)
{
    return w>0 && h>0 && w<=8192 && h<=8192 && (uint64_t)w*(uint64_t)h<=4000000;
}
static int alloc_arrays(NavGrid *g)
{
    if(!g || !valid_grid_size(g->w,g->h))return 0;
    size_t n = (size_t)g->w * g->h * L;
    g->height = nav_alloc(n * sizeof(float));
    g->flags = nav_alloc(n);
    g->edges = nav_alloc(n);
    g->fences = nav_alloc(n);
    return g->height && g->flags && g->edges && g->fences;
}

NavGrid *nav_create(const char *level, float x0, float z0, float x1, float z1, float cell)
{
    if(!level || !isfinite(x0) || !isfinite(z0) || !isfinite(x1) || !isfinite(z1) ||
       !isfinite(cell) || cell<0.5f || cell>16 || fabsf(x1-x0)/cell>8192 || fabsf(z1-z0)/cell>8192)return NULL;
    NavGrid *g = new_grid();if(!g)return NULL;
    strncpy(g->level, level, sizeof g->level - 1);
    g->cell = cell;
    g->x0 = (x0 < x1 ? x0 : x1) + cell * 0.5f;
    g->z0 = (z0 < z1 ? z0 : z1) + cell * 0.5f;
    g->w = (int)(fabsf(x1 - x0) / cell);
    g->h = (int)(fabsf(z1 - z0) / cell);
    if(!alloc_arrays(g)){nav_free(g);return NULL;}
    return g;
}

int nav_covers(const NavGrid *g,float x,float z,float margin)
{
    return g && isfinite(x) && isfinite(z) && isfinite(margin) &&
        x>=g->x0+margin && z>=g->z0+margin &&
        x<=g->x0+(g->w-1)*g->cell-margin && z<=g->z0+(g->h-1)*g->cell-margin;
}
NavGrid *nav_expand_bounds(const NavGrid *old,float x0,float z0,float x1,float z1)
{
    if(!old || old->phase!=2 || !valid_grid_size(old->w,old->h) || !old->height || !old->flags || !old->edges || !old->fences ||
       !isfinite(x0) || !isfinite(z0) || !isfinite(x1) || !isfinite(z1) ||
       !isfinite(old->x0) || !isfinite(old->z0) || old->cell<0.5f || !isfinite(old->cell))return NULL;
    float c=old->cell;
    float nl=ceilf(fmaxf(0,old->x0-x0)/c),nt=ceilf(fmaxf(0,old->z0-z0)/c);
    float nr=ceilf(fmaxf(0,x1-(old->x0+(old->w-1)*c))/c),nb=ceilf(fmaxf(0,z1-(old->z0+(old->h-1)*c))/c);
    if(nl>8192 || nt>8192 || nr>8192 || nb>8192)return NULL;
    int left=(int)nl,top=(int)nt,w=old->w+left+(int)nr,h=old->h+top+(int)nb;
    if(!valid_grid_size(w,h))return NULL;
    if(w==old->w && h==old->h)return NULL;
    NavGrid *g=new_grid();if(!g)return NULL;
    memcpy(g->level,old->level,sizeof g->level);g->cell=c;g->w=w;g->h=h;
    g->x0=old->x0-left*c;g->z0=old->z0-top*c;
    if(!alloc_arrays(g)){nav_free(g);return NULL;}
    for(int z=0;z<old->h;z++){
        size_t from=(size_t)z*old->w*L,to=((size_t)(z+top)*w+left)*L,n=(size_t)old->w*L;
        memcpy(g->height+to,old->height+from,n*sizeof(float));
        memcpy(g->flags+to,old->flags+from,n);memcpy(g->edges+to,old->edges+from,n);memcpy(g->fences+to,old->fences+from,n);
    }
    g_expanded.grid=g;g_expanded.left=left;g_expanded.top=top;g_expanded.width=old->w;g_expanded.height=old->h;
    return g;
}
static int expansion_old_cell(const NavGrid *g,int cell,int inset)
{
    if(g_expanded.grid!=g)return 0;
    int x=cell%g->w,z=cell/g->w;
    return x>=g_expanded.left+inset && x<g_expanded.left+g_expanded.width-inset &&
           z>=g_expanded.top+inset && z<g_expanded.top+g_expanded.height-inset;
}
static void nav_blocks_forget(const NavGrid *g);
static unsigned char *g_stairOk;          // bit d: the link in straight direction d is a real staircase up (re-checked)
static const NavGrid *g_stairOkGrid;
static int g_stairOkW, g_stairOkH;
void nav_free(NavGrid *g)
{
    if (!g) return;
    if(g_expanded.grid==g)memset(&g_expanded,0,sizeof g_expanded);
    if (g_stairOkGrid == g) { nav_release(g_stairOk); g_stairOk = NULL; g_stairOkGrid = NULL; }
    nav_cancel_paths();
    nav_blocks_forget(g);   // its stuck-spot blocks die with it (a new map's grid can land at the same address)
    nav_release(g->height);
    nav_release(g->flags);
    nav_release(g->edges);
    nav_release(g->fences);
    nav_release(g);
}

static double now_ms(void)
{
    static LARGE_INTEGER f;
    LARGE_INTEGER c;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

static void cell_pos(const NavGrid *g, int cx, int cz, float *x, float *z)
{
    *x = g->x0 + cx * g->cell;
    *z = g->z0 + cz * g->cell;
}

int nav_cell_index(const NavGrid *g, float x, float z)
{
    if(!g || !isfinite(x) || !isfinite(z) || !isfinite(g->cell) || g->cell<=0)return -1;
    double fx=floor(((double)x-g->x0)/g->cell+0.5),fz=floor(((double)z-g->z0)/g->cell+0.5);
    if(!isfinite(fx) || !isfinite(fz) || fx<0 || fz<0 || fx>=g->w || fz>=g->h)return -1;
    int cx=(int)fx,cz=(int)fz;
    return cz * g->w + cx;
}

// Peels surfaces top-down (up to MAX_PEELS rays) and keeps the first L *walkable* ones: flat enough and with headroom
// under the surface above. Non-walkable surfaces (vehicle wrecks, railings, roof edges) still limit headroom but do not
// use up a layer, so the ground under an overpass next to a wreck is still found.
#define MAX_PEELS 16
#define MAX_SURFACES 16
static NavRayFn g_rayNoTerrain;   // same ray ignoring terrain: tells terrain hits apart (nothing walkable lies under terrain)
void nav_set_terrain_test(NavRayFn rayIgnoringTerrain) { g_rayNoTerrain = rayIgnoringTerrain; }
static NavHitClassFn g_classify;
void nav_set_hit_classifier(NavHitClassFn fn) { g_classify = fn; }

// A cell with more walkable surfaces than layers (a bridge over a road over a basement) keeps the top 2 and the
// bottom L-2: the deck you walk on and the ground under it matter more than the middle storeys. Water ends the probe:
// nothing under its surface is walked on.
static int probe_ground(NavGrid *g, NavRayFn ray, int cell)
{
    float x, z, p[3], n[3], top = PROBE_TOP, above = 1e30f, hs[MAX_SURFACES];
    unsigned char wet[MAX_SURFACES];
    cell_pos(g, cell % g->w, cell / g->w, &x, &z);
    int rays = 0, ns = 0;
    for (int peel = 0; peel < MAX_PEELS && ns < MAX_SURFACES; peel++) {
        float a[3] = {x, top, z}, b[3] = {x, PROBE_BOTTOM, z};
        rays++;
        if (!ray(a, b, p, n)) break;
        int water = g_classify && g_classify() == NAV_HIT_WATER;
        if (water || (n[1] >= MIN_NORMAL_Y && above - p[1] >= MIN_HEADROOM)) { hs[ns] = p[1]; wet[ns] = (unsigned char)water; ns++; }
        if (water) break;
        above = p[1];
        top = p[1] - PEEL_OFFSET;
        // Terrain is a one-sided heightfield: peeling on would find the sea plane that continues under the land.
        if (g_rayNoTerrain) {
            float q[3];
            rays++;
            if (!g_rayNoTerrain(a, b, q, NULL) || q[1] < p[1] - 0.05f) break;
        }
    }
    for (int k = 0, l = 0; k < ns && l < L; k++) {
        if (ns > L && k >= 2 && k < ns - (L - 2)) continue;   // a middle storey
        int node = cell * L + l++;
        g->height[node] = hs[k];
        g->flags[node] |= NAV_WALKABLE | (wet[k] ? NAV_WATER : 0);
    }
    return rays;
}

// Layer of neighbour cell `nc` that node (c, l) connects to: the walkable one within a step, closest in height.
static int neighbour_layer(const NavGrid *g, int c, int l, int nc, int diagonal)
{
    if(c<0 || nc<0 || c>=g->w*g->h || nc>=g->w*g->h || l<0 || l>=L)return -1;
    float h = g->height[c * L + l], best = MAX_STEP_UP * (diagonal ? 1.41f : 1.0f);
    int bl = -1;
    for (int k = 0; k < L; k++) {
        int nn = nc * L + k;
        if (!(g->flags[nn] & NAV_WALKABLE)) continue;
        float dh = fabsf(g->height[nn] - h);
        if (dh <= best) { best = dh; bl = k; }
    }
    return bl;
}

// Layer of neighbour cell `nc` that is up a staircase from node (c, l): the closest walkable surface between
// MAX_STEP_UP and MAX_STAIR_UP above it.
static int stair_layer(const NavGrid *g, int c, int l, int nc)
{
    if(c<0 || nc<0 || c>=g->w*g->h || nc>=g->w*g->h || l<0 || l>=L)return -1;
    float h = g->height[c * L + l], best = MAX_STAIR_UP;
    int bl = -1;
    for (int k = 0; k < L; k++) {
        int nn = nc * L + k;
        if (!(g->flags[nn] & NAV_WALKABLE)) continue;
        float dh = g->height[nn] - h;
        if (dh > MAX_STEP_UP && dh <= best) { best = dh; bl = k; }
    }
    return bl;
}

// Ground between (ax, ha, az) and (bx, hb, bz) rises in steps a soldier walks up: samples at 1/4, 1/2, 3/4, rising
// gently - and at least two of them part-way up (a wall face shows only the bottom and the top: gabions, kerbs).
static int stairs_between(NavRayFn ray, float ax, float ha, float az, float bx, float hb, float bz)
{
    float prev = ha;
    int between = 0;
    for (int k = 1; k <= 4; k++) {
        float t = k * 0.25f, h;
        if (k < 4) {
            float x = ax + (bx - ax) * t, z = az + (bz - az) * t, p[3], n[3];
            float a[3] = {x, hb + 1.0f, z}, b[3] = {x, ha - 1.0f, z};
            if (!ray(a, b, p, n) || n[1] < MIN_NORMAL_Y) return 0;
            h = p[1];
            if (h > ha + 0.15f && h < hb - 0.15f) between++;
        } else h = hb;
        if (h - prev > MAX_STAIR_STEP || h < prev - 0.3f) return 0;
        prev = h;
    }
    return between >= 2;
}

// Knee and chest rays between two ground points shifted sideways by `off` (m): clear?
static int pair_clear(NavRayFn ray, float ax, float ha, float az, float bx, float hb, float bz, float off)
{
    float dx = bx - ax, dz = bz - az, len = sqrtf(dx * dx + dz * dz);
    float px = -dz / len * off, pz = dx / len * off;
    const float heights[2] = {KNEE_HEIGHT, CHEST_HEIGHT};
    for (int k = 0; k < 2; k++) {
        float a[3] = {ax + px, ha + heights[k], az + pz}, b[3] = {bx + px, hb + heights[k], bz + pz};
        if (ray(a, b, NULL, NULL)) return 0;
    }
    return 1;
}
static const float kDoorOffsets[4] = {0.5f, -0.5f, 0.9f, -0.9f};

// Layer of neighbour cell `nc` that node (c, l) can drop down to when there is no step: the highest walkable surface
// between MAX_STEP_UP and MAX_DROP below it.
static int drop_layer(const NavGrid *g, int c, int l, int nc)
{
    if(c<0 || nc<0 || c>=g->w*g->h || nc>=g->w*g->h || l<0 || l>=L)return -1;
    float h = g->height[c * L + l], bestH = -1e30f;
    int bl = -1;
    for (int k = 0; k < L; k++) {
        int nn = nc * L + k;
        if (!(g->flags[nn] & NAV_WALKABLE)) continue;
        float dh = h - g->height[nn];
        if (dh > MAX_STEP_UP && dh <= MAX_DROP && g->height[nn] > bestH) { bestH = g->height[nn]; bl = k; }
    }
    return bl;
}

// One-way drop from node (c, l) into cell nc: walk horizontally off the ledge (knee/chest rays at the upper height).
static int probe_drop(NavGrid *g, NavRayFn ray, int c, int l, int nc, int d)
{
    int node = c * L + l;
    if (neighbour_layer(g, c, l, nc, d & 1) >= 0 || drop_layer(g, c, l, nc) < 0) return 0;
    float ax, az, bx, bz, h = g->height[node];
    cell_pos(g, c % g->w, c / g->w, &ax, &az);
    cell_pos(g, nc % g->w, nc / g->w, &bx, &bz);
    const float heights[2] = {KNEE_HEIGHT, CHEST_HEIGHT};
    for (int k = 0; k < 2; k++) {
        float a[3] = {ax, h + heights[k], az}, b[3] = {bx, h + heights[k], bz};
        if (ray(a, b, NULL, NULL)) return k + 1;
    }
    g->edges[node] |= (unsigned char)(1 << d);
    return 2;
}

// Probes the edge from cell towards direction d (0..3) for every walkable layer; sets bits on both nodes.
static int probe_edge(NavGrid *g, NavRayFn ray, int cell, int d)
{
    int cx = cell % g->w, cz = cell / g->w;
    int nx = cx + kDir[d][0], nz = cz + kDir[d][1];
    if (nx < 0 || nz < 0 || nx >= g->w || nz >= g->h) return 0;
    int nc = nz * g->w + nx, rays = 0;
    float ax, az, bx, bz;
    cell_pos(g, cx, cz, &ax, &az);
    cell_pos(g, nx, nz, &bx, &bz);
    for (int l = 0; l < L; l++) {
        int node = cell * L + l;
        if (!(g->flags[node] & NAV_WALKABLE)) continue;
        int nl = neighbour_layer(g, cell, l, nc, d & 1), stairs = 0;
        if (nl < 0 && !(d & 1)) {   // up a staircase? (straight directions)
            int sl = stair_layer(g, cell, l, nc);
            if (sl >= 0) {
                rays += 3;
                if (stairs_between(ray, ax, g->height[node], az, bx, g->height[nc * L + sl], bz)) { nl = sl; stairs = 1; }
            }
        }
        if (nl < 0) { rays += probe_drop(g, ray, cell, l, nc, d); continue; }
        float ha = g->height[node], hb = g->height[nc * L + nl];
        int blocked = 0, fence = 1;
        const float heights[3] = {KNEE_HEIGHT, CHEST_HEIGHT, 1.0f};
        for (int k = 0; k < 2; k++) {
            float a[3] = {ax, ha + heights[k], az}, b[3] = {bx, hb + heights[k], bz};
            rays++;
            if (ray(a, b, NULL, NULL)) {
                blocked = 1;
                if (k == 1 && !(g_classify && g_classify() == NAV_HIT_FENCE)) fence = 0;   // chest height: the fence itself
                if (k == 0 && !(g_classify && g_classify() == NAV_HIT_FENCE)) {   // knee: a low base is fine, a wall is not
                    float a1[3] = {ax, ha + heights[2], az}, b1[3] = {bx, hb + heights[2], bz};
                    rays++;
                    if (ray(a1, b1, NULL, NULL) && !(g_classify && g_classify() == NAV_HIT_FENCE)) fence = 0;
                }
            }
            if (blocked && !fence) break;
        }
        if (blocked && !(d & 1)) {   // a doorway beside the centre line? (straight directions)
            for (int k = 0; k < 4 && blocked; k++) {
                rays += 2;
                if (pair_clear(ray, ax, ha, az, bx, hb, bz, kDoorOffsets[k])) {
                    blocked = 0;
                    g->fences[node] |= (unsigned char)(1 << (d + 1));
                    g->fences[nc * L + nl] |= (unsigned char)(1 << (d + 5));
                }
            }
        }
        if (blocked) {
            if (fence && !(d & 1) && g_classify) {   // straight crossings only
                g->fences[node] |= (unsigned char)(1 << d);
                g->fences[nc * L + nl] |= (unsigned char)(1 << (d + 4));
            }
            if (stairs) continue;   // (no drop probe: a blocked staircase is just blocked)
            continue;
        }
        g->edges[node] |= (unsigned char)(1 << d);
        g->edges[nc * L + nl] |= (unsigned char)(1 << (d + 4));
    }
    if (!(d & 1))   // a staircase rising from the neighbour back into this cell (only ever probed from the top before)
        for (int l2 = 0; l2 < L; l2++) {
            int lower = nc * L + l2;
            if (!(g->flags[lower] & NAV_WALKABLE) || neighbour_layer(g, nc, l2, cell, 0) >= 0) continue;
            int sl = stair_layer(g, nc, l2, cell);
            if (sl < 0) continue;
            float hl = g->height[lower], hu = g->height[cell * L + sl];
            rays += 5;
            if (!stairs_between(ray, bx, hl, bz, ax, hu, az)) continue;
            int clear = 1;
            const float hs[2] = {KNEE_HEIGHT, CHEST_HEIGHT};
            for (int k = 0; k < 2 && clear; k++) {
                float a1[3] = {bx, hl + hs[k], bz}, b1[3] = {ax, hu + hs[k], az};
                if (ray(a1, b1, NULL, NULL)) clear = 0;
            }
            if (!clear) continue;
            g->edges[lower] |= (unsigned char)(1 << (d + 4));
            g->edges[cell * L + sl] |= (unsigned char)(1 << d);
        }
    for (int l = 0; l < L; l++)   // drops the other way (neighbour is the higher side)
        if (g->flags[nc * L + l] & NAV_WALKABLE) rays += probe_drop(g, ray, nc, l, cell, d + 4);
    return rays;
}

int nav_build_step(NavGrid *g, NavRayFn ray, int rayBudget, double maxMs)
{
    if(!g || !ray || !valid_grid_size(g->w,g->h) || !g->height || !g->flags || !g->edges || !g->fences)return 0;
    double t0 = now_ms();
    int n = g->w * g->h, used = 0, steps = 0;
    while (g->phase < 2 && used < rayBudget) {
        if (g->phase == 0) {
            if(!expansion_old_cell(g,g->cursor,0))used += probe_ground(g,ray,g->cursor);
            g->cursor++;
            if (g->cursor >= n) { g->phase = 1; g->cursor = 0; }
        } else {
            if(!expansion_old_cell(g,g->cursor>>2,1))used += probe_edge(g,ray,g->cursor>>2,g->cursor&3);
            if (++g->cursor >= n * 4) { g->phase = 2; g->cursor = 0; }
        }
        if ((++steps & 63) == 0 && now_ms() - t0 > maxMs) break;
    }
    g->raysCast += used;
    g->buildMs += now_ms() - t0;
    return g->phase == 2;
}

typedef struct NavFileHeader { unsigned magic; char level[32]; float x0, z0, cell; int w, h, layers; } NavFileHeader;

int nav_save(const NavGrid *g, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    NavFileHeader hd = {NAV_FILE_MAGIC, {0}, g->x0, g->z0, g->cell, g->w, g->h, L};
    memcpy(hd.level, g->level, sizeof hd.level);
    size_t n = (size_t)g->w * g->h * L;
    int ok = fwrite(&hd, sizeof hd, 1, f) == 1 && fwrite(g->height, sizeof(float), n, f) == n &&
             fwrite(g->flags, 1, n, f) == n && fwrite(g->edges, 1, n, f) == n && fwrite(g->fences, 1, n, f) == n;
    fclose(f);
    return ok;
}

NavGrid *nav_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    NavFileHeader hd;
    NavGrid *g = NULL;
    if (fread(&hd, sizeof hd, 1, f) == 1 && (hd.magic == NAV_FILE_MAGIC || hd.magic == NAV_FILE_MAGIC4) && hd.layers == L &&
        valid_grid_size(hd.w,hd.h) && isfinite(hd.x0) && isfinite(hd.z0) && isfinite(hd.cell) && hd.cell>=0.5f && hd.cell<=16) {
        uint64_t need=(uint64_t)sizeof hd+(uint64_t)hd.w*hd.h*L*(hd.magic==NAV_FILE_MAGIC?7u:6u);
        if(fseek(f,0,SEEK_END)!=0){fclose(f);return NULL;}
        long fileSize=ftell(f);
        if(fileSize<0 || (uint64_t)fileSize<need || fseek(f,sizeof hd,SEEK_SET)!=0){fclose(f);return NULL;}
        g = new_grid();if(!g){fclose(f);return NULL;}
        memcpy(g->level, hd.level, sizeof g->level);g->level[sizeof g->level-1]=0;
        g->x0 = hd.x0; g->z0 = hd.z0; g->cell = hd.cell; g->w = hd.w; g->h = hd.h; g->phase = 2;
        if(!alloc_arrays(g)){nav_free(g);fclose(f);return NULL;}
        size_t n = (size_t)g->w * g->h * L;
        if (fread(g->height, sizeof(float), n, f) != n || fread(g->flags, 1, n, f) != n || fread(g->edges, 1, n, f) != n ||
            (hd.magic == NAV_FILE_MAGIC && fread(g->fences, 1, n, f) != n)) {
            nav_free(g);
            g = NULL;
        }
        if(g)for(size_t k=0;k<n;k++)if((g->flags[k]&NAV_WALKABLE) && !isfinite(g->height[k])){nav_free(g);g=NULL;break;}
    }
    fclose(f);
    return g;
}

// ---- A* over (cell, layer) nodes ----
typedef struct HeapItem { float f; int node; } HeapItem;

static void heap_push(HeapItem *h, int *n, float f, int node)
{
    int i = (*n)++;
    while (i > 0) {
        int p = (i - 1) / 2;
        if (h[p].f <= f) break;
        h[i] = h[p];
        i = p;
    }
    h[i].f = f;
    h[i].node = node;
}

static HeapItem heap_pop(HeapItem *h, int *n)
{
    HeapItem top = h[0], last = h[--(*n)];
    int i = 0;
    for (;;) {
        int c = 2 * i + 1;
        if (c >= *n) break;
        if (c + 1 < *n && h[c + 1].f < h[c].f) c++;
        if (last.f <= h[c].f) break;
        h[i] = h[c];
        i = c;
    }
    h[i] = last;
    return top;
}

// Node reached from `node` in direction d, or -1.
static int step_node(const NavGrid *g, int node, int d)
{
    if(!g || node<0 || node>=g->w*g->h*L || d<0 || d>7)return -1;
    if (!(g->edges[node] & (1 << d))) return -1;
    int c = node / L, l = node % L;
    int nx=c%g->w+kDir[d][0],nz=c/g->w+kDir[d][1];
    if(nx<0 || nz<0 || nx>=g->w || nz>=g->h)return -1;
    int nc=nz*g->w+nx;
    int nl = neighbour_layer(g, c, l, nc, d & 1);
    // A cell can hold both a surface ~1 m up and one below: the next step of a staircase plus the floor under it (Seine),
    // or an escalator's handrail top plus its steps going down (Metro). The load-time stair re-check records which links
    // are real staircases: those go up, everything else is the drop. (Up first sent bots onto Metro's handrails - they
    // could ride the escalators up but never find the way back down.)
    if (nl < 0) {
        int stairUp = g_stairOk && g_stairOkGrid == g && g_stairOkW == g->w && g_stairOkH == g->h && (g_stairOk[node] & (1 << d));
        if (stairUp) nl = stair_layer(g, c, l, nc);
        if (nl < 0) nl = drop_layer(g, c, l, nc);    // one-way link off a ledge (or down a staircase / escalator)
        if (nl < 0) nl = stair_layer(g, c, l, nc);   // up a staircase (no drop there)
    }
    return nl < 0 ? -1 : nc * L + nl;
}
int nav_step_node(const NavGrid *g, int node, int d) { return step_node(g, node, d); }
// AltBotMod V8: the neighbour across a knifeable fence (the edge is closed for walking, a vehicle drives through)
static int fence_step(const NavGrid *g, int node, int d)
{
    if(!g || node<0 || node>=g->w*g->h*L || d<0 || d>7)return -1;
    int c = node / L, l = node % L, nx = c % g->w + kDir[d][0], nz = c / g->w + kDir[d][1];
    if(nx<0 || nz<0 || nx>=g->w || nz>=g->h)return -1;
    int nc = nz * g->w + nx, nl = neighbour_layer(g, c, l, nc, d & 1);
    return nl < 0 ? -1 : nc * L + nl;
}
// 1.6.5: the neighbour a vehicle would drive to: along a walking link, or across a knifeable fence
int nav_drive_step(const NavGrid *g, int node, int d) { int r = step_node(g, node, d); return r >= 0 ? r : fence_step(g, node, d); }


// Connected walkable node near (x, y, z): nearest cell within a few cells, layer closest to y (NAN = lowest).
static int nearest_node(const NavGrid *g, float x, float y, float z)
{
    int c = nav_cell_index(g, x, z);
    if (c < 0) return -1;
    int cx = c % g->w, cz = c / g->w;
    for (int r = 0; r <= 4; r++) {
        int best = -1;
        float bestD = 1e30f;
        for (int dz = -r; dz <= r; dz++)
            for (int dx = -r; dx <= r; dx++) {
                if (abs(dx) != r && abs(dz) != r) continue;   // ring only
                int x2 = cx + dx, z2 = cz + dz;
                if (x2 < 0 || z2 < 0 || x2 >= g->w || z2 >= g->h) continue;
                for (int l = 0; l < L; l++) {
                    int node = (z2 * g->w + x2) * L + l;
                    if (!(g->flags[node] & NAV_WALKABLE) || !g->edges[node]) continue;
                    if (g_pathMask && !g_pathMask[node]) continue;
                    float d = isnan(y) ? -g->height[node] : fabsf(g->height[node] - y);
                    if (!isnan(y) && d > 3.0f) continue;   // wrong level (e.g. an overpass above the bot)
                    if (d < bestD) { bestD = d; best = node; }
                }
            }
        if (best >= 0) return best;
    }
    return -1;
}

void nav_set_bad_links(const NavGrid *g, const unsigned char *bits) { g_badLinks = bits; g_badLinksGrid = bits ? g : NULL; }
int nav_link_bad(const NavGrid *g, int a, int b, int d)
{
    if (!g_badLinks || g != g_badLinksGrid || a < 0 || b < 0) return 0;
    switch (d) {
    case 0: return g_badLinks[a] & 1;
    case 2: return (g_badLinks[a] >> 1) & 1;
    case 4: return g_badLinks[b] & 1;
    case 6: return (g_badLinks[b] >> 1) & 1;
    }
    /* a diagonal: open if one of the two corners can be driven round on good links */
    int hx = kDir[d][0] > 0 ? 0 : 4, hz = kDir[d][1] > 0 ? 2 : 6;
    int m1 = step_node(g, a, hx), m2 = step_node(g, a, hz);
    int c1 = m1 >= 0 && !nav_link_bad(g, a, m1, hx) && (step_node(g, m1, hz) != b || !nav_link_bad(g, m1, b, hz));
    int c2 = m2 >= 0 && !nav_link_bad(g, a, m2, hz) && (step_node(g, m2, hx) != b || !nav_link_bad(g, m2, b, hx));
    return !c1 && !c2;
}

// True if walking straight from node a to cell of node b stays on connected nodes (for string pulling).
static int straight_walkable(const NavGrid *g, int a, int b)
{
    int bc = b / L;
    int x = (a / L) % g->w, z = (a / L) / g->w, x1 = bc % g->w, z1 = bc / g->w, node = a;
    /* 1.6.1 vehicles keep to the road: a short cut from road to road may not leave it (a bend is driven, not cut across
       the field), and one between two cells a human drove stays on what he drove */
    int ca = g_pathMask ? g_pathMask[a] : 0, cb = g_pathMask ? g_pathMask[b] : 0;
    int keepHuman = ca == 6 && cb == 6, keepRoad = (ca == 1 || ca == 6 || ca == 7) && (cb == 1 || cb == 6 || cb == 7);   // 7: the level's own road (1.6.6)
    /* 1.6.8: a short cut with only ONE end on the road may leave it for 3 cells at most - the route turns off the road
       where A* turned off, not 80 m back up it straight across the field */
    int nearRoad = !keepRoad && g_pathMask && (ca == 1 || ca == 6 || ca == 7 || cb == 1 || cb == 6 || cb == 7), offRoad = 0;
    int dx = abs(x1 - x), dz = abs(z1 - z), sx = x < x1 ? 1 : -1, sz = z < z1 ? 1 : -1, err = dx - dz;
    while (x != x1 || z != z1) {
        int e2 = 2 * err, mx = 0, mz = 0;
        if (e2 > -dz) { err -= dz; mx = sx; }
        if (e2 < dx) { err += dx; mz = sz; }
        int d;
        for (d = 0; d < 8; d++) if (kDir[d][0] == mx && kDir[d][1] == mz) break;
        int prevNode = node;
        node = step_node(g, node, d);
        if (node < 0 || (g_pathMask && !g_pathMask[node])) return 0;
        if (g_pathMask && nav_link_bad(g, prevNode, node, d)) return 0;   // 1.6.6: no short cut over a kerb or divider
        if (g_pathMask && g_pathMask[node] == 5 && node != b) return 0;   // no short cut past a tree trunk
        if (keepHuman && g_pathMask[node] != 6 && node != b) return 0;
        if (keepRoad && g_pathMask[node] != 1 && g_pathMask[node] != 6 && g_pathMask[node] != 7 && node != b) return 0;
        if (nearRoad && g_pathMask[node] != 1 && g_pathMask[node] != 6 && g_pathMask[node] != 7 && ++offRoad > 3) return 0;
        if (!g_pathMask && TREE_DIST(g) && TREE_DIST(g)[node] <= TREE_NEAR_DM && node != b) return 0;
        x += mx;
        z += mz;
    }
    return node == b;
}

// A soldier is ~0.8 m wide: test the centre line and lines offset BODY_HALF_WIDTH to each side, at knee and chest height.
#define BODY_HALF_WIDTH 0.6f   // (a little more than a body: straightened legs keep off walls and corners)
int nav_segment_clear(NavRayFn ray, const float *a, const float *b)
{
    float dx = b[0] - a[0], dz = b[2] - a[2], len = sqrtf(dx * dx + dz * dz);
    if (len < 0.01f) return 1;
    if(g_pathMask && g_vehicleAvoid[3]>0){
        float t=((g_vehicleAvoid[0]-a[0])*dx+(g_vehicleAvoid[2]-a[2])*dz)/(len*len);
        t=fmaxf(0,fminf(1,t));
        if(vehicle_avoids_point(a[0]+t*dx,a[1]+t*(b[1]-a[1]),a[2]+t*dz))return 0;
    }
    float half = g_pathMask ? g_vehicleWidth : BODY_HALF_WIDTH;
    float sx = -dz / len * half, sz = dx / len * half;
    const float heights[3] = {KNEE_HEIGHT, CHEST_HEIGHT, 0.35f};   // (vehicles also low: gabion corners, kerbs)
    const float side[7] = {0, 1, -1, 0.33f, -0.33f, 0.67f, -0.67f};   // (vehicles: 7 lines ~0.5 m apart - trees, poles)
    for (int k = 0; k < (g_pathMask ? 3 : 2); k++)
        for (int s = 0; s < (g_pathMask ? 7 : 3); s++) {
            float p[3] = {a[0] + sx * side[s], a[1] + heights[k], a[2] + sz * side[s]};
            float q[3] = {b[0] + sx * side[s], b[1] + heights[k], b[2] + sz * side[s]};
            if (ray(p, q, NULL, NULL)) return 0;
        }
    return 1;
}

static void node_pos(const NavGrid *g, int node, float *out)
{
    cell_pos(g, (node / L) % g->w, (node / L) / g->w, &out[0], &out[2]);
    out[1] = g->height[node];
}

// Walkable ground near (x, y, z) (same cell rules as path planning). Returns 1 and the ground point in out.
int nav_snap(const NavGrid *g, float x, float y, float z, float *out)
{
    int node = nearest_node(g, x, y, z);
    if (node < 0) return 0;
    node_pos(g, node, out);
    return 1;
}

int nav_direct_route(const NavGrid *g,NavRayFn ray,const float *from,const float *to)
{
    if(!g || !from || !to || !isfinite(from[0]) || !isfinite(from[1]) || !isfinite(from[2]) ||
       !isfinite(to[0]) || !isfinite(to[1]) || !isfinite(to[2]))return 0;
    int a=nearest_node(g,from[0],from[1],from[2]),b=nearest_node(g,to[0],to[1],to[2]);
    return a>=0 && b>=0 && straight_walkable(g,a,b) && (!ray || nav_segment_clear(ray,from,to));
}

// Links blocked where a bot got stuck come back after NAV_BLOCK_MS: a block is a guess, and on Seine one on the only
// staircase up from the river made the whole lower quay a trap for the rest of the round.
#define NAV_BLOCK_MS   (90 * 1000.0)   /* Phoenix: 90 s (was 3 min) */
#define MAX_NAV_BLOCKS 1024
static struct { const NavGrid *g; int w, h, node, nb, d; double until; } g_blocks[MAX_NAV_BLOCKS];
static int g_blockNext;
static void nav_blocks_forget(const NavGrid *g)
{
    for (int k = 0; k < MAX_NAV_BLOCKS; k++) if (g_blocks[k].g == g) g_blocks[k].g = NULL;
}
// A record still describes this grid: same grid, same size, nodes inside it (an old map's records once wrote past
// the end of a new grid allocated at the old address - 6 crashes on the AM1 test box, 1 s after "level ready").
static int block_ok(const NavGrid *g, int k)
{
    int n = g->w * g->h * L;
    return g_blocks[k].g == g && g_blocks[k].w == g->w && g_blocks[k].h == g->h && g_blocks[k].node >= 0 && g_blocks[k].node < n &&
           g_blocks[k].nb >= 0 && g_blocks[k].nb < n;
}
int nav_unblock_expired(NavGrid *g, int all)
{
    double t = now_ms();
    int n = 0;
    for (int k = 0; k < MAX_NAV_BLOCKS; k++) {
        if (!g_blocks[k].g || g_blocks[k].g != g) continue;
        if (!block_ok(g, k)) { g_blocks[k].g = NULL; continue; }
        if (!all && t < g_blocks[k].until) continue;
        g->edges[g_blocks[k].node] |= (unsigned char)(1 << g_blocks[k].d);
        g->edges[g_blocks[k].nb] |= (unsigned char)(1 << ((g_blocks[k].d + 4) & 7));
        g_blocks[k].g = NULL;
        n++;
    }
    return n;
}
int nav_block_towards(NavGrid *g, float x, float y, float z, float tx, float tz)
{
    int node = nearest_node(g, x, y, z), removed = 0;
    if (node < 0) return 0;
    float dx = tx - x, dz = tz - z, len = sqrtf(dx * dx + dz * dz);
    if (len < 0.01f) return 0;
    for (int d = 0; d < 8; d++) {
        float ddx = (float)kDir[d][0], ddz = (float)kDir[d][1], dl = sqrtf(ddx * ddx + ddz * ddz);
        if ((ddx * dx + ddz * dz) / (dl * len) < 0.5f) continue;   // only directions within +-60 deg
        int nb = step_node(g, node, d);
        if (nb < 0) continue;
        if (!(g->edges[node] & (1 << d))) continue;
        g->edges[node] &= (unsigned char)~(1 << d);
        g->edges[nb] &= (unsigned char)~(1 << ((d + 4) & 7));
        int k = g_blockNext++ % MAX_NAV_BLOCKS;
        if (g_blocks[k].g == g && block_ok(g, k)) {   // (the oldest record is overwritten: put its link back first)
            g->edges[g_blocks[k].node] |= (unsigned char)(1 << g_blocks[k].d);
            g->edges[g_blocks[k].nb] |= (unsigned char)(1 << ((g_blocks[k].d + 4) & 7));
        }
        g_blocks[k].g = g; g_blocks[k].w = g->w; g_blocks[k].h = g->h; g_blocks[k].node = node; g_blocks[k].nb = nb; g_blocks[k].d = d; g_blocks[k].until = now_ms() + NAV_BLOCK_MS;
        removed++;
    }
    return removed;
}

// Phoenix: a permanent cut (learned: bots never get through here) - no expiry record
int nav_cut_towards(NavGrid *g, float x, float y, float z, float tx, float tz)
{
    int node = nearest_node(g, x, y, z), removed = 0;
    if (node < 0) return 0;
    float dx = tx - x, dz = tz - z, len = sqrtf(dx * dx + dz * dz);
    if (len < 0.01f) return 0;
    for (int d = 0; d < 8; d++) {
        float ddx = (float)kDir[d][0], ddz = (float)kDir[d][1], dl = sqrtf(ddx * ddx + ddz * ddz);
        if ((ddx * dx + ddz * dz) / (dl * len) < 0.7f) continue;   // +-45 deg
        int nb = step_node(g, node, d);
        if (nb < 0 || !(g->edges[node] & (1 << d))) continue;
        g->edges[node] &= (unsigned char)~(1 << d);
        g->edges[nb] &= (unsigned char)~(1 << ((d + 4) & 7));
        removed++;
    }
    return removed;
}

// ---- Local repair: fine slope check of links near a spot where bots get stuck ----
#define FINE_STEP        0.5f     // ground sample spacing along a link
#define MAX_FINE_RISE    0.5f     // per FINE_STEP (45 deg): steeper than this a soldier cannot walk up (stairs and escalators stay under it)
static int link_too_steep(const NavGrid *g, NavRayFn ray, int node, int nb)
{
    float a[3], b[3];
    node_pos(g, node, a);
    node_pos(g, nb, b);
    if (b[1] - a[1] < 0.3f) return 0;   // level or downhill: fine
    float dx = b[0] - a[0], dz = b[2] - a[2], len = sqrtf(dx * dx + dz * dz);
    int steps = (int)(len / FINE_STEP + 0.5f);
    if (steps < 2) steps = 2;
    float prev = a[1], top = b[1] + 1.0f, bottom = a[1] - 1.0f;
    for (int k = 1; k <= steps; k++) {
        float t = (float)k / steps, from[3] = {a[0] + dx * t, top, a[2] + dz * t}, to[3] = {from[0], bottom, from[2]}, hit[3];
        if (!ray(from, to, hit, NULL)) continue;
        if (hit[1] - prev > MAX_FINE_RISE * (len / steps) / FINE_STEP) return 1;
        prev = hit[1];
    }
    return 0;
}

int nav_repair_around(NavGrid *g, NavRayFn ray, float x, float y, float z, float radius, float dirX, float dirZ)
{
    float dlen = sqrtf(dirX * dirX + dirZ * dirZ);
    int c = nav_cell_index(g, x, z), removed = 0;
    if (c < 0) return 0;
    int cx = c % g->w, cz = c / g->w, r = (int)(radius / g->cell + 0.5f);
    for (int zz = cz - r; zz <= cz + r; zz++)
        for (int xx = cx - r; xx <= cx + r; xx++) {
            if (xx < 0 || zz < 0 || xx >= g->w || zz >= g->h) continue;
            for (int l = 0; l < L; l++) {
                int node = (zz * g->w + xx) * L + l;
                if (!(g->flags[node] & NAV_WALKABLE) || fabsf(g->height[node] - y) > 4.0f) continue;
                for (int d = 0; d < 8; d++) {
                    if (dlen > 0.01f) {   // only links going the way the bot was trying to go (+-60 deg)
                        float ddx = (float)kDir[d][0], ddz = (float)kDir[d][1];
                        if ((ddx * dirX + ddz * dirZ) / (sqrtf(ddx * ddx + ddz * ddz) * dlen) < 0.5f) continue;
                    }
                    int nb = step_node(g, node, d);
                    if (nb < 0 || !link_too_steep(g, ray, node, nb)) continue;
                    g->edges[node] &= (unsigned char)~(1 << d);   // one way: walking down that bank is still fine
                    removed++;
                }
            }
        }
    return removed;
}

static char g_lastError[128];

// ---- special links (ladders) ----
#define NAV_MAX_LINKS 256
typedef struct NavLink { const NavGrid *g; int from, to; float cost; float a[3], b[3]; } NavLink;
static NavLink g_links[NAV_MAX_LINKS];
static int g_linkCount;
#define NAV_MAX_OUT 256
static short g_outLink[NAV_MAX_OUT];
void nav_clear_links(void) { g_linkCount = 0; }
static int nearest_node(const NavGrid *g, float x, float y, float z);
int nav_add_link(const NavGrid *g, const float *a, const float *b, float cost)
{
    if (!g || g_linkCount >= NAV_MAX_LINKS) return -1;
    int from = nearest_node(g, a[0], a[1], a[2]), to = nearest_node(g, b[0], b[1], b[2]);
    if (from < 0 || to < 0 || from == to) return -1;
    NavLink *k = &g_links[g_linkCount];
    k->g = g; k->from = from; k->to = to; k->cost = cost;
    memcpy(k->a, a, 12); memcpy(k->b, b, 12);
    return g_linkCount++;
}
void nav_disable_link(int id) { if (id >= 0 && id < g_linkCount) g_links[id].from = -1; }
int nav_path_link(int k) { return k >= 0 && k < NAV_MAX_OUT ? g_outLink[k] : -1; }
static int link_between(const NavGrid *g, int a, int b)
{
    for (int k = 0; k < g_linkCount; k++) if (g_links[k].g == g && g_links[k].from == a && g_links[k].to == b) return k;
    return -1;
}
const char *nav_last_error(void) { return g_lastError; }

int nav_reprobe_area(NavGrid *g, NavRayFn ray, float x0, float z0, float x1, float z1, float fromY)
{
    int c0 = nav_cell_index(g, x0, z0), c1 = nav_cell_index(g, x1, z1), added = 0;
    if (c0 < 0 || c1 < 0) return 0;
    int ax = c0 % g->w, az = c0 / g->w, bx = c1 % g->w, bz = c1 / g->w;
    if (ax > bx) { int t = ax; ax = bx; bx = t; }
    if (az > bz) { int t = az; az = bz; bz = t; }
    for (int cz = az; cz <= bz; cz++)
        for (int cx = ax; cx <= bx; cx++) {
            int cell = cz * g->w + cx;
            float x, z, hit[3];
            cell_pos(g, cx, cz, &x, &z);
            float a[3] = {x, fromY, z}, b[3] = {x, fromY - 30.0f, z};
            if (!ray(a, b, hit, NULL)) continue;
            int have = 0, freeL = -1;
            for (int l = 0; l < L; l++) {
                int node = cell * L + l;
                if (g->flags[node] & NAV_WALKABLE) { if (fabsf(g->height[node] - hit[1]) < 0.4f) have = 1; }
                else if (freeL < 0) freeL = l;
            }
            if (have) continue;
            if (freeL < 0) {   // every slot used by structure above (bridge girders, stairwells): give up the least connected
                int best = -1, bestEdges = 99;
                for (int l = 1; l < L; l++) {   // never the topmost surface
                    int ne = 0;
                    for (int d = 0; d < 8; d++) if (g->edges[cell * L + l] & (1 << d)) ne++;
                    if (ne < bestEdges || (ne == bestEdges && g->height[cell * L + l] < g->height[cell * L + best])) { bestEdges = ne; best = l; }
                }
                if (best < 0) continue;
                freeL = best;
            }
            int node = cell * L + freeL;
            g->height[node] = hit[1];
            g->flags[node] |= NAV_WALKABLE;
            g->edges[node] = 0;
            added++;
        }
    // re-link the area and its border (both directions for every pair)
    for (int cz = az - 1; cz <= bz + 1; cz++)
        for (int cx = ax - 1; cx <= bx + 1; cx++) {
            if (cx < 0 || cz < 0 || cx >= g->w || cz >= g->h) continue;
            for (int d = 0; d < 8; d++) probe_edge(g, ray, cz * g->w + cx, d);
        }
    return added;
}

unsigned char *nav_drive_mask(const NavGrid *g, int *count)
{
    if (count) *count=0;
    if (!g || !valid_grid_size(g->w,g->h) || !g->height || !g->flags || !g->edges) return NULL;
    size_t nodes=(size_t)g->w*(size_t)g->h*L;
    if (nodes>(size_t)INT_MAX) return NULL;
    int n = (int)nodes, ok = 0;
    unsigned char *m = nav_alloc(nodes);
    if (!m) return NULL;
    for (int node = 0; node < n; node++) {
        // AltBotMod V8: a knifeable fence is no wall for a vehicle - it drives through it (a straight crossing opens the
        // straight edge and the two diagonals beside it)
        unsigned fe = g->fences ? (g->fences[node] & 0x55u) : 0u, open = g->edges[node] | fe;
        for (int d = 1; d < 8; d += 2) if (fe & ((1u << (d - 1)) | (1u << ((d + 1) & 7)))) open |= 1u << d;
        if (!(g->flags[node] & NAV_WALKABLE) || (g->flags[node] & NAV_WATER) || (open & 0xff) != 0xff) continue;
        // under cover (a floor / roof less than 6 m above - layers run top-down): a building, not a road
        int l = node % L, covered = 0;
        for (int k = 0; k < l && !covered; k++) {
            int up = node - l + k;
            if ((g->flags[up] & NAV_WALKABLE) && g->height[up] - g->height[node] < 6.0f) covered = 1;
        }
        if (covered) continue;
        int good = 1, nbs[8];
        for (int d = 0; d < 8 && good; d++) {
            int nb = (g->edges[node] & (1u << d)) ? step_node(g, node, d) : fence_step(g, node, d);   // (V8: across a fence)
            nbs[d] = nb;
            if (nb < 0 || (g->flags[nb] & NAV_WATER) || fabsf(g->height[nb] - g->height[node]) > 0.7f * (d & 1 ? 1.41f : 1.0f)) good = 0;
        }
        if (!good) continue;
        // bumpiness: how far the node sits off the straight line through its opposite neighbours (a road - flat or
        // up a hill - is ~0; rough ground is not)
        float rough = 0, h = g->height[node];
        for (int d = 0; d < 4; d++) {
            float r = fabsf(2.0f * h - g->height[nbs[d]] - g->height[nbs[d + 4]]);
            if (r > rough) rough = r;
        }
        float slope = 0;   // steepest rise to a straight neighbour (m per cell)
        for (int d = 0; d < 8; d += 2) { float s = fabsf(g->height[nbs[d]] - h); if (s > slope) slope = s; }
        int cls = rough < 0.08f ? 1 : rough < 0.2f ? 2 : rough < 0.4f ? 3 : 4;   // cost classes: x0.5, x1, x1.6, x2.5
        if (slope > 0.25f && cls < 4) cls++;   // climbing (over 12%): roads mostly run flat - a smooth hillside is not a road
        if (slope > 0.45f && cls < 4) cls++;
        m[node] = (unsigned char)cls;
        ok++;
    }
    // next to the edge of the drivable area (a wall, a building, a drop): the dearest class - routes keep to the middle
    // of open ground (vehicles are 5 m long and turn wide) but can still squeeze through a gate when there is no other way
    unsigned char *e = nav_alloc(nodes);
    if (e) {
        memcpy(e, m, nodes);
        for (int node = 0; node < n; node++) {
            if (!m[node]) continue;
            for (int d = 0; d < 8; d++) { int nb = step_node(g, node, d); if (nb < 0 || !m[nb]) { e[node] = 4; break; } }
        }
        for (int node = 0; node < n; node++) {   // the next ring in: dearer too (4 m from a wall where there is room)
            if (!m[node] || e[node] == 4) continue;
            for (int d = 0; d < 8; d++) { int nb = step_node(g, node, d); if (nb >= 0 && e[nb] == 4 && m[nb]) { if (e[node] < 3) e[node] = 3; break; } }
        }
        nav_release(m);
        m = e;
    }
    if (count) *count = ok;
    return m;
}

void nav_node_pos(const NavGrid *g, int node, float *out) { node_pos(g, node, out); }
static void cancel_requests_on_mask(const unsigned char *mask);
/* 1.6: queued routes planned on this mask are dropped first (they kept the pointer: use after free) */
void nav_mask_free(unsigned char *mask) { if (mask) cancel_requests_on_mask(mask); nav_release(mask); }

static const NavGrid *g_stairGrid;
static int g_stairCursor, g_stairRemoved, g_stairRestored, g_railCursor, g_railsRemoved, g_stairsAdded;
int nav_stairs_added(void) { return g_stairsAdded; }
int nav_stairs_restored(void) { return g_stairRestored; }
int nav_rails_removed(void) { return g_railsRemoved; }
static int g_railPassOn = 0;
// Handrail / balustrade / wall tops: a cell's single ground sample can land on one and make it "floor" (Metro: bots walked
// onto escalator and staircase rails and bashed into walls). Real floor is there half a metre to each side; a rail
// falls away on at least two sides - such nodes stop being walkable.
#define RAIL_PROBE      0.5f
#define RAIL_FALL       0.6f
static int rail_top(const NavGrid *g, NavRayFn ray, int node, int *used)
{
    float p[3];
    node_pos(g, node, p);
    static const float off[4][2] = {{RAIL_PROBE, 0}, {-RAIL_PROBE, 0}, {0, RAIL_PROBE}, {0, -RAIL_PROBE}};
    int falls = 0;
    for (int k = 0; k < 4 && falls < 2; k++) {
        float a[3] = {p[0] + off[k][0], p[1] + 0.5f, p[2] + off[k][1]}, b[3] = {a[0], p[1] - 1.5f, a[2]}, hit[3];
        (*used)++;
        if (!ray(a, b, hit, NULL) || hit[1] < p[1] - RAIL_FALL) falls++;
    }
    return falls >= 2;
}
int nav_fix_stairs_step(NavGrid *g, NavRayFn ray, int rayBudget, int *removed)
{
    int n = g->w * g->h * L, used = 0;
    if (g_stairGrid != g || g_stairOkGrid != g) {
        g_stairGrid = g; g_stairCursor = 0; g_stairRemoved = 0; g_stairRestored = 0; g_railCursor = 0; g_railsRemoved = 0; g_stairsAdded = 0;
        nav_release(g_stairOk);
        g_stairOk = nav_alloc((size_t)n);   // (zeroed: nothing known to be a staircase until checked)
        g_stairOkGrid = g; g_stairOkW = g->w; g_stairOkH = g->h;
    }
    if (g_stairCursor < 0 && g_railCursor < 0) { if (removed) *removed = g_stairRemoved; return 1; }
    for (; g_stairCursor >= 0 && g_stairCursor < n && used < rayBudget; g_stairCursor++) {
        int node = g_stairCursor, c = node / L, l = node % L;
        if (!(g->flags[node] & NAV_WALKABLE)) continue;
        for (int d = 0; d < 8; d += 2) {   // straight directions (staircase links are straight)
            if (!(g->edges[node] & (1 << d))) {
                int nx2 = c % g->w + kDir[d][0], nz2 = c / g->w + kDir[d][1];
                if (nx2 < 0 || nz2 < 0 || nx2 >= g->w || nz2 >= g->h) continue;
                int nc2 = nz2 * g->w + nx2;
                {   // a staircase up that the build never linked (it rises towards -x / -z: only ever seen from the top)
                    int sl2 = neighbour_layer(g, c, l, nc2, 0) < 0 ? stair_layer(g, c, l, nc2) : -1;
                    if (sl2 >= 0) {
                        float a2[3], b2[3];
                        node_pos(g, node, a2); node_pos(g, nc2 * L + sl2, b2);
                        used += 5;
                        int ok = stairs_between(ray, a2[0], a2[1], a2[2], b2[0], b2[1], b2[2]);
                        const float hs2[2] = {KNEE_HEIGHT, CHEST_HEIGHT};
                        for (int k = 0; k < 2 && ok; k++) {
                            float p1[3] = {a2[0], a2[1] + hs2[k], a2[2]}, p2[3] = {b2[0], b2[1] + hs2[k], b2[2]};
                            if (ray(p1, p2, NULL, NULL)) ok = 0;
                        }
                        if (ok) {
                            g->edges[node] |= (unsigned char)(1 << d);
                            g->edges[nc2 * L + sl2] |= (unsigned char)(1 << ((d + 4) & 7));
                            if (g_stairOk) g_stairOk[node] |= (unsigned char)(1 << d);
                            g_stairsAdded++;
                            continue;
                        }
                    }
                }
                // a drop this check used to delete (and save): a staircase-height surface next door failed the stair
                // test, so the link went - though it was the drop down (Metro's escalator tops). Back if the way is clear.
                if (neighbour_layer(g, c, l, nc2, 0) >= 0 || stair_layer(g, c, l, nc2) < 0 || drop_layer(g, c, l, nc2) < 0) continue;
                float ax, az, bx, bz, h = g->height[node];
                cell_pos(g, c % g->w, c / g->w, &ax, &az);
                cell_pos(g, nx2, nz2, &bx, &bz);
                const float hs[2] = {KNEE_HEIGHT, CHEST_HEIGHT};
                int clear = 1;
                for (int k = 0; k < 2 && clear; k++) {
                    float a1[3] = {ax, h + hs[k], az}, b1[3] = {bx, h + hs[k], bz};
                    used++;
                    if (ray(a1, b1, NULL, NULL)) clear = 0;
                }
                if (clear) { g->edges[node] |= (unsigned char)(1 << d); g_stairRestored++; }
                continue;
            }
            int nx = c % g->w + kDir[d][0], nz = c / g->w + kDir[d][1];
            if (nx < 0 || nz < 0 || nx >= g->w || nz >= g->h) continue;
            int nc = nz * g->w + nx;
            if (neighbour_layer(g, c, l, nc, 0) >= 0) continue;   // an ordinary step
            int sl = stair_layer(g, c, l, nc);
            if (sl < 0) continue;
            float a[3], b[3];
            node_pos(g, node, a); node_pos(g, nc * L + sl, b);
            used += 3;
            if (stairs_between(ray, a[0], a[1], a[2], b[0], b[1], b[2])) { if (g_stairOk) g_stairOk[node] |= (unsigned char)(1 << d); }
            else if (drop_layer(g, c, l, nc) < 0) { g->edges[node] &= (unsigned char)~(1 << d); g_stairRemoved++; }   // (else: the drop)
        }
    }
    if (g_stairCursor >= n) g_stairCursor = -1;
    if (g_stairCursor >= 0) return 0;
    // then the rail pass (off: it also took floor edges and corners - 25,000 of Metro's nodes cut off)
    if (!g_railPassOn) g_railCursor = -1;
    for (; g_railCursor >= 0 && g_railCursor < n && used < rayBudget; g_railCursor++) {
        int node = g_railCursor;
        if (!(g->flags[node] & NAV_WALKABLE) || (g->flags[node] & NAV_WATER) || !g->edges[node]) continue;
        if (rail_top(g, ray, node, &used)) { g->flags[node] &= (unsigned char)~NAV_WALKABLE; g->edges[node] = 0; g_railsRemoved++; }
    }
    if (g_railCursor >= n) g_railCursor = -1;
    if (g_railCursor >= 0) return 0;
    if (removed) *removed = g_stairRemoved;
    return 1;
}

unsigned char *nav_boat_mask(const NavGrid *g, int *count)
{
    int n = g->w * g->h * L, ok = 0;
    unsigned char *m = nav_alloc((size_t)n);
    if (!m) return NULL;
    for (int node = 0; node < n; node++) {
        if (!(g->flags[node] & NAV_WATER) || g->edges[node] != 0xff) continue;
        int open = 1, good = 1;
        for (int d = 0; d < 8 && good; d++) {
            int nb = step_node(g, node, d);
            if (nb < 0) good = 0;
            else if (!(g->flags[nb] & NAV_WATER)) open = 0;
        }
        if (!good) continue;
        m[node] = open ? 1 : 4;   // right against land: the shallows - dearest
        ok++;
    }
    // distance from land in cells (breadth-first from the shore): within 3 cells (6 m) the dearest class, within 10
    // (20 m) the middle one - boats keep 20 m off land and obstacles and only come in to land
    int *dist = nav_alloc((size_t)n * sizeof(int)), *queue = nav_alloc((size_t)n * sizeof(int));
    if (dist && queue) {
        int head = 0, tail = 0;
        for (int node = 0; node < n; node++) {
            dist[node] = m[node] == 4 ? 0 : -1;
            if (m[node] == 4) queue[tail++] = node;
        }
        while (head < tail) {
            int u = queue[head++];
            if (dist[u] >= 10) continue;
            for (int d = 0; d < 8; d++) {
                int v = step_node(g, u, d);
                if (v < 0 || !m[v] || dist[v] >= 0) continue;
                dist[v] = dist[u] + 1;
                queue[tail++] = v;
            }
        }
        for (int node = 0; node < n; node++)
            if (m[node] && dist[node] >= 0) m[node] = dist[node] <= 3 ? 4 : 3;   // (beyond 10 cells: open water, class 1)
    }
    nav_release(dist); nav_release(queue);
    if (count) *count = ok;
    return m;
}

int nav_mask_nearest(const NavGrid *g, const unsigned char *mask, float x, float z, float maxDist, float *out)
{
    int c = nav_cell_index(g, x, z);
    if (c < 0) return 0;
    int cx = c % g->w, cz = c / g->w, rmax = (int)(maxDist / g->cell);
    for (int r = 0; r <= rmax; r++) {
        int best = -1;
        float bestD = 1e30f;
        for (int dz = -r; dz <= r; dz++)
            for (int dx = -r; dx <= r; dx++) {
                if (abs(dx) != r && abs(dz) != r) continue;   // ring only
                int x2 = cx + dx, z2 = cz + dz;
                if (x2 < 0 || z2 < 0 || x2 >= g->w || z2 >= g->h) continue;
                for (int l = 0; l < L; l++) {
                    int node = (z2 * g->w + x2) * L + l;
                    if (!mask[node]) continue;
                    float d = (float)(dx * dx + dz * dz);
                    if (d < bestD) { bestD = d; best = node; }
                }
            }
        if (best >= 0) { node_pos(g, best, out); return 1; }
    }
    return 0;
}

// Match the destination floor as well as its map cell (road under a bridge).
int nav_drive_near(const NavGrid *g,const unsigned char *mask,float x,float y,float z,float maxDist,float *out)
{
    if (!g || !mask || !out || !isfinite(x) || !isfinite(y) || !isfinite(z) || !isfinite(maxDist) || maxDist<0) return 0;
    int c=nav_cell_index(g,x,z);if(c<0)return 0;
    int cx=c%g->w,cz=c/g->w,limit=(int)ceilf(maxDist/g->cell);
    for(int r=0;r<=limit;r++){
        int best=-1;float score=INFINITY;
        for(int dz=-r;dz<=r;dz++)for(int dx=-r;dx<=r;dx++){
            if(abs(dx)!=r && abs(dz)!=r)continue;
            int nx=cx+dx,nz=cz+dz;if(nx<0 || nz<0 || nx>=g->w || nz>=g->h)continue;
            for(int l=0;l<L;l++){
                int node=(nz*g->w+nx)*L+l;if(!mask[node] || !(g->flags[node]&NAV_WALKABLE))continue;
                float px=g->x0+nx*g->cell,pz=g->z0+nz*g->cell;
                float distance=hypotf(px-x,pz-z),vertical=fabsf(g->height[node]-y);
                if(distance>maxDist || vertical>3.0f+distance*0.25f)continue;
                float value=distance+vertical*4.0f;if(value<score){score=value;best=node;}
            }
        }
        if(best>=0){node_pos(g,best,out);return 1;}
    }
    return 0;
}

int nav_landings(const NavGrid *g, const unsigned char *mask, float sx, float sz, float gx, float gz, float walkW,
                 float *water, float *land, int max)
{
    int n = g->w * g->h * L, cnt = 0;
    float score[16];
    if (max > 16) max = 16;
    for (int node = 0; node < n; node++) {
        if (mask[node] != 3) continue;   // beach: water with land next to it
        float p[3];
        node_pos(g, node, p);
        float s = sqrtf((p[0] - sx) * (p[0] - sx) + (p[2] - sz) * (p[2] - sz)) + walkW * sqrtf((p[0] - gx) * (p[0] - gx) + (p[2] - gz) * (p[2] - gz));
        if (cnt == max && s >= score[cnt - 1]) continue;
        int dry = -1;
        for (int d = 0; d < 8 && dry < 0; d++) {
            int nb = step_node(g, node, d);
            if (nb >= 0 && !(g->flags[nb] & NAV_WATER)) dry = nb;
        }
        if (dry < 0) continue;
        int k = cnt < max ? cnt++ : max - 1;   // insert, sorted
        while (k > 0 && score[k - 1] > s) { score[k] = score[k - 1]; memcpy(&water[k * 3], &water[(k - 1) * 3], 12); memcpy(&land[k * 3], &land[(k - 1) * 3], 12); k--; }
        score[k] = s;
        memcpy(&water[k * 3], p, 12);
        node_pos(g, dry, &land[k * 3]);
    }
    return cnt;
}

int nav_land_around(const NavGrid *g, const unsigned char *mask, float x, float z, float radius, float *mx, float *mz, float *nearest)
{
    int c = nav_cell_index(g, x, z), cnt = 0;
    *nearest = 1e30f;
    if (c < 0) return 0;
    int cx = c % g->w, cz = c / g->w, r = (int)(radius / g->cell);
    float sx = 0, sz = 0;
    for (int dz = -r; dz <= r; dz++)
        for (int dx = -r; dx <= r; dx++) {
            int x2 = cx + dx, z2 = cz + dz;
            if (dx * dx + dz * dz > r * r) continue;
            int land = 1;
            if (x2 >= 0 && z2 >= 0 && x2 < g->w && z2 < g->h)
                for (int l = 0; l < L && land; l++) if (mask[(z2 * g->w + x2) * L + l]) land = 0;
            if (!land) continue;
            float px, pz;
            cell_pos(g, x2, z2, &px, &pz);
            float d = sqrtf((px - x) * (px - x) + (pz - z) * (pz - z));
            if (d < *nearest) *nearest = d;
            sx += px; sz += pz; cnt++;
        }
    if (cnt) { *mx = sx / cnt; *mz = sz / cnt; }
    return cnt;
}

void nav_mask_strip(const NavGrid *g, unsigned char *mask, unsigned char cls, const float *keep, int nkeep, float keepR)
{
    int n = g->w * g->h * L;
    for (int node = 0; node < n; node++) {
        if (mask[node] != cls) continue;
        float p[3];
        node_pos(g, node, p);
        int kept = 0;
        for (int k = 0; k < nkeep && !kept; k++) {
            float dx = p[0] - keep[k * 2], dz = p[2] - keep[k * 2 + 1];
            if (dx * dx + dz * dz < keepR * keepR) kept = 1;
        }
        if (!kept) mask[node] = 0;
    }
}

int nav_open_water_near(const NavGrid *g, const unsigned char *mask, float x, float z, float maxDist, float *out)
{
    int c = nav_cell_index(g, x, z);
    if (c < 0) return 0;
    int cx = c % g->w, cz = c / g->w, rmax = (int)(maxDist / g->cell);
    for (int r = 0; r <= rmax; r++)
        for (int dz = -r; dz <= r; dz++)
            for (int dx = -r; dx <= r; dx++) {
                if (abs(dx) != r && abs(dz) != r) continue;
                int x2 = cx + dx, z2 = cz + dz;
                if (x2 < 0 || z2 < 0 || x2 >= g->w || z2 >= g->h) continue;
                for (int l = 0; l < L; l++) { int node = (z2 * g->w + x2) * L + l; if (mask[node] == 1) { node_pos(g, node, out); return 1; } }
            }
    return 0;
}

int *nav_land_labels(const NavGrid *g)
{
    int n = g->w * g->h * L, next = 0;
    int *lab = nav_alloc((size_t)n * sizeof(int)), *queue = nav_alloc((size_t)n * sizeof(int));
    if (!lab || !queue) { nav_release(lab); nav_release(queue); return NULL; }
    for (int s = 0; s < n; s++) {
        if (lab[s] || !(g->flags[s] & NAV_WALKABLE) || (g->flags[s] & NAV_WATER) || !g->edges[s]) continue;
        int head = 0, tail = 0;
        lab[s] = ++next;
        queue[tail++] = s;
        while (head < tail) {
            int u = queue[head++];
            for (int d = 0; d < 8; d++) {
                int v = step_node(g, u, d);
                if (v < 0 || lab[v] || (g->flags[v] & NAV_WATER)) continue;
                lab[v] = next;
                queue[tail++] = v;
            }
        }
    }
    nav_release(queue);
    return lab;
}

int nav_label_at(const NavGrid *g, const int *labels, float x, float y, float z)
{
    int c = nav_cell_index(g, x, z);
    if (c < 0) return 0;
    int cx = c % g->w, cz = c / g->w, best = 0;
    float bestD = 1e30f;
    for (int r = 0; r <= 6 && !best; r++)
        for (int dz = -r; dz <= r; dz++)
            for (int dx = -r; dx <= r; dx++) {
                int x2 = cx + dx, z2 = cz + dz;
                if (x2 < 0 || z2 < 0 || x2 >= g->w || z2 >= g->h) continue;
                for (int l = 0; l < L; l++) {
                    int node = (z2 * g->w + x2) * L + l;
                    if (!labels[node]) continue;
                    float d = fabsf(g->height[node] - y);
                    if (d < bestD) { bestD = d; best = labels[node]; }
                }
            }
    return best;
}

int nav_landing_on(const NavGrid *g, const unsigned char *mask, const int *labels, int label, float sx, float sz, float gx, float gz,
                   float walkW, float minStart, float *water)
{
    int n = g->w * g->h * L, found = 0;
    float best = 1e30f;
    for (int node = 0; node < n; node++) {
        if (!mask[node]) continue;   // (a beach: next to dry land of the landmass - checked below)
        float p[3];
        node_pos(g, node, p);
        float ds = sqrtf((p[0] - sx) * (p[0] - sx) + (p[2] - sz) * (p[2] - sz));
        if (ds < minStart) continue;
        float s = ds + walkW * sqrtf((p[0] - gx) * (p[0] - gx) + (p[2] - gz) * (p[2] - gz));
        if (s >= best) continue;
        int ok = 0;
        for (int d = 0; d < 8 && !ok; d++) { int nb = step_node(g, node, d); if (nb >= 0 && labels[nb] == label) ok = 1; }
        if (!ok) continue;
        best = s; memcpy(water, p, 12); found = 1;
    }
    return found;
}

int nav_mask_block(const NavGrid *g, unsigned char *mask, float x, float z, float radius)
{
    int r = (int)(radius / g->cell) + 1, c = nav_cell_index(g, x, z), n = 0;
    if (c < 0) return 0;
    int cx = c % g->w, cz = c / g->w;
    for (int dz = -r; dz <= r; dz++)
        for (int dx = -r; dx <= r; dx++) {
            int x2 = cx + dx, z2 = cz + dz;
            if (x2 < 0 || z2 < 0 || x2 >= g->w || z2 >= g->h || (dx * dx + dz * dz) * g->cell * g->cell > radius * radius) continue;
            for (int l = 0; l < L; l++) { int node = (z2 * g->w + x2) * L + l; if (mask[node]) { mask[node] = 0; n++; } }
        }
    return n;
}

int nav_mask_restore(const NavGrid *g, unsigned char *mask, const unsigned char *base, float x, float z, float radius)
{
    int r = (int)(radius / g->cell) + 1, c = nav_cell_index(g, x, z), n = 0;
    if (c < 0 || !base) return 0;
    int cx = c % g->w, cz = c / g->w;
    for (int dz = -r; dz <= r; dz++)
        for (int dx = -r; dx <= r; dx++) {
            int x2 = cx + dx, z2 = cz + dz;
            if (x2 < 0 || z2 < 0 || x2 >= g->w || z2 >= g->h || (dx * dx + dz * dz) * g->cell * g->cell > radius * radius) continue;
            for (int l = 0; l < L; l++) { int node = (z2 * g->w + x2) * L + l; if (!mask[node] && base[node]) { mask[node] = base[node]; n++; } }
        }
    return n;
}

void nav_debug_edge(const NavGrid *g, NavRayFn ray, float x, float y, float z, int d, char *out, int outSize)
{
    int node = nearest_node(g, x, y, z), k = 0;
    if (node < 0) { snprintf(out, outSize, "no node"); return; }
    int c = node / L, l = node % L, cx = c % g->w, cz = c / g->w, nx = cx + kDir[d][0], nz = cz + kDir[d][1];
    if (nx < 0 || nz < 0 || nx >= g->w || nz >= g->h) { snprintf(out, outSize, "off grid"); return; }
    int nc = nz * g->w + nx;
    float ax, az, bx, bz, ha = g->height[node];
    cell_pos(g, cx, cz, &ax, &az);
    cell_pos(g, nx, nz, &bx, &bz);
    int nl = neighbour_layer(g, c, l, nc, d & 1), sl = stair_layer(g, c, l, nc), dl = drop_layer(g, c, l, nc);
    k += snprintf(out + k, outSize - k, "node (%.1f,%.2f,%.1f) edges %02x bit%d=%d | next cell layers:", ax, ha, az, g->edges[node], d,
                  (g->edges[node] >> d) & 1);
    for (int q = 0; q < L; q++) if (g->flags[nc * L + q] & NAV_WALKABLE) k += snprintf(out + k, outSize - k, " %.2f", g->height[nc * L + q]);
    k += snprintf(out + k, outSize - k, " | step %d stair %d drop %d", nl, sl, dl);
    int tl = nl >= 0 ? nl : sl;
    if (tl >= 0) {
        float hb = g->height[nc * L + tl];
        k += snprintf(out + k, outSize - k, " | samples:");
        for (int s = 1; s <= 3; s++) {
            float t = s * 0.25f, sx = ax + (bx - ax) * t, sz = az + (bz - az) * t, p[3], n[3];
            float a[3] = {sx, hb + 1.0f, sz}, b[3] = {sx, ha - 1.0f, sz};
            if (ray(a, b, p, n)) k += snprintf(out + k, outSize - k, " %.2f(n%.2f)", p[1], n[1]); else k += snprintf(out + k, outSize - k, " miss");
        }
        k += snprintf(out + k, outSize - k, " stairs_between=%d", stairs_between(ray, ax, ha, az, bx, hb, bz));
        const float hs[2] = {KNEE_HEIGHT, CHEST_HEIGHT};
        for (int q = 0; q < 2; q++) {
            float a[3] = {ax, ha + hs[q], az}, b[3] = {bx, hb + hs[q], bz}, p[3];
            int hit = ray(a, b, p, NULL);
            k += snprintf(out + k, outSize - k, " %s %s", q ? "chest" : "knee", hit ? "BLOCKED" : "clear");
            if (hit) k += snprintf(out + k, outSize - k, "@(%.1f,%.2f,%.1f)", p[0], p[1], p[2]);
        }
    }
}

int nav_floors_at(const NavGrid *g, float x, float y, float z, float *heights, int max)
{
    int c = nav_cell_index(g, x, z), n = 0;
    if (c < 0) return 0;
    for (int l = 0; l < L && n < max; l++) {
        int node = c * L + l;
        if (!(g->flags[node] & NAV_WALKABLE) || !g->edges[node]) continue;
        float h = g->height[node];
        int k = n++;   // insert, nearest to y first
        while (k > 0 && fabsf(heights[k - 1] - y) > fabsf(h - y)) { heights[k] = heights[k - 1]; k--; }
        heights[k] = h;
    }
    return n;
}

/* 1.5.2: water the probes took for ground (Valparaiso's harbour: the rays stop on the surface). From a spot where a bot
   swims, flood the flat surface around it (neighbours within 0.25 m of the start height, out to 'radius') and flag it
   NAV_WATER. Returns the nodes newly flagged. */
int nav_mark_water(NavGrid *g, float x, float y, float z, float radius)
{
    if (!g) return 0;
    int c = nav_cell_index(g, x, z); if (c < 0) return 0;
    int start = -1; float bd = 1e9f;
    for (int l = 0; l < L; l++) { int nd = c * L + l; if (!(g->flags[nd] & NAV_WALKABLE)) continue;
        float dy = g->height[nd] - y; if (dy > -1.2f && dy < 2.5f && fabsf(dy) < bd) { bd = fabsf(dy); start = nd; } }
    if (start < 0) return 0;
    float h0 = g->height[start];
    int cells = (int)(radius / g->cell) + 1, side = 2 * cells + 1, cx0 = c % g->w, cz0 = c / g->w;
    int *queue = nav_alloc((size_t)side * side * sizeof(int)); unsigned char *seen = nav_alloc((size_t)side * side);
    if (!queue || !seen) { nav_release(queue); nav_release(seen); return 0; }
    memset(seen, 0, (size_t)side * side);
    int head = 0, tail = 0, added = 0; queue[tail++] = start; seen[cells * side + cells] = 1;
    while (head < tail) {
        int nd = queue[head++], cc = nd / L, cx = cc % g->w, cz = cc / g->w;
        if (!(g->flags[nd] & NAV_WATER)) { g->flags[nd] |= NAV_WATER; added++; }
        for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
            if (!dx && !dz) continue;
            int nx = cx + dx, nz = cz + dz, ox = nx - cx0 + cells, oz = nz - cz0 + cells;
            if (nx < 0 || nz < 0 || nx >= g->w || nz >= g->h || ox < 0 || oz < 0 || ox >= side || oz >= side || seen[oz * side + ox]) continue;
            int nc = nz * g->w + nx, pick = -1;
            for (int l = 0; l < L; l++) { int m = nc * L + l; if ((g->flags[m] & NAV_WALKABLE) && fabsf(g->height[m] - h0) < 0.25f) { pick = m; break; } }
            seen[oz * side + ox] = 1;
            if (pick >= 0) queue[tail++] = pick;
        }
    }
    nav_release(queue); nav_release(seen);
    return added;
}
int nav_is_water(const NavGrid *g, float x, float y, float z)
{
    int c = nav_cell_index(g, x, z);
    if (c < 0) return 0;
    for (int l = 0; l < L; l++) {
        int node = c * L + l;
        if ((g->flags[node] & NAV_WATER) && fabsf(g->height[node] - y) < 2.0f) return 1;
    }
    return 0;
}

int nav_reach(const NavGrid *g, float x, float y, float z, float *nodePos)
{
    if(!g || !nodePos)return 0;
    int start = nearest_node(g, x, y, z);
    if (start < 0) return 0;
    node_pos(g, start, nodePos);
    int n = g->w * g->h * L, count = 0, head = 0, tail = 0;
    unsigned char *seen = nav_alloc(n);
    int *queue = nav_alloc((size_t)n * sizeof(int));
    if(!seen || !queue){nav_release(seen);nav_release(queue);return 0;}
    seen[start] = 1; queue[tail++] = start;
    while (head < tail) {
        int u = queue[head++];
        count++;
        for (int d = 0; d < 8; d++) {
            int v = step_node(g, u, d);
            if (v >= 0 && !seen[v]) { seen[v] = 1; queue[tail++] = v; }
        }
    }
    nav_release(seen); nav_release(queue);
    return count;
}

int nav_find_path_sync(const NavGrid *g, NavRayFn ray, float sx, float sy, float sz, float tx, float ty, float tz, float *out, int maxPts)
{
    if(!g || !out || maxPts<1 || !isfinite(sx) || !isfinite(sz) || !isfinite(tx) || !isfinite(tz) || isinf(sy) || isinf(ty)){
        snprintf(g_lastError,sizeof g_lastError,"invalid route position/grid");return 0;
    }
    int start = nearest_node(g, sx, sy, sz), goal = nearest_node(g, tx, ty, tz);
    if (goal < 0 && !isnan(ty)) goal = nearest_node(g, tx, NAN, tz);
    if (start < 0 || goal < 0) {
        snprintf(g_lastError, sizeof g_lastError, "no %s node", start < 0 ? "start" : "goal");
        return 0;
    }
    // Search buffers are allocated once per grid size and reused; a per-search stamp marks which entries are valid,
    // so nothing has to be cleared (a plan used to allocate and clear ~20 MB).
    // Weighted A*: leaning on the heuristic expands far fewer nodes for near-shortest routes (CPU on small LAN boxes).
    #define ASTAR_HEURISTIC_WEIGHT 1.5f
    #define ASTAR_MAX_EXPANDED     150000
    int n = g->w * g->h * L;
    static int bufN;
    static float *gs;
    static int *from;
    static unsigned *stamp;
    static unsigned curStamp;
    static HeapItem *heap;
    static int heapCap;
    if (bufN != n) {
        nav_release(gs); nav_release(from); nav_release(stamp); nav_release(heap);
        gs = nav_alloc(n * sizeof(float));
        from = nav_alloc(n * sizeof(int));
        stamp = nav_alloc(n * sizeof(unsigned));
        heapCap = n + 1024;
        heap = nav_alloc(heapCap * sizeof(HeapItem));
        bufN = n;
        curStamp = 0;
        if(!gs || !from || !stamp || !heap){
            nav_release(gs);nav_release(from);nav_release(stamp);nav_release(heap);
            gs=NULL;from=NULL;stamp=NULL;heap=NULL;bufN=0;
            snprintf(g_lastError,sizeof g_lastError,"navigation allocation failed");return 0;
        }
    }
    curStamp += 2;   // stamp = curStamp: open (gs/from valid); curStamp + 1: closed
    if(!curStamp){memset(stamp,0,(size_t)n*sizeof *stamp);curStamp=2;}
    int hn = 0, found = 0, count = 0, closedCount = 0;
    #define SEEN(x)   (stamp[x] >= curStamp)
    #define CLOSED(x) (stamp[x] == curStamp + 1)
    int gx = (goal / L) % g->w, gz = (goal / L) / g->w;
    // the estimate assumes the cheapest ground there is (roads), or the search never looks sideways for them
    const unsigned char *g_roadMask = ROAD_MASK(g), *g_treeDist = TREE_DIST(g), *g_danger = DANGER(g);   // (only this grid's tables)
    // vehicles: the full weight too (0.5 kept it exact for the cheap paved roads - and searched half of a big map: a
    // Damavand re-route froze the server 1.5 s); a road-hugging route a little less than perfect plans in a fraction
    float hw = ASTAR_HEURISTIC_WEIGHT * (g_pathMask ? 1.0f : g_roadMask ? 0.6f : 1.0f);
    int maxExpanded = 60000;   // (an unreachable goal used to search a whole big map - ~100 ms - and bots retried every second)
    gs[start] = 0; from[start] = -1; stamp[start] = curStamp;
    heap_push(heap, &hn, 0, start);
    while (hn > 0) {
        int cur = heap_pop(heap, &hn).node;
        if (CLOSED(cur)) continue;
        if (cur == goal) { found = 1; break; }
        stamp[cur] = curStamp + 1;
        if (++closedCount > maxExpanded) break;   // unreachable goals would otherwise search the whole grid
        for (int d = 0; d < 8; d++) {
            int nb = step_node(g, cur, d);
            if (nb < 0 || (g_pathMask && !g_pathMask[nb])) continue;
            if (g_pathMask && nav_link_bad(g, cur, nb, d)) continue;
            if(g_pathMask && g_vehicleAvoid[3]>0){float p[3];node_pos(g,nb,p);if(vehicle_avoids_point(p[0],p[1],p[2]))continue;}
            // no slicing through a solid corner: a diagonal needs at least one adjacent straight move (requiring both
            // cut MP_Subway in two - its halves join only through diagonal steps)
            if ((d & 1) && step_node(g, cur, (d + 7) & 7) < 0 && step_node(g, cur, (d + 1) & 7) < 0) continue;
            float climb = g->height[nb] - g->height[cur];
            float step = ((d & 1) ? 1.4142f : 1.0f) * g->cell;
            static const float kDriveCost[8] = {1.0f, 0.5f, 1.0f, 1.6f, 2.5f, 12.0f, 0.3f, 0.35f};   // 5: dear (tree, kerb), 6: humans drove here, 7: the level's road (1.6.6)
            float mul = g_pathMask ? kDriveCost[g_pathMask[nb] & 7] : (g->flags[nb] & NAV_WATER) ? WATER_COST_MUL :
                        g_roadMask && g_roadMask[nb] == 1 ? 0.6f : g_roadMask && g_roadMask[nb] == 2 ? 0.85f : 1.0f;   // roads
            if (!g_pathMask && g_treeDist && g_treeDist[nb] <= TREE_NEAR_DM) mul *= 6.0f;   // round a tree, not into it
            if (!g_pathMask) mul *= DANGER_MUL(g_danger, nb);
            if (!g_pathMask && g->edges[nb] != 0xff && !(g->flags[nb] & NAV_WATER)) mul *= 1.5f;   // beside a wall / edge: a cell off it where there is room
            float cost = gs[cur] + step * mul + (climb > 0 ? climb * 2.0f : 0);
            if (CLOSED(nb)) continue;
            if (!SEEN(nb) || cost < gs[nb]) {
                gs[nb] = cost;
                from[nb] = cur;
                stamp[nb] = curStamp;
                int hx = abs(gx - (nb / L) % g->w), hz = abs(gz - (nb / L) / g->w);
                float heur = (float)(hx > hz ? hx + 0.4142f * hz : hz + 0.4142f * hx) * g->cell;
                if (hn < heapCap) heap_push(heap, &hn, cost + heur * hw, nb);
            }
        }
        for (int d = 0; d < 8; d += 2) {   // knifeable fences (even bits): crossable at a price (AltBotMod V8: vehicles drive through)
            if (!(g->fences[cur] & (1 << d))) continue;
            int c=cur/L,l=cur%L,nx=c%g->w+kDir[d][0],nz=c/g->w+kDir[d][1];
            if(nx<0 || nz<0 || nx>=g->w || nz>=g->h)continue;
            int nc=nz*g->w+nx;
            int nl = neighbour_layer(g, c, l, nc, 0);
            if (nl < 0) continue;
            int nb = nc * L + nl;
            if (g_pathMask && !g_pathMask[nb]) continue;
            float cost = gs[cur] + g->cell + (g_pathMask ? 2.0f : FENCE_COST);
            if (CLOSED(nb)) continue;
            if (!SEEN(nb) || cost < gs[nb]) {
                gs[nb] = cost;
                from[nb] = cur;
                stamp[nb] = curStamp;
                int hx = abs(gx - (nb / L) % g->w), hz = abs(gz - (nb / L) / g->w);
                float heur = (float)(hx > hz ? hx + 0.4142f * hz : hz + 0.4142f * hx) * g->cell;
                if (hn < heapCap) heap_push(heap, &hn, cost + heur * hw, nb);
            }
        }
        for (int k = 0; k < g_linkCount && !g_pathMask; k++) {   // ladders
            if (g_links[k].g != g || g_links[k].from != cur) continue;
            int nb = g_links[k].to;
            float cost = gs[cur] + g_links[k].cost;
            if (CLOSED(nb)) continue;
            if (!SEEN(nb) || cost < gs[nb]) {
                gs[nb] = cost;
                from[nb] = cur;
                stamp[nb] = curStamp;
                int hx = abs(gx - (nb / L) % g->w), hz = abs(gz - (nb / L) / g->w);
                float heur = (float)(hx > hz ? hx + 0.4142f * hz : hz + 0.4142f * hx) * g->cell;
                if (hn < heapCap) heap_push(heap, &hn, cost + heur * hw, nb);
            }
        }
    }
    snprintf(g_lastError, sizeof g_lastError, "%s after %d nodes (start h%.1f, goal h%.1f)", found ? "ok" : "A* exhausted",
             closedCount, g->height[start], g->height[goal]);
    // Boxed in (the 2 m grid sees a tight spot as enclosed): plan from a nearby open node and walk to it first.
    static int rescuing;
    if (!found && closedCount < 64 && !rescuing && maxPts > 1 && !g_pathMask) {
        int cx = (start / L) % g->w, cz = (start / L) / g->w, tries = 0;
        unsigned pocket = curStamp + 1;   // stamp of the nodes the failed search closed (nested searches use newer stamps)
        for (int r = 1; r <= 3 && tries < 3; r++)
            for (int dz = -r; dz <= r; dz++)
                for (int dx = -r; dx <= r; dx++) {
                    if (abs(dx) != r && abs(dz) != r) continue;
                    int x2 = cx + dx, z2 = cz + dz;
                    if (x2 < 0 || z2 < 0 || x2 >= g->w || z2 >= g->h) continue;
                    for (int l = 0; l < L; l++) {
                        int node = (z2 * g->w + x2) * L + l;
                        if (!(g->flags[node] & NAV_WALKABLE) || !g->edges[node] || stamp[node] == pocket) continue;
                        if (fabsf(g->height[node] - g->height[start]) > 1.5f || tries >= 3) continue;
                        float p[3], s[3] = {sx, sy, sz};
                        node_pos(g, node, p);
                        if (ray && !nav_segment_clear(ray, s, p)) continue;   // must be walkable to in a straight line
                        tries++;
                        rescuing = 1;
                        int n = nav_find_path_sync(g, ray, p[0], p[1], p[2], tx, ty, tz, out + 3, maxPts - 1);
                        rescuing = 0;
                        if (n > 0) {
                            memcpy(out, p, sizeof p);
                            memmove(g_outLink + 1, g_outLink, sizeof g_outLink[0] * (size_t)(n < NAV_MAX_OUT - 1 ? n : NAV_MAX_OUT - 1));
                            g_outLink[0] = -1;
                            return n + 1;
                        }
                    }
                }
    }
    if (found) {
        int len = 0;
        for (int c = goal; c >= 0; c = from[c]) len++;
        int *nodes = nav_alloc(len * sizeof(int));
        if(!nodes){snprintf(g_lastError,sizeof g_lastError,"navigation allocation failed");return 0;}
        int k = len;
        for (int c = goal; c >= 0; c = from[c]) nodes[--k] = c;
        // String pulling from the bot's actual position: take the farthest node reachable in a straight line.
        float from[3] = {sx, isnan(sy) ? g->height[start] : sy, sz};
        int anchor = -1;
        while (anchor < len - 1 && count < maxPts) {
            int next = anchor + 1, limit = len - 1, link = -1;
            if (anchor >= 0) link = link_between(g, nodes[anchor], nodes[anchor + 1]);
            if (link < 0)   // the next link start is as far as a straight run may go
                for (int j = anchor + 1; j < len - 1; j++) if (link_between(g, nodes[j], nodes[j + 1]) >= 0) { limit = j; break; }
            if (link < 0)
                for (int j = limit; j > next; j--) {
                    if (j - anchor > (g_pathMask && !g_longLegs ? 32 : g_longLegs ? 250 : 64)) continue;
                    if (anchor >= 0 && !straight_walkable(g, nodes[anchor], nodes[j])) continue;
                    float to[3];
                    node_pos(g, nodes[j], to);
                    if (ray && !nav_segment_clear(ray, from, to)) continue;
                    next = j;
                    break;
                }
            if (link < 0 && next == anchor + 1 && anchor >= 0 && count < maxPts - 1) {   // through a doorway: via its gap
                int a = nodes[anchor], b = nodes[next], ca = a / L, cb = b / L;
                int ddx = cb % g->w - ca % g->w, ddz = cb / g->w - ca / g->w, dd = -1;
                for (int k = 0; k < 8; k += 2) if (kDir[k][0] == ddx && kDir[k][1] == ddz) dd = k;
                if (dd >= 0 && (g->fences[a] & (1 << (dd + 1)))) {
                    float pa[3], pb[3], off = 0;
                    node_pos(g, a, pa); node_pos(g, b, pb);
                    for (int k = 0; k < 4 && ray; k++)
                        if (pair_clear(ray, pa[0], pa[1], pa[2], pb[0], pb[1], pb[2], kDoorOffsets[k])) { off = kDoorOffsets[k]; break; }
                    float dx = pb[0] - pa[0], dz = pb[2] - pa[2], ln = sqrtf(dx * dx + dz * dz);
                    float *o = &out[count * 3];
                    o[0] = (pa[0] + pb[0]) * 0.5f - dz / ln * off;
                    o[1] = pa[1] > pb[1] ? pa[1] : pb[1];
                    o[2] = (pa[2] + pb[2]) * 0.5f + dx / ln * off;
                    if (count < NAV_MAX_OUT) g_outLink[count] = -1;
                    memcpy(from, o, sizeof from);
                    count++;
                }
            }
            // A valid infantry grid edge is not proof that an armored hull
            // fits through it. Never output an unchecked adjacent fallback.
            if(g_pathMask && ray){
                float to[3];node_pos(g,nodes[next],to);
                if(!nav_segment_clear(ray,from,to)){
                    float progress=hypotf(from[0]-sx,from[2]-sz);
                    if(!count || progress<=4)count=0;
                    snprintf(g_lastError,sizeof g_lastError,count?"ok (clear prefix before obstacle)":"vehicle segment obstructed");break;
                }
            }
            int startsLink = next < len - 1 && link_between(g, nodes[next], nodes[next + 1]) >= 0;
            if (link >= 0) memcpy(&out[count * 3], g_links[link].b, 12);   // over the ladder: its exit spot
            else if (startsLink) memcpy(&out[count * 3], g_links[link_between(g, nodes[next], nodes[next + 1])].a, 12);   // its foot
            else node_pos(g, nodes[next], &out[count * 3]);
            if (count < NAV_MAX_OUT) g_outLink[count] = (short)link;
            memcpy(from, &out[count * 3], sizeof from);
            count++;
            anchor = next;
        }
        nav_release(nodes);
    }
    #undef SEEN
    #undef CLOSED
    return count;
}

#include "nav_async.inc"
