/* Project Phoenix BC2 launcher (1.2; players and LAN games 1.6).
   One window to set everything up and start a game, like AltBotMod's launcher:
   - where the BC2 server is started from (BF-Portable's BFBC2.bat or the server exe) and the game client, with their
     arguments - remembered in launcher.ini next to this exe;
   - the bot settings - written to phoenix.ini (comments and other keys are kept);
   - Start All: the backend programs first (XAMPP's MySQL / Apache, the redirector / emulator - up to three, each with a
     wait), then the server, then the bots (Start-Bots.bat waits for the server and attaches), then the game after a delay;
   - Stop All: bots, server and the backend programs.
   Plain Win32, no installer, nothing written outside this folder. */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define FD_SETSIZE 1024   /* LAN scan: a few hundred probes at once */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wincrypt.h>
#include <commdlg.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <wctype.h>
#include <stdint.h>

#define SERVER_EXE L"Frost.Game.Main_Win32_Final.exe"
enum {
    ID_SRV = 100, ID_SRVBR, ID_SRVARG, ID_GAME, ID_GAMEBR, ID_GAMEARG, ID_AUTOBOTS, ID_AUTOGAME, ID_DELAY,
    ID_BOTS, ID_DIFF, ID_AIM, ID_RESPAWN, ID_VEH, ID_HELI, ID_DMG, ID_LEARN, ID_EQUIP, ID_GREN, ID_REVIVE, ID_EMPL, ID_BOATS, ID_LHUM, ID_RUSHOBJ, ID_KEEP,
    ID_STARTALL, ID_STARTSRV, ID_STARTBOTS, ID_STOPBOTS, ID_GAMEBTN, ID_SAVE, ID_LOGS, ID_STATUS, ID_STOPALL, ID_FINDXAMPP, ID_AUTOSVC, ID_MAPS, ID_VIEWER, ID_PLAYERS, ID_LAN,
    ID_SVC = 300,   /* + 10*i: path, browse, args, wait */
    ID_TIMER = 900
};
static HWND g_wnd, g_status; static HFONT g_font, g_bold;
static WCHAR g_dir[MAX_PATH], g_launcherIni[MAX_PATH], g_phoenixIni[MAX_PATH];
static int g_waitGame; static DWORD g_serverSeenAt;
#define NSVC 3
static int g_seq = -1; static DWORD g_seqAt;   /* Start All sequence: 0..NSVC-1 services, NSVC server, NSVC+1 bots */

static void status(const WCHAR *fmt, ...)
{
    WCHAR b[512]; va_list ap; va_start(ap, fmt); _vsnwprintf(b, 511, fmt, ap); va_end(ap); b[511] = 0;
    SetWindowTextW(g_status, b);
}
static int server_running(void)
{
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); if (s == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W e; e.dwSize = sizeof e; int found = 0;
    if (Process32FirstW(s, &e)) do { if (!_wcsicmp(e.szExeFile, SERVER_EXE)) { found = 1; break; } } while (Process32NextW(s, &e));
    CloseHandle(s); return found;
}
static int process_running(const WCHAR *exe)
{
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); if (s == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W e; e.dwSize = sizeof e; int found = 0;
    if (Process32FirstW(s, &e)) do { if (!_wcsicmp(e.szExeFile, exe)) { found = 1; break; } } while (Process32NextW(s, &e));
    CloseHandle(s); return found;
}
static int process_kill(const WCHAR *exe)
{
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); if (s == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W e; e.dwSize = sizeof e; int n = 0;
    if (Process32FirstW(s, &e)) do { if (!_wcsicmp(e.szExeFile, exe)) { HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, e.th32ProcessID); if (h) { if (TerminateProcess(h, 0)) n++; CloseHandle(h); } } } while (Process32NextW(s, &e));
    CloseHandle(s); return n;
}
static const WCHAR *base_name(const WCHAR *p) { const WCHAR *b = wcsrchr(p, L'\\'); return b ? b + 1 : p; }
static int is_exe(const WCHAR *p) { size_t n = wcslen(p); return n > 4 && !_wcsicmp(p + n - 4, L".exe"); }
static void get_text(int id, WCHAR *out, int n) { GetWindowTextW(GetDlgItem(g_wnd, id), out, n); }
static void set_text(int id, const WCHAR *t) { SetWindowTextW(GetDlgItem(g_wnd, id), t); }
static int get_check(int id) { return SendMessageW(GetDlgItem(g_wnd, id), BM_GETCHECK, 0, 0) == BST_CHECKED; }
static void set_check(int id, int on) { SendMessageW(GetDlgItem(g_wnd, id), BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0); }
static int get_combo(int id) { return (int)SendMessageW(GetDlgItem(g_wnd, id), CB_GETCURSEL, 0, 0); }
static void set_combo(int id, int i) { SendMessageW(GetDlgItem(g_wnd, id), CB_SETCURSEL, i, 0); }

static HWND add(const WCHAR *cls, const WCHAR *text, DWORD style, int x, int y, int w, int h, int id)
{
    HWND c = CreateWindowExW(!wcscmp(cls, L"EDIT") ? WS_EX_CLIENTEDGE : 0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, g_wnd, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE); return c;
}
static void label(const WCHAR *t, int x, int y, int w) { add(L"STATIC", t, 0, x, y + 3, w, 20, -1); }
static void heading(const WCHAR *t, int x, int y) { HWND c = add(L"STATIC", t, 0, x, y, 400, 20, -1); SendMessageW(c, WM_SETFONT, (WPARAM)g_bold, TRUE); }
static void combo(int id, int x, int y, int w, const WCHAR **items, int n)
{
    HWND c = add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, x, y, w, 200, id);
    for (int i = 0; i < n; i++) SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)items[i]);
}

