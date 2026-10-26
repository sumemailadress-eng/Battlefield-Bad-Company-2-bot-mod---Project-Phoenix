/* 1.6.6: the level's own roads.
   A BC2 level carries its roads as data: RoadData objects (a VectorShapeAttachment - the road's centre line as a
   VectorShapeData point list, made for the single-player AI drivers) and terrain decals painted along terrain splines
   (TerrainSplineDecal -> TerrainSplineData -> TerrainSplinePointData positions). The server loads the level data, so
   the objects are in its memory; the worker-thread memory scan finds them by their vtables (getType at vtable slot 2,
   see ENGINE-NOTES "Roads"). Every line read is checked against the ground before it is trusted: a line that does not
   run over drivable ground for most of its length (a river spline, a cliff decal, a shape in local coordinates) is
   dropped. Accepted lines become drive-map class 7 (the cheapest after human-driven cells) - navglue.c applies them.
   Simulation thread (scan_collect) only; the scan itself only collects addresses. */
#ifdef PHX_HARNESS
#include "harness_win.h"
#else
#include "win_common.h"
#endif
#include "rt.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

#define MAX_LINES 192
#define MAX_PTS 8192
static RoadLine g_lines[MAX_LINES]; static int g_nLines;
static float g_pts[MAX_PTS][3]; static int g_nPts;
static unsigned g_level; static int g_found, g_tries;

/* a reference array (count +0xC, element pointers at [+0x10]) or a vector of pointers (begin +8, end +0xC) */
static int read_ptr_array(uint32_t field, uint32_t *out, int max)
{
    uint32_t cnt = 0, el = 0, b = 0, e = 0;
    if (eng_u32(field + 0xc, &cnt) && eng_u32(field + 0x10, &el) && cnt > 0 && cnt <= 4096 && eng_ptr(el)) {
        int n = (int)cnt > max ? max : (int)cnt;
        return eng_read(el, out, (unsigned)n * 4) ? n : 0;
    }
    if (eng_u32(field + 8, &b) && eng_u32(field + 0xc, &e) && eng_ptr(b) && e > b && (e - b) % 4 == 0 && (e - b) / 4 <= 4096) {
        int n = (int)((e - b) / 4); if (n > max) n = max;
        return eng_read(b, out, (unsigned)n * 4) ? n : 0;
    }
    return 0;
}
static int sane(const float *p) { return isfinite(p[0]) && isfinite(p[1]) && isfinite(p[2]) && fabsf(p[0]) < 20000 && fabsf(p[2]) < 20000 && fabsf(p[1]) < 5000; }
static int add_line(int src, const char *name, const float (*pts)[3], int n)
{
    if (n < 2 || g_nLines >= MAX_LINES || g_nPts + n > MAX_PTS) return 0;
    float len = 0; for (int i = 1; i < n; i++) len += hypotf(pts[i][0] - pts[i - 1][0], pts[i][2] - pts[i - 1][2]);
    if (len < 8) return 0;
    RoadLine *l = &g_lines[g_nLines++]; memset(l, 0, sizeof *l);
    l->src = src; l->first = g_nPts; l->n = n; l->length = len; snprintf(l->name, sizeof l->name, "%s", name && name[0] ? name : "(no name)");
    memcpy(g_pts[g_nPts], pts, sizeof(float) * 3 * (size_t)n); g_nPts += n;
    return 1;
}
/* VectorShapeData points: a struct array of Vec3 (16 bytes; 12 tried too) at +52 (begin +8, end +0xC) */
static int read_shape(uint32_t shape, float (*pts)[3], int max)
{
    uint32_t vt = 0, b = 0, e = 0;
    if (!eng_ptr(shape) || !eng_u32(shape, &vt) || vt != RB(VA_VECTOR_SHAPE_VT)) return 0;
    if (!eng_u32(shape + 52 + 8, &b) || !eng_u32(shape + 52 + 0xc, &e) || !eng_ptr(b) || e <= b || e - b > 16u * 4096) return 0;
    unsigned stride = (e - b) % 16 == 0 ? 16 : (e - b) % 12 == 0 ? 12 : 0; if (!stride) return 0;
    int n = (int)((e - b) / stride); if (n > max) n = max;
    for (int i = 0; i < n; i++) { if (!eng_read(b + (uint32_t)i * stride, pts[i], 12) || !sane(pts[i])) return 0; }
    return n;
}
/* TerrainSplineData points: reference array at +60 of TerrainSplinePointData (Position at +16) */
static int read_spline(uint32_t sp, float (*pts)[3], int max)
{
    uint32_t vt = 0, el[1024];
    if (!eng_ptr(sp) || !eng_u32(sp, &vt) || vt != RB(VA_TSPLINE_VT)) return 0;
    int n = read_ptr_array(sp + 60, el, max < 1024 ? max : 1024), k = 0;
    for (int i = 0; i < n; i++) {
        uint32_t pvt = 0;
        if (!eng_ptr(el[i]) || !eng_u32(el[i], &pvt) || pvt != RB(VA_TSPLINE_POINT_VT) || !eng_read(el[i] + 16, pts[k], 12) || !sane(pts[k])) continue;
        k++;
    }
    return k;
}
static int lower_has(const char *s, const char *sub) { char u[64]; int i = 0; for (; s[i] && i < 63; i++) u[i] = (char)tolower((unsigned char)s[i]); u[i] = 0; return strstr(u, sub) != NULL; }
/* +1 a road by its name, -1 surely not a road, 0 unknown */
static int name_class(const char *a, const char *b)
{
    static const char *yes[] = {"road", "asphalt", "street", "tarmac", "highway", "track", "path", "dirt", "gravel", "lane", "drive", "runway", "concrete"};
    static const char *no[] = {"river", "water", "sea", "lake", "shore", "cliff", "rock", "fence", "rail", "trench", "ditch", "bush", "grass", "foliage", "hedge", "sand", "beach"};
    const char *s[2] = {a, b};
    for (int j = 0; j < 2; j++) if (s[j] && s[j][0]) for (unsigned i = 0; i < sizeof no / sizeof *no; i++) if (lower_has(s[j], no[i])) return -1;
    for (int j = 0; j < 2; j++) if (s[j] && s[j][0]) for (unsigned i = 0; i < sizeof yes / sizeof *yes; i++) if (lower_has(s[j], yes[i])) return 1;
    return 0;
}

