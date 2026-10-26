/* Project Phoenix BC2 0.6 - bot runtime module (loaded into the BF-Portable BC2 dedicated server).
   All engine work happens in a detour that runs right after ServerPlayerManager::update on the
   simulation thread. */
#include "win_common.h"
#include "rt.h"
#include "bc2_tick_hook.h"
#include <tlhelp32.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <wchar.h>

PhxConfig g_cfg = {15, 0, 6.0f, 2, 1, 2.5f, 5.0f, 1, 15, 1, 1.0f, 0, 1, 1, 1, 1, 1, 0, 0, 4, 0, 2, 30.0f, 2, 1, 1, 1.2f, 1, 1, 1, 1, 1, 1};
WCHAR g_rootDir[1024];
static HMODULE g_module;
static HANDLE g_logFile = INVALID_HANDLE_VALUE;
static volatile LONG g_started, g_stopRequested, g_stopped, g_failed;
static uint32_t g_simThread;
static LARGE_INTEGER g_freq, g_t0;
static double g_lastTick, g_nextStatus;
static HANDLE g_frozen[512]; static unsigned g_frozenCount;
static uint32_t g_rng = 0x9e3779b9u;
static unsigned g_raysAtStatus;   /* 1.6.10: rays counted at the last perf line */
static CRITICAL_SECTION g_logLock;

uint32_t rt_rand(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
double rt_now(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return (double)(t.QuadPart - g_t0.QuadPart) / (double)g_freq.QuadPart; }

void rt_log(const char *fmt, ...)
{
    if (g_logFile == INVALID_HANDLE_VALUE) return;
    char line[1600]; SYSTEMTIME st; GetLocalTime(&st);
    int used = snprintf(line, sizeof line, "[%02u:%02u:%02u.%03u] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list a; va_start(a, fmt); int n = vsnprintf(line + used, sizeof line - used - 3, fmt, a); va_end(a);
    if (n < 0) return;
    phx_hide_user_names(line, sizeof line);
    size_t len = strlen(line); line[len++] = '\r'; line[len++] = '\n';
    EnterCriticalSection(&g_logLock);
    DWORD wrote = 0; WriteFile(g_logFile, line, (DWORD)len, &wrote, NULL);
    LeaveCriticalSection(&g_logLock);
}

static int obs_read(void *c, uint32_t a, void *o, unsigned n) { (void)c; return eng_read(a, o, n); }
int phx_observe_now(BC2Observation *o) { return bc2_observe(obs_read, NULL, g_engine.base, o) == BC2_OBSERVE_READY; }

/* 1.6.9: ONE navcache for every Phoenix install - next to the BC2 server (BF-Portable\Battlefield_bc2_Server\PhoenixNavcache),
   or NavcacheDir= in phoenix.ini. 1.6.8 kept it in the package folder, so unzipping a new version started every map from
   nothing: the grid rebuilt, the vehicle scan again, and the routes humans drove, the drawn routes and the stuck spots gone. */
static WCHAR g_navDir[1100];
static void navcache_default(const WCHAR *serverExe)
{
    _snwprintf(g_navDir, 1100, L"%ls", serverExe); g_navDir[1099] = 0;
    WCHAR *s = wcsrchr(g_navDir, L'\\'); if (s) *s = 0; else g_navDir[0] = 0;
    wcsncat(g_navDir, L"\\PhoenixNavcache", 1099 - wcslen(g_navDir));
}
static int file_size(const WCHAR *p, LONGLONG *size)
{
    WIN32_FILE_ATTRIBUTE_DATA a; if (!GetFileAttributesExW(p, GetFileExInfoStandard, &a)) return 0;
    *size = ((LONGLONG)a.nFileSizeHigh << 32) | a.nFileSizeLow; return 1;
}
/* the install's own navcache\ (older versions saved there) is merged in: files the shared folder lacks are copied, and a
   .learn / .routes file is taken when it is bigger (more learned / more drawn). Nothing is deleted. */
static void navcache_import(void)
{
    CreateDirectoryW(g_navDir, NULL);
    WCHAR local[1100]; _snwprintf(local, 1100, L"%ls\\navcache", g_rootDir); local[1099] = 0;
    if (!_wcsicmp(local, g_navDir)) return;
    WCHAR pat[1200]; _snwprintf(pat, 1200, L"%ls\\*", local); pat[1199] = 0;
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat, &fd);
    int added = 0, bigger = 0, kept = 0;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            WCHAR src[1200], dst[1200]; _snwprintf(src, 1200, L"%ls\\%ls", local, fd.cFileName); _snwprintf(dst, 1200, L"%ls\\%ls", g_navDir, fd.cFileName);
            src[1199] = dst[1199] = 0;
            LONGLONG ss = 0, ds = 0; if (!file_size(src, &ss)) continue;
            if (!file_size(dst, &ds)) { if (CopyFileW(src, dst, TRUE)) added++; continue; }
            const WCHAR *ext = wcsrchr(fd.cFileName, L'.');
            if (ext && (!_wcsicmp(ext, L".learn") || !_wcsicmp(ext, L".routes")) && ss > ds) { if (CopyFileW(src, dst, FALSE)) bigger++; }
            else kept++;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        /* a note for whoever opens the old folder */
        WCHAR note[1200]; _snwprintf(note, 1200, L"%ls\\WHERE-IS-MY-NAVCACHE.txt", local); note[1199] = 0;
        FILE *f = _wfopen(note, L"wb");
        if (f) { char a[1100]; WideCharToMultiByte(CP_ACP, 0, g_navDir, -1, a, sizeof a, NULL, NULL);
            fprintf(f, "Project Phoenix %s keeps its navigation cache and everything the bots learned in:\r\n\r\n    %s\r\n\r\n"
                       "The files in this folder were merged into it (missing files copied, a bigger .learn / .routes taken).\r\n"
                       "Every Phoenix version and install uses that one folder, so nothing is lost when you unzip a new version.\r\n", PHX_VERSION, a);
            fclose(f); }
    }
    char a[1100]; WideCharToMultiByte(CP_ACP, 0, g_navDir, -1, a, sizeof a, NULL, NULL);
    rt_log("navcache: %s (shared by every Phoenix install) - from this install's navcache: %d file(s) added, %d bigger .learn/.routes taken, %d already there",
           a, added, bigger, kept);
}

