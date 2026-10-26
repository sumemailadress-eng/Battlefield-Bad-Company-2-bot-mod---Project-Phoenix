/* Turret finder (1.0.1).
   The BC2 server's vehicle turrets are aimed by rate inputs (the game's own AI uses TurretData PitchP / YawP
   gains), so a bot needs to know where its turret points. Nobody has mapped the vehicle part layout of this
   server, so Phoenix looks for it: it walks the vehicle object and the objects it points to (two levels) for
   transform matrices (three unit rows at 16-byte steps, as the root transform is stored), remembers each one
   by its pointer path, and the bot then moves one input at a time and watches which matrix turns with it
   relative to the hull. That matrix is the turret (or gun), and its path works for every vehicle of the type.
   All reads go through eng_read (ReadProcessMemory in the server), so a stale pointer only fails a read. */
#include "rt.h"
#include <math.h>
#include <string.h>

#define TUR_CAND_MAX 1024
#define TUR_L1_MAX 512
#define TUR_L2_MAX 6000

typedef struct TurCand { TurPath path; float rel0, pit0; int noisy; } TurCand;

static struct {
    uint32_t veh; int phase;            /* 0 idle, 1 level 1, 2 level 2, 3 done */
    uint32_t l1[TUR_L1_MAX]; uint16_t l1off[TUR_L1_MAX]; int n1, i1;
    uint32_t l2[TUR_L2_MAX]; uint16_t l2off[TUR_L2_MAX][2]; int n2, i2;
    TurCand cand[TUR_CAND_MAX]; uint32_t candAbs[TUR_CAND_MAX]; int nc;
    float pos[3];
} S;