/* ---- settings ---- */
static void ini_get(const WCHAR *file, const WCHAR *sec, const WCHAR *key, const WCHAR *def, WCHAR *out, int n) { GetPrivateProfileStringW(sec, key, def, out, n, file); }
static void load(void)
{
    WCHAR v[MAX_PATH];
    ini_get(g_launcherIni, L"Launcher", L"ServerPath", L"", v, MAX_PATH); set_text(ID_SRV, v);
    ini_get(g_launcherIni, L"Launcher", L"ServerArgs", L"", v, MAX_PATH); set_text(ID_SRVARG, v);
    ini_get(g_launcherIni, L"Launcher", L"GamePath", L"", v, MAX_PATH); set_text(ID_GAME, v);
    ini_get(g_launcherIni, L"Launcher", L"GameArgs", L"", v, MAX_PATH); set_text(ID_GAMEARG, v);
    ini_get(g_launcherIni, L"Launcher", L"AutoBots", L"1", v, 8); set_check(ID_AUTOBOTS, _wtoi(v));
    ini_get(g_launcherIni, L"Launcher", L"AutoGame", L"1", v, 8); set_check(ID_AUTOGAME, _wtoi(v));
    ini_get(g_launcherIni, L"Launcher", L"GameDelay", L"20", v, 16); set_text(ID_DELAY, v);
    for (int i = 0; i < NSVC; i++) { WCHAR k[32];
        swprintf(k, 32, L"Service%dPath", i + 1); ini_get(g_launcherIni, L"Launcher", k, L"", v, MAX_PATH); set_text(ID_SVC + 10 * i, v);
        swprintf(k, 32, L"Service%dArgs", i + 1); ini_get(g_launcherIni, L"Launcher", k, L"", v, MAX_PATH); set_text(ID_SVC + 10 * i + 2, v);
        swprintf(k, 32, L"Service%dWait", i + 1); ini_get(g_launcherIni, L"Launcher", k, L"5", v, 16); set_text(ID_SVC + 10 * i + 3, v); }
    ini_get(g_launcherIni, L"Launcher", L"AutoServices", L"1", v, 8); set_check(ID_AUTOSVC, _wtoi(v));
    ini_get(g_phoenixIni, L"Bots", L"BotsPerTeam", L"15", v, 16); set_text(ID_BOTS, v);
    ini_get(g_phoenixIni, L"Bots", L"Difficulty", L"2", v, 8); set_combo(ID_DIFF, max(0, min(2, _wtoi(v) - 1)));
    ini_get(g_phoenixIni, L"Bots", L"AimError", L"1.0", v, 16); set_text(ID_AIM, v);
    ini_get(g_phoenixIni, L"Bots", L"RespawnDelay", L"6", v, 16); set_text(ID_RESPAWN, v);
    ini_get(g_phoenixIni, L"Bots", L"Grenades", L"1", v, 8); set_check(ID_GREN, _wtoi(v));
    ini_get(g_phoenixIni, L"Behaviour", L"Vehicles", L"2", v, 8); set_combo(ID_VEH, max(0, min(2, _wtoi(v))));
    ini_get(g_phoenixIni, L"Behaviour", L"Helicopters", L"1", v, 8); set_check(ID_HELI, _wtoi(v));
    ini_get(g_phoenixIni, L"Behaviour", L"BotDamage", L"1.2", v, 16); set_text(ID_DMG, v);
    ini_get(g_phoenixIni, L"Behaviour", L"Learning", L"1", v, 8); set_check(ID_LEARN, _wtoi(v));
    ini_get(g_phoenixIni, L"Behaviour", L"Equipment", L"1", v, 8); set_check(ID_EQUIP, _wtoi(v));
    ini_get(g_phoenixIni, L"Behaviour", L"Revive", L"1", v, 8); set_check(ID_REVIVE, _wtoi(v));
    ini_get(g_phoenixIni, L"Behaviour", L"StationaryWeapons", L"1", v, 8); set_check(ID_EMPL, _wtoi(v));
    ini_get(g_phoenixIni, L"Behaviour", L"Boats", L"1", v, 8); set_check(ID_BOATS, _wtoi(v));
    ini_get(g_phoenixIni, L"Behaviour", L"LearnFromHumans", L"1", v, 8); set_check(ID_LHUM, _wtoi(v));
    ini_get(g_phoenixIni, L"Behaviour", L"RushObjectives", L"1", v, 8); set_check(ID_RUSHOBJ, _wtoi(v));
    ini_get(g_phoenixIni, L"Experimental", L"KeepBotPlayers", L"1", v, 8); set_check(ID_KEEP, _wtoi(v));
}
static void put(const WCHAR *file, const WCHAR *sec, const WCHAR *key, const WCHAR *val) { WritePrivateProfileStringW(sec, key, val, file); }
static void puti(const WCHAR *file, const WCHAR *sec, const WCHAR *key, int v) { WCHAR b[16]; swprintf(b, 16, L"%d", v); put(file, sec, key, b); }
static void save(void)
{
    WCHAR v[MAX_PATH];
    get_text(ID_SRV, v, MAX_PATH); put(g_launcherIni, L"Launcher", L"ServerPath", v);
    get_text(ID_SRVARG, v, MAX_PATH); put(g_launcherIni, L"Launcher", L"ServerArgs", v);
    get_text(ID_GAME, v, MAX_PATH); put(g_launcherIni, L"Launcher", L"GamePath", v);
    get_text(ID_GAMEARG, v, MAX_PATH); put(g_launcherIni, L"Launcher", L"GameArgs", v);
    puti(g_launcherIni, L"Launcher", L"AutoBots", get_check(ID_AUTOBOTS));
    puti(g_launcherIni, L"Launcher", L"AutoGame", get_check(ID_AUTOGAME));
    get_text(ID_DELAY, v, 16); puti(g_launcherIni, L"Launcher", L"GameDelay", max(0, _wtoi(v)));
    for (int i = 0; i < NSVC; i++) { WCHAR k[32];
        swprintf(k, 32, L"Service%dPath", i + 1); get_text(ID_SVC + 10 * i, v, MAX_PATH); put(g_launcherIni, L"Launcher", k, v);
        swprintf(k, 32, L"Service%dArgs", i + 1); get_text(ID_SVC + 10 * i + 2, v, MAX_PATH); put(g_launcherIni, L"Launcher", k, v);
        swprintf(k, 32, L"Service%dWait", i + 1); get_text(ID_SVC + 10 * i + 3, v, 16); puti(g_launcherIni, L"Launcher", k, max(0, _wtoi(v))); }
    puti(g_launcherIni, L"Launcher", L"AutoServices", get_check(ID_AUTOSVC));
    get_text(ID_BOTS, v, 16); puti(g_phoenixIni, L"Bots", L"BotsPerTeam", max(0, min(22, _wtoi(v))));
    puti(g_phoenixIni, L"Bots", L"Difficulty", get_combo(ID_DIFF) + 1);
    get_text(ID_AIM, v, 16); put(g_phoenixIni, L"Bots", L"AimError", v[0] ? v : L"1.0");
    get_text(ID_RESPAWN, v, 16); put(g_phoenixIni, L"Bots", L"RespawnDelay", v[0] ? v : L"6");
    puti(g_phoenixIni, L"Bots", L"Grenades", get_check(ID_GREN));
    puti(g_phoenixIni, L"Behaviour", L"Vehicles", get_combo(ID_VEH));
    puti(g_phoenixIni, L"Behaviour", L"Helicopters", get_check(ID_HELI));
    get_text(ID_DMG, v, 16); put(g_phoenixIni, L"Behaviour", L"BotDamage", v[0] ? v : L"1.2");
    puti(g_phoenixIni, L"Behaviour", L"Learning", get_check(ID_LEARN));
    puti(g_phoenixIni, L"Behaviour", L"Equipment", get_check(ID_EQUIP));
    puti(g_phoenixIni, L"Behaviour", L"Revive", get_check(ID_REVIVE));
    puti(g_phoenixIni, L"Behaviour", L"StationaryWeapons", get_check(ID_EMPL));
    puti(g_phoenixIni, L"Behaviour", L"Boats", get_check(ID_BOATS));
    puti(g_phoenixIni, L"Behaviour", L"LearnFromHumans", get_check(ID_LHUM));
    puti(g_phoenixIni, L"Behaviour", L"RushObjectives", get_check(ID_RUSHOBJ));
    puti(g_phoenixIni, L"Experimental", L"KeepBotPlayers", get_check(ID_KEEP));
    status(L"Saved. Bot settings take effect the next time bots are started (restart the server first if bots are running).");
}