int roads_wanted(void) { return g_level != g_world.levelSerial || (!g_found && g_tries < 3); }
const RoadLine *roads_lines(int *n, const float (**pts)[3]) { *n = g_nLines; *pts = (const float (*)[3])g_pts; return g_lines; }

void roads_collect(const uint32_t *roadData, int nr, const uint32_t *decals, int nd, const uint32_t *splines, int ns)
{
    if (g_level != g_world.levelSerial) { g_level = g_world.levelSerial; g_found = 0; g_tries = 0; }
    if (g_found) return;
    g_tries++;
    g_nLines = 0; g_nPts = 0;
    static float pts[1024][3];
    static uint32_t used[1024]; int nUsed = 0;
    int okR = 0, okD = 0, okS = 0;
    for (int i = 0; i < nr; i++) {   /* RoadData: the road's own centre line */
        uint32_t vt = 0, shape = 0; char nm[48] = "";
        if (!eng_u32(roadData[i], &vt) || vt != RB(VA_ROAD_DATA_VT) || !eng_u32(roadData[i] + 48, &shape)) continue;
        eng_string(roadData[i] + 12, nm, sizeof nm);
        int n = read_shape(shape, pts, 1024);
        if (n >= 2 && add_line(ROAD_SRC_ROAD, nm, (const float (*)[3])pts, n)) { g_lines[g_nLines - 1].named = 1; okR++; }
    }
    for (int i = 0; i < nd; i++) {   /* terrain decals painted along a spline */
        uint32_t vt = 0, mat = 0, sp = 0; uint8_t en = 1; char nm[48] = "", mn[64] = "";
        if (!eng_u32(decals[i], &vt) || vt != RB(VA_TSPLINE_DECAL_VT) || !eng_u32(decals[i] + 40, &sp) || !eng_ptr(sp)) continue;
        eng_read(decals[i] + 33, &en, 1); if (!en) continue;
        eng_string(decals[i] + 16, nm, sizeof nm);
        if (eng_u32(decals[i] + 28, &mat) && eng_ptr(mat)) eng_string(mat + 12, mn, sizeof mn);
        int cls = name_class(nm, mn); if (cls < 0) continue;
        int dup = 0; for (int k = 0; k < nUsed; k++) if (used[k] == sp) dup = 1; if (dup) continue;
        int n = read_spline(sp, pts, 1024);
        if (nUsed < 1024) used[nUsed++] = sp;
        char full[48]; snprintf(full, sizeof full, "%s%s%s", nm, mn[0] ? " / " : "", mn);
        if (n >= 2 && add_line(ROAD_SRC_DECAL, full, (const float (*)[3])pts, n)) { g_lines[g_nLines - 1].named = cls > 0; okD++; }
    }
    for (int i = 0; i < ns; i++) {   /* bare terrain splines (no decal found for them): the ground check decides */
        int dup = 0; for (int k = 0; k < nUsed; k++) if (used[k] == splines[i]) dup = 1; if (dup) continue;
        char nm[48] = ""; uint32_t vt = 0;
        if (!eng_u32(splines[i], &vt) || vt != RB(VA_TSPLINE_VT)) continue;
        eng_string(splines[i] + 12, nm, sizeof nm);
        int cls = name_class(nm, NULL); if (cls < 0) continue;
        int n = read_spline(splines[i], pts, 1024);
        if (nUsed < 1024) used[nUsed++] = splines[i];
        if (n >= 2 && add_line(ROAD_SRC_SPLINE, nm, (const float (*)[3])pts, n)) { g_lines[g_nLines - 1].named = cls > 0; okS++; }
    }
    rt_log("roads: level data scan %d - %d road object(s) (%d found), %d road decal(s) (%d found), %d terrain spline(s) (%d found)",
           g_tries, okR, nr, okD, nd, okS, ns);
    if (g_nLines) { g_found = 1; navg_roads_set(g_lines, g_nLines, (const float (*)[3])g_pts); }
    else if (g_tries >= 3) rt_log("roads: no road lines in this level's data on the server - vehicles use the drive map, the vehicle scan, human driving and drawn routes");
}