static int finite3(const float *v) { return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]); }
static float len3(const float *v) { return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

/* 0 no, 1 world-space (translation near the vehicle), 2 local (small translation) */
static int matrix_kind(const float *m, const float *vpos)
{
    if (!finite3(m) || !finite3(m + 4) || !finite3(m + 8) || !finite3(m + 12)) return 0;
    float a = len3(m), b = len3(m + 4), c = len3(m + 8);
    if (fabsf(a - 1) > 0.02f || fabsf(b - 1) > 0.02f || fabsf(c - 1) > 0.02f) return 0;
    if (fabsf(dot3(m, m + 4)) > 0.03f || fabsf(dot3(m, m + 8)) > 0.03f || fabsf(dot3(m + 4, m + 8)) > 0.03f) return 0;
    float dx = m[12] - vpos[0], dy = m[13] - vpos[1], dz = m[14] - vpos[2];
    if (dx * dx + dy * dy + dz * dz < 12.0f * 12.0f) return 1;
    if (len3(m + 12) < 10.0f) return 2;
    return 0;
}

static void add_cands(uint32_t base, const unsigned char *buf, unsigned size, int depth, const uint16_t *offs)
{
    for (unsigned o = (16 - (base & 15)) & 15; o + 64 <= size && S.nc < TUR_CAND_MAX; o += 16) {
        float m[16]; memcpy(m, buf + o, 64);
        int k = matrix_kind(m, S.pos); if (!k) continue;
        uint32_t abs = base + o; int dup = 0;
        for (int i = 0; i < S.nc; i++) if (S.candAbs[i] == abs) {
            /* reached again: keep the path with the smaller last offset (more likely inside its own object) */
            TurPath *pp = &S.cand[i].path;
            if (o < pp->off[pp->depth]) { pp->depth = (uint8_t)depth; for (int k = 0; k < depth; k++) pp->off[k] = offs[k]; pp->off[depth] = (uint16_t)o; }
            dup = 1; break;
        }
        if (dup) continue;
        TurCand *c = &S.cand[S.nc]; memset(c, 0, sizeof *c);
        c->path.depth = (uint8_t)depth; for (int i = 0; i < depth; i++) c->path.off[i] = offs[i];
        c->path.off[depth] = (uint16_t)o; c->path.local = k == 2;
        S.candAbs[S.nc++] = abs;
    }
}

void tur_reset(void) { memset(&S, 0, sizeof S); }
uint32_t tur_busy_vehicle(void) { return S.phase ? S.veh : 0; }

/* step the search for this vehicle; returns -1 none found, 0 still looking, >0 candidates */
int tur_search(uint32_t veh, const float *pos, int budget)
{
    if (S.veh != veh || !S.phase) {
        memset(&S, 0, sizeof S); S.veh = veh; memcpy(S.pos, pos, 12);
        unsigned char b0[0x800];
        if (!eng_read(veh, b0, sizeof b0)) { S.phase = 3; return -1; }
        for (unsigned o = 0; o + 4 <= sizeof b0 && S.n1 < TUR_L1_MAX; o += 4) {
            uint32_t p; memcpy(&p, b0 + o, 4);
            if (!eng_ptr(p) || p == veh) continue;
            int dup = 0; for (int i = 0; i < S.n1; i++) if (S.l1[i] == p) { dup = 1; break; }
            if (!dup) { S.l1[S.n1] = p; S.l1off[S.n1++] = (uint16_t)o; }
        }
        S.phase = 1;
    }
    if (S.phase == 3) return S.nc ? S.nc : -1;
    while (budget-- > 0) {
        if (S.phase == 1) {
            if (S.i1 >= S.n1) { S.phase = 2; continue; }
            uint32_t p = S.l1[S.i1]; uint16_t off0 = S.l1off[S.i1++];
            unsigned char b[0x400];
            if (!eng_read(p, b, sizeof b)) continue;
            add_cands(p, b, sizeof b, 1, &off0);
            for (unsigned o = 0; o + 4 <= sizeof b && S.n2 < TUR_L2_MAX; o += 4) {
                uint32_t q; memcpy(&q, b + o, 4);
                if (!eng_ptr(q) || q == p || q == veh) continue;
                S.l2[S.n2] = q; S.l2off[S.n2][0] = off0; S.l2off[S.n2][1] = (uint16_t)o; S.n2++;
            }
        } else if (S.phase == 2) {
            if (S.i2 >= S.n2) {
                /* matrices inside the vehicle object itself last: a part reached through a pointer keeps the path that
                   is valid for every vehicle of the type (memory next to the object belongs to something else) */
                unsigned char b0[0x800];
                if (eng_read(S.veh, b0, sizeof b0)) add_cands(S.veh, b0, sizeof b0, 0, NULL);
                S.phase = 3; break;
            }
            int i = S.i2++;
            unsigned char b[0x200];
            if (!eng_read(S.l2[i], b, sizeof b)) continue;
            add_cands(S.l2[i], b, sizeof b, 2, S.l2off[i]);
        } else break;
    }
    if (S.phase == 3) return S.nc ? S.nc : -1;
    return 0;
}

/* relative heading / pitch of a matrix's forward row in the hull frame */
static void rel_of(const float *f, int local, const VehicleInfo *vi, float *rel, float *pit)
{
    float l[3];
    if (local) memcpy(l, f, 12);
    else { l[0] = dot3(f, vi->right); l[1] = dot3(f, vi->up); l[2] = dot3(f, vi->fwd); }
    *rel = atan2f(l[0], l[2]);
    *pit = asinf(fmaxf(-1, fminf(1, l[1])));
}

int tur_read(uint32_t veh, const TurPath *p, const VehicleInfo *vi, float *rel, float *pit)
{
    uint32_t a = veh;
    for (int i = 0; i < p->depth; i++) { uint32_t q = 0; if (!eng_u32(a + p->off[i], &q) || !eng_ptr(q)) return 0; a = q; }
    float m[16]; if (!eng_read(a + p->off[p->depth], m, 64)) return 0;
    if (!matrix_kind(m, vi->pos)) return 0;
    rel_of(m + 8, p->local, vi, rel, pit);
    return 1;
}

float tur_change(uint32_t veh, const VehicleInfo *vi, int i, int pitch);
int tur_count(void) { return S.phase == 3 ? S.nc : 0; }
const TurPath *tur_path(int i) { return i >= 0 && i < S.nc ? &S.cand[i].path : NULL; }

/* baseline every candidate now */
void tur_baseline(uint32_t veh, const VehicleInfo *vi)
{
    for (int i = 0; i < S.nc; i++) { float r = 0, p = 0; if (S.cand[i].noisy || !tur_read(veh, &S.cand[i].path, vi, &r, &p)) { r = 99; p = 99; } S.cand[i].rel0 = r; S.cand[i].pit0 = p; }
}
/* candidates that moved since the baseline with no test input (rotors, wheels, suspension) are left out for good */
int tur_mark_noisy(uint32_t veh, const VehicleInfo *vi, float thresh)
{
    int n = 0;
    for (int i = 0; i < S.nc; i++) {
        if (S.cand[i].noisy) continue;
        if (S.cand[i].rel0 > 90) { S.cand[i].noisy = 1; n++; continue; }
        if (fabsf(tur_change(veh, vi, i, 0)) > thresh || fabsf(tur_change(veh, vi, i, 1)) > thresh) { S.cand[i].noisy = 1; n++; }
    }
    tur_baseline(veh, vi);
    return n;
}
/* the candidate that turned (yaw) or tilted (pitch) most since the baseline; *amount gets the change */
int tur_moved(uint32_t veh, const VehicleInfo *vi, int pitch, float *amount)
{
    int best = -1; float bv = 0;
    for (int i = 0; i < S.nc; i++) {
        if (S.cand[i].rel0 > 90) continue;
        float r, p; if (!tur_read(veh, &S.cand[i].path, vi, &r, &p)) continue;
        if (fabsf(p) > 1.2f || fabsf(S.cand[i].pit0) > 1.2f) continue;   /* a wheel or a vertical part */
        float d = pitch ? p - S.cand[i].pit0 : r - S.cand[i].rel0;
        while (d > 3.14159265f) d -= 6.2831853f; while (d < -3.14159265f) d += 6.2831853f;
        if (fabsf(d) > fabsf(bv)) { bv = d; best = i; }
    }
    *amount = bv; return best;
}
float tur_change(uint32_t veh, const VehicleInfo *vi, int i, int pitch)
{
    if (i < 0 || i >= S.nc) return 0;
    float r, p; if (!tur_read(veh, &S.cand[i].path, vi, &r, &p)) return 0;
    float d = pitch ? p - S.cand[i].pit0 : r - S.cand[i].rel0;
    while (d > 3.14159265f) d -= 6.2831853f; while (d < -3.14159265f) d += 6.2831853f;
    return d;
}
void tur_done(void) { S.phase = 0; S.veh = 0; }

/* 1.6.4: a candidate's heading / pitch at its last baseline (before the test input moved it) */
float tur_rel0(int i) { return i >= 0 && i < S.nc ? S.cand[i].rel0 : 0; }
float tur_pit0(int i) { return i >= 0 && i < S.nc ? S.cand[i].pit0 : 0; }