/* ---- actions ---- */
static int run(const WCHAR *file, const WCHAR *args)
{
    if (!file[0]) return 0;
    WCHAR dir[MAX_PATH]; wcsncpy(dir, file, MAX_PATH - 1); dir[MAX_PATH - 1] = 0;
    WCHAR *slash = wcsrchr(dir, L'\\'); if (slash) *slash = 0; else dir[0] = 0;
    HINSTANCE h = ShellExecuteW(g_wnd, L"open", file, args && args[0] ? args : NULL, dir[0] ? dir : NULL, SW_SHOWNORMAL);
    return (INT_PTR)h > 32;
}
static int run_here(const WCHAR *name, const WCHAR *args)
{
    WCHAR p[MAX_PATH]; swprintf(p, MAX_PATH, L"%ls\\%ls", g_dir, name);
    return (INT_PTR)ShellExecuteW(g_wnd, L"open", p, args, g_dir, SW_SHOWNORMAL) > 32;
}
static void start_server(void)
{
    WCHAR f[MAX_PATH], a[MAX_PATH]; get_text(ID_SRV, f, MAX_PATH); get_text(ID_SRVARG, a, MAX_PATH);
    if (server_running()) { status(L"The server is already running."); return; }
    if (!f[0]) { status(L"Set the server path first (BF-Portable's BFBC2.bat or the server .exe)."); return; }
    status(run(f, a) ? L"Server starting..." : L"Could not start the server - check the path.");
}
static void start_bots(void) { status(run_here(L"Start-Bots.bat", NULL) ? L"Start-Bots.bat is running: it waits for the server, then adds the bots." : L"Start-Bots.bat not found next to the launcher."); }
static void stop_bots(void) { status(run_here(L"Stop-Bots.bat", NULL) ? L"Removing the bots..." : L"Stop-Bots.bat not found next to the launcher."); }
static void start_game(void)
{
    WCHAR f[MAX_PATH], a[MAX_PATH]; get_text(ID_GAME, f, MAX_PATH); get_text(ID_GAMEARG, a, MAX_PATH);
    if (!f[0]) { status(L"Set the game path first (BFBC2Game.exe or your BF-Portable game launcher)."); return; }
    status(run(f, a) ? L"Game starting..." : L"Could not start the game - check the path.");
}
/* a backend program: skipped if its .exe already runs (XAMPP's mysqld / httpd stay up between games) */
static int start_service(int i)
{
    WCHAR f[MAX_PATH], a[MAX_PATH]; get_text(ID_SVC + 10 * i, f, MAX_PATH); get_text(ID_SVC + 10 * i + 2, a, MAX_PATH);
    if (!f[0]) return 0;
    if (is_exe(f) && process_running(base_name(f))) { status(L"%ls is already running.", base_name(f)); return 0; }
    if (!_wcsicmp(base_name(f), L"mysql_start.bat") && process_running(L"mysqld.exe")) { status(L"MySQL is already running."); return 0; }
    if (!_wcsicmp(base_name(f), L"apache_start.bat") && process_running(L"httpd.exe")) { status(L"Apache is already running."); return 0; }
    if (run(f, a)) { status(L"Started %ls", base_name(f)); return 1; }
    status(L"Could not start %ls - check the path.", f); return 0;
}
static void start_all(void)
{
    save();
    g_seq = get_check(ID_AUTOSVC) ? 0 : NSVC; g_seqAt = 0;
    if (get_check(ID_AUTOGAME)) { g_waitGame = 1; g_serverSeenAt = 0; }
}
/* one step of Start All per timer tick: each backend program, its wait, then server, bots, game */
static void seq_step(void)
{
    if (g_seq < 0) return;
    DWORD nowt = GetTickCount();
    if (g_seqAt && nowt < g_seqAt) return;
    if (g_seq < NSVC) {
        int i = g_seq++; WCHAR w[16]; get_text(ID_SVC + 10 * i + 3, w, 16);
        int started = start_service(i);
        g_seqAt = started ? nowt + (DWORD)max(0, _wtoi(w)) * 1000 : 0;
        return;
    }
    if (g_seq == NSVC) { g_seq++; start_server(); g_seqAt = nowt + 1500; return; }
    if (g_seq == NSVC + 1) { g_seq = -1; if (get_check(ID_AUTOBOTS)) start_bots();
        if (g_waitGame) status(L"Waiting for the server before starting the game..."); }
}
static void stop_all(void)
{
    g_seq = -1; g_waitGame = 0;
    stop_bots();
    int n = process_kill(SERVER_EXE);
    int sv = 0;
    for (int i = 0; i < NSVC; i++) { WCHAR f[MAX_PATH]; get_text(ID_SVC + 10 * i, f, MAX_PATH); if (f[0] && is_exe(f)) sv += process_kill(base_name(f)); }
    /* 1.6.1: MySQL is shut down properly with XAMPP's own xampp_stop.exe (killing mysqld can damage the database) */
    WCHAR xs[MAX_PATH] = L""; int xamppStopped = 0;
    for (int i = 0; i < NSVC && !xs[0]; i++) {
        WCHAR f[MAX_PATH]; get_text(ID_SVC + 10 * i, f, MAX_PATH); if (!f[0]) continue;
        WCHAR d[MAX_PATH]; wcscpy(d, f); WCHAR *sl = wcsrchr(d, L'\\'); if (!sl) continue; *sl = 0;
        for (int up = 0; up < 3 && !xs[0]; up++) {   /* ...\xampp\mysql_start.bat, ...\BFBC2-EMU\mase_bc2.exe -> ...\xampp */
            WCHAR c[MAX_PATH]; swprintf(c, MAX_PATH, L"%ls\\xampp_stop.exe", d); if (GetFileAttributesW(c) != INVALID_FILE_ATTRIBUTES) { wcscpy(xs, c); break; }
            swprintf(c, MAX_PATH, L"%ls\\xampp\\xampp_stop.exe", d); if (GetFileAttributesW(c) != INVALID_FILE_ATTRIBUTES) { wcscpy(xs, c); break; }
            sl = wcsrchr(d, L'\\'); if (!sl) break; *sl = 0;
        }
    }
    if (xs[0] && (process_running(L"mysqld.exe") || process_running(L"httpd.exe"))) {
        WCHAR d[MAX_PATH]; wcscpy(d, xs); WCHAR *sl = wcsrchr(d, L'\\'); if (sl) *sl = 0;
        xamppStopped = (INT_PTR)ShellExecuteW(g_wnd, L"open", xs, NULL, d, SW_SHOWMINNOACTIVE) > 32;
    }
    status(L"Stopped: bots, %ls%ls%ls", n ? L"the server" : L"no server was running", sv ? L", the backend programs" : L"",
           xamppStopped ? L", and XAMPP's MySQL / Apache (xampp_stop.exe, shut down safely)." : xs[0] ? L". (MySQL / Apache were not running.)" : L". (xampp_stop.exe not found - stop MySQL in the XAMPP Control Panel.)");
}
/* BF-Portable: XAMPP's MySQL, the redirector and the BC2 emulator, and the BC2 server - all in one folder */
static int find_bfportable(int quiet)
{
    static const WCHAR *roots[] = {L"D:\\BF-Portable", L"C:\\BF-Portable", L"E:\\BF-Portable"};
    for (unsigned r = 0; r < sizeof roots / sizeof *roots; r++) {
        WCHAR p[MAX_PATH]; swprintf(p, MAX_PATH, L"%ls\\BFBC2-EMU\\mase_bc2.exe", roots[r]);
        if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) continue;
        swprintf(p, MAX_PATH, L"%ls\\xampp\\mysql_start.bat", roots[r]); set_text(ID_SVC, p); set_text(ID_SVC + 3, L"6");
        swprintf(p, MAX_PATH, L"%ls\\Redirector\\Redirector.cmd", roots[r]); set_text(ID_SVC + 10, p); set_text(ID_SVC + 13, L"2");
        swprintf(p, MAX_PATH, L"%ls\\BFBC2-EMU\\mase_bc2.exe", roots[r]); set_text(ID_SVC + 20, p); set_text(ID_SVC + 23, L"5");
        WCHAR cur[MAX_PATH]; get_text(ID_SRV, cur, MAX_PATH);
        swprintf(p, MAX_PATH, L"%ls\\Battlefield_bc2_Server\\BFBC2.bat", roots[r]);
        if (!cur[0] && GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) set_text(ID_SRV, p);
        set_check(ID_AUTOSVC, 1);
        if (!quiet) status(L"Found BF-Portable in %ls: MySQL, the redirector and the BC2 emulator filled in. Set the game path, then Save.", roots[r]);
        return 1;
    }
    return 0;
}
static void find_xampp(void)
{
    if (find_bfportable(0)) return;
    static const WCHAR *roots[] = {L"C:\\xampp", L"D:\\xampp", L"C:\\Program Files\\xampp", L"C:\\BF-Portable\\xampp"};
    for (unsigned r = 0; r < sizeof roots / sizeof *roots; r++) {
        WCHAR p[MAX_PATH]; swprintf(p, MAX_PATH, L"%ls\\mysql_start.bat", roots[r]);
        if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) continue;
        set_text(ID_SVC, p); set_text(ID_SVC + 3, L"5");
        swprintf(p, MAX_PATH, L"%ls\\apache_start.bat", roots[r]);
        if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) { set_text(ID_SVC + 10, p); set_text(ID_SVC + 13, L"3"); }
        status(L"Found XAMPP in %ls - MySQL and Apache filled in. Add the redirector / emulator as the third program.", roots[r]);
        return;
    }
    status(L"XAMPP not found in the usual places - use Browse to pick mysql_start.bat (or xampp-control.exe) in your XAMPP folder.");
}
static void browse(int id, const WCHAR *title)
{
    WCHAR f[MAX_PATH]; get_text(id, f, MAX_PATH);
    OPENFILENAMEW o; ZeroMemory(&o, sizeof o); o.lStructSize = sizeof o; o.hwndOwner = g_wnd;
    o.lpstrFilter = L"Programs and scripts (*.exe;*.bat;*.cmd;*.lnk)\0*.exe;*.bat;*.cmd;*.lnk\0All files\0*.*\0";
    o.lpstrFile = f; o.nMaxFile = MAX_PATH; o.lpstrTitle = title; o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&o)) set_text(id, f);
}