void rt_nav_cache_path(const char *key, char *out, unsigned size)
{
    WCHAR dir[1100]; _snwprintf(dir, 1100, L"%ls", g_navDir); dir[1099] = 0;
    CreateDirectoryW(dir, NULL);
    WCHAR wkey[64]; MultiByteToWideChar(CP_ACP, 0, key, -1, wkey, 64);
    WCHAR path[1200]; _snwprintf(path, 1200, L"%ls\\%ls.nav", dir, wkey); path[1199] = 0;
    WideCharToMultiByte(CP_ACP, 0, path, -1, out, (int)size, NULL, NULL);
}

/* ---------------- config ---------------- */
static void load_config(void)
{
    WCHAR path[1100]; _snwprintf(path, 1100, L"%ls\\phoenix.ini", g_rootDir); path[1099] = 0;
    FILE *f = _wfopen(path, L"rb");
    if (!f) { rt_log("config: phoenix.ini not found - defaults"); return; }
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char *s = line; while (*s == ' ' || *s == '\t') s++;
        if (*s == ';' || *s == '#' || *s == '[') continue;
        char *eq = strchr(s, '='); if (!eq) continue; *eq = 0;
        char *k = s, *v = eq + 1; char *e = k + strlen(k); while (e > k && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
        while (*v == ' ' || *v == '\t') v++;
        float x = (float)atof(v);
        if (!_stricmp(k, "BotsPerTeam")) g_cfg.botsPerTeam = (int)fmaxf(0, fminf(24, x));
        else if (!_stricmp(k, "FillTeamsTo")) g_cfg.fillToPerTeam = (int)fmaxf(0, fminf(24, x));
        else if (!_stricmp(k, "RespawnDelay")) g_cfg.respawnDelay = fmaxf(1, fminf(60, x));
        else if (!_stricmp(k, "Difficulty")) g_cfg.difficulty = (int)fmaxf(1, fminf(3, x));
        else if (!_stricmp(k, "Navigation")) g_cfg.navBuild = x != 0;
        else if (!_stricmp(k, "NavCellSize")) g_cfg.navCell = fmaxf(1.5f, fminf(6, x));
        else if (!_stricmp(k, "NavBuildMsPerTick")) g_cfg.navBudgetMs = fmaxf(0.5f, fminf(20, x));
        else if (!_stricmp(k, "AimPitchSign")) g_cfg.pitchSign = x < 0 ? -1 : 1;
        else if (!_stricmp(k, "StatusEvery")) g_cfg.logEvery = (int)fmaxf(3, fminf(600, x));
        else if (!_stricmp(k, "Grenades")) g_cfg.grenades = x != 0;
        else if (!_stricmp(k, "AimError")) g_cfg.aimSkill = fmaxf(0.1f, fminf(4, x));
        else if (!_stricmp(k, "Verbose")) g_cfg.verbose = x != 0;
        else if (!_stricmp(k, "MovementStep")) g_cfg.moveStep = x != 0;
        else if (!_stricmp(k, "UnlockAll")) g_cfg.unlockAll = x != 0;
        else if (!_stricmp(k, "Equipment")) g_cfg.equipment = x != 0;
        else if (!_stricmp(k, "Revive")) g_cfg.revive = x != 0;
        else if (!_stricmp(k, "Fireteams")) g_cfg.fireteams = x != 0;
        else if (!_stricmp(k, "FollowHuman")) g_cfg.followHuman = x != 0;
        else if (!_stricmp(k, "RealSquads")) g_cfg.realSquads = x != 0;
        else if (!_stricmp(k, "GroupSize")) g_cfg.groupSize = (int)fmaxf(1, fminf(16, x));
        else if (!_stricmp(k, "GuardEvery")) g_cfg.guardEvery = (int)fmaxf(-1, fminf(32, x));
        else if (!_stricmp(k, "Vehicles")) g_cfg.vehicles = (int)fmaxf(0, fminf(2, x));
        else if (!_stricmp(k, "Learning")) g_cfg.learn = x != 0;
        else if (!_stricmp(k, "Helicopters")) g_cfg.helicopters = x != 0;
        else if (!_stricmp(k, "QuietTeamJoin")) g_cfg.quietTeam = x != 0;
        else if (!_stricmp(k, "KeepBotPlayers")) g_cfg.keepPlayers = x != 0;
        else if (!_stricmp(k, "StationaryWeapons")) g_cfg.emplacements = x != 0;
        else if (!_stricmp(k, "Boats")) g_cfg.boats = x != 0;
        else if (!_stricmp(k, "LearnFromHumans")) g_cfg.learnHumans = x != 0;
        else if (!_stricmp(k, "RushObjectives")) g_cfg.objectives = x != 0;
        else if (!_stricmp(k, "BotDamage")) g_cfg.botDamage = fmaxf(0.5f, fminf(3.0f, x));
        else if (!_stricmp(k, "ScoreboardNames")) g_cfg.scoreboard = (int)fmaxf(0, fminf(2, x));
        else if (!_stricmp(k, "FrozenRespawn")) g_cfg.frozenRespawn = fmaxf(0, fminf(600, x));
        else if (!_stricmp(k, "NavcacheDir")) {   /* 1.6.9: a folder of your choice for the shared navcache */
            char *e2 = v + strlen(v); while (e2 > v && (e2[-1] == '\r' || e2[-1] == '\n' || e2[-1] == ' ' || e2[-1] == '\t' || e2[-1] == '\\')) *--e2 = 0;
            if (*v) MultiByteToWideChar(CP_ACP, 0, v, -1, g_navDir, 1100);
        }
    }
    fclose(f);
    if (g_cfg.botsPerTeam * 2 > 44) g_cfg.botsPerTeam = 22;
    rt_log("config: %d bots per team%s, respawn %.0f s, difficulty %d, navigation %s (cell %.1f m, %.1f ms/tick), aim pitch sign %+d, grenades %s, movement step %s",
           g_cfg.botsPerTeam, g_cfg.fillToPerTeam ? " (fill mode)" : "", g_cfg.respawnDelay, g_cfg.difficulty, g_cfg.navBuild ? "on" : "off",
           g_cfg.navCell, g_cfg.navBudgetMs, g_cfg.pitchSign, g_cfg.grenades ? "on" : "off", g_cfg.moveStep ? "on" : "off");
}

