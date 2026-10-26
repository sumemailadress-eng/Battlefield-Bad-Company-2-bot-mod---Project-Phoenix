/* Offline harness: drives the real runtime (world.c, bots.c, navglue.c, nav.c) against the mock server. */
#include "harness_win.h"
#include "rt.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

PhxConfig g_cfg = {15, 0, 6.0f, 2, 1, 2.5f, 5.0f, 1, 15, 1, 1.0f, 0, 1, 1, 1, 1, 1, 0, 0, 4, 0, 2, 30.0f, 2, 1, 1, 1.2f, 1, 1, 1, 1, 1, 1};
static double g_now;
static uint32_t rng = 12345;
static int quiet;
uint32_t rt_rand(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
double rt_now(void) { return g_now; }
static FILE *logf_;
void rt_log(const char *fmt, ...)
{
    va_list a; char line[1600]; va_start(a, fmt); vsnprintf(line, sizeof line, fmt, a); va_end(a);
    if (logf_) fprintf(logf_, "[%7.2f] %s\n", g_now, line);
    if (!quiet) printf("[%7.2f] %s\n", g_now, line);
}
static int obs_read(void *c, uint32_t a, void *o, unsigned n) { (void)c; return eng_read(a, o, n); }
int phx_observe_now(BC2Observation *o) { return bc2_observe(obs_read, NULL, g_engine.base, o) == BC2_OBSERVE_READY; }
void rt_nav_cache_path(const char *key, char *out, unsigned size) { snprintf(out, size, "%s/navcache_%s.nav", getenv("NAVDIR") ? getenv("NAVDIR") : ".", key); }
void mock_init(void); void mock_step(float dt); void mock_report(void); void mock_level_change(void);
extern int sim_kills[3], sim_deaths[3], sim_captures[3];

int main(int argc, char **argv)
{
    double seconds = argc > 1 ? atof(argv[1]) : 300;
    if (argc > 2) g_cfg.navBuild = atoi(argv[2]);
    if (argc > 3) rng = (uint32_t)atoi(argv[3]);
    quiet = getenv("QUIET") != NULL;
    g_cfg.verbose = getenv("VERBOSE") != NULL;
    logf_ = fopen(getenv("HLOG") ? getenv("HLOG") : "harness.log", "w");
    if (getenv("PINTEST")) g_cfg.frozenRespawn = 15;
    mock_init();
    float dt = 1.0f / 30;
    double nextStatus = 0;
    for (long t = 0; g_now < seconds; t++) {
        g_now = t * dt;
        mock_step(dt);
        if (getenv("LEVELCHANGE") && fabs(g_now - atof(getenv("LEVELCHANGE"))) < dt / 2) mock_level_change();
        if (getenv("STOPAT") && fabs(g_now - atof(getenv("STOPAT"))) < dt / 2) { bots_stop_all(); printf("STOP issued\n"); }
        world_tick(g_now);
        navg_tick(g_now);
        bots_tick(g_now, dt);
        if (getenv("WATERTEST") && t % 45 == 0 && g_now > 20) {   /* 1.6: water marked while vehicle routes are queued (Isla Inocentes crash) */
            static int k; float p[3] = {-200.0f + (k * 37) % 400, 0, -150.0f + (k * 53) % 300}; k++;
            float sn[3]; extern int navg_snap(const float *, float *); if (navg_snap(p, sn)) learn_water(sn);
        }
        if (g_now >= nextStatus) { nextStatus = g_now + 30; bots_status(); if (!quiet) mock_report(); }
    }
    mock_report();
    extern int sim_actions[];
    printf("ACTIONS grenade %d launcher %d gadget1 %d gadget2 %d melee %d switch %d jump %d\n", sim_actions[BC2_EiaThrowGrenade], sim_actions[BC2_EiaGrenadeLauncher],
           sim_actions[BC2_EiaDynamicGadget1], sim_actions[BC2_EiaDynamicGadget2], sim_actions[BC2_EiaMeleeAttack], sim_actions[BC2_EiaSwitchPrimaryWeapon], sim_actions[BC2_EiaJump]);
    { extern int sim_armed, sim_disarmed, sim_mcom_destroyed; printf("RUSH armed %d defused %d destroyed %d\n", sim_armed, sim_disarmed, sim_mcom_destroyed); }
    { extern int sim_tool_shots; printf("REPAIRTOOL shots at enemies %d\n", sim_tool_shots); }
    { extern int sim_team_switch, sim_repairs; printf("TEAMSWITCH %d (player ids reused by the other team), vehicles repaired %d\n", sim_team_switch, sim_repairs); }
    printf("RESULT kills %d/%d deaths %d/%d captures %d/%d\n", sim_kills[1], sim_kills[2], sim_deaths[1], sim_deaths[2], sim_captures[1], sim_captures[2]);
    if (logf_) fclose(logf_);
    return 0;
}