static void build(void)
{
    int x = 16, y = 12, W = 600;
    heading(L"Game and server", x, y); y += 26;
    label(L"Server start (BFBC2.bat or server .exe)", x, y, 260); add(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, x + 265, y, 260, 23, ID_SRV);
    add(L"BUTTON", L"Browse...", BS_PUSHBUTTON | WS_TABSTOP, x + 530, y, 70, 23, ID_SRVBR); y += 28;
    label(L"Server arguments", x, y, 260); add(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, x + 265, y, 335, 23, ID_SRVARG); y += 28;
    label(L"Game (BFBC2Game.exe or game launcher)", x, y, 260); add(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, x + 265, y, 260, 23, ID_GAME);
    add(L"BUTTON", L"Browse...", BS_PUSHBUTTON | WS_TABSTOP, x + 530, y, 70, 23, ID_GAMEBR); y += 28;
    label(L"Game arguments", x, y, 260); add(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, x + 265, y, 335, 23, ID_GAMEARG); y += 30;
    add(L"BUTTON", L"Start All also adds the bots", BS_AUTOCHECKBOX | WS_TABSTOP, x, y, 240, 22, ID_AUTOBOTS);
    add(L"BUTTON", L"and starts the game", BS_AUTOCHECKBOX | WS_TABSTOP, x + 250, y, 160, 22, ID_AUTOGAME);
    label(L"seconds after the server:", x + 415, y, 140); add(L"EDIT", L"20", ES_NUMBER | WS_TABSTOP, x + 555, y, 45, 22, ID_DELAY); y += 38;

    heading(L"Backend (started first by Start All, in this order)", x, y); y += 26;
    static const WCHAR *svcLabel[NSVC] = {L"1 (e.g. XAMPP mysql_start.bat)", L"2 (e.g. XAMPP apache_start.bat)", L"3 (e.g. redirector / emulator)"};
    for (int i = 0; i < NSVC; i++) {
        label(svcLabel[i], x, y, 200);
        add(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, x + 200, y, 205, 23, ID_SVC + 10 * i);
        add(L"BUTTON", L"...", BS_PUSHBUTTON | WS_TABSTOP, x + 408, y, 26, 23, ID_SVC + 10 * i + 1);
        add(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, x + 438, y, 100, 23, ID_SVC + 10 * i + 2);
        label(L"wait s", x + 543, y, 40); add(L"EDIT", L"5", ES_NUMBER | WS_TABSTOP, x + 580, y, 20, 23, ID_SVC + 10 * i + 3);
        y += 28;
    }
    add(L"BUTTON", L"Start All starts these first (skipped if already running)", BS_AUTOCHECKBOX | WS_TABSTOP, x, y, 360, 22, ID_AUTOSVC);
    add(L"BUTTON", L"Find BF-Portable", BS_PUSHBUTTON | WS_TABSTOP, x + 480, y, 120, 23, ID_FINDXAMPP); y += 38;

    heading(L"Bots", x, y); y += 26;
    static const WCHAR *diff[] = {L"Easy", L"Normal", L"Hard"}, *veh[] = {L"Never", L"Ride as passengers", L"Drive, fly and ride"};
    label(L"Bots per team", x, y, 120); add(L"EDIT", L"15", ES_NUMBER | WS_TABSTOP, x + 125, y, 50, 23, ID_BOTS);
    label(L"Difficulty", x + 200, y, 70); combo(ID_DIFF, x + 270, y, 110, diff, 3);
    label(L"Aim error (lower = sharper)", x + 395, y, 160); add(L"EDIT", L"1.0", ES_AUTOHSCROLL | WS_TABSTOP, x + 555, y, 45, 23, ID_AIM); y += 30;
    label(L"Respawn delay (s)", x, y, 120); add(L"EDIT", L"6", ES_AUTOHSCROLL | WS_TABSTOP, x + 125, y, 50, 23, ID_RESPAWN);
    label(L"Vehicles", x + 200, y, 70); combo(ID_VEH, x + 270, y, 160, veh, 3);
    label(L"Bot damage", x + 445, y, 100); add(L"EDIT", L"1.2", ES_AUTOHSCROLL | WS_TABSTOP, x + 555, y, 45, 23, ID_DMG); y += 30;
    add(L"BUTTON", L"Helicopters", BS_AUTOCHECKBOX | WS_TABSTOP, x, y, 110, 22, ID_HELI);
    add(L"BUTTON", L"Learning", BS_AUTOCHECKBOX | WS_TABSTOP, x + 115, y, 90, 22, ID_LEARN);
    add(L"BUTTON", L"Gadgets", BS_AUTOCHECKBOX | WS_TABSTOP, x + 210, y, 85, 22, ID_EQUIP);
    add(L"BUTTON", L"Grenades", BS_AUTOCHECKBOX | WS_TABSTOP, x + 300, y, 90, 22, ID_GREN);
    add(L"BUTTON", L"Medics revive", BS_AUTOCHECKBOX | WS_TABSTOP, x + 395, y, 120, 22, ID_REVIVE); y += 26;
    add(L"BUTTON", L"Stationary guns", BS_AUTOCHECKBOX | WS_TABSTOP, x, y, 125, 22, ID_EMPL);
    add(L"BUTTON", L"Boats", BS_AUTOCHECKBOX | WS_TABSTOP, x + 130, y, 70, 22, ID_BOATS);
    add(L"BUTTON", L"Learn from humans", BS_AUTOCHECKBOX | WS_TABSTOP, x + 205, y, 140, 22, ID_LHUM);
    add(L"BUTTON", L"Rush M-COMs", BS_AUTOCHECKBOX | WS_TABSTOP, x + 350, y, 110, 22, ID_RUSHOBJ);
    add(L"BUTTON", L"Keep bot players", BS_AUTOCHECKBOX | WS_TABSTOP, x + 465, y, 135, 22, ID_KEEP); y += 40;

    add(L"BUTTON", L"Stop All", BS_PUSHBUTTON | WS_TABSTOP, x + 314, y + 38, 85, 28, ID_STOPALL);
    add(L"BUTTON", L"Maps / Hardcore / Vietnam...", BS_PUSHBUTTON | WS_TABSTOP, x, y + 38, 200, 28, ID_MAPS);
    add(L"BUTTON", L"Bot learning...", BS_PUSHBUTTON | WS_TABSTOP, x + 206, y + 38, 102, 28, ID_VIEWER);
    add(L"BUTTON", L"Players...", BS_PUSHBUTTON | WS_TABSTOP, x + 407, y + 38, 85, 28, ID_PLAYERS);
    add(L"BUTTON", L"LAN games...", BS_PUSHBUTTON | WS_TABSTOP, x + 500, y + 38, 100, 28, ID_LAN);
    add(L"BUTTON", L"Start All", BS_DEFPUSHBUTTON | WS_TABSTOP, x, y, 110, 32, ID_STARTALL);
    add(L"BUTTON", L"Start Server", BS_PUSHBUTTON | WS_TABSTOP, x + 118, y, 95, 32, ID_STARTSRV);
    add(L"BUTTON", L"Start Bots", BS_PUSHBUTTON | WS_TABSTOP, x + 221, y, 85, 32, ID_STARTBOTS);
    add(L"BUTTON", L"Stop Bots", BS_PUSHBUTTON | WS_TABSTOP, x + 314, y, 85, 32, ID_STOPBOTS);
    add(L"BUTTON", L"Start Game", BS_PUSHBUTTON | WS_TABSTOP, x + 407, y, 85, 32, ID_GAMEBTN);
    add(L"BUTTON", L"Save", BS_PUSHBUTTON | WS_TABSTOP, x + 500, y, 48, 32, ID_SAVE);
    add(L"BUTTON", L"Logs", BS_PUSHBUTTON | WS_TABSTOP, x + 552, y, 48, 32, ID_LOGS); y += 76;
    g_status = add(L"STATIC", L"", SS_LEFT, x, y, W, 36, ID_STATUS);
}