/* ---------------- fault logging ---------------- */
static LONG CALLBACK on_exception(PEXCEPTION_POINTERS info)
{
    DWORD code = info->ExceptionRecord->ExceptionCode;
    if (GetCurrentThreadId() == g_simThread &&
        (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_BREAKPOINT || code == EXCEPTION_STACK_OVERFLOW)) {
        uint32_t at = (uint32_t)(uintptr_t)info->ExceptionRecord->ExceptionAddress;
        uint32_t mine = (uint32_t)(uintptr_t)g_module;
        rt_log("FAULT code 0x%08lx at 0x%08lx (exe+0x%lx, phoenix+0x%lx) esp 0x%08lx", (unsigned long)code, (unsigned long)at,
               (unsigned long)(at - g_engine.base), (unsigned long)(at - mine), (unsigned long)info->ContextRecord->Esp);
        if (g_logFile != INVALID_HANDLE_VALUE) FlushFileBuffers(g_logFile);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* ---------------- tick ---------------- */
static void capture_human_kit_flags(void)
{
    if (g_world.humanKitFlagsKnown || !g_world.obsReady) return;
    for (int i = 0; i < g_world.obs.count; i++) {
        BC2ObservedPlayer *p = &g_world.obs.players[i];
        if (p->isAI || !p->soldierValid) continue;
        uint32_t flags = 0, data = 0;
        if (eng_u32(p->pointer + PL_FLAGS17C, &flags) && eng_u32(p->pointer + PL_SOLDIER_DAT, &data) && data) {
            g_world.humanKitFlags = flags; g_world.humanKitFlagsKnown = 1;
            rt_log("kits: human kit flags 0x%08lx", (unsigned long)flags);
            /* log the human's unlock selection for reference */
            uint32_t r[2];
            if (eng_read(p->pointer + PL_UNLOCKS, r, 8) && r[1] >= r[0] && r[1] - r[0] <= 32 * 8 && eng_ptr(r[0])) {
                PhxPair pairs[32]; unsigned n = (r[1] - r[0]) / 8;
                if (n && eng_read(r[0], pairs, n * 8)) for (unsigned k = 0; k < n; k++) {
                    char nm[40] = ""; uint32_t asset = eng_resource_by_id(pairs[k].resourceId); if (asset) eng_string(asset + ASSET_NAME, nm, sizeof nm);
                    rt_log("kits: human unlock slot %lu -> '%s'", (unsigned long)pairs[k].category, nm);
                }
            }
        }
        break;
    }
}

static void after_update(void *context, uint32_t manager, float dt, uint32_t thread)
{
    (void)context;
    if (g_failed || g_stopped) return;
    uint32_t expected = eng_player_manager();
    if (!expected || expected != manager) return;
    if (!g_simThread) { g_simThread = thread; rt_log("hook: simulation thread %lu, player manager 0x%08lx", (unsigned long)thread, (unsigned long)manager); }
    if (thread != g_simThread) return;
    double now = rt_now();
    if (!isfinite(dt) || dt <= 0 || dt > 0.25f) dt = (float)fmin(0.1, fmax(0.005, now - g_lastTick));
    g_lastTick = now;
    if (g_stopRequested) {   /* 1.6.6: a few ticks - living soldiers are killed first, then their players go */
        if (!bots_stop_step(now)) return;
        rt_log("stop: all bots removed; the hook stays dormant until the server restarts");
        InterlockedExchange(&g_stopped, 1);
        return;
    }
    /* 1.6.10: what Phoenix costs the server per tick - average and worst since the last status line, by part */
    static double tSum[3], tMax[3], tAll; static unsigned tN;
    double t0 = rt_now();
    world_tick(now);
    capture_human_kit_flags();
    double t1 = rt_now();
    navg_tick(now);
    double t2 = rt_now();
    bots_tick(now, dt);
    double t3 = rt_now(), part[3] = {t1 - t0, t2 - t1, t3 - t2};
    for (int k = 0; k < 3; k++) { tSum[k] += part[k]; if (part[k] > tMax[k]) tMax[k] = part[k]; }
    if (t3 - t0 > tAll) tAll = t3 - t0;
    tN++;
    if (now >= g_nextStatus) {
        g_nextStatus = now + g_cfg.logEvery; bots_status();
        double bp[3]; bots_perf(bp);
        if (tN) rt_log("perf: Phoenix per server tick (%u ticks): world %.2f ms (worst %.1f), navigation %.2f (worst %.1f), bots %.2f (worst %.1f) [thinking %.2f, driving %.2f, movement step %.2f]; slowest tick %.1f ms; rays %.0f a tick",
                       tN, tSum[0] * 1000 / tN, tMax[0] * 1000, tSum[1] * 1000 / tN, tMax[1] * 1000, tSum[2] * 1000 / tN, tMax[2] * 1000,
                       bp[0] * 1000 / tN, bp[1] * 1000 / tN, bp[2] * 1000 / tN, tAll * 1000, (double)(eng_ray_count() - g_raysAtStatus) / tN);
        g_raysAtStatus = eng_ray_count();
        memset(tSum, 0, sizeof tSum); memset(tMax, 0, sizeof tMax); tAll = 0; tN = 0;
        FlushFileBuffers(g_logFile);
    }
}

/* ---------------- patch guard (freeze other threads while writing the 6-byte detour) ---------------- */
static void resume_threads(void *c)
{
    (void)c;
    for (unsigned i = 0; i < g_frozenCount; i++) { ResumeThread(g_frozen[i]); CloseHandle(g_frozen[i]); }
    g_frozenCount = 0;
}
static int freeze_threads(void *c, uint32_t target, unsigned bytes)
{
    (void)c;
    HANDLE handles[512]; unsigned count = 0; int okay = 1;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 e; memset(&e, 0, sizeof e); e.dwSize = sizeof e;
    if (Thread32First(snap, &e)) do {
        if (e.th32OwnerProcessID != GetCurrentProcessId() || e.th32ThreadID == GetCurrentThreadId()) continue;
        if (count == 512) { okay = 0; break; }
        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, e.th32ThreadID);
        if (!h) { okay = 0; break; }
        handles[count++] = h;
    } while (Thread32Next(snap, &e));
    CloseHandle(snap);
    if (!okay || !count) { for (unsigned i = 0; i < count; i++) CloseHandle(handles[i]); return 0; }
    unsigned suspended = 0;
    for (unsigned i = 0; i < count; i++) { if (SuspendThread(handles[i]) == (DWORD)-1) { okay = 0; break; } suspended++; }
    if (okay) for (unsigned i = 0; i < count; i++) {
        CONTEXT ctx; memset(&ctx, 0, sizeof ctx); ctx.ContextFlags = CONTEXT_CONTROL;
        if (!GetThreadContext(handles[i], &ctx) || (ctx.Eip >= target && ctx.Eip < target + bytes)) { okay = 0; break; }
    }
    if (!okay) { for (unsigned i = 0; i < suspended; i++) ResumeThread(handles[i]); for (unsigned i = 0; i < count; i++) CloseHandle(handles[i]); return 0; }
    for (unsigned i = 0; i < count; i++) g_frozen[i] = handles[i];
    g_frozenCount = count; return 1;
}

/* ---------------- exports ---------------- */
__declspec(dllexport) DWORD WINAPI PhoenixStart(void *parameter)
{
    PhoenixStartConfig config;
    if (!parameter || !phx_read(GetCurrentProcess(), (uint32_t)(uintptr_t)parameter, &config, sizeof config) ||
        config.magic != PHX_ATTACH_MAGIC || config.size != sizeof config || !config.logPath[0] || config.logPath[1023]) return 10;
    if (InterlockedCompareExchange(&g_started, 1, 0)) return 11;
    WCHAR serverPath[1024];
    if (!GetModuleFileNameW(NULL, serverPath, 1024) || !phx_verify_server_file(serverPath)) return 12;
    g_engine.base = (uint32_t)(uintptr_t)GetModuleHandleW(NULL);
    InitializeCriticalSection(&g_logLock);
    g_logFile = CreateFileW(config.logPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_logFile == INVALID_HANDLE_VALUE) return 13;
    QueryPerformanceFrequency(&g_freq); QueryPerformanceCounter(&g_t0);
    g_rng ^= GetTickCount() * 2654435761u ^ GetCurrentProcessId();
    /* package root = parent of the folder holding this DLL */
    GetModuleFileNameW(g_module, g_rootDir, 1024);
    WCHAR *slash = wcsrchr(g_rootDir, L'\\'); if (slash) *slash = 0;
    slash = wcsrchr(g_rootDir, L'\\'); if (slash) *slash = 0;
    rt_log("Project Phoenix BC2 %s bot runtime - server executable SHA256 matched", PHX_VERSION);
    char why[64];
    if (!eng_verify(why, sizeof why)) { rt_log("RESULT FAILED code fingerprint mismatch: %s", why); InterlockedExchange(&g_failed, 1); return 14; }
    rt_log("engine: %u native fingerprints verified, module base 0x%08lx", 22u, (unsigned long)g_engine.base);
    navcache_default(serverPath);
    load_config();
    navcache_import();
    AddVectoredExceptionHandler(0, on_exception);
    BC2TickHookConfig hook; memset(&hook, 0, sizeof hook);
    hook.imageBase = g_engine.base; hook.exactImageHashVerified = 1; hook.afterUpdate = after_update;
    hook.beginPatch = freeze_threads; hook.endPatch = resume_threads;
    int result = BC2_HOOK_NOT_QUIESCENT;
    for (int i = 0; i < 40 && result == BC2_HOOK_NOT_QUIESCENT; i++) { result = bc2_tick_hook_install(&hook); if (result == BC2_HOOK_NOT_QUIESCENT) Sleep(10); }
    if (result != BC2_HOOK_OK) { rt_log("RESULT FAILED hook install %d", result); InterlockedExchange(&g_failed, 1); return 15; }
    rt_log("RESULT ATTACHED - bots will join once a level is running");
    FlushFileBuffers(g_logFile);
    return 0;
}

__declspec(dllexport) DWORD WINAPI PhoenixStop(void *unused)
{
    (void)unused;
    if (!g_started) return 20;
    if (g_failed || g_stopped) return 0;
    InterlockedExchange(&g_stopRequested, 1);
    for (int i = 0; i < 400; i++) { if (g_stopped) { FlushFileBuffers(g_logFile); return 0; } Sleep(25); }
    rt_log("stop: the simulation did not finish the stop within 10 s - restart the server to remove bots");
    return 21;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) { g_module = module; DisableThreadLibraryCalls(module); }
    return TRUE;
}