/* ---------------------------------------------------------------- map list maker (1.4)
   Writes the server instance's maplist.txt (mode line, then "Levels/<map> <rounds>"), vars.hardCore in
   AdminScripts\Startup.txt, and GameModID (BC2 / VIETNAM) in ServerOptions.ini - each backed up once as *.phoenix.bak. */
typedef struct MapDef { const WCHAR *level, *name; int mode; int vietnam; } MapDef;
enum { MODE_CQ, MODE_RUSH, MODE_SQRUSH, MODE_SQDM, MODE_N };
static const WCHAR *kModeKey[MODE_N] = {L"CONQUEST", L"RUSH", L"SQRUSH", L"SQDM"};
static const WCHAR *kModeName[MODE_N] = {L"Conquest (bots play it fully)", L"Rush", L"Squad Rush", L"Squad Deathmatch"};
static const MapDef kMaps[] = {
    {L"MP_001", L"Panama Canal", MODE_CQ, 0}, {L"MP_003", L"Laguna Alta", MODE_CQ, 0}, {L"MP_005", L"Atacama Desert", MODE_CQ, 0},
    {L"MP_006CQ", L"Arica Harbor", MODE_CQ, 0}, {L"MP_007", L"White Pass", MODE_CQ, 0}, {L"MP_008CQ", L"Nelson Bay", MODE_CQ, 0},
    {L"MP_009CQ", L"Laguna Presa", MODE_CQ, 0}, {L"MP_012CQ", L"Port Valdez", MODE_CQ, 0}, {L"BC1_Oasis_CQ", L"Oasis", MODE_CQ, 0},
    {L"BC1_Harvest_Day_CQ", L"Harvest Day", MODE_CQ, 0}, {L"MP_SP_005CQ", L"Heavy Metal", MODE_CQ, 0},
    {L"MP_002", L"Valparaiso", MODE_RUSH, 0}, {L"MP_004", L"Isla Inocentes", MODE_RUSH, 0}, {L"MP_006", L"Arica Harbor", MODE_RUSH, 0},
    {L"MP_008", L"Nelson Bay", MODE_RUSH, 0}, {L"MP_005GR", L"Atacama Desert", MODE_RUSH, 0}, {L"MP_007GR", L"White Pass", MODE_RUSH, 0},
    {L"MP_009GR", L"Laguna Presa", MODE_RUSH, 0}, {L"MP_012GR", L"Port Valdez", MODE_RUSH, 0}, {L"BC1_Harvest_Day_GR", L"Harvest Day", MODE_RUSH, 0},
    {L"BC1_Oasis_GR", L"Oasis", MODE_RUSH, 0}, {L"MP_SP_002GR", L"Cold War", MODE_RUSH, 0},
    {L"MP_001SR", L"Panama Canal", MODE_SQRUSH, 0}, {L"MP_002SR", L"Valparaiso", MODE_SQRUSH, 0}, {L"MP_003SR", L"Laguna Alta", MODE_SQRUSH, 0},
    {L"MP_005SR", L"Atacama Desert", MODE_SQRUSH, 0}, {L"MP_009SR", L"Laguna Presa", MODE_SQRUSH, 0}, {L"MP_012SR", L"Port Valdez", MODE_SQRUSH, 0},
    {L"BC1_Harvest_Day_SR", L"Harvest Day", MODE_SQRUSH, 0}, {L"BC1_Oasis_SR", L"Oasis", MODE_SQRUSH, 0}, {L"MP_SP_002SR", L"Cold War", MODE_SQRUSH, 0},
    {L"MP_001SDM", L"Panama Canal", MODE_SQDM, 0}, {L"MP_004SDM", L"Isla Inocentes", MODE_SQDM, 0}, {L"MP_006SDM", L"Arica Harbor", MODE_SQDM, 0},
    {L"MP_007SDM", L"White Pass", MODE_SQDM, 0}, {L"MP_008SDM", L"Nelson Bay", MODE_SQDM, 0}, {L"MP_009SDM", L"Laguna Presa", MODE_SQDM, 0},
    {L"BC1_Harvest_Day_SDM", L"Harvest Day", MODE_SQDM, 0}, {L"BC1_Oasis_SDM", L"Oasis", MODE_SQDM, 0}, {L"MP_SP_002SDM", L"Cold War", MODE_SQDM, 0},
    {L"MP_SP_005SDM", L"Heavy Metal", MODE_SQDM, 0},
    {L"NAM_MP_002CQ", L"Vantage Point", MODE_CQ, 1}, {L"NAM_MP_003CQ", L"Hill 137", MODE_CQ, 1}, {L"NAM_MP_005CQ", L"Cao Son Temple", MODE_CQ, 1},
    {L"NAM_MP_006CQ", L"Phu Bai Valley", MODE_CQ, 1}, {L"NAM_MP_007CQ", L"Operation Hastings", MODE_CQ, 1},
};
#define NMAPS ((int)(sizeof kMaps / sizeof *kMaps))
enum { IDM_MODE = 1000, IDM_VIET, IDM_HC, IDM_ROUNDS, IDM_LIST, IDM_ALL, IDM_NONE, IDM_SAVE, IDM_CLOSE, IDM_STATUS, IDM_UP, IDM_DOWN };
static HWND g_maps; static int g_shown[NMAPS], g_nshown;

/* the server instance folder: -serverInstancePath from the server .bat, else Instance\ next to it */
static int instance_dir(WCHAR *out)
{
    WCHAR f[MAX_PATH]; get_text(ID_SRV, f, MAX_PATH);
    if (!f[0]) return 0;
    WCHAR dir[MAX_PATH]; wcscpy(dir, f); WCHAR *sl = wcsrchr(dir, L'\\'); if (sl) *sl = 0; else return 0;
    WCHAR inst[MAX_PATH] = L"Instance";
    FILE *fp = _wfopen(f, L"rb");
    if (fp) { char buf[4096]; size_t n = fread(buf, 1, sizeof buf - 1, fp); buf[n] = 0; fclose(fp);
        char *k = strstr(buf, "-serverInstancePath");
        if (k) { k += 19; while (*k == ' ' || *k == '"') k++; int i = 0; while (k[i] && k[i] != '"' && k[i] != ' ' && k[i] != '\r' && k[i] != '\n' && i < 200) { inst[i] = (WCHAR)(k[i] == '/' ? '\\' : k[i]); i++; }
            inst[i] = 0; if (i && inst[i - 1] == L'\\') inst[i - 1] = 0; } }
    swprintf(out, MAX_PATH, L"%ls\\%ls", dir, inst);
    return GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES;
}
static void backup_once(const WCHAR *path)
{
    WCHAR b[MAX_PATH]; swprintf(b, MAX_PATH, L"%ls.phoenix.bak", path);
    if (GetFileAttributesW(b) == INVALID_FILE_ATTRIBUTES) CopyFileW(path, b, TRUE);
    DWORD a = GetFileAttributesW(path); if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_READONLY)) SetFileAttributesW(path, a & ~FILE_ATTRIBUTE_READONLY);
}
/* replace (or add) a "key<sep>value" line in a small text file */
static int set_line(const WCHAR *path, const char *key, const char *line)
{
    char *buf = NULL; long n = 0; FILE *fp = _wfopen(path, L"rb");
    if (fp) { fseek(fp, 0, SEEK_END); n = ftell(fp); fseek(fp, 0, SEEK_SET); buf = malloc(n + 1); n = (long)fread(buf, 1, n, fp); buf[n] = 0; fclose(fp); }
    backup_once(path);
    FILE *o = _wfopen(path, L"wb"); if (!o) { free(buf); return 0; }
    int done = 0; char *p = buf;
    while (p && *p) {
        char *e = strchr(p, '\n'); size_t len = e ? (size_t)(e - p + 1) : strlen(p);
        char *q = p; while (*q == ' ' || *q == '\t') q++;
        if (!_strnicmp(q, key, strlen(key)) && (q[strlen(key)] == ' ' || q[strlen(key)] == '=' || q[strlen(key)] == '\t')) { if (!done) { fprintf(o, "%s\r\n", line); done = 1; } }
        else fwrite(p, 1, len, o);
        p += len;
    }
    if (!done) { if (n && buf[n - 1] != '\n') fputs("\r\n", o); fprintf(o, "%s\r\n", line); }
    fclose(o); free(buf); return 1;
}
static void maps_status(const WCHAR *t) { SetWindowTextW(GetDlgItem(g_maps, IDM_STATUS), t); }
static HWND madd(const WCHAR *cls, const WCHAR *text, DWORD style, int x, int y, int w, int h, int id)
{
    HWND c = CreateWindowExW(!wcscmp(cls, L"EDIT") || !wcscmp(cls, L"LISTBOX") ? WS_EX_CLIENTEDGE : 0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, g_maps, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE); return c;
}
/* fill the list for the chosen mode; tick the maps the current maplist.txt has */
static void maps_fill(void)
{
    HWND lb = GetDlgItem(g_maps, IDM_LIST);
    int viet = SendMessageW(GetDlgItem(g_maps, IDM_VIET), BM_GETCHECK, 0, 0) == BST_CHECKED;
    int mode = (int)SendMessageW(GetDlgItem(g_maps, IDM_MODE), CB_GETCURSEL, 0, 0); if (mode < 0) mode = 0;
    if (viet && mode != MODE_CQ) { mode = MODE_CQ; SendMessageW(GetDlgItem(g_maps, IDM_MODE), CB_SETCURSEL, 0, 0); maps_status(L"Vietnam: Conquest maps only in this list."); }
    SendMessageW(lb, LB_RESETCONTENT, 0, 0); g_nshown = 0;
    char cur[4096] = ""; WCHAR inst[MAX_PATH], ml[MAX_PATH];
    if (instance_dir(inst)) { swprintf(ml, MAX_PATH, L"%ls\\maplist.txt", inst); FILE *fp = _wfopen(ml, L"rb"); if (fp) { size_t n = fread(cur, 1, sizeof cur - 1, fp); cur[n] = 0; fclose(fp); }
        for (char *c2 = cur; *c2; c2++) { *c2 = (char)toupper((unsigned char)*c2); if (*c2 == '\t' || *c2 == '\r' || *c2 == '\n') *c2 = ' '; } }
    for (int i = 0; i < NMAPS; i++) {
        if (kMaps[i].mode != mode || kMaps[i].vietnam != viet) continue;
        WCHAR t[128]; swprintf(t, 128, L"%ls   (%ls)", kMaps[i].name, kMaps[i].level);
        int idx = (int)SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)t); g_shown[idx] = i; g_nshown++;
        char lv[64]; snprintf(lv, sizeof lv, "LEVELS/%ls ", kMaps[i].level);
        for (char *c2 = lv; *c2; c2++) *c2 = (char)toupper((unsigned char)*c2);
        if (strstr(cur, lv)) SendMessageW(lb, LB_SETSEL, TRUE, idx);
    }
}
static void maps_save(void)
{
    WCHAR inst[MAX_PATH]; if (!instance_dir(inst)) { maps_status(L"Set the server path (BFBC2.bat) in the launcher first - the map list goes into its Instance folder."); return; }
    HWND lb = GetDlgItem(g_maps, IDM_LIST);
    int mode = (int)SendMessageW(GetDlgItem(g_maps, IDM_MODE), CB_GETCURSEL, 0, 0); if (mode < 0) mode = 0;
    int viet = SendMessageW(GetDlgItem(g_maps, IDM_VIET), BM_GETCHECK, 0, 0) == BST_CHECKED;
    int hc = SendMessageW(GetDlgItem(g_maps, IDM_HC), BM_GETCHECK, 0, 0) == BST_CHECKED;
    WCHAR r[8]; GetWindowTextW(GetDlgItem(g_maps, IDM_ROUNDS), r, 8); int rounds = max(1, min(9, _wtoi(r)));
    int n = (int)SendMessageW(lb, LB_GETCOUNT, 0, 0), picked = 0;
    for (int i = 0; i < n; i++) if (SendMessageW(lb, LB_GETSEL, i, 0) > 0) picked++;
    if (!picked) { maps_status(L"Pick at least one map."); return; }
    WCHAR ml[MAX_PATH]; swprintf(ml, MAX_PATH, L"%ls\\maplist.txt", inst);
    backup_once(ml);
    FILE *o = _wfopen(ml, L"wb"); if (!o) { maps_status(L"Could not write maplist.txt (is the folder writable?)."); return; }
    fprintf(o, "%ls\r\n", kModeKey[mode]);
    for (int i = 0; i < n; i++) if (SendMessageW(lb, LB_GETSEL, i, 0) > 0) fprintf(o, "Levels/%ls %d\r\n", kMaps[g_shown[i]].level, rounds);
    fclose(o);
    WCHAR st[MAX_PATH]; swprintf(st, MAX_PATH, L"%ls\\AdminScripts\\Startup.txt", inst);
    set_line(st, "vars.hardCore", hc ? "vars.hardCore true" : "vars.hardCore false");
    WCHAR so[MAX_PATH]; swprintf(so, MAX_PATH, L"%ls\\ServerOptions.ini", inst);
    set_line(so, "GameModID", viet ? "GameModID = VIETNAM" : "GameModID = BC2");
    put(g_launcherIni, L"Maps", L"Mode", kModeKey[mode]); puti(g_launcherIni, L"Maps", L"Vietnam", viet); puti(g_launcherIni, L"Maps", L"Hardcore", hc); puti(g_launcherIni, L"Maps", L"Rounds", rounds);
    WCHAR msg[400]; swprintf(msg, 400, L"Saved %d map(s), %ls%ls%ls, %d round(s) each, into %ls. Restart the server to use it.", picked, kModeKey[mode], viet ? L", Vietnam" : L"", hc ? L", hardcore" : L"", rounds, inst);
    maps_status(msg); status(L"%ls", msg);
}
static LRESULT CALLBACK maps_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDM_MODE: if (HIWORD(w) == CBN_SELCHANGE) maps_fill(); break;
        case IDM_VIET: maps_fill(); break;
        case IDM_ALL: SendMessageW(GetDlgItem(h, IDM_LIST), LB_SETSEL, TRUE, -1); break;
        case IDM_NONE: SendMessageW(GetDlgItem(h, IDM_LIST), LB_SETSEL, FALSE, -1); break;
        case IDM_SAVE: maps_save(); break;
        case IDM_CLOSE: DestroyWindow(h); break;
        }
        return 0;
    case WM_CTLCOLORSTATIC: SetBkMode((HDC)w, TRANSPARENT); return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    case WM_DESTROY: g_maps = NULL; EnableWindow(g_wnd, TRUE); SetForegroundWindow(g_wnd); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static void open_maps(void)
{
    if (g_maps) { SetForegroundWindow(g_maps); return; }
    static int reg; if (!reg) { reg = 1; WNDCLASSW wc; ZeroMemory(&wc, sizeof wc); wc.lpfnWndProc = maps_proc; wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = L"PhoenixMaps"; wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE); wc.hCursor = LoadCursor(NULL, IDC_ARROW); RegisterClassW(&wc); }
    RECT r = {0, 0, 460, 470}; AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
    RECT pr; GetWindowRect(g_wnd, &pr);
    g_maps = CreateWindowExW(0, L"PhoenixMaps", L"Map list - Project Phoenix", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                             pr.left + 40, pr.top + 40, r.right - r.left, r.bottom - r.top, g_wnd, NULL, GetModuleHandleW(NULL), NULL);
    int x = 14, y = 12;
    HWND hd = madd(L"STATIC", L"Game mode", 0, x, y + 3, 80, 20, -1); (void)hd;
    HWND cb = madd(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, x + 85, y, 220, 200, IDM_MODE);
    for (int i = 0; i < MODE_N; i++) SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)kModeName[i]);
    madd(L"STATIC", L"Rounds per map", 0, x + 320, y + 3, 90, 20, -1); madd(L"EDIT", L"1", ES_NUMBER | WS_TABSTOP, x + 410, y, 30, 23, IDM_ROUNDS); y += 32;
    madd(L"BUTTON", L"Vietnam (its own maps; the server runs the VIETNAM mod)", BS_AUTOCHECKBOX | WS_TABSTOP, x, y, 430, 22, IDM_VIET); y += 26;
    madd(L"BUTTON", L"Hardcore (no HUD / minimap help, more damage)", BS_AUTOCHECKBOX | WS_TABSTOP, x, y, 430, 22, IDM_HC); y += 30;
    madd(L"LISTBOX", L"", LBS_MULTIPLESEL | LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP, x, y, 430, 270, IDM_LIST); y += 274;
    madd(L"BUTTON", L"All", BS_PUSHBUTTON | WS_TABSTOP, x, y, 60, 26, IDM_ALL);
    madd(L"BUTTON", L"None", BS_PUSHBUTTON | WS_TABSTOP, x + 66, y, 60, 26, IDM_NONE);
    madd(L"BUTTON", L"Save map list", BS_DEFPUSHBUTTON | WS_TABSTOP, x + 230, y, 120, 26, IDM_SAVE);
    madd(L"BUTTON", L"Close", BS_PUSHBUTTON | WS_TABSTOP, x + 356, y, 74, 26, IDM_CLOSE); y += 32;
    madd(L"STATIC", L"Click maps to pick them; they play in this order.", SS_LEFT, x, y, 430, 40, IDM_STATUS);
    /* last choices */
    WCHAR v[32]; int mode = 0;
    GetPrivateProfileStringW(L"Maps", L"Mode", L"CONQUEST", v, 32, g_launcherIni); for (int i = 0; i < MODE_N; i++) if (!_wcsicmp(v, kModeKey[i])) mode = i;
    SendMessageW(cb, CB_SETCURSEL, mode, 0);
    SendMessageW(GetDlgItem(g_maps, IDM_VIET), BM_SETCHECK, GetPrivateProfileIntW(L"Maps", L"Vietnam", 0, g_launcherIni) ? BST_CHECKED : BST_UNCHECKED, 0);
    WCHAR inst[MAX_PATH]; int hc = 0;
    if (instance_dir(inst)) { WCHAR st[MAX_PATH]; swprintf(st, MAX_PATH, L"%ls\\AdminScripts\\Startup.txt", inst); FILE *fp = _wfopen(st, L"rb");
        if (fp) { char b[4096]; size_t n = fread(b, 1, sizeof b - 1, fp); b[n] = 0; fclose(fp); char *k = strstr(b, "vars.hardCore"); if (k) { char *e = strchr(k, '\n'); if (e) *e = 0; if (strstr(k, "true") || strstr(k, "1")) hc = 1; } }
        WCHAR so[MAX_PATH]; swprintf(so, MAX_PATH, L"%ls\\ServerOptions.ini", inst); WCHAR gm[32]; GetPrivateProfileStringW(L"Options", L"GameModID", L"BC2", gm, 32, so);
        if (!_wcsicmp(gm, L"VIETNAM")) SendMessageW(GetDlgItem(g_maps, IDM_VIET), BM_SETCHECK, BST_CHECKED, 0); }
    SendMessageW(GetDlgItem(g_maps, IDM_HC), BM_SETCHECK, hc ? BST_CHECKED : BST_UNCHECKED, 0);
    WCHAR rr[8]; swprintf(rr, 8, L"%d", GetPrivateProfileIntW(L"Maps", L"Rounds", 1, g_launcherIni)); SetWindowTextW(GetDlgItem(g_maps, IDM_ROUNDS), rr);
    maps_fill();
    ShowWindow(g_maps, SW_SHOW);
}

#include "launcher_net.inc"

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case ID_SRVBR: browse(ID_SRV, L"Server: BFBC2.bat or Frost.Game.Main_Win32_Final.exe"); break;
        case ID_SVC + 1: case ID_SVC + 11: case ID_SVC + 21: browse(LOWORD(w) - 1, L"Backend program: mysql_start.bat, apache_start.bat, xampp-control.exe, the redirector / emulator"); break;
        case ID_GAMEBR: browse(ID_GAME, L"Game: BFBC2Game.exe or your game launcher"); break;
        case ID_STARTALL: start_all(); break;
        case ID_STOPALL: stop_all(); break;
        case ID_MAPS: open_maps(); break;
        case ID_PLAYERS: open_players(); break;
        case ID_LAN: open_lan(); break;
        case ID_VIEWER: { WCHAR p[MAX_PATH]; swprintf(p, MAX_PATH, L"%ls\\LearnViewer.html", g_dir);
            if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) status(L"LearnViewer.html is not next to the launcher.");
            else { ShellExecuteW(h, L"open", p, NULL, NULL, SW_SHOWNORMAL);
                /* 1.6.9: the shared navcache next to the server (or NavcacheDir= in phoenix.ini) */
                WCHAR nd[MAX_PATH]; ini_get(g_phoenixIni, L"Behaviour", L"NavcacheDir", L"", nd, MAX_PATH);
                if (!nd[0]) { get_text(ID_SRV, nd, MAX_PATH); WCHAR *sl = wcsrchr(nd, L'\\'); if (sl) { *sl = 0; wcsncat(nd, L"\\PhoenixNavcache", MAX_PATH - wcslen(nd) - 1); } else swprintf(nd, MAX_PATH, L"%ls\\navcache", g_dir); }
                status(L"The learning map opened in your browser: click Open navcache folder and pick %ls", nd); } break; }
        case ID_FINDXAMPP: find_xampp(); break;
        case ID_SVC: case ID_SVC + 10: case ID_SVC + 20: break;
        case ID_STARTSRV: save(); start_server(); break;
        case ID_STARTBOTS: save(); start_bots(); break;
        case ID_STOPBOTS: stop_bots(); break;
        case ID_GAMEBTN: save(); start_game(); break;
        case ID_SAVE: save(); break;
        case ID_LOGS: { WCHAR p[MAX_PATH]; swprintf(p, MAX_PATH, L"%ls\\reports", g_dir); CreateDirectoryW(p, NULL); ShellExecuteW(h, L"open", p, NULL, NULL, SW_SHOWNORMAL); break; }
        }
        return 0;
    case WM_TIMER: {
        seq_step();
        int up = server_running();
        if (g_waitGame) {
            WCHAR d[16]; get_text(ID_DELAY, d, 16); DWORD delay = (DWORD)max(0, _wtoi(d)) * 1000;
            if (up && !g_serverSeenAt) g_serverSeenAt = GetTickCount();
            if (g_serverSeenAt) {
                DWORD left = GetTickCount() - g_serverSeenAt >= delay ? 0 : delay - (GetTickCount() - g_serverSeenAt);
                if (!left) { g_waitGame = 0; start_game(); }
                else status(L"Server is up - starting the game in %lu s...", (unsigned long)(left + 999) / 1000);
            }
        }
        WCHAR t[128]; swprintf(t, 128, L"Project Phoenix BC2 Launcher - server %ls", up ? L"running" : L"not running"); SetWindowTextW(h, t);
        return 0; }
    case WM_CTLCOLORSTATIC: SetBkMode((HDC)w, TRANSPARENT); return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show)
{
    (void)prev; (void)cmd;
    GetModuleFileNameW(NULL, g_dir, MAX_PATH); WCHAR *s = wcsrchr(g_dir, L'\\'); if (s) *s = 0;
    /* the launcher may sit in the package folder or in bin\ */
    WCHAR probe[MAX_PATH]; swprintf(probe, MAX_PATH, L"%ls\\Start-Bots.bat", g_dir);
    if (GetFileAttributesW(probe) == INVALID_FILE_ATTRIBUTES) { WCHAR up[MAX_PATH]; wcscpy(up, g_dir); WCHAR *t = wcsrchr(up, L'\\');
        if (t) { *t = 0; swprintf(probe, MAX_PATH, L"%ls\\Start-Bots.bat", up); if (GetFileAttributesW(probe) != INVALID_FILE_ATTRIBUTES) wcscpy(g_dir, up); } }
    swprintf(g_launcherIni, MAX_PATH, L"%ls\\launcher.ini", g_dir);
    swprintf(g_phoenixIni, MAX_PATH, L"%ls\\phoenix.ini", g_dir);
    g_font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_bold = CreateFontW(-14, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    WNDCLASSW wc; ZeroMemory(&wc, sizeof wc); wc.lpfnWndProc = proc; wc.hInstance = inst; wc.lpszClassName = L"PhoenixLauncher";
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE); wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassW(&wc);
    RECT r = {0, 0, 632, 668}; AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    g_wnd = CreateWindowExW(0, L"PhoenixLauncher", L"Project Phoenix BC2 Launcher", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                            CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    { WSADATA wd; WSAStartup(MAKEWORD(2, 2), &wd); }
    build(); load();
    { WCHAR v[MAX_PATH]; get_text(ID_SVC, v, MAX_PATH); WCHAR v2[MAX_PATH]; get_text(ID_SVC + 20, v2, MAX_PATH);
      if (!v[0] && !v2[0]) find_bfportable(1); }   /* first run: fill in BF-Portable's backend */
    if (GetFileAttributesW(g_phoenixIni) == INVALID_FILE_ATTRIBUTES) status(L"phoenix.ini not found next to the launcher - Save creates one.");
    else status(L"Set the server and game paths once; they are kept in launcher.ini. Start All runs server, bots and game.");
    SetTimer(g_wnd, ID_TIMER, 500, NULL);
    ShowWindow(g_wnd, show); UpdateWindow(g_wnd);
    MSG msg; while (GetMessageW(&msg, NULL, 0, 0)) { if (!(g_maps && IsDialogMessageW(g_maps, &msg)) && !(g_players && IsDialogMessageW(g_players, &msg)) && !(g_lan && IsDialogMessageW(g_lan, &msg)) && !IsDialogMessageW(g_wnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
    return 0;
}
